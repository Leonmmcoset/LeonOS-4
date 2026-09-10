# Linux ABI implementation ledger

## 2026-09-10：暂停 ABI 扩展并保存工作区检查点

按用户要求，Linux ABI 后续扩展暂时暂停。保留当前实现和未完成清单；
此次提交不把局部验证通过的 syscall 提升为完整兼容。

提交前重新运行 component-config、musl-gcc-package、live-iso、Linux
vfork-stack、threads、resources、sysv-msg、sysv-sem、process-vm 和
socket-batches 定向测试，均通过。未在此次清理中重复构建或启动 QEMU；
镜像及 QEMU 的实际验证范围仍以下方先前记录为准。

清理前已逐字节校验并归档 460 个日志、截图及诊断文件。此工作站归档位置：
`/home/xiaobai/Projects/Projects/LeonOS-4-test-archives/checkpoint-20260910-173155.tar.gz`。
归档保留原始项目相对路径；本文旧 `build/...` 日志引用可从归档查阅。
归档不进入 Git。已删除专用 GCC 探针临时目录、过时实验 ISO、旧 exFAT 镜像
及 Python 缓存；保留最新桌面 ISO、installer ISO、VMDK、SDK 和构建依赖缓存。

## 2026-09-10 GCC 修复复核：vfork 边界与同步缺页信号

复核确认此前 GCC 编译链能够运行，但其异常退出测试接受正常退出码 14，
不能证明 Linux SIGSEGV 语义。相同宿主/来宾测试现共用
`tools/tests/vfork_linux_edges.c`，原来两个接受 exit(14) 的断言收紧为
`WIFSIGNALED && WTERMSIG == SIGSEGV`。修复前 QEMU 记录
`build/gcc-probe/guest-serial.edges-before.log` 的 6 项失败：备用栈继承、
STOP/CONT、定向定时器、同步缺页处理器、子进程异常状态及栈溢出状态。
这纠正了先前报告对“按 Linux 一样终止进程”的过度表述。

实际修复：

- clone 仅在 `CLONE_VM` 且没有 `CLONE_VFORK` 时清除备用信号栈，依据
  Linux v6.12 `kernel/fork.c:copy_process` 的 `sas_ss_reset` 条件。
- vfork 等待期间延后非致命信号和停止动作；SIGCONT、SIGEV_THREAD_ID、
  通用唤醒、signalfd 重配置及调度选择不再提前返回共享地址空间中的父线程。
  默认非 core 致命信号（如 SIGTERM）允许结束等待；core 默认信号继续等待
  正常交付，符合 `complete_signal` 与 `wait_for_vfork_done` 的区别。
  STOP/CONT 状态与 vfork completion 使用 scheduler_lock 协调。
- 子进程退出不在远端任务仍运行时发布 vfork completion；先退休 CPU 用户帧，
  再执行 robust futex/clear_child_tid 清理，随后唤醒父线程。exec 保留既有
  mm 提交时完成路径。依据 `exit_mm_release`/`exec_mm_release`/`mm_release`。
- Ring-3 未处理缺页改为强制 SIGSEGV，使用真实 VMA 区分 SEGV_MAPERR/ACCERR，
  siginfo 带故障地址，ucontext 保存 CR2、trap 14 和错误位。支持 SA_ONSTACK
  的同步故障处理器；被屏蔽或忽略的同步故障重置默认动作，默认退出留下信号
  wait 状态。移除原缺页直接释放 fd 并正常 exit(14) 的路径，复用统一退出清理。
  PTY 所有者清理接入统一资源释放，避免信号/缺页退出遗漏终端会话。
  调度器在信号交付终止/停止任务后重新选择任务，避免返回已失效用户帧。
- 降低 RLIMIT_STACK 后，既有栈跨度内尚未物化的页仍可缺页分配；仅扩张边界
  检查新限额。此前把限制重新套用到已有 VMA 内部，会错误撤销已有地址空间。

新增 6 组共同用例：备用栈继承、STOP/CONT、定向定时器、clear_child_tid 完成
顺序、父进程 SIGTERM，以及缺页/保护错误/栈溢出在备用栈上的 SIGSEGV 处理。
处理器核对 si_code、si_addr、CR2 和 trap 14。CHILD_CLEARTID 用 raw clone，
因为 musl 的公开 clone() 有意在用户态拒绝该 flag，不能误判为内核 EINVAL。

验证：宿主 raw 对照、真实信号队列/帧及资源限制 ASan/UBSan、内存与描述符
回归通过；48 个 UAPI 头文件与 Linux ABI 契约检查通过。新内核/SDK/探针和
独立 ext2 测试盘构建通过。QEMU 1/2 vCPU 的新增 6 组、原 8 组生命周期回归
及 GCC 六步骤均通过，最终 `DONE failures=0`，证据为
`build/gcc-probe/guest-serial.edges-final-1vcpu.log` 和
`build/gcc-probe/guest-serial.edges-final-2vcpu.log`。musl ABI 宿主静态/动态
回归也通过，见 `build/gcc-probe/musl-abi-edges.log`。这不等同完整 SMP 压力认证。
最后补入 PTY 统一清理后，PTY/线程宿主回归、构建及同一 GCC 全流程再次通过，
最新证据为 `build/gcc-probe/guest-serial.edges-final-pty-2vcpu.log`；单 CPU
结果来自该 PTY 清理增补前的同组修复。

另发现未完成的 mmap 契约：fork 前未触页的 MAP_SHARED|MAP_ANONYMOUS 页面，
父子首次缺页会各自分配不同物理页。先前共享用例在 fork 前写入，因而未覆盖；
见 `guest-serial.edges-after-2vcpu.log` 中共享握手失败及
`syscall_mm.c:task_map_anonymous_page` 的无共享 backing 分配路径。
SIGTERM 用例改用 pipe 独立验证信号生命周期，本项已单独列入 mmap 的 CSV
证据，尚未修复。还需完整共享匿名对象、bprm_stack_limits、其他 12 类 rlimit、
namespace/ptrace/core dump、完整信号竞争、LTP、VMware 及发布镜像工作流验证。
本轮未重新生成普通/installer ISO，也未执行桌面交互和完整 LTP；只构建、启动
独立 ext2 GCC 测试盘。原工具链四个二进制 SHA256 与初始归档一致。
CSV 仍为 115 missing、5 completed、81 routed、157 implemented、17 reserved；
本次没有把任何 syscall 升级为完整兼容。

## 2026-09-10 CLONE_VFORK/RLIMIT_STACK 修复与预构建 musl GCC 全链路

> 历史检查点；后续复核发现的信号/退出边界、严格测试与修正见上一节。

本轮按用户要求复核旧日志后，实际补齐 native `clone(56)` 的 `CLONE_VFORK`
生命周期和真实 `CLONE_VM` 共享、`vfork(58)` 的 Linux 语义、`getrlimit/
setrlimit/prlimit64` 的 `RLIMIT_STACK`，并让 ELF 加载器接受无 `PT_INTERP`
的 `ET_DYN` static PIE（预构建 `as`/`ld` 正是 static-pie）。依据固定本地
Linux v6.12 的 `kernel/fork.c`、`kernel/exit.c`、`fs/exec.c`、
`mm/mmap.c:acct_stack_growth`、`kernel/sys.c:do_prlimit`、`include/uapi/
linux/resource.h` 的 `_STK_LIM`/`INIT_RLIMITS` 及既有调度/信号实现，
没有修改预构建 GCC、musl 或应用。

### clone/vfork 生命周期

- `sched_clone_current` 接受 `CLONE_VFORK`，普通 legacy `clone(2)` 只消费低
  32 位（高半部按 Linux 忽略），退出信号字节按 native 规则保存；vfork(58)
  改为 `CLONE_VM|CLONE_VFORK|SIGCHLD` 走同一路径，不再以 COW fork 顶替。
- 任务结构新增 `vfork_parent`/`vfork_child`，在 `scheduler_lock` 下成对发布：
  子任务完成后才置 `TASK_READY`，父线程置 `TASK_BLOCKED`。父线程保存的返回
  帧保留子 PID；被唤醒后从同一 syscall 返回，不会重新 clone。
- 子进程 `exec` 在 `sched_exec_replace_mm` 中释放旧 mm 引用时完成等待，对应
  Linux `exec_mmap()/exec_mm_release()` 的提交点；`execve` 在提交前失败
  （路径、参数、ELF 验证、`sched_prepare_exec_current` 等）不会提前唤醒。
- 子进程最终退出（普通 exit、`exit_group`、缺页终止）在 `sched_exit` 中完成
  等待；父线程先死亡时清除子任务的父指针，避免任务槽复用后的悬空引用；
  `sched_release_task_resources` 再做一次幂等清理。
- 等待按 Linux `TASK_KILLABLE` 语义处理：非致命且有用户处理器的信号保持
  pending，不把父线程从 vfork 等待中唤醒；SIGKILL/默认致命动作走进程退出，
  在退出路径解除链接。SMP 下完成与父线程仍在自己 CPU 上的竞争由
  `scheduler_lock` 和 `running_cpu` 所有权协议处理。
- `CLONE_VM` 仍使用现有共享 mm 对象和同一 CR3，父子 VMA/页表真实共享；
  QEMU 用例由子进程写共享变量并在退出/exec 前被父进程观察到，排除 COW
  fork 冒充。
- ELF 加载器新增 static PIE：`ET_DYN` 且无 `PT_INTERP` 时按 load bias 直接
  使用镜像入口（`AT_BASE=0`），使 `as`/`ld` 可被 exec；带 `PT_INTERP` 的
  动态路径和既有解释器校验保持不变。

### RLIMIT_STACK

- `task_rlimit_state` 增加真实 `stack` 字段；新任务默认
  `{8 MiB, RLIM_INFINITY}`，来自 Linux v6.12 `_STK_LIM` 与 `INIT_RLIMITS`，
  不读取或照搬宿主机配置。
- `getrlimit(97)`、`setrlimit(160)`、`prlimit64(302)` 现在支持 RLIMIT_STACK：
  保留 native 16 字节结构、`RLIM_INFINITY`、软硬限制比较、`CAP_SYS_RESOURCE`
  提权检查、`RLIMIT_NOFILE` 上限特例，以及先复制输入、提交更新、后写旧值
  的顺序；线程通过 `shared_limits` 共享，fork 继承，exec 保留。
- 栈增长按 `acct_stack_growth` 的核心规则实现：候选扩张后的完整栈跨度
  `stack_top - new_page` 大于 `rlim_cur` 即拒绝，`RLIM_INFINITY` 只关闭该
  检查；`RLIMIT_AS` 的累计虚拟地址检查继续独立执行。
- native 栈窗口从 8 MiB 扩为 64 MiB（`NTCLKS_USER_STACK_MAX_PAGES`），
  低于 mmap 区；该 64 MiB 是平台地址窗口上限，不是把限制返回值固定成
  64 MiB。设置小限额后，越限的栈缺页按 Linux 一样终止进程，而不是静默
  映射；现有 exec 参数/环境区上限 8 KiB 仍低于 Linux `ARG_MAX` 下限，未
  改变用户可见契约。

### 实际验证

- 宿主 Linux raw syscall 对照 `tools/test_linux_vfork_stack.py` PASS：正常/失败
  clone flags、CLONE_VM 可见性、exec 提交时唤醒、退出等待、被捕获信号不打断
  `TASK_KILLABLE`、父进程 SIGKILL、vfork(58)、RLIMIT_STACK 默认/往返/越限
  SIGSEGV/fork+exec 继承，见 `build/host-regressions-20260910.log`。
- 预构建 GCC 15.1.0（归档 SHA256 未变，四个二进制哈希见
  `build/gcc-probe/binary-sha256.json`）在 QEMU/KVM、OVMF、q35、4 GiB、
  1 vCPU 下依次完成 `--version`、`-v`、`-E`、`-O0 -c`、`-static` 链接和
  生成程序执行；生成程序含 stdio，探针校验输出 `GCC_GENERATED_OK` 和退出码 0，
  最终 `[gcc-probe] DONE failures=0`：`build/gcc-probe/guest-serial.log`。
- 同一探针先运行 raw/musl 回归并全部 PASS：真实 CLONE_VM 共享与 8 次
  wait/reap、vfork(58)、exec 提交唤醒、被捕获 SIGUSR1 延迟、父进程致命退出、
  子进程缺页异常退出、失败回滚、pthread create/cond/futex/join 8 次、RLIMIT_STACK
  默认/往返/实际约束/继承：`build/gcc-probe/guest-serial.run4-parentexit.log`。
- 同一镜像再以 q35、2 vCPU 运行，相同探针和 GCC 全链路仍为
  `DONE failures=0`：`build/gcc-probe/guest-serial.smp2.log`。这只是 SMP 启动和
  并发调度压力冒烟，不是多核并行完整认证。
- 宿主回归 `test-linux-resources`、`test-linux-threads`、`test-linux-descriptors`、
  `test-linux-memory`、`test-linux-vfork-stack`、`test linux-abi-contract`、
  `test uapi` 全部 0 errors：`build/host-regressions-20260910.log`；
  `musl-probes` 静态/动态构建与 `test musl-abi` 结果见
  `build/musl-probes-20260910.log`、`build/musl-abi-20260910.log`。
- 复现命令：先 `LEONOS_GCC_ARCHIVE=<archive> python3 build.py run gcc-probe-image`，
  再 `python3 tools/run_gcc_probe_qemu.py --smp 1`（或 `--smp 2`）。本轮脚本日志：
  `build/gcc-probe/guest-serial.script-1vcpu.log`、
  `build/gcc-probe/guest-serial.script-2vcpu.log`。
- CSV 把 clone/vfork/getrlimit/setrlimit/prlimit64 的证据更新为本轮 QEMU 结果；
  状态计数相应变为 115 missing、5 completed、81 routed、157 implemented、
  17 reserved；局部通过对不把整项标记为 completed。
- 本轮没有重新运行 LTP：既有 `build/musl/final-ltp-serial.log` 等 pthread
  结果作为历史证据保留，不能与本轮 QEMU 结果混称；需要针对 clone/vfork 的
  LTP 子集和 VMware 运行仍待补。

### 明确未完成

- `clone` 的 `CLONE_PIDFD`、`CLONE_PTRACE`、namespace/cgroup/io/time 等标志仍
  返回 ENOSYS/EINVAL；clone3 的 pidfd/set_tid/cgroup 字段、vfork 与完整
  siginfo/`SA_NOCLDWAIT`/reparent 语义、Linux ptrace 交互没有实现或认证。
- rlimit 只支持 NOFILE/AS/SIGPENDING/STACK；其余 12 个资源仍 ENOSYS，
  `RLIMIT_CPU` 的 SIGXCPU、reaped/僵尸任务访问、完整 capability 与
  namespace/LSM 检查、exec 后 AS 再收紧等尚未完成。RLIMIT_STACK 的 64 MiB
  平台窗口和 1/4 栈参数的 Linux 精确 `bprm_stack_limits` 交互也仍需更多用例。
- static PIE 仅完成无解释器 ET_DYN 的直接入口加载；未实现 PT_GNU_RELRO、
  更多动态标签/异常布局和完整 ASLR 策略认证。
- 未运行 VMware；未运行完整 LTP；QEMU 结果只覆盖 BSP/2 vCPU 冒烟，不替代
  计划中的 VMware 与完整多核/压力验证。

## 2026-09-10 预构建 musl GCC 来宾实测（修复前基线）

