# x86_64

The only implemented architecture in LeonOS 4 v1.

- Boot: UEFI firmware loads GRUB, GRUB loads `ntclks.elf` through Multiboot2.
- ABI: Linux x86_64 syscall register convention.
- Kernel Rust is compiled with `-C no-redzone=yes`.
