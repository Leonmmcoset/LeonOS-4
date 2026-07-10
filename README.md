# LeonOS 4
此代码库是 **LeonOS 4** 项目的源代码仓库，此仓库的保密规定位于[LICENSE](LICENSE)。

## 编译源代码
> 本项目只能在 Linux 和 WSL 平台编译
编译命令：
```bash
python3 tools/gen_ninja.py --out build.ninja
ninja -f build.ninja all image-vmdk
```
编译出来的产物位于`build/images/leonos4.vmdk`，可以通过
```
ninja run
```
来通过 QEMU + KVM 运行操作系统。

## 界面样式

系统默认使用蓝色、直角、平面化的 Metro 样式。管理员可在“设置 → 显示”中切换为完整保留的 Win95 样式；选择会立即应用到 Desktop 和已打开程序，并保存到 `0:/etc/display.conf` 供下次启动的登录、OOBE、安装器与内核早期画面使用。

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
