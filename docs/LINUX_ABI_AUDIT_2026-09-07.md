> 2026-09-10 ioctl(FIOCLEX/FIONCLEX) 通用描述符语义修复：普通文件不再返回
> `-ENOSYS`。返修后宿主内核 ASan/UBSan 单测通过，宿主 Linux 与 QEMU 各 38 组通过，
> 未修改的静态 musl CPython 3.15 `python3 /bin/hello.py` 打印完整结果并退出 0。
> VMware 未验证；ioctl 整项仍为“有分发、未认证兼容”。见下文
> “2026-09-10 通用 ioctl close-on-exec 修复”。
> 2026-09-10 最新检查点：默认 userland、普通镜像、installer 和 SDK 已切换至 musl+mimalloc；Vim/ncurses 已纳入正式镜像。
> Picolibc 源码、端口、旧运行库、私有链接参数及旧标准头文件已移除；
> 静态 musl GCC 15.1.0/binutils 2.44 已正式纳入默认桌面 ISO、VMDK 和 installer；
> 最终 ISO 的 QEMU Terminal 编译/执行与安装器前四页验证通过，完整落盘安装及
> VMware 尚未验证，不能据此提升 syscall 整项状态。证据见进度清单末节。
> `musl-only-release-build.log` 验证普通镜像、installer 与 SDK 全量构建通过。
> CSV 当前缺少分发 115 项；新增入口及局部行为实现不表示整项兼容。
> 后续严格回归修正 vfork 的备用栈继承/提前唤醒/退出顺序及缺页 SIGSEGV；
> 旧测试接受 exit(14)，不能作为 Linux 故障信号证明。未触页共享匿名 mmap
> 在 fork 后的共享缺口仍未修复，详见进度清单首节。
> 预构建静态 musl GCC 15.1.0 的 `clone(CLONE_VM|CLONE_VFORK|SIGCHLD)`
> 阻塞及 `RLIMIT_STACK` 缺失已在 2026-09-10 修复；QEMU 中 `--version/-v/-E/-c/
> -static 链接/运行生成程序` 全部退出 0，生成 stdio 程序输出经校验，
> `[gcc-probe] DONE failures=0`。详见进度清单“CLONE_VFORK/RLIMIT_STACK 修复
> 与预构建 musl GCC 全链路”。这只是所测路径通过，不等于整个 clone/rlimit ABI 完整。
> 原本误列为缺失的 `sync`/`syncfs` 已改为“有入口但未完整认证”；
> 完整审计范围保持不变，不能据此计算兼容率。
> `session-notify-probes-serial.log` 静态/动态各 49 组通过；包括 forkpty、会话身份、
> SIGCHLD 通知、pthread 和 Unix socket 已有用例。Linux 6.12 参考 43 组通过。
> 默认 Terminal/nano 已恢复；设备内 TCC 生成的 musl 程序执行退出 0；
> installer 可到 Thanks。落盘安装/更新、未启用组件和 VMware 运行验证仍未完成。
> 下文 Picolibc 描述及已删除文件链接是审计历史，不是当前依赖。

# Linux x86-64 ABI 静态审计

- 日期：2026-09-07。
- LeonOS 提交：`c1fec352f8d67851be9773f074484d3e6d5d9e7a`。
- 对照基线：Linux **v6.12 native x86-64**，只统计 syscall_64.tbl 中 `common` 和 `64`，排除 x32、i386 和没有编号的空洞。这是固定版本的覆盖审计，不是“当前最新 Linux”的调用数量。
- 方法：检查真实 Ring 3 trap 分发、类别转发和实际处理函数，再与官方 Linux UAPI/实现及 musl v1.2.5 初始化路径对照。官方源码通过 `http://127.0.0.1:12334` 获取。
- 初始报告是静态审计；截至 2026-09-09，已有源码修复、宿主测试、真实 musl 静态/动态程序和部分 LTP 的 QEMU 结果。VMware 尚未验证。逐项证据及完整剩余范围见 `docs/LINUX_ABI_PROGRESS_2026-09-08.md`。
- CSV 仍覆盖 Linux v6.12 native x86-64 的 375 个编号。当前状态为：115 项 `missing_dispatch`、5 项定时器 `completed`、81 项 `routed_not_certified`、157 项 `implemented_pending_runtime`、17 项 Linux 保留/ni。新增 capability、`flock`、时间戳族、`sched_attr`、`rseq`、`futex_waitv`、symlink、sigqueue、mmsg、signalfd、process_vm、SysV 消息队列/信号量族、真实 `CLONE_VFORK`/`vfork` 生命周期和 `RLIMIT_STACK` 已有 native 分发或实现，但完整行为及运行证据仍未完成。部分调用通过定向测试，不表示整项兼容；`verification_scope` 和 `verification_evidence` 单独记录验证边界。

## 本轮实际修复与状态

### 2026-09-10 通用 ioctl close-on-exec 修复（`FIOCLEX`/`FIONCLEX`）

**已证实的缺口**：静态 musl CPython 3.15 执行 `python3 /bin/hello.py` 报
`can't open file '/bin/hello.py': [Errno 38] Function not implemented`。
串口证据（`/home/xiaobai/installer-serial.log` 第 1645 行附近）：`nr=2 open`
返回 fd 9，紧接着 `nr=16 args=9,5451,0,... result=-38`（`0x5451` 即
`FIOCLEX`），随后 `close(9)` 与 `exit_group(2)`；同一个二进制在宿主 Linux 上
`ioctl(FIOCLEX)` 成功。根因是 `kernel/ntclks/syscall.c` 只在 signalfd 分支
处理这两个请求，普通文件落到默认 `-ENOSYS`。这是已有 ioctl 入口内的子命令/
描述符语义缺口，不是缺失 `open` 或整个 ioctl 入口。

**代码修改**

- `kernel/ntclks/syscall.c`：新增 `task_fd_descriptor_flags()`/
  `task_fd_set_descriptor_flags()`，把 FD_CLOEXEC 统一存放在描述符表项
  （`task_file.fd_flags`）、PTY 别名（`task_pty_fd.flags`）或隐式 stdio
  （`cloexec_stdio_mask`）三处真实存储；`syscall_ioctl_descriptor_flags()`
  实现 `FIOCLEX`/`FIONCLEX`，第三个参数根本不进入接口，因此不会被校验或解引用。
  通用处理挂在 `syscall_dispatch_regs()` 中类别/GPU/设备分发之前，凡
  `fdget()` 能解析的描述符（普通文件、目录、pipe、socket、PTY、设备、匿名
  inode）都生效；无效 fd 返回 `EBADF`，其它请求不伪造成功。
- `syscall_ioctl_resolve_fd()`：核对 O_PATH 特殊规则。Linux 的 `ioctl()`
  用 `fdget()`，它拒绝 `FMODE_PATH`，所以 O_PATH 描述符上任何 ioctl 都是
  `EBADF`，而 `fcntl()` 用 `fdget_raw()` 仍可读写 FD_CLOEXEC。因
  `LINUX_O_PATH` 与内部 `TASK_FILE_FLAG_EPOLL` 数值相同，只在 `open/openat`
  用户 flags 入口把该位翻译为 `TASK_FILE_FLAG_PATH`；通用分配器保留内部
  epoll 类型位。所有 ioctl 在命令分发前验证 fd 存在性，无效 fd 返回 `EBADF`。
- signalfd 分支删除重复实现，改为复用通用路径（该分支原来的
  `task_descriptor_for_fd()->fd_flags` 写法在异常路径上还可能解引用空指针）。
- 未识别的 ioctl 请求不再落到整个分发函数末尾的 `-ENOSYS`：Linux 通用层在
  `do_vfs_ioctl()` 与设备 `->unlocked_ioctl` 都不认识请求时返回 `ENOTTY`，
  现有 evdev/OSS/socket/PTY 后端也都用该错误码。只修正错误码，不新增设备实现。
- `fcntl(F_GETFD/F_SETFD)` 改用同一组辅助函数，隐式 stdin/stdout/stderr 的
  标志不再“返回成功但不保存”；`dup()`/`dup2()`/`dup3()`/`F_DUPFD_CLOEXEC`
  语义保持不变，并补齐 `dup2()` 落到 fd 0..2 时清理 `cloexec_stdio_mask`
  的陈旧位（否则复用该 fd 号会带着旧标志进入 exec）。
- `kernel/ntclks/user/userland.c`：新增仅用于诊断 ISO 的
  `autospawn=ioctlcloexec` 与 `autospawn=python315` 钩子，使回归程序和
  未修改的 CPython 在无 GUI 输入的情况下运行并把自身 stdout 写进串口。

**验证证据（按层次区分）**

| 层次 | 内容 | 结果 |
| --- | --- | --- |
| 宿主内核单测 | `tools/tests/ioctl_cloexec_table_test.c` 直接编译真实 `kernel/ntclks/syscall.c`，ASan/UBSan：设置/清除/重复、dup 隔离与共享 OFD、PTY 别名、隐式 stdio 存储与 fd 复用、无效 fd/负 fd/超界 fd、未知命令 `ENOTTY`、fd 与 cmd 32 位截断、O_PATH `EBADF` | PASS，无泄漏 |
| 宿主 Linux 对照 | `tools/tests/linux_ioctl_cloexec_test.c`（静态 musl，raw syscall）在宿主 Linux 运行，同一二进制随后进来宾 | 38 组检查，0 失败，0 跳过 |
| QEMU/KVM 来宾 | `[ioctl-clex] DONE checks=38 failures=0 skips=0`，含原有 32 项、epoll 注册/等待/dup/exec、无效 fd 对未知及 TTY 命令返回 EBADF、open 忽略 bit 2 | PASS |
| QEMU/KVM 来宾（Python） | 未修改 `build/python315-stdlib-test/python/bin/python3.15`（静态 musl，完整标准库）执行 `python3 /bin/hello.py`：串口出现 `Hello from Python 3 on LeonOS!`、`Numbers:`、`Sum: 55`、`Fibonacci:`，`name=python3.15 code=0`，且 `nr=16 args=3,5451,... result=0` | PASS |
| QEMU/KVM 桌面 Terminal | OOBE、登录后通过开始菜单启动 Terminal，输入 `python3 /bin/hello.py`；PTY 1 中 pid 22 退出 0，`build/ioctl-cloexec/terminal-python.png` 显示完整结果和返回 shell | PASS |
| 头文件/契约 | `FIOCLEX=0x5451`、`FIONCLEX=0x5450`、`FD_CLOEXEC=1` 与 Linux v6.12 一致；通用分发顺序、fcntl 共用存储、signalfd 不再私有实现由 `tools/test_linux_ioctl_cloexec.py` 断言 | PASS |
| VMware | 未执行 | 待验证 |

证据文件：`build/ioctl-cloexec/evidence.txt`（含宿主探针完整输出、来宾关键行、
kernel.sys/探针/ISO 的 sha256）、`build/ioctl-cloexec/guest-serial.log`（串口 +
syscall trace）、可测试 ISO `build/ioctl-cloexec/leonos4-ioctl-cloexec.iso`。

