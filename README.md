# LeonOS 4
此代码库是**LeonOS 4**项目的源代码仓库，此仓库的保密规定位于[LICENSE](LICENSE)。

## 编译源代码
> 本项目只能在Linux和WSL平台编译
编译命令：
```bash
python3 tools/gen_ninja.py --out build.ninja
ninja -f build.ninja all image-vmdk
```
编译出来的产物位于`build/images/leonos4.vmdk`，可以通过
```
ninja run
```
来通过QEMU+KVM运行操作系统。

## 代码根目录结构
- `arch`：占位用的
- `boot`：Grub配置文件和loader的源代码
- `configs`：配置文件
- `docs`：文档
- `include`：一些公共的C语言头文件
- `kernel`：ntclks源代码
- `middlelayer`：osmlayer源代码
- `system`：一些资源文件
- `tools`：用于源代码生成/编译和配置文件加载等等的工具
- `userland`：用户态程序源代码