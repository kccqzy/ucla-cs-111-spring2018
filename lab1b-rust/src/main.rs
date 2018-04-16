extern crate nix;
extern crate termios;

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
use nix::unistd::Pid;
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
    let mut child = Command::new("/bin/bash")
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .unwrap();
    let pid = Pid::from_raw(child.id() as i32);

    let child_stdout_fd = child.stdout.as_ref().unwrap().as_raw_fd();
    let child_stderr_fd = child.stderr.as_ref().unwrap().as_raw_fd();
    let child_stdin_fd = child.stdin.as_ref().unwrap().as_raw_fd();

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
        T: Read,
    {
        let mut buf = [0; 65536];
        let bytes_read = from.read(&mut buf).unwrap();
        if bytes_read == 0 {
            false
        } else {
            to.push_into(&buf[0..bytes_read]);
            true
        }
    }

    fn do_write<T>(from: &mut WriterBuffer, to: &mut T) -> bool
    where
        T: Write,
    {
        match to.write(from.get_some()) {
            Ok(bytes_written) => {
                from.consume(bytes_written);
                true
            }
            Err(e) => {
                if let std::io::ErrorKind::BrokenPipe = e.kind() {
                    false
                } else {
                    panic!("unexpected error {:?}", e)
                }
            }
        }
    }

    loop {
        poll_fds[2] = if child_stdin_buf.has_content() {
            PollFd::new(child_stdin_fd, EventFlags::POLLOUT)
        } else {
            PollFd::new(-1, EventFlags::empty())
        };

        poll_fds[3] = if socket_buf.has_content() {
            PollFd::new(
                socketstream.as_raw_fd(),
                EventFlags::POLLIN.bitor(EventFlags::POLLOUT),
            )
        } else {
            PollFd::new(socketstream.as_raw_fd(), EventFlags::POLLIN)
        };

        poll(&mut poll_fds, -1).unwrap();

        // Always handle all readers first
        if child.stdout.is_some() && has_input(&poll_fds[0]) {
            if do_read(child.stdout.as_mut().unwrap(), &mut socket_buf) == false {
                child.stdout = None;
                poll_fds[0] = PollFd::new(-1, EventFlags::empty());
            }
        }

        if child.stderr.is_some() && has_input(&poll_fds[1]) {
            if do_read(child.stderr.as_mut().unwrap(), &mut socket_buf) == false {
                child.stderr = None;
                poll_fds[1] = PollFd::new(-1, EventFlags::empty());
            }
        }

        if has_input(&poll_fds[3]) {
            if do_read(&mut socketstream, &mut child_stdin_buf) == false {
                // No more read
                // TODO ignore for now
            }
        }

        // Now handle writers
        if child.stdin.is_some() && can_output(&poll_fds[2]) && child_stdin_buf.has_content() {
            if do_write(&mut child_stdin_buf, child.stdin.as_mut().unwrap()) == false {
                child.stdin = None
            }
        }

        if can_output(&poll_fds[3]) && socket_buf.has_content() {
            if do_write(&mut socket_buf, &mut socketstream) == false {
                // No more write to the socket
                // TODO
            }
        }
    }
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
