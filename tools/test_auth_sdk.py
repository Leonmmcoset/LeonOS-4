#!/usr/bin/env python3
"""Compile the real PAM runtime probe using only the delivered SDK headers/libs."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
work = ROOT / "build/auth-upstream"
with tempfile.TemporaryDirectory(prefix="sdk-check-", dir=work) as directory:
    extracted = Path(directory)
    with zipfile.ZipFile(ROOT / "LeonOS4-Developer-SDK.zip") as archive:
        if len(archive.namelist()) != len(set(archive.namelist())):
            raise SystemExit("duplicate SDK members")
        archive.extractall(extracted)
    sdk = extracted / "devtools"
    recipe = json.loads((sdk / "THIRD_PARTY/LINUX-PAM-BUILD.json").read_text())
    assert recipe["source"]["version"] == "1.7.2"
    crypt_recipe = json.loads((sdk / "THIRD_PARTY/LIBXCRYPT-BUILD.json").read_text())
    assert crypt_recipe["source"]["version"] == "4.5.2"
    assert "CRYPT_GENSALT_IMPLEMENTS_AUTO_ENTROPY" in (sdk / "include/crypt.h").read_text()
    environment = {**os.environ, "PKG_CONFIG_LIBDIR": str(sdk / "lib/pkgconfig"),
                   "PKG_CONFIG_PATH": "", "PKG_CONFIG_SYSROOT_DIR": ""}
    flags = subprocess.check_output(["pkg-config", "--cflags", "--libs", "pam", "pam_misc", "pamc"],
                                    env=environment, text=True)
    subprocess.run(["python3", str(sdk / "bin/leonos-musl-cc"), "-O2",
                    str(ROOT / "tools/tests/pam_runtime_probe.c"), *shlex.split(flags),
                    "-o", str(work / "pam-sdk-runtime.elf")], check=True)
    dynamic = subprocess.check_output(["readelf", "-d", str(work / "pam-sdk-runtime.elf")], text=True)
    for name in ("libpam.so.0", "libpam_misc.so.0", "libpamc.so.0"):
        assert name in dynamic, (name, dynamic)
    crypt_flags = subprocess.check_output(["pkg-config", "--cflags", "--libs", "libcrypt"],
                                         env=environment, text=True)
    subprocess.run(["python3", str(sdk / "bin/leonos-musl-cc"), "-x", "c", "-",
                    *shlex.split(crypt_flags), "-o", str(work / "crypt-sdk-runtime.elf")],
                   input="#include <crypt.h>\nint main(void) { char b[CRYPT_GENSALT_OUTPUT_SIZE]; "
                         "return crypt_gensalt_rn(\"$6$\", 0, 0, 0, b, sizeof(b)) == 0; }\n",
                   text=True, check=True)
    dynamic = subprocess.check_output(["readelf", "-d", str(work / "crypt-sdk-runtime.elf")], text=True)
    assert "libcrypt.so.2" in dynamic
    print("PASS extracted SDK: PAM public headers, relocatable pkg-config, all three DSOs and probe link")
    print(f"Guest-ready probe: {work / 'pam-sdk-runtime.elf'}")