**返修说明**：最初 32 项测试遗漏了 epoll，`alloc_task_fd()` 的 O_PATH 转换
误清除了内部 EPOLL 类型位，导致 epoll 操作失败并漏释放对象；原 ioctl fd
解析也未拒绝不存在的 fd，未知命令错误返回 ENOTTY。新增内核单测分别复现
失败后修复；ASan/UBSan 检查 epoll 识别和关闭释放，raw 探针验证实际分发。
修复前证据保留在 `build/ioctl-cloexec/evidence.before-rework.txt` 和
`guest-serial.before-rework.log`，不能用其 32 项通过结论认证 epoll。

**未完成与边界**

- ioctl 整项**不得**标为兼容：其余 socket/设备/文件 ioctl 请求仍按各自后端
  返回 `ENOTTY`/`EINVAL` 等，未逐项认证；本轮只修复已证实的通用子命令缺口。
- O_PATH 只实现“ioctl `EBADF` + fcntl 可用”这一条 Linux 规则；`read`/`write`/
  `fchdir` 等其余 FMODE_PATH 限制仍未实现，`openat2(2)` 带 `O_PATH` 仍返回
  `EOPNOTSUPP`（Linux 接受该组合），`open()` 对 O_PATH 的构造尚未与其它
  O_PATH 语义完全对齐。
- `i386`/`x32` compat ioctl 不在本轮范围；`FIOCLEX`/`FIONCLEX` 之外
  （如 `FIONBIO`、`FIOASYNC`）仍只在 signalfd/部分设备分支实现。
- VMware 启动、完整 LTP ioctl 用例、SMP 压力下的并发描述符复用未验证。

2026-09-10 修复 `clone(CLONE_VFORK)`/`vfork(58)` 生命周期和
`getrlimit(97)`/`setrlimit(160)`/`prlimit64(302)` 的 `RLIMIT_STACK`，并支持无
`PT_INTERP` 的 static PIE（预构建 `as`/`ld` 所需）。`CLONE_VM` 为真实共享
地址空间，父线程按 Linux `wait_for_vfork_done` 的 killable 语义等待子进程
exec 提交或最终退出，exec 失败不提前唤醒；被捕获的非致命信号保持 pending，
父退出/子异常退出/回滚/任务槽复用有显式生命周期处理。RLIMIT_STACK 默认
`{8 MiB, RLIM_INFINITY}`，线程共享、fork 继承、exec 保留，并按
`acct_stack_growth` 的“扩张后完整栈跨度 > rlim_cur”规则实际约束缺页，
不再只返回固定值或接受设置不实施。预构建 GCC 15.1.0 在 QEMU/KVM 1 vCPU 和
2 vCPU 冒烟中完成版本、预处理、编译、static 链接和生成 stdio 程序执行，
`[gcc-probe] DONE failures=0`；宿主 raw 回归、pthread/fork/exec/wait/信号和
RLIMIT_STACK 定向测试见 `build/host-regressions-20260910.log` 与
`build/gcc-probe/guest-serial*.log`。clone 的 pidfd/ptrace/namespace/io/time 等
标志和 clone3 特有字段、其余 12 个 rlimit 资源、VMware/完整 LTP/多核压力仍
未完成；局部通过不将任一整项标为 completed。

2026-09-10 增加 `semget(64)/semop(65)/semctl(66)/semtimedop(220)`：真实数组、
原子向量/回滚、阻塞快照与唤醒顺序、权限/控制命令、SEM_UNDO/CLONE_SYSVSEM、
退出调整、超时/EINTR 和 group stop 后不重启。x86-64 104 字节状态布局经 musl
对照；ASan/UBSan、宿主 raw syscall、真实调度/信号帧测试及构建通过。
namespace、procfs/sysctl、unshare、高精度定时、完整并发和 guest 验证仍未完成，
具体证据和边界见进度清单的“SysV 信号量实现检查点”。

2026-09-10 增加 `msgget(68)/msgsnd(69)/msgrcv(70)/msgctl(71)`，实现真实 SysV
消息队列、owner/group/capability 权限、类型选择、COPY、容量和状态统计、阻塞
快照、直接交付、SET/RMID 唤醒及信号/退出清理。内核 ASan/UBSan 与宿主 raw
对照、信号帧/调度回归、musl 布局和内核/SDK/探针构建通过。namespace、procfs、
sysctl、LSM、完整并发和 guest 验证仍未完成，详见进度清单的 SysV 检查点。

2026-09-10 增加 `process_vm_readv(310)/process_vm_writev(311)`，按两端页表
执行真实跨进程复制、VMA 权限/COW、远端页引用、部分结果及 REALCREDS/dumpable/
capability 检查，并为现有凭据修改路径补充 dumpability 重置。内核页表 ASan/UBSan、
宿主 raw syscall 对照、相关回归及内核/musl 探针构建通过。memfd 映射、共享文件
写回、完整地址范围、namespace/LSM、并发和 guest 执行仍未完成，详见进度清单。

2026-09-10 增加 `signalfd(282)/signalfd4(289)`，读取真实私有/进程信号队列。
实现共享 mask、read/readv 的记录/故障消费语义、阻塞向量快照、唤醒与信号恢复顺序，
接入 poll/epoll、dup、ioctl、CLOEXEC 和 native 128 字节输出布局。内核定向测试、
宿主 raw syscall 对照、musl ABI 检查及内核/探针构建通过。匿名 inode 元数据、
procfs、其他信号源、完整并发及 LeonOS 执行仍未完成，详见“signalfd 实现检查点”。

2026-09-10 增加 `sendmmsg(307)/recvmmsg(299)`，接入实际 Unix socket 消息后端。
实现批量进度、部分成功、短发送、WAITFORONE、timeout 写回和 SO_ERROR 延迟错误；
修正消息头输出顺序、iovec 导入、数据报复制失败的消费行为以及信号中断返回消息数。
跨任务 copyout 复用真实缺页处理，支持信号帧和接收结果的惰性页/COW 写入。
内核、musl 探针构建及定向宿主测试通过；INET、完整阻塞参数快照、流式/辅助数据
复制故障、竞争和 guest 执行仍未完成。详见进度文件的“消息批量调用实现检查点”。

2026-09-10 增加 `rt_sigqueueinfo/rt_tgsigqueueinfo` 及真实 siginfo 队列，
接入每 UID 的 RLIMIT_SIGPENDING、信号帧、sigtimedwait 和 clone/exec/exit 生命周期。
修正分发层错误重试 EAGAIN、等待输入/输出故障及信号栈故障处理。
宿主 raw syscall 对照、内核队列 ASan/UBSan、帧和资源限制测试、内核及 musl
探针构建通过。POSIX timer 混合队列、其他信号源、init/ptrace/namespace 和
多核/guest 验证仍未完成；详细范围见进度文件的“siginfo 队列实现检查点”。

2026-09-10 增加 ext2 符号链接对象，接入 `symlink/symlinkat/readlink/readlinkat`，
修正参数顺序、内联/块存储边界、路径组件展开、nofollow、目录引用计数和大小写查找。
真实 ext2 的宿主 ASan/UBSan 测试、三个文件系统镜像的 e2fsck 检查、权限路径测试
与内核编译通过。完整 pathname 长度、O_PATH、最终 unlink 后 fd 引用、完整权限及
时间戳等缺口仍未完成，详细证据见进度文件的“符号链接实现检查点”。本批未启动虚拟机。
旧 hard-link 文档将目录链接计数误作打开 fd 生命周期，已据 `ext2_unlink` 源码订正。

2026-09-10 新增 `futex_waitv`（449）native x86-64 分发。实现最多 128 个 Linux
24 字节描述符、U32/private flags、用户地址和保留字段校验、单调/实时绝对超时，
并让一个任务同时挂在多个 futex key 上；唤醒返回触发描述符索引，支持 EAGAIN、
EINTR、ETIMEDOUT。`python3 tools/test_linux_threads.py` 中 futex 队列、进程信号和
CPU barrier 三项通过，`python3 build.py run kernel`、`run musl-probes`、
`test linux-abi-contract` 和 `test uapi` 通过。完整 QEMU guest 探针尚未认证：
标准 QMP 桌面测试在 OOBE 因物理内存耗尽反复失败，`qmp-abittest-serial.log`
未启动 ABI 程序；重复 key、取消、requeue 交互和调度压力继续标为待验证。

2026-09-09 按用户要求，将 CSV 中 5 项 `implemented_pending_validation` 定时器调用改为 `completed`（任务完成）。该状态调整未新增测试，不等同于完整 Linux ABI 已验证；原有 `partial_guest` 范围和未覆盖项保持不变。历史日志和截图已按用户要求清理，证据列中的路径仅作为历史运行记录。

状态定义：`implemented_pending_runtime` = 本轮新增了实现，但完整运行语义仍待验证或补齐，可有局部 QEMU/LTP 通过证据；`routed_not_certified` = 原有处理入口仍未证明完整 Linux 语义；`missing_dispatch` = 当前仍无 Linux 分发入口；`linux_reserved_or_ni` = Linux 自身保留或 `sys_ni_syscall`。B01-B46 另按未处理、已实现待验证、已验证、具体阻塞记录，没有把编译成功当作 ABI 兼容认证。

已实现待运行验证的入口包括：`pause`(34)、`lstat`、`pread64`/`pwrite64`、`readv`/`writev`、`preadv`/`pwritev`、`preadv2`/`pwritev2`、`access`、`fsync`/`fdatasync`、`truncate`、`fchdir`、`umask`、`arch_prctl`、`gettid`、`futex`、`getdents64`、`set_tid_address`、`clock_getres`、`clock_nanosleep`、`time`、`getcpu`、`close_range`、`statx`、`sendfile`、`copy_file_range`、`eventfd`/`eventfd2`、`setreuid`/`setregid`、`setresuid`/`getresuid`、`setresgid`/`getresgid`、`exit_group`、`newfstatat`、`getrandom`、`personality`、`rt_sigtimedwait`、`epoll_create`/`epoll_create1`、`epoll_ctl`、`epoll_wait`、`epoll_pwait`、`epoll_pwait2`、`timerfd_create`、`timerfd_settime` 和 `timerfd_gettime`。这些行已在 CSV 中从缺失/冲突改为 `implemented_pending_runtime`，源码位置和验证边界以 CSV 为准。

权限阶段另新增 12 个入口：`lchown`、`getgroups`/`setgroups`、`statfs`/`fstatfs`、`mkdirat`、`fchownat`、`unlinkat`、`fchmodat`、`faccessat`/`faccessat2`、`fchmodat2`。owner/group/other、32 位 UID/GID、umask、sticky 目录、附加组、目录遍历以及 chmod/chown 已有实际实现；FAT/exFAT 使用持久化元数据，ext2 写真实 inode。旧 ACL 记录按需迁移，文件属性页和 BusyBox/Picolibc 消费者同步修改。

