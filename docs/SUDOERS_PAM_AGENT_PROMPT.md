# LeonOS-4 完整 sudoers / Linux-PAM 实施提示词

请在 `/home/xiaobai/Projects/Projects/LeonOS-4` 实际实现完整的 sudoers 策略体系和 Linux-PAM 本地认证体系，同步迁移 sudo、su、登录及图形提权消费者。要求提交可构建、可执行、经过自检和行为验证的代码，不要停留在分析、方案、接口占位或配置文件堆砌。

## 1. 强制约束

1. 全程由当前主 Agent 完成，禁止创建、调用或委派任何 Subagent，也禁止通过新任务、新会话绕过限制。
2. 先检查 `git status` 和适用的 AGENTS.md，保留所有已有修改。工作区已有大量尚未提交的修复，不得覆盖、回滚或顺手清理。
3. 联网统一使用代理 `http://127.0.0.1:12334`，优先官方源码、官方手册及安全公告。依赖必须固定正式版本或提交，记录来源、校验值、许可证和补丁；不能把浮动分支当成可复现构建依赖。
4. 内核接口以 Linux v6.12 native x86-64 ABI 为基准，不包含 i386/x32。用户态语义以所选固定版本的 upstream sudo、Linux-PAM 和 util-linux su 为准。这三者不是“Linux 6.12 内核规范”的组成部分，必须分别引用依据。
5. 使用现有 musl 工具链和 Alpine 风格、非 usr-merge 的 rootfs。不得恢复 Picolibc，不得借机替换 libc、重做桌面或恢复整个 Linux ABI 扩展任务。
6. 禁止伪成功、认证失败时降级放行、空模块、只识别配置但不执行语义、删除功能、削弱测试断言。遇到真实内核缺口，应实现必要前置条件并验证，不能通过绕过访问控制解决。
7. 本任务授权修改所需代码和构建配置，不要求逐步请求批准；不包含 commit、push 或操作真实宿主账户。所有破坏性认证测试在隔离测试目录或临时来宾磁盘中完成。

## 2. 先复核当前实现

### 2026-09-12 代码复核修正

本节下列路径和事实保留为实施前基线，不再表示当前生产状态。
原 `authd`、`authd_client` 和私有协议已移入 `tools/tests/legacy_authd/`，
不参与生产构建；原提权设计保存在 `docs/history/AUTHD_BROKER_RETIRED.md`。
当前入口使用固定版本的官方 sudo/Linux-PAM/util-linux/shadow，
账户权威已改为 passwd/shadow/group/gshadow；证据位置为
`userland/libc/src/{sudo_client,pam_session,auth_accounts}.c`、
`userland/auth/standard_accounts.c`、`system/rootfs/etc/pam.d/` 和 `build.py`。
用户随后明确暂停进一步测试并要求完成生产替换；这不构成完整兼容性验收。
实现、既有验证边界和未完成项以 `SUDOERS_PAM_STATUS.md` 为准。
以下强制实现要求和验收要求仍完整保留。

首先阅读实际代码和以下资料，历史文档与当前代码不一致时，给出证据并更正文档：

- `docs/SUDO_AND_ELEVATION.md`，尤其顶部 2026-09-11 修复说明；下方保留的历史设计不能覆盖顶部修正。
- `docs/security/2026-09-11.md`、`docs/POSIX_PERMISSIONS_2026-09-08.md`。
- `userland/apps/{sudo,su,sudod,authd}/`，重点为 authd 的 `accounts.c`、`authd_sudo.c`、`sudo_policy.h`。
- `userland/libc/src/{admin,sudo_client,authd_client,unix_ipc}.c` 及对应公开、私有头文件；查找实际密码输入实现。
- `userland/apps/fileman/elevation.c`、登录程序、installer 账户创建代码，以及所有账户数据库读写消费者。
- `kernel/ntclks/{permissions,syscall_process,syscall_socket,pty}.c`、调度器和 ELF exec 凭据处理路径。
- `include/leonos/auth.h`、`include/uapi/leonos/auth_db.h`、`build.py`、`configs/components.toml`、`Kconfig.components`、`tools/leonos_layout.py`。
- `tools/test_sudo_policy.py`、`tools/test_sudo_repair_qemu.py` 及其实际调用的测试源码。

需要复核的当前事实，不得直接假定它们已经变化：

