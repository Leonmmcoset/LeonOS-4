#!/usr/bin/env python3
"""Host-side contract checks for the native x86-64 Linux syscall ABI."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def macro(text: str, name: str) -> int:
    match = re.search(rf"^#define {re.escape(name)}\s+(0x[0-9a-fA-F]+|\d+)", text, re.M)
    assert match, f"missing {name}"
    return int(match.group(1), 0)


def test_linux_numbers_and_flags() -> None:
    syscall = read("include/uapi/linux/syscall.h")
    fcntl = read("include/uapi/linux/fcntl.h")
    assert macro(syscall, "__NR_pause") == 34
    assert macro(syscall, "__NR_arch_prctl") == 158
    assert macro(syscall, "__NR_set_tid_address") == 218
    assert macro(syscall, "__NR_exit_group") == 231
    assert macro(syscall, "__NR_gettid") == 186
    assert macro(fcntl, "LINUX_O_NONBLOCK") == 0x800
    assert macro(fcntl, "LINUX_F_DUPFD_CLOEXEC") == 1030


def test_native_syscall_entry_and_stack_protocol() -> None:
    syscall_asm = read("userland/libc/src/syscall.S")
    boot_asm = read("kernel/ntclks/arch/x86_64/boot.S")
    gdt = read("kernel/ntclks/arch/x86_64/gdt.c")
    userland = read("kernel/ntclks/user/userland.c")
    assert "syscall" in syscall_asm and "int $0x80" not in syscall_asm
    assert "x86_64_syscall_entry" in boot_asm
    assert "X86_IA32_EFER" in gdt and "read_msr(X86_IA32_EFER) | 1ULL" in gdt
    assert "AT_PHDR" in userland and "AT_RANDOM" in userland


def test_contract_fixes_are_present() -> None:
    syscall_h = read("kernel/ntclks/include/ntclks/syscall.h")
    process = read("kernel/ntclks/syscall_process.c")
    syscall = read("kernel/ntclks/syscall.c")
    ipc = read("kernel/ntclks/syscall_ipc.c")
    assert "LINUX_SYS_PAUSE" in syscall_h
    assert "LINUX_SYS_NICE __NR_nice" not in syscall_h
    assert "SIG_BLOCK=0" in process or "SIG_BLOCK" in process
    assert "case LINUX_SYS_PAUSE" in syscall
    assert "flags & ~(uint32_t)(LEONOS_O_NONBLOCK | LEONOS_O_CLOEXEC)" in ipc
    assert "return -LEONOS_EBADF" in syscall


if __name__ == "__main__":
    tests = [test_linux_numbers_and_flags,
             test_native_syscall_entry_and_stack_protocol,
             test_contract_fixes_are_present]
    for test in tests:
        test()
        print(f"PASS {test.__name__}")
