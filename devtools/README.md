# LeonOS 4 Developer SDK

这个目录是 LeonOS 4 用户态应用开发套件。它可以单独复制到没有
LeonOS 4 源码的开发环境中，用来构建可以由 LeonOS 4 启动的 x86_64
ELF 应用程序。

## 内容

- `include/`: LeonOS 4 用户态 C 标准库兼容头文件和公开系统 API。
- `lib/libc.a`: 与本 SDK 头文件匹配的 freestanding 用户态 C 库。
- `linker.ld`: LeonOS 4 用户态 ELF 的链接布局，入口为 `_start`。
- `examples/helloworld/`: 最小可构建的 HelloWorld 应用。
- `Makefile`: 独立构建入口，不会引用 LeonOS 4 源码目录。

## 前置条件

需要一个可以生成 freestanding x86_64 ELF 的交叉工具链。默认工具名为：

```text
x86_64-elf-gcc
x86_64-elf-ld
```

Makefile 也可使用其他前缀。例如工具名是 `x86_64-unknown-elf-gcc` 时：

```sh
make CROSS=x86_64-unknown-elf-
```

在 Windows 上建议从 WSL、MSYS2 或其他提供 GNU Make 与交叉工具链的
环境执行构建。

## 构建示例

在本目录执行：

```sh
make
```

输出文件为：

```text
build/helloworld.elf
```

清理构建结果：

```sh
make clean
```

## 创建应用

1. 复制 `examples/helloworld` 为你的应用目录，例如 `examples/myapp`。
2. 修改其中的 `main.c`。程序入口是普通的 `int main(void)`；SDK 的
   `libc.a` 会提供 `_start`、系统调用封装和常用 C 库函数。
3. 构建：

```sh
make APP=examples/myapp APP_NAME=myapp
```

结果是 `build/myapp.elf`。将其复制到 LeonOS 分区中的可执行位置，例如：

```text
0:/programs/myapp/myapp.elf
```

可由桌面、文件管理器或 `execve()` 启动。

## API 使用

使用 SDK 携带的头文件，而不是宿主操作系统的 API：

```c
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/gui.h>
#include <leonos/ui.h>
```

`leonos/stdio.h` 提供 `puts()` 和 `printf()`。`leonos/syscall.h` 提供
文件、进程、内存和调度相关接口。`leonos/gui.h` 与 `leonos/ui.h` 提供
窗口、事件与软件绘制接口。文件路径使用 LeonOS 驱动器格式，例如
`0:/programs/myapp/data.txt`。

程序必须是 freestanding：不要依赖宿主系统的动态链接器、POSIX 运行时或
宿主系统的库。请只链接本 SDK 的 `lib/libc.a`，并保留 Makefile 的编译、
链接参数与 `linker.ld`。

## 兼容性

`include/`、`lib/libc.a` 与运行时 LeonOS 版本必须匹配。升级 LeonOS 4
后，请使用同版本发布的 SDK 重新构建应用。SDK 仅面向 x86_64 LeonOS 4
用户态，不可用于内核、驱动或宿主系统程序。