- 现有 sudo/su 使用 root authd broker，不能假定内核已支持正确的 setuid executable / secure-exec 行为。
- 当前没有 sudoers/PAM；sudo 默认认证目标管理员密码，只允许管理员目标。这不是 upstream sudo 的默认策略。
- 现有缓存按请求 UID 和内核 session ID 分组，不是 upstream sudo 的 per-TTY timestamp，也不是命令授权策略。
- `/var/lib/leonos/users.db` 的 AUS2 数据目前是账户权威来源；`/etc/passwd`、`/etc/group` 是导出的身份镜像。
- 密码使用自定义 `$pbkdf2-sha256$100000$...` 格式。不能仅把它写入 shadow 就声称 pam_unix 可用。
- `authd_check_password()` 是 1 成功、0 失败；提权适配层采用 0 成功、负值失败。此前发生过返回值约定反转，必须保留真实密码算法回归。
- RUN 协议现已分别传递三个标准流，并处理 cwd、TERM、CLOEXEC、exec 失败反馈。WAIT、信号和结果访问绑定原请求 UID/PID；新实现不能丢失这些安全边界。
- 当前信号转发主要面向直接子进程，完整作业控制、后代监督和 PTY shared-OFD 语义仍需核对。
- 当前账户、参数数量、参数长度和 shell 选择仍有定制限制，应逐项消除与本任务有关的不兼容。

先输出简明的实际差异和实施决策，然后持续实现。维护逐项能力矩阵，至少含：上游依据、实现位置、测试、当前状态、具体缺口。状态区分“未处理 / 已实现待验证 / 已验证 / 存在具体阻塞”。

## 3. 完整性的定义与架构要求

目标是完整的 upstream sudoers 本地文件策略引擎、Linux-PAM 核心框架和本地账户认证闭环，不是只支持 `root ALL=(ALL) ALL` 的自写解析器，也不是把 authd 改名为 PAM。

优先直接集成固定版本的 upstream sudo/sudoers plugin、Linux-PAM 和成熟 su 实现，尽量保持上游代码不变，把必要适配集中在清晰的平台边界。必须解释任何不能直接采用上游实现的技术原因。禁止另写弱化版本取代已有成熟策略和 PAM 栈解释器。

允许保留 broker 作为 GUI 等场景的平台执行边界，但真实策略判断、PAM 状态机、会话和凭据语义必须贯通；不能同时保留一条绕过 sudoers/PAM 的旧 root 执行通道。标准 sudo/su 入口所需的 setuid/setgid exec、真实/有效/保存 ID、补充组、no_new_privs、nosuid、secure-exec/AT_SECURE、动态加载器环境处理，以及相关 ptrace/dumpable 约束必须补齐并验证；仅设置文件 mode 4755 不算完成。

建立所选上游版本的能力清单，覆盖其 CLI、sudoers 语法、PAM API、模块和插件。不允许悄悄关闭编译选项后宣称“完整”。LDAP、SSSD、Kerberos、SELinux、远程日志服务器等外部服务集成不要求凭空建设其服务端，但必须说明构建状态、依赖、未验证项及遇到相关配置时的实际行为；它们不能成为缩减本地核心功能的借口。Linux-PAM 完整核心不等于世界上所有第三方 PAM 模块均已适配。

## 3.1 前置条件同属必须修复的范围

以下条件不能仅列为“系统暂不支持”后绕过。先逐条复核，已经正确实现的保留并补证据；存在缺口的实际修复，再接入上层功能。任何必需项未完成，都不得把完整 sudoers/PAM 任务标为完成。

### 标准适用与证据

- 通用 Unix 接口以 POSIX.1-2024 的适用条款为依据；Linux 特有 ABI、凭据和对象生命周期以 Linux v6.12 native x86-64 源码为依据；libc 行为对照固定版本 musl。遇到 Linux 与 POSIX 的差异必须明确记录，并为本项目的 Linux 二进制兼容目标采用 Linux 的真实行为。
- Alpine 约束 rootfs 布局和集成风格，不决定内核 ABI，也不是 sudo/PAM 的替代规范。不能因为 Alpine 常见配置或宿主配置允许某事，就当成通用权限标准。
- sudoers、PAM、shadow 密码格式、crypt 算法并非全部由 POSIX 规定，应分别遵循对应上游规范。严禁用“符合 Unix”掩盖尚未核对的实现。
- 对每个前置条件记录：适用规范/固定源码位置、真实调用者、当前行为、目标行为、失败路径、对象生命周期和验证证据。Linux 源码重点包括 `kernel/cred.c`、`kernel/sys.c`、`fs/exec.c`、`security/commoncap.c`、`fs/namei.c`、`net/core/scm.c` 和相应 TTY、ptrace、rlimit 实现；具体路径以 v6.12 源码为准。