账户服务现导出标准 `/etc/passwd`、`/etc/group`，普通 QEMU 桌面已验证按用户名执行 chown。保留现有 UID，不把名字为 root 的 UID 1 账户变成 UID 0。实际账户更新暴露并修复了同目录 rename 不能覆盖目标及覆盖后读取旧权限记录的问题；FAT32 原生 musl 用例和 ext2 的 debugfs/e2fsck 检查通过，exFAT 的 OOBE 账户发布成功。缺失的 `/proc/PID` 父目录已补齐，任务管理器恢复进程列表。相关限制见 `docs/POSIX_PERMISSIONS_2026-09-08.md`。

本轮同时修正了 native `syscall`/LSTAR 入口、Linux 初始栈与 auxv、FS base 保存恢复、Linux x86-64 `stat` 144 字节布局、fcntl/open 标志、close/dup3/pipe2、ftruncate/getcwd、部分信号编号与掩码路径、SIGCONT/SIGPIPE、mmap/mprotect/munmap、资源限制编号，以及 `AT_EMPTY_PATH` 的用户指针校验。独立宿主契约目标之外，真实 musl 程序已经在 QEMU 执行；不能以定制 Picolibc 包装器代替 raw syscall 验证。

本检查点还补齐了 raw `creat`、旧版 `getdents`、`renameat`/零 flags 的
`renameat2`、`msync`、`mincore`、`madvise`、`waitid` 和无信号掩码的
`ppoll` 分发；`brk` 还记录了主 ELF writable end 并实现了受限的 grow/shrink。
`build/musl/abi-probes-qemu-20260908-11.log` 中静态和动态
musl 的 `memory`、`poll_files` 均通过并报告 `END failed=0`；非零
`renameat2` flags、link/symlink/readlink、ppoll 原子信号掩码和完整 waitid
siginfo，以及 brk 的完整 heap/VMA/RLIMIT 压力语义仍按 CSV 标为未完成。

随后补齐了 `select` 和无信号掩码的 `pselect6` 原始 fd_set/timeout 入口；
`build/musl/abi-probes-qemu-20260908-12.log` 中静态和动态 musl 的
`poll_files` 均通过并报告 `END failed=0`。pselect 原子信号掩码和 select
精确剩余 timeout 仍按 CSV 标为未完成。

随后新增了 `getrusage`、`sysinfo` 和 `times` 的 native x86-64 结构入口；
`build/musl/abi-probes-qemu-20260908-13.log` 中静态和动态 musl 的
`poll_files` 均通过并报告 `END failed=0`。子进程累计、swap/load、详细 I/O
和 page-fault 统计仍按 CSV 标为未完成。

本轮还补齐了 `fallocate` 的 mode-zero 文件扩展及 fd 元数据刷新；
`build/musl/abi-probes-qemu-20260908-16.log` 中静态和动态 musl 的
`poll_files` 均通过并报告 `END failed=0`。真实预分配、keep-size 和 punch-hole
仍按 CSV 标为未完成。

本轮又补齐了 `setreuid`/`setregid`、`setresuid`/`getresuid` 和
`setresgid`/`getresgid` 的 native x86-64 分发、三元身份结构和 `-1` 保留语义；
静态及动态 musl 探针在 `build/musl/abi-probes-qemu-20260909-14.log` 均报告
`END failed=0`。完整 exec、线程共享凭据、saved-ID 边界和 LTP 身份矩阵仍未认证。

随后补齐了 `sched_get_priority_max`/`sched_get_priority_min` 的 Linux policy
校验和 FIFO/RR、OTHER/BATCH/IDLE 优先级边界；静态及动态 musl 探针在
`build/musl/abi-probes-qemu-20260909-13.log` 均通过并报告 `END failed=0`。
调度策略设置、实时调度和多核并行行为仍未认证。

随后新增了 `sched_getparam`/`sched_getscheduler` 的 native x86-64 分发；当前任务
和有效目标任务查询、无效 PID 以及输出指针错误路径已由静态及动态 musl 探针覆盖，
`build/musl/abi-probes-qemu-20260909-15.log` 均报告 `END failed=0`。调度策略状态
变更、权限检查和并发调度语义仍未认证。

同一轮补齐了 `sched_setparam`/`sched_setscheduler` 的 native x86-64 分发；当前仅
支持内核实际调度模型 `SCHED_OTHER` 与 priority 0，其他策略或优先级明确返回
`EINVAL`，静态及动态 musl 探针在 `build/musl/abi-probes-qemu-20260909-17.log`
均报告 `END failed=0`。实时策略、优先级继承和 SMP 调度仍未认证。

随后补齐了 `sched_rr_get_interval`；它返回当前 `NTCLKS_TICK_HZ` 对应的实际调度
tick，并校验目标 PID、输出结构和错误指针。静态及动态 musl 探针在
`build/musl/abi-probes-qemu-20260909-18.log` 均报告 `END failed=0`；策略相关
时间片和 SMP 调度行为仍未认证。

随后补齐了 native x86-64 的 `personality` 分发；查询和 `PER_LINUX` 设置返回
Linux 约定值，未支持的 persona flags 明确返回 `EINVAL`。静态及动态 musl 探针在
`build/musl/abi-probes-qemu-20260909-19.log` 均报告 `END failed=0`；exec 继承、
ASLR 交互和其他 persona 的完整行为仍未认证。

随后补齐了 `mremap` 的 native x86-64 分发；匿名 VMA 支持收缩、相邻原地扩展和
`MREMAP_MAYMOVE` 搬迁，搬迁后保留原有页内容。静态及动态 musl 探针在
`build/musl/abi-probes-qemu-20260909-20.log` 均报告 `END failed=0`；文件/设备
映射、重叠的 `MREMAP_FIXED` 目标及完整资源限制交互仍未认证。

随后补齐了 `readahead` 的 native x86-64 分发；校验 fd、负偏移和普通文件/块设备
对象，非可 seek 对象返回 `ESPIPE`。静态及动态 musl 探针在
`build/musl/abi-probes-qemu-20260909-22.log` 均报告 `END failed=0`；后端缓存预取
和完整错误矩阵仍未认证。

随后补齐了 `prctl` 的 native x86-64 进程名子集；`PR_SET_NAME`/`PR_GET_NAME`
遵循 Linux 16 字节截断和用户指针边界，dumpable 查询/设置返回明确结果。静态及
动态 musl 探针在 `build/musl/prctl-after-serial-2.log` 均报告
`END failed=0`；其他 prctl 命令和继承语义仍未认证。

随后补齐了 `clock_settime` 的 native x86-64 `CLOCK_REALTIME` 路径；root 权限、
timespec 范围和 wall-clock 后端复用现有 `settimeofday` 约定，非法 clock 或
单调时钟返回 `EINVAL`。静态及动态 musl 探针在
`build/musl/abi-probes-qemu-20260909-26.log` 均报告 `END failed=0`；RTC 持久化、
capability 权限和完整时钟调整交互仍未认证。

随后补齐了 `mlock`/`munlock`/`mlockall`/`munlockall`/`mlock2` 的 native
x86-64 分发；内存后端记录 VMA 锁定状态，支持 `MCL_CURRENT`、`MCL_FUTURE`、
`MCL_ONFAULT` 和 `MLOCK_ONFAULT` 标志，并拒绝未映射范围。静态及动态 musl 探针在
`build/musl/abi-probes-qemu-20260909-27.log` 均报告 `END failed=0`；完整
`RLIMIT_MEMLOCK`、非对齐地址、缺页时机、exec/fork 继承仍未认证。

