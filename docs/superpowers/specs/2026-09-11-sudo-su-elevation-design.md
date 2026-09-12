# sudo / su 提权与 Fileman 权限提升 设计规格

- 状态：待审查
- 日期：2026-09-11
- 范围：LeonOS 4 用户态（authd 协议扩展 + libc 客户端 + 两个 CLI + 一个特权 worker + Fileman）
- 明确排除：内核改动、公开 ABI/SDK 改动、文件 setuid 位、sudoers/PAM

## 1. 问题陈述

LeonOS 4 拥有真实的 Unix 权限模型，但**没有任何从普通用户到 root 的提权通道**。这不是"缺少一个命令"，而是一条缺失的机制链：

| 事实 | 证据 |
| --- | --- |
| 账号体系真实存在：`root` = UID 0 `/root`，普通账号 = UID 1000，PBKDF2-HMAC-SHA256（100000 轮，随机 16 字节盐）存于 `/var/lib/leonos/users.db` | `include/uapi/leonos/auth_user.h:13-20`、`userland/apps/authd/accounts.c:54-112`、`docs/INSTALLER_ACCOUNTS.md:9-20` |
| 文件权限由内核强制执行，owner/group/other 三段判定，UID 0 完全绕过 | `kernel/ntclks/permissions.c:186-201` |
| authd 的 `LOGIN` 与 `ELEVATE` 都要求**对端 UID 为 0** | `userland/apps/authd/main.c:208`、`:238` |
| 内核只允许真实 UID 0 改变身份 | `kernel/ntclks/syscall_process.c:406`、`:430` |
| 登录后 GUI 会话以登录用户 UID 运行 | `kernel/ntclks/syscall_process.c:412-424`、`userland/libc/src/launch.c:219-237` |
| 旧的提权 ioctl 已被删除，`TASK_FLAG_ELEVATED_ADMIN` 全树从未被设置 | `sched.h:144`（只读/清除/保留）、提交 `dbf187d` |

组合起来的结果：`leonos_admin_elevate()`（`userland/libc/src/admin.c:18-56`）对普通用户**永远走不到密码校验分支**，只会在 authd 守卫处失败并返回 0。diskmgr 的格式化、apiapp 的包安装等"需要管理员授权"的功能在安装后的系统里静默失效 —— 这是本规格顺带修复的既有缺陷。

同时，Fileman 遇到受保护目录只把错误写进状态栏（`userland/apps/fileman/model.c:350-362`），没有任何提权入口。

## 2. 目标与非目标

### 目标

1. `sudo <命令>`：验证管理员口令后以 UID 0 执行命令，退出码如实回传。
2. `su [-c <命令>]`：验证 root 口令后切到 UID 0 起交互 shell 或执行单条命令。
3. 5 分钟凭据缓存，`sudo -k` 手动失效。
4. Fileman 打开 root 权限文件夹时弹密码框，验证通过后该目录可正常浏览、新建、重命名、删除。
5. 修复 `leonos_admin_elevate()`，使 diskmgr / apiapp 在普通用户会话下真正可用。

### 非目标（本轮不做，且不得在文档中暗示已完成）

- 内核改动、`include/leonos/` 公开 ABI 改动、`devtools/` SDK 与 zip 改动。
- 文件 setuid/setgid 位执行（`docs/POSIX_PERMISSIONS_2026-09-08.md:69-72` 的"未实现"结论保持准确）。
- sudoers 策略、wheel 组、PAM、`sudo -u <普通用户>` 的受限委托。
- Fileman 的文件内容复制/粘贴提权（受保护目录下仍如实报权限错误）。
- 恶意 GUI 应用的防护（见第 9 节威胁模型）。

## 3. 架构：authd 特权代理

核心原则：**口令校验发生在 authd 内，root 身份由 authd 派生的子进程持有；客户端既不获得也无法声称凭据。** 与 Linux sudo 一致 —— 权限来自服务端校验结果，而非客户端声明。

```text
普通用户进程 (UID 1000)                     authd（常驻 UID 0）
  sudo / su / fileman ─┐                        │
                       │ RUN/WAIT/FILEOP 请求   │
                       ├───────────────────────▶│ 1. 校验口令或查凭据缓存
                       │                        │ 2. fork（子进程保持 UID 0）
                       │◀───────────────────────┤ 3. 子进程继承调用者 stdio
                       │  ACK{child_pid}        │ 4. execve
                       │                        │
                       │ WAIT 轮询              │ 5. 以父身份回收，回报退出码
                       └───────────────────────▶│
```

