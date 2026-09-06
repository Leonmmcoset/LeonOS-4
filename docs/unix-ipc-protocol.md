# LeonOS Unix IPC protocol

This document is the normative wire contract for the LeonOS service sockets.
All paths live below `/run/leonos/`. Every service uses an AF_UNIX
SOCK_STREAM connection and the length-prefixed framing described below.

## Framing

```
u32 magic  = 'LNXU' (0x554e4c4c on little-endian x86_64)
u32 version = 1
repeat:
  u32 length
  u8  payload[length]
```

All integers are little-endian. A client opens one connection and issues a
`HELLO` message first. The server uses `SO_PEERCRED` to learn the peer
`{pid, uid, gid}`; the pid in `HELLO` must match the kernel-reported peer pid
or the connection is rejected with `ERROR{code=EPERM}`.

## Status

| Phase | Service | Socket | Status |
|---|---|---|---|
| 0 | kernel IPC infrastructure | blocking AF_UNIX, socketpair, SCM_RIGHTS, SO_PEERCRED, /dev/shm0, AF_INET fd path | implemented |
| 1 | windowd | `/run/leonos/windowd.sock` | pending |
| 2 | imd | `/run/leonos/input-method.sock` | pending |
| 3 | netmand | `/run/leonos/net.sock` | pending |
| 4 | authd / sessiond | `/run/leonos/authd.sock`, `/run/leonos/session.sock` | pending |
| 5 | devmand / procfs | `/run/leonos/devman.sock`, `/proc` | pending |
| 6 | fd-3 removal and cleanup | n/a | pending |

## Common message set

| Message | Direction | Fields | Permission |
|---|---|---|---|
| `HELLO` | client -> server | `u32 client_pid` | SO_PEERCRED match |
| `ERROR` | server -> client | `s32 code` | any |

## Phase 0 verification

Run `/programs/ipctest/ipctest.elf` on the target. It covers blocking
socketpair reads, SCM_RIGHTS passing of a `/dev/shm0` descriptor, shared mmap,
credential syscalls, `uname`, and an AF_INET connect probe.