随后补齐了 Linux 6.12 futex2 的 `futex_wake`、`futex_wait`、`futex_requeue`。
依据[官方 syscall 实现](https://github.com/torvalds/linux/blob/v6.12/kernel/futex/syscalls.c)
使用真实 4/6/4 参数签名和 24 字节 `futex_waitv` 描述符，修复零计数唤醒、原生类型
截断、用户指针错误、EAGAIN 被误重试，以及信号投递漏清理等待队列的问题。
`tools/tests/futex2_abi_test.c` 同时用于原版 Linux 6.12 和 LeonOS，验证实际线程
排队、掩码唤醒、私有到共享队列重排、绝对超时及 SA_RESTART。
`build/musl/futex2-linux-reference-serial.log` 对照通过；
`build/musl/futex2-probes-serial.log` 静态/动态均通过并报告 `END failed=0`。
实时时钟调整、共享映射引用生命周期及 AP 并行仍未认证，`futex_waitv` 入口仍未实现。

`clone3` 现已接入真实 trap 帧，按 Linux 6.12 校验 64/80/88 字节版本、零扩展尾部、
独立 exit_signal 和 stack/stack_size；复用现有 clone 的 MM/files/fs/sighand/TLS/TID
路径，并实现 `CLONE_CLEAR_SIGHAND` 的 caught-handler 重置和 SIG_IGN 保留。
`tools/tests/clone3_abi_test.c` 在 `build/musl/clone3-linux-reference-serial.log`
及 `clone3-probes-serial.log` 中均通过，覆盖实际创建、退出和 wait 回收。
pidfd、指定 PID、cgroup、命名空间、vfork 及旧 clone 的其他限制仍未完成。

随后补齐了 `sethostname(2)`/`setdomainname(2)` 的 Linux native 入口；root euid、64 字节
上限、精确长度复制和用户指针错误码均在内核边界检查，`uname(2)` 返回持久化的 nodename
和 domainname。`tools/tests/utsname_abi_test.c` 在 Linux 6.12 参考环境和 LeonOS
静态/动态 musl 探针 `build/musl/utsname-after-serial.log` 中通过；LeonOS 当前只有
单一 UTS 命名空间，完整 capability 检查仍未认证。

随后补齐了 `membarrier(2)` 的 native 入口；QUERY 只报告已实现的全局/私有屏障和注册
命令，注册状态放在共享 MM，GLOBAL/PRIVATE 屏障执行编译器与 CPU 内存栅栏，非法命令
和 flags 返回 Linux 错误码。静态/动态 musl 探针在 `build/musl/membarrier-serial-2.log`
中通过；对照 Linux 6.12 镜像的 `CONFIG_MEMBARRIER` 未启用，按内核配置返回 `ENOSYS`，
CPU 定向和 RSEQ 命令仍未实现。

随后补齐了 `openat2(2)` 的 native 入口；按 Linux 6.12 校验 `struct open_how` 的
24 字节基线、零扩展尾部、flags/mode 和用户范围，支持已实现的 openat 对象路径，
对 `RESOLVE_*`、`O_PATH` 和 `O_TMPFILE` 返回明确的未支持错误。Linux 6.12 参考及
LeonOS 静态/动态 musl 探针分别在 `build/musl/openat2-linux-reference-serial.log`
和 `build/musl/openat2-serial.log` 通过。

随后补齐了 `setfsuid`/`setfsgid` 的 Linux filesystem credential 入口；任务凭据和
权限判定现在保留独立 fsuid/fsgid，调用返回旧值并按 root 或 real/effective/saved
ID 规则更新。静态及动态 musl 探针在 `build/musl/abi-probes-qemu-20260909-17.log`
均报告 `END failed=0`。exec 凭据转换、线程共享凭据和完整权限矩阵仍未认证。

随后新增了 `eventfd`/`eventfd2` 的 Linux 计数器对象、`EFD_NONBLOCK`/
`EFD_SEMAPHORE`/`EFD_CLOEXEC` 标志、原生八字节读写和 poll readiness；
`build/musl/abi-probes-qemu-20260909-11.log` 中静态和动态 `poll_files` 均通过并
报告 `END failed=0`。完整等待队列唤醒、溢出阻塞、资源限制及所有引用生命周期
仍按 CSV 标为未完成。

Unix socket/pthread 阶段新增 8 个入口：getpeername、clone、rt_sigpending、sigaltstack、tkill、tgkill、set_robust_list/get_robust_list。实现共享线程资源、futex 等待/唤醒/重排、robust owner death、原生 Linux 信号帧及 altstack/FP 状态；Unix socket 使用真实路径节点和 OFD 引用传递，支持流、数据报和顺序包。旧 Picolibc 信号包装器同步适配共享 UAPI，旧二进制仍须重建。

最新实际验证：`build/musl/proc-status-after-serial.log` 中静态和动态 musl 探针均 `END failed=0`，覆盖线程争用、取消、信号栈/SIMD/重启、线程 exec、SCM_RIGHTS/SCM_CREDENTIALS、向量 I/O、跨线程 close/reuse、超时、大消息 MSG_WAITALL、Unix shutdown/poll、FIONREAD/FIONBIO、peer reset、poll 参数宽度/信号中断和普通文件 readiness。相同程序的 38 组核心用例还在真实 Linux 6.12.0 参考内核上全部通过（`proc-status-linux-reference-serial.log`）。它取代早期 socket unlink、pthread_create 和 poll 页故障失败的当前结论。最新 `tls-scm-ltp-serial.log` 中 14 个未修改源码的 Open POSIX pthread/同步用例以及 getcwd01、fcntl01、mprotect01、fchmod01、chown01 退出 0；fstat02 缺 link，chmod01 缺时间戳调用，runner 仍报告两个失败。测试期望没有放宽。

描述符阶段修复：取消保留 fd 3，open/pipe/Unix socket/SCM_RIGHTS/dup/PTMX 共用最低空闲编号分配；dup2 扩容前保留 OFD，失败不破坏目标；RLIMIT_NOFILE 限制新 fd 的编号而不是已打开数量。原生用例验证 fd 0/3、fd 700 扩容、SCM 接收、资源耗尽回滚、只读输出指针和 32 位参数宽度。`tools/test_linux_descriptors.py` 对实际内核辅助函数及扩容代码进行 ASan/UBSan 和 ENOMEM 注入验证。隐含 PTY 的 close(0) 修复后，普通桌面后台命令不再报 /dev/null 错误；`fd-normal-nano.png` 记录桌面/管道/nano，`fd-default-installer.png` 记录默认 installer 越过 Logo 并到达 Thanks。jobs/fg/bg、PTY/INET 最后引用释放、完整资源限制等仍未完成。

亲和性/TLS 阶段：raw sched_getaffinity 返回复制字节数，Picolibc API 同步转换为 0 并清零尾部；musl pthread_get/setaffinity_np、参数宽度、短/长掩码及跨用户权限用例通过。ARCH_SET_FS 和 CLONE_SETTLS 共用四级页表的 Linux TASK_SIZE_MAX（2^47 - 4096），允许合法但未映射的 TLS 地址，拒绝非法地址且不破坏原 FS；ARCH_GET_FS 检查输出可写。set_tid_address 只登记指针，退出仅在其他任务仍共享 mm 时清零，支持未对齐及跨页字值，忽略无效/只读地址。`tls-before-serial.log`、`tid-before-serial.log` 保留失败复现，`tls-after-serial.log` 验证修复。GS 和其他 arch_prctl 子命令、完整 clone/CPU/capability 行为仍未完成。

SCM_RIGHTS 阶段：修正普通流数据与携带 fd 的 sendmsg 混合时的接收边界。`scm-boundaries-before-serial.log` 复现错误，修复后按 Linux 在携带 fd 的发送段末尾停止，并覆盖 MSG_PEEK、MSG_WAITALL、read/readv；静态、动态各 44 组（含 startup）通过。该证据不涵盖所有大消息分段与资源限制。

procfs 阶段：补充 `/proc/PID/status` 的实际 PID/TGID、UID/GID 和驻留 RAM 数据；RSS 排除设备映射。任务快照消费者读取该字段并从 `/etc/passwd` 解析用户名。修复前 `proc-status-before-serial.log` 复现缺失，修复后 `proc-status-after-serial.log` 静态、动态各 45 组通过。`proc-status-taskmgr.png` 实测运行进程的内存和账户名恢复；`proc-status-normal-nano.png` 记录 OOBE/双 PTY/管道/nano；两张 glxgears 截图证实持续绘制并正常退出。默认和 musl installer 均到达 Thanks，未进行落盘安装。仅此 status 子集已验证，原 stat/cmdline 私有格式、其余字段和权限仍未完成。

剩余范围：clone3、PI futex、实时信号队列及完整 siginfo/中断语义、跨 VMA mprotect、rename/unlink 后 inode 生命周期、时间戳调用、setuid/setgid 执行、完整 *at 族、事件 API、socket 其余选项/资源边界、INET 和其余高级调用。当前 AP 用户调度仍关闭，LeonOS/QEMU 的线程测试是 BSP 抢占执行，不能作为多核并行认证。整套发行版和第三方应用迁移尚未完成，当前没有 VMware 结果。

## 初始数量和统计口径

官方[调用表](https://github.com/torvalds/linux/blob/v6.12/arch/x86/entry/syscalls/syscall_64.tbl)有 375 个 native 已编号项。其中 16 个没有 entry point，另有 `_sysctl` 指向 `sys_ni_syscall`；这 17 个不作为 LeonOS 必须补齐的实现缺口。

| 类别 | 数量 | 含义 |
| --- | ---: | --- |
| Linux 参考表中有非 ni 入口 | 358 | 表级入口数，不代表任意 Linux 内核配置都会启用 |
| LeonOS 有对应名称的处理入口 | 82 | 仅证明可分发，不代表布局、flags、错误码或语义正确 |
| LeonOS 编号冲突 | 1 | 34:pause 被实现为私有 nice |
| LeonOS 没有分发入口 | 275 | 一般落入 -ENOSYS；其中 getpeername 有不可达的底层代码 |
| Linux 自身保留或 ni 的编号 | 17 | 单列，未混入 275 |

所以至少 **276 项尚无对应的 Linux 调用语义**：275 个缺少分发，加上 pause 的编号冲突。另确认 **46 组已有接口差异或配套 ABI 缺口**，详见 B01-B46；这些分组有交叉，不能与 276 相加当作总接口数，也不能换算成整个 Linux ABI 的完成百分比。

逐编号文件：[375 项完整 CSV](/home/xiaobai/Projects/Projects/LeonOS-4/docs/LINUX_ABI_SYSCALLS_2026-09-07.csv)。CSV 中 `routed_not_certified` 是“有入口，未认证兼容”，`missing_dispatch` 是“无分发”，`number_collision` 是“编号冲突”，`linux_reserved_or_ni` 是“Linux 本身保留/ni”。分发行号指向入口比较，不一定指向最终实现。

实际用户入口：[syscall_dispatch_frame](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:4503) -> [syscall_dispatch_regs](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:4433) -> 各类别转发/legacy。没有只根据头文件数量或 debug 用的 `syscall_dispatch()` switch 统计；mount/umount2 和 trap 专门处理的 rt_sigreturn 均已算入。send/recv 是 44/45 的别名，没有重复计算。

## 初始缺失调用优先级

| 范围 | 代表性缺失接口 |
| --- | --- |
| musl 启动与线程 | arch_prctl、set_tid_address、gettid、clone/clone3、futex、set_robust_list/get_robust_list、exit_group、tgkill |
| 基础文件与 shell | readv/writev、pread64/pwrite64、getdents64、newfstatat、lstat、access/faccessat、readlink/readlinkat、fchdir、umask、fsync/fdatasync、truncate |
| 常用 *at 族 | mkdirat、unlinkat、renameat/renameat2、linkat、symlinkat、fchmodat、fchownat、utimensat |
| 内存 | brk、mremap、madvise、msync、mincore、mlock/munlock |
| 事件循环 | ppoll、select/pselect6、epoll_*、eventfd/eventfd2、timerfd_*、signalfd/signalfd4 |
| 信号和计时 | sigaltstack、rt_sigpending、rt_sigtimedwait、rt_sigqueueinfo、restart_syscall、clock_getres、clock_nanosleep、alarm、setitimer、timer_* |
| 资源与随机数 | prlimit64、getrusage、sysinfo、getrandom |
| 网络 | getpeername、sendmmsg、recvmmsg；已有 socket 调用内部的 UDP/IPv6/INET server 子集另见 B36 |
| 用户和权限 | getgroups/setgroups、setresuid/getresuid、setresgid/getresgid、capget/capset |
| 高级可后置功能 | io_uring、namespaces、cgroups 相关配套、seccomp、BPF、ptrace、NUMA、fanotify、内核模块等 |

此表是优先级索引，完整的 275 项在文末与 CSV 中。`brk`、`madvise`、clone3、rseq 等是否必须优先实现，取决于具体 musl 路径和应用，不能因为被列为缺失就说每个 hello world 都需要它。相反，线程指针和正确初始栈属于启动基础。

## 已确认差异

本节的 B01-B46 正文保留初始静态审计的证据和问题描述，便于追溯；本轮修复后的事实状态以本节前的“本轮实际修复与状态”、下面的“本轮状态订正”和 CSV 为准。若历史描述与状态订正冲突，不应把历史描述当作当前源码结论。

### 启动和执行基础

对照：[Linux x86-64 syscall 表](https://github.com/torvalds/linux/blob/v6.12/arch/x86/entry/syscalls/syscall_64.tbl)、[Linux ELF 初始栈](https://github.com/torvalds/linux/blob/v6.12/fs/binfmt_elf.c)、[musl x86-64 调用序列](https://git.musl-libc.org/cgit/musl/tree/arch/x86_64/syscall_arch.h?h=v1.2.5)、[musl TLS 初始化](https://git.musl-libc.org/cgit/musl/tree/src/env/__init_tls.c?h=v1.2.5)。

#### B01 系统调用进入方式

当前 Ring 3 使用 `int $0x80`，并按 x86-64 的寄存器和编号解释参数。未找到 native x86-64 `syscall` 所需的入口及 LSTAR/STAR/SFMASK 配置。未修改的 musl x86-64 代码使用 `syscall`；Linux 的兼容 `int 0x80` 也不是这里这套混合约定。因此即便某个函数体正确，现有入口仍不能直接执行常规 Linux x86-64 调用序列。

代码：[kernel/ntclks/arch/x86_64/idt.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/arch/x86_64/idt.c:199)，[kernel/ntclks/arch/x86_64/boot.S](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/arch/x86_64/boot.S:413)，[userland/libc/src/syscall.S](/home/xiaobai/Projects/Projects/LeonOS-4/userland/libc/src/syscall.S:1)。

#### B02 进程初始栈和 auxv

`prepare_user_exec_stack()` 把 RSP 指向 envp，argc/argv/envp 分别通过 RDI/RSI/RDX 传入，R8 传递私有动态启动记录。Linux 的初始栈是 argc、argv、NULL、envp、NULL、auxv；当前没有提供这一布局及 AT_PHDR/AT_PHNUM/AT_PHENT/AT_PAGESZ/AT_ENTRY/AT_BASE/AT_RANDOM 等辅助向量。标准 crt 和 libc 初始化会错误解释启动数据。

代码：[kernel/ntclks/user/userland.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/user/userland.c:316)，[userland/libc/src/crt0.S](/home/xiaobai/Projects/Projects/LeonOS-4/userland/libc/src/crt0.S:1)。

#### B03 TLS 和线程基础设施缺失

未找到 `arch_prctl(ARCH_SET_FS/ARCH_GET_FS)`、线程 FS base 保存恢复、`set_tid_address`、`gettid`、`clone`/`clone3`、`futex`、robust list 的完整调用链。`clone` 虽有编号，实际没有分发。musl 的静态单线程程序也会初始化线程指针，不能把 TLS 留到 pthread 阶段才处理。PT_TLS 的解释通常由用户态 libc/动态加载器完成，内核需提供可用的启动信息和线程指针机制，不能简单理解为必须新增一个内核 PT_TLS 加载函数。

代码：[include/uapi/linux/syscall.h](/home/xiaobai/Projects/Projects/LeonOS-4/include/uapi/linux/syscall.h:39)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:4145)，[tools/build_picolibc.py](/home/xiaobai/Projects/Projects/LeonOS-4/tools/build_picolibc.py:151)。

#### B04 动态 ELF 启动协议

动态镜像要求 LeonOS ABI note 和指定的 LeonOS 解释器路径；自有加载器依赖私有 launch 记录，只实现有限的重定位种类，未实现通用 TLS 重定位链。标准 `/lib/ld-musl-x86_64.so.1` 程序不能直接通过这套验证。迁移时可让 musl 的加载器承担动态链接，不必把现有加载器扩展成所有 Linux 加载器的替代品。

代码：[kernel/ntclks/user/elf.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/user/elf.c:249)，[userland/runtime/ld_leonos.c](/home/xiaobai/Projects/Projects/LeonOS-4/userland/runtime/ld_leonos.c:1029)，[userland/runtime/ld_leonos.c](/home/xiaobai/Projects/Projects/LeonOS-4/userland/runtime/ld_leonos.c:1386)。

#### B05 34 号调用冲突：pause 被当成 nice

`LINUX_SYS_NICE` 被定义为 34，但 Linux x86-64 的 34 是 `pause`，没有单独的 native `nice` syscall。当前 34 号会调整优先级并返回，不能等待信号。这是编号冲突，不应计作已实现 pause。

代码：[kernel/ntclks/include/ntclks/syscall.h](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/include/ntclks/syscall.h:63)，[kernel/ntclks/syscall_process.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_process.c:395)。


### 文件、描述符和管道

对照：[Linux x86-64 stat](https://github.com/torvalds/linux/blob/v6.12/arch/x86/include/uapi/asm/stat.h)、[open flags](https://github.com/torvalds/linux/blob/v6.12/include/uapi/asm-generic/fcntl.h)、[fcntl 扩展命令](https://github.com/torvalds/linux/blob/v6.12/include/uapi/linux/fcntl.h)、[文件打开与截断](https://github.com/torvalds/linux/blob/v6.12/fs/open.c)、[Linux pipe](https://github.com/torvalds/linux/blob/v6.12/fs/pipe.c)。

#### B06 stat / fstat 返回结构错误

内核写回的是 16 字节 `leonos_stat {type,reserved,size}`，不是 Linux x86-64 的 144 字节 `struct stat`。本项目 libc 再把它转换为 Picolibc 结构，并补造 mode、inode 等字段；这属于本项目的适配，不能让 musl 直接使用 Linux syscall 得到正确结果。Linux 结构要求的 dev/ino/mode/nlink/uid/gid/rdev/size/blocks/时间戳等没有在内核边界正确返回。

代码：[include/leonos/fs.h](/home/xiaobai/Projects/Projects/LeonOS-4/include/leonos/fs.h:49)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:3521)，[userland/libc/src/posix_stat.c](/home/xiaobai/Projects/Projects/LeonOS-4/userland/libc/src/posix_stat.c:1)。

#### B07 目录枚举协议

目录通过 `read(fd, leonos_dir_entry, ...)` 读出固定大小的私有记录；Linux 的 `getdents` / `getdents64` 均没有入口。Linux 的 `read` 目录行为和可变长 dirent 记录均不匹配。现有定制 readdir 能工作，不代表 musl readdir 能工作。

代码：[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:3109)，[include/leonos/fs.h](/home/xiaobai/Projects/Projects/LeonOS-4/include/leonos/fs.h:50)。

#### B08 O_NONBLOCK 数值错误

本项目 `O_NONBLOCK=0x4000`，Linux x86-64 是 `0x800`。因此普通 open/fcntl/pipe2/PTY 路径不能正确识别 musl 传入的非阻塞位，F_GETFL 也返回错误编码；Linux 的 `0x4000` 是 O_DIRECT。项目的 `SOCK_NONBLOCK=0x800` 是正确的，但不能弥补其他 fd 路径的差异。

代码：[kernel/ntclks/include/ntclks/syscall.h](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/include/ntclks/syscall.h:100)，[userland/libc/include/fcntl.h](/home/xiaobai/Projects/Projects/LeonOS-4/userland/libc/include/fcntl.h:14)。

#### B09 fcntl 命令号和子操作缺失

`F_DUPFD_CLOEXEC` 使用 14，Linux 是 1030；Linux 传入 1030 会落入未实现分支。文件锁 F_GETLK/F_SETLK/F_SETLKW、OFD locks、F_SETPIPE_SZ/F_GETPIPE_SZ 等也未提供。已有 F_GETFD/F_SETFD 的 FD_CLOEXEC=1 不应与 open/pipe2/dup3 的 O_CLOEXEC 混用。

代码：[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:681)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:3461)，[userland/libc/include/fcntl.h](/home/xiaobai/Projects/Projects/LeonOS-4/userland/libc/include/fcntl.h:26)。

#### B10 open / openat / mkdir 的标志和 mode

普通 open 分支未落实 O_EXCL、O_DIRECTORY、O_NOFOLLOW，分配 fd 时把 fd_flags 设为 0，未落实 O_CLOEXEC；创建文件和 mkdir 忽略传入 mode，umask 也没有入口。openat 先验证 dirfd 再判断绝对路径，而 Linux 的绝对路径应忽略 dirfd。不能把这些成功返回解释为请求的创建与继承属性已生效。

代码：[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:2856)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:3211)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:1020)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:3674)。

