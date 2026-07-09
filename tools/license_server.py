#!/usr/bin/env python3
from __future__ import annotations

import base64
import hashlib
import hmac
import html
import os
import secrets
import sqlite3
import sys
import threading
from datetime import date, datetime
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlencode, urlparse, urlunparse


DEFAULT_BIND = "127.0.0.1:30301"
DEFAULT_PUBLIC_BASE = "http://localhost:30301"
DEFAULT_DB = "build/license-server/license.db"
OFFLINE_SECRET = b"LeonOS4 offline license v1"
ONLINE_KEY_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"
OFFLINE_ALPHABET = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
LANG_EN = "en"
LANG_ZH = "zh"
MESSAGE_ZH = {
    "Registered.": "已注册。",
    "Invalid email or password.": "邮箱或密码无效。",
    "That email is already registered.": "该邮箱已注册。",
    "Monthly online key limit reached.": "本月在线密钥生成次数已达上限。",
    "Could not generate key.": "无法生成密钥。",
    "Monthly offline request already exists.": "本月已经提交过离线密钥申领。",
    "Offline request submitted.": "离线密钥申领已提交。",
    "Invalid date range.": "日期范围无效。",
    "Request not found.": "未找到申领。",
    "Invalid login.": "登录信息无效。",
}


def normalize_lang(lang: str) -> str:
    return LANG_ZH if lang == LANG_ZH else LANG_EN


def tr(lang: str, en: str, zh: str) -> str:
    return zh if normalize_lang(lang) == LANG_ZH else en


def localized_message(message: str, lang: str) -> str:
    if not message or normalize_lang(lang) != LANG_ZH:
        return message
    return MESSAGE_ZH.get(message, message)


def now_month() -> str:
    return datetime.utcnow().strftime("%Y-%m")


def normalize_email(email: str) -> str:
    return email.strip().lower()


def b36(value: int, width: int) -> str:
    chars: list[str] = []
    for _ in range(width):
        chars.append(OFFLINE_ALPHABET[value % 36])
        value //= 36
    return "".join(reversed(chars))


def email_hash36(email: str) -> str:
    digest = hashlib.sha256(normalize_email(email).encode("utf-8")).digest()
    return b36(int.from_bytes(digest[:8], "big"), 10)


def offline_mac(payload: str) -> str:
    digest = hmac.new(OFFLINE_SECRET, payload.encode("ascii"), hashlib.sha256).digest()
    return b36(int.from_bytes(digest[:8], "big"), 11)


def make_offline_key(email: str, start_yyyymmdd: str, end_yyyymmdd: str) -> str:
    if not valid_date_text(start_yyyymmdd) or not valid_date_text(end_yyyymmdd):
        raise ValueError("dates must be YYYYMMDD")
    if start_yyyymmdd > end_yyyymmdd:
        raise ValueError("start date must be before end date")
    nonce = "".join(secrets.choice(OFFLINE_ALPHABET) for _ in range(12))
    payload = "A" + start_yyyymmdd + end_yyyymmdd + email_hash36(email) + nonce
    return payload + offline_mac(payload)


def validate_offline_key(email: str, key: str, today_yyyymmdd: str) -> tuple[bool, str]:
    clean = key.strip().upper()
    if len(clean) != 50 or any(ch not in OFFLINE_ALPHABET for ch in clean):
        return False, "bad-format"
    payload = clean[:39]
    if clean[0] != "A":
        return False, "bad-version"
    start = clean[1:9]
    end = clean[9:17]
    if not valid_date_text(start) or not valid_date_text(end) or start > end:
        return False, "bad-date"
    if clean[17:27] != email_hash36(email):
        return False, "email-mismatch"
    if offline_mac(payload) != clean[39:]:
        return False, "bad-checksum"
    if not valid_date_text(today_yyyymmdd):
        return False, "bad-clock"
    if today_yyyymmdd < start or today_yyyymmdd > end:
        return False, "out-of-range"
    return True, "ok"


def valid_date_text(value: str) -> bool:
    if len(value) != 8 or not value.isdigit():
        return False
    try:
        date(int(value[:4]), int(value[4:6]), int(value[6:8]))
    except ValueError:
        return False
    return True


def hash_password(password: str, salt: bytes | None = None) -> str:
    salt = salt or secrets.token_bytes(16)
    digest = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, 80_000)
    return base64.b64encode(salt + digest).decode("ascii")


