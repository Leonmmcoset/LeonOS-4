# LeonOS-4 全面 UNIX 化改造任务书（私有 ioctl 通道 → 标准 Unix 机制）

> 交付对象：执行 Agent。本文件是自包含任务书，执行前通读一遍，按 Phase 0 → 6 顺序推进。
> 工作目录：/home/xiaobai/Projects/Projects/LeonOS-4，基线分支 rewrite/new-kernel（提交 cef8861，即 PR Leonmmcoset/LeonOS-4#15）。
> 总目标：**彻底删除"服务请求式"私有 ioctl 通道**，所有服务间通信改用 Unix domain socket / 标准设备语义 / 标准系统调用。最终内核中只剩 Linux 兼容的设备 UAPI ioctl。

---

## 0. 硬约束（违反任何一条即返工）

1. **禁止运行 QEMU**。编译验证用：
   - `python3 build.py run kernel`
   - `python3 build.py run userland`
   - `python3 build.py run installer`
   三个全绿才算阶段完成。运行时验证由用户完成，你在每个阶段末尾给出"用户手测清单"。
2. **不得推翻已完成的工作**：Linux ABI v1 迁移（18 应用、`tools/check_abi_migration.py --strict` 通过）是基线，只能在它之上继续；PTY 的 POSIX 化（forkpty/dup2/ hangup 语义）刚做完，`kernel/ntclks/syscall.c` 的 dup2 路径与 `kernel/ntclks/pty.c` 不要顺手重构。Terminal 空白问题由另一条工作线排查，与本任务书无关，不要"顺手修"。
3. **每个子系统一个独立 commit**，遵循仓库现有风格（中文、conventional 前缀，如 `feat(ipc): ...`、`refactor(gui): ...`、`remove(ioctl): ...`）。每个 commit 必须可独立编译通过。
4. **每个 Phase 结束**运行 `python3 tools/check_abi_migration.py --strict` 并保持通过；该脚本的符号清单要随删除同步更新。
5. 删除代码时先 grep 确认零调用者；libc 导出函数删除前 grep 全部 userland/apps。
6. 不改 `userland/libc/src/syscall.S` 的 trampoline 机制；不改动已通过严格检查的信号路径。
7. 建议从 `origin/rewrite/new-kernel` 拉 `rewrite/unix-ipc` 工作分支（如用户另有指示以用户为准）。

---

## 1. 架构总则：边界划清

**允许保留的 ioctl（设备 UAPI，Unix 惯例）**——必须限定在对应设备节点的 fd 上，禁止 `fd==3` 魔法直通：
- 终端：`TCGETS/TCSETS*/TIOCGWINSZ/TIOCSWINSZ/TIOCGPTN/TIOCSPTLCK/TIOCGPGRP/TIOCSPGRP`（kernel/ntclks/syscall.c:5784-5848，已是 Linux 语义）
- evdev：`EVIOCGNAME/EVIOCGID/EVIOCGBIT/EVIOCGKEY/EVIOCGRAB/EVIOCGVERSION`（syscall.c:1463-1535）
- OSS 音频：`/dev/dsp` 的 `SNDCTL_DSP_*`（syscall.c:1574-1674）
- 块设备：`BLKGETSIZE64/BLKSSZGET/BLKRRPART/BLKROGET`（syscall.c:4342-4376）
- fbdev：`FBIOGET_VSCREENINFO/FBIOPUT_VSCREENINFO/FBIOGET_FSCREENINFO`（syscall.c:4393-4447）
- GPU：`/dev/gpu` 上的 0x4c475001-05（DRM 风格设备 UAPI）——保留但收窄到 `/dev/gpu` fd

**必须消灭的（服务请求式私有通道）**：
- `LEONOS_GUI_IOCTL_*`（窗口/呈现/事件/显示/外观/任务/电源/光标/taskbar）
- `LEONOS_INPUTM_IOCTL_*`（/dev/input-method）
- `LEONOS_IOCTL_NET_*`（/dev/net0：PING/CONFIG/DHCP/DNS/HTTP_GET/SOCKET_OPEN..CLOSE/CONNECTIONS/DNS_POLICY）
- `LEONOS_AUTH_IOCTL_*`、`LEONOS_STARTUP_IOCTL_*`、`LEONOS_FS_IOCTL_ACL_*`
- `LEONOS_IOCTL_AUDIO_*`（/dev/audio0 legacy）、`LEONOS_PTY_IOCTL_*` 私有 family（CREATE/SPAWN/OWNER_*，POSIX 路径已全覆盖）
- `LEONOS_IOCTL_DEVICE_LIST`、`LEONOS_IOCTL_DRIVER_LIST/CONTROL`、`LEONOS_IOCTL_SYSTEM_INFO/TIME_INFO/TIME_NTP_SYNC/MACHINE_IDENTITY/PERF_INFO/TASK_AFFINITY`、`LEONOS_TEXT_IOCTL_LAYOUT_UTF8`、`LEONOS_IOCTL_LIST_DIR`、`LEONOS_SIGNAL_IOCTL_ACTION`（legacy）、`LEONOS_KERNEL_DEBUG_IOCTL_CONTROL`
- libc 的 **fd==3 控制描述符**机制本身（见 §7）

