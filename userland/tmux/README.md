# tmux on LeonOS 4

This port builds tmux 3.5a as `/programs/tmux/tmux.elf` against the shared
LeonOS runtime and the poll-only libevent core. It uses AF_UNIX sockets for
the persistent server and the native PTY API for pane processes.

The default server socket is `/tmp/tmux-<uid>/default`; the root path is kept
without a trailing separator so exFAT path normalization remains stable.

The ANSI terminfo adapter covers the `xterm`, `screen`, `screen-256color`,
`tmux-256color`, and `linux` terminal profiles exposed by LeonOS Terminal and
TTY mode. The initial port intentionally omits sixel, utempter, control mode,
and external terminfo databases.
