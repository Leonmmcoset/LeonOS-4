# 系统调用 ABI

## 进入方式

LeonOS 4 x86_64 用户态通过 `int $0x80` 进入内核。SDK 中的
`syscall0`、`syscall1`、`syscall2`、`syscall3` 和 `syscall6` 已完成寄存器
转换：

```text
输入函数参数: rdi=n, rsi=a0, rdx=a1, rcx=a2, r8=a3, r9=a4, 栈上的第 7 个参数=a5
内核入口:     rax=n, rdi=a0, rsi=a1, rdx=a2, r10=a3, r8=a4, r9=a5
返回值:       rax
```

应用通常不需要直接调用 `int $0x80`，应包含 `<leonos/syscall.h>` 并使用
封装函数。系统调用号是公开 ABI 的一部分，但尚未实现的号不能自行假设。

## 当前公开调用号

| 名称 | 号 | SDK 封装 |
| --- | ---: | --- |
| `read` | 0 | `read` |
| `write` | 1 | `write` |
| `open` | 2 | `open` |
| `close` | 3 | `close` |
| `pipe` | 22 | `pipe` (anonymous 4 KiB pipe, returns read/write fds) |
| `stat` | 4 | `stat` |
| `fstat` | 5 | `fstat` |
| `lseek` | 8 | `lseek` |
| `mmap` | 9 | `mmap` |
| `munmap` | 11 | `munmap` |
| `ioctl` | 16 | `ioctl` |
| `sched_yield` | 24 | `sched_yield` |
| `nanosleep` | 35 | `sleep_ms` |
| `getpid` | 39 | `getpid` |
| `fork` | 57 | `fork` |
| `vfork` | 58 | `vfork` (currently COW `fork` equivalent) |
| `execve` | 59 | `execve` |
| `exit` | 60 | `exit`、`_exit` |
| `wait4` | 61 | `wait4` |
| `kill` | 62 | `kill`（有限信号终止语义） |
| `getppid` | 110 | `getppid` |
| `setpgid` | 109 | `setpgid` |
| `getpgrp` | 111 | `getpgrp` |
| `setsid` | 112 | `setsid` |
| `getpgid` | 121 | `getpgid` |
| `nice` | 34 | `nice` |
| `getpriority` | 140 | `getpriority` |
| `setpriority` | 141 | `setpriority` |
| `getrlimit` | 97 | `getrlimit` (`RLIMIT_NOFILE`/`RLIMIT_AS`) |
| `setrlimit` | 160 | `setrlimit` (`RLIMIT_NOFILE`/`RLIMIT_AS`) |
| `getcwd` | 79 | `getcwd` |
| `chdir` | 80 | `chdir` |
| `rename` | 82 | `rename` |
| `mkdir` | 83 | `mkdir` |
| `rmdir` | 84 | `rmdir` |
| `unlink` | 87 | `unlink` |

## 文件与路径

路径使用 LeonOS 驱动器格式，例如 `0:/users/admin/file.txt`。文件打开标志
和偏移常量来自 `<leonos/fs.h>` / `<fcntl.h>`：`O_RDONLY`、`O_WRONLY`、
`O_RDWR`、`O_CREAT`、`O_TRUNC`、`O_APPEND` 以及 `SEEK_SET/CUR/END`。
`stat.type` 使用 `LEONOS_FS_TYPE_FILE`、`LEONOS_FS_TYPE_DIR` 或
`LEONOS_FS_TYPE_DEVICE`。

`read` 和 `write` 可能返回短读或短写。对普通文件使用循环，直到完成、到达
EOF 或返回负值。目录通过 `leonos_list_dir()` 或 `leonos_readdir()` 访问，
不是把目录当作普通字节流解析。

## 进程语义

`fork()` 创建当前任务的写时复制地址空间副本，父进程获得子 PID，子进程返回
零；`vfork()` 暂时采用相同语义。`execve()` 替换当前进程映像且保留 PID、工作目录、
身份、PTY 和未标记 `FD_CLOEXEC` 的描述符。需要等待子任务时使用 `wait4()` 或
`waitpid()`；终止当前任务使用 `exit()`。`waitpid()` 支持 `WNOHANG`、`WUNTRACED`
和 `WCONTINUED`，并接受 PID、零（调用方进程组）或负进程组选择器。

`kill()` 支持同 UID 或管理员发送信号，正 PID 指向单个进程，零指向调用方进程组，
负 PID 指向指定进程组。`SIGHUP`、`SIGINT`、`SIGQUIT`、`SIGKILL`、`SIGTERM` 会终止
目标任务；`SIGSTOP`/`SIGTSTP` 停止任务，`SIGCONT` 恢复它。`setpgid()`、`getpgrp()`、
`getpgid()` 和 `setsid()` 提供作业控制基础；PTY 的 `TIOCGPGRP`/`TIOCSPGRP` 设置前台
组，`Ctrl+C` 与 `Ctrl+Z` 分别向该组投递 `SIGINT` 和 `SIGTSTP`。用户态自定义信号
处理器和信号掩码仍是兼容性 stub。

`pipe()` 创建容量为 4 KiB 的匿名管道，`dup2()` 可把管道或普通文件绑定到标准流。
管道在无数据时返回 `EAGAIN`，写端没有读者时返回 `EPIPE`；libc 会短暂让出 CPU
后重试。Hush 已支持普通流水线、重定向、后台任务与 `jobs`/`fg`/`bg`。

## 内存映射

```c
void *p = mmap(0, size, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
```

支持匿名私有映射以及受限的只读文件映射。成功返回用户地址，失败返回
`LEONOS_MAP_FAILED`；长度应为正数并检查溢出。`MAP_FIXED` 只有在调用者已经
保留并验证目标区间时使用。映射释放用 `munmap`，不要把 `free` 和 `munmap`
混用。用户地址空间、VMA 数量和单进程内存仍受内核资源限制，应用应按需分配。

## 错误处理

封装函数一般直接返回内核结果：非负值表示成功或传输字节数，负值表示失败。
不要把一个负返回值当作合法文件描述符或窗口 ID。`ioctl` 的请求结构体必须
使用对应头文件定义的大小和布局，未知请求会失败。