### P01：凭据模型与身份切换

- 完整区分 real/effective/saved UID/GID、filesystem UID/GID 和 supplementary groups，使用正确宽度及无符号 ID、哨兵值和错误码，不能把 UID、GID 或 real/effective ID 混作同一字段。
- 对实际需要的 get/setuid、get/setgid、get/setreuid、get/setregid、get/setresuid、get/setresgid、setfsuid/setfsgid、getgroups/setgroups 等逐项遵循 Linux 的权限检查、保存 ID 更新规则和返回语义。尤其不能把所有调用统一改成“real UID 为 0 才允许”，也不能用统一错误返回代替 setfsuid 等特殊契约。
- 正确处理 fork/clone/exec/exit 的凭据继承和共享边界。Linux raw syscall 的每线程凭据与 musl/POSIX setxid 包装器协调线程的行为需要区分；线程组内切换身份不能产生未受控制的高权限线程。
- 从标准 group 数据实际建立补充组，安全执行 initgroups、setgroups、setresgid、setresuid 的上游顺序及失败处理。永久降权之后不能靠保存 ID 或残留 capability 恢复 root。
- 不把 capability 检查简化成一个“管理员账户”布尔值。落实本任务路径依赖的 Linux capability 语义；没有实现的能力模型必须明确列为缺口，不能对 capget/capset 或相关 prctl 伪报成功。

### P02：特权 exec 与加载器安全

- 本任务应补齐标准 setuid/setgid 可执行文件机制和必要的 secure-exec 支持，不能以 broker 能运行 root 命令为理由豁免它们。broker 可以用于明确的 GUI 边界，但标准 sudo/su 入口不能依赖一个未披露的 Linux 语义替代品。
- 按 Linux v6.12 核对文件所有者、set-ID 位、nosuid、no_new_privs、被跟踪状态、脚本与解释器的处理，计算新有效/保存 ID 及相关 capability。不能给 setuid shell script 自行添加 Linux 不具备的提权行为。
- 使用 exec 已解析并持有的真实文件对象计算权限，保持权限判定、ELF/解释器读取与实际执行对象一致；处理执行中的写入、文件替换和失败回滚。失败的 exec 不得提前提交新凭据。
- 正确生成 auxv 中 AT_UID、AT_EUID、AT_GID、AT_EGID、AT_SECURE 等字段，检查初始栈和动态解释器约定。musl 动态加载器必须据此实施安全模式，不能只在 sudo main 中清理环境来代替加载器防护。
- 核对特权 exec 前后的 dumpable、ptrace、`/proc/<pid>` 敏感接口和进程内存访问规则。普通用户不能借调试、core dump、procfs FD 或内存接口读取 root 认证秘密或控制提权后进程。
- no_new_privs 必须具有正确继承和不可撤销语义；不能设置成功后仍允许 set-ID exec 增权。nosuid 必须由实际挂载状态生效，不能只出现在 `/proc/mounts` 文本中。

### P03：文件系统权限与持久化

- 对路径每级目录实施 search 权限，正确执行文件读写、目录创建/删除/rename 的 DAC 判断；支持 owner/group/other、补充组、umask、sticky bit、setgid 目录继承，以及本任务使用的 chmod/chown/fchmod/fchown 语义。
- 按 Linux 处理 chown 和写入等操作造成的 set-ID 位变化。文件所有者或普通写入者不能通过修改已标记特权的文件内容保留不应保留的提权能力。
- root 并非所有检查无条件成功，例如执行权限与 capability 的交互应对照 Linux；access/faccessat 与实际 open/exec 使用的身份不同，不能统一套用 effective UID 判断。
- ext2 持久化真实 UID/GID、mode、锁定相关元数据及必要挂载标志；不能只有内存权限正确、重启后所有文件变为 root 或默认可写。
- 正确实现本任务依赖的 dirfd、O_NOFOLLOW、O_CLOEXEC、O_EXCL、O_DIRECTORY、O_PATH、rename/unlink 后 FD 生命周期和错误语义。认证配置、模块、锁与结果文件的安全不能依赖内核未执行的标志。
- passwd/shadow 和 sudoers 更新所需锁、rename、fsync 必须实际生效；持久化能力不足必须修复或准确阻断写入流程，不能同步接口返回 0 后宣称具备崩溃一致性。

