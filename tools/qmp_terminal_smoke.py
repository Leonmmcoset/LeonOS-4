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


def text_keys(text: str) -> tuple[str, ...]:
    special = {
        " ": "spc",
        ".": "dot",
        "/": "slash",
        "-": "minus",
        "_": "shift-minus",
        ":": "shift-semicolon",
    }
    keys: list[str] = []
    for character in text:
        keys.append(special.get(character, character))
    return tuple(keys)


def main() -> int:
    arguments = sys.argv[1:]
    skip_oobe = False
    exit_only = False
    tcc_smoke = False
    desktop_app: str | None = None
    login_password: str | None = None
    editor = "nano"
    if arguments and arguments[0] == "--skip-oobe":
        skip_oobe = True
        arguments = arguments[1:]
    if arguments and arguments[0] == "--exit-only":
        exit_only = True
        arguments = arguments[1:]
    if arguments and arguments[0] == "--tcc":
        tcc_smoke = True
        arguments = arguments[1:]
    if len(arguments) >= 2 and arguments[0] == "--desktop-app":
        desktop_app = arguments[1]
        arguments = arguments[2:]
    if len(arguments) >= 2 and arguments[0] == "--login-password":
        login_password = arguments[1]
        arguments = arguments[2:]
    if len(arguments) >= 2 and arguments[0] == "--editor":
        editor = arguments[1]
        arguments = arguments[2:]
    if editor not in ("nano", "pleditor"):
        return 2
    if desktop_app is not None and (not desktop_app.isascii() or not desktop_app.isalnum()):
        return 2
    if desktop_app is not None and (tcc_smoke or exit_only):
        return 2
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
        # A freshly generated image opens OOBE.  The default username is admin
        # and the password field owns focus, so create the first account before
        # starting Terminal through the Start-menu keyboard search.
        send_keys(sock, ("n", "a", "n", "o", "t", "e", "s", "t", "ret"))
        # The desktop relocks its input routing after the fullscreen OOBE
        # window is destroyed, so wait until the desktop owns keyboard focus.
        # OOBE exits before the desktop has necessarily removed its fullscreen
        # window and restored Start-menu focus.  Wait for that focus handoff
        # before opening Terminal; otherwise the search keystrokes can be
        # delivered to the dying OOBE window on a cold QEMU boot.
        time.sleep(12.0)
    elif login_password is not None:
        send_keys(sock, tuple(login_password) + ("ret",))
        time.sleep(2.0)
    hmp(sock, "sendkey meta_l", 0.5)
    # Opening Start is asynchronous.  Give the menu time to claim keyboard
    # focus before the search text starts arriving.
    time.sleep(1.5)

    if desktop_app is not None:
        # GUI examples are normal desktop apps, so start them through the same
        # Start-menu search route a user uses.  The screenshot is the runtime
        # assertion: it catches failures after process spawn, window creation
        # or the first framebuffer presentation.
        send_keys(sock, text_keys(desktop_app) + ("ret",))
        time.sleep(10.0)
        hmp(sock, f"screendump build/images/{desktop_app}-qmp-smoke.ppm", 0.4)
        hmp(sock, "sendkey alt-f4", 2.0)
        send(sock, {"execute": "quit"}, 0.2)
        return 0

    send_keys(sock, ("t", "e", "r", "m", "i", "n", "a", "l", "ret"))
    # Wait for Terminal to create its window and for BusyBox to enter the PTY
    # read loop before sending the editor command.  A cold guest may still be
    # loading the font and starting BusyBox after the window first appears.
    time.sleep(18.0)

    if tcc_smoke:
        # Compile the image-staged example with the on-device compiler, then
        # execute the resulting ELF through the resident BusyBox shell.
        send_keys(sock, text_keys("cd 0:/programs/tcc") + ("ret",))
        time.sleep(1.0)
        send_keys(sock, text_keys("tcc examples/hello.c -o hello.elf") + ("ret",))
        # The first full compile parses the staged Picolibc headers from the
        # FAT image.  On a cold QEMU guest that can exceed the generic editor
        # smoke-test delay, so do not inject the executable command while the
        # compiler still owns the PTY.
        time.sleep(60.0)
        # Hush intentionally does not search the current directory unless it
        # is in PATH.  Use the absolute output path so this verifies the ELF
        # produced by TCC rather than depending on a shell PATH policy.
        send_keys(sock, text_keys("0:/programs/tcc/hello.elf") + ("ret",))
        time.sleep(3.0)
        hmp(sock, "screendump build/images/tcc-qmp-smoke.ppm", 0.4)
        send(sock, {"execute": "quit"}, 0.2)
        return 0

    # Both terminal editors are launched by BusyBox as external children
    # through the PTY.  They must write a file, restore the terminal and hand
    # control back to the resident shell.
    filename = "nanotest.txt" if editor == "nano" else "pleditortest.txt"
    send_keys(sock, text_keys(f"{editor} {filename}"))
    # The editor is loaded lazily; wait until its first userspace scheduling
    # turn before sending raw-mode input.
    time.sleep(5.0)
    if exit_only:
        hmp(sock, "sendkey ctrl-x" if editor == "nano" else "sendkey ctrl-q", 20.0)
        send(sock, {"execute": "quit"}, 0.2)
        return 0

    if editor == "nano":
        send_keys(sock, ("n", "a", "n", "o", "s", "m", "o", "k", "e"))
        hmp(sock, "sendkey ctrl-o", 0.3)
        hmp(sock, "sendkey ret", 2.0)
        hmp(sock, "sendkey ctrl-x", 2.0)
    else:
        send_keys(sock, ("p", "l", "o", "s", "s", "m", "o", "k", "e"))
        hmp(sock, "sendkey ctrl-s", 2.0)
        hmp(sock, "sendkey ctrl-q", 2.0)

    send_keys(sock, text_keys(f"cat {filename}") + ("ret",))

    time.sleep(2.0)
    hmp(sock, f"screendump build/images/{editor}-qmp-smoke.ppm", 0.4)
    send(sock, {"execute": "quit"}, 0.2)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