为什么只能这样：内核权限判定读**调用任务自己的凭据**（`kernel/ntclks/permissions.c:191-200`），而 UID != 0 的任务无法把自己变成 0（`syscall_process.c:406`）。因此"让一个已经是 UID 0 的进程替你执行"是唯一不触碰内核的路径。

选择不引入内核 ioctl 的额外理由：被删除的 `LEONOS_AUTH_IOCTL_ELEVATE_ADMIN`（`dbf187d`）证明这条路在本项目里已被否决过一次；重新引入会同时牵动 `include/leonos/`、内核、libc、`devtools/` 四层 ABI 闭环（`AGENT.md:84-99`），代价远高于收益。

## 4. 协议扩展

全部落在 authd 私有线协议头 `userland/libc/include/leonos/authd.h`，**不进入公开 ABI**。

```c
enum leonos_authd_msg {
    /* ... 现有 10..29 保持不变 ... */
    LEONOS_AUTHD_MSG_RUN        = 30,  /* 特权执行 */
    LEONOS_AUTHD_MSG_WAIT       = 31,  /* 轮询子进程退出状态 */
    LEONOS_AUTHD_MSG_SUDO_KILL  = 32,  /* sudo -k：作废凭据缓存 */
    LEONOS_AUTHD_MSG_SUDO_CHECK = 33,  /* 查询缓存是否有效 */
    LEONOS_AUTHD_MSG_FILEOP     = 34,  /* Fileman 特权文件操作 */
};

#define LEONOS_AUTHD_RUN_MAX_ARGS 8U
#define LEONOS_AUTHD_RUN_ARG_LEN  192U

struct leonos_authd_run {
    char username[LEONOS_AUTH_USERNAME_LEN]; /* 空 = root */
    char password[LEONOS_AUTH_PASSWORD_LEN]; /* 空 = 仅走凭据缓存 */
    uint32_t argc;
    uint32_t reserved;
    char argv[LEONOS_AUTHD_RUN_MAX_ARGS][LEONOS_AUTHD_RUN_ARG_LEN];
};

struct leonos_authd_run_ack {
    int32_t  code;       /* 0 或负 errno */
    uint32_t child_pid;  /* code==0 时有效 */
};

struct leonos_authd_wait {
    uint32_t child_pid;
    uint32_t options;    /* 0 = 阻塞至多 100 ms；1 = WNOHANG */
};

struct leonos_authd_wait_ack {
    int32_t  code;       /* 0 子进程已退出；EAGAIN 仍在运行；ESRCH 未知 */
    uint32_t reserved;
    int32_t  status;     /* code==0 时有效，wait4 风格状态字 */
};

#define LEONOS_FILEOP_LIST   1U  /* path1 = 目录，返回其目录项 */
#define LEONOS_FILEOP_MKDIR  2U  /* path1 = 父目录，path2 = 新目录名 */
#define LEONOS_FILEOP_RENAME 3U  /* path1 = 旧路径，path2 = 新路径 */
#define LEONOS_FILEOP_UNLINK 4U  /* path1 = 目标（目录则递归） */

struct leonos_authd_fileop {
    char username[LEONOS_AUTH_USERNAME_LEN]; /* 空 = root */
    char password[LEONOS_AUTH_PASSWORD_LEN]; /* 空 = 仅走凭据缓存 */
    uint32_t op;
    uint32_t reserved;
    char path1[LEONOS_FS_PATH_LEN];
    char path2[LEONOS_FS_PATH_LEN];
};
```

帧容量：`LEONOS_IPC_ATOMIC_FRAME_CAP` 为 8192 字节（`unix_ipc.h:12`）。`run` 约 1.7 KB、`fileop` 约 0.7 KB，均在限额内。`LEONOS_AUTHD_RUN_ARG_LEN 192` 足以容纳 `/usr/lib/leonos/apps/...` 全路径加参数。

### 4.1 为什么需要 WAIT 消息

`wait4` 存在（`kernel/ntclks/syscall.c:6232-6251`），但 `sched_wait_reap()` 只允许**父进程**回收（`kernel/ntclks/sched/sched.c:2880`：`task->parent_pid != sched_task_tgid(waiter)` 即跳过）。sudo 的子进程是 authd 的，因此 sudo 无法 `waitpid` 它。由 authd 代持等待责任，正是"特权代理"的自然延伸，且完全不需要内核改动。

