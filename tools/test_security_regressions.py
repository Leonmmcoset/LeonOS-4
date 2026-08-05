#!/usr/bin/env python3
"""只读检测 LeonOS 4 已知安全缺陷的源码回归测试。

默认模式用于审计当前源码：发现仍存在的缺陷模式不会使进程失败。
修复完成后，用 ``--strict`` 在 CI 中要求所有检测项均得到缓解。
本程序不会启动虚拟机、调用 OS IOCTL、发送网络帧或访问网络。
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Callable


ROOT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class FindingResult:
    finding_id: str
    title: str
    status: str
    evidence: str
    remediation_signal: str

    @property
    def outstanding(self) -> bool:
        return self.status == "仍存在"

    @property
    def unavailable(self) -> bool:
        return self.status == "无法检查"


class SourceAudit:
    def __init__(self) -> None:
        self._cache: dict[str, str] = {}

    def read(self, relative_path: str) -> str:
        if relative_path not in self._cache:
            path = ROOT / relative_path
            self._cache[relative_path] = path.read_text(encoding="utf-8")
        return self._cache[relative_path]

    def function(self, relative_path: str, name: str) -> str:
        source = self.read(relative_path)
        match = re.search(
            rf"\b{re.escape(name)}\s*\([^;{{}}]*\)\s*\{{", source, re.DOTALL
        )
        if not match:
            raise ValueError(f"找不到函数 {relative_path}:{name}")
        start = match.end() - 1
        depth = 0
        for index in range(start, len(source)):
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
                if depth == 0:
                    return source[start : index + 1]
        raise ValueError(f"函数大括号不完整 {relative_path}:{name}")

    def ioctl_block(self, request: str) -> str:
        relative_path = "kernel/ntclks/syscall.c"
        source = self.read(relative_path)
        marker = f"a1 == {request}"
        start = source.find(marker)
        if start < 0:
            raise ValueError(f"找不到 IOCTL 分支 {relative_path}:{request}")
        next_branch = source.find("\n    if (number == LINUX_SYS_IOCTL", start + len(marker))
        return source[start : next_branch if next_branch >= 0 else len(source)]

    def evidence(self, relative_path: str, needle: str) -> str:
        source = self.read(relative_path)
        offset = source.find(needle)
        if offset < 0:
            return relative_path
        line = source.count("\n", 0, offset) + 1
        return f"{relative_path}:{line} ({needle})"


def result_if(
    finding_id: str,
    title: str,
    vulnerable: bool,
    evidence: str,
    remediation_signal: str,
) -> FindingResult:
    return FindingResult(
        finding_id=finding_id,
        title=title,
        status="仍存在" if vulnerable else "未检测到已知缺陷模式",
        evidence=evidence,
        remediation_signal=remediation_signal,
    )


def check_gui_geometry(audit: SourceAudit) -> FindingResult:
    path = "kernel/ntclks/gui_ipc.c"
    body = audit.function(path, "ensure_window_buffer")
    legacy_cast = (
        "uint32_t pages;" in body
        and "bytes = (uint64_t)width * height * sizeof(uint32_t);" in body
        and "pages = (uint32_t)((bytes + 4095ULL) / 4096ULL);" in body
    )
    has_overflow_guard = "UINT32_MAX" in body or "SIZE_MAX" in body
    return result_if(
        "SEC-001",
        "GUI 几何参数整数回绕导致内核越界写",
        legacy_cast and not has_overflow_guard,
        audit.evidence(path, "pages = (uint32_t)((bytes + 4095ULL) / 4096ULL);"),
        "在页数缩窄前限制 bytes，并拒绝页数为零或超过分配器上限的几何参数。",
    )


def check_power_ioctl(audit: SourceAudit) -> FindingResult:
    path = "kernel/ntclks/syscall.c"
    reboot = audit.ioctl_block("LEONOS_GUI_IOCTL_REBOOT")
    shutdown = audit.ioctl_block("LEONOS_GUI_IOCTL_SHUTDOWN")
    privileged = ("authz_check" in reboot or "require_window_server" in reboot) and (
        "authz_check" in shutdown or "require_window_server" in shutdown
    )
    vulnerable = "power_reboot();" in reboot and "power_shutdown();" in shutdown and not privileged
    return result_if(
        "SEC-002",
        "电源控制 IOCTL 缺少授权检查",
        vulnerable,
        audit.evidence(path, "a1 == LEONOS_GUI_IOCTL_REBOOT"),
        "在重启和关机分支调用统一授权检查，拒绝非管理员或非受信任系统服务。",
    )


def check_display_deputy(audit: SourceAudit) -> FindingResult:
    path = "kernel/ntclks/syscall.c"
    block = audit.ioctl_block("LEONOS_GUI_IOCTL_DISPLAY_REQUEST")
    authorized = "authz_check" in block or "require_window_server" in block
    return result_if(
        "SEC-003",
        "显示请求可驱动受信任桌面持久化配置",
        "gui_ipc_request_display" in block and not authorized,
        audit.evidence(path, "a1 == LEONOS_GUI_IOCTL_DISPLAY_REQUEST"),
        "将请求绑定至调用者窗口并由桌面服务按调用者权限和可接受字段处理。",
    )


def check_login_rate_limit(audit: SourceAudit) -> FindingResult:
    path = "middlelayer/osmlayer/runtime.c"
    body = audit.function(path, "osmlayer_auth_login").lower()
    protection_terms = ("rate_limit", "throttle", "lockout", "failed_attempt", "retry_after")
    return result_if(
        "SEC-004",
        "登录认证缺少失败限速和锁定",
        not any(term in body for term in protection_terms),
        audit.evidence(path, "static int osmlayer_auth_login"),
        "按用户和来源记录失败次数，加入退避、短期锁定及成功后的计数清除。",
    )


def check_chunked_overflow(audit: SourceAudit) -> FindingResult:
    path = "userland/libc/src/libc.c"
    body = audit.function(path, "http_decode_chunked")
    legacy_parse = "chunk_size = chunk_size * 16U + http_hex_value(buffer[src]);" in body
    wrapped_bound = "if (src + chunk_size > raw_len)" in body
    safe_parse = "uint64_t chunk_size" in body or "UINT32_MAX" in body
    safe_bound = "chunk_size > raw_len - src" in body
    return result_if(
        "SEC-005",
        "HTTP 分块解码整数回绕导致越界读取",
        legacy_parse and wrapped_bound and not (safe_parse and safe_bound),
        audit.evidence(path, "chunk_size = chunk_size * 16U + http_hex_value(buffer[src]);"),
        "解析十六进制长度时检测乘加回绕，并用 chunk_size > raw_len - src 进行差值边界检查。",
    )


def check_https_downgrade(audit: SourceAudit) -> FindingResult:
    path = "userland/libc/src/libc.c"
    body = audit.function(path, "leonos_http_request")
    follows_redirect = (
        "leonos_http_resolve_url(current_url, location," in body
        and "http_copy_text(current_url, sizeof(current_url), next_url);" in body
    )
    downgrade_guards = (
        "REDIRECT_DOWNGRADE" in body,
        "current.secure && !next.secure" in body,
        'http_starts_with_ignore_case(current_url, "https://")' in body
        and 'http_starts_with_ignore_case(next_url, "http://")' in body,
    )
    return result_if(
        "SEC-006",
        "HTTPS 下载允许降级重定向到 HTTP",
        follows_redirect and not any(downgrade_guards),
        audit.evidence(path, "http_copy_text(current_url, sizeof(current_url), next_url);"),
        "在跟随重定向前拒绝 https:// 到 http:// 的方案降级，并向调用者返回明确状态。",
    )


def check_arp_cache(audit: SourceAudit) -> FindingResult:
    path = "kernel/ntclks/net.c"
    body = audit.function(path, "net_handle_arp")
    store_at_receive = "net_arp_cache_store(sender_ip, sender_mac);" in body
    return result_if(
        "SEC-007",
        "ARP 缓存接受未关联的发送者映射",
        store_at_receive,
        audit.evidence(path, "net_arp_cache_store(sender_ip, sender_mac);"),
        "仅在匹配未完成的 ARP 查询或经过一致性校验的回复中更新缓存。",
    )


def check_dhcp_server_binding(audit: SourceAudit) -> FindingResult:
    path = "kernel/ntclks/net.c"
    wait_body = audit.function(path, "net_dhcp_wait")
    request_body = audit.function(path, "net_dhcp_request")
    legacy_ack_wait = "net_dhcp_wait(xid, timeout_ms, NET_DHCP_ACK, &offer)" in request_body
    server_bound = "expected_server" in wait_body or "offer->server_ip !=" in wait_body
    return result_if(
        "SEC-008",
        "DHCP ACK 未绑定已选择的服务器",
        legacy_ack_wait and not server_bound,
        audit.evidence(path, "net_dhcp_wait(xid, timeout_ms, NET_DHCP_ACK, &offer)"),
        "将 OFFER 的 server identifier 传入 ACK 等待逻辑，拒绝不匹配服务器或租约地址的 ACK。",
    )


def check_tcp_rst(audit: SourceAudit) -> FindingResult:
    path = "kernel/ntclks/net.c"
    body = audit.function(path, "net_socket_handle_tcp")
    unvalidated_rst = re.search(
        r"if\s*\(\s*flags\s*&\s*TCP_FLAG_RST\s*\)\s*\{\s*"
        r"net_socket_mark_closed\(",
        body,
    )
    return result_if(
        "SEC-009",
        "TCP RST 在缺少序号验证时关闭连接",
        bool(unvalidated_rst),
        audit.evidence(path, "if (flags & TCP_FLAG_RST)"),
        "仅当 RST 的序号落在连接当前接收窗口内时关闭连接。",
    )


def check_basic_auth_scope(audit: SourceAudit) -> FindingResult:
    path = "userland/apps/browser/auth.c"
    source = audit.read(path)
    legacy_key = "static int auth_entry_for(const char *host, uint32_t port)" in source
    called_without_scheme = "auth_entry_for(parsed.host, parsed.port)" in source
    return result_if(
        "SEC-010",
        "浏览器 Basic 凭据作用域未包含传输方案",
        legacy_key and called_without_scheme,
        audit.evidence(path, "static int auth_entry_for(const char *host, uint32_t port)"),
        "将 URL 方案纳入凭据键；绝不将 HTTPS Basic Authorization 发送到 HTTP。",
    )


CHECKS: tuple[Callable[[SourceAudit], FindingResult], ...] = (
    check_gui_geometry,
    check_power_ioctl,
    check_display_deputy,
    check_login_rate_limit,
    check_chunked_overflow,
    check_https_downgrade,
    check_arp_cache,
    check_dhcp_server_binding,
    check_tcp_rst,
    check_basic_auth_scope,
)


def run_checks() -> list[FindingResult]:
    audit = SourceAudit()
    results: list[FindingResult] = []
    for check in CHECKS:
        try:
            results.append(check(audit))
        except (OSError, UnicodeError, ValueError) as error:
            results.append(
                FindingResult(
                    finding_id=check.__name__.removeprefix("check_").upper(),
                    title=check.__name__,
                    status="无法检查",
                    evidence=str(error),
                    remediation_signal="修复测试脚本与源码锚点的对应关系后重新执行。",
                )
            )
    return results


def print_human(results: list[FindingResult]) -> None:
    print("LeonOS 4 安全回归源码检测（只读，不启动系统或网络）")
    for item in results:
        print(f"[{item.status}] {item.finding_id} {item.title}")
        print(f"  证据: {item.evidence}")
        if item.outstanding:
            print(f"  修复信号: {item.remediation_signal}")
    outstanding = sum(item.outstanding for item in results)
    unavailable = sum(item.unavailable for item in results)
    mitigated = len(results) - outstanding - unavailable
    print(
        f"汇总: 仍存在 {outstanding}，未检测到已知缺陷模式 {mitigated}，无法检查 {unavailable}。"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--report",
        action="store_true",
        help="显式选择报告模式；这是默认行为，发现未修复问题时仍返回零。",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="任一仍存在或无法检查的项目均以非零状态退出，适用于修复后的 CI。",
    )
    parser.add_argument("--json", action="store_true", help="以 JSON 输出检测结果。")
    args = parser.parse_args()

    results = run_checks()
    if args.json:
        print(
            json.dumps(
                {
                    "tool": "test_security_regressions",
                    "read_only": True,
                    "results": [asdict(item) for item in results],
                    "summary": {
                        "outstanding": sum(item.outstanding for item in results),
                        "unavailable": sum(item.unavailable for item in results),
                    },
                },
                ensure_ascii=False,
                indent=2,
            )
        )
    else:
        print_human(results)

    if args.strict and any(item.outstanding or item.unavailable for item in results):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