### P04：Unix socket 与文件描述符能力

- 按 Linux 核对 SO_PEERCRED 的凭据采样时机和连接继承，以及 SCM_CREDENTIALS/SO_PASSCRED 的消息级身份语义。连接建立时的 peer PID/UID 不等于所有后续消息的实时发送者；fork 或传递连接 FD 后必须有明确、安全的身份绑定方案。
- 不接受客户端自报身份，也不靠可复用 PID 的单次查询实现认证。需要发送者凭据或稳定任务身份时补齐真实内核支持，不能只在用户态保存一个 PID 数字。
- SCM_RIGHTS 正确持有 open file description 引用，独立保存接收端 FD_CLOEXEC，处理 MSG_CMSG_CLOEXEC、截断、丢弃、断连、接收失败和资源耗尽。PTY、pipe、socket、普通文件等实际使用对象均需覆盖。
- 实现正确的共享偏移/status flags 与逐描述符 flags，核对 dup/fcntl/close/exec 的边界。不能因当前应用刚好不用 F_SETFL 就把 PTY OFD 共享缺口视为已解决。
- 特权 IPC 的凭据、协议长度、FD 数量和队列限制失败时保持引用计数及授权状态一致；不能出现客户端收到错误但命令已执行的不可追踪状态。

### P05：会话、TTY、信号与等待

- 补齐实际使用的 setsid/getsid、setpgid/getpgid、controlling TTY、tcgetpgrp/tcsetpgrp 及相关 ioctl 的权限和生命周期语义。
- 实施前台/后台进程组的 SIGINT、SIGQUIT、SIGTSTP、SIGTTIN、SIGTTOU、SIGHUP 和窗口变化行为，正确处理 orphaned process group 等本任务触及的条件；不能只把信号转发给第一个 shell PID。
- termios 密码输入应真实关闭 echo，并在失败、中断、取消和异常退出后恢复终端。阻塞读、EINTR、SA_RESTART、waitpid/wait4 状态与错误行为对照 Linux。
- 为 sudo/su 会话正确提供退出状态、停止/继续状态和终端归还，防止退出后 Terminal 无法输入或遗留 root 后台任务失去管理。

### P06：PAM 运行时、模块与资源

- musl 的 dlopen/dlsym/dlclose、ELF 重定位、TLS、构造/析构和必要 pthread 行为需足以执行真实 PAM 模块；不能把模块 API 静态伪装为成功来绕过加载器缺口。
- 动态模块和辅助程序的查找路径在特权环境下必须可信，处理符号加载失败、ABI 不匹配和依赖缺失，不能回退到用户目录或用户提供的替代模块。
- 正确提供认证所需安全随机数、时钟、进程与线程同步。随机源不可用时不能用 PID/时间作为密码盐或安全令牌的替代；时间戳超时使用上游定义的时钟和溢出处理。
- pam_limits 使用到的 rlimit 必须有真实内核实施，软/硬上限、继承、权限检查和信号行为按 Linux 对照。维护资源逐项清单，不允许只保存配置数值。
- 对认证模块依赖的 passwd/group/shadow 查询、crypt、锁和错误传播实现完整 contract，保留 ERANGE、未知用户、锁定账户、I/O 错误等区别，并控制对非授权调用者暴露的信息。

### 前置条件验收门槛

- 为 P01–P06 各建立正常、拒绝、失败回滚和并发/生命周期测试；对内核行为使用按 Linux 头文件和 raw syscall 约定构建的程序，不只测试 LeonOS 自定义包装器。
- 必测：set-ID ELF 正常转换与保存 ID；no_new_privs/nosuid 抑制增权；exec 失败不变更凭据；永久降权不能恢复 root；supplementary groups 正确；AT_SECURE 与恶意加载器环境；非特权 ptrace/procfs 访问拒绝；shadow 不可读；重启后权限持久化；共享 socket/FD 后的身份绑定；TTY 前后台信号；PAM 动态模块实际执行；所配 rlimit 真正生效。
- 使用宿主 Linux 同一探针建立参考，再在 LeonOS QEMU 中运行。不同 Linux 版本的宿主只能作为辅助证据，版本特有差异以固定 v6.12 源码或 v6.12 参考来宾确认。
- 这些前置条件必须进入最终能力矩阵，不能移到“任务范围外”来换取全部完成结论。可以分阶段实现，但最终交付必须准确保留所有未完成项。

