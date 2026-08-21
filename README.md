# LeonOS 4
此代码库是 **LeonOS 4** 项目的源代码仓库，此仓库的开源协议位于[LICENSE](LICENSE)。

## 编译源代码
> 本项目只能在 Linux 和 WSL 平台编译

首次克隆必须递归获取第三方子模块：

```bash
git clone --recurse-submodules https://github.com/Leonmmcoset/LeonOS-4.git
cd LeonOS-4
```

Ubuntu 或 WSL 需要安装完整构建依赖：

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential ninja-build meson clang llvm lld \
  grub-efi-amd64-bin grub-pc-bin grub-common xorriso \
  mtools dosfstools e2fsprogs gdisk qemu-utils python3 python3-pil \
  git make bison flex bc pkg-config

# Requires rustup; install it from https://rustup.rs when it is unavailable.
rustup toolchain install stable --profile minimal
rustup default stable
rustup target add x86_64-unknown-none
```

编译命令：

```bash
python3 build.py run image-vmdk
```
编译出来的产物位于`build/images/leonos4.vmdk`，可以通过
```
python3 build.py run run
```
来通过 QEMU + KVM 运行操作系统。

常规 VMDK 使用 GPT 双分区：FAT32 ESP 存放 UEFI/GRUB、loader、kernel 和
middlelayer，ext2 分区作为运行时 `/` 根目录。FAT32 仍用于 ESP、安装器
ramdisk、旧镜像兼容和可移动介质；构建 ext2 镜像需要 `e2fsprogs` 提供的
`mke2fs`。

`build.py` 是唯一受支持的构建入口，不再兼容 Ninja。常用命令包括：

```bash
python3 build.py help
python3 build.py run all
python3 build.py gen build/userland/browser.elf
python3 build.py why build/userland/browser.elf
python3 build.py affected userland/apps/browser/navigation.c
python3 build.py profile all
python3 build.py cache stats
python3 build.py why kernel --json
python3 build.py test all
python3 build.py client run image-vmdk
python3 build.py -v run image-vmdk
python3 build.py --theme=linux run image-vmdk
python3 build.py run image-vmdk --theme=meson
python3 build.py --theme=cargo run image-vmdk
python3 build.py status <九位任务ID>
```

构建产物统一位于`build/`；构建核心、依赖缓存、配置、任务状态与日志位于
`buildsystem/`。通过`python3 build.py settings`编辑并行设置；系统 Kconfig 配置继续使用
`python3 build.py run menuconfig`。查询命令默认输出可读文本；传入`--json`
（可置于命令前后）才输出机器可读 JSON。
`-v` 或 `--verbose` 同样可置于命令前后；它会展开构建图、缓存命中/失效原因、
每个目标的输入输出、实际命令、工作目录、显式环境覆盖、子进程输出和 action 处理细节。
后台任务使用 `python3 build.py client -v run image-vmdk`，详细内容会写入该任务的日志。
构建日志默认使用 LeonOS 原有主题；也可用 `--theme=linux`、`--theme=meson` 或
`--theme=cargo` 切换为 Linux Kbuild、Meson/Ninja 或 Cargo 风格。主题会传递给后台
`client` worker，日志文件始终保存为不含 ANSI 控制序列的纯文本。
非默认主题不会追加 LeonOS 自定义的 `Result:` 汇总块，以保持对应上游工具的原生收尾格式。

版本元数据头文件保留在`include/generated/build_info.h`，`python3 build.py run clean`
不会删除它。每次 OS 构建、生成或 profile 任务都会递增构建号；清理、配置和纯主机测试不递增。

## 界面样式

系统默认使用蓝色、直角、平面化的 Metro 样式。管理员可在“设置 → 显示”中切换为完整保留的 Win95 样式；选择会立即应用到 Desktop 和已打开程序，并保存到 `/system/config/display.conf` 供下次启动的登录、OOBE、安装器与内核早期画面使用。

## 代码与目录结构

根目录中的主要源码、构建输入和工具按职责组织如下：

- `arch/`：各架构相关说明和预留代码（当前主要支持 x86_64）。
- `boot/`：GRUB 配置、启动汇编和早期 loader 源代码。
- `build.py`：唯一受支持的构建入口。
- `buildsystem/`：构建图、缓存、依赖、任务状态、日志和本机设置实现。
- `configs/`：组件清单、默认配置和可提交的构建 profile。
- `devtools/`：面向应用开发的 SDK 头文件、库、链接脚本、示例和文档。
- `docs/`：架构、ABI、构建、文件系统、安全和工具文档。
- `drivers/`：可加载的 Ring-0 驱动及其构建输入。
- `include/`：内核、中间层和用户态共用的公共 C 头文件；生成头文件位于 `include/generated/`。
- `kernel/ntclks/`：LeonOS 内核，包括调度、内存、ELF、系统调用、GUI IPC、网络和存储桥接。
- `los2w/`：宿主机上的 LeonOS/Windows 兼容工具和模拟器代码。
- `middlelayer/osmlayer/`：Rust + C 中间层，负责 VFS、账户与 ACL、Unicode、设备和挂载策略。
- `system/`：镜像中 staging 的系统配置、字体、证书、壁纸、图标和其他资源。
- `test/`：测试输入和测试资源。
- `third_party/`：通过 Git submodule 引入的上游或分叉项目源码，具体归属见 `.gitmodules`。
- `tools/`：构建辅助、资源生成、组件同步、镜像/安装器打包、验证、Doxygen 文档、启动日志分析、许可证归属检查和代码统计工具（包括 `count_code.py`、`analyze_boot_log.py` 与 `check_licenses.py`）。
- `userland/`：用户态运行库、窗口/UI 支持、BusyBox、TCC、Lua、Nano、Fastfetch 及桌面应用源码。

`Kconfig` 和 `Kconfig.components` 定义配置菜单；后者由工具根据组件清单生成，
不应手工维护。`build/`、`dist/`、`buildsystem/logs/`、`buildsystem/tmp/` 等目录
由构建或发布流程生成，不是手写源码的权威来源。

## 代码注释规范

内核 `kernel/ntclks/` 与中间层 `middlelayer/osmlayer/` 的每个函数定义和公共函数
声明都必须使用 Doxygen 风格注释。C、C++、汇编预处理源和 Rust 均采用以下块注释形式，
以便 Doxygen 和 Rust 文档工具都可读取：

```c
/**
 * @brief 简要说明函数负责的行为、边界和可观察效果。
 * @param request 输入请求；说明所有权、可空性和缓冲区容量（如适用）。
 * @param out_result 输出结构；调用方提供有效可写空间。
 * @return 0 表示成功，负 errno 表示失败。
 */
int subsystem_handle(const struct request *request, struct result *out_result);
```

- `@brief` 必须描述职责，不能只把函数名改写为一句话。涉及权限、用户指针、硬件、
  锁、引用计数、映射或中断上下文时，简要说明关键前置条件或副作用。
- 每个参数使用 `@param`。明确输入/输出、可空性、所有权转移、字节长度与数组容量；
  无参数函数不写空的 `@param`。
- 非 `void` 函数使用 `@return`，说明成功结果及错误值/特殊值。不会返回的函数标明
  不返回的原因；异步接口还应说明完成或回调语义。
- 注释紧贴其声明或定义。静态私有函数至少在定义处有注释；公共函数在头文件声明处
  和实现处保持一致，不要让两处描述相互矛盾。
- 结构、宏、全局状态与复杂算法仍应保留必要的独立注释；函数 Doxygen 注释不能替代
  ABI、并发、内存安全和错误处理说明。

## 作者注

源代码里还包含神秘的许可证服务端和客户端完整代码，但是这个项目是 Apache 2.0 开源协议且我也打算放弃许可证机制所以就作为纪念保留吧。
