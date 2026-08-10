# LeonOS 4 Agent Guide

本文件是 LeonOS 4 仓库内所有自动化代理、维护者和贡献者的工作约定。它
描述当前架构、构建与验证边界，以及本项目维护者偏好的协作方式。除非用户的
明确指令与本文件冲突，否则应遵守本文件。

## 1. 总则：先检查、再修改、再证明

- 先检查真实工作区、相关源码、构建图和 `git diff`，再判断问题或实施修改。
  不要根据目录名、历史印象或“应该如此”推断已交付功能。
- 任何结论都要区分四个层次：**源码已检查**、**已修改**、**已编译/已打包**、
  **已在目标虚拟机运行验证**。不能把前一层说成后一层。
- 发现工作区已有未提交修改时，默认把它们视为用户工作：保留、绕开、不要
  重置、覆盖、批量格式化或还原。先用 `git status --short` 和定向
  `git diff` 确定自己的改动范围。
- 不要为了排查而随意结束用户的构建、QEMU、VMware、WSL、TUI 或其他进程。
  先检查进程、任务状态和日志；只有用户明确要求或确认目标后才停止进程。
- 变更应保持在请求范围内。发现相邻的明确缺陷可一并修复，但应说明其与
  当前问题的关系；不要未经授权引入大范围重构、替代工具链或可选功能。
- 涉及数据删除、镜像重建覆盖、磁盘格式化、清理用户文件、Git 强制操作或
  远程推送前，必须核实精确目标和用户授权。禁止 `git reset --hard`、
  `git checkout --` 这类会丢弃用户改动的操作，除非用户明确要求。

## 2. 项目定位与运行时架构

LeonOS 4 是面向 x86_64、UEFI 启动的操作系统项目。正常系统使用 FAT32
根文件系统，应用运行在 Ring 3，内核和可加载驱动运行在 Ring 0。主要启动
与服务链如下：

```text
UEFI/GRUB
  -> boot/ loader.elf
  -> kernel/ntclks (kernel.sys, Ring 0)
  -> middlelayer/osmlayer (middlelayer.sys)
  -> userland init.elf
  -> desktop.elf（窗口服务器）
  -> 登录 / OOBE / 服务 / 普通桌面应用
```

安装器 ISO 是另一条启动路径：顶层 ISO 只包含启动所需的 loader、内核、
中间层和 installer root；真正安装到磁盘的常规 ESP 内容作为
`0:/install/esp` 载荷保存在 installer root 中。不要把“安装器运行时镜像
内容”和“安装后系统内容”混为一谈。

### 根目录职责

| 路径 | 职责 |
| --- | --- |
| `boot/` | UEFI loader、GRUB 配置、早期显示和完整性装载。 |
| `kernel/ntclks/` | 内核：调度、内存、ELF 进程、syscall、GUI IPC、网络、驱动管理、存储桥接。 |
| `middlelayer/osmlayer/` | Rust + C 的中间层：VFS、账户和 ACL 策略、Unicode、设备服务、挂载策略。 |
| `drivers/` | 可加载 Ring-0 驱动模块及其打包输入。 |
| `userland/libc/` | LeonOS libc、syscall 包装、UI/字体、网络/HTTP/TLS、PTY 等公共实现。 |
| `userland/apps/` | Ring-3 系统与桌面应用；`desktop/` 是窗口服务器，其他应用为它的客户端。 |
| `userland/{picolibc,busybox,tcc,lua,nano,file,cmd,stardustui}/` | 第三方软件的 LeonOS 端口、适配层与构建输入。 |
| `include/leonos/` | 公共 C ABI 头文件；修改公开 ABI 时优先检查这里。 |
| `system/` | 被 staging 的系统配置、字体、壁纸、证书、图标、应用资源和默认内容。 |
| `configs/` | 动态组件清单、可提交 build profile 与默认配置。 |
| `tools/` | 构建、Kconfig 同步、镜像、安装器、SDK、资源生成和验证脚本。 |
| `buildsystem/` | `build.py` 的实现、任务状态、缓存、日志、本机设置及依赖。 |
| `devtools/` | 开发 SDK 输入（公共头文件、库、示例、文档、链接脚本）。 |
| `docs/` | 架构、ABI、构建、文件系统、驱动、安全等项目文档。 |
| `third_party/` | 通过 Git submodule 引入的上游源码；见 `.gitmodules`。 |
| `build/` | 可再生产物与 staging 输出；不可作为手写源码的唯一来源。 |

