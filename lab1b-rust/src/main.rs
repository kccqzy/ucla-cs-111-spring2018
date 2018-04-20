extern crate nix;
extern crate termios;

use std::ascii::escape_default;
use std::collections::VecDeque;
use std::net::{TcpListener, TcpStream};
use std::env;
use std::os::unix::process::ExitStatusExt;
use nix::sys::signal::Signal;
use std::ops::BitOr;
use std::process::{Command, Stdio};
use std::fs::File;
use std::io::{Error, Read, Write};
use std::os::unix::prelude::*;
use std::process::exit;
use nix::poll::{poll, EventFlags, PollFd};
use nix::unistd::{Pid, dup2};
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
            _ => exit(1),
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

fn client_event_loop(socketstream: TcpStream, stdin: File, stdout: File) {
    unimplemented!()
}

pub struct WriterBuffer {
    buf: VecDeque<u8>,
}

impl WriterBuffer {
    fn new() -> WriterBuffer {
        WriterBuffer {
            buf: VecDeque::with_capacity(65536),
        }
    }
    fn push_into(&mut self, buf: &[u8]) {
        let mut b = buf.into_iter().cloned().collect(); // TODO move
        self.buf.append(&mut b)
    }
    fn get_next(&self) -> Option<u8> {
        self.buf.front().map(|c| *c)
    }
    fn get_some(&self) -> &[u8] {
        self.buf.as_slices().0
    }
    fn consume(&mut self, size: usize) {
        let extras = self.buf.split_off(size);
        self.buf = extras;
    }
    fn has_content(&self) -> bool {
        !self.buf.is_empty()
    }
}

