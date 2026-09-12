#!/usr/bin/env python3
"""Run upstream suites on Linux with the target musl loader and isolated /etc."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", choices=("sudo", "linux-pam"))
    args = parser.parse_args()
    work = ROOT / "build/auth-upstream"
    musl = ROOT / "build/musl/sysroot"
    metadata = json.loads((work / f"{args.package}-build.json").read_text())
    build_directory = Path(metadata.get("build_directory", work / args.package))
    with tempfile.TemporaryDirectory(prefix="suite-", dir=work) as tmp:
        etc = Path(tmp) / "etc"
        etc.mkdir()
        (etc / "passwd").write_text("root:x:0:0::/root:/bin/sh\nnobody:!:65534:65534::/:/bin/false\n")
        (etc / "group").write_text("root:x:0:\nnobody:x:65534:\n")
        pam_services = etc / "pam.d"
        pam_services.mkdir()
        (pam_services / "other").write_text("".join(
            f"{kind} required {work}/root/lib/security/pam_deny.so\n"
            for kind in ("auth", "account", "password", "session")))
        command = ["bwrap", "--unshare-user", "--uid", "0", "--gid", "0", "--unshare-pid",
                   "--unshare-net", "--die-with-parent", "--ro-bind", "/", "/",
                   "--bind", str(work), str(work), "--bind", str(etc), "/etc",
                   "--ro-bind", str(musl / "lib/libc.so"), str(Path("/lib/ld-musl-x86_64.so.1").resolve()),
                   "--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp"]
        isolation = command.copy()
        # Shell wrappers launch target ELF programs by their PT_INTERP path.
        env = {**os.environ, **metadata["environment"],
               "LD_LIBRARY_PATH": f"{work}/root/lib:{musl}/lib"}
        if args.package == "sudo":
            command += ["make", "-C", str(work / "sudo"), "-j8", "check"]
        else:
            command += ["meson", "test", "-C", str(build_directory),
                        "--no-rebuild", "--print-errorlogs"]
        log = work / f"{args.package}-upstream-isolated.log"
        with log.open("w") as output:
            result = subprocess.run(command, env=env, stdout=output, stderr=subprocess.STDOUT)
            status = result.returncode
            if args.package == "linux-pam":
                # Meson skips direct target executables without a cross wrapper.
                # Replay those exact test commands on this explicit Linux reference.
                tests = json.loads(subprocess.check_output(
                    ["meson", "introspect", str(build_directory), "--tests"], text=True))
                replayed = 0
                for test in tests:
                    executable = Path(test["cmd"][0])
                    if not executable.is_relative_to(build_directory):
                        continue
                    output.write("\nLinux reference replay: " + test["name"] + "\n$ " +
                                 shlex.join(test["cmd"]) + "\n")
                    output.flush()
                    test_env = {**env, **test["env"]}
                    test_env["LD_LIBRARY_PATH"] += ":" + env["LD_LIBRARY_PATH"]
                    replay = subprocess.run([*isolation, *test["cmd"]],
                        cwd=test["workdir"] or build_directory, env=test_env,
                        stdout=output, stderr=subprocess.STDOUT, timeout=test["timeout"])
                    output.write(f"REPLAY {test['name']}: exit={replay.returncode}\n")
                    status |= replay.returncode != 0
                    replayed += 1
                output.write(f"Direct target tests replayed on Linux: {replayed}\n")
        print(f"{args.package} upstream Linux reference: exit={status} log={log}")
        raise SystemExit(status)


if __name__ == "__main__":
    main()
