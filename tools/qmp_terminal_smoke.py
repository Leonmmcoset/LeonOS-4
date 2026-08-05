#!/usr/bin/env python3
import json
import socket
import sys
import time


def recv_some(sock: socket.socket) -> None:
    sock.settimeout(0.2)
    while True:
        try:
            data = sock.recv(4096)
        except OSError:
            break
        if not data or len(data) < 4096:
            break


def send(sock: socket.socket, obj: dict, delay: float = 0.1) -> None:
    sock.sendall((json.dumps(obj) + "\r\n").encode())
    time.sleep(delay)
    recv_some(sock)


def hmp(sock: socket.socket, cmdline: str, delay: float = 0.1) -> None:
    send(sock, {"execute": "human-monitor-command", "arguments": {"command-line": cmdline}}, delay)


def send_keys(sock: socket.socket, keys: tuple[str, ...], delay: float = 0.08) -> None:
    for key in keys:
        hmp(sock, f"sendkey {key}", delay)


def main() -> int:
    arguments = sys.argv[1:]
    skip_oobe = False
    exit_only = False
    login_password: str | None = None
    if arguments and arguments[0] == "--skip-oobe":
        skip_oobe = True
        arguments = arguments[1:]
    if arguments and arguments[0] == "--exit-only":
        exit_only = True
        arguments = arguments[1:]
    if len(arguments) >= 2 and arguments[0] == "--login-password":
        login_password = arguments[1]
        arguments = arguments[2:]
    if len(arguments) != 1 or (login_password is not None and not skip_oobe):
        return 2
    sock_path = arguments[0]
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    for _ in range(200):
        try:
            sock.connect(sock_path)
            break
        except OSError:
            time.sleep(0.05)
    else:
        return 3

    recv_some(sock)
    send(sock, {"execute": "qmp_capabilities"})
    # The full UEFI boot and userland handoff take about 30 seconds under
    # headless QEMU.  Send the OOBE keys only after its fullscreen window is
    # accepting GUI events.
    time.sleep(30.0)

    if not skip_oobe:
        # A freshly generated image opens OOBE.  The default username is admin;
        # create the first account before starting the terminal via the Start-menu
        # keyboard search, so this test has no persisted-image prerequisite.
        send_keys(sock, ("tab", "n", "a", "n", "o", "t", "e", "s", "t", "ret"))
        # The desktop relocks its input routing after the fullscreen OOBE
        # window is destroyed, so wait until the desktop owns keyboard focus.
        time.sleep(6.0)
    elif login_password is not None:
        send_keys(sock, tuple(login_password) + ("ret",))
        time.sleep(2.0)
    hmp(sock, "sendkey meta_l", 0.5)
    send_keys(sock, ("t", "e", "r", "m", "i", "n", "a", "l", "ret"))
    # Wait for Terminal to create its window and for BusyBox to enter the PTY
    # read loop before sending the editor command.
    time.sleep(10.0)

    # Nano is launched by BusyBox as an external child through the PTY.  It
    # must be able to write a file, restore the terminal, and return control
    # to the resident shell.
    send_keys(sock, ("n", "a", "n", "o", "spc", "n", "a", "n", "o", "t", "e", "s", "t", "dot", "t", "x", "t", "ret"))
    # Nano is loaded lazily; wait until its first userspace scheduling turn
    # before writing into its raw terminal mode.
    time.sleep(5.0)
    if exit_only:
        hmp(sock, "sendkey ctrl-x", 20.0)
        send(sock, {"execute": "quit"}, 0.2)
        return 0

    send_keys(sock, ("n", "a", "n", "o", "s", "m", "o", "k", "e"))
    hmp(sock, "sendkey ctrl-o", 0.3)
    hmp(sock, "sendkey ret", 2.0)
    hmp(sock, "sendkey ctrl-x", 2.0)

    send_keys(sock, ("c", "a", "t", "spc", "n", "a", "n", "o", "t", "e", "s", "t", "dot", "t", "x", "t", "ret"))

    time.sleep(2.0)
    hmp(sock, "screendump build/images/terminal-qmp-smoke.ppm", 0.4)
    send(sock, {"execute": "quit"}, 0.2)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