约束：authd 崩溃重启后其子进程被重新挂到 init，客户端会收到 `ESRCH`，此时 sudo 以非零码退出并报告"无法确认执行结果"，**不谎报成功**。

### 4.2 结果传递（仅 FILEOP 需要）

`FILEOP` 需要一个返回目录项列表的数据通道。方案：authd 以**调用者 UID** 在 0700 root-only 目录 `/run/leonos/fileop/` 下创建 0600 结果文件（路径含 pid 与递增序号），把路径作为 worker 的固定 argv 传入；worker 执行完成后写入结果。`sudo` 客户端轮询 `WAIT` 拿到退出状态后读取并删除该文件。

- `RUN` 路径**不使用结果文件**：退出码经 `WAIT` 回传即可。
- **口令绝不落盘**：只在 authd 内存中校验，校验后立即清零（沿用 `admin_clear_secret` 的 volatile 擦除写法）。
- `LIST` 结果上限 64 项 × `sizeof(struct leonos_dir_entry)`，整体封顶 64 KB，超出返回 `EOVERFLOW`。

## 5. authd 服务端行为

新增处理函数与现有 `authd_handle_elevate()` 并列，复用 `authd_find_name()`、`authd_verify()`、`authd_load/save`。

### 5.1 请求者授权规则

每个新消息都必须显式校验对端身份，**沿用 SO_PEERCRED 作为唯一信任来源**（`authd/main.c:549-562`），不得从请求体读取任何身份字段：

| 消息 | 允许的对端 |
| --- | --- |
| `RUN` | 任意 uid（口令/缓存才是门禁） |
| `WAIT` | 仅该子进程的创建者（记录于 authd 侧 `run_slots[]`） |
| `SUDO_KILL` | 任意 uid，只作用于自己的缓存条目 |
| `SUDO_CHECK` | 任意 uid，只查询自己的缓存条目 |
| `FILEOP` | 任意 uid（同 `RUN`） |

### 5.2 授权判定（`RUN` / `FILEOP` 共用）

判定输入为**请求者身份（取自 SO_PEERCRED，不可伪造）**、目标账号、口令三者。

1. 解析目标账号：`username` 为空视为 `root`。
2. 账号必须存在且未设 `LEONOS_AUTH_USER_DISABLED`。
3. 分两种情况：
   - **切换到普通账号**（`role == LEONOS_AUTH_ROLE_USER`）：允许。口令必须是**该目标账号自己的口令**。
   - **提权到管理员账号**（`role == LEONOS_AUTH_ROLE_ADMIN`，含 root）：允许。口令必须是该管理员账号的口令。
4. **请求者已是 UID 0 时，切换/提权到任意启用账号免密**（与真实 Unix 中 root 执行 `su` 一致）。这是唯一免密路径。
5. 口令非空：`authd_verify()` 校验目标账号口令；成功则刷新该 **请求者 UID** 的凭据缓存时间戳。
6. 口令为空且非第 4 条情形：仅当该请求者 UID 的缓存未过期（300 秒）才放行，否则返回 `-EACCES`。
7. 口令为空且无有效缓存 → `-EACCES`；口令错误 → `-EACCES`（客户端文案不区分，见第 12 节）。
8. `FILEOP` 额外执行路径校验：拒绝 `/proc`、`/sys`、`/dev` 前缀与元数据路径（`metadata_path`），路径必须是绝对路径且不含 `..` 组件。

> **为什么"普通账号 → 普通账号"也需要口令**：内核只允许 UID 0 改变身份（`kernel/ntclks/syscall_process.c:406`），因此 `su xiaobai` 由 alice 发起时，身份切换必须由 authd 代做。要求 xiaobai 自己的口令是防止 alice 无凭据冒充他人；要求调用者已提供正确口令，正是"认证后授权"的全部含义。


### 5.3 凭据缓存

- 结构：`struct authd_sudo_cache { uint32_t uid; uint32_t authenticated_at_ms; uint32_t valid; }`，最多 32 条（与 `LEONOS_AUTH_MAX_USERS` 对齐）。
- 有效期：`LEONOS_SUDO_CACHE_MS 300000`（300 秒）。
- 失效时机：`sudo -k`、`leonos_auth_logout()`、authd 启动、以及**该账号口令被改写或被禁用时**（`CHANGE_PASSWORD` / `UPDATE` 命中目标账号即清空其相关条目）。
- 语义边界：缓存只记录"谁验证过"，不记录"验证去干什么"，不扩大授权范围；每次请求仍要重新做第 5.2 节的目标账号、角色与路径判定。

