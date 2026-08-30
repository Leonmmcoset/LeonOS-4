# LeonOS 4 设备端 TinyCC

LeonOS 4 镜像提供设备端 C 编译器：

```text
/programs/tcc/tcc.elf
```

在 Terminal 的 BusyBox shell 中，可直接使用 `tcc`：

```sh
tcc -c hello.c -o hello.o
tcc hello.c -o hello.elf
/path/to/hello.elf
```

它自动使用 `/programs/tcc/include/` 中的 Picolibc 与 LeonOS 头文件，以及
`/programs/tcc/lib/` 中的 `crt0.o`、`libleonos.a`、`libpicolibc.a`、
`libz.a`、`libpng.a`、`libleonos-tcc-rt.a` 和 `libtcc1.a`，输出普通的静态
x86_64 LeonOS ELF 文件。`libpicolibc.a` 与 Picolibc 头文件保持上游内容；
LeonOS ABI 预定义宏由 TCC 的目标定义层提供，不再改写 Picolibc 私有头文件。

PNG 程序可显式链接这两个库：

```sh
tcc viewer.c -lpng -lz -o viewer.elf
```

当前不支持动态链接、共享库、PIE 或 `tcc -run`。路径应使用 Unix 风格根目录，
例如 `/programs/demo/main.c`。如果要在 `C_INCLUDE_PATH`、`CPATH` 或
`LIBRARY_PATH` 中指定多个目录，请用分号（`;`）分隔；路径本身不带卷名前缀。

生成程序暂不具备 LeonOS 进程计时和完整 POSIX 信号 ABI，因此 `times()` 返回
`ENOSYS`；`signal()` 支持 `SIG_DFL`/`SIG_IGN`，但用户回调仍返回 `SIG_ERR`。

本 SDK 的 `Makefile` 仍面向宿主交叉工具链；它与设备端 TinyCC 使用相同的
Picolibc/LeonOS ABI，但不是对 TCC 的替代或封装。
