> 2026-09-08：默认 SDK 已迁移至 musl 1.2.6 + mimalloc 3.5.1。构建以
> `docs/BUILDING.md` 和当前 Makefile 为准，旧 ABI 二进制必须重建。
> TinyCC 已在 QEMU 中编译并执行 musl 测试程序，退出码为 0；完整程序覆盖仍待验证。

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

它自动使用 `/programs/tcc/include/` 中的 musl 与 LeonOS 头文件，以及
`/programs/tcc/lib/` 中的 `crt1.o`、`crti.o`、`crtn.o`、`mimalloc.o`、`libleonos.a`、`libc.a`、
`libz.a`、`libpng.a`、`libleonos-tcc-rt.a` 和 `libtcc1.a`，输出普通的静态
x86_64 LeonOS ELF 文件。`libc.a` 与 musl 头文件保持上游内容；
LeonOS ABI 预定义宏由 TCC 的目标定义层提供，不再改写 musl 私有头文件。

PNG 程序可显式链接这两个库：

```sh
tcc viewer.c -lpng -lz -o viewer.elf
```

当前不支持动态链接、共享库、PIE 或 `tcc -run`。路径应使用 Unix 风格根目录，
例如 `/programs/demo/main.c`。如果要在 `C_INCLUDE_PATH`、`CPATH` 或
`LIBRARY_PATH` 中指定多个目录，请用冒号（`:`）分隔；路径本身不带卷名前缀。

标准函数由 musl 提供，信号处理程序通过原生 Linux 信号帧运行。
内核仍有未实现的调用和语义，具体范围以 Linux ABI 审计清单为准。

本 SDK 的 `Makefile` 仍面向宿主交叉工具链；它与设备端 TinyCC 使用相同的
musl/LeonOS ABI，但不是对 TCC 的替代或封装。