**目标拓扑**（全部走 `/run/leonos/*.sock`，AF_UNIX SOCK_STREAM，长度前缀帧协议：`u32 magic 'LNXU' + u32 version + {u32 len + payload}` 消息序列）：

| 服务 | socket 路径 | 取代的私有通道 | 宿主进程 |
|---|---|---|---|
| windowd（窗口服务器/合成器） | `/run/leonos/windowd.sock` | GUI ioctl 全家（除电源/任务枚举见 procfs） | 新守护进程（由 desktop 演化或独立，见 §6.1） |
| imd（输入法管理） | `/run/leonos/input-method.sock` | `LEONOS_INPUTM_IOCTL_*`、/dev/input-method | 独立守护（oschinpt 是 provider 客户端） |
| authd（认证） | `/run/leonos/authd.sock` | `LEONOS_AUTH_IOCTL_*` | 独立守护（uid==0） |
| netmand（网络管理面） | `/run/leonos/net.sock` | `LEONOS_IOCTL_NET_*` 的 CONFIG/DHCP/DNS_POLICY/CONNECTIONS/PING（管理面） | 并入 serviced |
| devmand（设备/驱动） | `/run/leonos/devman.sock` | /dev/hwinfo、/dev/driverctl、DEVICE_LIST、DRIVER_* | 并入 serviced 或独立 |
| sessiond（会话策略） | `/run/leonos/session.sock` | STARTUP_IOCTL、GUI REBOOT/SHUTDOWN、DISPLAY_*/APPEARANCE_* 发布 | 并入 desktop 或 serviced |
| procfs（内核只读） | `/proc` | TASKS、UPTIME_MS、PERF_INFO、SYSTEM_INFO、MACHINE_IDENTITY | 内核 |

**数据面**一律不走 socket 大流量：窗口像素走共享内存（shm 设备，§5.3），网络数据走 AF_INET socket（§6.3），音频走 /dev/dsp，块设备走 read/write。

---

## 2. 事实基线（调研结论，执行时可复核但不要盲信——标注 ⚠ 的需现场确认）

### 2.1 内核侧
- 分发总入口 `syscall_dispatch_regs`（kernel/ntclks/syscall.c:6028-6048）：fs→ipc→security→gui→device→legacy 顺序认领。**没有私有系统调用号**，一切私有操作骑在 `LINUX_SYS_IOCTL`(16) 上。
- GUI：命令宏 syscall.c:56-82；逐条处理 syscall.c:4456-5034；内核实现核心 `kernel/ntclks/gui_ipc.c`（窗口表 32、全局消息环 128、每窗事件环 32、光标区域 64/窗）；`syscall_gui.c:8-19` 按 request 高 16 位认领 `0x4c47/0x4c46/0x4c44/0x4c41/0x4c54`；窗口服务器权限门 `require_window_server()`（syscall.c:238-245，`TASK_FLAG_WINDOW_SERVER`）。
  - `WAIT_WINDOW_EVENT`（syscall.c:4801-4823）内核阻塞：`sched_wait_current_for_window_event`（sched/sched.c:1518-1531，task 内嵌 wait_window_id/wake_tick），唤醒 `sched_wake_window_event`（sched.c:1536-1549），由 `gui_ipc_push_event`（gui_ipc.c:1010）触发。
  - `PRESENT_WINDOW`（syscall.c:4639-4658 → gui_ipc.c:719-738）整窗逐行拷贝到内核窗口缓冲；几何上限 1920×1080（gui_ipc.h:15-17）。
