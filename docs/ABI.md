# LeonOS 4 ABI

## Linux x86_64 syscall convention

- `rax`: syscall number.
- `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9`: arguments 0 through 5.
- `rax`: return value.
- Negative return values are `-errno`.

Unimplemented syscalls return `-ENOSYS`.

## First syscall subset

The v1 ABI reserves Linux-compatible numbers for:

- `read`, `write`, `open`, `close`
- `stat`, `fstat`, `lseek`
- `mmap`, `munmap`, `ioctl`
- `getcwd`, `chdir`
- `execve`, `wait4`, `exit`, `nanosleep`

## Device model

The Ring-3 desktop window server owns the display/input devices:

- `0:/dev/fb0`
- `0:/dev/input/kbd0`
- `0:/dev/input/mouse0`

Client applications talk to the window server through osmlayer IPC.