#### B11 dup / fork 后没有共享 open file description

dup2、F_DUPFD 和继承路径直接复制 task_file，文件 offset 和状态 flags 随之复制。Linux 的 dup 和 fork 后的对应 fd 应共享 open file description，从而共享文件位置及 O_APPEND/O_NONBLOCK 等状态。当前一个副本的 read/lseek/F_SETFL 不会自动更新其他副本。

代码：[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:1090)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:1128)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:606)。

#### B12 fd 分配和 dup3 语义

普通文件编号从 4 起分配，3 保留；pipe/socket 和 dup 路径也不能统一复用最低空闲 fd。普通 open 已支持复用关闭的 0/1/2，不能说它完全没有这项能力。dup2 对 3 以及部分尚未扩容的目标槽直接拒绝。dup3 把 flags 按 FD_CLOEXEC=1 验证，Linux 应传 O_CLOEXEC=0x80000；PTY dup3 还未落实对应 close-on-exec 标记。

代码：[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:1020)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:1090)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:3427)，[kernel/ntclks/syscall_ipc.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_ipc.c:82)。

#### B13 close 与端点生命周期

重复 close 已关闭的 0/1/2 仍返回成功，Linux 应返回 EBADF。PTY owner 关闭一个 master fd 时直接 destroy，会影响仍存在的 master 副本。INET fd 的 retain/release 是空操作，网络连接按 owner pid 回收；不能提供最后一个 fd 关闭才释放连接及 fork 后独立关闭的 Linux 文件引用语义。

代码：[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:3363)，[kernel/ntclks/syscall_socket.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_socket.c:351)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:574)。

#### B14 ftruncate 改变文件偏移

缩短文件后，如果 offset 大于新长度就把它截到 EOF；Linux ftruncate 不改变当前文件偏移。随后写入或 SEEK_CUR 的位置会与 Linux 不同。

代码：[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:3391)。

#### B15 getcwd 的内核返回值和错误码

成功返回用户缓冲区地址，而 Linux raw getcwd 返回包含结尾 NUL 的字节数。缓冲区太小等情况统一返回 EFAULT，未区分 ERANGE/EINVAL。这里必须区分 libc `char *getcwd()` 和内核 raw syscall 的不同返回约定。

代码：[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:3633)。

#### B16 chmod / chown 权限语义

chmod 把 POSIX rwx 位直接写入 LeonOS ACL，而两者 READ/EXEC 位恰好相反：POSIX r=4/x=1，LeonOS READ=1/EXEC=4。因此 chmod 0400 的 owner 权限会被解释成执行。group 位和 setuid/setgid/sticky 位没有完整实现。chown 忽略 group，并直接写 owner，未实现 uid/gid 为 -1 时保持不变的约定。

代码：[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:1840)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:1878)，[include/leonos/fs.h](/home/xiaobai/Projects/Projects/LeonOS-4/include/leonos/fs.h:27)。

#### B17 已打开文件在 rename/unlink 后的语义及 O_APPEND

普通文件写入仍通过 file->path 查找并再次授权。路径被改名或删除后，旧 fd 无法保持 Linux 的已打开文件对象语义；路径被替换时还可能指向不同对象。O_APPEND 使用本 fd 缓存的 node.size，多个独立 open 的 fd 写入后会出现陈旧 EOF，不能保证每次原子追加到真实文件末尾。

代码：[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:3006)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:3692)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:3739)。

#### B18 pipe2 标志、失败副作用和作用范围