### 5.4 执行路径

`RUN` 成功判定后：

1. `fork()`。子进程不调用任何 `setuid`（见第 7.2 节硬约束），按目标账号设定凭据 —— 目标为 root 时保持 UID 0；目标为其他管理员账号时，由 authd 侧以 `setgid`/`setuid` 系统调用设置（authd 自身是 UID 0，合法）。
2. 子进程继承调用者的 `stdin`/`stdout`/`stderr`（由客户端经 `leonos_ipc_send_fd` 传入，`unix_ipc.h:32-33` 已验证支持 SCM_RIGHTS）。
3. `execve(argv[0], argv, envp)`，`envp` 由 authd 构造最小集合（`HOME`、`USER`、`LOGNAME`、`PATH`、`TERM`），不继承 authd 自身环境。
4. 在 `run_slots[]` 登记 `{child_pid, owner_uid, result_path, used}`，回复 `run_ack{0, child_pid}`。
5. authd 主循环在没有客户端活动时以 `waitpid(WNOHANG)` 回收已退出子进程，把状态记入对应 slot，槽位保留到客户端 `WAIT` 取走或超时 60 秒。

`FILEOP` 走同一 fork/exec 骨架，但 argv 固定为 `sudod --op <n> --result <path> --path1 <p> --path2 <p>`，不接受客户端提供的可执行路径 —— 这是刻意区分：`RUN` 允许任意程序（管理员口令已授权），`FILEOP` 只允许固定 worker。

## 6. 客户端与命令

### 6.1 libc 客户端

新增 `userland/libc/include/leonos/sudo.h` 与 `userland/libc/src/sudo_client.c`：

```c
int leonos_sudo_run(const char *username, const char *password,
                    char *const argv[], uint32_t *out_pid);   /* 0 / -errno */
int leonos_sudo_wait(uint32_t child_pid, int *out_status);     /* 0 / -EAGAIN / -errno */
int leonos_sudo_check(void);                                   /* 1 缓存有效 / 0 需口令 */
int leonos_sudo_kill(void);                                    /* 作废自己的缓存 */
int leonos_fileop(uint32_t op, const char *path1, const char *path2,
                  const char *username, const char *password,
                  struct leonos_dir_entry *entries, uint32_t capacity,
                  uint32_t *out_count);                        /* 0 / -errno */
```

`leonos_fileop()` 内部完成：发请求 → `WAIT` → 读结果文件 → 删除结果文件 → 解析目录项。

### 6.2 `sudo`

```
sudo [-u 用户] [-k] [-n] <命令> [参数...]
```

- 无 `-u` 时目标为 `root`。
- `-k`：调用 `leonos_sudo_kill()` 后退出 0，不执行命令。
- `-n`：非交互，缓存无效时直接失败（`sudo: 需要口令`），用于脚本与测试断言。
- 默认流程：`leonos_sudo_check()` 为 1 则口令留空直接执行；否则在 TTY 上用 `getpass` 风格读取（关闭回显），口令只存在栈上，使用后 `volatile` 擦除。
- 退出码 = 子进程退出码；被子进程信号终止时按 `128 + signo` 报告；无法确认结果时打印明确错误并返回 1。
- 三次错误口令后退出 1（不锁定账号，锁定策略不在本轮范围）。

### 6.3 `su`

```
su [-c <命令>] [-] [用户名]
```

- 无用户名或用户名为 `root` 时目标为 root，要求该管理员账号口令。
- 目标是其他账号时：
  - 请求者已是 root → 免密切换（真实 Unix 语义）。
  - 请求者为普通用户 → 要求**目标账号自己的口令**，验证通过后以目标身份起 shell。
- 无 `-c`：以目标身份执行 `/bin/sh` 交互 shell；`-` 额外把 `HOME` 设为目标家目录、`cwd` 切到该目录。
- `-c "cmd"`：以目标身份执行 `/bin/sh -c "cmd"`。
- 目标账号被禁用或不存在 → 拒绝。

### 6.4 `sudod` 特权 worker

独立的 LeonOS app（与 `terminal`/`fileman` 同级），实现四个 op 并输出结果文件：

