# 构建用户态应用

## 工具链和 Makefile

SDK 默认通过 `python3 bin/leonos-musl-cc` 驱动宿主 clang/lld，使用 SDK
内的 musl 头文件与库。无需 x86_64-elf 工具链，不能链接宿主 glibc。
需要 Python 3、clang、lld 和 make；可用 `LEONOS_CC` 指定 clang 路径。

```sh
cd devtools
make APP=examples/helloworld APP_NAME=helloworld
make APP=examples/helloworld APP_NAME=helloworld STATIC=1 BUILD_DIR=build-static
```

`APP` 选择源码目录，`APP_NAME` 选择输出名称，`BUILD_DIR` 选择中间目录。
C 使用 C11，C++ 使用 C++17；所有用户程序遵循 Linux native x86-64 浮点、
栈和 TLS ABI。C++ 的 freestanding 扩展仍不包含异常、RTTI 或宿主标准库。

应用定义普通 `main`。驱动自动加入 musl 的 Scrt1/crt1、crti/crtn，链接
mimalloc、LeonOS 扩展库及 musl。动态程序使用 `/lib/ld-musl-x86_64.so.1`，
依赖 `libleonos.so.2`。STATIC=1 选择 musl 静态 CRT 与 mimalloc.o。

自定义构建系统同样调用 SDK 驱动，不能复用旧 linker.ld、私有 CRT、旧
FILE/errno 或 stat/termios 布局。旧 ABI 程序必须重新编译。

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
/usr/lib/leonos/apps/myapp/myapp.elf
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
