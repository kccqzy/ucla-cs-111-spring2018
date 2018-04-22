extern crate libc;
extern crate nix;
extern crate termios;

use nix::Error;
use nix::errno::Errno;
use std::vec::Vec;
use std::net::{TcpListener, TcpStream};
use std::env;
use std::os::unix::process::ExitStatusExt;
use nix::sys::signal::Signal;
use std::ops::BitOr;
use std::process::{Command, Stdio};
use std::os::unix::prelude::*;
use std::process::exit;
use nix::poll::{poll, EventFlags, PollFd};
use nix::unistd::{close, read, write, Pid};
use nix::sys::signal::kill;
use termios::*;

const CR: u8 = 13;
const LF: u8 = 10;

pub struct RawTerminalRAII {
    pub original_termios: termios::Termios,
}

impl RawTerminalRAII {
    fn new() -> RawTerminalRAII {
        let fd = 0;
        let mut termios = Termios::from_fd(fd).unwrap();
        let orig = termios;
        termios.c_iflag = ISTRIP;
        termios.c_oflag = 0;
        termios.c_lflag = 0;
        tcsetattr(fd, TCSANOW, &termios).unwrap();
        RawTerminalRAII {
            original_termios: orig,
        }
    }
}

impl Drop for RawTerminalRAII {
    fn drop(&mut self) {
        match tcsetattr(0, TCSANOW, &self.original_termios) {
            Ok(_) => (),
            _ => exit(10),
        }
    }
}

enum Mode {
    Server,
    Client,
}
struct Args {
    mode: Mode,
    server_port: u16,
}

fn parse_args() -> Args {
    let mut default_args = Args {
        mode: Mode::Client,
        server_port: 5000,
    };
    for arg in env::args().skip(1) {
        if arg == "--server" {
            default_args.mode = Mode::Server
        } else if arg == "--client" {
            default_args.mode = Mode::Client
        } else {
            exit(1)
        }
    }
    default_args
}

#[derive(Debug, Copy, Clone)]
enum LineEndingTranslation {
    Identity,
    CRtoLF,   // (Client) From keyboard to network
    CRtoCRLF, // (Client) From keyboard to screen (echo)
    LFtoCRLF, // (Server) From shell to network
}

fn do_translate(buf: Vec<u8>, trans: LineEndingTranslation) -> Vec<u8> {
    match trans {
        LineEndingTranslation::Identity => buf,
        LineEndingTranslation::CRtoLF => buf.into_iter()
            .map(|c| if c == CR { LF } else { c })
            .collect(),
        LineEndingTranslation::LFtoCRLF => buf.into_iter()
            .flat_map(|c| {
                if c == LF {
                    Some(CR).into_iter().chain(Some(LF).into_iter())
                } else {
                    Some(c).into_iter().chain(None.into_iter())
                }
            })
            .collect(),
        LineEndingTranslation::CRtoCRLF => buf.into_iter()
            .flat_map(|c| {
                if c == CR {
                    Some(CR).into_iter().chain(Some(LF).into_iter())
                } else {
                    Some(c).into_iter().chain(None.into_iter())
                }
            })
            .collect(),
    }
}

pub struct WriterBuffer {
    buf: Vec<u8>,
}

impl WriterBuffer {
    fn new() -> WriterBuffer {
        WriterBuffer {
            buf: Vec::with_capacity(65536),
        }
    }
    fn push_into(&mut self, mut buf: Vec<u8>) {
        self.buf.append(&mut buf)
    }
    fn get_next(&self) -> Option<u8> {
        if self.buf.is_empty() {
            None
        } else {
            Some(self.buf[0])
        }
    }
    fn get_some(&self) -> &[u8] {
        self.buf.as_slice()
    }
    fn consume(&mut self, size: usize) {
        if size == self.buf.len() {
            self.buf.clear()
        } else {
            let extras = self.buf.split_off(size);
            self.buf = extras;
        }
    }
    fn has_content(&self) -> bool {
        !self.buf.is_empty()
    }
}

fn make_non_blocking(fd: RawFd) {
    unsafe {
        let flags = libc::fcntl(fd, libc::F_GETFL);
        libc::fcntl(fd, libc::F_SETFL, flags | libc::O_NONBLOCK);
    }
}