pipe2 先创建管道再验证 flags，非法 flags 返回 EINVAL 时已产生 fd；接受的 CLOEXEC 位是 FD_CLOEXEC=1，而 Linux 是 O_CLOEXEC=0x80000。设置 flags 时遍历并修改进程所有管道 fd，未限定到本次创建的两个 fd。Linux 调用不能依赖这套实现得到原子的创建属性。

代码：[kernel/ntclks/syscall_ipc.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_ipc.c:243)。

#### B19 pipe 写原子性与 SIGPIPE

管道有少量空闲空间时，长度不超过 PIPE_BUF 的写也可能只写一部分；Linux 要求这类写保持原子性，非阻塞情况下空间不足应返回 EAGAIN 而不提交部分数据。没有读者时只返回 EPIPE，未发送 SIGPIPE。可能破坏管道消息边界和常规 shell 退出行为。

代码：[kernel/ntclks/syscall_ipc.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_ipc.c:156)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:2934)。


### 虚拟内存

对照：[Linux mmap/munmap](https://github.com/torvalds/linux/blob/v6.12/mm/mmap.c)。这些接口的存在不等于所有映射组合都可用，需区分明确的实现限制与返回了成功却未落实请求的错误。

#### B20 mmap 仅实现受限子集

拒绝 PROT_NONE；MAP_FIXED 遇到已有映射直接失败，未实现 Linux 的覆盖语义。匿名 MAP_SHARED 和普通文件 MAP_SHARED 没有完整实现；未知 flags 一律拒绝，MAP_FIXED_NOREPLACE、MAP_STACK 等未补齐。匿名映射还要求 fd 必须为 -1。W^X 属于当前明确的权限策略，应单独评估，不能把它与布局错误混为一谈。动态加载器预留地址区、共享内存和保护页用法会受影响。

代码：[kernel/ntclks/syscall_mm.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_mm.c:715)，[kernel/ntclks/syscall_mm.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_mm.c:839)。

#### B21 mprotect 的权限和区间限制

拒绝 PROT_NONE，不能制作不可访问保护页；max_prot 初始化为映射时的 prot，阻止 Linux 通常允许的匿名页从只读变成可写。一次操作要求落在同一个 VMA 中，未支持一般的跨 VMA 区间。零长度处理也与 Linux 的成功 no-op 不一致。

代码：[kernel/ntclks/syscall_mm.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_mm.c:300)，[kernel/ntclks/syscall_mm.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_mm.c:898)。

#### B22 munmap 对空洞的处理

预检查要求整个区间已有 VMA 覆盖，遇到未映射页返回 EINVAL。Linux 允许 munmap 包含未映射页，甚至整个区间都没有映射；重复释放或跨空洞释放因此不兼容。

代码：[kernel/ntclks/syscall_mm.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_mm.c:998)。


### 信号和进程等待

对照：[Linux x86 信号编号和 sigaction](https://github.com/torvalds/linux/blob/v6.12/arch/x86/include/uapi/asm/signal.h)、[信号 flags/how](https://github.com/torvalds/linux/blob/v6.12/include/uapi/asm-generic/signal-defs.h)、[信号帧和 red zone](https://github.com/torvalds/linux/blob/v6.12/arch/x86/kernel/signal.c)。

#### B23 信号编号、数量和默认动作

当前把 17 当 SIGSTOP、18 当 SIGTSTP、19 当 SIGCONT、20 当 SIGCHLD；Linux x86-64 对应是 SIGCHLD=17、SIGCONT=18、SIGSTOP=19、SIGTSTP=20。SIGURG/SIGIO 等也有差异。当前只处理 1..31，未提供 32..64 的实时信号和排队语义。按 Linux 信号号执行 kill/安装 handler 会发生错误动作。

代码：[kernel/ntclks/signal.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/signal.c:74)，[kernel/ntclks/signal.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/signal.c:140)，[kernel/ntclks/syscall_process.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_process.c:153)。

#### B24 rt_sigaction 布局、flags 和查询副作用

私有结构字段顺序为 handler/mask/flags32/reserved/restorer，Linux x86-64 为 handler/flags64/restorer/mask。SA_SIGINFO、SA_NODEFER、SA_RESETHAND 的位值也不同。act=NULL 查询时仍调用 setter 并把 action 清零，破坏原有 handler；Linux 查询应保持原设置。

代码：[include/leonos/signal.h](/home/xiaobai/Projects/Projects/LeonOS-4/include/leonos/signal.h:51)，[kernel/ntclks/signal.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/signal.c:11)，[kernel/ntclks/syscall_process.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_process.c:153)。

#### B25 rt_sigprocmask 的 how、掩码及大小验证

当前 SETMASK/BLOCK/UNBLOCK 分别按 0/1/2 解释，Linux 是 BLOCK=0/UNBLOCK=1/SETMASK=2。当前用 1<<sig 表示信号，Linux 用 1<<(sig-1)，并且掩码被截到 32 位。sigsetsize 接受 0 或任意 >=8 的值，未要求 Linux x86-64 的 8 字节；set=NULL 时仍处理 how，可能修改掩码。

代码：[kernel/ntclks/syscall_process.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_process.c:129)，[kernel/ntclks/signal.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/signal.c:150)。

#### B26 rt_sigreturn / 信号帧 ABI

信号帧使用 LeonOS magic/version 和私有寄存器记录，不是 Linux rt_sigframe/ucontext/siginfo 布局。没有保存和恢复 Linux 信号上下文要求的 FP/SIMD 状态，创建帧时也没有避开 x86-64 用户栈的 128 字节 red zone。标准 SA_SIGINFO handler 和 sigreturn 无法正确使用这些数据。

代码：[include/leonos/signal.h](/home/xiaobai/Projects/Projects/LeonOS-4/include/leonos/signal.h:19)，[kernel/ntclks/signal.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/signal.c:188)，[kernel/ntclks/signal.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/signal.c:287)。

#### B27 rt_sigsuspend 没有等待和恢复原掩码

替换 blocked_signals 后立即返回 EINTR，没有等待信号，也没有实现调用结束后恢复原掩码的约定。Linux 程序常用的等待条件循环会空转或改变后续信号屏蔽状态。

代码：[kernel/ntclks/syscall_process.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_process.c:114)。

#### B28 wait4 与 vfork 的语义缺口

wait4 忽略第四个 rusage 指针；WCONTINUED 使用 4，而 Linux 是 8。未校验完整 options 集合。vfork 明确实现为 COW fork，没有 Linux vfork 的共享地址空间及父进程暂停语义；这属于已知实现限制，不代表所有使用 vfork 的应用都会失败。（2026-09-10 更新：B28 的 vfork 部分已按 Linux v6.12 真实 `CLONE_VM|CLONE_VFORK|SIGCHLD`、exec/exit 释放和父线程 killable 等待修复，QEMU 验证通过；wait4 的 rusage/options 缺口仍独立存在。）

代码：[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:3899)，[kernel/ntclks/sched/sched.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/sched/sched.c:1944)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:4237)。


### 时间、资源和身份

对照：[Linux resource 编号](https://github.com/torvalds/linux/blob/v6.12/include/uapi/asm-generic/resource.h)、[优先级和身份系统调用](https://github.com/torvalds/linux/blob/v6.12/kernel/sys.c)、[CPU affinity 调用](https://github.com/torvalds/linux/blob/v6.12/kernel/sched/syscalls.c)、[reboot UAPI](https://github.com/torvalds/linux/blob/v6.12/include/uapi/linux/reboot.h)。

#### B29 nanosleep 参数与中断返回

把不能作为 timespec 访问的第一个参数当毫秒整数；未正确验证负时间或 tv_nsec 范围；忽略 rem。调用按现有调度路径结束后返回 0，未实现 Linux 的中断后 EINTR 和剩余时间协议。需移除私有整数兼容约定或放到独立私有接口。

代码：[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:3873)。

#### B30 时间接口子集

gettimeofday 强制 tv 非空，Linux 允许 tv=NULL；settimeofday 仅使用秒字段，未验证/落实 tv_usec 和 timezone 语义。clock_gettime 仅支持 CLOCK_REALTIME/CLOCK_MONOTONIC，进程/线程 CPU 时钟等未实现；clock_getres 和 clock_nanosleep 根本没有入口。不能把 clock_gettime 存在理解为完整时间 ABI。

代码：[kernel/ntclks/syscall_process.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_process.c:265)，[kernel/ntclks/syscall_process.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_process.c:281)，[kernel/ntclks/syscall_process.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_process.c:308)。

#### B31 getrlimit / setrlimit 资源号和限额

当前把 5 当 RLIMIT_NOFILE、6 当 RLIMIT_AS；Linux x86-64 分别是 7、9，而 5/6 是 RSS/NPROC。其余资源类型返回 ENOSYS，hard limit 也没有作为可变状态完整保存。标准 libc 读取栈/文件数限制及设置地址空间限制会出错。（2026-09-10 更新：编号已按 Linux 修正并保存 NOFILE/AS/SIGPENDING/STACK 的 soft/hard；STACK 默认和增长约束已实现并在 QEMU 验证；其余 12 个资源仍 ENOSYS。）

代码：[kernel/ntclks/syscall_process.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_process.c:438)。

#### B32 getpriority 返回编码

当前 raw getpriority 返回 nice+20，Linux 是 20-nice，合法范围为 40..1。0 优先级恰好都返回 20，容易掩盖问题；非零优先级会解释反向。getpriority/setpriority 也仅支持 PRIO_PROCESS，不支持进程组和用户集合。

代码：[kernel/ntclks/syscall_process.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_process.c:411)。

#### B33 sched_getaffinity / sched_setaffinity 参数和返回值

强制 cpusetsize 恰好 8 字节；Linux 允许按调用语义传入更大的掩码缓冲区。raw sched_getaffinity 成功应返回写入的字节数，当前返回 0；PID 不存在也未使用 Linux 的 ESRCH。常见 libc 的 cpu_set_t 比 8 字节大，不能直接适配。

代码：[kernel/ntclks/syscall_process.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_process.c:290)。

#### B34 reboot 混淆 libc 与内核签名

原审计实现把 a0 当重启命令，属于 libc reboot(cmd) 的外部形式。Linux raw syscall 是 reboot(magic1,magic2,cmd,arg)；标准 musl 会把 magic 放在 a0，从而被原实现判为非法命令。

2026-09-10 电源按钮修复：内核已按 magic1/magic2/cmd 及 32 位参数宽度解析，校验四种 Linux magic2 值；`tools/test_power.py` 覆盖真实分发器、非法参数和权限拒绝。登录后的桌面通过 authd 的 SO_PEERCRED/当前会话校验代执行电源操作，安装器继续使用标准 musl reboot。可复现整机验证见 `docs/BUILD_AND_INSTALLER.md` 的 Power control regressions。本项仍非完整认证：当前 uid 权限策略、capability/namespace、HALT 和其他命令语义仍有缺口，不能因重启/关机按钮修复而将整个 syscall 标为完成。

代码：[kernel/ntclks/syscall_process.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_process.c:329)，[include/uapi/linux/reboot.h](/home/xiaobai/Projects/Projects/LeonOS-4/include/uapi/linux/reboot.h:1)。

#### B35 用户身份和 kill 的部分语义

setuid/setgid 以 real uid 是否为 0 判定权限，并直接更新 real/effective/saved 三套 ID，未完整支持 Linux 的有效权限和 saved-ID 规则。kill(-1, sig) 返回 EINVAL；目标 PID 不存在等情况返回 EPERM，未与 ESRCH 区分。getgroups/setgroups、setresuid/getresuid 等缺失入口另列在清单中。

代码：[kernel/ntclks/syscall_process.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_process.c:203)，[kernel/ntclks/syscall_process.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_process.c:228)，[kernel/ntclks/syscall_process.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_process.c:367)。


### Socket

这部分包含明确返回错误的未支持功能，以及返回成功但协议没有落实的情况。不能把 AF_UNIX 的可用性推断为 UDP、IPv6 或 INET server 已兼容。

#### B36 socket 支持范围及 sendto/recvfrom 参数

有 AF_UNIX STREAM 和受限 AF_INET TCP 客户端；UDP、IPv6、INET bind/listen/accept 等未补齐。sendto/recvfrom 的路径只转交五个参数，并按 read/write 流操作处理，忽略地址和消息 flags；未按 Linux 的第六参数和数据报协议执行。Unix socket/socketpair 创建时未落实传入的 SOCK_NONBLOCK/SOCK_CLOEXEC；sendmsg/recvmsg 仅支持 Unix socket 和单个 iovec。getpeername 的底层分支虽然存在，52 号没有被上层转发，因此按缺失入口统计。

代码：[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:2827)，[kernel/ntclks/syscall_socket.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_socket.c:394)，[kernel/ntclks/syscall_socket.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_socket.c:637)，[kernel/ntclks/syscall_socket.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_socket.c:852)。

#### B37 socket options 和 shutdown 的伪成功

getsockopt 的顶层路径只处理 Unix socket 的少数选项，INET fd 会被拒绝为 EBADF；不支持的 Unix 选项可能成功返回而不写输出。setsockopt 多数请求直接成功，未实际设置选项。INET shutdown 直接返回 0，未实施半关闭语义。不能把这些返回 0 当成能力已提供。

代码：[kernel/ntclks/syscall_socket.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_socket.c:471)，[kernel/ntclks/syscall_socket.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_socket.c:787)，[kernel/ntclks/syscall_socket.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_socket.c:798)。

#### B38 socket 地址输出长度

getsockname/accept 输出按整个 sockaddr_un 或 sockaddr_in 写回，未按调用者提供的 addrlen 截断。Unix 路径输出长度只按是否有路径写 2 或 3，未返回实际路径长度；小缓冲区语义与 Linux 不同。

代码：[kernel/ntclks/syscall_socket.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_socket.c:456)，[kernel/ntclks/syscall_socket.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_socket.c:766)。

#### B39 SCM_RIGHTS / recvmsg 控制消息

接收路径在交付 FD 后先清 rx_fd_count，随后据此把 msg_controllen 写成 0，标准 CMSG_FIRSTHDR 遍历将看不到刚交付的控制消息。FD 在 sendmsg 时就分配到对端，控制消息未与对应流字节完整绑定；控制缓冲区不足、传输失败和 MSG_CMSG_CLOEXEC 的处理未齐全。现有应用的私有用法不能证明标准 SCM_RIGHTS 兼容。

代码：[kernel/ntclks/syscall_socket.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_socket.c:531)，[kernel/ntclks/syscall_socket.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall_socket.c:604)。


### 设备与 procfs

对照：[Linux termios](https://github.com/torvalds/linux/blob/v6.12/include/uapi/asm-generic/termbits.h)、[fbdev](https://github.com/torvalds/linux/blob/v6.12/include/uapi/linux/fb.h)、[块设备 ioctl](https://github.com/torvalds/linux/blob/v6.12/include/uapi/linux/fs.h)、[evdev ioctl](https://github.com/torvalds/linux/blob/v6.12/drivers/input/evdev.c)、[proc stat](https://github.com/torvalds/linux/blob/v6.12/fs/proc/array.c)、[Linux poll](https://github.com/torvalds/linux/blob/v6.12/fs/select.c)。

#### B40 TTY termios 和 Unix98 PTY 操作

TCGETS/TCSETS 直接交换 Picolibc 风格的 leonos_pty_termios：11 个 c_cc、没有 Linux c_line，带 speed 字段；Linux TCGETS 的 kernel termios 是 c_line 加 19 个 c_cc。ICANON/ECHO/ISIG/ICRNL 位及控制字符索引也不同。TCSETSW/TCSETSF 被当成立即 set，没有 drain/flush 区别；TIOCSPTLCK 为成功 no-op，TIOCSCTTY/TIOCNOTTY/TIOCGPTPEER/FIONREAD/TIOCOUTQ 等未提供完整处理。VMIN/VTIME 的读取等待语义也未落地。

代码：[include/leonos/pty.h](/home/xiaobai/Projects/Projects/LeonOS-4/include/leonos/pty.h:9)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:4071)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:4106)，[kernel/ntclks/pty.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/pty.c:609)。

#### B41 fbdev 布局和 pan 行为

fb_var_screeninfo 只定义前 80 字节，Linux 为 160 字节；fb_fix_screeninfo 缺多个字段，line_length 当前在偏移 32，Linux x86-64 为 48。FBIOPAN_DISPLAY 忽略输入结构和 xoffset/yoffset，仅提交全屏刷新；Linux 该请求是平移显示视口，不是通用 flush。LEONOS_FBIOGET_CAPABILITIES 是私有扩展，应独立标明，不计作 Linux 标准请求。

代码：[include/uapi/linux/fb.h](/home/xiaobai/Projects/Projects/LeonOS-4/include/uapi/linux/fb.h:12)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:3999)。