| op | 行为 | 返回 |
| --- | --- | --- |
| `LIST` | `open` + `leonos_readdir` 枚举目录 | 目录项数组 + count |
| `MKDIR` | `mkdir(path1/path2, 0755)` | 成功后返回父目录新列表 |
| `RENAME` | `rename(path1, path2)` | 成功后返回目标父目录新列表 |
| `UNLINK` | `unlink`；目标是目录时递归删除 | 成功后返回父目录新列表 |

写操作**一并返回新目录内容**，使 Fileman 一次调用即获得"操作结果 + 新状态"，避免中间态与额外往返。

结果文件格式（定长头 + 变长目录项）：

```c
struct sudod_result {
    uint32_t magic;      /* 'SDOR' */
    int32_t  status;     /* 0 或负 errno */
    uint32_t count;      /* 目录项数量，非 LIST 类操作沿用父目录 */
    uint32_t reserved;
    struct leonos_dir_entry entries[LEONOS_FS_MAX_ENTRIES];
};
```

`sudod` 侧同样执行第 5.2 节第 6 条的路径校验（防御性第二道），并以 `O_CREAT | O_EXCL | O_NOFOLLOW` 打开结果文件。

**递归删除的确认要求**：`UNLINK` 的目标是目录时，`path2` 必须等于确认词 `DELETE`，否则 `sudod` 与 authd 均返回 `-EINVAL` 且不做任何删除。`authd` 侧先判、`sudod` 侧再判，两道都不能省 —— 前者防止请求被排队后才失效，后者防止绕过 authd 直接调用 worker。

## 7. Fileman 集成

### 7.1 触发点

`navigate_to_path()`（`userland/apps/fileman/model.c:1422-1445`）与 `reload_dir()`（`:1001-1015`）在 `open(current_path)` 返回 `-EACCES`/`-EPERM` 时进入提权流程：

```text
reload_dir() 失败
  └─ fileman_prompt_elevation(current_path)
       ├─ leonos_ui_show_input_dialog("管理员权限提升", "请输入管理员用户名：")
       ├─ leonos_ui_show_password_dialog("管理员权限提升", "请输入管理员密码：")
       │    （复用 userland/libc/src/admin.c:33-50 的同一对对话框）
       └─ leonos_fileop(FILEOP_LIST, path, NULL, user, pass, ...)
            ├─ 成功 → 用返回目录项重绘；状态栏显示"已提权"
            └─ -EACCES → 状态栏"密码错误"，重新弹框；用户取消则退回"权限被拒绝"
```

**用户名为空口直接回车**时按 `root` 处理，与命令行一致，避免必须手输用户名。

### 7.2 写操作与硬约束

`create_new_folder()`、`rename_selected_entry()`、`delete_selected_entry()`、`permanent_delete_selected_entries()` 在 `current_path` 处于已授权状态时改走 `FILEOP_MKDIR/RENAME/UNLINK`，成功后直接用返回的目录项刷新列表。

> **硬约束（来自宿主实现）**：musl 的 `setuid()` 经 `__setxid` 广播到所有线程，任一线程失败会 **SIGKILL 整个进程**（`third_party/musl/src/unistd/setxid.c:11-34`）。因此**任何客户端进程都不得用 `setuid()` 改变自己的身份**；身份只在 authd 的 `fork` 之后、`exec` 之前设定。

### 7.3 授权窗口与过期

一次成功后，该路径标记为已授权；窗口内后续操作靠 authd 凭据缓存免密通过。缓存过期后再次 `EACCES` 时**重新弹框**，不静默失败。已授权状态不跨 Fileman 进程生命周期保存。

### 7.4 修复 `leonos_admin_elevate()`

`userland/libc/src/admin.c` 改为经新通道验证（`leonos_sudo_check()` 命中则直接返回 1，否则弹框后走 `leonos_sudo_run()` 的校验路径），使 diskmgr 格式化/挂载、apiapp 包安装在普通用户会话下真正生效。`leonos_auth_delegate_elevation()` 保持空实现不变（其调用点 `apiapp/main.c:553` 与本轮无关）。

## 8. 构建、staging 与文档

