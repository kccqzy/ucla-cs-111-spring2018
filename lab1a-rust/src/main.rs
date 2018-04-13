extern crate nix;
extern crate termios;

use std::os::unix::process::ExitStatusExt;
use nix::sys::signal::Signal;
use std::ops::BitOr;
use std::process::{Command, Stdio};
use std::fs::File;
use std::io::Read;
use std::io::Write;
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

fn read_char_echo(stdin: &mut File, stdout: &mut File) -> Option<u8> {
    let mut buf = [0];
    let read_size = stdin.read(&mut buf).unwrap();
    if read_size == 0 {
        None
    } else {
        if buf[0] == CR || buf[0] == LF {
            stdout.write_all(&[CR, LF]).unwrap();
            Some(buf[0])
        } else if buf[0] == 4 {
            None
        } else {
            stdout.write(&buf).unwrap();
            Some(buf[0])
        }
    }
}

fn do_echo(stdin: &mut File, stdout: &mut File) {
    loop {
        if read_char_echo(stdin, stdout).is_none() {
            break;
        }
    }
}

fn translate_buffer(inbuf: &[u8]) -> Vec<u8> {
    inbuf
        .iter()
        .flat_map(|c| {
            if *c == LF {
                Some(CR).into_iter().chain(Some(LF).into_iter())
            } else {
                Some(*c).into_iter().chain(None.into_iter())
            }
        })
        .collect()
}

fn do_shell(stdin: &mut File, stdout: &mut File) {
    let mut child = Command::new("/bin/bash")
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .spawn()
        .unwrap();
    let pid = Pid::from_raw(child.id() as i32);

    {
        // Inspecting the source here reveals that the child's stdin and stdout are
        // thin wrappers of a file descriptor. No buffering yay!
        let child_stdout_fd = child.stdout.as_ref().unwrap().as_raw_fd();

        let mut poll_fds = [
            PollFd::new(0, EventFlags::POLLIN),
            PollFd::new(child_stdout_fd, EventFlags::POLLIN),
        ];

        fn has_input(pfd: &PollFd) -> bool {
            pfd.revents()
                .map_or(false, |e| e.contains(EventFlags::POLLIN))
        }
        fn has_hup(pfd: &PollFd) -> bool {
            pfd.revents().map_or(false, |e| {
                e.intersects(EventFlags::POLLHUP.bitor(EventFlags::POLLERR))
            })
        }

        loop {
            poll(&mut poll_fds, -1).unwrap();

            // Does the shell have any output for us?
            if child.stdout.is_some() && has_input(&poll_fds[1]) {
                let mut buf = [0; 65536];
                let bytes_read = child.stdout.as_mut().unwrap().read(&mut buf).unwrap();
                if bytes_read == 0 {
                    child.stdout = None;
                    poll_fds[1] = PollFd::new(-1, EventFlags::empty());
                } else {
                    let outbuf = translate_buffer(&buf[0..bytes_read]);
                    stdout.write_all(&outbuf).unwrap()
                }
            }

            // Has the shell exited?
            if child.stdout.is_some() && has_hup(&poll_fds[1]) {
                child.stdout = None;
                poll_fds[1] = PollFd::new(-1, EventFlags::empty());
            }

            // Has the user typed anything here?
            if child.stdin.is_some() && has_input(&poll_fds[0]) {
                match read_char_echo(stdin, stdout) {
                    None => child.stdin = None,
                    Some(3) => kill(pid, Some(Signal::SIGTERM)).unwrap(),
                    Some(c) => child
                        .stdin
                        .as_mut()
                        .unwrap()
                        .write_all(&[if c == CR { LF } else { c }])
                        .unwrap(),
                }
            }

            // Has the user closed it?
            if child.stdin.is_some() && has_hup(&poll_fds[0]) {
                child.stdin = None
            }

            // TODO SIGPIPE handling

            if child.stdout.is_none() {
                break;
            }
        }
    }

    // Now perform orderly shutdown
    let status = child.wait().unwrap();
    eprintln!(
        "SHELL EXIT SIGNAL={} STATUS={}\r",
        status.signal().unwrap_or(0),
        status.code().unwrap_or(0)
    )
}

fn main() {
    let mut stdin = unsafe { File::from_raw_fd(0) };
    let mut stdout = unsafe { File::from_raw_fd(1) };

    let args: Vec<String> = std::env::args().collect();
    if args.len() == 2 && args[1] == "--shell" {
        let _raw_terminal_raii = RawTerminalRAII::new();
        do_shell(&mut stdin, &mut stdout)
    } else if args.len() > 1 {
        eprintln!("{}: unrecognized arguments", args[0])
    } else {
        let _raw_terminal_raii = RawTerminalRAII::new();
        do_echo(&mut stdin, &mut stdout)
    }
}
