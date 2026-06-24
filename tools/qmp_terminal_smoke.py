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


def main() -> int:
    if len(sys.argv) != 2:
        return 2
    sock_path = sys.argv[1]
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
    time.sleep(9.5)

    hmp(sock, "mouse_move -280 538", 0.3)
    hmp(sock, "mouse_button 1", 0.1)
    hmp(sock, "mouse_button 0", 0.5)
    hmp(sock, "mouse_move 60 -32", 0.3)
    hmp(sock, "mouse_button 1", 0.1)
    hmp(sock, "mouse_button 0", 1.5)

    for scancode in [0x23, 0x12, 0x26, 0x19, 0x1C]:
        hmp(sock, f"sendkey 0x{scancode:x}", 0.08)

    time.sleep(1.2)
    send(sock, {"execute": "quit"}, 0.2)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