> 修复前证据，保留用于对照；修复结果见上一节。

用户提供 `dyne-gcc-musl-x86_64.tar.xz`，GCC 版本 15.1.0。归档 SHA256：
`31420e4f978e7ccbcc597ca5e18c2dcbe640ea6985ed6325112d8a171a72ece3`。
直接解包到来宾 `/opt/dyne`，没有修改或重编译 gcc、cc1、as、ld；四个程序的
SHA256 保存在 `build/gcc-probe/binary-sha256.json`。gcc 与 cc1 是静态链接的
native x86-64 ELF，没有 PT_INTERP，不依赖 LeonOS 的动态加载器或 glibc。

实际环境：QEMU/KVM、OVMF、q35、host CPU、1 vCPU、4 GiB RAM；独立 GPT
测试盘包含 FAT32 ESP 和 ext2 根分区，以 snapshot 模式启动当前构建的内核、
loader 和 middlelayer，复用现有 `build/esp` 的用户态文件。不是 installer
或 ISO 发布验证，也未验证 VMware。启动探针自动执行以下步骤：

| 步骤 | LeonOS 实际结果 |
| --- | --- |
| `gcc --version` | 输出 GCC 15.1.0，退出 0 |
| `gcc -v` | 输出构建配置，退出 0 |
| `gcc --sysroot=... -E gcc-probe.c` | 退出 1，无法通过 posix_spawn 启动 cc1 |
| `gcc --sysroot=... -v -O0 -c gcc-probe.c` | 同样退出 1 |
| 链接及运行生成程序 | 编译失败，未执行 |

直接阻塞是 `clone(56)` 的参数语义缺失：来宾日志第 631-632、910-911 行显示
flags 为 `0x4111 = CLONE_VM | CLONE_VFORK | SIGCHLD`，返回 `-38` / ENOSYS。
`sched_clone_current` 的 supported mask 未包含 CLONE_VFORK，实际在 flags
校验处拒绝；不是缺少 56 号分发入口。GCC 随后报告
`cannot execute .../cc1: posix_spawn: Function not implemented`。
posix_spawn 是 libc API，本次触发的 raw syscall 是 clone。项目 musl
`src/process/posix_spawn.c` 使用相同 flags；原预构建 GCC 在宿主 strace 中
也确实使用该组合。宿主运行原 GCC 编译空 C 翻译单元退出 0，产出目标文件，
证据为 `build/gcc-probe/host-compile.trace` 和 `host-empty.o`。

另有 `prlimit64(302, RLIMIT_STACK)` 和回退 `getrlimit(97, RLIMIT_STACK)`
均返回 ENOSYS：四次 GCC 启动各出现一对，共 8 次；资源编号为 3。
`process_resource_limit` 当前只支持 NOFILE、AS、SIGPENDING，未实现 STACK。
这些失败在本次调用中非致命，GCC 继续执行，不能与 clone 的直接阻塞混称。
此次跟踪总共观察到 10 次 ENOSYS，均来自上述三个编号。cc1、汇编器和链接器
尚未在来宾中执行，不能据此声称补齐这三个缺口便满足整个 GCC 工具链。

后续修复需要真实 CLONE_VFORK 父调用线程等待、共享地址空间、子进程 exec/exit
释放及唤醒生命周期；不能只放行 flag 或把 clone 强制改成 fork。RLIMIT_STACK
也需要真实限制值、继承、修改及栈增长约束。本次仅增加诊断与测试设施，未修改
这些 ABI 行为，也未将相关调用标记完成，CSV 总体状态计数不变。

重现镜像构建（需已有普通系统的 `build/esp` 暂存树）：

```sh
LEONOS_GCC_ARCHIVE=/home/xiaobai/下载/dyne-gcc-musl-x86_64.tar.xz python3 build.py run gcc-probe-image
qemu-system-x86_64 -enable-kvm -cpu host -machine q35 -m 4096M -smp 1 \
  -bios /usr/share/edk2/x64/OVMF.4m.fd -display none \
  -serial file:build/gcc-probe/guest-serial.log \
  -device VGA,xres=1280,yres=720 -netdev user,id=net0 -device e1000,netdev=net0 \
  -drive file=build/gcc-probe/gcc-probe.vmdk,if=none,id=sata0,format=vmdk,snapshot=on \
  -device ich9-ahci,id=ahci -device ide-hd,drive=sata0,bus=ahci.0 -no-reboot -no-shutdown
```

本次镜像构建退出 0，来宾报告 `[gcc-probe] DONE failures=2`；QEMU 随后经 QMP
退出。`autospawn=gcc` 启动测试器，`syscall-trace=/opt/dyne/` 仅跟踪匹配可执行
路径的 syscall 编号、六个原始参数和返回值；普通启动默认关闭。完整来宾证据：
`build/gcc-probe/guest-serial.log`。本节保留关键失败证据，避免后续清理 build
日志后丢失结论。

## 2026-09-10 SysV 信号量实现检查点

新增 `semget(64)`、`semop(65)`、`semctl(66)`、`semtimedop(220)` 的 native x86-64
分发与实际信号量数组，状态为 `implemented_pending_runtime` / `partial_host`。
当前 375 项：115 项缺少分发、154 项实现待补齐或验证、84 项原有入口未完整认证、
5 项历史完成标记、17 项 Linux 保留/ni。下文计数为历史快照，完整范围未缩减。

依据本地 Linux v6.12 `ipc/sem.c`、`ipc/util.c`、`kernel/fork.c`、`kernel/exit.c`、
`kernel/signal.c`、`arch/x86/include/uapi/asm/sembuf.h` 和 IPC UAPI。

- 公共 `include/uapi/linux/sem.h` 定义 native 104 字节 semid64_ds、6 字节 sembuf、
  40 字节 seminfo 和操作常量。x86-64 的 otime/ctime 后有额外保留槽，不能套用
  asm-generic 的布局；ctime 在偏移 64，nsems 在偏移 80。内核 nsems 为 64 位，
  musl 使用 unsigned short 加保留填充，默认上限 32000 在其有效范围内。
- 真实分配零初始化数组，支持 PRIVATE、key、CREAT/EXCL、默认数量/总量上限、
  循环 ID 和 generation。权限按 effective UID、fsgid/附加组、owner/creator、
  CAP_IPC_OWNER 和 SET/RMID 的 CAP_SYS_ADMIN 判定，保留各操作的检查顺序。
- semop 的增减/等待归零向量原子执行，重复索引按序计算；阻塞、NOWAIT、ERANGE
  均回滚已经修改的值和 undo。输入复制后保存在任务内，重试不读取修改后的用户
  数组；生产者执行完整等待向量，提交结果后唤醒。零等待者优先、复杂向量队列
  合并/拆分、首个未满足操作的 GETNCNT/GETZCNT 计数按源码处理。
- SEM_UNDO 维护每个 owner/array 的真实调整量，范围为 [-32768,32767]。
  CLONE_SYSVSEM 共享引用，包括父进程尚未操作时的空列表；普通 fork 不继承。
  exec 保留调整量，最后一个引用退出时应用并夹到 [0,32767]，记录退出者 TGID、
  更新时间并唤醒等待者。SETVAL/SETALL 清除相应调整，RMID 移除关联和等待对象。
- semctl 支持 GETPID/VAL/ALL/NCNT/ZCNT、SETVAL/ALL、IPC_STAT/SET/RMID/INFO、
  SEM_STAT/INFO/STAT_ANY，返回真实值、计数和时间。SETALL 全部导入并验证后提交；
  STAT_ANY 跳过 DAC，按 index 返回完整 ID；native 命令不接受 IPC_64 位。
  IPC_SET 不重新验证已排队操作的权限，也不唤醒它们，此处与消息队列行为不同。
- semtimedop 先导入 timeout，再按源码顺序验证数量/操作/ID/时间；保存单调绝对
  deadline，零超时仍先尝试操作，超时返回 EAGAIN 且不修改用户 timespec。
  非阻塞和超时 EAGAIN 不再被通用 I/O 路径吞掉重试。
- 已提交成功/错误或超时先于 handler 交付；捕获信号始终 EINTR，包括 SA_RESTART。
  修正默认停止路径：group stop 前移除尚未完成的 semaphore 等待，SIGCONT 后
  返回 EINTR，没有 handler 也不重启；已提交的结果保持不变。信号、exec、退出
  清理在途向量和对象引用，RMID 后等待者不会访问复用的新 ID。
- musl 使用原 Linux 包装器，SDK 共享 UAPI；原来这些调用返回 ENOSYS，现在按
  原 Linux 编号提供实现，无需移植应用或保留另一套私有信号量 ABI。
  raw 用例加入正式静态/动态 musl 探针 `sysv_sem_syscalls`。

实际验证：

- `python3 build.py run test-linux-sysv-sem` 通过：真实内核实现由 ASan/UBSan
  驱动，覆盖原子回滚、重复索引、输入快照、队列顺序/计数、权限更新、undo
  范围及共享/退出、SETALL 原子性、RMID 引用、分配/复制失败和超时。
  时钟、用户内存服务及等待调度部分使用测试桩，不是完整内核运行。
- 同目标以宿主 Linux 头文件/raw syscall 验证布局、参数宽度、错误顺序、数组
  操作、fork/CLONE_SYSVSEM、SA_RESTART 下 EINTR 和停止/继续后等待计数/EINTR。
  宿主为 `7.2.2-1-cachyos-bore-lto`，Linux 6.12 依据为固定本地源码。
- `run test-linux-threads` 的真实调度函数与信号帧测试通过。新增停止用例在
  修复前复现“继续等待”的错误，修复后验证组内移除、EINTR 和完成结果优先。
  `run test-linux-descriptors`、`test musl-abi`、`test uapi` 通过；48 个独立
  C/C++ UAPI 头文件及 musl 原生布局通过检查。内核、SDK、静态/动态探针构建通过。
  本批未运行 guest 探针、LTP、ISO、QEMU/Vim 或 VMware，既有编译警告仍存在。

明确未完成：IPC/user/PID namespace、可配置 LSM、`/proc/sysvipc/sem`、信号量
sysctl/next_id/扩展 ID 配置、unshare(CLONE_SYSVSEM)、完整 PID 对象引用与复用。
当前固定 Linux 默认上限并使用全局执行锁；Linux 的对象细粒度并发、远端 CPU
停止/退出/exec 竞争及实际调度压力尚未验证。超时仍受现有 100 Hz 唤醒机制限制，
尚未实现 Linux hrtimer/timer_slack 的完整行为。以上仍在任务范围内，不能把这
四项标为 completed，也不能以局部宿主通过代替 LeonOS 完整运行认证。

## 2026-09-10 SysV 消息队列实现检查点

新增 `msgget(68)`、`msgsnd(69)`、`msgrcv(70)`、`msgctl(71)` 的 native x86-64
分发及真实队列实现，状态为 `implemented_pending_runtime` / `partial_host`。
当前 375 项：119 项缺少分发、150 项实现待补齐或验证、84 项原有入口未完整认证、
5 项历史完成标记、17 项 Linux 保留/ni。下文计数均为历史快照，完整范围未缩减。

依据本地 Linux v6.12 `ipc/msg.c`、`ipc/msgutil.c`、`ipc/util.c`、
`include/linux/ipc_namespace.h`、`kernel/groups.c` 和 native IPC UAPI。

- 公共 `include/uapi/linux/{ipc,msg}.h` 定义 48 字节 ipc64_perm、120 字节
  msqid64_ds、32 字节 msginfo 和 Linux 常量；内核编号复用共享 syscall 表。
  key/id/flags/cmd 按 32 位取值，消息类型及选择器为有符号 64 位，长度为 64 位。
  musl 的原生 SysV 包装器无需改动；SDK 重新打包，静态/动态探针加入本组 raw 调用。
  旧二进制此前得到 ENOSYS，现在使用原 Linux 入口，不引入另一套消息 ABI。
- 队列支持 IPC_PRIVATE、key 查找、CREAT/EXCL、默认 32000 队列上限、8192 字节
  单条上限、16384 字节默认容量、Linux 默认 15 位 index 的循环与 generation。
  删除后旧 ID 无效；正在等待的操作保留原对象，不会误用复用后的新队列。
- 权限按 effective UID 与 owner/creator 比较，组检查使用 fsgid/附加组及
  owner/creator group。CAP_IPC_OWNER 绕过读写 DAC，CAP_SYS_ADMIN 用于 SET/RMID
  的所有者检查；CAP_SYS_RESOURCE 控制超过默认上限的容量调整。
  IPC_SET 更新 uid/gid/mode 而保留 creator；拒绝无效 UID/GID。
- msgsnd 先读取 mtype，再验证 id/长度/正类型，分配并复制载荷后才查队列和权限。
  零字节消息仍占一个容量单位，非阻塞满队列返回真实 EAGAIN，不进入通用 I/O 重试。
  阻塞任务内保存等待节点及输入快照；应用修改原缓冲区不影响已进入内核的消息。
- msgrcv 支持 FIFO、正类型、MSG_EXCEPT、负类型最小值选择及 LONG_MIN。
  E2BIG 保留消息；正常接收先消费再复制，EFAULT 保留消费和已写入的页。
  MSG_NOERROR 截断；MSG_COPY 按 ordinal 复制且不消费，遵循先读取临时缓冲区、
  必须 NOWAIT、排斥 EXCEPT、COPY+NOERROR 空间不足仍 EINVAL 的源码行为。
- 直接交付扫描等待接收者，过小者收到 E2BIG，继续寻找可容纳者；直接交付不增加
  队列计数，lrpid 记录接收 TID，常规取队列记录 TGID。status 时间和 INFO/MSG_INFO
  来自真实对象和计数。MSG_STAT/STAT_ANY 按 index 查找并返回完整 ID；STAT_ANY
  跳过 DAC。native msgctl 不接受额外 IPC_64 命令位，输出填充/保留字段清零。
- IPC_SET 唤醒接收者重新检查权限，并唤醒可容纳的发送者；IPC_RMID 移除 key/ID、
  释放队列消息、唤醒原等待者返回 EIDRM。已经直接交付的消息仍可完成接收。
  clone 清空在途状态，完成、handler、exec 和退出路径接入等待/载荷/引用清理。
- 收发被捕获的信号中断时均返回 EINTR，即使 SA_RESTART；已经提交的直接交付、
  E2BIG 或 EIDRM 先恢复 syscall 再交付 handler。等待发布后检查 pending，唤醒在
  调度锁下仅修改 BLOCKED 状态，保留尚未退栈 CPU 的所有权，不恢复 STOPPED/EXITED。

实际验证与纠正：

- `python3 build.py run test-linux-sysv-msg` 通过。真实队列函数由 ASan/UBSan
  驱动，涵盖复制/选择/权限/计数、容量、分配失败、阻塞快照、直接交付、SET/RMID、
  引用清理和 ID 重用。时钟、用户内存和等待调度为测试桩，不是完整内核运行。
  初次测试在实现缺失时失败，增加实现后通过。
- 同目标使用宿主 Linux 头文件和 raw syscall 对照布局、参数宽度、错误顺序、
  COPY、容量、跨保护页复制失败和 SA_RESTART 下的 EINTR。宿主为
  `7.2.2-1-cachyos-bore-lto`，固定 6.12 依据是上述本地源码，未混称为 6.12 实机测试。
  对照发现 msgctl_down 虽接收 int，但 msg_ctlmnb 是 unsigned int；已按该类型
  转换修正负截断值的 CAP_SYS_RESOURCE 检查。musl 不声明 MSG_COPY，raw 测试
  直接引用 Linux 源码中的 040000，不修改 musl 或应用。