### 特权边界

- Ring-3 程序经 Linux 编号的 x86_64 syscall ABI 进入内核，入口目前为
  `int $0x80`；参数使用 `rax/rdi/rsi/rdx/r10/r8/r9`，负返回值为
  `-errno`。已实现接口才可视为可用，未知 syscall 返回 `-ENOSYS`。
- 内核负责用户指针与长度验证、页表/进程资源、硬件和最终授权。中间层不应
  直接信任用户指针或直接访问硬件。
- `osmlayer_bridge` 将内核与中间层连接。账户、ACL、Unicode、部分 VFS 和
  挂载策略跨越这一边界；修改双方结构或 callback 时必须同步版本、边界校验
  和文档。
- GUI 客户端与 `desktop.elf` 通过 GUI IPC/ioctl 通信，而不是共享窗口服务器
  的私有像素内存。应用提交自己的缓冲内容；不要把窗口服务器内部 buffer
  当作公共 ABI。
- 路径使用 LeonOS 盘符格式，例如 `0:/system/apps/desktop/desktop.elf`。
  相对路径依赖任务当前目录，不能假定 Unix `/` 或 Windows 盘符可用。

## 3. 公共 ABI、库和 SDK 的联动规则

任何新 syscall、ioctl、公共结构、窗口/鼠标/网络/输入法 API 或 UI 库接口，
都要逐项检查以下闭环，不能只修改一处：

1. `include/leonos/*.h`：公共定义、常量、结构布局、权限语义和返回值。
2. `kernel/ntclks/`：编号、用户范围检查、权限检查、实现和错误路径。
3. `userland/libc/include/` 与 `userland/libc/src/`：声明、包装和实现。
4. 使用该 API 的系统应用、窗口服务器及相关测试程序。
5. `devtools/include/`：SDK 内对应公共头文件必须同步。
6. SDK 库、示例、文档和 `LeonOS4-Developer-SDK.zip` 的打包规则。
7. `docs/ABI.md`、`docs/SYSCALLS.md` 或对应专题文档。

公开结构应采用定宽类型，校验用户提供的指针、容量、长度、枚举值和版本。
不要在 ABI 中泄漏内核指针、窗口服务器私有地址或未经界定的可变对象。扩展
现有结构时，要考虑旧调用方的大小与兼容行为。

### libc 与第三方移植

- LeonOS libc 与 Picolibc 共同构成用户态 C 环境；不要把“成功链接”误称为
  “已完整移植”。每个移植软件都需要确认其真实源码、适配层、启动代码、
  libc/Picolibc 依赖、ELF 输出、镜像 staging 和运行路径。
- 新增第三方软件时，除上游源码外还要处理：构建脚本、组件清单、镜像路径、
  launcher/桌面入口（若需要）、许可证和归属、SDK/API 包（若公开）、以及
  对应文档。
- `third_party/` 有 submodule 时，遵守 submodule 工作流：上游或分叉仓库的
  源码变更先在正确仓库提交，再在主仓库更新 gitlink；不要把未初始化或宿主机
  目录误当成可提交的第三方源码。
- 任何 API 安装包都必须同时检查包元数据、归档内容、目标安装路径、部分写入
  和失败清理、启动/自启动授权以及安装后的可运行性。

## 4. UI、桌面、主题、字体和输入原则

UI 修改必须横向检查，而不是只改一个应用。典型关联范围包括：

- `userland/apps/desktop/`：窗口管理、桌面、任务栏、开始菜单、状态栏、
  覆盖层、主题广播、图标和壁纸。
- `userland/libc/src/ui*.c` 及公开 UI 头文件：控件、布局、绘制、文字输入、
  文件选择、窗口协议、主题状态与字体。
- 所有受影响内置应用：窗口尺寸、焦点、键鼠、文本编辑、主题变化事件、
  图标/快捷方式和高 DPI/分辨率边界。
- `system/` 中的字体、壁纸、BMP/PNG/图标等 staging 资产，以及
  `tools/prepare_ui_font.py`、`tools/make_app_icons.py` 等生成步骤。
- 登录、OOBE、Installer 与早期 framebuffer：它们可能使用不同的启动阶段和
  配置读取时机，不能仅以登录后桌面正常就宣布完成。

### 主题与个性化

- 用户个性化数据属于 `0:/users/<name>/appearance.conf`；Metro 与 Win95
  的基础色配置相互独立，不能相互覆盖。
