#!/usr/bin/env python3
"""Exercise repaired sudo/su on an isolated, already installed test disk."""
import argparse
from pathlib import Path
import time
import re

from test_installer_accounts_qemu import boot, USER_PASSWORD, ROOT_PASSWORD
from test_sudo_e2e_qemu import Probe, login_desktop_terminal


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--desktop", action="store_true")
    args = parser.parse_args()
    with boot(args.output.resolve(), args.disk.resolve()) as (probe, serial, process):
        probe.__class__ = Probe

        def wait(text, offset=0, timeout=60):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                log = serial.read_text(errors="replace").replace("\r", "")
                if text in log[offset:]:
                    return log
                if process.poll() is not None:
                    raise AssertionError("QEMU exited")
                time.sleep(.2)
            probe.frame("failed")
            raise AssertionError(f"Missing {text!r}; see {serial}")

        def line(text):
            probe.text(text)
            probe.key("ret")

        if args.desktop:
            login_desktop_terminal(probe, serial, process)
            line("sudo -k; sudo grep Uid /proc/self/status")
            time.sleep(4)
            probe.frame("password-prompt")
            line(ROOT_PASSWORD)
            time.sleep(6)
            line("sudo -n sh -c 'echo GUI_SUDO_OK; pwd'; echo gui-pipe | sudo -n cat")
            time.sleep(5)
            line("su - -c 'pwd'")
            time.sleep(5)
            probe.frame("desktop-sudo-su")
            print(f"Captured desktop sudo/su for visual verification: {args.output}")
            return

        time.sleep(4)
        probe.key("down")
        probe.key("ret")
        wait("Username: ")
        line("alice")
        wait("Password: ")
        line(USER_PASSWORD)
        wait("Login successful.")

        def command(text, answers=()):
            offset = len(serial.read_text(errors="replace").replace("\r", ""))
            line(text)
            cursor = offset
            for password in answers:
                log = wait("assword", cursor)
                cursor = len(log)
                line(password)
            return offset

        def uid(log, offset, value):
            # Kernel/service serial writes may precede command output on the
            # same line; require the complete UID tuple, not column zero.
            rows = re.findall(r"\bUid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)", log[offset:])
            assert rows and all(row == (str(value),) * 4 for row in rows), rows

        offset = command("grep Uid /proc/self/status; sudo -k; sudo -n grep Uid /proc/self/status; echo INITIAL=$?")
        log = wait("\nINITIAL=1\n", offset)
        uid(log, offset, 1000)
        offset = command("sudo grep Uid /proc/self/status; echo WRONG=$?", ("incorrect",) * 3)
        log = wait("\nWRONG=1\n", offset)
        assert "\n0\n" not in log[offset:]
        offset = command("sudo grep Uid /proc/self/status; echo CORRECT=$?", (ROOT_PASSWORD,))
        log = wait("\nCORRECT=0\n", offset)
        uid(log, offset, 0)
        offset = command("sudo -n grep Uid /proc/self/status > /tmp/sudo-uid; cat /tmp/sudo-uid; echo REDIRECT=$?")
        log = wait("\nREDIRECT=0\n", offset)
        uid(log, offset, 0)
        offset = command("echo stream | sudo -n cat > /tmp/sudo-pipe; cat /tmp/sudo-pipe; echo PIPE=$?")
        log = wait("\nPIPE=0\n", offset)
        assert "\nstream\n" in log[offset:]
        offset = command("cd /tmp; sudo -n pwd; echo CWD=$?")
        log = wait("\nCWD=0\n", offset)
        assert "\n/tmp\n" in log[offset:]
        offset = command("su - -c 'pwd'; echo LOGIN=$?")
        log = wait("\nLOGIN=0\n", offset)
        assert "\n/root\n" in log[offset:]
        offset = command("sudo -k; su alice -c 'grep Uid /proc/self/status'; sudo -n grep Uid /proc/self/status; echo ORDINARY=$?", (USER_PASSWORD,))
        log = wait("\nORDINARY=1\n", offset)
        uid(log, offset, 1000)
        probe.frame("sudo-su-verified")
        print(f"PASS QEMU: ordinary/root passwords, cache boundaries, redirection, pipe, cwd, su login; {serial}")


if __name__ == "__main__":
    main()