- `run test-linux-threads` 通过，新增真实信号帧 EINTR/优先完成断言及真实调度函数
  对远端 CPU 所有权、STOPPED/EXITED 的检查；`run test-linux-descriptors` 通过。
  `test musl-abi` 的 UAPI 布局和 musl+mimalloc 宿主静态/动态回归通过。
  内核、SDK 和静态/动态 musl 探针构建通过；既有 unused 等警告仍存在。
  本批未运行 guest 探针、LTP、ISO、QEMU/Vim 或 VMware。

明确未完成：IPC/user/PID namespace、可配置 LSM、`/proc/sysvipc/msg`、消息队列
sysctl 调整/扩展 ID 模式及 checkpoint 的 next_id 接口；与完整 PID 对象引用和
复用的集成；Linux 的 RCU/对象细粒度并发、停止/继续及多核实际调度压力验证。
当前固定上限取 Linux 默认值，系统使用单 IPC namespace 和现有全局执行锁。
这些配套缺口仍在任务范围内，不能把本批四项标为 completed。

## 2026-09-10 process_vm 实现检查点

新增 `process_vm_readv(310)`、`process_vm_writev(311)` 的 native x86-64 入口。
两项为 `implemented_pending_runtime` / `partial_host`。当前 375 项：123 项缺少
分发、146 项实现待补齐或验证、84 项原有入口未完整认证、5 项历史完成标记、
17 项 Linux 保留/ni。入口计数不等同完整兼容率，所有旧缺口继续在任务范围内。

依据本地 Linux v6.12 `mm/process_vm_access.c`、`lib/iov_iter.c`、`mm/gup.c`、
`kernel/fork.c:mm_access`、`kernel/ptrace.c:__ptrace_may_access`、
`security/commoncap.c:cap_ptrace_access_check` 和 `kernel/cred.c:commit_creds`。

- flags 首先检查；本地 iovec 导入、零总长度、远端 iovec 导入、页数组分配、
  目标查找与权限按源码顺序执行。pid 为有符号 32 位；local_count 在 import_iovec
  截为 32 位，remote_count 保留 64 位检查。逐项先读长度再读地址，负长度 EINVAL。
  区分单向量先 MAX_RW_COUNT 截断、多向量先 access_ok；不提前验证远端数据地址。
- 按读取者真实 UID/GID 对照目标 real/effective/saved ID，检查共享 MM dumpable、
  permitted capability 子集和有效 CAP_SYS_PTRACE。自身、共享 MM、同线程组使用
  Linux 的豁免规则；无任务或已失去地址空间返回 ESRCH，拒绝访问返回 EPERM。
- 有效 ID、fs ID 或 permitted capability 增加前，将共享 MM 设为 nondumpable，
  覆盖现有 set*id/capset 与登录身份路径；capability 仅减少不触发此变化。
  该默认行为对应 suid_dumpable=0，完整凭据/exec/namespace 语义仍有下述缺口。
- 实际读取两端页表，远端检查 VMA READ/WRITE，拒绝设备映射及 PROT_NONE，
  接入真实 COW/惰性页处理；远端 GUP 不向初始栈低边界以下扩栈。
  每批最多固定 1024 个远端页，栈内保存 16 个引用，较大批次真实分配最多 8 KiB。
  先解析并保留远端物理页/页缓存引用，再访问本地数据，保留本地故障前远端已完成
  的缺页/COW 副作用；全部成功和错误路径释放引用及 iovec 数组。
- 复制连续跨越双方向量与页边界；长度取两侧总长度的较小者，后续 EFAULT 返回
  已复制字节，即使只有一条 iovec 也可按页部分完成。没有预先复制所有用户载荷，
  没有把远端地址当成当前地址空间指针。调用仍使用现有执行锁保护任务和 MM 生命周期。
- musl 的 process_vm 包装器继续使用原 Linux 调用，新增用例加入正式静态/动态
  musl 探针 `process_vm_syscalls`；共享 UAPI 增加 CAP_SYS_PTRACE，没有应用移植。

实际验证：

- `python3 build.py run test-linux-process-vm` 通过。真实跨进程复制函数与 x86-64
  页表/COW 函数由 ASan/UBSan 驱动，验证错误顺序、权限、长度、部分复制、设备/
  VMA 拒绝、分配失败、预先固定远端页及所有引用释放。缺页服务由测试桩模拟，
  不是完整调度/MM 实际执行；实现缺失的用例先失败，增加代码后通过。
- 同目标运行 `tools/tests/process_vm_abi_test.c`，以宿主 Linux 头文件/raw syscall
  验证双向向量复制、计数/PID 宽度、零长度、guard page、读写保护、fork 私有页
  隔离及 nondumpable 拒绝。宿主 `7.2.2-1-cachyos-bore-lto`；6.12 依据为固定源码。
- `run test-linux-resources` 验证共享 MM 的 dumpability 转换；原内存页表/ELF 页
  缓存、线程/信号回归通过。内核及静态/动态 musl 探针构建通过，仍有原 unused
  等编译警告。`test uapi`、`test linux-abi-contract` 与 `git diff --check` 通过。
  本批未运行 guest 探针、LTP、ISO、QEMU/Vim 或 VMware。

明确未完成：用户地址空间仍受 LeonOS 512 MiB 布局限制；memfd/shm 目前标记为
设备映射，本接口因而拒绝，不能据 Linux 的 MMIO 拒绝规则宣称这些普通内存对象
已兼容。共享文件页写回、dirty 跟踪、完整页固定与并发 unmap/truncate/exec、
多核 TLB/COW 竞争待完善；全局执行锁和远端缺页等待不等同 Linux 可中断的
exec_update_lock/mmap_lock。user/PID namespace、可配置 LSM/Yama、ptrace/exec
凭据及 suid_dumpable=2、setuid/capability 全部契约仍未完成。支持本批复制路径
不表示这些两项已完全复刻 Linux 6.12，不能标为 completed。

## 2026-09-10 signalfd 实现检查点

新增 `signalfd(282)`、`signalfd4(289)` 的 native x86-64 入口和真实信号队列读取。
两项状态为 `implemented_pending_runtime`，验证范围为 `partial_host`。当前 375 项：
125 项缺少分发、144 项实现待补齐或验证、84 项原有入口未完整认证、5 项历史完成
标记、17 项 Linux 保留/ni。下文旧检查点计数为历史快照，全部未完成项仍在范围内。

依据本地 Linux v6.12 `fs/signalfd.c`、`fs/anon_inodes.c`、`fs/read_write.c`、
`fs/ioctl.c`、`lib/iov_iter.c` 及 `kernel/signal.c`。

- UAPI 增加 128 字节 signalfd_siginfo 的布局及 SFD_NONBLOCK/CLOEXEC 常量。
  native mask 为 8 字节，按 size、输入复制、flags、fd 的顺序校验；过滤 KILL/STOP。
  更新已有 fd 的 mask 作用于共享 OFD，创建 flags 在更新时只校验、不改变状态。
- 从读取任务的私有/共享信号队列取记录，不读取创建者的队列；沿用私有队列优先、
  同信号 FIFO 和实时载荷。转换 Linux siginfo_layout 对应的 sender、RT、timer、
  poll、fault、child、sys 字段，未使用字段清零。其他信号源仍有下述配套缺口。
- read/readv 返回完整 128 字节记录，只有第一条允许阻塞；零长度 read 为 EINVAL，
  零总长度 readv 为 0。非阻塞和 RWF_NOWAIT 返回真正的 EAGAIN，不走内核 I/O 重试。
  先消费信号再 copyout；失败保留已复制字节和消费副作用，已有完整记录优先返回。
- readv 导入并保留跨阻塞的向量副本，可跨多个 iovec 复制一条记录。区分单向量
  ITER_UBUF 先截断 MAX_RW_COUNT 与多向量先 access_ok；处理 RWF_*、长度和计数。
  阻塞发布后重新检查 pending，信号到达及共享 sighand 的 mask 更新可唤醒等待。
  已有匹配记录时先恢复读取，再处理用户 handler；readv/preadv/preadv2 的信号帧
  区分 EINTR 与 SA_RESTART。clone 清空在途状态，完成、信号、exec、退出释放副本。
- 接入 poll/epoll 的 POLLIN，dup/fork 共享 mask 与 O_NONBLOCK，FD_CLOEXEC 独立。
  FIONBIO、FIOCLEX/FIONCLEX 可用；write/writev 拒绝 EINVAL，位置读取返回 ESPIPE，
  preadv2 的 offset=-1 走流式读取；noop lseek 和 whence 的 32 位截断遵循源码。
  新增文件 kind 标识，避免把 Linux open flags 位复用为新的对象类型。
- musl/SDK 使用共享 UAPI 与原 Linux 调用约定；新 raw 用例加入静态/动态探针
  `signalfd_syscalls`，没有修改应用，也未添加旧私有编号二进制兼容层。

实际验证：

- `python3 build.py run test-linux-threads` 通过。真实 signal/signalfd 队列代码
  ASan/UBSan 用例覆盖 mask、FIFO、读者身份、readv、故障消费、阻塞恢复、payload，
  调度器单测覆盖等待发布后检查及 mask 更新唤醒。部分调度/复制服务用测试桩隔离。
  新增单向量超长用例在修复前失败，按 6.12 截断顺序修复后通过。
- 同一目标运行 `tools/tests/signalfd_abi_test.c`，宿主 Linux raw syscall 对照
  通过，包括错误顺序、dup/ioctl、poll/epoll、readv、guard page、fork 队列隔离。
  宿主为 `7.2.2-1-cachyos-bore-lto`，6.12 依据是固定版本源码。
- 信号帧目标地址空间 unittest 通过；新增用例先复现提前投递 handler 的错误，
  修复后验证 signalfd 先恢复读取，以及向量调用的 EINTR/SA_RESTART 保存上下文。
- `run test-linux-descriptors`、`run test-musl-abi`、`test uapi` 和内核构建通过；
  musl 检查核对 signalfd 20 个字段偏移、128 字节大小和 flags。
  静态/动态 musl guest 探针构建通过。本批未执行 guest 探针、LTP、ISO、QEMU/Vim
  或 VMware；已有 unused 等编译警告仍在。不能用宿主结果代替 LeonOS 运行结果。

未完成的具体语义：POSIX timer 的预分配 siginfo/overrun、SIGCHLD 和其他信号源
仍需与新队列完整整合；当前字段转换正确不代表源头信息完整。匿名 inode 的真实
元数据、fstat/fstatfs、共享 inode chmod/chown、procfs fd/fdinfo 尚未实现，当前
旧 stat 路径仍把 signalfd 当设备节点。poll/epoll 仍依赖既有扫描模型，缺少完整
Linux sighand waitqueue 注册及 fork 后 epoll 唤醒关系。默认致命信号的组退出处理、
完整 SMP 和 fd 关闭复用/exec 竞争仍需继续实现及运行验证；本批不能认证完全兼容。

## 2026-09-10 消息批量调用实现检查点

新增 `recvmmsg(299)` 与 `sendmmsg(307)` 的 native x86-64 入口和实际 Unix socket
批量处理，状态为 `implemented_pending_runtime`。当前 375 项：127 项缺少分发、
142 项实现待补齐或验证、84 项原有入口未完整认证、5 项历史完成标记、17 项保留/ni。
下文旧检查点中的计数是当时快照。所有剩余调用及已有实现缺口仍在任务范围内。

依据本地 Linux v6.12 `net/socket.c` 的 `__sys_sendmmsg`、`do_recvmmsg`、
`____sys_recvmsg`、`__copy_msghdr`，`net/unix/af_unix.c` 和 `lib/iov_iter.c`。

- 共享 UAPI 定义 64 字节 mmsghdr、56 字节 msghdr 和 msg_len 偏移 56。
  fd 为有符号 32 位，vlen/flags 为无符号 32 位，用户指针和长度保留 64 位。
  sendmmsg 限制 vlen 为 1024；recvmmsg 不套用此限制。零 vlen 仍检查 socket fd。
- 发送按条使用 MSG_BATCH，最后一条恢复原 flags；输入 msg_flags 只允许带入 MSG_EOR。
  短流式发送停止批次；错误前已完成的条数优先返回。msg_len 在真实发送/接收后写回，
  写回故障不会回滚数据或已安装的 SCM_RIGHTS fd，也不会把失败条目计入完成数。
- 接收保留批次进度，WAITFORONE 在第一条之后启用 DONTWAIT；部分成功后的非 EAGAIN
  错误保存在 socket SO_ERROR。timeout 检查范围、饱和相加和剩余值写回遵循源码；
  Linux 在单条 recvmsg 阻塞时不检查该批次 timeout，零 timeout 仍可等待第一条。
  SO_RCVTIMEO 独立处理；timeout 输出故障在已有接收副作用后返回 EFAULT。
- 通过原 Unix 数据报队列、流式环形缓冲区及 SCM_RIGHTS 引用传递处理每条消息。
  导入并检查内核 iovec 副本，按 MAX_RW_COUNT 截断总长度；数据报在实际复制失败时
  已消费，MSG_PEEK 保持队列。消息头只依序写 namelen/flags/controllen，不覆盖输入
  指针或 padding；保留返回的 MSG_CMSG_CLOEXEC。部分 WAITALL 后 EOF/超时也写消息头。
- 信号中断返回已完成的消息数，部分 WAITALL 的 msg_len 和 timeout 经目标任务页表
  写回；区分无限等待的 ERESTARTSYS 与 SO_RCVTIMEO 的 EINTR。任务保留 socket OFD，
  clone 清空批次状态，退出/exec/完成路径清理引用。单条操作完成后重置 socket timeout。
- 将信号帧原有目标页表复制提取为 user_copy_to_task，复用带目标 task 的真实缺页
  处理函数，支持合法惰性页和 COW，避免当前 CR3 指向其他任务时写错地址空间。
- musl/SDK 继续使用原生 Linux 编号和结构，新 raw 用例加入静态/动态 musl 探针的
  `socket_batch_syscalls`。未更改应用，也未增加旧私有 syscall 二进制兼容层。

实际验证：

- `python3 build.py run test-linux-socket-batches`：真实批量状态机及 Unix 后端的
  ASan/UBSan 测试通过，覆盖包边界、部分结果、阻塞恢复、短发送、输出故障、
  数据报消费/PEEK、SCM_RIGHTS 引用和 CLOEXEC、SO_ERROR、WAITFORONE 与超时。
  协议状态测试隔离时钟/调度/对象表服务，不是完整内核运行结果。
- `tools/tests/socket_batch_abi_test.c` 在宿主 Linux 上用 Linux 头文件和 raw syscall
  对照通过；宿主版本 `7.2.2-1-cachyos-bore-lto`，6.12 依据是上列固定版本源码。
- 目标地址空间信号帧 unittest 通过，覆盖批次消息数、msg_len、timeout、red zone、
  siginfo 与惰性页触发。真实内存映射、ELF 页缓存测试 `run test-linux-memory` 通过。
- `run test-linux-descriptors` 首次因既有 flock 关闭路径缺少等待队列链接而失败；
  测试目标补入真实 wait.c 后通过，并新增最后 OFD 引用关闭才释放锁、等待者出队
  的断言。测试未覆盖真实调度唤醒，未改动内核预期行为来消除失败。
