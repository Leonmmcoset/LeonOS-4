"""Account and component setup regressions using the production C helpers."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from leonos_layout import tool_payload_paths

ROOT = Path(__file__).resolve().parents[1]
CRYPTO = [f"third_party/mbedtls/library/{name}.c" for name in
          ("md", "pkcs5", "sha1", "sha256", "sha512", "platform_util", "aes")]


class InstallerSetupTests(unittest.TestCase):
    def test_login_input(self):
        with tempfile.TemporaryDirectory(prefix="leonos-login-input-") as temporary:
            executable = Path(temporary) / "login-input"
            subprocess.run([
                "cc", "-std=c11", "-D_GNU_SOURCE", "-O1", "-g",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-Wl,--wrap=read,--wrap=write,--wrap=tcgetattr,--wrap=tcsetattr",
                "-Iinclude", "-Iinclude/uapi", "-idirafter", "userland/libc/include",
                "tools/tests/login_input_test.c", "-o", str(executable),
            ], cwd=ROOT, check=True)
            subprocess.run([str(executable)], check=True, timeout=10)

    def test_components(self):
        with tempfile.TemporaryDirectory(prefix="leonos-components-test-") as temporary:
            root = Path(temporary)
            (root / "opt/python").mkdir(parents=True)
            (root / "opt/dyne").mkdir(parents=True)
            manifest = root / "components.list"
            manifest.write_text("".join(f"{component}\t/{path}\n"
                for component in ("python", "musl-gcc") for path in tool_payload_paths(component)))
            executable = root / "components"
            subprocess.run([
                "cc", "-std=c11", "-D_GNU_SOURCE", "-O1", "-g",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                '-DMBEDTLS_CONFIG_FILE="leonos_mbedtls_config.h"',
                "-Iinclude", "-Iinclude/uapi", "-Ithird_party/mbedtls/include",
                "-idirafter", "userland/libc/include", "tools/tests/installer_components_test.c",
                "userland/apps/installer/installer_setup.c", "userland/apps/authd/accounts.c",
                "userland/libc/src/auth_password.c", *CRYPTO, "-o", str(executable),
            ], cwd=ROOT, check=True)
            subprocess.run([str(executable), str(manifest), str(root)], check=True, timeout=30)

    def test_accounts(self):
        with tempfile.TemporaryDirectory(prefix="leonos-setup-test-") as temporary:
            executable = Path(temporary) / "accounts"
            subprocess.run([
                "cc", "-std=c11", "-D_GNU_SOURCE", "-O1", "-g",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                '-DMBEDTLS_CONFIG_FILE="leonos_mbedtls_config.h"',
                "-Iinclude", "-Iinclude/uapi", "-Ithird_party/mbedtls/include",
                "-idirafter", "userland/libc/include", "tools/tests/installer_accounts_test.c",
                "userland/apps/authd/accounts.c", "userland/libc/src/auth_password.c",
                *CRYPTO, "-o", str(executable),
            ], cwd=ROOT, check=True)
            salt = bytes(range(16))
            derived = hashlib.pbkdf2_hmac("sha256", b"reference-password", salt, 100000)
            reference = f"$pbkdf2-sha256$100000${salt.hex()}${derived.hex()}"
            subprocess.run([str(executable)], check=True, timeout=30,
                           env={**os.environ, "LEONOS_TEST_PASSWORD_HASH": reference})


if __name__ == "__main__":
    unittest.main()
