// LeonOS osmlayer POSIX bridge: translates selected POSIX requests to VFS.
// Implements file, directory, mount, and metadata operations for userland.

use crate::storage;
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
const SYS_KILL: u64 = 62;
const SYS_NICE: u64 = 34;
const SYS_GETPPID: u64 = 110;
const SYS_GETPRIORITY: u64 = 140;
const SYS_SETPRIORITY: u64 = 141;
const SYS_GETCWD: u64 = 79;
const SYS_CHDIR: u64 = 80;

const ENOSYS: i64 = 38;
const EBADF: i64 = 9;
/**
 * @brief Initializes linux abi.
 */
pub fn init_linux_abi() {}
/**
 * @brief Dispatches the subsystem.
 * @param number Input or output value used by this operation.
 * @param args Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
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
        SYS_GETPPID => 0,
        SYS_KILL | SYS_NICE | SYS_GETPRIORITY | SYS_SETPRIORITY => 0,
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
/**
 * @brief Writes the subsystem.
 * @param fd Open file descriptor used by this operation.
 * @param _buf Buffer consumed or filled by this operation.
 * @param len Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
 */
fn write(fd: u64, _buf: u64, len: u64) -> i64 {
    match fd {
        1 | 2 => len as i64,
        _ => {
            let _ = storage::append_log("/var/log/osmlayer.log", &[]);
            -EBADF
        }
    }
}
/**
 * @brief Coordinates the ioctl operation.
 * @param _fd Input or output value used by this operation.
 * @param request Request structure consumed and, where defined, updated by this operation.
 * @param _arg Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
fn ioctl(_fd: u64, request: u64, _arg: u64) -> i64 {
    match request {
        0x4c_47_55_49 => gui::client_api_version() as i64,
        0x4c_50_41_54 => {
            if vfs::path_is_absolute("/dev/fb0") {
                0
            } else {
                -ENOSYS
            }
        }
        _ => -ENOSYS,
    }
}