- `run test-linux-threads`、`run test-musl-abi`、`test linux-abi-contract`、`test uapi`
  通过；内核与静态/动态 musl guest 探针构建通过。内核仍有既有 unused/sign-compare
  等编译警告。本批没有执行 guest 探针、LTP、ISO、QEMU/Vim 或 VMware 验证。

明确未完成的语义：INET sendmsg/recvmsg 后端仍不具备 Linux 消息接口，批量调用对此
返回 EOPNOTSUPP；需继续实现协议功能，不能把它当成有效 Linux 配置裁剪。Unix OOB、
完整辅助数据/错误队列、缓冲限制和并发也未齐全。iovec 仅在单次处理期间捕获，跨阻塞
恢复仍重新读取当前条目的用户头/向量、发送载荷和控制消息，尚需保留整个在途消息。
流式接收仍预验证数据缓冲区，辅助数据/地址输出也有提前验证，跨页部分复制副作用
未完全一致。信号与新数据同时到达时，完成部分 WAITALL 后是否继续消费后续条目
仍需改进；不因此宣称信号/重启完全一致。需验证完整 SMP、close/fork/exec 竞争及
目标任务缺页失败路径。这两项及所有上述配套缺口均未达到完整 Linux 6.12 兼容。

## 2026-09-10 siginfo 队列实现检查点

本批新增 `rt_sigqueueinfo(129)`、`rt_tgsigqueueinfo(297)`，状态为
`implemented_pending_runtime`。当前 375 个编号中：129 项 `missing_dispatch`、
140 项 `implemented_pending_runtime`、84 项 `routed_not_certified`、
5 项历史 `completed`、17 项 Linux 保留/ni。入口数量不表示完整兼容率。

对照本地 `build/linux-6.12/kernel/signal.c` 的 `__copy_siginfo_from_user`、
`known_siginfo_layout`、`do_rt_sigqueueinfo`、`do_rt_tgsigqueueinfo`、
`check_kill_permission`、`__send_signal_locked`、`collect_signal`、
`next_signal`、`do_sigtimedwait`，及 `kernel/fork.c` 的默认资源限制。

- 新增真实动态 siginfo 队列，区分线程私有与进程共享队列；普通信号保留第一份
  信息并合并重复发送，实时信号按同一信号的 FIFO 顺序保存多份载荷。
  出队遵守私有先于共享、同步信号优先、其余按编号选择的 Linux 规则。
- 原始用户 siginfo 先复制 48 字节 kernel_siginfo，覆盖 signo 为调用参数；
  未知 si_code 检查其余 80 字节，非零返回 E2BIG，不可读返回 EFAULT。
  已知布局不要求扩展区可读。保留 pid/uid/errno/64 位 sigval，输出扩展清零。
- 实现 SI_CODE 防伪造、TGID/TID 匹配、真实/有效/保存 UID、CAP_KILL、
  SIGCONT 的 Unix 会话权限，以及 sig=0 检查。源码和宿主测试确认：
  `rt_sigqueueinfo` 可以用有效 TID 找到其所属进程共享队列。
- `RLIMIT_SIGPENDING` 接入 getrlimit/setrlimit/prlimit64，按接收任务真实 UID
  对所有进程的排队记录计费，UID 改变后仍释放原始费用；实时队列耗尽返回 EAGAIN。
  默认限额取 Linux 非 KASAN x86-64 的 RAM/线程公式；提高 hard limit 使用
  CAP_SYS_RESOURCE。普通信号在无法分配 siginfo 时仍记录信号位，遵循 Linux
  明确允许的退化行为；没有用空成功替代实时信号排队。
- clone 的新线程私有队列为空，CLONE_THREAD 共享进程队列；组长退出保留
  共享记录，exec 唯一存活线程接管队列，最后线程退出释放全部记录。
  发送/出队/清理串行化，等待发布后再检查 pending，防止到达与睡眠之间丢失唤醒。
- `rt_sigtimedwait` 修正 sigset 长度错误码、timeout 校验顺序、KILL/STOP 屏蔽、
  私有/共享信号只消费一个、输出故障后的消费语义、超时饱和与 EINTR。
  48 字节输出成功后扩展区故障保留先前写入。此等待不因 SA_RESTART 自动重启。
  队列耗尽及超时的 EAGAIN 直接返回应用，避免分发层错误地当成内部 I/O 重试。
- SA_SIGINFO 信号帧携带真实载荷；信号栈写入失败按 force_sigsegv 路径处理，
  不再静默丢弃信号。tkill/tgkill 和正 PID kill 发送实际 SI_TKILL/SI_USER 信息。
  设置 SIG_IGN 或默认忽略动作清理队列；已阻塞的忽略信号允许进入 pending。
- 共享 UAPI 增加 sigval/timer 字段及常量，保持 siginfo 128 字节和信号帧布局；
  musl 与 SDK 继续使用原 Linux raw syscall，无应用移植或二进制编号变化。

实际验证：

- `python3 build.py run test-linux-threads` 通过。真实内核队列、等待和资源限制
  函数由 ASan/UBSan 驱动，覆盖 FIFO/合并、队列优先级、UID 费用、内存分配失败、
  权限、用户指针、忽略动作、exec 转移、退出清理和丢失唤醒检查；原 futex 与
  membarrier 用例仍通过。调度默认动作等部分服务由测试桩隔离，非完整内核运行。
- `tools/tests/signal_queue_abi_test.c` 使用宿主 Linux 头文件及 raw syscall，
  FIFO、线程/进程隔离、TID 定位、载荷、零限额、guard page、EINVAL/EPERM/
  EFAULT/E2BIG 等通过。宿主是 Linux `7.2.2-1-cachyos-bore-lto`，不是 Linux 6.12
  guest 或 LeonOS；6.12 依据是上述源码。
- `python3 -m unittest tools.test_runtime_responsiveness.RuntimeResponsivenessTests.test_signal_frame_uses_destination_address_space`
  通过，覆盖目标地址空间、red zone、实际 siginfo、sigtimedwait 中断和坏信号栈。
- `python3 build.py run kernel`、`run musl-probes`、`run test-musl-abi`、
  `test uapi`、`test linux-abi-contract` 均通过。新增 raw 用例已加入静态/动态
  musl 探针；本批只编译这些 guest 探针，未宣称它们已在 LeonOS 执行。
  musl 头文件检查核对 si_value/si_timerid/si_overrun 的真实偏移。
- 未构建 ISO，未启动 QEMU/Vim，未验证 VMware。`git diff --check` 通过。

剩余具体范围仍在任务内：POSIX timer 仍用旧 pending 位与 timer_pending 位，
尚缺独立预分配 siginfo、完整 sigval/overrun 及与用户实时信号混合排队语义；
SIGCHLD、异步 I/O 等其他信号源还需迁移。init 的特殊保护、ptrace、namespace、
LSM、core dump、完整 job-control/kill 进程组规则尚未齐全；高精度等待仍受 tick
调度限制，多核高压并发、完整 fork/exec/exit 实际执行及 guest 原始调用待验证。
这两项不能据本批通过的子集被标为完整 Linux 6.12 兼容。

## 2026-09-10 符号链接实现检查点

本批新增 `symlink(88)`、`symlinkat(266)` 实现，并补齐 ext2 的链接对象与
`readlink/readlinkat` 路径。两项状态为 `implemented_pending_runtime`，不是完整
Linux ABI 认证。CSV 当前共 375 项：131 项缺少分发、138 项新增实现待补齐或验证、
84 项原有入口未完整认证、5 项历史任务完成标记、17 项 Linux 保留/ni。

- 对照本地 Linux v6.12 的 `fs/namei.c:do_symlinkat/filename_create`、
  `fs/ext2/namei.c:ext2_symlink` 和 `fs/ext2/inode.c:ext2_inode_is_fast_symlink`。
  修正了 `symlinkat(target, newdirfd, linkpath)` 参数顺序、空目标 ENOENT、
  同名 EEXIST、尾斜杠及只复制链接文本的创建行为。
- ext2 实现小于 60 字节的内联链接及从 60 字节起的数据块链接，目标长度小于
  文件系统块大小；读取按容量截断且不补 NUL。短链接删除不再把文本解释为块号；
  rename、hard link 和目录类型保留 symlink 身份与目录引用计数。失败分配执行回收。
- 路径权限检查按组件展开相对和绝对链接，再处理 `..`，最多跟随 40 次；
  stat/open/exec/chdir 跟随目标，lstat/readlink/lchown 及相应 NOFOLLOW 标志
  保留最终链接，创建/删除/重命名解析父目录。Unix socket connect 跟随链接，
  bind 对最终名字执行创建检查。getdents 返回 `DT_LNK`，ext2 保持大小写敏感。
- symlink mode 固定为 0777，不应用 umask；继承 setgid 父目录的组，
  nofollow chmod 返回 EOPNOTSUPP，chown 可修改链接自身。
- `include/uapi/leonos/fs_abi.h` 增加类型值 5，结构尺寸及现有类型值不变；
  Linux 用户态仍使用 `S_IFLNK`/`DT_LNK`，未添加应用端 syscall 适配。
  `include/uapi/linux/limits.h` 记录 Linux 的 255/4096 限制，尚未替换全部旧路径缓冲区。

实际验证：`python3 build.py test storage-rename` 通过；使用真实 ext2 实现和
ASan/UBSan，覆盖 1 KiB/4 KiB 块、目录 filetype 开/关、59/60/61/255/1023/4095
字节目标、截断、大小写区分、硬链接和重命名生命周期；三个镜像均通过 `e2fsck -f -n`。
`python3 build.py test linux-permissions` 通过，包括链接后的 `..`、权限检查、
nofollow/parent 策略、40/41 次跟随及链接 mode/owner/group。
新增 `tools/tests/symlink_abi_test.c` 使用 Linux 头文件和 raw syscall，并纳入
musl 探针的 `--case symlink_syscalls`。该程序在宿主 Linux
`7.2.2-1-cachyos-bore-lto` 通过；这不是 LeonOS 或 Linux 6.12 运行结果。
`python3 build.py run kernel` 和 `python3 build.py run musl-probes` 均构建通过，
静态与动态 musl 探针都包含新用例。宿主运行静态探针的 `symlink_syscalls` 子项
通过，但完整程序退出 1：既有 startup 检查固定预期 SCHED_FIFO 设置失败并返回
EINVAL，宿主结果不符合该预期。未改动这一断言，也未将整套探针记作通过。
本批没有构建 ISO、启动 QEMU/Vim 或验证 VMware。

仍需实现或验证的具体范围：完整 4096 字节 pathname/255 字节组件及目录 fd 的
inode 路径解析、O_PATH|O_NOFOLLOW 和空路径 fd 的 rename/unlink 后生命周期、
protected_symlinks/capability/ACL 权限矩阵、xattr 块引用与磁盘错误回收、完整
atime/ctime 和父目录时间戳、跨目录 rename、mount 并发与 guest raw syscall 执行。
这些缺口及 CSV 的所有剩余调用继续属于目标范围，不能把本批局部通过计为整项完成。

订正旧 hard-link 检查点：`links_count` 只跟踪目录引用。
`storage_unlink -> ext2_unlink -> ext2_destroy_inode` 在最终目录项删除时仍直接
回收 inode，并没有检查打开 fd 的引用。旧文档和 CSV 的“保持 open-file lifetime”
表述没有源码依据，已更正；最终 unlink 后的 fd 生存期仍未实现完整 Linux 语义。

## 2026-09-10 current continuation checkpoint

- Added the shared Linux capability UAPI and native `capget(2)`/`capset(2)`
  paths. Tasks now carry permitted, effective and inheritable capability
  bitmaps through clone/exec identity inheritance; root sessions receive the
  supported capability set and non-root sessions cannot raise bits outside
  their permitted set. Linux capability header versions 1, 2 and 3 are
  validated with native v1/v2/v3 data widths. `python3 build.py run kernel`,
  `python3 build.py run test-musl-abi` and the static/dynamic probe builds
  pass. The first 2 GiB QEMU ABI run reached Terminal and launched
  `/programs/abittest/abittest.elf`, but the probe assumed the OOBE test
  account was root and exited 1; the probe now covers both root and non-root
  paths. A fresh guest run is required before these rows can move beyond
  `implemented_pending_runtime`.

- Rechecked the native dispatch table against the source after the interrupted
  build. `flock`, `utime`, `utimes`, `futimesat`, `utimensat`, `clock_getres`,
  `sched_setattr`, `sched_getattr`, `rseq`, and `futex_waitv` all have real
  kernel paths; their CSV rows now say `implemented_pending_runtime` rather
  than `missing_dispatch`.
- Added a raw musl regression for `utime(path, NULL)`. Linux treats the NULL
  form as `UTIME_NOW`; the previous common timestamp validator rejected that
  marker for the legacy `utime`/`utimes` family. The validator now accepts the
  marker while retaining range checks for ordinary timeval/timespec values.
- Fresh validation: `python3 build.py run kernel`, `python3 build.py run
  test-musl-abi`, `python3 build.py run test-terminal-packages`,
  `python3 build.py test installer-input`, and `python3 build.py run
  test-qmp-vim` all completed with zero build/test errors. The QMP run reached
  OOBE, desktop, Terminal/PTY, shell pipeline and Vim; it is QEMU evidence,
  not VMware evidence.

## 2026-09-10 sched_attr ABI correction

- Rechecked the native Linux signature against the v6.12 UAPI: `sched_setattr`
  receives `flags` as its third argument and reads the versioned buffer size
  from `struct sched_attr.size`; it does not receive a separate size argument.
- Corrected the kernel validation and raw musl probe accordingly. Existing
  `sched_getattr` size handling remains separate (`pid, attr, size, flags`).
- Kernel, musl probe compilation, Linux ABI contract and standalone UAPI tests
  pass after the correction. A fresh QEMU execution of the complete probe is
  still required before changing the CSV state from pending runtime.

## 2026-09-10 utime family audit correction

- Rechecked Linux native dispatch and found `utime(132)`, `utimes(235)` and
  `futimesat(261)` already routed through the real syscall boundary with native
  x86-64 argument layouts, user-pointer checks, timeval/utimbuf range validation,
  current-time behavior, permission checks and storage updates.
- Added raw `futimesat` coverage to `tools/tests/musl_guest_test.c`. These rows
  are now `implemented_pending_runtime` rather than `missing_dispatch`; full
  permission/error/precision matrices still require guest evidence.

## 2026-09-10 futex_waitv checkpoint

- Added native x86-64 `futex_waitv(2)` dispatch for Linux syscall 449. The
  kernel validates up to 128 native 24-byte descriptors, U32/private flags,
  aligned user addresses, absolute realtime/monotonic deadlines, and returns
  Linux EAGAIN/EINTR/ETIMEDOUT outcomes. Scheduler futex wait state now tracks
  multiple keys so a wake on any descriptor releases the task.
- Kernel and static/dynamic musl probe binaries build successfully. QEMU
  execution of the new waitv assertions, duplicate-key and requeue interaction,
  cancellation, and scheduler stress remain pending; CSV therefore records
  `implemented_pending_runtime`, not full compatibility.
- The 2026-09-10 2 GiB desktop QMP run reaches OOBE/login, Terminal/PTY and
  Vim, but it is the editor smoke path and does not run the complete futex
  waitv probe. The earlier standard desktop ABI run still failed before the
  guest probe after OOBE physical-memory exhaustion; this remains a separate
  QEMU validation blocker.

## 2026-09-10 formal Vim/ncurses and image checkpoint

