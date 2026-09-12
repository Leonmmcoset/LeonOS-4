#!/usr/bin/env python3
"""Build pinned musl with recorded platform patches and unmodified mimalloc."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile

from fetch_auth_upstream import verify_source_tree

ROOT = Path(__file__).resolve().parents[1]
REVISIONS = {
    "musl": "9fa28ece75d8a2191de7c5bb53bed224c5947417",
    "mimalloc": "34fbd7e7cd4627424490afe19b20f8066bfc537d",
}


def run(command: list[str], **kwargs) -> None:
    subprocess.run(command, check=True, **kwargs)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 16))
    args = parser.parse_args()
    build_dir, prefix = args.build_dir.resolve(), args.prefix.resolve()
    for name, expected in REVISIONS.items():
        source = ROOT / "third_party" / name
        actual = subprocess.check_output(
            ["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
        if actual != expected:
            raise SystemExit(f"{name}: expected {expected}, found {actual}")
    build_dir.mkdir(parents=True, exist_ok=True)
    patch = ROOT / "patches/musl/0001-enforce-password-file-lock.patch"
    patch_digest = hashlib.sha256(patch.read_bytes()).hexdigest()
    patched_source = build_dir / "sources" / patch_digest / "musl"
    patched_source.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="musl-patch-", dir=build_dir) as temporary:
        temporary = Path(temporary)
        archive = temporary / "upstream.tar"
        with archive.open("wb") as output:
            run(["git", "-C", str(ROOT / "third_party/musl"), "archive", REVISIONS["musl"]], stdout=output)
        expected = temporary / "musl"
        expected.mkdir()
        with tarfile.open(archive) as source:
            source.extractall(expected, filter="data")
        run(["patch", "--batch", "--forward", "-p1", "-i", str(patch)], cwd=expected)
        if patched_source.exists():
            with tarfile.open(archive, "w") as packed:
                packed.add(expected, arcname="musl")
            verify_source_tree(archive, patched_source, False)
        else:
            expected.rename(patched_source)
    stamp = prefix / ".leonos-musl.json"
    previous = json.loads(stamp.read_text()) if stamp.exists() else {}
    patches = [{"path": str(patch.relative_to(ROOT)), "sha256": patch_digest}]
    if previous.get("patches") != patches and (build_dir / "Makefile").exists():
        run(["make", "clean"], cwd=build_dir)
    # No host includes or host libc are used in the resulting runtime.
    env = {**os.environ, "CC": "clang --target=x86_64-linux-musl -fuse-ld=lld",
           "AR": "llvm-ar", "RANLIB": "llvm-ranlib", "LIBCC": "",
           "CFLAGS": "-O2 -fno-stack-protector -mno-avx"}
    run([str(patched_source / "configure"), "--target=x86_64-linux-musl",
         f"--prefix={prefix}", f"--syslibdir={prefix / 'lib'}",
         "--disable-gcc-wrapper"], cwd=build_dir, env=env)
    run(["make", f"-j{args.jobs}"], cwd=build_dir, env=env)
    run(["make", "install"], cwd=build_dir, env=env)
    resource = subprocess.check_output(["clang", "-print-resource-dir"], text=True).strip()
    mimalloc = ROOT / "third_party/mimalloc"
    obj = build_dir / "mimalloc.o"
    run(["clang", "--target=x86_64-linux-musl", "-O2", "-fPIC", "-mno-avx",
         "-fno-stack-protector", "-nostdinc", "-isystem", str(prefix / "include"),
         "-isystem", str(Path(resource) / "include"), "-I", str(mimalloc / "include"),
         "-DMI_LIBC_MUSL=1", "-DMI_MALLOC_OVERRIDE=1", "-DMI_SHARED_LIB_EXPORT=1",
         "-DMI_FREE_USE_PAGEMAP=1",
         "-DMI_BUILD_RELEASE=1", "-DNDEBUG", "-c", str(mimalloc / "src/static.c"),
         "-o", str(obj)])
    run(["ld.lld", "-shared", "--no-undefined", "-soname", "libmimalloc.so.3",
         "-o", str(prefix / "lib/libmimalloc.so.3"), str(obj),
         "-L", str(prefix / "lib"), "-lc"])
    shutil.copy2(mimalloc / "include/mimalloc.h", prefix / "include/mimalloc.h")
    shutil.copy2(obj, prefix / "lib/mimalloc.o")
    for name, filename in (("musl", "COPYRIGHT"), ("mimalloc", "LICENSE")):
        destination = prefix / "share/licenses" / name
        destination.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / "third_party" / name / filename, destination / filename)
    (prefix / ".leonos-musl.json").write_text(
        json.dumps({"sources": REVISIONS, "patches": patches, "target": "x86_64-linux-musl"}, indent=2) + "\n")


if __name__ == "__main__":
    main()
