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
| 3 | netmand | `/run/leonos/net.sock` | implemented |
| 4 | authd / sessiond | `/run/leonos/authd.sock`, `/run/leonos/session.sock` | implemented |
| 5 | devmand / procfs | `/run/leonos/devman.sock`, `/proc` | implemented |
| 6 | fd-3 removal and cleanup | n/a | implemented |

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

## authd (`/run/leonos/authd.sock`)

authd runs uid==0 and owns `/system/config/users.db`. All mutating
operations are gated by SO_PEERCRED: creation requires peer uid 0, updates
require uid 0 or the same uid, and login never returns a password hash.

| Message | Direction | Fields | Permission |
|---|---|---|---|
| `HELLO` | client -> authd | `u32 pid` | SO_PEERCRED pid match |
| `ACK` | authd -> client | `s32 code` | any |
| `STATUS` | client -> authd | none -> `struct leonos_auth_status` | any |
| `LIST` | client -> authd | include_disabled, capacity -> count + user array | any |
| `LOGIN` | client -> authd | username, password -> user | any |
| `ELEVATE` | client -> authd | admin username/password -> user | peer uid 0 |
| `CURRENT` | client -> authd | none -> user | peer uid or current session |
| `LOGOUT` | client -> authd | none | current session |
| `CREATE/UPDATE/CHANGE_PASSWORD` | client -> authd | authd structs | uid 0 / same uid |

## sessiond (`/run/leonos/session.sock`, hosted by serviced)

| Message | Direction | Fields | Permission |
|---|---|---|---|
| `HELLO` | client -> sessiond | `u32 pid,uid` | SO_PEERCRED match |
| `ACK` | sessiond -> client | `s32 code,u32 request_id` | any |
| `REQUEST` | client -> sessiond | `struct leonos_startup_command` | uid!=0 |
| `REQUEST_STATUS` | client -> sessiond | request_id | any |
| `DIALOG_GET/RESOLVE` | client -> sessiond | request_id/decision | any |
| `LIST/SET_ENABLED/REMOVE` | client -> sessiond | startup entry records | uid owner |
| `LAUNCH_CURRENT` | client -> sessiond | none | current session; child is setuid |

## devmand (`/run/leonos/devman.sock`, hosted by serviced)

| Message | Direction | Fields | Permission |
|---|---|---|---|
| `HELLO` | client -> devmand | `u32 pid,uid` | SO_PEERCRED match |
| `ACK` | devmand -> client | `s32 code,u32 count` | any |
| `DEVICE_LIST` | client -> devmand | capacity -> count + `struct leonos_device_info[]` | any |
| `DRIVER_LIST` | client -> devmand | capacity -> count + `struct leonos_driver_info[]` | any |
| `DRIVER_CONTROL` | client -> devmand | action/file | SO_PEERCRED uid==0 |

## Phase 6 cleanup

* The fd 3 control-descriptor mechanism and every private service-request
  ioctl family are deleted from kernel and libc source.
* Remaining ioctls are device-UAPI only: TTY termios/winsize, evdev, OSS,
  block BLK*, fbdev FBIO*, and `/dev/gpu` GPU commands.
* ACL compatibility is expressed through `chmod`/`chown`; kernel-debug state
  uses `/system/state/kernel-debug`.

## procfs (kernel, read-only)

`/proc/uptime`, `/proc/meminfo`, `/proc/version`, `/proc/machine-id`,
`/proc/<pid>/stat`, `/proc/<pid>/cmdline`, and `/proc/self/*` are synthetic
storage nodes. They are read through ordinary open/read/readdir and are never
served through a private ioctl.

## netmand (`/run/leonos/net.sock`, hosted by serviced)

| Message | Direction | Fields | Permission |
|---|---|---|---|
| `HELLO` | client -> netmand | `u32 pid` | SO_PEERCRED pid match |
| `ACK` | netmand -> client | `s32 code` | any |
| `CONFIG` | client -> netmand | none, response `struct leonos_net_config` | any |
| `DNS_POLICY` | client -> netmand | `struct leonos_net_dns_policy` | set requires uid==0 (SO_PEERCRED) |
| `DHCP` | client -> netmand | `struct leonos_net_dhcp` | any; no-device status until NIC exists |
| `PING` | client -> netmand | `struct leonos_net_ping` | any |
| `DNS` | client -> netmand | `struct leonos_net_dns` | any |
| `CONNECTIONS` | client -> netmand | count header + entries | any |

Data-plane sockets (`socket(AF_INET, SOCK_STREAM)`, connect/send/recv/close)
never use this control socket.

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
