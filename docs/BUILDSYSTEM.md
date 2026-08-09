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
- `run release`：生成 VMDK、普通 ISO、安装器 ISO 和开发 SDK；按 menuconfig
  中的发布开关写入 `build/release/SHA256SUMS.txt` 及第三方声明副本。

## 动态配置与 Profile

`configs/components.toml` 是程序、工具和第三方组件的构建清单。
`run menuconfig` 会先生成 `Kconfig.components`，菜单中的组件开关随后同时
控制编译、镜像 staging 和桌面入口。依赖由清单自动启用；关闭组件时只清理
清单登记的输出路径。构建器会拒绝未知 `CONFIG_*` 覆写、重复组件符号、循环
依赖和没有对应源码目标的组件。入口关闭会写入系统的桌面入口策略并清理受管
图标/元数据，不影响 API 安装的未知第三方程序。

`Debug`、`Develop`、`Release` 预设默认直接决定优化、调试符号、LTO、二进制
剥离和开发诊断；只有启用 `Override build preset defaults` 后，才可单独覆写这些
高级项。该选择同时传递给内核、用户态、Picolibc 和所有受管第三方程序构建。
SDK 中的 `TCC`、`Lua` 与 StardustUI 示例开关会分别收录其运行时/端口或示例
源码和二进制，公共 ABI 库仍独立由其组件开关控制。
API 产物的镜像路径也由清单声明；带有附属组件的 API（例如 DOOM）由同步阶段
自动补齐附属 API 开关，避免只生成半个安装包。
Menuconfig 只为清单声明了 API 输出路径的组件显示 API 开关。目前可直接生成
API 包的是 `helloworld`、`doom` 和 `oschinpt`；DOOM 的 `doomlauncher` 是内部
附属组件，不单独生成包。没有 API 打包器的旧配置项会在同步时归一化为关闭。

- `python3 build.py config list`：列出 `configs/profiles/*.conf`。
- `python3 build.py config save <name>` / `load <name>`：保存或加载可提交的 profile。
- `python3 build.py config reset`：恢复 `configs/default.conf`。
- `python3 build.py config import <name> <file>` / `export <name> <file>`：导入或导出 profile。
- 构建目标可追加 `--profile <name>`；`--set CONFIG_KEY=VALUE` 仅对本次构建生效，
  不修改 active 配置或 profile。

宿主机并行度、进程上限和下载重试仍由 `build.py settings` 与
`buildsystem/config/settings.toml` 独立管理。

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

## TUI

`python3 build.py tui` 启动独立的 ncurses 构建前端，不会替换或修改现有的
`settings` 和 `map` CUI 命令。

- 首屏是构建仪表盘：左侧选择常用构建目标，回车立即前台构建，`b` 放入后台；
  右侧显示任务 ID、完成目标数、百分比、当前运行目标和任务状态。
- `o` 打开实时监控，持续尾随任务日志；前台构建输出也会实时进入活动面板。
  前台命令会自动打开输出页并默认尾随最新输出；手动上翻会暂停尾随，按 `G`
  或 `f` 可回到底部并恢复。构建进程退出后仍可从任务历史重新打开状态和完整日志。
- `e` 进入完整目标探索器，支持筛选构建图、profile、目标信息、重建原因和生成操作。
- 内置依赖图、测试选择、缓存统计与确认后的清理、调度器设置编辑、任务历史、
  状态/日志查看，以及实时显示前台 `build.py` 命令输出的面板。
- 按 `:` 打开命令面板，可输入正常的 `run`、`gen`、`test`、`profile`、`info`、
  `why`、`affected`、`cache`、`client`、`status` 和 `log` 命令。
- `run menuconfig` 会暂时恢复终端，把交互交给 Kconfig；返回后 TUI 会继续运行。
