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
| 1 | windowd | `/run/leonos/windowd.sock` | implemented |
| 2 | imd | `/run/leonos/input-method.sock` | implemented |
| 3 | netmand | `/run/leonos/net.sock` | pending |
| 4 | authd / sessiond | `/run/leonos/authd.sock`, `/run/leonos/session.sock` | pending |
| 5 | devmand / procfs | `/run/leonos/devman.sock`, `/proc` | pending |
| 6 | fd-3 removal and cleanup | n/a | pending |

## Common message set

| Message | Direction | Fields | Permission |
|---|---|---|---|
| `HELLO` | client -> server | `u32 client_pid` | SO_PEERCRED match |
| `ERROR` | server -> client | `s32 code` | any |

## windowd (`/run/leonos/windowd.sock`)

The userspace windowd daemon owns the window registry. Pixel storage is a
`/dev/shm0` segment per window and crosses the socket only as an SCM_RIGHTS
fd. Desktop connects with the policy handshake token
`desktop-policy-v1`; the kernel SO_PEERCRED uid must be zero.

| Message | Direction | Fields | Permission |
|---|---|---|---|
| `HELLO` | app -> windowd | `u32 pid`, `u32 role` | SO_PEERCRED pid match |
| `HELLO_ACK` | windowd -> client | `u32 version` | any |
| `POLICY_HELLO` | desktop -> windowd | `u32 pid`, token[32] | uid==0 + token |
| `CREATE` | app -> windowd | `u32 width,height,flags`, title[48], text[1024] | app connection |
| `CREATE_ACK` | windowd -> app | `u32 win_id,width,height,stride` + SCM_RIGHTS shm fd | any |
| `DESTROY` | app -> windowd | `u32 window_id` | owner |
| `PRESENT` | app -> windowd | `u32 win_id,width,height,stride` | owner |
| `UPDATE` | app -> windowd | `u32 win_id,mask,flags`, title[48] | owner |
| `FETCH` | desktop -> windowd | `u32 win_id,cap_w,cap_h,stride` | policy |
| `FETCH_ACK` | windowd -> desktop | `u32 win_id,width,height,stride` + SCM_RIGHTS shm fd | policy |
| `EVENT` | desktop -> windowd -> app | `struct leonos_gui_app_event` | policy sender, target by win_id |
| `INPUT` | windowd -> desktop | `struct leonos_input_event` | policy |
| `WINDOW_NOTIFY` | windowd -> desktop | `struct leonos_gui_window_msg` types 1..7 | policy |
| `MOUSE_VISIBLE` | app/desktop <-> windowd | `u32 win_id,visible` | query uses win_id=0xffffffff |
| `CURSOR_REQUEST/CURSOR_REGION` | app -> windowd -> desktop | gui cursor structs | any app |
| `DISPLAY_STATE/APPEARANCE_STATE` | publisher/query | gui state structs | policy publishes; app queries |
| `DISPLAY_REQUEST/APPEARANCE_REQUEST` | app -> windowd -> desktop | gui request structs | policy receives |

## imd (`/run/leonos/input-method.sock`)

| Message | Direction | Fields | Permission |
|---|---|---|---|
| `HELLO` | client -> imd | `u32 pid`, `u32 role(app/provider)` | SO_PEERCRED pid match |
| `ACK` | imd -> client | `s32 code` | any |
| `REGISTER` | provider -> imd | `struct leonos_inputm_provider` | uid!=0 |
| `UNREGISTER` | provider -> imd | none | provider connection |
| `KEY_EVENT` | imd -> provider | `struct leonos_inputm_key_event` | active provider |
| `SUBMIT_KEY` | app -> imd | `struct leonos_inputm_key_event` (window_id/keycode/pressed) | focused context |
| `RESULT` | provider -> imd; imd -> app | `struct leonos_inputm_result` | provider / target app |
| `SET_CONTEXT` | app -> imd | `struct leonos_inputm_context` | owning pid |
| `SET_ACTIVE/GET_STATE/LIST/NOTIFY_CONFIG` | app -> imd | uid + id / provider array / state | uid owner or uid==0 |

## Phase 0 verification

Run `/programs/ipctest/ipctest.elf` on the target. It covers blocking
socketpair reads, SCM_RIGHTS passing of a `/dev/shm0` descriptor, shared mmap,
credential syscalls, `uname`, and an AF_INET connect probe.
