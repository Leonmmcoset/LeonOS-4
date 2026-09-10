#!/usr/bin/env python3
"""Execute the packaged aliases, headers, assembler, archives and static linker."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

from package_musl_gcc import ARCHIVE_SHA256, COMMANDS, ROOT, digest


def verify(root: Path) -> None:
    manifest = json.loads((root / ".leonos-package.json").read_text())
    assert manifest["archive_sha256"] == ARCHIVE_SHA256
    for name, expected in manifest["upstream_files"].items():
        assert digest(root / name) == expected, name
    launcher = root / "opt/dyne/bin/leonos-musl-cc"
    assert launcher.is_file(), launcher
    with tempfile.TemporaryDirectory(prefix="leonos-gcc-package-") as directory:
        work = Path(directory)
        bin_dir = work / "bin"
        bin_dir.mkdir()
        for name in COMMANDS:
            (bin_dir / name).symlink_to(launcher)
        for name in COMMANDS:
            result = subprocess.run([bin_dir / name, "--version"], capture_output=True, timeout=30)
            assert result.returncode == 0, (name, result.stdout, result.stderr)

        def run(name, *args):
            return subprocess.check_output([str(bin_dir / name), *map(str, args)], cwd=work,
                                           stderr=subprocess.STDOUT, timeout=60)

        (work / "hello.c").write_text('#include <stdio.h>\nint main(void) { puts("PACKAGED_GCC_OK"); return 0; }\n')
        for compiler in ("gcc", "cc", "musl-gcc", "x86_64-linux-musl-gcc"):
            run(compiler, "-static", "hello.c", "-o", "hello")
            assert subprocess.check_output([work / "hello"]) == b"PACKAGED_GCC_OK\n"
            headers = run("readelf", "-l", "-d", "hello")
            assert b"INTERP" not in headers and b"(NEEDED)" not in headers
        (work / "hello.cc").write_text('#include <iostream>\nint main() { std::cout << "PACKAGED_CXX_OK\\n"; }\n')
        run("g++", "-static", "hello.cc", "-o", "hello-cxx")
        assert subprocess.check_output([work / "hello-cxx"]) == b"PACKAGED_CXX_OK\n"
        (work / "start.s").write_text('.global _start\n.text\n_start:\n mov $60,%eax\n xor %edi,%edi\n syscall\n')
        run("as", "start.s", "-o", "start.o")
        run("ar", "rcs", "libstart.a", "start.o")
        run("ranlib", "libstart.a")
        run("ld", "-static", "-o", "asm", "--whole-archive", "libstart.a", "--no-whole-archive")
        subprocess.run([work / "asm"], check=True)
        assert b"_start" in run("nm", "asm")
        run("objcopy", "asm", "asm-copy")
        run("strip", "asm-copy")
        subprocess.run([work / "asm-copy"], check=True)
        headers = root / "opt/dyne/gcc-musl/x86_64-linux-musl/include"
        for header in ("linux/netfilter/xt_CONNMARK.h", "linux/netfilter/xt_connmark.h"):
            assert (headers / header).is_file()
            (work / "header.c").write_text(f"#include <{header}>\n")
            run("cpp", "header.c", "-o", "header.i")
        subprocess.run(["cc", "-D_GNU_SOURCE", "-O1", "-g", "-fsanitize=address,undefined",
                        "-I" + str(ROOT / "include"), "-I" + str(ROOT / "include/uapi"),
                        str(ROOT / "tools/tests/installer_directory_test.c"),
                        "-o", str(work / "installer-directory")], check=True)
        subprocess.run([work / "installer-directory"], check=True)
    print("PASS packaged static musl GCC: all aliases, C/C++, as/ld/ar/ranlib/nm/objcopy/strip, case-sensitive headers, upstream hashes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    verify(parser.parse_args().root.resolve())
