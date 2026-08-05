# LeonOS 4 设备端 TinyCC

LeonOS 4 镜像提供设备端 C 编译器：

```text
0:/programs/tcc/tcc.elf
```

在 Terminal 的 BusyBox shell 中，可直接使用 `tcc`：

```sh
tcc -c hello.c -o hello.o
tcc hello.c -o hello.elf
0:/path/to/hello.elf
```

它自动使用 `0:/programs/tcc/include/` 中的 Picolibc 与 LeonOS 头文件，以及
`0:/programs/tcc/lib/` 中的 `crt0.o`、`libleonos.a`、`libpicolibc.a` 和
`libtcc1.a`，输出普通的静态 x86_64 LeonOS ELF 文件。

当前不支持动态链接、共享库、PIE 或 `tcc -run`。路径应使用 LeonOS 驱动器格式，
例如 `0:/programs/demo/main.c`。如果要在 `C_INCLUDE_PATH`、`CPATH` 或
`LIBRARY_PATH` 中指定多个目录，请用分号（`;`）分隔；冒号属于 `0:/` 路径前缀。

本 SDK 的 `Makefile` 仍面向宿主交叉工具链；它与设备端 TinyCC 使用相同的
Picolibc/LeonOS ABI，但不是对 TCC 的替代或封装。