- Unmodified upstream Vim and ncurses are enabled in `configs/default.conf`
  and are staged in the normal musl image. Host package validation passes
  terminfo, wide-character ncurses, Vim editing, runtime syntax and timer
  behavior.
- A fresh `python3 build.py run test-qmp-vim` run rebuilt the current VMDK and
  reached OOBE, desktop, Terminal/PTY, shell pipeline and `/programs/vim/vim.elf`;
  Vim exited with code 0 and the runner reported `0 errors`. This is QEMU
  evidence for the desktop/Vim path, not a claim that every Linux syscall is
  complete.
- Fresh deliverables are `build/images/leonos4-musl-installer.iso` and
  `build/images/leonos4-musl-desktop-vim-fixed.iso`. The installer contains
  GRUB EFI boot files, `kernel.sys`, `middlelayer.sys`, and the musl root
  payload; the payload contains musl, mimalloc, Vim and Vim runtime files.
  `build/images/root.ext2` is an ext2 filesystem and the generated VMDK uses
  it as the runtime root.

## 2026-09-10 flock checkpoint

- Added native `flock(2)` dispatch with Linux operation validation, shared and
  exclusive lock state keyed by the underlying storage node, `LOCK_NB` error
  reporting, wait-queue blocking, and shared open-file-description cleanup on
  `dup`, fork, explicit unlock, and final close. The kernel and static/dynamic
  musl probe binaries build successfully; QEMU contention, fork, and SCM_RIGHTS
  runtime evidence remain pending, so the CSV state is `implemented_pending_runtime`.

Scope remains all 375 native Linux v6.12 syscall rows and all B01-B46 groups.
The original 17 Linux reserved/ni numbers remain excluded from new functionality.
No syscall is certified from a dispatch entry, compilation, or a libc wrapper.

## 2026-09-10 utimensat checkpoint

- Added native `utimensat(2)` dispatch with Linux `AT_*` flag validation,
  dirfd and `AT_EMPTY_PATH` resolution, two native 64-bit timespec values,
  `UTIME_NOW`/`UTIME_OMIT`, owner/write permission checks, and ext2 inode
  persistence for atime/mtime seconds. Unsupported timestamp backends return
  `EOPNOTSUPP`; ext2's classic inode format cannot persist nanoseconds, which
  remains an explicit limitation. The syscall is built into the musl probe
  image; targeted runtime validation and the chmod01 LTP rerun remain next.

## 2026-09-10 rseq checkpoint

- Added the native x86-64 `rseq(2)` entry using Linux 6.12's 32-byte aligned
  `struct rseq` ABI. Registration writes the single CPU/node state, rejects
  unsupported flags and malformed size/alignment, and keeps the address,
  length and signature in per-thread task state. Re-registration and
  unregistration follow Linux's `EBUSY`, `EINVAL` and `EPERM` distinctions;
  clone and exec clear the child/old-image registration state.
- Initial ELF stacks now expose `AT_RSEQ_FEATURE_SIZE=32` and
  `AT_RSEQ_ALIGN=32`. The raw static/dynamic musl `rseq` case passes in
  `build/musl/rseq-probes-serial.log`. The complete probe's dynamic run still
  has unrelated `thread_alarm` and `poll_interrupts` timing failures; the
  static run is otherwise fully green. Restartable critical-section abort,
  CPU migration and full exit notification behavior remain pending.

Status vocabulary: `unprocessed` (outstanding implementation remains),
`implemented_pending_validation`, `verified`, `blocked` (a concrete external
blocker must be named). A group with partial fixes and known missing behavior
remains `unprocessed`; its completed subparts are recorded separately below.

## 2026-09-10 sched_attr checkpoint

- Added native x86-64 `sched_setattr(2)` and `sched_getattr(2)` dispatch with
  the Linux 6.12 `struct sched_attr` layout, size/extension checks, user-buffer
  validation, policy/flag validation, zero-filled forward-compatible output,
  and the scheduler's actual `SCHED_OTHER` nice state. Unsupported realtime and
  deadline fields return explicit `EINVAL` rather than being silently ignored.
- `tools/tests/musl_guest_test.c` now exercises both raw calls, 48-byte and
  full-size structures, nice update/query, invalid size, and unsupported policy.
  The static and dynamic probes ran in LeonOS QEMU and both reported
  `END failed=0` in `build/musl/sched-attr-probes-serial-2.log`.
- The calls remain `implemented_pending_validation` in the ledger because
  realtime/deadline scheduling, capability checks, and SMP policy behavior are
  not implemented or certified.

## 2026-09-09 futex2 and clone3 checkpoint

- The native futex2 wake/wait/requeue entry points use Linux 6.12's exact
  4/6/4 arguments, U32 size flags, 24-byte vector descriptors and independent
  private/shared keys. Zero-count wake, int argument truncation, user-copy errors,
  EAGAIN delivery and signal cancellation/restart were repaired.
- `tools/tests/futex2_abi_test.c` runs unchanged with upstream Linux headers on
  Linux 6.12 (`build/musl/futex2-linux-reference-serial.log`) and in static/dynamic
  LeonOS musl probes (`build/musl/futex2-probes-serial.log`). It covers real queued
  threads, masked wakes, private-to-shared requeue, absolute clock deadlines and
  SA_RESTART. The host queue regression first failed on strict zero wake, then
  passed under ASan/UBSan. Realtime clock changes, mapping lifetime and SMP remain.
- `clone3` accepts Linux's size-versioned argument block and validates extension
  bytes, flags, exit_signal and stack arithmetic before cloning the live trap
  frame. CLEAR_SIGHAND resets caught handlers while preserving SIG_IGN. Existing
  MM/files/fs/sighand/TLS/TID behavior is reused; unsupported requests are rejected.
- `tools/tests/clone3_abi_test.c` passes on Linux 6.12 in
  `build/musl/clone3-linux-reference-serial.log` and static/dynamic LeonOS in
  `build/musl/clone3-probes-serial.log`. It verifies real children, private stacks,
  TLS, TID writeback/clear, handler inheritance and wait/reap. pidfd, requested
  PIDs, cgroups, namespaces, vfork and inherited clone limits remain outstanding.
- All 375 rows remain tracked: 159 missing dispatches, 5 timer rows pending
  validation, 110 new implementations pending complete validation, 84 preexisting
  uncertified routes, 17 reserved/ni.
  These are implementation states, not a compatibility percentage.
- The post-fix QEMU run `build/musl/prctl-after-serial-2.log` reports static and
  dynamic `process_prctl PASS`, `thread_futex2 PASS`, `thread_clone3 PASS`, and
  `END failed=0`. The dumpable state now persists in the shared address-space
  object, while the remaining `prctl` command set is still partial.
- `sethostname(2)` and `setdomainname(2)` now enforce the Linux root, length,
  pointer, and exact-copy contract; `uname(2)` exposes the stored names. The
  Linux 6.12 reference probe and static/dynamic LeonOS probe both pass in
  `build/musl/utsname-linux-reference-serial.log` and
  `build/musl/utsname-after-serial.log`.
- `membarrier(2)` now implements the advertised Linux command subset with
  shared-MM registration state and real memory barriers. LeonOS static/dynamic
  probes pass in `build/musl/membarrier-serial-2.log`; the Linux 6.12 reference
  image reports `ENOSYS` because its `CONFIG_MEMBARRIER` is disabled.
- `openat2(2)` now validates the Linux `open_how` size/version, extension bytes,
  flags and mode before sharing the existing openat allocator. Linux reference
  and static/dynamic LeonOS probes pass in `build/musl/openat2-linux-reference-serial.log`
  and `build/musl/openat2-serial.log`; path-resolution flags remain explicit gaps.

## 2026-09-08 musl default / PTY and process-session checkpoint

- Picolibc source, port, old CRT/loader and static/dynamic libc wrappers have been
  removed from the active tree. Default build and SDK use musl+mimalloc. Native
  C/POSIX consumers use musl; old private ABI binaries must be rebuilt.
- `ctty-normal-terminal-test.log` completes fresh ordinary OOBE/login, dual PTY,
  background/pipeline and nano save/readback. Before-fix failure is retained in
  `default-musl-terminal-test.log`; missing TIOCSCTTY and 64-bit comparisons of
  Linux's 32-bit ioctl request prevented musl forkpty from starting Terminal.
- `session-notify-probes-serial.log`: static/dynamic each 49 groups pass, including
  public musl forkpty, sign-extended raw TIOCGPTN, getsid/getpgid pid_t width,
  cross-user queries, zombie identity until wait and ESRCH after reap. The new
  SIGCHLD failure is preserved in `session-probes-serial.log`. The last thread's
  retirement now queues the configured parent signal once, retaining process
  identity independently of resource release. `session-notify-threads-host.log`
  checks final-thread ordering and duplicate notification with ASan/UBSan.
- `session-linux-reference-after-serial.log`: the same 43 selected groups pass
  on Linux 6.12, using its installed UAPI headers. Reference init can inherit
  session 0; the test now correctly permits that upstream behavior.
- Installer update payload validation and refresh lists now include `/lib`
  (musl loader/libc/mimalloc) and `libleonos.so.2`; old mandatory loader paths
  would reject a correct musl image. This is implemented pending install/update
  execution; reaching Thanks does not validate disk installation.
- TCC first QEMU failure is preserved in `default-musl-tcc-failed.png`: a normal
  user cannot write `/programs/tcc/examples`. The regression now compiles into
  `/tmp` and requires the generated program's actual zero exit.
  `default-musl-tcc-after-test.log` passes; `default-musl-tcc-after-serial.log`
  records compilation and generated ELF exit 0, with `default-musl-tcc-pass.png`.
  Full TCC language/runtime coverage remains unverified.
- Remaining: complete siginfo, SA_NOCLDWAIT/SIG_IGN autoreap, clone wait options,
  reparenting, thread-group-wide setsid/setpgid, TTY lock/detach/hangup and all
  previously recorded Linux ABI gaps. AP user scheduling remains disabled;
  these QEMU checks do not certify SMP or VMware behavior.

## 2026-09-08 raw filesystem, memory and poll checkpoint

- PTY masters now start with Linux devpts' locked state. Slave opens fail until
  `TIOCSGPTLCK` clears it; `TIOCGPTLCK` reads the state. The master remains alive
  until its last duplicated/SCM reference closes. `tools/test_linux_pty.py` passes;
  full controlling-terminal detach, termios queues and hangup semantics remain.
- Raw `creat`, old `getdents`, `renameat` and zero-flag `renameat2` now have native
  x86-64 dispatch and descriptor-relative path handling. The musl guest's
  `proc_directories` test parses both Linux dirent layouts and completes the
  create/rename/old-getdents round trip. Link/symlink/readlink still return their
  real unsupported error because the storage layer has no link target API.
- `msync`, `mincore` and `madvise` validate page/range/user pointers and Linux
  flags; anonymous mappings pass the raw calls in both static and dynamic musl.
  File-backed writeback and unsupported advice are explicitly not claimed.
  `fadvise64` now distinguishes negative offset/length (`EINVAL`) from bad fd
  (`EBADF`), while its cache advice remains a no-op pending a storage cache.
- `brk` now records the writable end of the main ELF, grows and shrinks private
  anonymous pages, and preserves the current break when a request crosses the
  task limit or the initial heap boundary. Raw static and dynamic musl probes
  pass the query/grow/shrink/failure cases in
  `build/musl/abi-probes-qemu-20260908-11.log`; full heap/VMA/RLIMIT stress is
  still pending.
- `waitid` accepts P_ALL/P_PID/P_PGID and WEXITED/WSTOPPED/WCONTINUED subsets and
  writes native siginfo for reaped children. WNOWAIT, exact uid/code fields,
  stopped/continued queues and complete wait4 rusage are still incomplete.
- `ppoll` now validates the Linux timespec and supports the no-signal-mask path;
  a non-null mask returns `EOPNOTSUPP` until the scheduler can atomically install
  and restore it across a parked wait. The raw probe covers valid, invalid and
  masked calls. `build/musl/abi-probes-qemu-20260908-11.log` records static and
  dynamic `memory PASS`, `poll_files PASS`, and `END failed=0`.
- `select` and no-mask `pselect6` now translate the native x86-64 fd_set and
  timeval/timespec layouts through the same poll readiness engine. Static and
  dynamic raw musl probes pass regular-file read/write readiness in
  `build/musl/abi-probes-qemu-20260908-12.log`; pselect signal-mask swapping
  and exact timeout remainder updates remain incomplete.
- `getrusage`, `sysinfo` and `times` now expose Linux x86-64 structure layouts
  using scheduler CPU ticks, resident memory, physical memory totals and task
  counts. Static and dynamic raw musl probes pass these fields in
  `build/musl/abi-probes-qemu-20260908-13.log`; child accounting, swap/load
  counters and detailed I/O/page-fault fields remain incomplete.
- `fallocate` now validates native mode, signed offset/length and fd type; mode
  zero extends the file through the storage truncate contract and refreshes all
  open-fd metadata. Static and dynamic raw musl probes pass extension, stat,
  unsupported-mode and invalid-range cases in
  `build/musl/abi-probes-qemu-20260908-16.log`; true preallocation and punch
  hole/keep-size modes remain unsupported.
- `eventfd` and `eventfd2` now create shared Linux event-counter descriptions,
  validate `EFD_NONBLOCK`, `EFD_SEMAPHORE` and `EFD_CLOEXEC`, implement native
  eight-byte read/write semantics and report counter readiness through `poll`.
  Static and dynamic raw musl probes pass in
  `build/musl/abi-probes-qemu-20260909-11.log`; complete wait-queue wakeups,
  overflow blocking and all resource/lifetime edges remain pending.
- The raw identity family `setreuid`, `setregid`, `setresuid`, `getresuid`,
  `setresgid` and `getresgid` now has Linux-numbered dispatch, three-field
  real/effective/saved ID handling, `-1` preservation and non-root checks. Static
  and dynamic musl probes pass in
  `build/musl/abi-probes-qemu-20260909-14.log`; complete exec/thread credential
  sharing and saved-ID edge cases remain pending. The same run exercises
  non-root EPERM cases from the permissions children.
- `sched_get_priority_max` and `sched_get_priority_min` now validate Linux
  scheduling policies and return the native FIFO/RR and OTHER/BATCH/IDLE bounds;
  invalid policies return `EINVAL`. Static and dynamic probes pass in
  `build/musl/abi-probes-qemu-20260909-13.log`; policy setters, real-time
  scheduling and SMP behavior remain pending.
- `sched_getparam` and `sched_getscheduler` now have native x86-64 dispatch,
  target-task validation, writable `sched_param` priority output and the current
  `SCHED_OTHER` query. Static and dynamic probes pass in
  `build/musl/abi-probes-qemu-20260909-15.log`; policy state changes, permission
  checks and concurrent scheduler semantics remain pending.
- `setfsuid` and `setfsgid` now maintain separate filesystem credentials in each
  task, return the previous ID, enforce the Linux root/real/effective/saved-ID
  rule, and feed permission checks plus `/proc/PID/status`. Static and dynamic
  raw probes pass in `build/musl/abi-probes-qemu-20260909-17.log`; exec and
  thread credential transitions and the complete permission matrix remain pending.
- `sched_setparam` and `sched_setscheduler` now dispatch the native x86-64
  setters for the implemented `SCHED_OTHER`/priority-zero model, with explicit
  `EINVAL` for unsupported real-time policies or priorities. Static and dynamic
  probes pass in `build/musl/abi-probes-qemu-20260909-17.log`; real-time policy,
  priority inheritance and SMP scheduling remain pending.