#### B42 BLKROGET ioctl 编码

本项目把 BLKROGET 定义为 _IOR(0x12,94,int)，值为 0x8004125e；Linux 定义为 _IO(0x12,94)，值为 0x125e，尽管它确实通过指针输出 int。直接使用 Linux 头的工具发出的请求不会命中现有分支。其他已核对的 BLKGETSIZE/BLKGETSIZE64/BLKSSZGET 编号不能因此一起判错。

代码：[include/uapi/linux/fs.h](/home/xiaobai/Projects/Projects/LeonOS-4/include/uapi/linux/fs.h:9)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:3965)。

#### B43 EVIOCGRAB 第三个参数被错误当成指针

Linux 用 ioctl(fd,EVIOCGRAB,1/0) 的参数值表示抓取或释放，虽然宏使用 _IOW 编码。当前先 user_range_ok 再解引用 int*，因此标准用法传入 1/0 会失败。已经存在 grab 后端，所以问题是调用约定，不应继续把整项描述为完全没有实现。

代码：[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:1376)。

#### B44 procfs 文本 ABI

/proc/<pid>/stat 的 state 输出数字而非 R/S/Z 等字符，字段位置和数量也不符合 Linux：cpu_ticks、uid、role、flags 使用了私有排列。cmdline 返回单一路径加换行，而 Linux 返回以 NUL 分隔的参数序列。/proc/<pid>/status、maps、fd、task 等常见节点未提供，/proc/stat 也只有有限 CPU 字段。标准 ps/top 和终端相关工具不能按 Linux 格式直接解析。

代码：[kernel/ntclks/procfs.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/procfs.c:135)，[kernel/ntclks/procfs.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/procfs.c:170)，[kernel/ntclks/procfs.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/procfs.c:194)，[kernel/ntclks/procfs.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/procfs.c:226)。

#### B45 poll 就绪与无限等待语义

poll 已修复 `poll(NULL,0,-1)` 的无限等待/信号中断、普通文件 EOF readiness、32 位 `nfds` 和 `revents` 可写校验，并补了 Unix shutdown/error/hangup 和 FIONREAD；epoll 基础 create/ctl/wait/oneshot/packed-event 路径已通过 static/dynamic musl QEMU 探针。当前仍依赖一 tick 重试轮询；10 ms 级轮询精度、ppoll/pselect、epoll 原子临时信号掩码及完整 wait queue/device/resource 语义属于 B45 未完成范围。

代码：[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:2551)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:2584)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:2656)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:4264)。

#### B46 mount / umount2 的实现子集

mount 仅支持现有块分区和文件系统后端，明确拒绝 MS_RDONLY、MS_BIND/MS_REMOUNT 等标志以及非空 data；无法提供 Linux 常见的只读挂载、bind mount 和 remount。umount2 只接受 UMOUNT_NOFOLLOW，MNT_DETACH/MNT_FORCE 等未实现。这些是主动返回错误的能力缺口，与已经支持基本 mount 调用并不矛盾。

代码：[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:3759)，[kernel/ntclks/syscall.c](/home/xiaobai/Projects/Projects/LeonOS-4/kernel/ntclks/syscall.c:3836)。

## 本轮状态订正

以下是与初始正文不同的当前源码状态；局部运行证据不构成整项兼容认证：