## 4. sudoers 策略与工具

1. 正确支持 User/Host/Runas/Cmnd Alias、用户与组、数值 UID/GID、ALL、否定、命令与参数匹配、转义、通配符及固定版本支持的正则、摘要、include/includedir 和 Defaults 的各作用域。多条规则匹配、覆盖和标签继承必须直接遵循上游，不能自己猜测“第一条/最后一条胜出”。
2. 完整执行所选版本的授权标签和选项，包括 PASSWD/NOPASSWD、SETENV/NOSETENV、环境策略、认证对象、时间戳、命令执行条件和日志设置。NOEXEC/INTERCEPT 等特性必须按上游实际机制和适用边界实现并记录限制；不能声称动态拦截天然约束所有静态二进制。
3. sudo 默认认证调用者，而不是固定认证 root。正确实现 rootpw、targetpw、runaspw 配置及组合；su 按目标账户认证策略工作，root 特例由正确的凭据和 PAM 栈决定。
4. 认证、授权、账户检查、会话必须分开。正确密码和有效 timestamp 不能代替本次命令的策略授权；每次执行重新判断当前策略、目标用户/组、命令、参数和环境，处理策略撤销、账户禁用与组变更。
5. 安装时普通账户不能因被创建就自动获得无限 sudo 权限。提供 root 管理策略和可明确配置的 wheel 规则，不得偷偷把所有普通用户加入 wheel。缺少授权时明确拒绝；认证失败、身份查询错误、策略加载错误不得放行。
6. 按上游保护 `/etc/sudoers`、`/etc/sudoers.d` 及其父目录，处理所有权、写权限、符号链接和并发替换。提供真实 `visudo`，支持检查、指定文件、安全编辑、锁、语法错误处理和原子替换。不能让普通用户选择任意策略文件或插件目录进入特权路径。
7. 支持上游 sudo 的正常 CLI 工作流，至少覆盖 `-u/-g/-l/-v/-k/-K/-n/-S/-A/-s/-i/-H/-E`、保留环境选项和 `--`；完整 CLI 差异表不能遗漏不支持选项。帮助、诊断信息、退出码、无 TTY 情况及 askpass 行为按上游核对。
8. 支持 sudoedit 的真实安全模型：编辑器以调用者身份执行，临时文件、目标路径检查、所有权和复制回写正确，处理 symlink、目录替换和编辑失败。不能把 root 编辑器执行冒充 sudoedit。
9. 取消现有 8 个参数、单参数 191 字节之类的任意 ABI 限制，使用受实际 exec 上限约束且检查溢出的动态数据结构，超限按明确的上游/内核错误返回。
10. 命令路径、参数和摘要检查必须与实际执行对象一致。审查 PATH、cwd、symlink、文件替换和检查到执行之间的竞争；采用上游适用的 fd 执行策略，说明脚本解释器带来的真实限制。

## 5. Linux-PAM 核心与模块

1. 接入真实的 Linux-PAM 头文件、库、模块加载器、配置解释器和构建安装规则。安装路径符合当前 Alpine 风格布局，SDK 中提供开发所需文件。来宾必须实际加载模块，不允许测试全靠宿主 Linux-PAM。
2. 实现并核对完整 PAM transaction 生命周期：pam_start/pam_end、authenticate、acct_mgmt、setcred、open_session/close_session、chauthtok，以及相关 item、data、environment、conversation API。具体 API 集合以固定版本公开头文件为准。
3. 正确解释 auth/account/password/session 四组管理栈，required/requisite/sufficient/optional、扩展返回值控制、跳转、reset、include/substack，以及默认服务行为。包含嵌套、错误返回、循环/过深配置等情况；不得把所有非零结果粗暴归一而破坏控制语义。
4. conversation 支持一批多条消息、echo on/off、提示与错误消息、EOF、取消、信号中断、非交互模式和错误清理。明确字符串分配与释放责任，擦除密码与 PAM_AUTHTOK 等秘密；不把密码放进 argv、环境、日志或持久文件。
5. 正确区分认证成功、账户允许、密码过期必须修改、账户过期、凭据不可用和会话建立失败。session/setcred 失败必须阻止执行或按上游明确行为处理；不能认证成功后忽略其余阶段。
6. 最低必需模块集：pam_unix、pam_deny、pam_permit、pam_rootok、pam_env、pam_limits、pam_nologin、pam_shells、pam_access、pam_wheel、pam_succeed_if、pam_faildelay、pam_faillock、pam_exec、pam_umask。核对固定版本是否提供这些模块及依赖，按真实语义接入；其余上游模块逐项记录状态，不得用同名恒成功模块填充。
7. pam_limits 必须真正应用资源限制；相关内核限制若只有存取数值、未实施，应修复必要部分或明确为未完成。pam_unix 的辅助程序权限、锁、crypt 后端及失败路径必须审查。
8. 创建并实际使用 `sudo`、`sudo-i`、`su`、`su-l`、`login`、`passwd` 和 GUI 登录所需服务配置；具体服务命名遵循调用者实际使用名称。`other` 默认拒绝，不通过备用栈隐藏配置缺失。
9. pam_permit 等允许模块本身有合法用途，不能为了“安全”破坏其 API；安全默认配置应通过正确栈组合实现。认证限速/锁定策略必须显式可配置，不得无说明地改变用户密码规则。
10. PAM handle、conversation 和模块数据按请求隔离；避免长期 root broker 因同步提示阻塞所有请求。并发、退出、取消和失败时必须正确清理，无跨请求认证状态串用。

