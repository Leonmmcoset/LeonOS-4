"""LeonOS network ioctl emulation using host Python sockets."""

from __future__ import annotations

import http.client
import socket
import struct
from dataclasses import dataclass

from . import constants as C
from . import structs


@dataclass
class GuestSocket:
    sock: socket.socket
    connected: bool = False
    host: str = ""
    port: int = 0
    remote_ip: int = 0
    tx: int = 0
    rx: int = 0


class NetworkManager:
    def __init__(self, logger=None):
        self.logger = logger
        self.sockets: dict[int, GuestSocket] = {}
        self.next_socket = 1

    def log(self, text: str) -> None:
        if self.logger:
            self.logger.write(text)

    @staticmethod
    def _timeout(seconds_ms: int) -> float:
        if not seconds_ms:
            seconds_ms = 10000
        return max(0.05, min(seconds_ms, 10000) / 1000.0)

    def ioctl(self, memory, request: int, arg: int) -> int | None:
        if request == C.NET_IOCTL_CONFIG:
            memory.write(arg, structs.pack_net_config())
            return 0
        if request == C.NET_IOCTL_DHCP:
            timeout_ms = memory.read_u32(arg)
            memory.write(arg, struct.pack("<II", timeout_ms, C.NET_STATUS_OK) + structs.pack_net_config())
            return 0
        if request == C.NET_IOCTL_DNS:
            return self._dns(memory, arg)
        if request == C.NET_IOCTL_SOCKET_OPEN:
            return self._socket_open(memory, arg)
        if request == C.NET_IOCTL_SOCKET_CONNECT:
            return self._socket_connect(memory, arg)
        if request == C.NET_IOCTL_SOCKET_SEND:
            return self._socket_send(memory, arg)
        if request == C.NET_IOCTL_SOCKET_RECV:
            return self._socket_recv(memory, arg)
        if request == C.NET_IOCTL_SOCKET_CLOSE:
            return self._socket_close(memory, arg)
        if request == C.NET_IOCTL_CONNECTIONS:
            return self._connections(memory, arg)
        if request == C.NET_IOCTL_HTTP_GET:
            return self._http_get(memory, arg)
        return None

    def _dns(self, memory, arg: int) -> int:
        query = structs.NetDNS.unpack(memory.read(arg, structs.NetDNS.SIZE))
        addresses = [0, 0, 0, 0]
        try:
            infos = socket.getaddrinfo(query.name, None, socket.AF_INET, socket.SOCK_STREAM)
            unique = []
            for info in infos:
                addr = info[4][0]
                value = structs.ipv4_to_u32(addr)
                if value and value not in unique:
                    unique.append(value)
                if len(unique) >= C.NET_DNS_MAX_ADDRESSES:
                    break
            if unique:
                for idx, value in enumerate(unique):
                    addresses[idx] = value
                query.status = C.NET_STATUS_OK
                query.address_count = len(unique)
            else:
                query.status = C.NET_STATUS_DNS_NO_ANSWER
                query.address_count = 0
        except OSError:
            query.status = C.NET_STATUS_DNS_FAILED
            query.address_count = 0
        query.addresses = tuple(addresses)
        memory.write(arg, query.pack())
        return 0

    def _socket_open(self, memory, arg: int) -> int:
        query = structs.SocketOpen.unpack(memory.read(arg, structs.SocketOpen.SIZE))
        if query.domain != C.NET_AF_INET or query.type != C.NET_SOCK_STREAM or query.protocol != C.NET_IPPROTO_TCP:
            query.status = C.NET_STATUS_PROTOCOL_UNSUPPORTED
            query.socket = -1
        elif len(self.sockets) >= C.NET_SOCKET_MAX:
            query.status = C.NET_STATUS_SOCKET_LIMIT
            query.socket = -1
        else:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(self._timeout(query.timeout_ms))
            socket_id = self.next_socket
            self.next_socket += 1
            self.sockets[socket_id] = GuestSocket(sock)
            query.status = C.NET_STATUS_OK
            query.socket = socket_id
        memory.write(arg, query.pack())
        return 0

    def _socket_connect(self, memory, arg: int) -> int:
        query = structs.SocketConnect.unpack(memory.read(arg, structs.SocketConnect.SIZE))
        guest = self.sockets.get(query.socket)
        if not guest:
            query.status = C.NET_STATUS_SOCKET_BAD_HANDLE
            memory.write(arg, query.pack())
            return 0
        try:
            guest.sock.settimeout(self._timeout(query.timeout_ms))
            guest.sock.connect((query.host, query.port or 80))
            remote = guest.sock.getpeername()
            local = guest.sock.getsockname()
            guest.connected = True
            guest.host = query.host
            guest.port = query.port or 80
            guest.remote_ip = structs.ipv4_to_u32(remote[0])
            query.status = C.NET_STATUS_OK
            query.remote_ip = guest.remote_ip
            query.local_ip = structs.ipv4_to_u32(local[0])
            query.local_port = int(local[1])
        except socket.timeout:
            query.status = C.NET_STATUS_TCP_TIMEOUT
        except OSError:
            query.status = C.NET_STATUS_TCP_FAILED
        memory.write(arg, query.pack())
        return 0

    def _socket_send(self, memory, arg: int) -> int:
        query = structs.SocketIO.unpack(memory.read(arg, structs.SocketIO.SIZE))
        guest = self.sockets.get(query.socket)
        if not guest:
            query.status = C.NET_STATUS_SOCKET_BAD_HANDLE
        elif not guest.connected:
            query.status = C.NET_STATUS_SOCKET_NOT_CONNECTED
        else:
            data = memory.read(query.buffer_ptr, query.length)
            try:
                guest.sock.settimeout(self._timeout(query.timeout_ms))
                sent = guest.sock.send(data)
                guest.tx += sent
                query.status = C.NET_STATUS_OK
                query.transferred = sent
            except socket.timeout:
                query.status = C.NET_STATUS_TCP_TIMEOUT
            except OSError:
                query.status = C.NET_STATUS_TCP_FAILED
        memory.write(arg, query.pack())
        return 0

    def _socket_recv(self, memory, arg: int) -> int:
        query = structs.SocketIO.unpack(memory.read(arg, structs.SocketIO.SIZE))
        guest = self.sockets.get(query.socket)
        if not guest:
            query.status = C.NET_STATUS_SOCKET_BAD_HANDLE
        elif not guest.connected:
            query.status = C.NET_STATUS_SOCKET_NOT_CONNECTED
        else:
            try:
                guest.sock.settimeout(self._timeout(query.timeout_ms))
                data = guest.sock.recv(query.length)
                if data:
                    memory.write(query.buffer_ptr, data)
                    guest.rx += len(data)
                    query.status = C.NET_STATUS_OK
                    query.transferred = len(data)
                else:
                    query.status = C.NET_STATUS_SOCKET_CLOSED
                    query.transferred = 0
            except socket.timeout:
                query.status = C.NET_STATUS_TCP_TIMEOUT
                query.transferred = 0
            except ConnectionResetError:
                query.status = C.NET_STATUS_TCP_RESET
                query.transferred = 0
            except OSError:
                query.status = C.NET_STATUS_TCP_FAILED
                query.transferred = 0
        memory.write(arg, query.pack())
        return 0

    def _socket_close(self, memory, arg: int) -> int:
        query = structs.SocketClose.unpack(memory.read(arg, structs.SocketClose.SIZE))
        guest = self.sockets.pop(query.socket, None)
        if not guest:
            query.status = C.NET_STATUS_SOCKET_BAD_HANDLE
        else:
            try:
                guest.sock.close()
                query.status = C.NET_STATUS_OK
            except OSError:
                query.status = C.NET_STATUS_TCP_FAILED
        memory.write(arg, query.pack())
        return 0

    def _connections(self, memory, arg: int) -> int:
        query = structs.ConnectionList.unpack(memory.read(arg, structs.ConnectionList.SIZE))
        count = min(query.capacity, len(self.sockets))
        for idx, (socket_id, guest) in enumerate(list(self.sockets.items())[:count]):
            data = structs.pack_connection_info(
                socket_id,
                C.NET_TCP_ESTABLISHED if guest.connected else C.NET_TCP_CLOSED,
                guest.remote_ip,
                guest.port,
                guest.tx,
                guest.rx,
            )
            memory.write(query.entries_ptr + idx * structs.CONNECTION_INFO_SIZE, data)
        query.count = count
        memory.write(arg, query.pack())
        return 0

    def _http_get(self, memory, arg: int) -> int:
        query = structs.NetHttpGet.unpack(memory.read(arg, structs.NetHttpGet.SIZE))
        port = query.port or 80
        try:
            conn = http.client.HTTPConnection(query.host, port, timeout=self._timeout(query.timeout_ms))
            path = query.path or "/"
            conn.request("GET", path, headers={"Connection": "close", "Host": query.host})
            resp = conn.getresponse()
            body = resp.read(C.NET_HTTP_RESPONSE_MAX)
            query.status = C.NET_STATUS_OK if len(body) < C.NET_HTTP_RESPONSE_MAX else C.NET_STATUS_HTTP_TOO_LARGE
            query.http_status = resp.status
            query.remote_ip = structs.ipv4_to_u32(socket.gethostbyname(query.host))
            query.response_len = min(len(body), C.NET_HTTP_RESPONSE_MAX - 1)
            query.response = body[: C.NET_HTTP_RESPONSE_MAX - 1] + b"\0"
            conn.close()
        except Exception:
            query.status = C.NET_STATUS_HTTP_FAILED
            query.response_len = 0
            query.response = b""
        memory.write(arg, query.pack())
        return 0
