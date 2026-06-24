# LeonOS 4

LeonOS 4 is a small UEFI desktop operating-system prototype. It boots through
GRUB using Multiboot2, builds with a Python-generated Ninja graph, and keeps the
tree split into a base kernel (`ntclks`), a kernel middle layer (`osmlayer`),
and Ring-3 userland ELF programs.

## Build

All build commands are intended to run inside WSL:

```sh
python3 tools/gen_ninja.py --out build.ninja
ninja -f build.ninja all image-vmdk
```

Useful targets:

```sh
ninja -f build.ninja menuconfig
ninja -f build.ninja run
ninja -f build.ninja run-debug
```

The main disk artifact is `build/images/leonos4.vmdk`. The raw GPT/FAT32 image
used to create it is `build/images/leonos4.raw`.

## Layout

- `kernel/ntclks`: base kernel, architecture code, drivers, memory, scheduler,
  syscall entry points, ELF loader, and early console.
- `middlelayer/osmlayer`: Rust `no_std` static library for VFS, POSIX/Linux ABI,
  IPC, GUI protocol, and FAT32 scaffolding.
- `userland`: tiny libc, startup code, `init.elf`, `desktop.elf`, and demos.
- `tools`: Kconfig sync, menuconfig, Ninja generation, and image tooling.
- `arch`: architecture reservation modules. Only `x86_64` builds in v1.

## Current v1 scope

The first prototype is a real bootable skeleton, not a complete Linux clone. It
boots, initializes the layered architecture, links C and Rust kernel code, builds
static userland ELF binaries, lays them onto a GPT ESP FAT32 disk, and exposes
the public ABI shape for Linux syscalls, FAT32 paths, IPC, and a Ring-3 desktop
window-server process. Unimplemented Linux calls return `-ENOSYS`.