def verify_password(password: str, stored: str) -> bool:
    try:
        raw = base64.b64decode(stored.encode("ascii"))
    except Exception:
        return False
    if len(raw) != 48:
        return False
    salt = raw[:16]
    want = raw[16:]
    got = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, 80_000)
    return hmac.compare_digest(want, got)


def make_online_key() -> str:
    chars = [secrets.choice(ONLINE_KEY_ALPHABET) for _ in range(15)]
    return f"{''.join(chars[:5])}-{''.join(chars[5:10])}-{''.join(chars[10:])}"


def valid_online_key_text(key: str) -> bool:
    clean = key.strip().upper()
    if len(clean) != 17 or clean[5] != "-" or clean[11] != "-":
        return False
    for index, ch in enumerate(clean):
        if index in (5, 11):
            continue
        if not ("0" <= ch <= "9" or "A" <= ch <= "Z"):
            return False
    return True


def online_key_hash(key: str) -> str:
    return hashlib.sha256(key.strip().upper().encode("ascii")).hexdigest()


class LicenseStore:
    def __init__(self, path: str | Path):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.lock = threading.RLock()
        self.db = sqlite3.connect(self.path, check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        self.init_schema()

    def close(self) -> None:
        self.db.close()

    def init_schema(self) -> None:
        with self.lock:
            self.db.executescript(
                """
                create table if not exists users (
                    id integer primary key autoincrement,
                    email text not null unique,
                    password_hash text not null,
                    is_admin integer not null,
                    created_at text not null
                );
                create table if not exists online_keys (
                    id integer primary key autoincrement,
                    user_id integer not null,
                    key_hash text not null unique,
                    display_key text not null,
                    issued_month text not null,
                    bound_email text,
                    bound_install_id text,
                    activated_at text
                );
                create table if not exists offline_requests (
                    id integer primary key autoincrement,
                    user_id integer not null,
                    issued_month text not null,
                    status text not null,
                    requested_at text not null,
                    start_date text,
                    end_date text,
                    offline_key text
                );
                """
            )
            self.db.commit()

    def register(self, email: str, password: str) -> tuple[bool, str]:
        email = normalize_email(email)
        if "@" not in email or len(email) > 160 or len(password) < 4:
            return False, "Invalid email or password."
        with self.lock:
            existing = self.db.execute("select count(*) from users").fetchone()[0]
            is_admin = 1 if existing == 0 else 0
            try:
                self.db.execute(
                    "insert into users(email,password_hash,is_admin,created_at) values(?,?,?,?)",
                    (email, hash_password(password), is_admin, datetime.utcnow().isoformat()),
                )
                self.db.commit()
            except sqlite3.IntegrityError:
                return False, "That email is already registered."
        return True, "Registered."

    def authenticate(self, email: str, password: str) -> sqlite3.Row | None:
        with self.lock:
            user = self.db.execute("select * from users where email=?", (normalize_email(email),)).fetchone()
            if user and verify_password(password, user["password_hash"]):
                return user
        return None

    def user_by_id(self, user_id: int) -> sqlite3.Row | None:
        with self.lock:
            return self.db.execute("select * from users where id=?", (user_id,)).fetchone()

    def generate_online_key(self, user_id: int) -> tuple[bool, str]:
        month = now_month()
        with self.lock:
            count = self.db.execute(
                "select count(*) from online_keys where user_id=? and issued_month=?",
                (user_id, month),
            ).fetchone()[0]
            if count >= 10:
                return False, "Monthly online key limit reached."
            for _ in range(20):
                key = make_online_key()
                try:
                    self.db.execute(
                        "insert into online_keys(user_id,key_hash,display_key,issued_month) values(?,?,?,?)",
                        (user_id, online_key_hash(key), key, month),
                    )
                    self.db.commit()
                    return True, key
                except sqlite3.IntegrityError:
                    continue
        return False, "Could not generate key."

    def request_offline(self, user_id: int) -> tuple[bool, str]:
        month = now_month()
        with self.lock:
            row = self.db.execute(
                "select * from offline_requests where user_id=? and issued_month=?",
                (user_id, month),
            ).fetchone()
            if row:
                return False, "Monthly offline request already exists."
            self.db.execute(
                "insert into offline_requests(user_id,issued_month,status,requested_at) values(?,?,?,?)",
                (user_id, month, "pending", datetime.utcnow().isoformat()),
            )
            self.db.commit()
        return True, "Offline request submitted."

    def issue_offline(self, request_id: int, start_date: str, end_date: str) -> tuple[bool, str]:
        if not valid_date_text(start_date) or not valid_date_text(end_date) or start_date > end_date:
            return False, "Invalid date range."
        with self.lock:
            row = self.db.execute(
                "select r.*, u.email from offline_requests r join users u on u.id=r.user_id where r.id=?",
                (request_id,),
            ).fetchone()
            if not row:
                return False, "Request not found."
            key = make_offline_key(row["email"], start_date, end_date)
            self.db.execute(
                "update offline_requests set status='issued', start_date=?, end_date=?, offline_key=? where id=?",
                (start_date, end_date, key, request_id),
            )
            self.db.commit()
        return True, key

    def activate_online(self, email: str, key: str, install_id: str) -> tuple[bool, str]:
        email = normalize_email(email)
        install_id = install_id.strip()
        if not email or not install_id or len(install_id) > 80:
            return False, "bad-request"
        if not valid_online_key_text(key):
            return False, "bad-format"
        with self.lock:
            row = self.db.execute(
                "select * from online_keys where key_hash=?",
                (online_key_hash(key),),
            ).fetchone()
            if not row:
                return False, "invalid-key"
            if row["bound_email"] and row["bound_install_id"]:
                if row["bound_email"] == email and row["bound_install_id"] == install_id:
                    return True, "already-activated"
                return False, "already-bound"
            user = self.db.execute("select * from users where id=?", (row["user_id"],)).fetchone()
            if not user or user["email"] != email:
                return False, "email-mismatch"
            self.db.execute(
                "update online_keys set bound_email=?, bound_install_id=?, activated_at=? where id=?",
                (email, install_id, datetime.utcnow().isoformat(), row["id"]),
            )
            self.db.commit()
        return True, "activated"

    def rows(self, sql: str, args: tuple[Any, ...] = ()) -> list[sqlite3.Row]:
        with self.lock:
            return list(self.db.execute(sql, args).fetchall())


class Session:
    def __init__(self, token: str, user_id: int):
        self.token = token
        self.user_id = user_id


class LicenseHandler(BaseHTTPRequestHandler):
    store: LicenseStore
    public_base: str = DEFAULT_PUBLIC_BASE
    sessions: dict[str, Session] = {}
    session_lock = threading.RLock()

    def log_message(self, fmt: str, *args: Any) -> None:
        sys.stderr.write("[license] " + fmt % args + "\n")

    def do_GET(self) -> None:
        self.route()

    def do_POST(self) -> None:
        self.route()

    def route(self) -> None:
        path = urlparse(self.path).path
        if path.startswith("/api/activate"):
            self.handle_api_activate()
            return
        if path.startswith("/logout"):
            self.handle_logout()
            return
        if path.startswith("/register"):
            self.handle_register()
            return
        if path.startswith("/login"):
            self.handle_login()
            return
        if path.startswith("/generate-online"):
            self.require_user(lambda user: self.handle_generate_online(user))
            return
        if path.startswith("/request-offline"):
            self.require_user(lambda user: self.handle_request_offline(user))
            return
        if path.startswith("/admin/issue-offline"):
            self.require_admin(lambda user: self.handle_issue_offline(user))
            return
        self.handle_home()

    def read_form(self) -> dict[str, str]:
        if self.command != "POST":
            return {}
        length = int(self.headers.get("Content-Length", "0") or "0")
        raw = self.rfile.read(min(length, 8192)).decode("utf-8", "replace")
        return {k: v[-1] for k, v in parse_qs(raw, keep_blank_values=True).items()}

    def session_token(self) -> str:
        query = parse_qs(urlparse(self.path).query)
        token = query.get("sid", [""])[-1]
        if token:
            return token
        cookie = self.headers.get("Cookie", "")
        for part in cookie.split(";"):
            name, _, value = part.strip().partition("=")
            if name == "session":
                return value
        return ""

    def lang(self) -> str:
        query = parse_qs(urlparse(self.path).query)
        return normalize_lang(query.get("lang", [""])[-1])

    def text(self, en: str, zh: str) -> str:
        return tr(self.lang(), en, zh)

    def message(self, message: str) -> str:
        return localized_message(message, self.lang())

    def session_user(self) -> sqlite3.Row | None:
        token = self.session_token()
        with self.session_lock:
            session = self.sessions.get(token)
        if not session:
            return None
        return self.store.user_by_id(session.user_id)

    def set_session(self, user_id: int) -> str:
        token = secrets.token_urlsafe(24)
        with self.session_lock:
            self.sessions[token] = Session(token, user_id)
        self.send_header("Set-Cookie", f"session={token}; Path=/; HttpOnly")
        return token

    def url_with_state(
        self,
        path: str,
        token: str | None = None,
        lang: str | None = None,
        include_sid: bool = True,
    ) -> str:
        parsed = urlparse(path)
        pairs: list[tuple[str, str]] = []
        for key, values in parse_qs(parsed.query, keep_blank_values=True).items():
            if key in ("sid", "lang"):
                continue
            pairs.extend((key, value) for value in values)
        active_lang = normalize_lang(lang if lang is not None else self.lang())
        if active_lang != LANG_EN:
            pairs.append(("lang", active_lang))
        token = token if token is not None else self.session_token()
        if include_sid and token:
            pairs.append(("sid", token))
        query = urlencode(pairs)
        return urlunparse((parsed.scheme, parsed.netloc, parsed.path, parsed.params, query, parsed.fragment))

    def url_with_sid(self, path: str, token: str | None = None) -> str:
        return self.url_with_state(path, token=token)

    def language_switch_html(self) -> str:
        path = urlparse(self.path).path or "/"
        english = self.url_with_state(path, lang=LANG_EN)
        chinese = self.url_with_state(path, lang=LANG_ZH)
        return (
            "<p class='lang'>"
            f"<a href='{url_attr(english)}'>English</a> "
            f"<a href='{url_attr(chinese)}'>中文</a>"
            "</p>"
        )

    def handle_logout(self) -> None:
        token = self.session_token()
        if token:
            with self.session_lock:
                self.sessions.pop(token, None)
        self.send_response(HTTPStatus.SEE_OTHER)
        self.send_header("Location", self.url_with_state("/", include_sid=False))
        self.send_header("Set-Cookie", "session=; Path=/; Max-Age=0")
        self.end_headers()

    def require_user(self, handler: Any) -> None:
        user = self.session_user()
        if not user:
            self.redirect("/login")
            return
        handler(user)

    def require_admin(self, handler: Any) -> None:
        user = self.session_user()
        if not user:
            self.redirect("/login")
            return
        if not user["is_admin"]:
            self.page(self.text("Forbidden", "禁止访问"), f"<p>{esc(self.text('Administrator access required.', '需要管理员权限。'))}</p>")
            return
        handler(user)

    def handle_register(self) -> None:
        msg = ""
        if self.command == "POST":
            form = self.read_form()
            ok, msg = self.store.register(form.get("email", ""), form.get("password", ""))
            if ok:
                user = self.store.authenticate(form.get("email", ""), form.get("password", ""))
                self.send_response(HTTPStatus.SEE_OTHER)
                token = ""
                if user:
                    token = self.set_session(user["id"])
                self.send_header("Location", self.url_with_sid("/", token))
                self.end_headers()
                return
        self.page(
            self.text("Register", "注册"),
            form_html(
                self.url_with_state("/register", include_sid=False),
                [("email", self.text("Email", "邮箱")), ("password", self.text("Password", "密码"))],
                self.text("Register", "注册"),
                self.message(msg),
                self.url_with_state("/", include_sid=False),
                self.text("Back", "返回"),
            ),
        )

    def handle_login(self) -> None:
        msg = ""
        if self.command == "POST":
            form = self.read_form()
            user = self.store.authenticate(form.get("email", ""), form.get("password", ""))
            if user:
                self.send_response(HTTPStatus.SEE_OTHER)
                token = self.set_session(user["id"])
                self.send_header("Location", self.url_with_sid("/", token))
                self.end_headers()
                return
            msg = "Invalid login."
        self.page(
            self.text("Login", "登录"),
            form_html(
                self.url_with_state("/login", include_sid=False),
                [("email", self.text("Email", "邮箱")), ("password", self.text("Password", "密码"))],
                self.text("Login", "登录"),
                self.message(msg),
                self.url_with_state("/", include_sid=False),
                self.text("Back", "返回"),
            ),
        )

    def handle_generate_online(self, user: sqlite3.Row) -> None:
        if self.command != "POST":
            self.redirect("/")
            return
        ok, msg = self.store.generate_online_key(user["id"])
        back_url = self.url_with_sid("/")
        self.page(
            self.text("Online Key", "在线密钥"),
            f"<p>{esc(msg if ok else self.message(msg))}</p><p><a href='{url_attr(back_url)}'>{esc(self.text('Back', '返回'))}</a></p>",
        )

    def handle_request_offline(self, user: sqlite3.Row) -> None:
        if self.command != "POST":
            self.redirect("/")
            return
        ok, msg = self.store.request_offline(user["id"])
        back_url = self.url_with_sid("/")
        self.page(
            self.text("Offline Request", "离线申领"),
            f"<p>{esc(self.message(msg))}</p><p><a href='{url_attr(back_url)}'>{esc(self.text('Back', '返回'))}</a></p>",
        )

    def handle_issue_offline(self, user: sqlite3.Row) -> None:
        msg = ""
        ok = False
        if self.command == "POST":
            form = self.read_form()
            ok, msg = self.store.issue_offline(
                int(form.get("request_id", "0") or "0"),
                form.get("start_date", ""),
                form.get("end_date", ""),
            )
        back_url = self.url_with_sid("/")
        self.page(
            self.text("Issue Offline Key", "签发离线密钥"),
            f"<p>{esc(msg if ok else self.message(msg))}</p><p><a href='{url_attr(back_url)}'>{esc(self.text('Back', '返回'))}</a></p>",
        )

    def handle_api_activate(self) -> None:
        form = self.read_form()
        ok, msg = self.store.activate_online(
            form.get("email", ""), form.get("key", ""), form.get("install_id", "")
        )
        body = f"ok={'1' if ok else '0'}\nstatus={msg}\n"
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body.encode("utf-8"))))
        self.end_headers()
        self.wfile.write(body.encode("utf-8"))

    def handle_home(self) -> None:
        user = self.session_user()
        body = [f"<p>{esc(self.text('LeonOS License Server', 'LeonOS 许可证服务器'))}</p>"]
        if not user:
            register_url = self.url_with_state("/register", include_sid=False)
            login_url = self.url_with_state("/login", include_sid=False)
            body.append(
                f"<p><a href='{url_attr(register_url)}'>{esc(self.text('Register', '注册'))}</a> "
                f"<a href='{url_attr(login_url)}'>{esc(self.text('Login', '登录'))}</a></p>"
            )
            self.page(self.text("LeonOS License", "LeonOS 许可证"), "\n".join(body))
            return
        logout_url = self.url_with_sid("/logout")
        online_url = self.url_with_sid("/generate-online")
        offline_url = self.url_with_sid("/request-offline")
        body.append(
            f"<p>{esc(self.text('Signed in as', '已登录为'))} {esc(user['email'])}. "
            f"<a href='{url_attr(logout_url)}'>{esc(self.text('Logout', '退出登录'))}</a></p>"
        )
        body.append(
            f"<form method='post' action='{url_attr(online_url)}'>"
            f"<input type='hidden' name='sid' value='{esc(self.session_token())}'>"
            f"<button type='submit'>{esc(self.text('Generate online key', '生成在线密钥'))}</button></form>"
        )
        body.append(
            f"<form method='post' action='{url_attr(offline_url)}'>"
            f"<input type='hidden' name='sid' value='{esc(self.session_token())}'>"
            f"<button type='submit'>{esc(self.text('Request offline key', '申领离线密钥'))}</button></form>"
        )
        body.append(f"<h2>{esc(self.text('Online keys', '在线密钥'))}</h2>")
        rows = self.store.rows(
            "select * from online_keys where user_id=? order by id desc", (user["id"],)
        )
        body.append(table([self.text("Key", "密钥"), self.text("Month", "月份"), self.text("Bound", "已绑定")], [[r["display_key"], r["issued_month"], r["bound_install_id"] or ""] for r in rows]))
        body.append(f"<h2>{esc(self.text('Offline requests', '离线申领'))}</h2>")
        rows = self.store.rows(
            "select * from offline_requests where user_id=? order by id desc", (user["id"],)
        )
        body.append(table([self.text("ID", "编号"), self.text("Month", "月份"), self.text("Status", "状态"), self.text("Dates", "日期"), self.text("Key", "密钥")], [[r["id"], r["issued_month"], r["status"], f"{r['start_date'] or ''}-{r['end_date'] or ''}", r["offline_key"] or ""] for r in rows]))
        if user["is_admin"]:
            pending = self.store.rows(
                "select r.*, u.email from offline_requests r join users u on u.id=r.user_id order by r.id desc"
            )
            body.append(f"<h2>{esc(self.text('Admin offline issuing', '管理员离线密钥签发'))}</h2>")
            for r in pending:
                issue_url = self.url_with_sid("/admin/issue-offline")
                body.append(
                    f"<form method='post' action='{url_attr(issue_url)}'>"
                    f"<input type='hidden' name='sid' value='{esc(self.session_token())}'>"
                    f"<input type='hidden' name='request_id' value='{r['id']}'>"
                    f"<p>#{r['id']} {esc(r['email'])} {esc(r['status'])}</p>"
                    "<p><input name='start_date' placeholder='YYYYMMDD'> "
                    "<input name='end_date' placeholder='YYYYMMDD'> "
                    f"<button type='submit'>{esc(self.text('Issue', '签发'))}</button></p>"
                    "</form>"
                )
        self.page(self.text("LeonOS License", "LeonOS 许可证"), "\n".join(body))

    def page(self, title: str, body: str) -> None:
        language_switch = self.language_switch_html()
        content = (
            "<!doctype html><html><head><meta charset='utf-8'>"
            f"<title>{esc(title)}</title>"
            "<style>body{font-family:Arial,sans-serif;color:#202020;background:#fff;margin:24px;}"
            "input{border:1px solid #777;padding:4px;margin:3px;}button{padding:4px 10px;}"
            "table{border-collapse:collapse;margin:12px 0;}td,th{border:1px solid #999;padding:4px 8px;}"
            "h1{font-size:24px;}h2{font-size:18px;margin-top:20px;}"
            ".lang{font-size:13px;margin-bottom:16px;}</style></head><body>"
            f"{language_switch}<h1>{esc(title)}</h1>{body}</body></html>"
        )
        raw = content.encode("utf-8")
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def redirect(self, location: str) -> None:
        self.send_response(HTTPStatus.SEE_OTHER)
        if location.startswith("/"):
            location = self.url_with_sid(location)
        self.send_header("Location", location)
        self.end_headers()


