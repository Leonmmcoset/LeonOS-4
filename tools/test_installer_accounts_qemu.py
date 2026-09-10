#!/usr/bin/env python3
"""Install onto an exclusive scratch disk, then verify actual login credentials."""
import argparse
from contextlib import contextmanager
from pathlib import Path
import subprocess
import time

from test_installer_window_qemu import Probe as WindowProbe, wait_log
from qmp_terminal_smoke import text_keys
from run_gcc_probe_qemu import qmp_quit

ROOT = Path(__file__).resolve().parents[1]
USER_PASSWORD = "U!" + "u" * 30
ROOT_PASSWORD = "r"


class Probe(WindowProbe):
    def text(self, text):
        punctuation = {";": "semicolon", "(": "shift-9", ")": "shift-0",
                       ">": "shift-dot", "$": "shift-4", "?": "shift-slash",
                       "'": "apostrophe", '"': "shift-apostrophe", "!": "shift-1"}
        for character in text:
            self.key(punctuation.get(character, text_keys(character)[0]))
        # The keyboard and USB pointer travel through separate guest queues.
        time.sleep(1)


@contextmanager
def boot(output, disk, iso=None):
    output.mkdir(parents=True, exist_ok=True)
    serial, qmp = output / "serial.log", output / "qmp.sock"
    command = ["qemu-system-x86_64", "-enable-kvm", "-cpu", "host", "-machine", "q35",
               "-m", "4096", "-smp", "2", "-bios", "/usr/share/edk2/x64/OVMF.4m.fd",
               "-display", "none", "-serial", f"file:{serial}",
               "-device", "VGA,xres=1280,yres=720", "-device", "qemu-xhci", "-device", "usb-tablet",
               "-netdev", "user,id=net0", "-device", "e1000,netdev=net0",
               "-drive", f"file={disk},format=raw,if=ide",
               "-qmp", f"unix:{qmp},server=on,wait=off", "-no-reboot", "-no-shutdown"]
    command += ["-cdrom", str(iso), "-boot", "d"] if iso else ["-boot", "c"]
    with (output / "qemu.log").open("w") as errors:
        process = subprocess.Popen(command, stdout=errors, stderr=errors, cwd=ROOT)
        probe = None
        try:
            deadline = time.monotonic() + 15
            while not qmp.exists() and time.monotonic() < deadline:
                if process.poll() is not None: raise RuntimeError("QEMU failed to start")
                time.sleep(.1)
            probe = Probe(qmp, output)
            yield probe, serial, process
        finally:
            try:
                if probe is not None:
                    try: probe.frame("last-frame")
                    finally: probe.close()
            finally:
                if process.poll() is None: qmp_quit(qmp, process)
                qmp.unlink(missing_ok=True)


def next_page(probe):
    probe.click(probe.width - 165, probe.height - 33)
    time.sleep(.8)


def wait_install(probe, serial, process):
    deadline = time.monotonic() + 2400
    while time.monotonic() < deadline:
        log = serial.read_text(errors="replace")
        if "[installer.elf] installation completed successfully" in log:
            probe.frame("installed")
            return
        if any("[installer.elf]" in line and " ret=-" in line for line in log.splitlines()):
            raise AssertionError("Installer failed; see serial.log and last-frame.png")
        if process.poll() is not None: raise RuntimeError("QEMU exited during installation")
        probe.frame("progress")
        time.sleep(5)
    raise AssertionError("Installation timed out")


def install_gui(probe, serial, process, components):
    wait_log(serial, "[installer.elf] starting installer wizard", process)
    time.sleep(4)
    probe.frame("language")
    for _ in range(6): next_page(probe)
    probe.frame("components")
    y = 64 if probe.height > 640 else 44
    for index, selected in enumerate((components in ("all", "python"), components in ("all", "gcc"))):
        if not selected: probe.click(270, y + 88 + index * 54)
    probe.frame("components-selected")
    next_page(probe)
    for index, text in enumerate(("alice", USER_PASSWORD, USER_PASSWORD, ROOT_PASSWORD, ROOT_PASSWORD)):
        probe.click(350, y + 96 + index * 54)
        probe.text(text)
    probe.frame("accounts")
    next_page(probe)
    probe.frame("confirmation")
    probe.click(350, y + 212)
    probe.text("INSTALL")
    probe.frame("confirmation-ready")
    next_page(probe)
    wait_install(probe, serial, process)


def install_tty(probe, serial, process, components):
    time.sleep(3)
    probe.key("down")
    probe.key("ret")
    answers = (("Mode [install/update]: ", "install"),
               ("Select disk number (r to refresh, q to quit): ", "0"),
               ("Install Python? [Y/n]: ", "y" if components in ("all", "python") else "n"),
               ("Install GCC and binutils? [Y/n]: ", "y" if components in ("all", "gcc") else "n"),
               ("Username: ", "alice"), ("Password: ", USER_PASSWORD),
               ("Confirm password: ", USER_PASSWORD), ("root password: ", ROOT_PASSWORD),
               ("Confirm root password: ", ROOT_PASSWORD),
               ("Type INSTALL to confirm erasing this disk: ", "INSTALL"))
    for prompt, answer in answers:
        wait_log(serial, prompt, process)
        probe.text(answer)
        probe.key("ret")
    wait_install(probe, serial, process)


