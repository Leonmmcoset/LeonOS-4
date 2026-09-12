#!/usr/bin/env python3
"""Build fixed official filesystem tools, out-of-tree, against target musl."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

from fetch_auth_upstream import ROOT, fetch, verify_source_tree
from build_auth_upstream import run
from build_musl import REVISIONS

MANIFEST = ROOT / "configs/storage-upstream.json"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=Path, default=ROOT / "build/storage-upstream")
    parser.add_argument("--musl", type=Path, default=ROOT / "build/musl/sysroot")
    parser.add_argument("--util-root", type=Path, default=ROOT / "build/auth-upstream/root")
    parser.add_argument("--linux-headers", type=Path, default=ROOT / "build/linux-6.12-headers/include")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 12))
    args = parser.parse_args()
    work, musl, util = args.work.resolve(), args.musl.resolve(), args.util_root.resolve()
    if not work.is_relative_to(ROOT / "build") or work == ROOT / "build":
        parser.error("--work must be a subdirectory of build/")
    runtime = json.loads((musl / ".leonos-musl.json").read_text())
    if runtime["sources"] != REVISIONS or runtime["target"] != "x86_64-linux-musl":
        parser.error("musl sysroot does not match the repository's fixed runtime")
    for name in ("libuuid.a", "libblkid.a"):
        if not (util / "usr/lib" / name).is_file():
            parser.error(f"build auth-upstream first: missing {name}")
    root = work / "root"
    root.mkdir(parents=True, exist_ok=True)
    resource = subprocess.check_output(["clang", "-print-resource-dir"], text=True).strip()
    compiler = ["clang", "--target=x86_64-linux-musl", f"--sysroot={musl}",
                "--gcc-toolchain=/nonexistent", "-fuse-ld=lld", "--rtlib=compiler-rt",
                "-nostdinc", "-isystem", str(musl / "include"),
                "-isystem", str(Path(resource) / "include"),
                "-idirafter", str(args.linux_headers.resolve()), "-L" + str(musl / "lib")]
    env = {**os.environ, "CC": shlex.join(compiler), "BUILD_CC": "cc",
           "AR": "llvm-ar", "RANLIB": "llvm-ranlib", "STRIP": "llvm-strip",
           "CFLAGS": "-O2 -std=gnu17 -mno-avx -mno-avx2",
           "CPPFLAGS": "-I" + shlex.quote(str(util / "usr/include")),
           "LDFLAGS": "-static -L" + shlex.quote(str(util / "usr/lib")), "LIBS": "",
           "PKG_CONFIG_PATH": "", "PKG_CONFIG_LIBDIR": str(util / "usr/lib/pkgconfig"),
           "PKG_CONFIG_SYSROOT_DIR": str(util)}
    options = {
        "e2fsprogs": ["--with-root-prefix=/usr", "--disable-libuuid", "--disable-libblkid",
            "--disable-elf-shlibs", "--disable-fsck", "--disable-uuidd", "--disable-nls",
            "--disable-fuse2fs", "--without-libarchive", "--with-udev-rules-dir=no",
            "--with-systemd-unit-dir=no", "--with-crond-dir=no"],
        "dosfstools": ["--enable-compat-symlinks"],
        "exfatprogs": ["--disable-shared", "--enable-static"],
    }
    manifest = json.loads(MANIFEST.read_text())
    for package, entry in manifest.items():
        source = fetch(package, entry, ROOT / "buildsystem/deps/storage", work / "src")
        output = work / package
        if output.exists():
            shutil.rmtree(output)
        output.mkdir()
        log = work / f"{package}.log"
        commands = [[str(source / "configure"), "--host=x86_64-linux-musl", "--prefix=/usr",
                     "--sbindir=/usr/sbin", "--libdir=/usr/lib", "--sysconfdir=/etc",
                     *options[package]], ["make", f"-j{args.jobs}"],
                    ["make", "install", f"DESTDIR={root}"]]
        if package == "exfatprogs":
            # libtool -static only covers libtool libraries, while -all-static
            # also keeps libc and libblkid in the executable.
            commands[1].extend(["LDFLAGS=" + env["LDFLAGS"] + " -all-static",
                                "BLKID_LIBS=" + str(util / "usr/lib/libblkid.a")])
        for command in commands:
            run(command, output, env, log)
        archive = ROOT / "buildsystem/deps/storage" / entry["url"].rsplit("/", 1)[1]
        verify_source_tree(archive, source, False)
        license_dir = root / "usr/share/licenses" / package
        license_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / entry["license_file"], license_dir)
        metadata = {"source": entry, "commands": commands, "compiler": compiler,
                    "environment": {key: env[key] for key in ("CFLAGS", "CPPFLAGS", "LDFLAGS",
                        "PKG_CONFIG_LIBDIR", "PKG_CONFIG_SYSROOT_DIR")},
                    "patches": [], "target": "x86_64-linux-musl", "guest_verified": False}
        (work / f"{package}-build.json").write_text(json.dumps(metadata, indent=2) + "\n")
    for name in ("mkfs.ext2", "fsck.ext2", "mkfs.fat", "fsck.fat", "mkfs.exfat", "fsck.exfat"):
        binary = root / "usr/sbin" / name
        if not binary.is_file():
            raise RuntimeError(f"missing official filesystem tool: {binary}")
        headers = subprocess.check_output(["readelf", "-l", "-d", str(binary)], text=True)
        if "(NEEDED)" in headers or "INTERP" in headers:
            raise RuntimeError(f"filesystem tool is not a self-contained target ELF: {binary}")
    (root / ".storage-package.json").write_text(json.dumps({
        "sources": manifest, "guest_verified": False,
        "files": {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in sorted(root.rglob("*")) if p.is_file() and not p.is_symlink()
                  and p.name != ".storage-package.json"},
    }, indent=2) + "\n")


if __name__ == "__main__":
    main()
