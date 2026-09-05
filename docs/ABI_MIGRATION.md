# LeonOS 4 POSIX/Linux ABI 迁移表

本文档是 ABI 迁移的唯一登记表。迁移期间旧接口可以继续存在于内核和
libc，但新应用不得增加对私有硬件入口的依赖。Linux 兼容范围是 LeonOS
明确实现的 x86_64 子集，不包含任意 glibc 二进制兼容承诺。

## 目标层

| 现有接口/实现 | 目标接口 | 当前状态 | 说明 |
| --- | --- | --- | --- |
| 文件读写、目录和进程 | POSIX libc + Linux syscall | 进行中 | 标准 `stat/fstat/lstat` 已返回完整 POSIX `struct stat`；第一方内部查询暂用显式命名的 `leonos_*_legacy`；`openat(AT_FDCWD)` 已接入，目录 fd 相对解析仍待完成 |
| `leonos_pty_*` 和私有 PTY ioctl | Unix98 PTY + `termios`/`TIOCGWINSZ` | 进行中 | libc 已提供 `posix_openpt/openpty/forkpty`；新的程序应使用 `/dev/ptmx`、`/dev/pts/<id>`、`/dev/tty`，旧入口仅作过渡 |
| `leonos_socket_*` 网络包装 | POSIX socket fd | 进行中 | `AF_UNIX/SOCK_STREAM` 已接入 socket syscall、FD 生命周期和 poll；IPv4 网络仍由过渡层驱动 |
| `LEONOS_GUI_IOCTL_*` | 版本化 GUI IPC + SDK GUI 库 | 设计完成，迁移中 | GUI 不属于 POSIX，内核 ioctl 仅作为过渡实现 |
| 帧缓冲私有 ioctl | Linux fbdev UAPI (`/dev/fb0`) | 基础完成 | `/dev/fb0` 支持 mmap 以及 `FBIOGET_VSCREENINFO`、`FBIOGET_FSCREENINFO`、`FBIOPUT_VSCREENINFO`；绘制扩展仍由 GUI 服务负责 |
| 原始键盘和鼠标输入 | evdev (`/dev/input/event*`) | 基础完成 | `/dev/input/event0` 是键盘、`event1` 是鼠标；独立 FD 游标支持 `read`、`O_NONBLOCK`、`poll` 和常用 `EVIOC*` 查询 |
| 输入法管理私有 ioctl | 版本化 GUI 文本输入服务 | 迁移中 | 输入法不是硬件事件设备；过渡期改走 `/dev/input-method`，不再借用 `event0`，后续移入 GUI Unix socket 协议 |
| 音频私有 ioctl | OSS `/dev/dsp` | 基础完成 | 16-bit little-endian stereo playback，首版不引入 ALSA ABI |
| 磁盘/分区私有 ioctl | `/dev/*` 块设备 + Linux 风格 ioctl | 基础完成 | BusyBox、installer、gptinit 和 diskmgr 使用 `/dev/diskN[pN]`、`BLK*`、原始对齐 I/O 与 `mount(2)`；旧公开 ioctl 已移除 |
| `leonos_device_list` | `/dev` 枚举、`stat`、设备服务 IPC | 进行中 | `/dev` devfs 已提供稳定节点，设备列表 ioctl 待淘汰 |
| 私有 signal ioctl | `rt_sigaction`/`rt_sigprocmask` | 进行中 | 当前只完整支持默认/忽略处置，用户 handler frame 仍待实现 |

## 统一 UAPI

内核和用户态共同使用 `include/uapi/linux` 下的定义，禁止在模块内重复
声明 syscall 编号、evdev/fbdev/tty 结构或 ioctl 编码。当前目录包括：

- `linux/syscall.h`：x86_64 syscall 编号和 `SYS_*` 别名
- `linux/types.h`：固定宽度 UAPI 类型
- `linux/ioctl.h`：Linux `_IOC` 编码
- `linux/tty.h`：`winsize` 和标准 TTY 请求
- `linux/input.h`：evdev 事件布局
- `linux/fb.h`：fbdev 基本屏幕信息结构
- `linux/soundcard.h`：OSS PCM 设备请求和缓冲区布局
- `linux/fs.h`：块设备容量、扇区大小和分区表刷新请求
- `linux/socket.h`：Unix/IPv4 socket 地址、类型和 shutdown 定义

