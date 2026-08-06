# LeonOS BuildSystem

LeonOS 使用根目录的 `build.py` 作为唯一构建入口，运行环境为 Linux 或 WSL。
构建产物写入根目录的 `build/`；构建核心、配置、缓存、任务状态和日志写入
`buildsystem/`。版本元数据头文件 `include/generated/build_info.h` 保留在其原有
位置，`run clean` 不会删除它。不会再生成或读取 Ninja 文件。
每次执行 OS 构建、生成或 profile 任务都会递增构建号；清理、配置和纯主机测试不递增。

## 任务

- `run all`：构建完整 ESP staging。
- `run kernel`、`run loader`、`run drivers`、`run middlelayer`：分别构建内核、loader、驱动和中间层。
- `run userland`：构建 libc、mbedTLS、用户程序和图标。
- `run image-vmdk`、`run image-iso`：分别生成 VMDK 与普通 ISO。
- `run run`、`run run-debug`、`run run-iso`：启动 QEMU。
- `run installer`：生成安装器 ISO；`run clean` 清理可再生产物。

## 查询与后台执行

每次命令都会分配九位任务 ID。`client` 将同一 ID 交给后台 worker，使用
`status` 查询状态、`log` 以只读 Vim 打开无 ANSI 控制符的日志。`settings` 和
`map` 是 ncurses TUI，必须在交互式终端运行。

- `why <任务/产物>`：说明目标为何需要重建，包括缺失产物、指纹变化、显式/隐式输入和 `.d` 头文件依赖更新。
- `affected <文件>`：列出直接引用该文件的目标，以及所有下游受影响目标；已构建的 `.d` 头文件也会参与查询。
- `profile <任务>`：执行任务并输出最慢目标、已执行/跳过目标数、并行利用率和图构建耗时。
- `cache stats`：显示目标状态、对象、depfile、临时目录和依赖缓存的文件数与体积；`cache prune` 仅删除失效状态和临时项，不删除构建产物或任务历史。
- 查询结果默认以可读文本显示；加上 `--json`（可放在命令前后）才输出机器可读 JSON，例如 `python3 build.py affected kernel/ntclks/version.c --json`。
- `-v` / `--verbose`（同样可放在命令前后）开启完整诊断日志：解析到的目标闭包、调度、每个目标的输入/输出/依赖、缓存命中或每条重建原因、实际命令、工作目录、显式环境覆盖、进程 PID/退出状态、子进程输出及 Python action 的文件处理。默认模式保持简洁。后台使用 `client -v ...` 时，verbose 状态会传递给 worker 并写入任务日志。

## 增量规则

构建器追踪显式依赖、GCC depfile、命令指纹和源 glob。缺失产物、依赖更新或命令
变化才重新执行目标。目标状态汇总为单一索引，路径与 mtime 在一次调用中缓存；只读命令不再构造构建图。下载任务使用 1、2、4 秒退避重试；其他失败会立即停止下游目标。