- `mlock`, `munlock`, `mlockall`, `munlockall` and `mlock2` now dispatch native
  memory-lock operations. VMAs retain locked state, `MCL_CURRENT`/`MCL_FUTURE`/
  `MCL_ONFAULT` and `MLOCK_ONFAULT` are validated, and unmapped ranges fail.
  Static and dynamic musl probes pass in
  `build/musl/abi-probes-qemu-20260909-27.log`; RLIMIT_MEMLOCK, unaligned
  ranges, fault-time locking and fork/exec inheritance remain pending.
- `clock_settime` now dispatches native `CLOCK_REALTIME` updates through the
  existing wall-clock backend with root and timespec validation; invalid clocks
  and monotonic clocks return `EINVAL`. Static and dynamic musl probes pass in
  `build/musl/abi-probes-qemu-20260909-26.log`; RTC persistence, capabilities
  and full adjustment interactions remain pending.
- `prctl` now dispatches the native process-name subset: `PR_SET_NAME`/
  `PR_GET_NAME` use Linux's 16-byte contract, and dumpable query/set return
  explicit values. Static and dynamic musl probes pass in
  `build/musl/abi-probes-qemu-20260909-25.log`; other commands and inheritance
  semantics remain pending.
- `readahead` now validates native fd, signed offset and regular-file/block-device
  objects, returning `ESPIPE` for non-seekable objects. Static and dynamic musl
  probes pass in `build/musl/abi-probes-qemu-20260909-22.log`; backend cache
  population and the full error matrix remain pending.
- `mremap` now has native x86-64 dispatch for anonymous VMAs: shrink, adjacent
  in-place growth and `MREMAP_MAYMOVE` relocation preserve page contents.
  Static and dynamic musl probes pass in
  `build/musl/abi-probes-qemu-20260909-20.log`; file/device mappings, overlapping
  `MREMAP_FIXED` targets and full resource-limit interaction remain pending.
- `personality` now has native x86-64 dispatch. The all-ones query and
  `PER_LINUX` setting return the Linux values, while unsupported persona flags
  return `EINVAL`. Static and dynamic musl probes pass in
  `build/musl/abi-probes-qemu-20260909-19.log`; exec inheritance, ASLR
  interaction and other persona modes remain pending.
- `sched_rr_get_interval` now returns the actual `NTCLKS_TICK_HZ` scheduler tick
  for valid targets and validates the native timespec output and PID errors.
  Static and dynamic probes pass in
  `build/musl/abi-probes-qemu-20260909-18.log`; policy-specific time slices and
  SMP scheduler behavior remain pending.
- `time`, `getcpu`, `close_range`, `preadv`, `pwritev`, `preadv2` and
  `pwritev2` now have native x86-64 dispatch. Positional vector I/O combines
  Linux's split 32-bit offset arguments and restores the shared open-file
  description offset; non-seekable descriptors return `ESPIPE`, and
  unsupported `RWF_*` flags return `EOPNOTSUPP`. `close_range` implements close,
  `CLOSE_RANGE_CLOEXEC` and file-table `UNSHARE` paths with the Linux flag
  values. Static and dynamic raw musl probes pass all new paths, including
  pointer/range errors, in `build/musl/abi-probes-qemu-20260909-1.log` with
  `END failed=0`. Full vector short-I/O/device behavior, RWF flags, CPU
  topology, and close-on-exec inheritance across every descriptor kind remain
  pending.
- `sync_file_range` now validates the Linux signed offset/length and the
  `WAIT_BEFORE|WRITE|WAIT_AFTER` flag set; `sync` and `syncfs` are recorded as
  routed-but-uncertified rather than missing. Raw regular-file checks pass in
  `build/musl/abi-probes-qemu-20260909-2.log` for static and dynamic musl;
  mount-wide writeback and storage error ordering remain incomplete.
- `statx` now has a native x86-64 path for regular/proc paths and
  `AT_EMPTY_PATH`, with the Linux 256-byte result layout, requested basic
  fields, mask-zero `EINVAL`, and invalid-pointer checks. Static and dynamic
  raw musl probes pass in `build/musl/abi-probes-qemu-20260909-4.log` with
  `END failed=0`; timestamp, birth-time, mount-ID, symlink and extended
  attribute fields remain unsupported and are not reported in `stx_mask`.

### Final source/header cleanup checks

`musl-only-release-build.log` rebuilds the full normal/installer/SDK release
with `-nostdinc`, musl headers and Clang builtin headers. All 26 retired
standard-header overrides in `userland/libc/include` and the non-musl syscall
prototype branch are deleted. Canonical LeonOS extensions and curses remain.
`musl-only-release-closure.log` validates ordinary ESP/ISO (78 ELF each) and
installer staging (95 ELF). The extracted full SDK builds dynamic/static
examples in `sdk-musl-only-{dynamic,static}.log`; source SDK snapshots are
refreshed from that archive. `musl-only-input-after.log` runs all six input/IPC
fixtures successfully after giving host test doubles explicit declarations.

`musl-removal-glx-serial.log` and `glxgears-qmp-smoke.png` show software gear
rendering and Escape exit 0. The original QMP harness reported failure because
it searched only an obsolete task-creation log format; its PID extraction now
accepts the actual exec record and requires exit 0 for that PID. The corrected
assertion passes the captured log and rejects failed-exit/wrong-PID controls;
a fresh end-to-end run of that revised harness remains pending.
`musl-removal-stardust-test.log` cannot validate Stardust: the selected profile
has build/image/entry/sdk all false for stardusthello. No runtime pass is
claimed; its consumer was migrated to musl stat in source.

## Group Status

| ID | Status | Implemented or observed evidence | Remaining contract |
| --- | --- | --- | --- |
| B01 | implemented_pending_validation | Native syscall/LSTAR used by static/dynamic musl probes and GUI services | Exhaustive register, entry/return and signal interaction tests |
| B02 | implemented_pending_validation | Contiguous argc/argv/envp/auxv; independent non-null AT_EXECFN; empty argv normalization; guest startup passes | Argument/environment limits and complete exec error-path tests |
| B03 | unprocessed | Native clone shares MM/files/fs/sighand as requested; per-thread TLS with Linux FS/clone address validation, deferred clear_child_tid registration and shared-mm exit rules, futex queues, robust owner death, create/join/detach/cancel and mimalloc contention pass static/dynamic guest probes; 14 unmodified LTP/Open POSIX pthread and synchronization tests exit 0 | clone3, PI futexes, complete clone flags and errors; AP user scheduling and TLB shootdown; broader cancellation/restart coverage |
| B04 | unprocessed | Actual upstream musl interpreter loads five GUI services; shared ELF file-page zero-fill collision fixed | Full ET_EXEC+interpreter/static PIE coverage, malformed ELF/lifetime tests, all application ports |
| B05 | implemented_pending_validation | pause=34; nice moved to private extension; shared number table | Native signal-driven pause interruption test |
| B06 | unprocessed | Native 144-byte stat; real mode/UID/GID in guest; ext2 inode/nlink/blocks/timestamps read from disk and debugfs cross-checked | FAT/exFAT inode identity and timestamps; stable fd metadata after rename/unlink; complete device metadata |
| B07 | unprocessed | Native musl proc getdents64 passes: 19-byte header, aligned records, writable usercopy, short-buffer cursor retention and task traversal | Ordinary directory read semantics, offsets, seek, inode/type identity and lifecycle |
| B08 | verified | Actual Picolibc sysroots and musl use O_NONBLOCK=0x800; raw fcntl/accept EAGAIN pass in guest; installer Logo regression reproduced and repaired | Verification is limited to this constant-drift item |
| B09 | unprocessed | Shared fcntl commands; flag paths exercised by musl probe | Locks, ownership, every command/flag, shared status flags and fd table limits |
| B10 | unprocessed | Guest creation mode/umask/DAC and directory-relative openat pass; no temporary cwd mutation; component traversal checks precede dot-dot normalization | Full open flags, symlinks, O_PATH/O_TMPFILE, trailing slashes and descriptor-relative ancestor semantics |
| B11 | unprocessed | Reference-counted OFDs shared across fork/dup/SCM_RIGHTS; CLONE_FILES and exec detachment; blocked Unix read/recv/readv retain their original OFD across another thread closing and reusing the fd; common lowest-free allocation includes fd 0/3 | PTY/INET ownership and remaining blocking operations |
| B12 | unprocessed | Common open/pipe/socket/SCM/dup/PTMX allocator; fd 3 is available; dup2 grows the table after retaining its source, preserves the target on failure and honors the numeric fd limit; native and sanitizer tests cover fd 700, lowered limits, stdio reuse and allocation failure | Complete races and fd object ownership; remaining fcntl commands and flags |
| B13 | unprocessed | Repeated close returns EBADF; closing implicit PTY stdio now releases its fd; raw close truncates its unsigned-int argument; ordinary desktop background command regression repaired | Master/INET duplicate and inherited references, last-close semantics and cleanup |
| B14 | implemented_pending_validation | ftruncate no longer assigns file offset | Independent native syscall regression for offsets and failure paths |
| B15 | implemented_pending_validation | Raw getcwd byte count and libc pointer conversion; LTP getcwd01 all five assertions pass with exit 0 | Deleted/renamed cwd, path limits and full size/error coverage |
| B16 | unprocessed | Actual owner/group/other DAC; 32-bit UID/GID; chmod/chown/fchmod/fchown; sticky/setgid inheritance; native musl probes and LTP fchmod01/chown01 pass; desktop controls and commands exercised | FAT/exFAT ctime; fd identity after rename/unlink; setuid/setgid exec; fsuid/capabilities; atomic metadata operations and full special-bit semantics |
| B17 | unprocessed | Same-directory replacement on FAT32/exFAT/ext2; real Unix socket nodes with DAC/umask and unlink/rename namespace updates; ext2 socket create/replace/unlink independently checked by debugfs/e2fsck | Cross-directory rename; stable inode/dentry lifecycle across unlink/rename/open descriptors; error/crash transactions; append atomicity |
| B18 | implemented_pending_validation | pipe2 validates flags before allocation; writable usercopy; common fd allocation and rollback including stdio; native descriptor-exhaustion tests retain the user's output array and release the partial allocation | Broader aliases/fork, concurrent close and endpoint lifetime tests |
| B19 | unprocessed | Small nonblocking pipe writes checked for atomicity; EPIPE/SIGPIPE path; QEMU shell pipeline returns 5 | Blocking writer queues, partial writes, interruptions, endpoint references and concurrency |
| B20 | unprocessed | PROT_NONE and MAP_FIXED_NOREPLACE; guest MAP_SHARED anonymous memory retains identity at fork; anonymous fd ignored; devzero backed by real RAM | 256 MiB user VA layout, eager commit, full flags, shared file writeback, ranges/offsets and backing-object references |
| B21 | unprocessed | Guest NONE/RW data preservation, unmapped ENOMEM and shared-readonly EACCES; latest LTP mprotect01 has three TPASS and exit 0 after native signal repair | Cross-VMA transactions, Linux W+X policy, complete rollback and SMP TLB coherence |
| B22 | implemented_pending_validation | Unmap supports holes and owned PROT_NONE pages; host ownership tests pass | Complete range splitting, backing references and SMP tests |
| B23 | unprocessed | 64-bit pending/mask storage; native signal 33 cancellation and high-number signal tests; tkill/tgkill permission and TGID checks; fatal signals terminate the group | Realtime signals still coalesce in a bitset; siginfo source/queues and complete group stop/continue/default semantics |
| B24 | unprocessed | Canonical Linux sigaction flags and record; query no longer resets action; CLONE_SIGHAND shares actions | Premature restorer rejection; SA_NOCLDWAIT/SA_NOCLDSTOP and complete action/error semantics |
| B25 | unprocessed | Full 8-byte stored mask, NULL-mask query and writable usercopy repaired; shared process pending is separate from TID pending; eligible thread wakeup and blocked/pending/unblock tests pass | Realtime signal queues, complete siginfo and exhaustive mask/error tests |
| B26 | unprocessed | Native rt_sigframe/ucontext/siginfo, 128-byte red zone, altstack, FXSAVE/FXRSTOR and register/mask restoration; musl header offsets independently checked; guest altstack/SIMD/cancellation pass | Complete siginfo contents; bad-frame forced SIGSEGV; MXCSR CPU mask; XSAVE is disabled by current CPU configuration |
| B27 | unprocessed | Interruptible sigsuspend blocks with temporary mask, delivers handler and returns EINTR; saved pre-suspend mask restored from native frame | Nested/default-action and concurrent signal edge cases |
| B28 | unprocessed | Shared-resource exit, group wait, worker exec/CLOEXEC isolation; read/futex restart; nanosleep and timed futex/socket waits return EINTR despite SA_RESTART; partial WAITALL returns its bytes | True vfork; full wait4 rusage/options; complete stop/continue and restart_syscall semantics |
| B29 | implemented_pending_validation | Native timespec/pointer validation, zero sleep, retained deadline, saturation/rounding and relative remaining-time copy; actual implementation passes host sanitizer tests and native guest interruption tests | Exhaustive signal/stop/restart and extreme-duration scheduling tests |
| B30 | unprocessed | Realtime uses wall-clock subsecond state; monotonic uses ticks; reported resolution is actual 10 ms; clock_nanosleep interruption and native alarm/getitimer/setitimer ITIMER_REAL with periodic rearm and exec preservation tested | CPU/dynamic clocks, ITIMER_VIRTUAL/ITIMER_PROF, other timers, clock adjustment wakeups, settime privilege/precision and complete restart behavior |
| B31 | unprocessed | Actual NOFILE/AS soft/hard limits, prlimit64, shared pthread and fork/exec inheritance pass native tests; existing higher fds survive lowering NOFILE | Resources other than NOFILE/AS, full enforcement, capabilities, reaped PID and exec AS limits |
| B32 | implemented_pending_validation | Raw getpriority encoding adjusted | Native process/group/user selection and permission/range tests |
| B33 | unprocessed | Raw getaffinity returns 8 copied bytes; 32-bit PID/length, oversized and short masks, writable output, ESRCH/EFAULT order and cross-user EPERM pass native tests; musl pthread_get/setaffinity_np pass in the native musl SDK | Capability/cpuset/hotplug rules, exited-task lifetime, AP scheduling and full scheduler policy/flag semantics |
| B34 | unprocessed | Audit distinguishes libc reboot API from raw syscall | Linux magic values, command, privilege and argument contracts |
| B35 | unprocessed | Refcounted supplementary groups inherited at fork; getgroups/setgroups widths/order/privilege and group DAC checked by native musl probes | Complete UID/GID/fsuid/capability rules; saved IDs; setuid side effects; signal target permissions and lifetime |
| B36 | unprocessed | Unix STREAM/DGRAM/SEQPACKET, pathname/abstract/autobind, packet boundaries, MSG_TRUNC/PEEK/WAITALL, credentials, blocked fd pinning, readv/writev, shutdown/readiness, FIONREAD and peer-reset behavior pass native guest probes; accepted address metadata no longer shadows a listener binding | Complete flags, options, low-water marks and resource/concurrency cases; UDP/IPv6/INET server behavior |
| B37 | unprocessed | Unix SO_TYPE/ERROR/PEERCRED/PASSCRED/ACCEPTCONN/DOMAIN/PROTOCOL/RCVLOWAT, short buffers, raw write SIGPIPE; OLD/NEW timeouts, WAITALL/PEEK/lowwater and EINTR; SO_ERROR consumes peer reset once; FIONBIO updates the shared OFD | Socket buffer limits and remaining options; INET unsupported options still need repair |
| B38 | unprocessed | Length-delimited abstract addresses and 108-byte paths; native output truncation and cached peer name; accept allocates before dequeue with rollback | All address errors, concurrent namespace changes, INET address contracts |
| B39 | unprocessed | SCM_RIGHTS OFD references/CLOEXEC/CTRUNC/PEEK/discard, Linux stream ancillary barriers and 80 cycles; SCM_CREDENTIALS/SO_PASSCRED, sender validation, stream credential boundaries and zero-byte datagrams; IPC framing/partial writes and explicit service socket modes | All resource exhaustion/concurrent close cases; partial IPC state with direct close(fd); concurrent same-fd consumer sends |
| B40 | unprocessed | Shared native termios 36/44-byte records and flags/cc indexes; raw PTY, TIOCSCTTY and forkpty pass; ioctl request width and TCSETSF input flush repaired | Separate master/slave termios, locking, controlling tty ioctls, VMIN/VTIME, canonical EOF, echo/output processing, queues and complete hangup semantics |
| B41 | unprocessed | QEMU installer framebuffer renders and keyboard events reach GUI | fbdev exact layouts, pan/variable mode errors and reported capabilities |
| B42 | unprocessed | Historical BLKROGET encoding audit retained | Correct request encoding, native block ioctls, usercopy and error tests |
| B43 | unprocessed | Historical EVIOCGRAB argument audit retained | Value-vs-pointer correction and native evdev behavior tests |
| B44 | unprocessed | Real /proc/PID and /proc/self parent directories fix task traversal; getdents64 enumerates live tasks; MemAvailable enables LTP startup; /proc/PID/status provides real IDs and resident RAM, and task snapshots resolve usernames from passwd | Linux stat/cmdline format, remaining status fields, thread/self links, access control, offsets and lifecycle |
| B45 | unprocessed | Native poll validates writable revents, 32-bit nfds, regular-file EOF readiness, POLLNVAL, signal interruption and poll(NULL,0,-1); no-mask ppoll timespec path, basic epoll create/ctl/wait/oneshot packed-event behavior, and timerfd periodic read/poll behavior pass static/dynamic musl QEMU probes (`/tmp/leonos-epoll-guest-20260910c.log`, `/tmp/leonos-timerfd-guest-20260910b.log`) | ppoll atomic signal masks, pselect, epoll temporary signal masks, timerfd clock-adjustment/cancel-on-set, and complete wait queue/event/device/resource semantics |
| B46 | unprocessed | Existing mount subset retained | Filesystem-specific flags/errors, privilege, mount lifetime, busy checks, umount2 semantics |

