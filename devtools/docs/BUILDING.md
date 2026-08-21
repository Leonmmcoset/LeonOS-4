# 构建用户态应用

## 工具链和 Makefile

SDK 默认使用生成 freestanding x86_64 ELF 的交叉工具链：

```text
x86_64-elf-gcc
x86_64-elf-g++
x86_64-elf-ld
```

可通过变量覆盖：

```sh
make CROSS=x86_64-unknown-elf-
make CC=clang CXX=clang++ LD=ld.lld
```

常用变量如下：

| 变量 | 默认值 | 说明 |
| --- | --- | --- |
| `APP` | `examples/helloworld` | 源文件目录 |
| `APP_NAME` | `helloworld` | 输出 ELF 名称 |
| `BUILD_DIR` | `build` | 中间文件目录 |
| `USE_STARDUSTUI` | `0` | 为 C++ 应用链接 StardustUI |
| `CROSS` | `x86_64-elf-` | 工具链前缀 |

C 源文件使用 C11，C++ 源文件使用 C++17。Makefile 已固定这些运行时约束：
`-ffreestanding`、`-fno-pie`、`-fno-pic`、`-mno-red-zone`、无栈保护、无
异常和 RTTI（C++）。不要删除这些选项来复用宿主程序的默认 ABI。

## 最小构建

```sh
cd devtools
make APP=examples/helloworld APP_NAME=helloworld
make clean
```

应用入口仍然是普通的 `int main(void)` 或 C 兼容的 `main(int, char **, char **)`。
`libc.a` 提供 `_start`，初始化 Picolibc 后调用 `main`，再通过 `exit` 结束。

一个应用目录可以包含多个 `.c`、`.cpp`、`.cc`、`.cxx` 和 `.S` 文件；Makefile
会将它们全部编译并链接。自定义构建系统应沿用 `devtools/linker.ld`，并按下面
顺序链接：

```text
应用对象 -> 可选库 -> leonos.a -> libpng.a -> libz.a -> libc.a
```

链接器使用 `--gc-sections` 和 4 KiB 最大页对齐，入口符号是 `_start`。

## C++ 和 StardustUI

```sh
make APP=examples/stardusthello APP_NAME=stardusthello USE_STARDUSTUI=1
```

SDK 不提供宿主 C++ 标准库、异常或 RTTI。`main` 必须保留 C 链接名：

```cpp
extern "C" int main(int argc, char **argv, char **envp);
```

StardustUI 的 LeonOS 后端仍然通过 `leonos_gui_*` 窗口提交像素；主题文件可
放在当前用户目录，系统回退路径是 `/etc/stardustui/theme`。

## 构建后的安装

普通 GUI 应用应安装到共享程序目录，例如：

```text
/programs/myapp/myapp.elf
```

需要标准输入输出的程序应同时安装同名 sidecar：

```ini
[app]
terminal=1
```

该标记只影响桌面、文件管理器、运行框、快捷方式和 `leonos_launch_argv()`
等启动路径；直接调用 `execve()` 不会自动创建 Terminal。详见
[PACKAGING.md](PACKAGING.md)。

## 诊断和限制

- 优先使用 SDK 的头文件，不要从宿主机包含 `/usr/include`。
- 所有指针、字符串和长度都位于当前进程的用户地址空间；内核不会替应用
  修复悬空指针。
- 长文件复制请分块读写并在循环中处理短读/短写，避免阻塞窗口事件循环。
- GUI 应用应在事件等待期间让出 CPU；需要定时刷新时使用
  `leonos_gui_wait_app_event()` 和有限超时。
- `fork`、`vfork`、`execve`、匿名管道、进程组和有限的默认信号动作属于当前 SDK
  ABI；`clone`、用户安装的信号处理器和动态 TLS 仍不受支持。