## 6. 账户库、密码与所有消费者

1. 明确唯一权威来源，目标建立可供标准 pam_unix 使用的 passwd/shadow/group/gshadow 本地账户体系。同步修改 authd、installer、登录、密码修改、组管理、内核或用户态身份查询和 SDK 消费者，不能让镜像与私有数据库各自可写而长期分叉。
2. 自定义 PBKDF2 哈希不能无密码转换成另一种哈希。核对 crypt 后端实际支持的安全格式，采用随机盐和正确失败处理。若需要过渡模块或登录后重哈希，应明确其范围；过渡 pam_leonos 不能替代必须可用的 pam_unix。
3. 项目不要求完整自动兼容所有旧系统结构，但必须明确现有账户数据的处置策略。不得静默重置密码、丢失用户、把锁定账户变为可登录或向外导出秘密。无法安全迁移的格式应明确拒绝并给出受控处理路径；新安装必须完整工作。
4. 标准账户文件格式、权限、锁定标记、UID/GID、补充组、HOME、shell、密码和账户过期字段按上游核对。普通用户不能读取 shadow/gshadow，也不能通过 procfs、日志、错误输出或备份文件获取哈希。
5. 更新使用真实锁与安全临时文件、必要的同步和原子替换。多文件更新必须有明确故障恢复策略；单次 rename 不等于多个数据库文件已构成事务。覆盖并发改密、磁盘满、短写和中途失败。
6. installer 保留已有产品要求：创建普通无 root 权限用户；管理员名称固定为 root；用户名可选；密码非空、长度 1–32、不允许空格，不新增强制复杂度要求。明确长度的字节/字符口径并统一 GUI、TTY 和服务端验证。账户名还需拒绝账户文件分隔符、控制字符和路径注入，错误必须可理解。
7. 标准 passwd 修改流程必须走一致的 PAM/backend，不可保留绕过新策略的旧改密接口。服务端不能只信任 GUI 的输入校验。

## 7. 权限边界、缓存、执行与图形提权