fn server_event_loop(mut socketstream: TcpStream) {
    {
        let logger = File::create("/tmp/lab1b-server.log").unwrap();
        dup2(logger.as_raw_fd(), 2).unwrap();
        // Let logger go out of scope and be dropped
    }

    let mut child = Command::new("/bin/bash")
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .unwrap();
    let pid = Pid::from_raw(child.id() as i32);
    eprintln!("created child process with PID {:?}", pid);

    let child_stdout_fd = child.stdout.as_ref().unwrap().as_raw_fd();
    let child_stderr_fd = child.stderr.as_ref().unwrap().as_raw_fd();
    let child_stdin_fd = child.stdin.as_ref().unwrap().as_raw_fd();

    eprintln!("Current fds: ");
    eprintln!("  child_stdout_fd: {:?}", child_stdout_fd);
    eprintln!("  child_stderr_fd: {:?}", child_stderr_fd);
    eprintln!("  child_stdin_fd: {:?}", child_stdin_fd);
    eprintln!("  socketstream_fd: {:?}", socketstream.as_raw_fd());

    let mut poll_fds = [
        PollFd::new(child_stdout_fd, EventFlags::POLLIN),
        PollFd::new(child_stderr_fd, EventFlags::POLLIN),
        PollFd::new(child_stdin_fd, EventFlags::POLLOUT),
        PollFd::new(
            socketstream.as_raw_fd(),
            EventFlags::POLLIN.bitor(EventFlags::POLLOUT),
        ),
    ];

    fn has_input(pfd: &PollFd) -> bool {
        pfd.revents()
            .map_or(false, |e| e.contains(EventFlags::POLLIN))
    }
    fn can_output(pfd: &PollFd) -> bool {
        pfd.revents()
            .map_or(false, |e| e.contains(EventFlags::POLLOUT))
    }
    fn has_hup(pfd: &PollFd) -> bool {
        pfd.revents().map_or(false, |e| {
            e.intersects(EventFlags::POLLHUP.bitor(EventFlags::POLLERR))
        })
    }

    let mut child_stdin_buf = WriterBuffer::new();
    let mut socket_buf = WriterBuffer::new();

    // Returns whether we can attempt another read; false means EOF
    fn do_read<T>(from: &mut T, to: &mut WriterBuffer) -> bool
    where
        T: Read + AsRawFd,
    {
        eprintln!("will read from fd {:?}", from.as_raw_fd());
        let mut buf = [0; 65536];
        let bytes_read = from.read(&mut buf).unwrap();
        eprintln!(
            "read {:?} bytes from fd {:?}: {}",
            bytes_read,
            from.as_raw_fd(),
            String::from_utf8(
                buf[0..bytes_read]
                    .iter()
                    .cloned()
                    .flat_map(escape_default)
                    .collect()
            ).unwrap()
        );
        if bytes_read == 0 {
            false
        } else {
            to.push_into(&buf[0..bytes_read]);
            true
        }
    }

    fn do_write<T>(from: &mut WriterBuffer, to: &mut T) -> bool
    where
        T: Write + AsRawFd,
    {
        eprintln!("will write to fd {:?}", to.as_raw_fd());
        match to.write(from.get_some()) {
            Ok(bytes_written) => {
                eprintln!(
                    "written {:?} bytes to fd {:?}: {}",
                    bytes_written,
                    to.as_raw_fd(),
                    String::from_utf8(
                        from.get_some()[0..bytes_written]
                            .iter()
                            .cloned()
                            .flat_map(escape_default)
                            .collect()
                    ).unwrap()
                );
                from.consume(bytes_written);
                true
            }
            Err(e) => {
                if let std::io::ErrorKind::BrokenPipe = e.kind() {
                    eprintln!("could not written to fd {:?}: EPIPE", to.as_raw_fd());
                    false
                } else {
                    panic!("unexpected error {:?}", e)
                }
            }
        }
    }

    let mut client_dead = false;

    eprintln!("Assigning poll_fds[0] to child_stdout");
    eprintln!("Assigning poll_fds[1] to child_stderr");
    loop {
        poll_fds[2] = if child.stdin.is_some() && child_stdin_buf.has_content() {
            eprintln!("Assigning poll_fds[2] to child_stdin_fd");
            PollFd::new(child_stdin_fd, EventFlags::POLLOUT)
        } else {
            eprintln!("Assigning poll_fds[2] to -1");
            PollFd::new(-1, EventFlags::empty())
        };

        poll_fds[3] = if !client_dead && socket_buf.has_content() {
            eprintln!("Assigning poll_fds[3] to socketstream with POLLIN | POLLOUT");
            PollFd::new(
                socketstream.as_raw_fd(),
                EventFlags::POLLIN.bitor(EventFlags::POLLOUT),
            )
        } else {
            eprintln!("Assigning poll_fds[3] to socketstream with POLLIN");
            PollFd::new(socketstream.as_raw_fd(), EventFlags::POLLIN)
        };

        eprintln!(
            "\n\nBeginning of server event loop with client_dead = {:?}",
            client_dead
        );
        let poll_rv = poll(&mut poll_fds, -1).unwrap();
        eprintln!("poll(2) returned {:?}", poll_rv);

        // Detect pipe closed
        if child.stdin.is_some() {
            eprintln!("child.stdin {:?}", child.stdin);
            // Special handling to detect the pipes are closed or not.
            let mut p = [PollFd::new(child_stdin_fd, EventFlags::POLLOUT)];
            poll(&mut p, 0).unwrap();
            eprintln!("child stdin pipe revents {:?}", p[0].revents());
            if p[0].revents()
                .unwrap()
                .intersects(EventFlags::POLLERR.bitor(EventFlags::POLLNVAL))
            {
                eprintln!("Closing stdin because it cannot be written any more");
                child.stdin = None;
            }
        }

        // Handle writers first: this is because we use the same condition that
        // we used to specify the poll fd to test. If we handled readers first,
        // we would need to keep track of the original state before readers
        // pushed data into our buffers.

        if child.stdin.is_some() && child_stdin_buf.has_content() {
            eprintln!("child.stdin is Some and has content");
            eprintln!("child.stdin revents {:?}", poll_fds[2].revents());
            match child_stdin_buf.get_next() {
                Some(3) => {
                    kill(pid, Some(Signal::SIGINT)).unwrap();
                    child_stdin_buf.consume(1)
                }
                Some(4) => {
                    child.stdin = None;
                    child_stdin_buf.consume(1);
                    continue;
                }
                _ => (),
            }
            if can_output(&poll_fds[2]) {
                if do_write(&mut child_stdin_buf, child.stdin.as_mut().unwrap()) == false {
                    eprintln!("Closing child stdin");
                    child.stdin = None;
                }
                continue;
            } else {
                eprintln!("Child not yet ready to accept input, skipping")
            }
        }

        if !client_dead && socket_buf.has_content() {
            eprintln!("Client not dead yet and has content");
            if can_output(&poll_fds[3]) {
                if do_write(&mut socket_buf, &mut socketstream) == false {
                    // No more write to the socket; so the client does not want to receive any data
                    // We also assume it does not want to send anything
                    client_dead = true;
                    child.stdin = None;
                }
                continue;
            } else {
                eprintln!("Socket not ready to accept input, skipping")
            }
        }

        // Done with writers, handle readers now
        if child.stdout.is_some() {
            eprintln!("child.stdout is Some");
            eprintln!("child.stdout revents {:?}", poll_fds[0].revents());
            if has_input(&poll_fds[0])
                && do_read(child.stdout.as_mut().unwrap(), &mut socket_buf) == false
                || has_hup(&poll_fds[0])
            {
                child.stdout = None;
                poll_fds[0] = PollFd::new(-1, EventFlags::empty());
                eprintln!("Closing child stdout and assigning poll_fds[0] to -1");
            }
        } else {
            eprintln!("child.stdout is None");
        }

        if child.stderr.is_some() {
            eprintln!("child.stderr is Some");
            eprintln!("child.stderr revents {:?}", poll_fds[1].revents());
            if has_input(&poll_fds[1])
                && do_read(child.stderr.as_mut().unwrap(), &mut socket_buf) == false
                || has_hup(&poll_fds[1])
            {
                child.stderr = None;
                poll_fds[1] = PollFd::new(-1, EventFlags::empty());
                eprintln!("Closing child stderr assigning poll_fds[1] to -1");
            }
        }

        if !client_dead {
            eprintln!("Client not dead yet");
            eprintln!("socket revents {:?}", poll_fds[3].revents());
            if has_input(&poll_fds[3]) {
                if do_read(&mut socketstream, &mut child_stdin_buf) == false {
                    // No more read; so the client will not send any more data to us
                    client_dead = true;
                    child.stdin = None;
                    eprintln!("Setting client_dead = true and closing child.stdin");
                }
            }
        }

        // Shutdown handling: quit if the shell has died
        if child.stdout.is_none() && child.stderr.is_none() && child.stdin.is_none() {
            break;
        }

        // Shutdown handling: quit if the client has died
        // TODO
    }

    let status = child.wait().unwrap();
    eprintln!(
        "SHELL EXIT SIGNAL={} STATUS={}\r",
        status.signal().unwrap_or(0),
        status.code().unwrap_or(0)
    )
}

fn main() {
    let args = parse_args();
    match args.mode {
        Mode::Server => {
            let listener = TcpListener::bind(("127.0.0.1", args.server_port)).unwrap();
            server_event_loop(listener.incoming().next().unwrap().unwrap())
        }
        Mode::Client => {
            let _raw_terminal_raii = RawTerminalRAII::new();
            let mut stream = TcpStream::connect(("127.0.0.1", args.server_port)).unwrap();
            client_event_loop(stream, unsafe { File::from_raw_fd(0) }, unsafe {
                File::from_raw_fd(1)
            })
        }
    }
}