构建系统将该目录加入内核、libc、应用和 SDK 的 include 搜索路径。SDK
打包后以 `include/linux/*` 导出。

## 迁移规则

1. 新应用只能使用 POSIX/Linux 标准接口、统一 GUI 库或明确的系统服务 IPC。
2. 不得新增 `leonos_*` 硬件访问函数、fd 3 控制通道或新的 `LEONOS_*_IOCTL`。
3. 设备必须先在 `/dev` 注册，再由标准 `open/read/write/poll/ioctl` 访问。
4. 旧接口只有在迁移表中登记且属于内核内部、libc 过渡层或尚未迁移的系统应用时才可保留。
5. 删除旧 ABI 前，先启用严格检查、完成所有消费者迁移，再提升 ABI 版本并移除实现。

## 阶段状态

- [x] ABI/UAPI 清点并建立唯一迁移表
- [x] 统一 syscall 编号来源，加入 `openat(AT_FDCWD)` 入口
- [x] 将 UAPI 头文件纳入内核、libc 和 SDK 构建
- [x] 普通文件、pipe、PTY、Unix socket 接入内核 `poll`
- [x] Unix98 PTY 基础 master/slave fd、标准 winsize/termios ioctl
- [x] POSIX `stat/fstat/lstat` 结构和错误语义，保留显式 legacy 查询入口
- [x] `/dev/fb0` 基础 Linux fbdev UAPI 和 framebuffer mmap
- [ ] `rt_sig*` 用户 handler frame 和完整 Unix98 PTY hangup 语义
- [x] evdev `/dev/input/event*` 原始读写、非阻塞、`poll` 和基础 `EVIOC*` 查询
- [ ] evdev 独占抓取、热插拔和完整能力/状态位图
- [x] OSS `/dev/dsp` 音频设备接口：`SNDCTL_DSP_SETFMT`、`CHANNELS`、`SPEED`、能力/缓冲区查询、非阻塞写入和 `poll(POLLOUT)`
- [x] 块设备 `/dev/diskN`、`/dev/diskNpN` 原始扇区读写和 `BLKGETSIZE64`、`BLKGETSIZE`、`BLKSSZGET`、`BLKROGET`、`BLKRRPART`
- [x] 磁盘工具迁移：BusyBox、installer、gptinit、diskmgr 使用块设备和 `mount(2)`
- [ ] 应用迁移完成并启用严格旧 ABI 检查（Terminal、通用启动器、TTY OOBE/login、bugtest 已迁移）
- [x] 删除公开磁盘 ioctl 和过渡头文件；内部启动期存储辅助代码不导出给用户态

## Socket 子集

libc 导出 `<sys/socket.h>`、`<sys/un.h>`、`<netinet/in.h>` 和
`<arpa/inet.h>`。当前内核已支持 `AF_UNIX/SOCK_STREAM` 的标准文件描述符
语义：`socket`、`bind`、`listen`、`connect`、`accept`、`send`、`recv`、
`shutdown`、`getsockname`、`poll`，并在 `fork`、`dup`、`close` 和进程退出时
维护引用计数。Unix socket 路径是内核端点命名空间，适用于 tmux 等本机 IPC；
IPv4 网络仍使用现有网络服务过渡接口，待后续迁移。

## OSS 音频子集

`/dev/dsp` 是标准的 OSS PCM 输出节点。当前只支持
`AFMT_S16_LE`、双声道和 8-48 kHz 采样率；`read` 不支持，因为首版没有
录音设备。`SNDCTL_DSP_SETFMT`、`SNDCTL_DSP_CHANNELS`、
`SNDCTL_DSP_SPEED`、`SNDCTL_DSP_GETFMTS`、`SNDCTL_DSP_GETCAPS`、
`SNDCTL_DSP_GETBLKSIZE`、`SNDCTL_DSP_GETOSPACE`、
`SNDCTL_DSP_GETODELAY` 和 `SNDCTL_DSP_NONBLOCK` 已实现。应用应通过
`write` 提交 PCM，并用 `poll(POLLOUT)` 或 `EAGAIN` 处理队列饱和。

`/dev/audio` 与 `/dev/audio0` 仅用于仍使用旧私有音频 ioctl 的二进制兼容，
新应用和 SDK 示例必须使用 `<linux/soundcard.h>` 与 `/dev/dsp`。
