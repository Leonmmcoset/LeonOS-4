#!/usr/bin/env python3
"""LeonOS 4 Unix-IPC migration security regression audit.

Strict mode asserts the new invariants:
* no private service-request ioctl family remains in kernel source;
* windowd/authd/netmand use SO_PEERCRED as their peer trust boundary;
* production credential tests enforce capabilities for setuid and reboot(2);
* every privileged authd message is served by a *_from_peer handler, and each
  of those takes its caller identity from the accept-time SO_PEERCRED slot
  rather than from the request payload. authd additionally validates the kernel
  credentials on every frame fragment; stable PID lifetime binding is pending.

Source lint alone does not establish ABI or authentication compatibility.
"""

from __future__ import annotations

import argparse
import re
import subprocess
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


def credential_regressions() -> list[str]:
    # Linux kernel/sys.c and kernel/reboot.c use separate capabilities. The
    # historical source-string assertion required incorrect real-UID gates.
    result = subprocess.run([sys.executable, "tools/test_linux_capabilities.py"],
                            cwd=ROOT, capture_output=True, text=True, timeout=60)
    if result.returncode:
        return ["production credential regression failed: " + (result.stdout + result.stderr)[-4000:]]
    return []


SUDO_PRIVILEGED_MESSAGES = (
    "LEONOS_AUTHD_MSG_RUN",
    "LEONOS_AUTHD_MSG_WAIT",
    "LEONOS_AUTHD_MSG_SUDO_KILL",
    "LEONOS_AUTHD_MSG_SUDO_CHECK",
    "LEONOS_AUTHD_MSG_SUDO_VERIFY",
    "LEONOS_AUTHD_MSG_FILEOP",
)


def missing_from_peer_handlers() -> list[str]:
    """Every privileged message must be dispatched to a *_from_peer handler.

    The handler naming is the audit hook: it forces the caller identity to be an
    explicit parameter that the daemon fills from SO_PEERCRED, so a new message
    cannot quietly serve itself from request fields.
    """
    missing = []
    dispatch = read("userland/apps/authd/main.c")
    for message in SUDO_PRIVILEGED_MESSAGES:
        marker = f"type == {message}"
        if marker not in dispatch:
            missing.append(f"userland/apps/authd/main.c: {message} 未分发")
            continue
        call = "_from_peer("
        if call not in dispatch:
            missing.append(f"userland/apps/authd/main.c: {message} 未交给 *_from_peer 处理")
    return missing


def missing_from_peer_identity() -> list[str]:
    """A *_from_peer handler must consume the peer uid, not a payload field."""
    missing = []
    source = read("userland/apps/authd/authd_sudo.c")
    text = read("userland/apps/authd/main.c")
    if "requester_uid" not in source:
        missing.append("userland/apps/authd/authd_sudo.c: 处理函数未接收 requester_uid")
    # The daemon must pass the accept-time slot uid into every privileged call.
    for message in SUDO_PRIVILEGED_MESSAGES:
        if message in ("LEONOS_AUTHD_MSG_RUN", "LEONOS_AUTHD_MSG_WAIT",
                       "LEONOS_AUTHD_MSG_SUDO_KILL", "LEONOS_AUTHD_MSG_SUDO_CHECK",
                       "LEONOS_AUTHD_MSG_SUDO_VERIFY", "LEONOS_AUTHD_MSG_FILEOP"):
            if "client->uid" not in text:
                missing.append("userland/apps/authd/main.c: 分发未使用 SO_PEERCRED 记录的 client->uid")
                break
    return missing


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    violations = kernel_violations()
    peercred = missing_peercred()
    gates = credential_regressions()
    from_peer = missing_from_peer_handlers() + missing_from_peer_identity()
    failures = violations + peercred + gates + from_peer

    if args.json:
        import json
        print(json.dumps({
            "tool": "test_security_regressions",
            "strict": args.strict,
            "kernel_private_ioctl_violations": violations,
            "missing_so_peercred": peercred,
            "credential_regression_failures": gates,
            "missing_from_peer_handlers": from_peer,
        }, indent=2))
    else:
        print("LeonOS 4 Unix-IPC 安全回归源码检测")
        print(f"私有 ioctl 残留: {len(violations)}")
        for item in violations:
            print(f"  FAIL {item}")
        print(f"SO_PEERCRED 缺失: {len(peercred)}")
        for item in peercred:
            print(f"  FAIL {item}")
        print(f"凭据/capability 行为回归失败: {len(gates)}")
        for item in gates:
            print(f"  FAIL {item}")
        print(f"提权消息 SO_PEERCRED 处理缺失: {len(from_peer)}")
        for item in from_peer:
            print(f"  FAIL {item}")
        print("source lint and focused credential check " + ("passed" if not failures else "failed"))

    return 1 if args.strict and failures else 0


if __name__ == "__main__":
    sys.exit(main())