- `0:/system/config/display.conf` 是尚无用户会话时的启动/默认外观，用于早期
  framebuffer、bugcheck、登录、OOBE 和安装器等场景。它不能替代每用户配置。
- 修改个性化设置后应立即经 Desktop 发布状态并让已打开应用收到主题变化；
  不要只写文件、等下次启动才生效。
- 现有壁纸 BMP 处理有安全上限：最大 1280 x 720，仅接受受支持的未压缩
  24-bit/32-bit BMP。任何格式、尺寸或缩放策略扩展都需检查解码器、内存占用、
  绘制与错误显示。
- TTF/字体加载、壁纸加载、目录扫描和图标读取不得长期阻塞 Desktop 主循环。
  需要分段、异步或有界处理时，保持 UI 可响应并保留明确的失败状态。

### 输入、窗口与可访问性

- 新输入、键盘快捷键、鼠标模式或终端行为要同时验证：事件消费顺序、焦点、
  修饰键释放、文本提交、预编辑文本、应用快捷键与窗口服务器全局快捷键。
- 密码或其他安全输入字段必须明确禁止第三方输入法；不得为了“兼容”把敏感
  文本交给扩展提供者。
- 控件应依赖真实 UI 控件/状态，而不是仅通过绘制模拟的伪控件来承载交互。
  新控件需处理尺寸不足、键盘导航、鼠标命中和主题变化。
- 窗口控制按钮、任务栏、状态栏、候选框和上下文菜单必须在默认窗口尺寸和
  边缘尺寸下不重叠。对不支持缩放的窗口，应按窗口能力隐藏而非仅灰化放大按钮。

## 5. 存储、网络与长期操作

### 存储与响应性

- 任何 FAT32 写入、API 解包、词库下载、游戏资源安装或大文件复制必须正确
  处理短读、短写、临时失败、重试边界、最终错误和资源关闭。`ret == 0` 不等同
  于完整写入成功；以实际传输字节数判断。
- 不能在 Desktop、窗口绘制、输入事件或持锁的热路径中执行长时间同步磁盘
  操作。将重操作放入 worker/服务路径，并持续让出调度机会；UI 只显示进度和
  可恢复的完成/失败状态。
- 概率性“权限拒绝、资源缺失、图标/字体/壁纸偶尔不加载”等问题，应优先从
  共享的存储、目录枚举、缓存、并发和状态提交路径排查，而不是分别添加 fallback。
- 对写入关键状态（账户创建完成标记、安装状态、配置）使用可诊断的提交顺序，
  保证重启后不会产生“主体成功、完成标记失败”的矛盾状态。

### 网络与 TLS

- 网络栈当前主要服务 e1000；QEMU/VMware 的网卡、DHCP、DNS、TCP 和显示/
  驱动行为并不完全相同。报告时必须说明实际运行平台。
- TLS 故障必须区分 DNS、TCP 连接、握手、证书链、主机名、时间、发送与读取。
  日志可输出主机名、阶段、返回码、网络状态、证书校验标志和时钟状态；绝不
  输出密码、Cookie、HTTP 请求体、令牌或私密证书内容。
- 不得通过关闭证书、主机名或时钟验证来掩盖问题。`MBEDTLS_SSL_VERIFY_REQUIRED`
  是安全基线；CA 解析的非负“部分成功”结果应结合已解析根数量处理。
- 网络错误 UI 不应把所有 TLS/传输错误都称为“证书验证失败”。保留底层状态，
  使诊断和用户提示能反映实际失败阶段。

## 6. 构建系统和动态 Menuconfig

### 支持的平台与入口

- Linux 或 WSL 是唯一权威的交叉编译和运行验证环境。Windows 本机可用于
  下载、编辑、Git 操作或启动 VMware，但不能把 Windows 构建输出当作最终证明。
- 根目录 `build.py` 是唯一受支持的构建入口。不要直接维护或调用 Ninja 图；
  不要引入平行的手工编译流程。
- 在本工作区从 Windows 调用时，使用 WSL 路径与当前仓库：

  ```powershell
  wsl.exe --cd "/mnt/d/Projects/C/LeonOS 4" -- bash -lc "python3 build.py run <task>"
  ```

  WSL 中若 `rg` 被错误解析到不可执行的 Windows 路径，改用 `git grep` 或
  确认 Linux `PATH` 后再搜索。

### 常用任务