- inputm：核心 kernel/ntclks/inputm.c（provider 槽位 + 事件环 32 + results 32 + context 64 + uid 状态槽）；ioctl 认领 syscall.c:4380-4387（fd==3 或 INPUT_METHOD 节点）；全非阻塞；/dev/input-method 无 read/write/poll 语义（task_device_read/write 返回 -EBADF，syscall.c:1344-1450）。
- evdev：input.c（原始队列 512、evdev 环 1024、keyboard/mouse 两设备）；read 支持 O_NONBLOCK（syscall.c:1374-1382）；poll（syscall.c:3123-3129）；EVIOCGRAB 独占（input.c:298-303）。abittest 已直接使用（userland/apps/abittest/main.c:121-154）。
- 网络：net.c 私有请求经 `LEONOS_IOCTL_NET_*`（syscall.c:5178-5303 直连 net.c API，**不校验 fd**）；私有 `struct net_socket`（net.c:119-141）、表 16 个（net.c:152）、TCP 状态机（net.c:1536+）；**connect/send/recv 内核内忙等**（net.c:2767-2788、2900-3000 区域，`net_poll_once + net_cpu_relax` 自旋，不睡调度器）；权限门 `require_network_config_access`（syscall.c:251-268）。
- 音频：/dev/dsp 已是标准 OSS（task_oss_dsp_ioctl syscall.c:1574-1674，8×2048B 队列）；/dev/audio0 legacy ioctl（syscall.c:5338-5386）零应用使用。
- 块设备：标准 BLK* ioctl 已实现（syscall.c:4342-4376）；`storage_installer_root_active()`（drivers/bootstrap/storage/storage_vfs.c:208-211）放宽 root 检查。
- fd3 家族内核处理：auth `auth_handle_ioctl`（syscall.c:2090-2270，用户表 `auth_user_scratch` syscall.c:111）；startup（syscall.c:2709-2900，内核驻留 DB syscall.c:113-151）；fs ACL（syscall.c:1958-2010）；SYSTEM/TIME/NTP/MACHINE（syscall.c:5139-5167）；PERF（5688）；TASK_AFFINITY（4908-4945）；DEVICE_LIST（5388-5686）；DRIVER_*（5305/5324）；kernel-debug（syscall.c:4449-4454 → kernel_debug.c:491-534）；signal legacy（syscall.c:3378-3400）；text layout（5036-5091）；LIST_DIR（5093-5137）。
- 内核对象表：KERNEL_OBJECT_MAX=256，种类 NONE/FILE/PIPE/SOCKET/DEVICE/VM（include/ntclks/object.h:11-21），实际只用 PIPE/SOCKET。
- pipe：**全非阻塞**（syscall_ipc.c:113-142，容量 4096、上限 256 条）；空读 -EAGAIN、满写部分写/-EAGAIN、无读者 -EPIPE。
- **通用等待队列已存在但零调用者**：kernel/ntclks/wait.c（`kernel_wait_queue_init/add/remove/wake_one/wake_all`，唤醒走 sched_mark_ready）⚠ 执行时先复核。另有 task 内嵌等待字段原语（sched_sleep_current_until sched.c:1497 等）。
- framebuffer：/dev/fb0 **支持 mmap**（syscall_mm.c:784-803，仅 FB0 节点设备映射，offset+len ≤ pitch*height）；fb0 read/write 无实现。
- devfs：静态别名表 drivers/bootstrap/storage/storage_vfs.c:17-58（null/zero/full/random/urandom/tty/console/ptmx/fb0/keyboard/mouse/dsp/audio/audio0/ttyS0/serial0/sda/vda/nvme0n1/disk0/net0/ethernet0/rtc/kmsg/driverctl/input-method/hwinfo/gpu/stdin/stdout/stderr + input/ 目录）；/dev/pts 动态展开（syscall.c:884、storage_vfs.c:332）可作动态节点参考。设备种类常量 ntclks/storage.h:99-122。节点路径宏 include/leonos/device.h:26-51。
- mmap 现状：MAP_SHARED|PRIVATE|FIXED|ANONYMOUS 标志位定义（syscall_mm.c:20-24），匿名只支持 PRIVATE（:753-754），**文件/设备 SHARED 不支持**（fb0 设备映射除外）。
- ⚠ `__NR_mount/__NR_umount2` 有号无实现（需确认）；`__NR_gettimeofday/uname/getuid/setuid/socketpair/sendmsg/recvmsg/accept4/chmod/fchmod/chown/fchown/sched_setaffinity/sched_getaffinity/settimeofday/reboot/pipe2/dup3` **均无号**，需要新增（见 §5）。

### 2.2 用户侧
- libc 的 fd==3 魔法重定向：`userland/libc/src/libc.c:485-613`——`ioctl()` 在 fd==3 时按 request 码选真实节点（fb0/audio0/net0/driverctl/input-method/tty/console），静态缓存，-EBADF 重开（:598-609）。`leonos_framebuffer_fd()`（libc.c:1611-1621）open("/dev/fb0") 失败回落 fd=3。`leonos_gui_connect()`（libc.c:1623-1626）= ioctl(fb_fd, VERSION)。
- libc 私有 API → 通道映射：
  - gui.h（libc.c:1611-2060：connect/create_app_window_ex/present/fetch/wait_app_event/send_window_event/task_snapshot/task_kill/display/appearance/reboot:4140/shutdown:4145）
  - inputm/text_input（inputm.c:8-163，open("/dev/input-method") + ioctl）
  - gpu.c（5 处 ioctl(3, 0x4c475001-05)）、gpu_sdk.c（open("/dev/gpu") + ioctl，gpu_sdk.c:14-68）
  - devmgr_service.c（open("/dev/hwinfo"|"/dev/driverctl") + ioctl，:47/:65/:90）
  - net_service.c（缓存 /dev/net0 fd，:14-35）+ libc.c:2294-2527（NET/DNS/DHCP/PING/HTTP_GET/SOCKET_*，全 ioctl(3)→net0）+ tls.c（复用 leonos_socket_send/recv，libc.c:3203/3728）
  - auth（libc.c:3912-4023）、startup（libc.c:4040-4093）、fs ACL（libc.c:2079-2130）、system/perf/ntp/machine-id/affinity（libc.c:4101-4135、1949/1963）、device_list（libc.c:2162-2177）、pty legacy（libc.c:4266-4404）、audio legacy（libc.c:2208-2286）、kernel_debug（libc.c:4156-4186）
  - **已纯 POSIX**：launch.c（fork+execve，:215）、environment.c（文件）、blockdev.c（BLK* + read/write，文件头声明无私有 ABI，:1-6）、api.c/app_registry.c/i18n（文件）、admin.c（UI+auth）、license.c（HTTP 127.0.0.1:30301）