## Verification Evidence

- Latest normal/UI validation: `proc-status-normal-build-test.log` completes
  the fresh ordinary image, OOBE/login, two Terminal PTYs, pipeline result 5,
  background sleep and nano save/readback. `proc-status-normal-nano.png` retains
  the real unsupported `jobs` diagnostic. `proc-status-ui-serial.log` and
  `proc-status-taskmgr.png` confirm nonzero resident memory and passwd-resolved
  names for live tasks; exited/released tasks retain 0 B. The pre-fix screenshot
  is `tls-scm-taskmgr.png`. `proc-status-glx-1.png` and `proc-status-glx-2.png`
  differ at 13963 pixels within the gear region, and glxgears exits with code 0.
  Rendering is PortableGL software; SVGA3D is unavailable in this QEMU setup.
  `proc-status-default-installer-build.log`, its serial log and screenshot
  confirm the default installer builds and reaches Thanks through keyboard
  navigation. `proc-status-musl-installer.png` covers the musl live services.
  Neither run performs a disk installation or VMware verification.
- Proc status checkpoint: `proc-status-before-serial.log` reproduces the
  missing status file in both musl programs. `proc-status-after-serial.log`
  passes 45 groups per program, including native status PID/TGID, four 32-bit
  UID/GID fields and VmRSS in kB. Residency counts actual user RAM page
  mappings, includes owned PROT_NONE pages and excludes device PFN mappings.
  The paging fixture verifies this distinction. `proc-status-after-host.log`
  passes seven tests, including actual procfs output and the task-snapshot
  consumer's passwd lookup, seven-byte reads and descriptor cleanup.
  `proc-status-musl-installer.png` shows the rebuilt live GUI responding to
  keyboard input. This implements a subset of status; private stat/cmdline,
  fsuid/fsgid setters, complete status fields, thread links and permissions
  remain. Legacy and musl task-snapshot consumers were rebuilt together.
- Additional Linux-only investigation: `tid-wake-linux-reference-serial.log`
  rejects the exploratory assumption that a shared FUTEX_WAIT on a read-only
  anonymous mapping can queue: Linux returns EFAULT. The corresponding kernel
  source is kernel/futex/core.c get_futex_key's read-only anonymous-page check.
  That invalid exploratory expectation was removed before LeonOS testing;
  the established Linux-validated exit tests and their expectations are unchanged.
  Shared futex backing-type validation and file-backed exit-write-fault wakeups
  still need implementation and independent regression tests.
- Affinity checkpoint: `affinity-before-serial.log` reproduces the raw return
  value/length failure; `affinity-after-serial.log` passes static and dynamic
  probes. `affinity-linux-reference-serial.log` validates 33 selected groups
  against Linux v6.12, including another pthread and a different-UID child.
  `tools/test_picolibc_abi.py` passes five adapter tests, including affinity and fcntl in both actual sysroots;
  raw affinity returns a byte count while the libc API returns 0 and clears
  the remaining output buffer. CPU-mask storage is eight bytes under the
  tested kernel configurations; LeonOS still schedules users only on the BSP.
- TLS/TID checkpoint: `tls-before-serial.log` fails the unchanged TLS and TID
  registration tests; `tid-before-serial.log` passes those fixes but reproduces
  unaligned exit-clear failure. `tls-after-serial.log` passes static/dynamic
  probes, including native arch_prctl command width, read-only/cross-page
  output, invalid FS bases, legal unmapped bases and CLONE_SETTLS. FS validation
  uses Linux's four-level TASK_SIZE_MAX (2^47 - 4096), independently of LeonOS's
  256 MiB mapping region. ARCH_SET_GS/GET_GS and other arch_prctl commands remain
  outstanding. set_tid_address registers without touching its pointer; only a
  shared mm with another user clears it at exit, including unaligned words
  crossing physical pages. Invalid/read-only pointers do not fault the kernel.
  `tls-linux-reference-serial.log` validates the same 36 selected groups on
  Linux v6.12. The futex host fixture now models actual shared-mm references
  and verifies that the last mm user leaves the word intact; this corrects
  its former incomplete ownership setup rather than changing a Linux result.
- SCM stream checkpoint: `scm-boundaries-before-serial.log` reproduces the
  wrong receive count on static/dynamic musl; `scm-boundaries-after-serial.log`
  passes all 44 groups per program (including startup). The native test sends
  ordinary `ab`, SCM_RIGHTS plus `cde`, then ordinary `fg`: recvmsg returns
  `abcde` and the fd, retaining `fg`. MSG_WAITALL, MSG_PEEK, partial consumption,
  read and readv obey the same ancillary boundary. Linux v6.12's
  net/unix/af_unix.c unix_stream_read_generic is the source reference; the
  matching independent reference test passes. Large fragmented SCM sends and
  remaining resource/concurrency semantics are not certified by this case.
  `scm-musl-installer.png` shows the final musl live installer at Thanks after
  keyboard input. Its installation payload still uses Picolibc.
- Descriptor checkpoint: `build/musl/fd-boundaries-after-serial.log` records static and
  dynamic `END failed=0`. `stdio-before-serial.log` reproduces closing an
  implicit PTY fd failing; `stdio-after-serial.log` passes the unchanged test.
  `fd-before-serial.log` reproduces the fd-3 allocation failures; the corrected
  implementation passes allocation, dup2 expansion, SCM_RIGHTS lowest-fd
  reception, PTMX at fd 0, pipe failure rollback and lowered-limit tests.
  `tools/test_linux_descriptors.py` compiles the actual syscall helpers and
  scheduler descriptor storage under ASan/UBSan, including injected ENOMEM
  during table growth and OFD reference-count checks.
  `fd-boundaries-before-serial.log` reproduces an unsigned-int fcntl command
  incorrectly retaining register high bits; the final run also covers native
  widths for dup3/pipe2/socket/socketpair, read-only output pointers and a zero
  descriptor limit. `fd-boundaries-linux-reference-serial.log` validates the
  same 32 selected cases on Linux 6.12.0, all exit 0.
- `fd-normal-build-test.log`, `fd-normal-serial.log`, and `fd-normal-nano.png`
  record a fresh ordinary image completing OOBE, two Terminal PTYs, the
  pipeline result 5, a background command without the old /dev/null error,
  and nano save/readback. `jobs` remains unavailable; the screenshot retains
  its real diagnostic. `fd-default-installer-build.log` and
  `fd-default-installer-serial.log` record a rebuilt default installer;
  `fd-default-installer.png` shows keyboard navigation to Thanks. These are
  QEMU results; VMware and actual disk installation remain unverified here.
- The legacy ipctest consumer previously assumed every fd was at least 4.
  It now checks descriptor validity with F_GETFD, distinct socketpair ends,
  and actual I/O/SCM behavior. The independent native allocation tests require
  exact Linux fd values (including 0 and 3); their expectations were unchanged.
- Current checkpoint: `build/musl/proc-status-after-serial.log` records static
  and dynamic probe `END failed=0`. This supersedes the socket unlink and
  pthread EAGAIN failures retained in the historical checkpoints below.
  Coverage includes thread contention, robust owner death, cancellation,
  signal altstack/SIMD/restart, worker exec, descriptor transfer and cycles,
  Unix packet types and 40000-byte multi-iovec MSG_WAITALL. A partial receive
  interrupted by a handler returns its byte count even with SA_RESTART.
  Added coverage includes SCM_CREDENTIALS, short socket options, raw SIGPIPE,
  nanosleep/timed-futex interruption, blocked fd close/reuse, vector packet
  boundaries, timeout expiry and the accepted-socket namespace regression.
  The same checkpoint additionally covers Unix shutdown/poll masks, queued
  byte counts (`FIONREAD`), peer reset after unread data, poll signal
  interruption, 32-bit `nfds` and timeout, read-only regular-file readiness,
  writable `revents` usercopy and FIONBIO across dup aliases.
  Process-directed pending signals are shared independently of TID signals;
  ITIMER_REAL, raw alarm and worker exec timer preservation pass. Futex tests
  include WAKE_OP signed operands, shift encoding, writable second word,
  realtime deadlines and saturation.
  Earlier timeout runs retained genuine failures: an expired EAGAIN was
  mistakenly retried by generic dispatch, then slot reuse exposed address
  metadata shadowing the listener. Both were repaired without relaxing tests.
- `build/musl/proc-status-linux-reference-serial.log`: the identical static musl
  program runs 38 selected heap/thread/signal/Unix/poll/fd/proc groups on an unmodified
  Linux 6.12.0 reference kernel; all exit 0, DONE failures=0. The reference
  kernel has two active vCPUs. This validates the selected expectations,
  not LeonOS multi-core support. Host Linux 7.2 instead fails the new
  SO_PASSCRED-at-accept assertion: current upstream copies flags at connect,
  while v6.12 `unix_sock_inherit_flags` runs at accept. The v6.12 test is kept.
- `build/musl/namespace-installer/thanks.png`: the same musl live image starts
  the desktop/installer and advances from Language to Thanks by keyboard.
- After host power loss interrupted ISO output, `resume-itimer-probe-build.log`
  completes packaging and `resume-itimer-installer.png` again shows Thanks.
  `socket-state-before-serial.log` reproduces three Unix failures; they pass
  after repair. `poll-final-serial.log` reproduces a kernel write fault on
  read-only pollfd memory; `poll-safe-serial.log` verifies EFAULT and no fault.
  `ioctl-before-serial.log` reproduces missing FIONBIO and 64-bit timeout
  interpretation; the current checkpoint passes both without test changes.
- `tools/test_linux_threads.py` exercises the actual futex queue implementation
  under ASan/UBSan. `tools/test_unix_ipc.py` runs the actual IPC consumer on
  host Linux, including partial framing, SCM association and stale bind paths.
  `tools/test_storage_rename.py` checks real ext2 socket nodes on volumes with
  and without dirent filetype, using debugfs and e2fsck as independent readers.
- Native syscall stacks are allocated per enabled CPU (128 KiB), replacing
  the overflowing 16 KiB budget. A static 64-CPU allocation initially collided
  with the fixed middlelayer image; the reproduced boot failure was fixed by
  allocating pages and adding a linker overlap assertion.
- QEMU has four virtual CPUs, but `SMP_USER_SCHEDULER_ENABLED` remains 0.
  These are BSP preemptive thread tests, not simultaneous multi-core user
  execution. AP scheduling and TLB shootdown remain unresolved implementation.
- `tools/test_uapi.py`: shared syscall ownership and 30 standalone C/C++ UAPI
  headers; `tools/test_musl_abi.py`: installed upstream musl constants/layouts
  including native signal context offsets, and static/dynamic allocation,
  TLS and pthread behavior on the host.
- `tools/test_linux_time.py` and `test_runtime_responsiveness.py`: actual
  timespec/deadline code and signal copy to the destination address space,
  including interrupted nanosleep remaining time, pass ASan/UBSan checks.
- `build/musl/service-mode-normal-serial.log` and `service-mode-normal/*.png`:
  fresh normal exFAT image completes OOBE, opens a user Terminal, returns 5
  from `printf hello | wc -c`, opens another PTY, and saves/reads a nano file.
  Task Manager lists 12 tasks. glxgears renders and two frames differ in
  8608 content pixels. RSS=0 and missing usernames remain known limitations.
  This exposed real Unix socket nodes created 0755 denying user connections.
  Public services now explicitly chmod their sockets 0666 and retain peer
  credential authorization; the generic helper defaults to 0600.
- Empty AHCI CD-ROM boot stalled with TFES set and PxCI uncleared. The
  completion loop now reports the hardware error promptly; the real driver
  fixture passes both synchronous/asynchronous cases, and the same normal
  image boots with an empty drive. This is separate from the original
  O_NONBLOCK installer Logo failure. No VMware execution has been performed.
- `build/musl/namespace-installer-fixed-build.log`: ordinary installer build
  succeeds after ordering TCC runtime replacement before manifest/icon
  staging, fixing a reproduced Directory-not-empty race.
- `build/musl/tls-scm-ltp-serial.log`: rerun after time/OFD/socket changes
  has 19 exit-0 tests and two real failures (link and timestamp setup);
  runner exits 1. Source tests and expected outcomes remain unchanged.
- `tools/test_picolibc_abi.py`: both installed fcntl sysroots, raw stat canary
  and termios conversion against actual host Linux. This is not guest stat
  or pthread certification.
- `tools/test_linux_memory.py`: real paging and ELF file-fault/page-cache
  implementation under ASan/UBSan. `tools/test_linux_pty.py`: real PTY raw
  mode and controlling-session isolation. Host Terminal/OOBE/input tests pass.