```sh
python3 build.py help
python3 build.py run userland
python3 build.py run kernel
python3 build.py run all
python3 build.py run image-vmdk
python3 build.py run installer
python3 build.py run release
python3 build.py run run
python3 build.py run run-debug
python3 build.py run run-iso
python3 build.py test component-config
python3 build.py test all
python3 build.py -v run image-vmdk
```

- 使用 `python3 build.py why <目标>` 与 `affected <文件>` 判断增量构建范围。
- `-v/--verbose` 会输出图、缓存、命令、工作目录、环境、子进程与 action 细节；
  出现“卡住”或 CI 失败时先用它收集证据。
- `client` 任务要用 `status <九位任务 ID>` 和 `log <九位任务 ID>` 查询。
  长时间无输出不等于失败；例如 VMDK 转换可以耗时数分钟。
- `build.py tui`、`settings`、`map` 是交互式 TUI。发现它们正在运行时，不要
  同时发起会争用同一生成目录/配置状态的构建。

### 配置来源与生成物

- `configs/components.toml` 是可选应用、第三方组件、API 包、SDK 内容和桌面
  入口的**清单真源**。组件记录决定构建、镜像 staging、入口和发行物选择。
- `tools/generate_component_kconfig.py` 根据该清单生成根目录
  `Kconfig.components`；不要手工修改生成文件。根 `Kconfig` 负责核心静态选项
  并 `source` 它。
- `tools/kconfig_sync.py`、`buildsystem/components.py` 和 `build.py` 共同将
  Kconfig 结果解析为构建、staging、API 与 SDK 选择。新增/变更组件时必须检查
  这四层与组件配置测试，避免出现菜单能勾选但 VMDK 不生效的情况。
- 实际构建配置为 `buildsystem/config/leonos.conf`；根 `.config`、
  `include/generated/autoconf*.h`、`include/generated/build_info.h` 等是配置或
  构建过程的生成物。不要直接手改生成头文件来实现产品功能。
- `configs/default.conf` 是可提交默认 profile，`configs/profiles/*.conf` 是
  命名 profile。宿主机并行度、进程上限和下载重试在
  `buildsystem/config/settings.toml` 中管理，不能塞进可提交 profile。

### Profile 与临时覆写

```sh
python3 build.py run menuconfig
python3 build.py config list
python3 build.py config save <name>
python3 build.py config load <name>
python3 build.py config reset
python3 build.py config import <name> <file>
python3 build.py config export <name> <file>
python3 build.py run image-vmdk --profile <name>
python3 build.py run image-vmdk --set CONFIG_KEY=VALUE
```

- 命令行 `--set` 只对当前构建有效且优先级最高；`--profile` 不应修改 active
  config；profile 优先于默认配置。
- Debug、Develop、Release 预设管理优化级别、符号、LTO、strip 和诊断。只有
  开启高级覆写后，才分别调整这些底层选项。
- 组件的 build/image/entry/API/SDK 选择彼此有关联但并非同义：构建表示生成
  产物，image 表示纳入镜像，entry 表示生成桌面入口，API/SDK 表示纳入相应
  发行物。不要仅勾选一个就假设全部发生。
- 关闭组件只能清理其清单显式登记的受管产物和 staging 路径，绝不能广泛删除
  未知用户文件或第三方内容。

### 产物与 staging

- 常规 ESP staging tree 是 `build/esp/`；VMDK 位于
  `build/images/leonos4.vmdk`，普通 ISO 位于 `build/images/leonos4.iso`，
  Installer ISO 位于 `build/images/leonos4-installer.iso`。
- 安装器 root 是 `build/install/root.fat`，常规 ESP 的安装载荷被复制到
  `0:/install/esp`。改变程序是否进入镜像时必须验证常规 VMDK、installer root
  与安装后 payload 的对应行为。
- 每次 OS 构建、生成或 profile 任务都可能更新构建号和
  `include/generated/build_info.h`。不要把这种自动改动误认为用户业务逻辑；
  若只为验证而触发递增，恢复时只能回退自己这次产生的元数据差异，绝不能覆盖
  用户原有的构建号/时间改动。
- `build/` 和大部分 `include/generated/` 内容应由构建系统重建；修改构建图
  时改源脚本、Kconfig 或清单，而不是修补生成输出。

## 7. 验证要求

按风险选择最小但充分的验证，并在交付时逐项报告实际执行过的命令与结果。