fn read_alot(from: RawFd) -> (Vec<u8>, bool) {
    let mut buf = Vec::with_capacity(65536);
    loop {
        let mut b = [0; 65536];
        match read(from, &mut b) {
            Ok(0) => {
                return (buf, false);
            }
            Ok(bytes_read) => {
                buf.extend_from_slice(&b[0..bytes_read]);
                continue;
            }
            Err(Error::Sys(Errno::EAGAIN)) => return (buf, true),
            Err(e) => panic!("unexpected error when reading: {:?}", e),
        }
    }
}

// Returns whether we can attempt another read; false means EOF
fn do_read(from: RawFd, to: &mut WriterBuffer, trans: LineEndingTranslation) -> bool {
    let (buf, rv) = read_alot(from);
    to.push_into(do_translate(buf, trans));
    rv
}

fn do_write(from: &mut WriterBuffer, to: RawFd) -> bool {
    loop {
        if from.get_some().is_empty() {
            return true;
        }
        match write(to, from.get_some()) {
            Ok(bytes_written) => {
                from.consume(bytes_written);
                continue;
            }
            Err(Error::Sys(Errno::EAGAIN)) => return true,
            Err(Error::Sys(Errno::EPIPE)) => return false,
            Err(e) => panic!("expected error when writing: {:?}", e),
        }
    }
}

fn write_all(fd: RawFd, buf: &[u8]) -> nix::Result<usize> {
    let mut written = 0;
    loop {
        written = write(fd, &buf[written..])?;
        if written == buf.len() {
            return Ok(written);
        }
    }
}

fn has_input(pfd: &PollFd) -> bool {
    pfd.revents()
        .map_or(false, |e| e.contains(EventFlags::POLLIN))
}

fn has_hup(pfd: &PollFd) -> bool {
    pfd.revents().map_or(false, |e| {
        e.intersects(EventFlags::POLLHUP.bitor(EventFlags::POLLERR))
    })
}

fn server_event_loop(mut sock_fd: RawFd) {
    let mut child = Command::new("/bin/bash")
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .unwrap();
    let pid = Pid::from_raw(child.id() as i32);

    let mut child_stdout_fd = child.stdout.as_ref().unwrap().as_raw_fd();
    let mut child_stderr_fd = child.stderr.as_ref().unwrap().as_raw_fd();
    let mut child_stdin_fd = child.stdin.as_ref().unwrap().as_raw_fd();

    make_non_blocking(child_stdout_fd);
    make_non_blocking(child_stderr_fd);
    make_non_blocking(child_stdin_fd);
    make_non_blocking(sock_fd);

    let mut child_stdin_buf = WriterBuffer::new();
    let mut socket_buf = WriterBuffer::new();

    let mut client_dead = false;

    loop {
        let mut poll_fds = [
            PollFd::new(child_stdout_fd, EventFlags::POLLIN),
            PollFd::new(child_stderr_fd, EventFlags::POLLIN),
            PollFd::new(
                sock_fd,
                if socket_buf.has_content() {
                    EventFlags::POLLIN.bitor(EventFlags::POLLOUT)
                } else {
                    EventFlags::POLLIN
                },
            ),
            PollFd::new(
                child_stdin_fd,
                if child_stdin_buf.has_content() {
                    EventFlags::POLLOUT
                } else {
                    EventFlags::empty()
                },
            ),
        ];

        poll(&mut poll_fds, -1).unwrap();

        // Handle writers first: previously this is because we use the same
        // condition that we used to specify the poll fd to test. Now we really
        // don't care.

        // Detect pipe closed
        if child.stdin.is_some() {
            // Special handling to detect the pipes are closed or not.
            let mut p = [PollFd::new(child_stdin_fd, EventFlags::POLLOUT)];
            poll(&mut p, 0).unwrap();
            if p[0].revents()
                .unwrap()
                .intersects(EventFlags::POLLERR.bitor(EventFlags::POLLNVAL))
            {
                child.stdin = None;
                child_stdin_fd = -1;
            }
        }

        if child.stdin.is_some() && child_stdin_buf.has_content() {
            match child_stdin_buf.get_next() {
                Some(3) => {
                    kill(pid, Some(Signal::SIGINT)).unwrap();
                    child_stdin_buf.consume(1);
                    continue;
                }
                Some(4) => {
                    child.stdin = None;
                    child_stdin_buf.consume(1);
                    continue;
                }
                _ => (),
            }
            if do_write(&mut child_stdin_buf, child_stdin_fd) == false {
                child.stdin = None;
                child_stdin_fd = -1;
            }
        }

        if !client_dead && socket_buf.has_content() {
            if do_write(&mut socket_buf, sock_fd) == false {
                // No more write to the socket; so the client does not want to receive any data
                // We also assume it does not want to send anything
                client_dead = true;
                child.stdin = None;
                child_stdin_fd = -1;
                close(sock_fd).unwrap();
                sock_fd = -1;
            }
        }

        // Done with writers, handle readers now
        if child.stdout.is_some() {
            if has_input(&poll_fds[0])
                && do_read(
                    child_stdout_fd,
                    &mut socket_buf,
                    LineEndingTranslation::LFtoCRLF,
                ) == false || has_hup(&poll_fds[0])
            {
                child.stdout = None;
                child_stdout_fd = -1;
            }
        }

        if child.stderr.is_some() {
            if has_input(&poll_fds[1])
                && do_read(
                    child_stderr_fd,
                    &mut socket_buf,
                    LineEndingTranslation::LFtoCRLF,
                ) == false || has_hup(&poll_fds[1])
            {
                child.stderr = None;
                child_stderr_fd = -1;
            }
        }

        if !client_dead {
            if has_input(&poll_fds[2]) {
                if do_read(
                    sock_fd,
                    &mut child_stdin_buf,
                    LineEndingTranslation::Identity,
                ) == false
                {
                    // No more read; so the client will not send any more data to us
                    client_dead = true;
                    child.stdin = None;
                    child_stdin_fd = -1;
                    close(sock_fd).unwrap();
                    sock_fd = -1;
                }
            }
        }

        // Shutdown handling: quit if the shell has died
        if child.stdout.is_none() && child.stderr.is_none() && child.stdin.is_none() {
            break;
        }

        // (non-) Shutdown handling: do not just quit merely because the client
        // has died because we still need to wait for the shell to die.
    }

    let status = child.wait().unwrap();
    eprintln!(
        "SHELL EXIT SIGNAL={} STATUS={}\r",
        status.signal().unwrap_or(0),
        status.code().unwrap_or(0)
    )
}