- 应用调用图（家族级）：
  - GUI 全家：desktop、terminal、fileman、settings、login、oobe、devmgr、drvmgr、browser、taskmgr、doomlauncher、installer、guitest、httpget、ping、downloadmgr、servicemgr、sysconfdialog、bugtest、apiapp、mp3play、wavplay、pleditor、calc/notepad/helloworld 等全部示例；portablegl（pgl，userland/portablegl/leonos_pgl.c:44-156）+ glxgears
  - auth：login、oobe、settings（create_user :1781）、fileman、browser（cookies.c:118）、drvmgr、diskmgr、apiapp、servicemgr
  - inputm：oschinpt（provider：register:1041/next:1068/result:831/state:1052）、desktop（set_active，inputm.c:317）、settings（:872/:2124）、login、oobe、browser（text_input_set_context，main.c:20）
  - net_service：netctl（:292-427）、serviced（dhcp :394/:417、ntp :475）、ping、httpget、browser（经 libc http/tls）、downloadmgr、apiapp
  - devmgr_service：devmgr（:274 起）、drvmgr（driver control）
  - startup：settings（:2041）、sysconfdialog（:100/:108）、desktop（startup_launch_current，input.c:829）
  - task snapshot/kill（GUI ioctl）：taskmgr（:341/:624）、doomlauncher（:183）、desktop
  - blockdev（POSIX，不动）：installer、gptinit（直接 open /dev/disk0）、diskmgr
  - OSS（POSIX，不动）：doom（leonos_audio.c:144）、mp3play、wavplay
  - evdev（POSIX，不动）：abittest
