# LeonOS 4
此代码库是 **LeonOS 4** 项目的源代码仓库，此仓库的保密规定位于[LICENSE](LICENSE)。

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
  mtools dosfstools gdisk qemu-utils python3 python3-pil \
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
python3 build.py status <九位任务ID>
```

构建产物统一位于`build/`；构建核心、依赖缓存、配置、任务状态与日志位于
`buildsystem/`。通过`python3 build.py settings`编辑并行设置；系统 Kconfig 配置继续使用
`python3 build.py run menuconfig`。查询命令默认输出可读文本；传入`--json`
（可置于命令前后）才输出机器可读 JSON。
`-v` 或 `--verbose` 同样可置于命令前后；它会展开构建图、缓存命中/失效原因、
每个目标的输入输出、实际命令、工作目录、显式环境覆盖、子进程输出和 action 处理细节。
后台任务使用 `python3 build.py client -v run image-vmdk`，详细内容会写入该任务的日志。

版本元数据头文件保留在`include/generated/build_info.h`，`python3 build.py run clean`
不会删除它。每次 OS 构建、生成或 profile 任务都会递增构建号；清理、配置和纯主机测试不递增。

## 界面样式

系统默认使用蓝色、直角、平面化的 Metro 样式。管理员可在“设置 → 显示”中切换为完整保留的 Win95 样式；选择会立即应用到 Desktop 和已打开程序，并保存到 `0:/system/config/display.conf` 供下次启动的登录、OOBE、安装器与内核早期画面使用。

## 代码根目录结构
- `arch`：占位用的
- `boot`：Grub 配置文件和 loader 的源代码
- `configs`：配置文件
- `docs`：文档
- `include`：一些公共的 C 语言 头文件
- `kernel`：ntclks 源代码
- `middlelayer`：osmlayer 源代码
- `system`：一些资源文件
- `tools`：用于源代码生成/编译和配置文件加载等等的工具
- `userland`：用户态程序源代码