- `build/musl/probes-pty-serial.log`: both static and dynamic probes report
  startup, heap, memory and PTY PASS. Both report nonblocking-group FAIL only
  at final socket-path unlink (ENOENT), and threads FAIL (pthread_create EAGAIN).
  Their nonblocking fcntl/accept checks preceding unlink passed. Overall exit
  remains failure, not suppressed or reclassified.
- `build/musl/probes-pty.png`: musl live installer remains at its language
  screen after both failing probes exit. `build/musl/installer-musl-enter.png`
  records the earlier keyboard transition to the Thanks screen.
- `build/musl/legacy-installer-uapi-build.log`: full installer build and SDK
  archive succeed. ZIP membership and canonical header content were checked.
- `build/musl/normal-terminal-uapi.log`, `build/qmp-nano-serial.log` and
  `build/images/nano-qmp-smoke.png`: normal SMP QEMU desktop, PTY tabs,
  `printf hello | wc -c` output 5, nano save/readback/exit. The old harness
  reported success despite `jobs: not found`; job control is disabled in the
  BusyBox configuration, and `&` injection was missing. The key mapping is
  corrected; background job control is NOT certified by that run.

## Permission Integration Checkpoint

- `build/musl/permissions-final-probe-build.log`: 247 files built, 202 generated,
  zero errors. Normal userland, installer and musl diagnostic images included.
- `build/musl/permissions-final-probe-serial.log`: both static and dynamic
  upstream-musl probes pass startup, mimalloc heap, memory, PTY and permissions.
  The permission group covers creation/umask, independent UID/GID, supplementary
  groups, owner/group/other selection, EPERM/EACCES, sticky deletion, relative
  and empty-path metadata calls, mkdirat/unlinkat, device DAC, and rejecting
  nonexistent/non-directory/inaccessible components before dot-dot folding.
  Both overall probes still fail: AF_UNIX final unlink returns ENOENT and
  pthread_create returns EAGAIN. Exit status remains 1, with two failed groups.
- `tools/test_linux_permissions.py`: ASan/UBSan real permission implementation
  and persistent OSM metadata tests pass; maximum nested ACL stack use 32080
  bytes, below the 32768-byte budget. Old ACL records and explicit modes survive.
- `tools/test_storage_metadata.py`: actual ext2 image generated by mke2fs;
  mode 6750, UID 70001, GID 90002 and ctime written via the inode adapter and
  independently read by debugfs. Real ext2 allocation counts and FAT/exFAT
  bitmap/ISO read-only accounting are checked. This is a host disk fixture,
  not an ext2 guest or VMware write certification.
- `tools/test_picolibc_abi.py`: four tests pass, including an open() error test
  that failed before converting raw negative errors to -1/errno. Its environment
  configuration consumer was migrated with it.
- `build/musl/permissions-normal-fixed.log`: normal QEMU OOBE/login, desktop,
  Terminal PTY tabs, pipeline result 5 and nano save/readback. This exposed and
  fixed the zero-byte users.db factory seed incompatibility; corrupt nonempty
  databases still fail without being overwritten.
- `build/musl/permissions-fileman/saved.png` and `chown-denied.png`: actual
  property dialog saves mode 640; unauthorized UID:GID change is rejected and
  original identity is reloaded. `permissions-terminal-results.png` records
  BusyBox chmod 000 denying read, chmod 600 restoring it, and chown EPERM.
  BusyBox chmod/umask/chown/geteuid/getgroups shims now make real syscalls.
- Existing application create calls with a legacy mode of zero were migrated
  to explicit file/directory modes; authd homes and account database use 0700
  and 0600. Installer copies file ownership/mode; fileman/tar preserve file
  permission bits. Fresh ordinary images now include /tmp; init creates it
  with 1777 on older installations only when it is absent.

## Account and Rename Checkpoint

- `build/musl/permissions-rename-probe-serial.log`: static and dynamic musl
  startup/heap/memory/PTY/permissions/proc_directories/rename_replacement PASS.
  Both overall probes still exit 1 for the same socket unlink and pthread
  failures. The directory test checks real proc traversal, EINVAL/EFAULT and
  cursor preservation. The rename test checks replacement data, mode, UID/GID,
  file/directory mismatch and nonempty-directory rejection on FAT32.
- `tools/test_storage_rename.py`: real ext2 backend on two mke2fs images, with
  and without filetype; both replacement data checks and e2fsck pass. This
  exposed and fixed writing filetype into a volume without that feature.
- `tools/test_osmlayer_acl.py`: replacement drops the destination's stale
  permission record; source mode and 32-bit UID/GID survive. Stack budget
  remains 32080 bytes. `tools/test_oobe.py` now passes seven groups, including
  passwd/group formatting, bounded credential fields and actual admin-password
  verification before clearing the local password buffer.
- `build/musl/permissions-name-success.png`: normal exFAT image completes OOBE
  and login; /etc/passwd maps root to existing UID:GID 1:1; group lookup and
  BusyBox `chown root:root` succeed. `permissions-commands-final.png` confirms
  chmod 000 denies reads, restoration to 640, unauthorized chown denied, and
  the pipe result 5. This does not make the named account UID 0.
- `build/musl/permissions-accounts-reboot.png` and its serial log: cold boot
  of a standalone persisted disk keeps mode 640, account UID:GID 1:1 and
  file contents `hello`; regenerated passwd/group still support named chown.
- `build/musl/permissions-taskmgr-fixed.png`: process listing recovers from the
  traversal regression and displays 13 tasks. This historical screenshot retains RSS=0 and missing usernames; the later
  proc-status checkpoint repairs the data path, with UI verification recorded above.
- `build/musl/permissions-rename-installer/thanks.png`: latest musl installer
  renders and advances by keyboard beyond its language page. Build logs:
  `permissions-rename-probe-build.log` (145 built, 23 generated, zero errors),
  `permissions-rename-normal-build.log` and `permissions-final-sdk.log`.
- `build/musl/permissions-final-installer-build.log`: default installer and
  SDK build succeed, five files built and 22 generated with zero errors.
  `permissions-final-installer/thanks.png` confirms the default installer
  also passes Logo and accepts keyboard navigation in QEMU. Installation onto
  a target disk and the complete installer wizard were not exercised here.
- Scope and operational limitations are also documented in
  `docs/POSIX_PERMISSIONS_2026-09-08.md`. In particular, FAT/exFAT metadata has
  64 records per directory; fd identity, cross-directory rename and crash
  transactions remain unfinished. No VMware run has been performed.

## LTP Guest Results

Unmodified LTP source revision: `3a64d78f58bdceba93ed321e91215fb969a047ed`.
Latest evidence: `build/musl/tls-scm-ltp-serial.log`. Sources and expected
results are unchanged. The 14 pthread/synchronization cases are upstream Open
POSIX 1-1.

| Test | Result | Remaining failure |
| --- | --- | --- |
| getcwd01 | PASS, exit 0, five assertions | Broader syscall coverage remains |
| fchmod01 | PASS, exit 0, eight mode cases | Not full descriptor lifecycle certification |
| chown01 | PASS, exit 0 | Only this LTP case plus separate musl matrix |
| mprotect01 | Three TPASS; exit 0 | Earlier signal 48 warning resolved |
| fcntl01 | Exit 0 | Earlier signal 48 warning resolved |
| fstat02 | TBROK, exit 2 | link ENOSYS |
| chmod01 | TBROK, exit 2 | Timestamp setup via utimes/utimensat ENOSYS |
| pthread_create_1-1 | PASS, exit 0 | Broader clone/attribute coverage remains |
| pthread_join_1-1 | PASS, exit 0 | Not all join/detach races |
| pthread_mutex_lock_1-1 | PASS, exit 0 | PI futex operations remain unsupported |
| pthread_cond_wait_1-1 | PASS, exit 0 | This does not certify alarm or timed waits |
| pthread_cancel_1-1 | PASS, exit 0 | Asynchronous cancellation case |
| pthread_key_create_1-1 | PASS, exit 0 | Key creation case, not exhaustive TLS lifecycle |
| pthread_barrier_wait_1-1 | PASS, exit 0 | Barrier lifecycle only |
| pthread_rwlock_rdlock_1-1 | PASS, exit 0 | Read/write lock edge cases remain |
| pthread_once_1-1 | PASS, exit 0 | Once recursion/error paths remain |
| pthread_mutex_timedlock_1-1 | PASS, exit 0 | Clock and cancellation edge cases remain |
| pthread_cond_timedwait_1-1 | PASS, exit 0 | Clock and cancellation edge cases remain |
| pthread_mutex_trylock_1-1 | PASS, exit 0 | Contention/error paths remain |
| sem_timedwait_1-1 | PASS, exit 0 | Semaphore signal/clock edge cases remain |
| pthread_spin_lock_1-1 | PASS, exit 0 | SMP execution remains disabled |

The latest LTP runner reports two failures and exits 1. Assertions and expected
results were not weakened. MemAvailable and unlinkat cleanup fixes removed
earlier harness startup failures, exposing these specific ABI gaps.

QEMU validation does not establish VMware behavior. OOBE guest account setup
and keyboard input were exercised by the normal desktop test. VMware remains
untested. Remaining ports, all B01-B46 contracts, 159 missing dispatches and the
five timer rows pending validation remain in scope. The CSV contains 84 originally
routed uncertified calls, 110 newly implemented but not fully certified calls and
17 excluded Linux reserved/ni entries.

## Resource checkpoint and default libc migration (2026-09-08)

- NOFILE/AS now retain actual soft/hard limits. prlimit64 validates native argument
  widths, pointer aliasing, permissions and old-limit write faults; pthreads share
  the limits, fork copies them and exec preserves them. AS charges initial/growing
  stacks and MAP_FIXED net growth, preserving old mappings when the new limit fails.
- `rlimit-as-after-serial.log`: static/dynamic 47 groups each, END failed=0.
  `rlimit-linux-reference-serial.log`: 40 selected groups, failures=0 on Linux 6.12.
  Host resource and memory helper sanitizers passed. These remain partial results:
  all other resources return ENOSYS; capabilities, dead/reaped target lookup and
  full exec/brk/mremap enforcement are unfinished.
- The user's final policy is complete removal of Picolibc. Default application
  links now use musl CRT, mimalloc and libleonos.so.2; installer policy libraries
  use that same ABI. The old libc source dependency, build script, POSIX wrappers
  and custom loader have been deleted. BusyBox and nano standard functions now
  resolve through musl instead of local libc stubs.
- `python3 build.py run release`: final ordinary/installer/SDK build completed
  with 557 compiled files, 207 generated files and 0 errors. The fresh raw musl
  QEMU checkpoints through `abi-probes-qemu-20260908-16.log` report static and
  dynamic `END failed=0`; `tools/test_uapi.py`, `tools/test_musl_abi.py`,
  `tools/test_linux_pty.py` and `tools/test_linux_memory.py` also pass.
- Picolibc resource-header tests discovered RLIMIT_NOFILE=5, RLIMIT_AS=6 and
  signed-width infinity in the retired headers. The old wrapper tests are removed
  with that runtime; native musl and Linux-reference resource cases remain.

## 2026-09-10 hard-link ABI checkpoint

- Added native x86-64 `link(2)` and `linkat(2)` dispatch with Linux argument
  ordering, dirfd-relative resolution, `AT_EMPTY_PATH` flag validation, parent
  directory authorization, `EEXIST`/`EXDEV`/`EPERM` handling, and no fake
  symlink fallback. The subsequent symlink checkpoint above supersedes the
  former ENOSYS status for `symlink(2)` and `symlinkat(2)`.
- Added ext2 hard-link creation using a second directory entry and an inode
  `i_links_count` increment. Unlink now decrements the count and only releases
  blocks/inode on the final directory reference. Open-fd retention on final
  unlink is still missing; directory link counts alone do not provide it.
- The raw musl regression now checks inode identity, link count, data retention
  after unlinking the original name, and final cleanup. `python3 build.py run
  musl-probes` and `python3 build.py run kernel` build with zero errors; QEMU
  execution of the new regression and non-ext2 error matrix remains pending.
## 2026-09-10：静态 musl GCC 正式构建与镜像集成

`musl-gcc` 组件默认启用，普通桌面 ISO、VMDK、installer 的 live 根及安装负载
均包含 Dyne 2.2.0 原始 GCC 15.1.0、binutils 2.44、C/C++ 编译器、头文件及静态库。
归档 SHA256 与官方发布一致；6058 个上游文件全部保持内容不变，58 个 `/bin`
静态 ELF 启动器只负责重定位编译器/sysroot 并转交参数。静态链接使用 `-static`。
此套件独立于 GUI SDK，不更改 GCC 或应用源码，也不改变当前 Linux ABI 状态计数。

为了保留大小写不同的 Linux 头文件，普通 Live ISO 与 installer 内存根改用
classic ext2；旧 `root.fat` 模块路径与 FAT32 介质仍兼容。真实 ext2 RAM 读写、
statfs 可写标志已同步处理。安装器遍历从每目录最多 64 项改为 musl readdir
动态读取，并保留安装目录权限。相同静态包文件用 ext2 硬链接减少内存根体积；
可变配置和账户数据库不共享 inode。普通 ISO 保留 BIOS El Torito 项，EFI 使用
仓库验证过的 GRUB standalone，避免宿主 grub-mkrescue EFI 在 OVMF 中缺页。

实际验证：

- `tools/test_musl_gcc_package.py`：58 个命令 `--version`、C/C++ 静态编译和执行、
  as/ld/ar/ranlib/nm/objcopy/strip、大小写头文件及原始内容哈希通过。
- `tools/test_storage_rename.py`：ASan/UBSan 实际 ext2 实现的 1/4 KiB 块、
  RAM LBA=0 读写、rename/symlink 及 e2fsck 通过；metadata 权限测试通过。
- `tools/test_live_iso.py`：ext2 镜像可读写、大小写文件与硬链接、配置不共享通过。
- 安装器目录测试：宿主 800 项；QEMU 来宾原始 Linux include 543 项全部读到。
- `build/gcc-probe/guest-packaged-2vcpu.log`：原 GCC 与 `/bin/musl-gcc` 的编译、
  静态链接、stdio 输出、as/ld 及 vfork/pthread 回归，`DONE failures=0`。
- `build/musl-gcc-release-final.log`：普通 ISO、VMDK、installer、SDK 统一构建
  0 errors；最终普通 ISO 的 EFI/BIOS 打包日志为 `build/musl-gcc/iso-final-build.log`。
- `build/musl-gcc/image-verification.log`：从最终两个 ISO 提取内存根，桌面根、
  installer live 根及 `/install/root` 各自 6058 个文件及 58 个启动器逐字节通过。
- `build/musl-gcc/desktop-test.log`：最终普通 ISO 无硬盘 QEMU/KVM、OVMF、
  4 GiB、2 vCPU，OOBE/登录/桌面/Terminal，`musl-gcc -static` 编译 hello 并执行
  退出 0；截图 `build/musl-gcc/desktop/terminal-gcc.png`。
- `build/musl-gcc/installer-test.log`：同配置启动最终 installer，真实点击进入
  Language/Thanks/Style/Welcome；截图位于 `build/musl-gcc/installer/`。

验证边界：本轮未执行完整落盘安装/更新、BIOS 内核启动、VMware 或完整 LTP；
C++ 实际执行仅宿主验证，不能据此宣布全部 Linux ABI 或全部 GCC 功能兼容。
此前记录的共享匿名 mmap 首次触页问题、其余 syscall 未完成项仍然有效。
