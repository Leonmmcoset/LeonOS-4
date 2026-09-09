#!/usr/bin/env python3
"""Build unmodified upstream terminal packages for Linux x86-64 musl."""

import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
REVISIONS = {
    "ncurses": "0096bd402c4a9c8f39bd7ed266e1b8920327e4d8",
    "vim": "af9a7a04f18693eee4400dd134135527f4e8cd5f",
}


def build(package: str, work: Path, prefix: Path, musl: Path,
          ncurses: Path | None, jobs: int) -> None:
    source = ROOT / "third_party" / package
    revision = subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if revision != REVISIONS[package]:
        raise SystemExit(f"{package}: expected {REVISIONS[package]}, found {revision}")
    for directory in (work, prefix):
        if not directory.is_relative_to(ROOT / "build") or directory == ROOT / "build":
            raise SystemExit("terminal package outputs must be below build/")
        if directory.exists():
            shutil.rmtree(directory)
        directory.mkdir(parents=True)
    resource = subprocess.check_output(["clang", "-print-resource-dir"], text=True).strip()
    compiler = shlex.join([
        "clang", "--target=x86_64-linux-musl", f"--sysroot={musl}",
        "--gcc-toolchain=/nonexistent", "-fuse-ld=lld", "--rtlib=compiler-rt",
        "-nostdinc", "-isystem", str(musl / "include"),
        "-isystem", str(Path(resource) / "include"),
    ])
    env = {**os.environ, "CC": compiler, "AR": "llvm-ar", "RANLIB": "llvm-ranlib",
           "CFLAGS": "-O2 -fno-stack-protector -mno-avx -mno-avx2",
           "CPPFLAGS": "", "LDFLAGS": "-static", "LIBS": ""}
    common = ["--host=x86_64-linux-musl", "--prefix=/usr"]
    if package == "ncurses":
        configure = [str(source / "configure"), *common,
                     "--with-normal", "--without-shared", "--without-debug",
                     "--enable-widec", "--with-termlib", "--enable-overwrite",
                     "--without-ada", "--without-cxx-binding", "--without-tests",
                     "--with-terminfo-dirs=/usr/share/terminfo:/lib/terminfo",
                     "--enable-mixed-case"]
        cwd = work
    else:
        if ncurses is None:
            raise SystemExit("Vim requires the ncurses install prefix")
        # Vim's supported build runs in its own tree; never configure a submodule in place.
        shutil.copytree(source, work / "source", ignore=shutil.ignore_patterns(".git"))
        cwd = work / "source"
        env["CPPFLAGS"] = "-I" + shlex.quote(str(ncurses / "include"))
        env["LDFLAGS"] += " -L" + shlex.quote(str(ncurses / "lib"))
        env["LIBS"] = "-lncursesw -ltinfow " + shlex.quote(str(musl / "lib/mimalloc.o"))
        configure = ["./configure", *common, "--with-features=normal",
                     "--enable-gui=no", "--without-wayland", "--without-x",
                     "--with-tlib=ncursesw", "--disable-nls", "--disable-gpm",
                     "--disable-pythoninterp", "--disable-python3interp",
                     "--disable-perlinterp", "--disable-rubyinterp",
                     "--disable-luainterp", "--disable-tclinterp"]
    for command in (configure, ["make", f"-j{jobs}"],
                    ["make", "install", f"DESTDIR={prefix.parent}"]):
        subprocess.run(command, cwd=cwd, env=env, check=True)
    if package == "ncurses":
        # FAT32/exFAT installer media are case-insensitive.  Keep one
        # deterministic entry for aliases that differ only by case; Linux
        # applications use the canonical lowercase terminfo names.
        terminfo = prefix / "share/terminfo"
        seen: set[str] = set()
        for terminfo_file in sorted((path for path in terminfo.rglob("*") if path.is_file()),
                                    key=lambda path: path.relative_to(terminfo).as_posix()):
            key = terminfo_file.relative_to(terminfo).as_posix().casefold()
            if key in seen:
                terminfo_file.unlink()
            else:
                seen.add(key)
    license_name = "COPYING" if package == "ncurses" else "LICENSE"
    licenses = prefix / "share/licenses" / package
    licenses.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source / license_name, licenses / license_name)
    (prefix / ".leonos-package.json").write_text(json.dumps({
        "package": package, "revision": revision, "target": "x86_64-linux-musl",
        "linkage": "static", "configure": configure[1:],
    }, indent=2) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", choices=REVISIONS)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--musl", type=Path, required=True)
    parser.add_argument("--ncurses", type=Path)
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 16))
    args = parser.parse_args()
    if args.prefix.name != "usr":
        parser.error("--prefix must end in usr (the /usr installation under DESTDIR)")
    build(args.package, args.work.resolve(), args.prefix.resolve(), args.musl.resolve(),
          args.ncurses.resolve() if args.ncurses else None, args.jobs)


if __name__ == "__main__":
    main()