- 进程角色：desktop=shell+显示/外观策略发布者+输入法会话管理（**不是** ioctl 服务端，服务端在内核）；serviced=DHCP/NTP/服务状态（/var/run/services.*）；oschinpt=唯一输入法 provider。
- 回归锚点工具：`tools/test_security_regressions.py`（静态审计 GUI ioctl 不变量，:68/:107-138，改造后必须重写）；`tools/check_unix_paths.py`；`tools/check_abi_migration.py`（清单 136 符号）；`tools/test_gpu.py` + tools/tests/*（gpu ABI 边界）；`tools/qmp_terminal_smoke.py`（用户侧验收用）。

### 2.3 Unix socket 基础设施现状（kernel/ntclks/syscall_socket.c，336 行）
- AF_UNIX + SOCK_STREAM 全套：socket/bind/listen(backlog≤8)/connect/accept/getsockname/shutdown/send/recv；`getsockopt/setsockopt` 空操作；fork 引用计数正确（task_socket_retain/release）。
- 限制：32 socket 上限、每 socket 16KiB 环（**大流量不可走 socket——像素/文件必须走 shm/AF_INET**）；bind 路径只在内核内存表（`unix_find_path` :79-91），**不落 VFS**（ls 看不到、无权限位）；全非阻塞（空读 -EAGAIN、对端亡 0、满写部分写/-EAGAIN）；poll 已集成（:201-214）；无 socketpair/sendmsg/recvmsg/SCM_RIGHTS/SO_PEERCRED/accept4。

---

## 3. Phase 0：内核基础设施（全部后续工作的地基，一个或多个 commit）

### 5.1 阻塞化（复用 kernel/ntclks/wait.c，⚠ 先读它确认 API）
1. **AF_UNIX socket 阻塞语义**：read/connect/accept/write 在未设 O_NONBLOCK 时挂入等待队列睡调度器（`TASK_BLOCKED` + `sched_mark_ready` 唤醒；socket 状态迁移点即唤醒点：push/pop/peer close/connect 完成/listen pending 变化）。O_NONBLOCK 行为保持现状。唤醒必须同时覆盖 `poll()` 轮询路径（保留现有 poll 语义即可）。
2. **pipe 阻塞化**：task_pipe_read/write 同样接等待队列（syscall_ipc.c）。这是事件管道/stdio 的基础。
3. SMP 注意：等待/唤醒必须在 scheduler_lock 内或使用其约定的安全点；参考 `sched_wait_current_for_window_event`（sched.c:1518）的既有模式。

### 5.2 缺失的标准系统调用（include/uapi/linux/syscall.h 加号 → kernel 实现 → libc 封装 → uapi 头声明）
必须新增（Linux x86_64 标准号）：`__NR_socketpair(53)`、`__NR_sendmsg(46)`、`__NR_recvmsg(47)`、`__NR_accept4(288)`、`__NR_getuid(102)/geteuid(107)/getgid(104)/getegid(108)`、`__NR_setuid(105)/setgid(106)`、`__NR_uname(63)`、`__NR_gettimeofday(96)`、`__NR_settimeofday(164)`、`__NR_chmod(90)/fchmod(91)/chown(92)/fchown(93)`、`__NR_sched_setaffinity(203)/sched_getaffinity(204)`、`__NR_reboot(169)`、`__NR_pipe2(293)`、`__NR_dup3(292)`。
- sendmsg/recvmsg 至少支持 AF_UNIX 的 `SCM_RIGHTS`（fd 传递，窗口 shm 与监听 socket 传递需要）与 IOV不要求（可只支持 iovec_cnt=1 起步）。
- SO_PEERCRED：`getsockopt(SOL_SOCKET, SO_PEERCRED)` 返回 `struct ucred{pid,uid,gid}`，从对端 task 凭据填充——**authd/windowd/devmand 的安全基石**。
- setuid/setgid：仅 uid==0 进程可降权任意目标；非特权进程只能改回真实 uid（先支持最小语义）。
- reboot(2)：RB_AUTOBOOT/RB_HALT_SYSTEM/RB_POWER_OFF，仅 uid==0。
- libc：getuid 当前硬编码 0（posix_process.c:324-327）必须改为真系统调用；posix_compat.c 补 socketpair/sendmsg/recvmsg/accept4 包装。

### 5.3 共享内存设备（窗口零拷贝的前提）
- 新设备种类 `STORAGE_DEV_KIND_SHM` + devfs 节点 `/dev/shm0`（storage_vfs.c:17-58 注册；若需多实例参考 /dev/pts 动态展开 syscall.c:884）。
- 语义：`open("/dev/shm0", O_RDWR)` + `ftruncate(fd, size)`（上限 1920*1080*4 + 页对齐）分配内核缓冲；`mmap` 走 **fb0 同款设备映射路径**（syscall_mm.c:784-803 泛化：SHM 节点允许设备 VMA）；fork 后同缓冲；close 全部引用后回收。
- 不做文件 MAP_SHARED 页缓存共享（工程量大），shm 设备即可满足窗口需求；在代码注释注明这是 Linux memfd 的替代品。

### 5.4 /run 目录
- 在现有 ROOT 卷上约定 `/run/leonos/`（安装器根 make_installer_root.py 补目录；FAT/exFAT 无权限位，socket 权限靠服务端 SO_PEERCRED 校验，文档写明）。若想彻底干净可加 ramfs，但**非必需，别为它扩 storage 层**——按"约定目录 + 内存表 socket 路径"最小实现。

### 5.5 AF_INET 接入标准 socket 路径（数据面）
- `syscall_socket.c` 的 `socket()` 放行 `AF_INET + (SOCK_STREAM|SOCK_DGRAM)`，后端复用 net.c 的 `struct net_socket` 状态机（net.c:119-141）：socket fd 的 task_file 挂 `KERNEL_OBJECT_SOCKET`（或为 INET 新增 object 种类），read/write/poll/close 全部走 fd 通用路径。
- **消灭忙等**：net.c:2767-2788（connect）、2900-3000（send/recv）的自旋改为等待队列；net 收包推进改为调度器 tick 周期 `net_poll_once`（⚠ 复核现有 tick 调用点）+ 唤醒等待者。TCP 语义保持：阻塞 connect/recv、SOCK_DGRAM 用于 DNS。
- `getsockopt/setsockopt` 支持最小集：SO_REUSEADDR、SO_TYPE、SO_ERROR；`getsockname/getpeername` 返回 AF_INET 地址。
- 监听/accept 对 AF_INET TCP 生效（net.c 现无 LISTEN 状态机则补最小 LISTEN/SYN 队列）。
- libc：`leonos_socket_*`/`leonos_http_*`/tls.c 传输层整体切换到标准 socket API；DNS 解析器在 libc 实现（UDP:53，读 /etc/resolv.conf，由 netmand 写该文件）；HTTP 客户端纯 libc 化。

---

## 6. Phase 1-6：子系统迁移（每项：目标设计 → 步骤 → 删除清单 → 验收）

### 6.1 GUI → windowd + shm + procfs（最大件，Phase 1）
**目标**：窗口服务器从内核搬到用户态 `windowd`；应用与 windowd 之间是 AF_UNIX 控制协议；像素走 `/dev/shm0`；内核删除 gui_ipc.c 全部窗口/合成逻辑。

**协议设计**（/run/leonos/windowd.sock，版本握手后按消息类型分发）：
- 应用 → windowd：`HELLO{client_pid}`（SO_PEERCRED 校验）、`CREATE_WINDOW{w,h,flags} → {win_id, shm_path(`/dev/shm0` 句柄经 SCM_RIGHTS 传 fd), stride}`、`PRESENT{win_id, damage rects[]}`（窗口已通过 shm 直接绘制，此消息仅提交损伤）、`DESTROY_WINDOW{win_id}`、`SET_CURSOR_REGION{win_id, rects[]}`、`UPDATE_WINDOW{win_id, title, flags}`、`POLICY_*`（仅 desktop/sessiond 身份可用：DISPLAY_STATE/APPEARANCE/SET_TASKBAR/SET_MOUSE_VISIBLE）
- windowd → 应用：`EVENT{window_id, type=KEY/MOUSE/BUTTON/CLOSE/RESIZE/DIRTY/FOCUS, payload}`（应用 poll 控制socket）；`DISPLAY_REQUEST/APPEARANCE_REQUEST` 转发给策略客户端
- windowd 职责：读 `/dev/input/event0/1`（evdev）做焦点路由与键盘→窗口分发（接 imd）；mmap /dev/fb0 合成；窗口缓冲即 shm 段（每窗一个，SCM_RIGHTS 下发）；鼠标光标平面由 windowd 绘制。
- 权限模型：`require_window_server()` 的等价物 = windowd 是 fb0/shm 的唯一合成者；策略消息仅接受 desktop 的已认证连接（SO_PEERCRED pid + /proc/<pid>/cmdline 校验，procfs 就绪前用握手 token，token 由 serviced 在启动时传递——**选简单方案并记录**）。

**步骤**：① libc 新增 `libwind` 风格客户端（实现于 userland/libc，导出与现 `leonos_gui_*` 同名的函数，内部换 socket+shm——**应用零改动**，这是整个迁移平滑的关键）；② windowd 守护进程（先复用 kernel gui_ipc 的几何上限/事件语义）；③ desktop 改为 windowd 策略客户端；④ 各应用经 libc 兼容层自然切换；⑤ 删除内核侧。
**删除清单**：kernel/ntclks/gui_ipc.c、syscall_gui.c 的 0x4c47/0x4c46/0x4c44 认领、syscall.c:4456-5034 的 GUI 分支与 LEONOS_GUI_IOCTL_* 宏、`require_window_server`、`sched_wait_current_for_window_event/sched_wake_window_event`（确认无它用后）、syscall.c:56-82 对应宏；libc fd3 的 GUI 重定向分支。
**验收**：三个 build 绿；strict 通过（清单去掉 GUI 符号）；用户手测：桌面开机出桌面、开 Terminal（另一工作线问题除外）、窗口拖动/焦点/关闭、glxgears 帧率不劣化 50% 以上、taskmgr 进程列表来自 procfs。
**注意**：PRESENT 频率=应用损伤驱动；windowd 合成循环用 poll(fb 定时器/损伤事件)，避免空转烧 CPU；present 兼容路径（libc 在 shm 未就绪时逐块写 shm 本身就是原窗口缓冲）。

### 6.2 输入法 → imd（Phase 2）
- /run/leonos/input-method.sock；provider（oschinpt）连接后 `REGISTER_PROVIDER{id}` 成为常驻连接；应用侧 API `text_input_*`（libc inputm.c 改写）→ `SUBMIT_KEY{context}`；imd 转发给活跃 provider，provider 回 `RESULT{sequence, commit/preedit}` 推回应用连接。
- 状态（active provider、per-window context）由 imd 持有；desktop 的 `text_input_set_active/notify_config` 走 imd 协议；配置文件仍读 `~/.inputm.conf`。
- **删除**：kernel/ntclks/inputm.c、syscall.c:4380-4387 认领、/dev/input-method 节点（storage_vfs.c）、LEONOS_INPUTM_IOCTL_* 宏（userland/libc/include/leonos/inputm.h、devtools 同名头）、libc fd3 input-method 分支。

### 6.3 网络（Phase 0.5 已做数据面；此处管理面，Phase 3）
- netmand（并入 serviced）：/run/leonos/net.sock，消息 `GET_CONFIG/SET_CONFIG/RUN_DHCP/SET_DNS_POLICY/LIST_CONNECTIONS/PING`；写 /etc/resolv.conf、/etc/net.conf；PING 用 AF_INET SOCK_DGRAM ICMP（内核 net.c 支持 ⚠ 复核，否则保留内核 ping 原语但挂到 AF_INET 路径）。
- 应用迁移：netctl、httpget、browser、downloadmgr、apiapp、ping 改用 libc 新 socket API（多数经 libc 兼容层自动完成）。
- **删除**：LEONOS_IOCTL_NET_*（syscall.c:5178-5303、include/leonos/net.h 命令段）、/dev/net0 节点、libc fd3 net0 分支、net_service.c 的 net0 直连、libc.c:2294-2527 的 NET ioctl 族。

### 6.4 认证 → authd + setuid（Phase 4）
- authd（uid==0）：用户库从内核 `auth_user_scratch` 迁到 /system/config/users.db（格式沿用现有 scratch 结构或 JSON，含口令哈希）；协议 `AUTH{user,pass}→{uid,role,home}`、`CREATE_USER/UPDATE_USER/CHANGE_PASSWORD/LIST_USERS/ELEVATE`（ELEVATE 需已认证 root 会话）。
- 登录流：login（uid==0）连 authd 验证 → fork 子进程 → 子进程 `setuid(uid)` + `setsid()` + `setpgid` + exec shell/desktop 会话。task->uid 继续由内核持有（现有 authz_check_path 逻辑不动）。
- **删除**：auth_handle_ioctl（syscall.c:2090-2270）、auth_user_scratch、LEONOS_AUTH_IOCTL_*（devtools/include/leonos/auth.h 命令段）、libc.c:3912-4023 的 auth ioctl 族（保留同名 leonos_auth_* 导出改走 socket，应用零改动）。
- 验收：OOBE 建户→登录→文件归属正确；settings 改密生效。

### 6.5 会话/自启动/电源 → sessiond（Phase 4）
- startup 审批：内核 DB（syscall.c:113-151、2709-2900）迁到 /system/state/startup.db（libc 直读 + sessiond 仲裁写）；`REQUEST/DIALOG_GET/DIALOG_RESOLVE/LAUNCH_CURRENT` 走 sessiond socket（弹窗仍由 desktop 完成）。
- 电源：desktop/settings 的 reboot/shutdown 改 `kill(1, SIGTERM)` 风格 → serviced 会话收尾 → `reboot(2)`（uid==0）。**删除** GUI REBOOT/SHUTDOWN、STARTUP_IOCTL、libc.c:4040-4093。

### 6.6 设备/驱动 → devmand（Phase 5）
- /run/leonos/devman.sock：`DEVICE_LIST/DRIVER_LIST/DRIVER_CONTROL`（DRIVER_CONTROL 需 root，SO_PEERCRED 校验）。
- **删除**：/dev/hwinfo、/dev/driverctl 节点、syscall.c:5305-5386（DRIVER）、5388-5686（DEVICE_LIST）、devmgr_service.c 的 open(hwinfo/driverctl) 改 socket（保留导出函数名）。

### 6.7 系统信息/任务 → procfs（Phase 5）
- 内核新增只读 procfs（挂 /proc，实现于 storage 层一个 DEVICE/PROC 卷或专用 lookup 分支，参考 /dev/pts 动态展开）：
  `/proc/uptime`、`/proc/meminfo`、`/proc/version`（=uname 内容）、`/proc/<pid>/stat`（pid comm state ppid pgrp session + CPU ticks）、`/proc/<pid>/cmdline`、`/proc/self` 链接、`/proc/machine-id`。
- `uname(2)`/`gettimeofday(2)` 落地；PERF_INFO→taskmgr 读 /proc 聚合；TASK_AFFINITY→sched_setaffinity 系统调用。
- **删除**：LEONOS_IOCTL_SYSTEM_INFO/TIME_INFO/MACHINE_IDENTITY/PERF_INFO/TASK_AFFINITY、GUI TASKS/UPTIME_MS、LIST_DIR ioctl（libc readdir 已有）、TEXT_IOCTL_LAYOUT_UTF8（搬进 libc 纯函数，调用点 grep `leonos_text_layout`）。
- taskmgr/doomlauncher/desktop 的 task_snapshot/task_kill 改读 procfs + `kill(2)`。

### 6.8 清扫（Phase 6）
- fs ACL ioctl → 新增 chmod/fchmod/chown 系统调用 + storage 节点权限位（或直接映射现有 ACL 存储）；fileman/model.c:109、fileman/view.c:197 迁移；删 LEONOS_FS_IOCTL_*。
- signal legacy ioctl（LEONOS_SIGNAL_IOCTL_ACTION）删除（rt_sigaction 已全覆盖，grep `LEONOS_SIGNAL_IOCTL` 确认仅 libc 兼容壳）。
- audio legacy（/dev/audio0 + LEONOS_IOCTL_AUDIO_* + libc.c:2208-2286）删除；dsp OSS 不动。
- pty legacy（libc.c:4266-4404 的 leonos_pty_* 全家 + LEONOS_PTY_IOCTL_* 命令段 + syscall.c:5928-6019）删除；/dev/tty 重定向分支删除。
- kernel-debug ioctl → /system/state/kernel-debug 文件 + /dev/kmsg 读；tools 同步。
- **fd3 终结**：删 libc.c:485-613 重定向、leonos_framebuffer_fd 的 fd3 回落（libc.c:1611-1621）、内核所有 fd==3 豁免（inputm 4383、audio 5343、fstat 3061-3065、poll 3166、gui/gpu 的 fd==3 分支 syscall_gui.c:20-33）。device.h 注释同步（:26-51）。
- devfs 清理：storage_vfs.c:17-58 移除 net0/hwinfo/driverctl/input-method/audio0（保留 shm 新节点）；include/leonos/device.h 对应宏删除。

---

## 7. libc 收尾清单（§6 各步的汇总断言）
- `grep -rn "ioctl(3" userland/libc/src/` 零命中；`grep -rn "LEONOS_.*_IOCTL" userland/libc/src/ | grep -v "TCGETS\|TIOC\|SNDCTL\|EVIO\|BLK\|FBIO\|GPU\|0x4c475"` 零命中。
- `include/leonos/gui.h` 等公共头：函数签名不变（应用零改动），仅实现层换传输；devtools/include/leonos/ 下对应头同步删除命令宏。
- tools/check_abi_migration.py 清单更新：加入本次删除的全部符号族，保证"已删除不复活"。

## 8. 工具与测试同步
- `tools/test_security_regressions.py`：重写为断言新不变量——内核源码中禁止出现 `LEONOS_GUI_IOCTL|LEONOS_AUTH_IOCTL|LEONOS_IOCTL_NET|LEONOS_INPUTM_IOCTL|LEONOS_STARTUP_IOCTL|LEONOS_FS_IOCTL|LEONOS_IOCTL_AUDIO|LEONOS_IOCTL_DEVICE_LIST|LEONOS_IOCTL_DRIVER_|LEONOS_TEXT_IOCTL|LEONOS_IOCTL_LIST_DIR|LEONOS_KERNEL_DEBUG_IOCTL`；windowd/authd/netmand 源码中必须存在 SO_PEERCRED 校验；setuid/reboot 的 uid==0 门。
- `tools/check_unix_paths.py`、`tools/test_gpu.py`+tools/tests/*：适配 /dev/gpu 收窄后的语义。
- `tools/make_installer_root.py`：补 /run/leonos、/etc/resolv.conf、/etc/machine-id、/system/config/users.db 初始结构、/proc 挂载点（若 procfs 需要挂载语义则由内核固定，无需挂载调用）。
- `tools/qmp_terminal_smoke.py` 不改（用户验收用）。

## 9. 执行顺序与阶段验收
1. Phase 0（§5）：等待队列阻塞化 + 新系统调用 + SO_PEERCRED/SCM_RIGHTS + shm 设备 + AF_INET fd 化。**验收**：build×3 绿；写一个 userland 自测小程序（tools/tests/ 下，用户可手跑）验证 socketpair 阻塞读写、SCM_RIGHTS、shm mmap、AF_INET connect。
2. Phase 1（§6.1 GUI）：最大风险件，单独成串 commit，保持每步可编译。
3. Phase 2/3（imd、net 管理面）。
4. Phase 4（authd+setuid、sessiond）。
5. Phase 5（devmand、procfs+uname+gettimeofday）。
6. Phase 6（清扫+fd3 终结+工具同步+清单更新）。
每阶段输出：变更摘要、新增协议文档（docs/ipc-<svc>.md 或头文件注释）、用户手测清单、strict 检查结果。

## 10. 风险与已知坑
- **IPC 大流量**：unix socket 16KiB 环 —— 任何像素/文件传输严禁走控制 socket（present 必须走 shm，这是硬设计约束）。
- **SMP 锁序**：socket/pipe 等待队列唤醒与 scheduler_lock 的顺序，参考 wait_window_event 既有模式；等待中持锁返回属 bug。
- **desktop 权限**：TASK_FLAG_WINDOW_SERVER 语义随 GUI ioctl 删除而消亡；windowd 的策略信任改为 SO_PEERCRED+握手 token，务必防"任意进程连 policy 通道"。
- **auth 时序**：setuid 前内核 task->uid 仍由旧 LOGIN 路径设置——authd 切换期间保证 login 流程原子（验证→fork→setuid→exec），避免出现 uid==0 的 shell 逃逸。
- **兼容性破坏明示**：fd3 机制删除会 break 一切旧静态二进制（installer root 内的应用会全部重编，无碍；但 devtools 下若有历史产物需重编）。在 commit message 中明示。
- **net 忙等删除后**的收包路径延迟：tick 周期 poll 的粒度决定 RTT；如性能不可接受，再评估定时器粒度或键盘中断式推进，但先简单后优化。
- **不要动**：pty dup2/controlling-tty 语义、rt_sigreturn trampoline、Terminal 主循环、blockdev/OSS/evdev 三个已 POSIX 化面。

## 11. 交付物
1. 全部 commit 推送到工作分支，commit 均通过三构建。
2. `docs/unix-ipc-protocol.md`：6 个服务 socket 的消息协议表（消息名/字段/方向/权限）。
3. `tools/test_security_regressions.py` 新版 + check_abi_migration 清单更新。
4. 阶段状态表（每子系统：删除的宏/文件数、新增 socket 服务、遗留 TODO）。
5. 用户手测清单（QEMU 或真机）。
