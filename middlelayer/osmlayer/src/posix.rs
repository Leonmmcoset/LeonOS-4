use crate::fat32;
use crate::gui;
use crate::vfs;

const SYS_READ: u64 = 0;
const SYS_WRITE: u64 = 1;
const SYS_OPEN: u64 = 2;
const SYS_CLOSE: u64 = 3;
const SYS_STAT: u64 = 4;
const SYS_FSTAT: u64 = 5;
const SYS_LSEEK: u64 = 8;
const SYS_MMAP: u64 = 9;
const SYS_MUNMAP: u64 = 11;
const SYS_IOCTL: u64 = 16;
const SYS_NANOSLEEP: u64 = 35;
const SYS_GETPID: u64 = 39;
const SYS_EXECVE: u64 = 59;
const SYS_EXIT: u64 = 60;
const SYS_WAIT4: u64 = 61;
const SYS_GETCWD: u64 = 79;
const SYS_CHDIR: u64 = 80;

const ENOSYS: i64 = 38;
const EBADF: i64 = 9;

pub fn init_linux_abi() {}

pub fn dispatch(number: u64, args: &[u64; 6]) -> i64 {
    match number {
        SYS_WRITE => write(args[0], args[1], args[2]),
        SYS_READ => 0,
        SYS_OPEN => 3,
        SYS_CLOSE => 0,
        SYS_STAT | SYS_FSTAT => 0,
        SYS_LSEEK => 0,
        SYS_GETCWD => args[0] as i64,
        SYS_CHDIR => 0,
        SYS_GETPID => 0,
        SYS_EXECVE => 0,
        SYS_WAIT4 => 0,
        SYS_EXIT => 0,
        SYS_NANOSLEEP => 0,
        SYS_MMAP => -ENOSYS,
        SYS_MUNMAP => 0,
        SYS_IOCTL => ioctl(args[0], args[1], args[2]),
        _ => -ENOSYS,
    }
}

fn write(fd: u64, _buf: u64, len: u64) -> i64 {
    match fd {
        1 | 2 => len as i64,
        _ => {
            let _ = fat32::append_log("0:/var/log/osmlayer.log", &[]);
            -EBADF
        }
    }
}

fn ioctl(_fd: u64, request: u64, _arg: u64) -> i64 {
    match request {
        0x4c_47_55_49 => gui::client_api_version() as i64,
        0x4c_50_41_54 => {
            if vfs::resolve_drive_path("0:/dev/fb0").is_some() {
                0
            } else {
                -ENOSYS
            }
        }
        _ => -ENOSYS,
    }
}
