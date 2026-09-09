#!/usr/bin/env python3
"""Compiler driver for the relocatable LeonOS musl SDK."""

import os
from pathlib import Path
import subprocess
import sys

sdk = Path(__file__).resolve().parents[1]
args = sys.argv[1:]
compiler = os.environ.get("LEONOS_CC", "clang")
if any(flag in args for flag in ("--version", "-dumpmachine", "-dumpversion", "-print-resource-dir")):
    sys.exit(subprocess.call([compiler, "--target=x86_64-linux-musl", *args]))
resource = subprocess.check_output([compiler, "-print-resource-dir"], text=True).strip()
command = [compiler, "--target=x86_64-linux-musl", "-fuse-ld=lld", "-nostdlib",
           "-nostdinc", "-isystem", str(sdk / "include"),
           "-isystem", str(Path(resource) / "include"),
           "-D_GNU_SOURCE", "-DLEONOS_USE_MUSL", "-mno-avx", "-mno-avx2"]
compiling_only = any(flag in args for flag in ("-c", "-S", "-E", "-fsyntax-only", "-r"))
shared = "-shared" in args
static = "-static" in args
if not compiling_only:
    if not shared:
        command += [str(sdk / "lib" / ("crt1.o" if static else "Scrt1.o")),
                    str(sdk / "lib/crti.o")]
        command += ["-Wl,--image-base=0x4000000"] if static else [
            "-pie", "-Wl,--dynamic-linker=/lib/ld-musl-x86_64.so.1",
            "-Wl,-rpath,/system/lib:/lib"]
    command += args + ["-L" + str(sdk / "lib")]
    if static:
        command += [str(sdk / "lib/mimalloc.o"), "-Wl,--start-group", "-lleonos", "-lc",
                    "-Wl,--end-group"]
    else:
        command += ["-l:libmimalloc.so.3", "-l:libleonos.so.2", "-lc"]
    if not shared:
        command += [str(sdk / "lib/crtn.o")]
else:
    command += args
sys.exit(subprocess.call(command))