| 修改类型 | 至少应验证 | 完成标准 |
| --- | --- | --- |
| 文档/单文件说明 | 定向检查 + `git diff --check` | 内容正确、无格式/空白问题。 |
| 构建脚本/Kconfig/组件清单 | `test component-config`、相关 `build.py run` | 菜单、同步和目标选择真实生效。 |
| 内核/中间层/ABI/libc | 受影响目标编译 + 相关用户态重建 | 编译链接、头文件与调用链一致。 |
| 应用/UI/主题/字体 | 受影响应用、VMDK/ESP 构建 + QEMU | GUI 可见行为、交互和日志被证明。 |
| VMware 专属显示/驱动 | VMware 启动与可见操作 | 不以 QEMU 成功替代 VMware 证明。 |
| Installer 变更 | Installer ISO + 实际安装/启动路径 | installer runtime 与安装后系统均正确。 |
| 网络/TLS | 相关构建 + VM 真实网络请求 | DNS、TCP、握手、验证与 UI 状态可区分。 |
| 安全修复 | 定向回归检查 + 合适的运行证明 | 触发条件被阻断，未用降级掩盖。 |

额外规则：

- 每次源码修改完成后运行 `git diff --check`。
- 编译通过不等于镜像已含文件；镜像已生成不等于虚拟机能启动；日志出现不等于
  UI 可见。分别证明。
- QEMU 的 QMP Unix socket 不要放在 `/mnt/d/...`（WSL DrvFs 不支持绑定）；
  使用 `/tmp/leonos-qmp-<id>.sock`。
- 遇到概率性失败，记录平台、镜像、网络、操作步骤和关键日志，重复定向验证；
  不要以单次成功或失败就宣布根因。
- 新增安全审计报告时，使用简体中文，归档为 `docs/security/YYYY-MM-DD.md`，
  明确静态覆盖范围和未做的 PoC/QEMU 验证，不能虚构修复版本或利用结论。

## 8. 文档、日志和交付质量

- 新功能、公开 API、构建开关、镜像布局或第三方移植发生变化时，更新相关
  `docs/`、`devtools/docs/`、SDK 说明、示例和归属/许可证文本。文档只能描述
  已确认存在的接口；计划中的接口必须明确标为计划。
- 日志应使用稳定前缀（例如 `[ntclks]`、`[desktop.elf]`、`[tls]`），包含足够的
  阶段、返回码和状态来定位问题，但不能泄露令牌、密码、Cookie、私钥或请求体。
- 对用户的最终交付应优先给出结果，然后列出：修改了什么、关键路径、构建/
  打包/运行验证分别是否完成、已知限制和下一步。默认使用简体中文。
- 当用户要求“继续”“开始改”“赶紧改代码”时，在确认工作区与范围后直接实施；
  不要重复给方案代替执行。只有缺少会实质改变范围或安全性的授权时才提问。
- 当用户问“为什么”或要求诊断时，先给源码和日志证据，再给结论；除非用户
  同时要求修复，否则不要擅自修改。

## 9. Git、CI 与发布

- 提交前检查 `git status --short`、`git diff --check`，并只 stage 本任务的文件。
  没有用户明确要求时，不要自行创建分支、提交、push 或创建 PR。
- CI 配置位于 `.github/workflows/`。凡是改变依赖、Kconfig、组件清单、构建
  目标、SDK、Installer、第三方移植或产物布局，都要检查 CI 是否仍能从干净
  checkout 和递归 submodule 状态构建。
- CI 的构建通过仅证明 Linux 自动化路径；不要把它等同于 VMware/QEMU 的图形、
  音频、鼠标、网络或安装交互已经验证。
- 发布任务应同时考虑 VMDK、普通 ISO、Installer ISO、SDK、API 包、校验和与
  第三方归属文件；任何一项是否包含某个组件由当前 profile 与组件清单决定。

## 10. 常用排查顺序

1. `git status --short`，查看是否有用户进行中的工作。
2. `git diff -- <相关路径>`，确定当前改动与请求关系。
3. `rg` 或 `git grep` 找到从 UI/API/构建到 staging 的完整调用链。
4. 用 `build.py why`、`affected` 和 `-v` 证明实际构建路径。
5. 以最小相关目标编译；必要时构建 VMDK/Installer。
6. 在对应的 QEMU 或 VMware 平台进行运行/可视验证。
7. `git diff --check`，并在交付中区分已验证与未验证项。

不要以删除 fallback、增加 sleep、关闭校验、无限增大超时、吞掉错误或硬编码
成功状态来“修复”问题。应找到真实边界、状态机、权限、I/O 或构建/staging
断点，并保留可观察的诊断。
