#!/usr/bin/env python3
"""LeonOS 4 Unix-IPC migration security regression audit.

Strict mode asserts the new invariants:
* no private service-request ioctl family remains in kernel source;
* windowd/authd/netmand use SO_PEERCRED as their peer trust boundary;
* setuid and reboot(2) retain the uid==0 gate in kernel syscall handlers.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

FORBIDDEN_KERNEL_RE = re.compile(
    r"\b(?:LEONOS_GUI_IOCTL|LEONOS_AUTH_IOCTL|LEONOS_IOCTL_NET_|"
    r"LEONOS_INPUTM_IOCTL|LEONOS_STARTUP_IOCTL|LEONOS_FS_IOCTL_|"
    r"LEONOS_IOCTL_AUDIO_|LEONOS_IOCTL_DEVICE_LIST|LEONOS_IOCTL_DRIVER_|"
    r"LEONOS_TEXT_IOCTL|LEONOS_IOCTL_LIST_DIR|LEONOS_KERNEL_DEBUG_IOCTL)"
    r"[A-Z0-9_]*\b"
)

KERNEL_ROOTS = ("kernel/ntclks", "drivers/bootstrap")
PEERCRED_PATHS = (
    "userland/apps/windowd/main.c",
    "userland/apps/authd/main.c",
    "userland/apps/serviced/netmand.c",
)


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def kernel_violations() -> list[str]:
    violations: list[str] = []
    for root in KERNEL_ROOTS:
        for path in sorted((ROOT / root).rglob("*")):
            if path.suffix not in {".c", ".h"}:
                continue
            text = path.read_text(encoding="utf-8")
            for match in FORBIDDEN_KERNEL_RE.finditer(text):
                line = text.count("\n", 0, match.start()) + 1
                violations.append(f"{path.relative_to(ROOT)}:{line}: {match.group(0)}")
    return violations


def missing_peercred() -> list[str]:
    missing = []
    for path in PEERCRED_PATHS:
        text = read(path)
        if "SO_PEERCRED" not in text or "leonos_ipc_peer_credentials" not in text:
            missing.append(path)
    return missing


def missing_root_gates() -> list[str]:
    missing = []
    process = read("kernel/ntclks/syscall_process.c")
    if "if (task->uid != 0 && target != task->uid) return -LEONOS_EPERM;" not in process:
        missing.append("kernel/ntclks/syscall_process.c: setuid 非特权目标门")
    if "if (!task || task->uid != 0) return -LEONOS_EPERM;" not in process:
        missing.append("kernel/ntclks/syscall_process.c: reboot uid==0 门")
    return missing


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    violations = kernel_violations()
    peercred = missing_peercred()
    gates = missing_root_gates()
    failures = violations + peercred + gates

    if args.json:
        import json
        print(json.dumps({
            "tool": "test_security_regressions",
            "strict": args.strict,
            "kernel_private_ioctl_violations": violations,
            "missing_so_peercred": peercred,
            "missing_root_gates": gates,
        }, indent=2))
    else:
        print("LeonOS 4 Unix-IPC 安全回归源码检测")
        print(f"私有 ioctl 残留: {len(violations)}")
        for item in violations:
            print(f"  FAIL {item}")
        print(f"SO_PEERCRED 缺失: {len(peercred)}")
        for item in peercred:
            print(f"  FAIL {item}")
        print(f"uid==0 权限门缺失: {len(gates)}")
        for item in gates:
            print(f"  FAIL {item}")
        print("strict ABI security check " + ("passed" if not failures else "failed"))

    return 1 if args.strict and failures else 0


if __name__ == "__main__":
    sys.exit(main())
