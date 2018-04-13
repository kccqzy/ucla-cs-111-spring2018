extern crate termios;

use std::fs::File;
use std::io::Read;
use std::io::Write;
use std::os::unix::prelude::*;
use std::process::exit;
use termios::*;

pub struct RawTerminalRAII {
    pub original_termios: termios::Termios,
}

fn new_raw_terminal() -> RawTerminalRAII {
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

impl Drop for RawTerminalRAII {
    fn drop(&mut self) {
        match tcsetattr(0, TCSANOW, &self.original_termios) {
            Ok(_) => (),
            _ => exit(1),
        }
    }
}

fn main() {
    let mut stdin = unsafe { File::from_raw_fd(0) };
    let mut stdout = unsafe { File::from_raw_fd(1) };
    let _raw_terminal_raii = new_raw_terminal();
    loop {
        let mut buf = [0];
        let read_size = stdin.read(&mut buf).unwrap();
        if read_size == 0 {
            break;
        } else {
            let cr = 13;
            let lf = 10;
            if buf[0] == cr || buf[0] == lf {
                stdout.write_all(&[cr, lf]).unwrap();
            } else if buf[0] == 4 {
                break;
            } else {
                stdout.write(&buf).unwrap();
            }
        }
    }
}