def login_tty(probe, serial, process, root, components):
    time.sleep(3)
    probe.key("down")
    probe.key("ret")
    wait_log(serial, "Username: ", process)
    probe.text("root" if root else "alice")
    probe.key("ret")
    wait_log(serial, "Password: ", process)
    probe.text(ROOT_PASSWORD if root else USER_PASSWORD)
    probe.key("ret")
    wait_log(serial, "Login successful.", process)
    time.sleep(2)
    if root:
        command = ("cat /proc/self/status; echo root-ok > /root/installer-root-check; "
                   "cat /root/installer-root-check; echo group-only > /tmp/root-group-only; "
                   "chmod 040 /tmp/root-group-only; echo ROOT_SESSION_DONE")
    else:
        command = (
            "cat /proc/self/status; echo HOME=$HOME; echo user-ok > $HOME/installer-user-check; "
            "cat $HOME/installer-user-check; "
            "if (echo bad > /root/installer-denied) 2>/dev/null; then echo ACCESS_FAILED; else echo ROOT_DENIED; fi; "
            "if (echo bad > /etc/passwd) 2>/dev/null; then echo ACCESS_FAILED; else echo PASSWD_DENIED; fi; "
            "if cat /tmp/root-group-only 2>/dev/null; then echo ACCESS_FAILED; else echo ROOT_GROUP_DENIED; fi; "
            "test -e /usr/bin/python3; echo PYTHON_STATUS=$?; "
            "test -e /usr/bin/gcc; echo GCC_STATUS=$?; echo USER_SESSION_DONE")
    probe.text(command)
    probe.key("ret")
    marker = "ROOT_SESSION_DONE" if root else "USER_SESSION_DONE"
    # The echoed input contains the marker too, so wait for a standalone output line.
    deadline = time.monotonic() + 30
    while marker not in serial.read_text(errors="replace").splitlines():
        if time.monotonic() >= deadline or process.poll() is not None:
            raise AssertionError(f"Missing shell output: {marker}")
        time.sleep(.2)
    log = serial.read_text(errors="replace").replace("\r", "")
    expected = ["0" if root else "1000"] * 4
    for field in ("Uid:", "Gid:"):
        identities = [line.split()[1:] for line in log.splitlines() if line.startswith(field)]
        assert identities and all(identity == expected for identity in identities), (field, identities)
    if not root:
        for line in ("ROOT_DENIED", "PASSWD_DENIED", "ROOT_GROUP_DENIED", "HOME=/home/alice", "user-ok"):
            assert "\n" + line + "\n" in log, line
        assert "\nACCESS_FAILED\n" not in log
        assert f"\nPYTHON_STATUS={0 if components in ('all', 'python') else 1}\n" in log
        assert f"\nGCC_STATUS={0 if components in ('all', 'gcc') else 1}\n" in log
    else:
        assert "\nroot-ok\n" in log
    probe.frame("root-session" if root else "user-session")


def login_desktop(probe, serial, process):
    wait_log(serial, "[login.elf] starting login UI", process)
    time.sleep(5)
    probe.frame("login")
    probe.key("down")
    probe.text(USER_PASSWORD)
    probe.key("ret")
    wait_log(serial, "name=login.elf code=0", process)
    time.sleep(5)
    probe.key("meta_l")
    time.sleep(1)
    probe.text("terminal")
    probe.key("ret")
    wait_log(serial, "terminal: PTY ready", process)
    time.sleep(2)
    probe.text("cat /proc/self/status; echo HOME=$HOME")
    probe.key("ret")
    wait_log(serial, "name=cat code=0", process)
    time.sleep(2)
    probe.frame("terminal-identity")
    log = serial.read_text(errors="replace")
    assert "name=terminal.elf code=127" not in log, "Terminal shell failed to execute"
    assert "name=terminal.elf code=126" not in log, "Terminal shell failed to drop credentials"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, default=ROOT / "build/images/leonos4-installer.iso")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--components", choices=("none", "all", "python", "gcc"), default="none")
    parser.add_argument("--installer", choices=("gui", "tty"), default="gui")
    parser.add_argument("--login-only", action="store_true", help="Boot this test's existing scratch disk without reinstalling")
    parser.add_argument("--desktop", action="store_true", help="Also log into the installed desktop and capture Terminal identity")
    parser.add_argument("--desktop-only", action="store_true", help="Only boot the existing test disk into the desktop")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    disk = output / "scratch.raw"
    if args.desktop_only:
        if not disk.is_file(): parser.error("--desktop-only requires an existing scratch disk")
        with boot(output / "desktop", disk) as session:
            login_desktop(*session)
        return
    if args.login_only:
        if not disk.is_file(): parser.error("--login-only requires an existing scratch disk")
    else:
        with disk.open("xb") as file: file.truncate(8 * 1024**3)
        with boot(output / "install", disk, args.iso.resolve()) as session:
            (install_gui if args.installer == "gui" else install_tty)(*session, args.components)
    with boot(output / "root", disk) as session:
        login_tty(*session, True, args.components)
    with boot(output / "user", disk) as session:
        login_tty(*session, False, args.components)
    if args.desktop:
        with boot(output / "desktop", disk) as session:
            login_desktop(*session)
    print(f"PASS: installer, {args.components}, ordinary/root credentials: {output}")


if __name__ == "__main__":
    main()
