#!/usr/bin/env python3
from __future__ import annotations

import tempfile
import threading
import unittest
import urllib.parse
import urllib.request

import license_server


class LicenseServerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.store = license_server.LicenseStore(f"{self.tmp.name}/license.db")

    def tearDown(self) -> None:
        self.store.close()
        self.tmp.cleanup()

    def test_first_registered_user_is_admin(self) -> None:
        self.assertTrue(self.store.register("Admin@Example.com", "pass1234")[0])
        self.assertTrue(self.store.register("user@example.com", "pass1234")[0])
        admin = self.store.authenticate("admin@example.com", "pass1234")
        user = self.store.authenticate("user@example.com", "pass1234")
        self.assertEqual(admin["is_admin"], 1)
        self.assertEqual(user["is_admin"], 0)

    def test_online_key_limit_and_binding(self) -> None:
        self.store.register("user@example.com", "pass1234")
        user = self.store.authenticate("user@example.com", "pass1234")
        keys = []
        for _ in range(10):
            ok, key = self.store.generate_online_key(user["id"])
            self.assertTrue(ok)
            keys.append(key)
        self.assertFalse(self.store.generate_online_key(user["id"])[0])
        self.assertEqual(self.store.activate_online("user@example.com", keys[0], "install-a"), (True, "activated"))
        self.assertEqual(self.store.activate_online("user@example.com", keys[0], "install-a"), (True, "already-activated"))
        self.assertEqual(self.store.activate_online("user@example.com", keys[0], "install-b"), (False, "already-bound"))
        self.assertEqual(self.store.activate_online("user@example.com", "bad-key", "install-a"), (False, "bad-format"))
        self.assertEqual(self.store.activate_online("user@example.com", "AB-CD-EFGH-IJKLM", "install-a"), (False, "bad-format"))

    def test_offline_request_limit_and_issuing(self) -> None:
        self.store.register("user@example.com", "pass1234")
        user = self.store.authenticate("user@example.com", "pass1234")
        self.assertTrue(self.store.request_offline(user["id"])[0])
        self.assertFalse(self.store.request_offline(user["id"])[0])
        request_id = self.store.rows("select id from offline_requests")[0]["id"]
        ok, key = self.store.issue_offline(request_id, "20260701", "20260731")
        self.assertTrue(ok)
        self.assertEqual(len(key), 50)
        self.assertTrue(license_server.validate_offline_key("user@example.com", key, "20260708")[0])

    def test_offline_validation_rejects_mismatch_dates_and_tamper(self) -> None:
        key = license_server.make_offline_key("user@example.com", "20260701", "20260731")
        self.assertFalse(license_server.validate_offline_key("user@example.com", "ABC", "20260708")[0])
        self.assertFalse(license_server.validate_offline_key("other@example.com", key, "20260708")[0])
        self.assertFalse(license_server.validate_offline_key("user@example.com", key, "20260801")[0])
        self.assertEqual(license_server.validate_offline_key("user@example.com", key, "bad-clock"), (False, "bad-clock"))
        tampered = key[:-1] + ("A" if key[-1] != "A" else "B")
        self.assertFalse(license_server.validate_offline_key("user@example.com", tampered, "20260708")[0])

    def test_http_register_and_login_work_from_request_threads(self) -> None:
        license_server.LicenseHandler.store = self.store
        license_server.LicenseHandler.sessions = {}
        server = license_server.ThreadingHTTPServer(("127.0.0.1", 0), license_server.LicenseHandler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base = f"http://127.0.0.1:{server.server_address[1]}"
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

        def post(path: str, fields: dict[str, str]) -> str:
            data = urllib.parse.urlencode(fields).encode("utf-8")
            request = urllib.request.Request(base + path, data=data)
            with opener.open(request, timeout=5) as response:
                self.assertEqual(response.status, 200)
                return response.read().decode("utf-8")

        try:
            body = post("/register", {"email": "thread@example.com", "password": "pass1234"})
            self.assertIn("Signed in as thread@example.com", body)
            body = post("/login", {"email": "thread@example.com", "password": "pass1234"})
            self.assertIn("Signed in as thread@example.com", body)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)

    def test_http_chinese_language_option_persists_after_register(self) -> None:
        license_server.LicenseHandler.store = self.store
        license_server.LicenseHandler.sessions = {}
        server = license_server.ThreadingHTTPServer(("127.0.0.1", 0), license_server.LicenseHandler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base = f"http://127.0.0.1:{server.server_address[1]}"
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

        def get(path: str) -> str:
            with opener.open(base + path, timeout=5) as response:
                self.assertEqual(response.status, 200)
                return response.read().decode("utf-8")

        def post(path: str, fields: dict[str, str]) -> str:
            data = urllib.parse.urlencode(fields).encode("utf-8")
            request = urllib.request.Request(base + path, data=data)
            with opener.open(request, timeout=5) as response:
                self.assertEqual(response.status, 200)
                return response.read().decode("utf-8")

        try:
            body = get("/?lang=zh")
            self.assertIn("注册", body)
            self.assertIn("登录", body)
            body = post("/register?lang=zh", {"email": "zh@example.com", "password": "pass1234"})
            self.assertIn("已登录为 zh@example.com", body)
            self.assertIn("生成在线密钥", body)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)


if __name__ == "__main__":
    unittest.main()
