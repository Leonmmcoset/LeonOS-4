#!/usr/bin/env python3
"""Build pinned upstream PAM/sudo/su into an isolated musl staging root.

This target does not activate privileged executables in a system image.
Guest prerequisite gates and consumer migration are tracked separately.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tarfile
import tempfile

from fetch_auth_upstream import MANIFEST, ROOT, fetch, verify_source_tree
from build_musl import REVISIONS
from build_musl_ltp import SOURCES


def util_linux_configure_args() -> list[str]:
    return ["--disable-all-programs", "--enable-su", "--enable-runuser",
            "--enable-libuuid", "--enable-libfdisk", "--enable-libsmartcols",
            "--enable-fdisks=check", "--enable-libblkid", "--enable-libmount",
            "--enable-mount", "--enable-blkid", "--enable-lsblk", "--enable-fsck",
            "--without-python", "--without-systemd",
            "--without-systemdsystemunitdir", "--disable-makeinstall-chown"]


def run(command: list[str], cwd: Path, env: dict, log: Path) -> None:
    print(shlex.join(command), flush=True)
    with log.open("a") as output:
        output.write("\n$ " + shlex.join(command) + "\n")
        output.flush()
        result = subprocess.run(command, cwd=cwd, env=env, stdout=output,
                                stderr=subprocess.STDOUT)
    if result.returncode:
        raise SystemExit(f"exit {result.returncode}: {log}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("packages", nargs="*")
    parser.add_argument("--work", type=Path, default=ROOT / "build/auth-upstream")
    parser.add_argument("--musl", type=Path, default=ROOT / "build/musl/sysroot")
    parser.add_argument("--linux-headers", type=Path, default=ROOT / "build/linux-6.12-headers/include")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 16))
    args = parser.parse_args()
    order = ("libxcrypt", "linux-pam", "libmd", "libbsd", "sudo", "util-linux", "shadow")
    if any(name not in order for name in args.packages):
        parser.error("packages must be " + ", ".join(order))
    work, musl = args.work.resolve(), args.musl.resolve()
    if not work.is_relative_to(ROOT / "build") or work == ROOT / "build":
        parser.error("--work must be a subdirectory of build/")
    root = work / "root"
    root.mkdir(parents=True, exist_ok=True)
    runtime = json.loads((musl / ".leonos-musl.json").read_text())
    if runtime["sources"] != REVISIONS or runtime["target"] != "x86_64-linux-musl":
        parser.error("musl sysroot does not match the repository's fixed runtime")
    filename, url, digest, directory = next(item for item in SOURCES if item[0] == "linux-6.12.tar.xz")
    linux = fetch("linux-headers", {"url": url, "version": "6.12", "sha256": digest,
                  "directory": directory, "license_file": "COPYING"}, ROOT / "build", work / "src",
                  allow_generated=True)
    header_prefix = args.linux_headers.resolve().parent
    run(["make", "-C", str(linux), "ARCH=x86_64", "headers_install", f"INSTALL_HDR_PATH={header_prefix}"],
        work, os.environ.copy(), work / "linux-headers.log")
    manifest = json.loads(MANIFEST.read_text())
    resource = Path(subprocess.check_output(["clang", "-print-resource-dir"], text=True).strip())
    compiler = ["clang", "--target=x86_64-linux-musl", f"--sysroot={musl}",
                "--gcc-toolchain=/nonexistent", "-fuse-ld=lld", "--rtlib=compiler-rt",
                "-nostdinc", "-isystem", str(musl / "include"),
                "-isystem", str(resource / "include"), "-idirafter", str(args.linux_headers.resolve())]
    env = {**os.environ, "CC": shlex.join(compiler), "AR": "llvm-ar", "RANLIB": "llvm-ranlib",
           "CFLAGS": "-O2 -mno-avx -mno-avx2", "CPPFLAGS": "", "LIBS": "",
           "LDFLAGS": "-Wl,-z,relro,-z,now", "PKG_CONFIG_PATH": "",
           "PKG_CONFIG_LIBDIR": str(root / "lib/pkgconfig") + ":" + str(root / "usr/lib/pkgconfig"),
           "PKG_CONFIG_SYSROOT_DIR": str(root)}
    cross = work / "musl.ini"
    # Meson machine files use Python-style string literals, not shell words.
    cross.write_text("[binaries]\n" +
        f"c = {compiler!r}\nar = 'llvm-ar'\nstrip = 'llvm-strip'\npkg-config = 'pkg-config'\n" +
        "[host_machine]\nsystem = 'linux'\ncpu_family = 'x86_64'\ncpu = 'x86_64'\nendian = 'little'\n" +
        "[properties]\nneeds_exe_wrapper = true\n" +
        f"pkg_config_libdir = {[str(root / 'lib/pkgconfig'), str(root / 'usr/lib/pkgconfig')]!r}\n" +
        f"sys_root = {str(root)!r}\n" +
        "[built-in options]\nc_args = ['-O2', '-mno-avx', '-mno-avx2']\n")
    selected = set(args.packages or order)
    if selected & {"sudo", "util-linux", "shadow"}:
        selected.add("linux-pam")
    if "linux-pam" in selected:
        selected.add("libxcrypt")
    if "shadow" in selected:
        selected.add("libbsd")
    if "libbsd" in selected:
        selected.add("libmd")
    packages = [name for name in order if name in selected]
    for package in packages:
        source = fetch(package, manifest[package], ROOT / "buildsystem/deps/auth", work / "src")
        patches = []
        if package == "linux-pam":
            patch_files = sorted((ROOT / "patches/linux-pam").glob("*.patch"))
            patches = [{"path": str(patch.relative_to(ROOT)),
                        "sha256": hashlib.sha256(patch.read_bytes()).hexdigest()}
                       for patch in patch_files]
            patch_digest = hashlib.sha256(b"".join(patch.read_bytes() for patch in patch_files)).hexdigest()
            adapted = work / "adapted" / f"linux-pam-{patch_digest}"
            adapted.mkdir(parents=True, exist_ok=True)
            patched_source = adapted / source.name
            with tempfile.TemporaryDirectory(prefix="patch-check-", dir=adapted) as temporary:
                expected = Path(temporary) / source.name
                shutil.copytree(source, expected, symlinks=True)
                for patch in patch_files:
                    subprocess.run(["patch", "--batch", "--forward", "--fuzz=0", "-p1", "-i", str(patch)],
                                   cwd=expected, check=True)
                if patched_source.exists():
                    source_archive = Path(temporary) / "expected.tar"
                    with tarfile.open(source_archive, "w") as packed:
                        packed.add(expected, arcname=expected.name)
                    verify_source_tree(source_archive, patched_source, False)
                else:
                    expected.rename(patched_source)
            source = patched_source
        output = work / package
        if patches:
            output = work / f"linux-pam-{patch_digest[:16]}"
        output.mkdir(exist_ok=True)
        log = work / f"{package}.log"
        package_env = env.copy()
        if package in ("libbsd", "shadow"):
            # libbsd's own -isystem overlay must precede the underlying libc
            # headers, including the include_next target.
            overlay_compiler = compiler.copy()
            overlay_compiler[overlay_compiler.index(str(musl / "include")) - 1] = "-idirafter"
            package_env["CC"] = shlex.join(overlay_compiler)
        if package == "linux-pam":
            configure = ["meson", "setup", str(output), str(source), "--cross-file", str(cross),
                         "--prefix=/usr", "--libdir=/lib", "--sbindir=/sbin", "--sysconfdir=/etc",
                         "--localstatedir=/var", "-Dsecuredir=/lib/security", "-Dpam_unix=enabled",
                         "-Dvendordir=", "-Ddocs=disabled"]
            if (output / "build.ninja").exists():
                configure += ["--reconfigure", "--clearcache"]
            commands = [configure, ["meson", "compile", "-C", str(output), f"-j{args.jobs}"],
                        ["meson", "install", "-C", str(output), "--destdir", str(root)]]
        else:
            package_env["CPPFLAGS"] = "-I" + shlex.quote(str(root / "usr/include"))
            package_env["LDFLAGS"] += " -L" + shlex.quote(str(root / "lib"))
            package_env["LDFLAGS"] += " -Wl,-rpath-link," + shlex.quote(str(root / "lib"))
            configure = [str(source / "configure"), "--host=x86_64-linux-musl", "--prefix=/usr",
                         "--sysconfdir=/etc", "--localstatedir=/var", "--libdir=/usr/lib"]
            if package == "sudo":
                configure += ["--with-pam", "--with-pam-login", "--with-secure-path=/usr/sbin:/usr/bin:/sbin:/bin",
                              "--with-rundir=/run/sudo", "--with-vardir=/var/lib/sudo",
                              "--libexecdir=/usr/lib"]
            elif package == "libxcrypt":
                configure += ["--libdir=/lib", "--enable-hashes=all", "--enable-obsolete-api=no"]
            elif package in ("libmd", "libbsd"):
                configure += ["--libdir=/lib"]
            elif package == "shadow":
                configure += ["--bindir=/bin", "--sbindir=/sbin", "--with-libpam",
                              "--with-yescrypt", "--without-su", "--disable-logind"]
            else:
                # libtool adds -L/usr/lib when relinking libfdisk at install
                # time. Put the target libc search path in CC, ahead of those
                # generated flags, so -lpthread/-lc cannot pick host glibc.
                package_env["CC"] += " -L" + shlex.quote(str(musl / "lib"))
                # 2.41.6 hook_idmap uses RESOLVE_NO_SYMLINKS without including
                # its defining Linux header. Use the real UAPI constants in
                # every translation unit, keeping upstream sources unchanged.
                package_env["CPPFLAGS"] += " -include linux/openat2.h"
                configure += ["--sbindir=/usr/sbin", *util_linux_configure_args()]
            install = ["make", "install", f"DESTDIR={root}"]
            if package == "sudo":
                install.append("INSTALL_OWNER=")
            commands = [configure, ["make", f"-j{args.jobs}"], install]
        for command in commands:
            run(command, output, package_env, log)
        if package in ("libmd", "libbsd"):
            # Installed libtool archives contain absolute target dependency
            # paths. Link consumers through the ELF libraries/pkg-config.
            (root / "lib" / f"{package}.la").unlink()
        image_permissions = {}
        if package == "linux-pam":
            # unix_chkpwd.8 requires setuid root or setgid shadow. Our shadow
            # files are root:root 0600, so use the documented setuid form.
            image_permissions["/sbin/unix_chkpwd"] = {"uid": 0, "gid": 0, "mode": "4755"}
            (root / "sbin/unix_chkpwd").chmod(0o4755)
        license_dir = root / "usr/share/licenses" / package
        license_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / manifest[package]["license_file"], license_dir)
        if package == "libxcrypt":
            shutil.copy2(source / "LICENSING", license_dir)
        if package == "shadow" and (source / "LICENSES").is_dir():
            shutil.copytree(source / "LICENSES", license_dir / "LICENSES", dirs_exist_ok=True)
        (work / f"{package}-build.json").write_text(json.dumps({
            "source": manifest[package], "commands": commands,
            "compiler": shlex.split(package_env["CC"]), "environment": {key: package_env[key] for key in
                ("CFLAGS", "CPPFLAGS", "LDFLAGS", "LIBS", "PKG_CONFIG_LIBDIR", "PKG_CONFIG_SYSROOT_DIR")},
            "patches": patches, "build_directory": str(output),
            "image_permissions": image_permissions,
            "target": "x86_64-linux-musl", "guest_verified": False,
            "notes": ["PAM docs disabled: XML publishing tools are not runtime dependencies.",
                      "PAM vendordir disabled: service configuration has one /etc authority.",
                      "Optional dependencies auto-detected inside target staging root only.",
                      "Staging files belong to the build user; image packaging must enforce root ownership and modes.",
                      "util-linux builds su, runuser, fdisk, sfdisk, mount, umount, blkid, lsblk, fsck and their target libraries; cfdisk is skipped without a terminal library."],
        }, indent=2) + "\n")
        print(f"BUILT {package}: {root}; log={log}", flush=True)

    if "linux-pam" in packages:
        sources = [ROOT / "userland/pam/pam_leonos_password.c",
                   ROOT / "userland/libc/src/auth_password.c"]
        module = root / "lib/security/pam_leonos_password.so"
        command = [*compiler, "-O2", "-fPIC", "-shared", "-Wl,-z,relro,-z,now",
                   "-Wl,--no-undefined", "-I" + str(root / "usr/include"),
                   "-I" + str(ROOT / "include"), "-I" + str(ROOT / "include/uapi"),
                   *map(str, sources), "-L" + str(root / "lib"), "-lpam", "-o", str(module)]
        run(command, work, env, work / "pam-leonos-password.log")
        (work / "pam-leonos-password-build.json").write_text(json.dumps({
            "command": command, "target": "x86_64-linux-musl",
            "sources": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                        for path in sources},
            "module": str(module),
            "purpose": "Shared installer/GUI/PAM new-password product rule only; pam_unix owns authentication and storage.",
        }, indent=2) + "\n")


if __name__ == "__main__":
    main()