| 新增物 | 落点 | 接入 |
| --- | --- | --- |
| `sudo` / `su` | `userland/apps/sudo/`、`userland/apps/su/` | app target + `tools/leonos_layout.py` 的 `_PAYLOAD_PATHS` |
| `sudod` | 独立 app | `_PAYLOAD_PATHS` 登记 `/usr/lib/leonos/apps/sudod/`，否则 staging prune 会当残留删除 |
| 客户端库 | `userland/libc/src/sudo_client.c` | 与 `admin.c`/`authd_client.c` 并列 |
| authd 策略单元 | `userland/apps/authd/sudo_policy.c` | 纯函数、可主机编译，供主机单测链接 |
| 命令链接 | `tools/leonos_layout.py:334` `builtin_command_links()` | 与 `tcc`/`lua` 同路 |
| 测试子命令 | `build.py` 的 `test` choice 列表（`:4650-4657`） | 新增 `sudo-policy` 项 |

`devtools/include/`、SDK zip、`include/leonos/` **不改动**。

文档更新：新增 `docs/SUDO_AND_ELEVATION.md`（协议、缓存语义、命令用法、已知限制）；`docs/INSTALLER_ACCOUNTS.md` 增加 sudo 段落；新增 `docs/security/2026-09-11.md` 记录威胁模型；`docs/POSIX_PERMISSIONS_2026-09-08.md:69-72` 的"setuid 位未实现"保持原样（本轮同样不实现）。

## 9. 安全边界与威胁模型

本轮**明确防御**：

1. 客户端无法声称自己的身份 —— 所有身份取自 SO_PEERCRED。
2. 客户端无法伪造免密 —— 凭据缓存只在 authd 侧，客户端仅能查询。
3. 客户端无法借 FILEOP 执行任意程序 —— worker 路径由 authd 固定，不接受客户端提供。
4. 客户端无法借 FILEOP 触碰 `/proc`、`/sys`、`/dev` 与元数据路径 —— authd 与 worker 双重校验。
5. 口令不落盘、不入日志、校验后立即擦除。

本轮**不防御**（需如实记录，不夸大）：

1. **恶意 GUI 应用**。`FILEOP` 的路径参数来自 Fileman，与 authd 属同一信任链；能运行任意 GUI 应用并能诱导用户输入管理员口令的攻击者，等价于已获得管理员授权。真正的隔离需要 sudoers 式的按程序授权模型，不在本轮。
2. **本地 DoS**：持有有效缓存的请求者可反复发起特权操作，无速率限制。
3. **审计**：本轮不记录特权操作审计日志。

## 10. 验证矩阵

每条都有可执行命令与明确断言，禁用"应该能工作"的表述。

### 10.1 主机单元测试（新增）

```sh
python3 tools/test_sudo_policy.py
```

覆盖 `userland/apps/authd/sudo_policy.c` 的纯函数：目标账号解析（空 → root）、账号不存在拒绝、禁用账号拒绝、口令错误、请求者为 UID 0 时切换任意账号免密、请求者非 0 且目标为普通账号时要求目标自身口令、用调用者自己口令冒充他人目标必须拒绝、缓存命中/过期边界（299999 ms 命中、300001 ms 未命中）、`SUDO_KILL` 后失效、口令改写后失效、路径校验（`/proc`、`/sys`、`/dev`、`..`、相对路径、元数据路径全部拒绝）、递归删除确认词（目录 + `path2 != "DELETE"` → `-EINVAL` 且不产生删除）。

### 10.2 回归

```sh
python3 build.py test installer-setup   # 账号与 PBKDF2 参考向量不得被改坏
python3 build.py test oobe              # 认证边界、会话文件语义、reboot 清理
python3 tools/test_security_regressions.py --strict
```

`tools/test_security_regressions.py` 是源码文本审计：`missing_peercred()`（`:51-59`，遍历 `PEERCRED_PATHS`，其中已含 `userland/apps/authd/main.c`（`:29-33`））断言 SO_PEERCRED 仍是信任边界，`missing_root_gates()`（`:61-66`）grep 内核 setuid 与 reboot 的 uid==0 门禁字面量。内核零改动意味着原有两条 grep 继续成立。

该脚本是 grep 架构，所以新增断言必须**可 grep**。为满足这一点，规格要求：

1. authd 侧每个新消息的授权判定必须收敛到命名以 `_from_peer` 结尾的函数；审计脚本新增断言：`userland/apps/authd/main.c` 中每个 `LEONOS_AUTHD_MSG_RUN`/`WAIT`/`SUDO_KILL`/`SUDO_CHECK`/`FILEOP` 的 dispatch 分支都必须调用一个 `*_from_peer(` 函数。
2. 审计脚本新增断言：`*_from_peer` 函数体内必须出现 `clients[slot].uid`（即身份取自 accept 时记录的 SO_PEERCRED，而非请求体）。