1. 请求身份来自内核可信凭据。审查 Unix socket 的连接生命周期、SO_PEERCRED、PID 复用、fork 后连接复用、凭据变化，以及 FD 传递后的请求代理行为；不能仅凭客户端提交的 UID/PID/用户名/TTY/session ID 决定权限。
2. 对每条提权 API 绘制调用链：调用者身份 -> 策略 -> PAM -> timestamp -> 凭据 -> 执行 -> wait/清理。所有 RUN、FILEOP、VERIFY、WAIT、signal、结果读取必须有明确授权边界。
3. timestamp 按上游实现 per-TTY/ppid/global 等支持模式及默认值，包含身份、生命周期、防伪造、过期和锁。核对 PID 重用、不同终端、无 TTY、时间回拨、重启、账户删除重建及 `-k/-K/-v` 差异。缓存只证明规定范围内的认证，不能成为任意 root 操作令牌。
4. root 命令的 real/effective/saved/fs UID/GID、补充组、HOME、USER、LOGNAME、SHELL、PATH、cwd、umask、rlimit、环境和信号状态符合目标模式。检查每次凭据设置的返回值，设置失败不得继续 exec。
5. 区分 sudo 普通执行、sudo -i/-s、su 和 su -，从账户数据库读取 shell，落实 restricted/nologin shell 策略。环境清理应遵循 sudoers/PAM/su 的各自契约，处理 LD_*、PATH、IFS、ENV、BASH_ENV 等危险输入和允许保留变量。
6. 保持 stdin/stdout/stderr 独立，包括已关闭标准 FD、pipe、普通文件、PTY。除明确允许保留的 FD 外，root 子进程不得继承认证 socket、目录 FD、结果文件、锁或秘密。保留 exec 错误的可靠同步反馈。
7. 完成终端前台进程组、Ctrl-C、Ctrl-Z、fg/bg、窗口大小变化、退出信号和 shell 管道的必要语义。正确报告命令 exit status；明确客户端退出、超时、服务退出时的子进程和后代处理，不可随意杀死全部同 UID 进程。
8. Fileman 和 GUI 登录/提权同步接入一致的 PAM 与授权机制。不能继续“输入一次 root 密码便允许所有 FILEOP”。为 GUI 操作定义可信、可审查的动作到策略映射；限定 worker、参数、目标对象和会话，禁止客户端自行声明已授权。
9. 保留并扩展 FILEOP 的 dirfd/O_NOFOLLOW 路径保护、受限结果 FD、请求所有者检查。认证改造不能重新引入 root 路径替换、结果文件泄漏或跨用户取结果。
10. 审计日志记录必要的调用者、目标、服务、策略结果、执行结果和会话关联信息；秘密不入日志，命令参数和环境中的敏感值遵循明确的日志策略。认证失败、日志失败和资源耗尽的处理须按配置及上游核对。

## 8. 验证与验收

必须编写有意义的回归并运行。宿主对照、真实实现单测、来宾测试分别报告，不能以编译成功、源代码字符串检查或 mock 恒返回成功证明兼容。

### A. 策略与认证

- 使用所选版本的上游解析器和 Linux 隔离环境作为参考，覆盖 aliases、Defaults 作用域、否定、include、参数、摘要、语法错误和权限错误。
- 普通账户默认无 sudo 授权，输入正确密码也不能执行未授权命令；配置限定命令后，默认使用该调用者密码；正确 root 密码不能取代默认调用者认证。分别验证 rootpw/targetpw/runaspw。
- 验证 `-u` 普通用户、`-g`、补充组、数值 ID、PASSWD/NOPASSWD、命令撤销、环境控制、sudoedit 和 visudo 安全编辑。
- 验证正确密码、错误密码、空输入、过长输入、多消息 conversation、取消、EOF、密码/账户过期、锁定和无登录 shell。
- 覆盖 PAM 控制栈的成功/失败组合、include/substack、session 和 setcred 失败、chauthtok 两阶段及所有资源清理。
- 保留真实密码算法测试，覆盖历史“布尔返回值反转”和“普通账户认证污染管理员缓存”漏洞；不要只测新的包装函数。

### B. 对抗与生命周期

- 验证不同用户、终端、进程、PAM 服务之间的缓存边界，策略更新后的拒绝，时间戳过期和 `-k/-K`。
- 覆盖恶意 PATH/环境、参数边界、shell 元字符、symlink/rename 竞争、摘要检查与执行对象不一致、非可信配置/模块路径、FD 泄漏和请求身份伪造。
- 覆盖资源耗尽、分配失败、短读写、EINTR、模块加载失败、exec 失败、结果接收方断连、并发请求和账户更新；失败不得产生未经授权的文件副作用或 root 子进程。
- 连续运行及并发运行，检查 worker、PAM handle、FD、PTY 引用、锁、敏感缓冲区和任务槽是否泄漏。设置并验证明确的有界资源策略。
- 提权 child、grandchild、pipeline 的退出和信号行为均需验证，不能只证明一个 `echo` 命令成功。

### C. 构建与系统体验

