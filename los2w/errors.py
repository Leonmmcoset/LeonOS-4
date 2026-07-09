"""Error helpers for los2w."""

EPERM = 1
ENOENT = 2
EIO = 5
ECHILD = 10
EBADF = 9
EAGAIN = 11
ENOMEM = 12
EACCES = 13
EFAULT = 14
EEXIST = 17
ENODEV = 19
ENOTDIR = 20
EISDIR = 21
EINVAL = 22
ENOSYS = 38
ENOTEMPTY = 39
ENOTSUP = 95


def neg(errno: int) -> int:
    return -abs(errno)


class Los2WError(Exception):
    """Base exception for expected los2w failures."""


class UnsupportedABI(Los2WError):
    """Raised when the guest asks for an ABI surface v1 does not emulate."""


class GuestStopped(Los2WError):
    """Raised internally when emulation has stopped cleanly."""


class GuestFault(Los2WError):
    """Raised when guest execution cannot continue."""

    def __init__(self, message: str, *, errno: int | None = None):
        super().__init__(message)
        self.errno = errno