def esc(value: Any) -> str:
    return html.escape(str(value), quote=True)


def url_attr(value: str) -> str:
    return html.escape(str(value), quote=True).replace("&amp;", "&")


def table(headers: list[str], rows: list[list[Any]]) -> str:
    out = ["<table><tr>"]
    out.extend(f"<th>{esc(h)}</th>" for h in headers)
    out.append("</tr>")
    for row in rows:
        out.append("<tr>")
        out.extend(f"<td>{esc(cell)}</td>" for cell in row)
        out.append("</tr>")
    out.append("</table>")
    return "".join(out)


def form_html(
    action: str,
    fields: list[tuple[str, str]],
    submit: str,
    msg: str = "",
    back_href: str = "/",
    back_label: str = "Back",
) -> str:
    parts = [f"<p>{esc(msg)}</p>" if msg else "", f"<form method='post' action='{url_attr(action)}'>"]
    for name, label in fields:
        typ = "password" if "password" in name else "email" if name == "email" else "text"
        parts.append(f"<p>{esc(label)}<br><input type='{typ}' name='{esc(name)}'></p>")
    parts.append(
        f"<p><button type='submit'>{esc(submit)}</button></p></form>"
        f"<p><a href='{url_attr(back_href)}'>{esc(back_label)}</a></p>"
    )
    return "".join(parts)


def serve() -> None:
    bind = os.environ.get("LEONOS_LICENSE_BIND", DEFAULT_BIND)
    host, _, port_text = bind.rpartition(":")
    host = host or "127.0.0.1"
    port = int(port_text or "30301")
    db_path = os.environ.get("LEONOS_LICENSE_DB", DEFAULT_DB)
    LicenseHandler.store = LicenseStore(db_path)
    LicenseHandler.public_base = os.environ.get("LEONOS_LICENSE_PUBLIC_BASE", DEFAULT_PUBLIC_BASE)
    server = ThreadingHTTPServer((host, port), LicenseHandler)
    print(f"LeonOS license server listening on http://{host}:{port}")
    server.serve_forever()


if __name__ == "__main__":
    serve()