- 运行适用的上游 sudo/Linux-PAM 测试集、宿主 ASan/UBSan 测试和现有 sudo/socket/PTY 回归。新增内核行为时运行对应 Linux raw syscall 对照及适用 LTP 子集；未运行的项目明确列出。
- 当前回归入口包括 `python3 tools/test_sudo_policy.py`、`python3 tools/test_linux_socket_batches.py`、`python3 tools/test_linux_pty.py`。先读取脚本确认参数和语义。
- 在关键阶段统一构建 kernel、userland、installer、SDK，避免每个小修改都重建 ISO。当前构建入口为分别执行 `python3 build.py run kernel`、`python3 build.py run userland`、`python3 build.py run installer`、`python3 build.py run sdk`；执行前复核当前 CLI。
- QEMU 使用独立 ext2 测试磁盘，验证 installer 注册、重启后的账户、TTY 登录、桌面登录、Terminal、sudo/su、Fileman 图形提权，以及管道、重定向和交互终端恢复。
- 证明来宾实际使用打包进去的 sudoers engine、Linux-PAM 库和模块；记录版本、文件、加载证据及可复现命令。标准库存在但认证仍全部走旧 authd，不算接入完成。
- 对新安装必测；旧数据库支持范围按已确定策略测试，不要求无限历史升级兼容，但不得静默损坏。
- VMware 无法运行时明确标记待验证。QEMU 单核结果不代表多核压力通过，宿主测试不代表来宾通过。
- 任何 skip、预期失败、未实现模块或缺失内核能力都列入能力矩阵；核心必需项未完成时不得宣布整个任务完成。

原有测试中的目标密码、管理员专属目标、简化 cache 等预期可能属于旧产品语义。只能在明确引用上游依据、增加新行为与安全回归后调整这些预期；不能为了通过测试而机械保留错误语义或降低安全断言。

## 9. 强制自检

实现后由当前主 Agent 独立重读最终 diff 和完整关键函数，执行一次攻击者视角的代码审查，不得调用其他 Agent。至少回答并给出源码/测试证据：

1. 普通用户知道自己的密码，能否越过 sudoers 获得 root？知道某个普通目标账户密码，能否污染其他服务或 root 的缓存？
2. 正确密码、NOPASSWD 或旧 timestamp 是否会绕过当前命令策略、账户限制或 session 失败？
3. 客户端可控的 UID/PID、TTY、环境、argv、路径、FD、服务名或模块配置是否被错误当成可信输入？
4. 认证后到 exec 之间改变用户、组、策略、命令或文件路径，会发生什么？执行对象和授权对象是否仍一致？
5. PAM 栈的每个提前返回、取消、fork、exec 和崩溃路径，是否恰当清理 handle、凭据、session、FD 和秘密？
6. 新旧登录、提权、改密和 FILEOP 路径是否仍存在绕过统一策略的入口？账户数据库是否只有一个权威来源？
7. 高权限进程是否运行了不可信 editor、helper、共享库、shell 启动文件或环境指定程序？这些行为是否符合上游明确授权语义？
8. 测试是否执行真实 production 实现？是否混淆宿主与来宾、文本检查与行为验证、配置可解析与功能已实施？

对发现的问题直接返修并重跑受影响测试，记录发现、修复和剩余风险。不得仅写“已自检，无问题”而没有具体证据；已知越权、凭据泄漏和核心正确性缺陷必须修复后交付。

## 10. 最终交付

- 实际修改摘要、架构与信任边界、固定依赖版本/来源/校验值和必要平台补丁。
- sudoers、PAM 服务栈、账户数据库与路径布局说明，以及普通账户如何由 root 明确授予限定权限的示例。
- 完整能力矩阵和上游差异清单，逐项写明验证层次、测试命令和证据，不以“支持 sudoers/PAM”一句话代替。
- 实际测试结果、关键负面测试结果、构建和 QEMU 日志、可复现步骤、最终测试 ISO 的绝对路径与校验值。
- 自检发现及修复记录；所有未完成项、具体阻塞、外部依赖和待 VMware 验证项。

持续实施到上述本地核心范围完成。不要用“基础框架完成”“后续可以扩展”替代必需功能；确有无法解决的阻塞，应准确指出上游要求、当前缺失、已尝试方案和最小解除条件。

## 官方依据入口

以下是查阅入口，不是构建时允许使用的浮动依赖；实施时固定具体版本并引用对应文件/章节。

- sudo 官方源码：https://github.com/sudo-project/sudo
- sudoers 手册源码：https://github.com/sudo-project/sudo/blob/main/docs/sudoers.man.in
- sudo 手册源码：https://github.com/sudo-project/sudo/blob/main/docs/sudo.man.in
- Linux-PAM 官方源码：https://github.com/linux-pam/linux-pam
- PAM 配置语法：https://github.com/linux-pam/linux-pam/blob/master/doc/man/pam.conf-syntax.xml
- util-linux su：https://github.com/util-linux/util-linux/tree/master/login-utils
- Linux v6.12：https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/?h=v6.12
- musl：https://git.musl-libc.org/cgit/musl/
- POSIX.1-2024：https://pubs.opengroup.org/onlinepubs/9799919799/