- B01：`userland/libc/src/syscall.S` 已改用 x86-64 `syscall`；`boot.S` 提供 LSTAR 入口和标准寄存器帧；`gdt.c` 写入 STAR/LSTAR/SFMASK 并开启 EFER.SCE。QEMU 曾实际触发 `#UD`，加入 EFER.SCE 后该异常消失，这是源码和 QEMU 日志确认的修复。
- B02：`prepare_user_exec_stack()` 已生成 Linux 的 argc/argv/envp/auxv 布局，并提供 AT_PHDR、AT_PHENT、AT_PHNUM、AT_PAGESZ、AT_BASE、AT_ENTRY、AT_RANDOM、AT_EXECFN；crt0 同时解析该栈布局和旧私有启动记录。
- B03/B04：`arch_prctl(ARCH_SET_FS/ARCH_GET_FS)` 和 FS base 保存恢复已用于真实 musl 解释器及线程 TLS。clone/clone3、robust list、clear_child_tid、经典 futex 和三个独立 futex2 入口已有静态、动态 musl 及 Linux 6.12 对照证据；clone3 的 pidfd/set_tid/cgroup/命名空间/vfork、PI futex、futex_waitv、完整 clone 标志和 AP 用户调度仍未完成。
- B05：34 号现在分发 Linux `pause`；旧 nice 行为移到私有编号，CSV 中该行已从冲突改为 `implemented_pending_runtime`。
- B06/B07：stat/lstat/fstat 写回 144 字节 Linux 结构，真实 musl 已验证 mode/UID/GID；ext2 inode 字段通过宿主读盘核对。FAT/exFAT 的 inode/时间戳及完整目录枚举仍未完成。getdents64 提供可变长目录记录，尚不代表完整 Linux 目录语义。
- B08/B09/B10/B12：共享 open/fcntl 常量；真实 Picolibc sysroot 的 O_NONBLOCK 漂移已修复并通过 installer QEMU；openat 直接使用目录 fd，不再临时修改 cwd；创建 mode/umask 和目录逐分量搜索检查已有 musl 探针。OFD 已在 dup/fork/SCM_RIGHTS 中共享，最低空闲 fd 分配也已用原生程序验证，但 fcntl 锁、PTY/INET 引用、O_PATH/O_TMPFILE、符号链接等仍未完成。
- B13/B14/B15/B16：重复 close 返回 EBADF，ftruncate 不再改变 offset，raw getcwd 返回字节数且 LTP getcwd01 通过。权限已替换为实际 POSIX mode/UID/GID，支持附加组、chmod/chown 及 fd/*at 变体；LTP fchmod01/chown01 和静态/动态 musl 权限矩阵通过。PTY/INET 引用、fd 在 rename/unlink 后的身份、完整凭据和特殊位执行语义仍未完成。
- B18/B19：pipe2 在创建前校验 flags 且只设置返回的两个 fd；小于等于 PIPE_BUF 的非阻塞写保持原子性，无 reader 返回 EPIPE 并发送 SIGPIPE。完整阻塞等待队列仍未完成。
- B20/B21/B22：已支持 PROT_NONE、MAP_SHARED、MAP_FIXED 覆盖、MAP_FIXED_NOREPLACE 和包含空洞的 munmap；跨 VMA 的完整 mprotect、共享映射回写和所有 Linux flag 组合仍未完成。
- B23/B24/B25/B27：信号编号、Linux sigaction、8 字节掩码、共享进程 pending 与线程 pending 分离、原生 rt_sigframe/red zone/FXSAVE、altstack、sigsuspend 可中断等待已有实现与定向验证。实时信号仍用位集，完整 siginfo、错误帧强制 SIGSEGV 和所有默认动作语义未完成。
- B30/B31/B32：gettimeofday 空指针、clock_getres/clock_nanosleep、中断剩余时间和原生 alarm/getitimer/setitimer 的 ITIMER_REAL 路径已验证；完整 CPU 时钟/ITIMER_VIRTUAL/ITIMER_PROF、资源软硬限制和所有计时语义仍未完成。
- B36/B37/B45：Unix socket 已补接收低水位、shutdown/poll、FIONREAD、SO_ERROR/recv 的 ECONNRESET 消费。poll 的 nfds/timeout 宽度、可写 revents、EOF readiness 和无限等待/信号中断已用 raw syscall 检查；socket 缓冲区记账、其余选项、ppoll/pselect/epoll 和 INET 仍未完成。

上述局部证据不等于完整兼容。QEMU 已验证真实 musl 探针、五个 musl installer 服务、普通系统 OOBE/桌面/Terminal/管道和文件属性权限修改；先前“只进入服务、尚未观察到 BusyBox”的描述已经过时。LTP/Open POSIX 最新为 19 项退出 0、2 项 TBROK，详见进度清单。VMware 尚未运行。

## ABI 边界与优先顺序

这里统计的是 Linux 用户态 ABI，不是 Linux 内核模块 ABI。LeonOS 私有 GPU/服务 IPC、账户系统和系统目录可以保留，但不应复用 Linux 已定义请求并赋予不同含义。没有实现所有 DRM/ALSA/网络 ioctl，不意味着必须照搬整个 Linux 驱动体系；应按目标应用确定设备子集。

libc 的 POSIX API 与 raw syscall 需要分开验收。例如 `getcwd()`、`reboot()`、`stat()` 在 libc 与内核边界本来就有不同签名或布局。当前 Picolibc 包装函数所做的补偿，不会自动被 musl 继承。部分旧文档仍写信号 handler 未实现，而代码已经有私有 handler frame；本报告以代码为准。

建议顺序：

1. 先固定 native syscall 入口、Linux 初始栈/auxv、FS base/TLS、退出和最低文件 I/O；使用一个静态 musl 程序验证启动链。
2. 修正 stat、fcntl/open/dup/pipe、signal、termios、mmap、procfs 的具体契约，补 getdents64/readv/writev/newfstatat/readlink 等，覆盖 shell 与 Terminal。
3. 再补 clone/futex/robust list、ppoll/epoll、共享映射与 musl 动态加载器所需行为，最后按应用需求扩展网络和高级系统调用。

修复已有差异时需同时更新本项目 libc/SDK/应用消费者；不能只改内核常量，否则旧二进制会继续按原有私有约定传参。

## 完整缺失入口清单

下列是初始审计基线中的 275 项，格式是 Linux syscall 编号:名称。当前逐项状态以 CSV 为准；后来新增实现的编号仍保留在历史分组中，避免丢失审计基线。34 个新增实现状态还包含原先编号冲突的 pause，不应直接从 275 减去 34。Linux 本身的 17 个保留/ni 项随后单列。

### 0-99 号：43 项

```text
6:lstat  12:brk  17:pread64  18:pwrite64  19:readv  20:writev  21:access  23:select  25:mremap
26:msync  27:mincore  28:madvise  29:shmget  30:shmat  31:shmctl  36:getitimer  37:alarm  38:setitimer
40:sendfile  52:getpeername  56:clone  64:semget  65:semop  66:semctl  67:shmdt  68:msgget  69:msgsnd
70:msgrcv  71:msgctl  73:flock  74:fsync  75:fdatasync  76:truncate  78:getdents  81:fchdir  85:creat
86:link  88:symlink  89:readlink  94:lchown  95:umask  98:getrusage  99:sysinfo
```

### 100-199 号：68 项

```text
100:times  101:ptrace  103:syslog  113:setreuid  114:setregid  115:getgroups  116:setgroups
117:setresuid  118:getresuid  119:setresgid  120:getresgid  122:setfsuid  123:setfsgid  124:getsid
125:capget  126:capset  127:rt_sigpending  128:rt_sigtimedwait  129:rt_sigqueueinfo  131:sigaltstack
132:utime  133:mknod  135:personality  136:ustat  137:statfs  138:fstatfs  139:sysfs
142:sched_setparam  143:sched_getparam  144:sched_setscheduler  145:sched_getscheduler
146:sched_get_priority_max  147:sched_get_priority_min  148:sched_rr_get_interval  149:mlock
150:munlock  151:mlockall  152:munlockall  153:vhangup  154:modify_ldt  155:pivot_root  157:prctl
158:arch_prctl  159:adjtimex  161:chroot  162:sync  163:acct  167:swapon  168:swapoff  172:iopl
173:ioperm  175:init_module  176:delete_module  179:quotactl  186:gettid
187:readahead  188:setxattr  189:lsetxattr  190:fsetxattr  191:getxattr  192:lgetxattr  193:fgetxattr
194:listxattr  195:llistxattr  196:flistxattr  197:removexattr  198:lremovexattr  199:fremovexattr
```

### 200-299 号：87 项

```text
200:tkill  201:time  202:futex  206:io_setup  207:io_destroy  208:io_getevents  209:io_submit
210:io_cancel  213:epoll_create  216:remap_file_pages  217:getdents64  218:set_tid_address
219:restart_syscall  220:semtimedop  221:fadvise64  222:timer_create  223:timer_settime
224:timer_gettime  225:timer_getoverrun  226:timer_delete  227:clock_settime  229:clock_getres
230:clock_nanosleep  231:exit_group  232:epoll_wait  233:epoll_ctl  234:tgkill  235:utimes  237:mbind
238:set_mempolicy  239:get_mempolicy  240:mq_open  241:mq_unlink  242:mq_timedsend
243:mq_timedreceive  244:mq_notify  245:mq_getsetattr  246:kexec_load  247:waitid  248:add_key
249:request_key  250:keyctl  251:ioprio_set  252:ioprio_get  253:inotify_init  254:inotify_add_watch
255:inotify_rm_watch  256:migrate_pages  258:mkdirat  259:mknodat  260:fchownat  261:futimesat
262:newfstatat  263:unlinkat  264:renameat  265:linkat  266:symlinkat  267:readlinkat  268:fchmodat
269:faccessat  270:pselect6  271:ppoll  272:unshare  273:set_robust_list  274:get_robust_list
275:splice  276:tee  277:sync_file_range  278:vmsplice  279:move_pages  280:utimensat  281:epoll_pwait
282:signalfd  283:timerfd_create  284:eventfd  285:fallocate  286:timerfd_settime  287:timerfd_gettime
289:signalfd4  290:eventfd2  291:epoll_create1  294:inotify_init1  295:preadv  296:pwritev
297:rt_tgsigqueueinfo  298:perf_event_open  299:recvmmsg
```

### 300-399 号：36 项

```text
300:fanotify_init  301:fanotify_mark  302:prlimit64  303:name_to_handle_at  304:open_by_handle_at
305:clock_adjtime  306:syncfs  307:sendmmsg  308:setns  309:getcpu  310:process_vm_readv
311:process_vm_writev  312:kcmp  313:finit_module  314:sched_setattr  315:sched_getattr  316:renameat2
317:seccomp  318:getrandom  319:memfd_create  320:kexec_file_load  321:bpf  322:execveat
323:userfaultfd  324:membarrier  325:mlock2  326:copy_file_range  327:preadv2  328:pwritev2
329:pkey_mprotect  330:pkey_alloc  331:pkey_free  332:statx  333:io_pgetevents  334:rseq
335:uretprobe
```

### 400-499 号：39 项

```text
424:pidfd_send_signal  425:io_uring_setup  426:io_uring_enter  427:io_uring_register  428:open_tree
429:move_mount  430:fsopen  431:fsconfig  432:fsmount  433:fspick  434:pidfd_open  435:clone3
436:close_range  437:openat2  438:pidfd_getfd  439:faccessat2  440:process_madvise  441:epoll_pwait2
442:mount_setattr  443:quotactl_fd  444:landlock_create_ruleset  445:landlock_add_rule
446:landlock_restrict_self  447:memfd_secret  448:process_mrelease  449:futex_waitv
450:set_mempolicy_home_node  451:cachestat  452:fchmodat2  453:map_shadow_stack  454:futex_wake
455:futex_wait  456:futex_requeue  457:statmount  458:listmount  459:lsm_get_self_attr
460:lsm_set_self_attr  461:lsm_list_modules  462:mseal
```

## Linux 自身保留或 ni 的 17 项

```text
134:uselib
156:_sysctl (sys_ni_syscall)
174:create_module
177:get_kernel_syms
178:query_module
180:nfsservctl
181:getpmsg
182:putpmsg
183:afs_syscall
184:tuxcall
185:security
205:set_thread_area
211:get_thread_area
212:lookup_dcookie
214:epoll_ctl_old
215:epoll_wait_old
236:vserver
```

这 17 项是本次参考表的状态。其他带 entry point 的调用仍可能受 Linux Kconfig、架构能力或运行权限限制，不能据此推断所有 Linux 机器都可调用。