### 10.3 构建与 staging

```sh
python3 build.py build
ls build/install/root/usr/bin/{sudo,su}
ls build/install/root/usr/lib/leonos/apps/sudod/sudod.elf
```

### 10.4 QEMU 实机闭环（唯一能证明提权真实生效的证据）

```sh
python3 tools/test_sudo_qemu.py --output build/sudo-e2e
```

以安装后的磁盘启动，用普通账号 `alice`（UID 1000）登录，在终端逐条执行并断言串口输出：

| 步骤 | 断言 |
| --- | --- |
| `id` | `uid=1000` |
| `sudo id`（正确 root 口令） | `uid=0`，退出码 0 |
| `sudo id`（5 分钟内第二次） | 不再提示口令，`uid=0` |
| `sudo -k && sudo id` | 再次要求口令 |
| `sudo id`（错误口令 ×3） | 拒绝，退出码非 0，串口无口令明文 |
| `sudo -n id`（无缓存） | 立即失败并提示需要口令 |
| `su -c 'id'`（正确 root 口令） | `uid=0` |
| `su -c 'id' alice`（在 root 会话内） | `uid=1000`，免密（请求者是 UID 0） |
| `su -c 'id' root`（在 alice 会话内，正确 root 口令） | `uid=0` |
| `su -c 'id' alice`（在 alice 会话内） | 要求 alice 自己的口令；正确 → `uid=1000`，错误 → 拒绝 |
| `su -c 'id' bob`（在 alice 会话内，给 alice 自己的口令） | 拒绝（不得用他人口令冒充） |
| `su -c 'id'`（普通账号 passwd） | 拒绝（非管理员账号不得提权） |
| 普通用户 `cat /root/secret` | `EACCES`（提权前） |
| Fileman 进入 `/root` | 弹出密码框 → 正确口令 → 列出内容；取消 → 保持"权限被拒绝" |
| Fileman 删除 `/root` 下的目录 | 要求确认词；确认词错误时不做任何删除 |
| diskmgr 在普通用户会话下格式化 | 走新通道成功（回归第 7.4 节修复） |

**主机测试不覆盖**：`fork`/`exec`/SCM_RIGHTS 描述符传递、PTY 下的口令输入回显、Ctrl+C 对子进程的传递（子进程与客户端不同进程组）。这些必须在 10.4 中观察并在 `docs/SUDO_AND_ELEVATION.md` 记录实测结果。

## 11. 已知限制

1. **Ctrl+C 语义未定**：子进程与客户端不在同一进程组，PTY 行规程的信号投递目标需在 10.4 中实测后写进文档，不预设结论。
2. **authd 重启丢失等待状态**：其子进程被重新挂到 init，`WAIT` 返回 `ESRCH`，sudo 如实报错，不谎报成功。
3. **Fileman 不做复制/粘贴提权**：受保护目录下的复制仍报权限错误。
4. **无审计日志、无速率限制**。
5. **单用户缓存**：缓存按请求者 UID 记录，同一 UID 的多个终端共享 300 秒窗口（与 sudo 的 per-user 时间戳一致，非 per-tty）。

## 12. 已定稿的边界决策

以下三项在规格自检中定稿，实现阶段不再反复：

1. **`sudod` 不登记桌面图标**。它是内部特权 worker，不接受用户直接启动；只在 `FILEOP` 路径由 authd 拉起。若登记图标，等于给出一个无参数运行即失败的入口，徒增攻击面与困惑。
2. **递归删除 root 私有目录需要二次确认**。复用 diskmgr 既有的"输入确认词后点击两次应用"模式，`sudod` 的 `UNLINK` 在目标是目录时要求 `path2` 携带确认词。理由：`rm -rf` 语义的不可恢复操作不应只靠一次密码框。
3. **错误提示不区分"账号不是管理员"与"口令错误"**。两者在客户端统一呈现为"认证失败"，因为区分会泄露账号角色信息（可被用于枚举哪些账号是管理员）。代价是用户自查稍难 —— 这是刻意选择的取舍：**信息披露 ≈ 授权信息的泄露**，与"客户端无法声称身份"的信任模型保持一致。脚本可用退出码区分（未授权 `1`、无法确认结果 `1`、子进程退出码原样透传），但文案不区分。