fn client_event_loop(sock_fd: RawFd) {
    make_non_blocking(0);
    make_non_blocking(sock_fd);
    // Special: stdout is blocking

    let mut socket_buf = WriterBuffer::new();

    loop {
        let mut poll_fds = [
            PollFd::new(0, EventFlags::POLLIN),
            PollFd::new(
                sock_fd,
                if socket_buf.has_content() {
                    EventFlags::POLLIN.bitor(EventFlags::POLLOUT)
                } else {
                    EventFlags::POLLIN
                },
            ),
        ];
        poll(&mut poll_fds, 50).unwrap();

        if socket_buf.has_content() {
            if do_write(&mut socket_buf, sock_fd) == false {
                // No more write to the socket, but the server couldn't have
                // just shutdown the read half, so the server must be dead.
                break;
            } else {
                continue;
            }
        }

        // Readers
        if has_input(&poll_fds[0]) {
            let (buf_ori, rv) = read_alot(0);
            let buf_ori_2 = buf_ori.clone();
            let buf_stdout = do_translate(buf_ori, LineEndingTranslation::CRtoCRLF);
            write_all(1, &buf_stdout).unwrap();
            socket_buf.push_into(do_translate(buf_ori_2, LineEndingTranslation::CRtoLF));
            if rv == false {
                panic!("unexpected inability to write to stdout");
            }
        } else if has_hup(&poll_fds[0]) {
            panic!("unexpected inability to write to stdout");
        }

        if has_input(&poll_fds[1]) {
            let (buf, rv) = read_alot(sock_fd);
            write_all(1, &buf).unwrap();
            if rv == false {
                break;
            }
        } else if has_hup(&poll_fds[0]) {
            break;
        }
    }
}

fn main() {
    let args = parse_args();
    match args.mode {
        Mode::Server => {
            let listener = TcpListener::bind(("127.0.0.1", args.server_port)).unwrap();
            server_event_loop(listener.incoming().next().unwrap().unwrap().as_raw_fd())
        }
        Mode::Client => {
            let _raw_terminal_raii = RawTerminalRAII::new();
            let stream = TcpStream::connect(("127.0.0.1", args.server_port)).unwrap();
            client_event_loop(stream.as_raw_fd())
        }
    }
}
