# LeonOS 4 开发套件文档

这里是随 `devtools` SDK 分发的 API 参考和开发约定。文档描述的是当前
SDK 头文件和静态库已经提供的接口；没有出现在头文件中的功能不能假定为
稳定 ABI。

## 从哪里开始

1. 先阅读 [BUILDING.md](BUILDING.md)，准备交叉工具链并构建最小应用。
2. GUI 应用阅读 [GUI.md](GUI.md) 和 [UI.md](UI.md)。
3. 终端程序、安装包或输入法分别阅读 [PACKAGING.md](PACKAGING.md)、
   [INPUTM.md](INPUTM.md)。
4. 需要直接访问系统能力时查阅 [SYSCALLS.md](SYSCALLS.md) 和
   [LIBRARIES.md](LIBRARIES.md)。
5. 需要设计生命周期、I/O 和安全边界时阅读 [PROGRAMS.md](PROGRAMS.md)。

Picolibc 的运行时接入和镜像内 TinyCC 的限制仍分别记录在 SDK 根目录的
`README-picolibc.md` 和 `README-tcc.md`；它们补充本目录的通用文档。

## SDK 目录

| 路径 | 用途 |
| --- | --- |
| `include/` | Picolibc 兼容头文件、LeonOS 公开头文件以及第三方公开头文件 |
| `lib/leonos.a` | LeonOS 系统调用封装、GUI/UI、文件、网络等公共库 |
| `lib/libc.a` | 与当前 SDK 匹配的 Picolibc 静态库 |
| `lib/libz.a`、`lib/libpng.a` | zlib 和 libpng 静态库 |
| `lib/libstardustui.a` | 可选的 StardustUI C++ 静态库（构建该组件时提供） |
| `linker.ld` | 用户态 ELF 链接布局，入口为 `_start` |
| `examples/` | C、C++ 和 InputM 示例 |
| `docs/` | 本文档 |
| `THIRD_PARTY/` | 第三方许可证和版本信息（SDK ZIP 中） |

SDK 是 freestanding x86_64 用户态环境。它不包含宿主系统的动态链接器，
也不能把宿主机的 libc、图形 API 或系统调用号混入应用。

## 头文件索引

- `leonos/syscall.h`：原始系统调用号和常用封装。
- `leonos/gui.h`：窗口、事件、像素提交、桌面外观和任务栏控制。
- `leonos/ui.h`：主题感知的像素控件、布局、编辑、树、列表和对话框。
- `leonos/inputm.h`：输入法提供者、编辑上下文、组词和提交结果。
- `leonos/fs.h`、`sys/stat.h`、`fcntl.h`：路径、目录、文件和 ACL。
- `leonos/net.h`、`leonos/http.h`：IPv4、DNS、TCP、HTTP 和下载。
- `leonos/mouse.h`：鼠标显示、位置和样式。
- `leonos/pty.h`：伪终端、终端属性和窗口大小。
- `leonos/launch.h`、`leonos/startup.h`：程序启动、文件关联和用户自启动。
- `leonos/api.h`：`.api` 安装包解析、解包和安装。
- `leonos/png.h`、`leonos/text.h`：有界 PNG 解码和 UTF-8 布局。
- `leonos/auth.h`、`leonos/admin.h`：用户、角色和管理员授权。
- `leonos/system.h`、`leonos/i18n.h`：系统信息、性能、时间和语言。

## 版本与错误处理

应用、`include/`、`lib/leonos.a` 和运行中的 LeonOS 版本必须来自同一 SDK
发布。大多数封装直接返回内核或服务返回值；失败时应检查小于零的结果，
不要只依赖宿主机的 `errno` 语义。网络和音频接口另外在结果结构中返回
状态码。所有来自文件、网络、窗口事件的长度都必须在应用侧再次校验。
