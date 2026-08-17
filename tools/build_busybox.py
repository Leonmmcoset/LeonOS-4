#!/usr/bin/env python3
"""Build a small static BusyBox for the LeonOS Picolibc userland."""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
WORK_PREFIX = "leonos4-busybox-"


# BusyBox's upstream libbb/Kbuild.src compiles a broad support library even
# when allnoconfig selects only a few applets. Keep the source-copy profile
# small; the LeonOS shim supplies the limited signal-mask helpers Ash needs.
MINIMAL_LIBBB_OBJECTS = (
    "appletlib.o",
    "ask_confirmation.o",
    "auto_string.o",
    "bb_cat.o",
    "leonos_shim.o",
    "bb_pwd.o",
    "bb_qsort.o",
    "bb_strtonum.o",
    "chomp.o",
    "compare_string_array.o",
    "concat_path_file.o",
    "concat_subpath_file.o",
    "const_hack.o",
    "copy_file.o",
    "copyfd.o",
    "default_error_retval.o",
    "common_bufsiz.o",
    "endofname.o",
    "fflush_stdout_and_exit.o",
    "full_write.o",
    "fclose_nonstdin.o",
    "get_last_path_component.o",
    "get_line_from_file.o",
    "getopt32.o",
    "getopt_allopts.o",
    "human_readable.o",
    "inode_hash.o",
    "isqrt.o",
    "iterate_on_dir.o",
    "last_char_is.o",
    "llist.o",
    "make_directory.o",
    "messages.o",
    "mode_string.o",
    "parse_mode.o",
    "nuke_str.o",
    "perror_msg.o",
    "perror_nomsg.o",
    "perror_nomsg_and_die.o",
    "print_flags.o",
    "print_numbered_lines.o",
    "printable.o",
    "printable_string.o",
    "process_escape_sequence.o",
    "ptr_to_globals.o",
    "read.o",
    "recursive_action.o",
    "remove_file.o",
    "safe_write.o",
    "safe_strncpy.o",
    "single_argv.o",
    "skip_whitespace.o",
    "verror_msg.o",
    "vfork_daemon_rexec.o",
    "xatonum.o",
    "xfunc_die.o",
    "xfuncs.o",
    "xfuncs_printf.o",
    "xgetcwd.o",
    "xreadlink.o",
    "xrealloc_vector.o",
    "xregcomp.o",
    "wfopen_input.o",
    "wfopen.o",
    "read_key.o",
    # Ash command-line editing and tab completion are enabled in the LeonOS
    # profile; keep the object in the deliberately small libbb source set.
    "lineedit.o",
    "lineedit_ptr_hack.o",
    # Fancy prompt expansion uses BusyBox's time formatter and hostname
    # helper; these are normally pulled in by the complete libbb archive.
    "safe_gethostname.o",
    "time.o",
    "safe_poll.o",
    "read_printf.o",
    # kill.c uses the shared process scanner for killall-compatible paths;
    # the LeonOS profile only enables kill, but the object still supplies the
    # common scanner symbols referenced by the applet.
    "procps.o",
    "bb_getgroups.o",
    "u_signal_names.o",
)

def run(command: list[str], *, cwd: Path | None = None) -> None:
    subprocess.run(command, cwd=cwd, check=True)


def source_revision(source: Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "HEAD"],
        check=True,
        text=True,
        capture_output=True,
    )
    return result.stdout.strip()


def copy_tree(source: Path, destination: Path) -> None:
    shutil.copytree(source, destination, ignore=shutil.ignore_patterns(".git"))


def archive_members(archive: Path) -> list[str]:
    result = subprocess.run(
        ["llvm-ar", "t", str(archive)], check=True, text=True, capture_output=True
    )
    return [line for line in result.stdout.splitlines() if line]


def merge_static_archives(output: Path, archives: list[Path], work_dir: Path) -> None:
    if work_dir.exists():
        shutil.rmtree(work_dir)
    work_dir.mkdir()
    members: list[Path] = []
    for archive in archives:
        names = archive_members(archive)
        run(["llvm-ar", "x", str(archive)], cwd=work_dir)
        members.extend(work_dir / name for name in names)
    run(["llvm-ar", "rcs", str(output), *map(str, members)])


def source_cache_key(revision: str) -> str:
    digest = hashlib.sha256()
    digest.update(revision.encode("ascii"))
    # The source copy is modified by this script and the LeonOS adapter, so
    # an upstream revision alone cannot identify a reusable copy.
    for path in (
        Path(__file__),
        ROOT / "userland/busybox/leonos_shim.c",
        ROOT / "userland/busybox/leonos.config",
    ):
        digest.update(path.read_bytes())
    return digest.hexdigest()


def cached_source(source: Path, cache_key: str) -> Path:
    cache_root = Path(tempfile.gettempdir()) / f"{WORK_PREFIX}{cache_key[:20]}"
    marker = cache_root / ".leonos-source-cache-key"
    existing_key = marker.read_text(encoding="ascii").strip() if marker.is_file() else ""
    if existing_key != cache_key:
        if cache_root.exists():
            shutil.rmtree(cache_root)
        cache_root.mkdir(parents=True)
        copy_tree(source, cache_root / "source")
        marker.write_text(cache_key + "\n", encoding="ascii")
    return cache_root / "source"


def write_minimal_libbb_kbuild(kbuild: Path, generated: bool) -> None:
    body = [
        "# Generated by tools/build_busybox.py for the LeonOS basic applet profile." if generated
        else "# BusyBox source-copy override for the LeonOS basic applet profile.",
        "# The upstream source tree is never modified; this file is in the /tmp copy.",
        "lib-y :=",
        "",
    ]
    body.extend(f"lib-y += {name}" for name in MINIMAL_LIBBB_OBJECTS)
    body.append("")
    kbuild.write_text("\n".join(body), encoding="utf-8")


def trim_libbb(source: Path) -> None:
    shutil.copyfile(ROOT / "userland/busybox/leonos_shim.c", source / "libbb/leonos_shim.c")
    patch_time_for_leonos(source)
    write_minimal_libbb_kbuild(source / "libbb/Kbuild.src", False)
    allowed = {Path(name).stem for name in MINIMAL_LIBBB_OBJECTS}
    for path in (source / "libbb").rglob("*.c"):
        if path.stem in allowed:
            continue
        text = path.read_text(encoding="utf-8")
        # scripts/generate_BUFSIZ.sh and the Kbuild generator discover these
        # annotations from source files and append them after Kbuild.src.
        text = text.replace("//kbuild:", "//leonos-kbuild:")
        path.write_text(text, encoding="utf-8")


def patch_optional_config_macros(source: Path) -> None:
    """Keep disabled dependent Kconfig symbols usable in C expressions.

    BusyBox normally emits ENABLE_* for every symbol.  Its Kconfig model
    suppresses FEATURE_PS_ADDITIONAL_COLUMNS when DESKTOP is disabled, while
    libbb.h still uses the symbol in an enum expression.  The LeonOS profile
    intentionally has no DESKTOP ps features, so provide the canonical zero
    fallback in the temporary source tree.
    """
    path = source / "include/libbb.h"
    text = path.read_text(encoding="utf-8")
    marker = "#include \"platform.h\"\n"
    fallback = (
        marker
        + "\n#ifndef ENABLE_FEATURE_PS_ADDITIONAL_COLUMNS\n"
          "#define ENABLE_FEATURE_PS_ADDITIONAL_COLUMNS 0\n"
          "#endif\n"
    )
    if "#define ENABLE_FEATURE_PS_ADDITIONAL_COLUMNS 0" not in text:
        if marker not in text:
            raise SystemExit("unable to add BusyBox ps config fallback")
        text = text.replace(marker, fallback, 1)
        path.write_text(text, encoding="utf-8")


def patch_ps_for_leonos(source: Path) -> None:
    """Replace Linux /proc parsing while retaining BusyBox build metadata."""
    path = source / "procps/ps.c"
    path.write_text(r'''/* LeonOS ps port: task snapshots replace Linux /proc parsing. */
//config:config PS
//config:	bool "ps"
//config:	default y
//config:config FEATURE_PS_ADDITIONAL_COLUMNS
//config:	bool "Enable -o rgroup, -o ruser, -o nice specifiers"
//config:	default n
//config:	depends on PS && DESKTOP
//applet:IF_PS(APPLET_NOEXEC(ps, ps, BB_DIR_BIN, BB_SUID_DROP, ps))
//kbuild:lib-$(CONFIG_PS) += ps.o

//usage:#define ps_trivial_usage
//usage:       ""
//usage:#define ps_full_usage "\\n\\n"
//usage:       "Show list of processes\\n"
//usage:#define ps_example_usage

#include "libbb.h"
#pragma push_macro("stat")
#pragma push_macro("fstat")
#undef stat
#undef fstat
#include <leonos/gui.h>
#pragma pop_macro("fstat")
#pragma pop_macro("stat")

int ps_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
    struct leonos_task_info tasks[LEONOS_TASK_MAX];
    uint64_t tick = 0;
    int count = leonos_task_snapshot(tasks, LEONOS_TASK_MAX, &tick);
    (void)tick;
    if (count < 0) {
        bb_error_msg("unable to read task snapshot");
        return EXIT_FAILURE;
    }
    puts("PID PPID PRI STATE NAME");
    for (int i = 0; i < count; ++i) {
        const char *state = tasks[i].state == 1 ? "RUN" :
                            tasks[i].state == 2 ? "SLEEP" :
                            tasks[i].state == 3 ? "EXIT" : "READY";
        printf("%u %u %d %s %s\n", tasks[i].pid, tasks[i].parent_pid,
               tasks[i].priority, state, tasks[i].name);
    }
    return EXIT_SUCCESS;
}
''', encoding="utf-8")


def patch_less_for_leonos(source: Path) -> None:
    """Keep the pager's file stream separate from the terminal PTY."""
    path = source / "miscutils/less.c"
    text = path.read_text(encoding="utf-8")
    globals_before = "\tint kbd_fd;  /* fd to get input from */\n"
    globals_after = (
        globals_before
        + "\tint input_fd; /* fd supplying the displayed file */\n"
    )
    if globals_after in text:
        return
    macro_before = "#define kbd_fd              (G.kbd_fd            )\n"
    macro_after = (
        macro_before
        + "#define input_fd            (G.input_fd          )\n"
    )
    open_before = """\tif (filename) {
\t\txmove_fd(xopen(filename, O_RDONLY), STDIN_FILENO);
#if ENABLE_FEATURE_LESS_FLAGS
\t\tnum_lines = REOPEN_AND_COUNT;
#endif
\t} else {
\t\t/* \"less\" with no arguments in argv[] */
\t\t/* For status line only */
\t\tfilename = xstrdup(bb_msg_standard_input);
#if ENABLE_FEATURE_LESS_FLAGS
\t\tnum_lines = REOPEN_STDIN;
#endif
\t}
"""
    open_after = """\tif (filename) {
\t\t/* Keep fd 0 attached to the PTY so keyboard input still works. */
\t\tif (input_fd != STDIN_FILENO)
\t\t\tclose(input_fd);
\t\tinput_fd = xopen(filename, O_RDONLY);
#if ENABLE_FEATURE_LESS_FLAGS
\t\tnum_lines = REOPEN_AND_COUNT;
#endif
\t} else {
\t\t/* \"less\" with no arguments in argv[] */
\t\t/* For status line only */
\t\tfilename = xstrdup(bb_msg_standard_input);
\t\tinput_fd = STDIN_FILENO;
#if ENABLE_FEATURE_LESS_FLAGS
\t\tnum_lines = REOPEN_STDIN;
#endif
\t}
"""
    required = (
        globals_before, macro_before, open_before,
        "int flags = ndelay_on(0);",
        "safe_read(STDIN_FILENO, readbuf, COMMON_BUFSIZE)",
        "fcntl(0, F_SETFL, flags)",
        "pfd[0].fd = STDIN_FILENO;",
    )
    if any(marker not in text for marker in required):
        raise SystemExit("unsupported BusyBox less source revision: LeonOS patch did not apply")
    text = text.replace(globals_before, globals_after, 1)
    text = text.replace(macro_before, macro_after, 1)
    text = text.replace(open_before, open_after, 1)
    text = text.replace("int flags = ndelay_on(0);", "int flags = ndelay_on(input_fd);", 1)
    text = text.replace("safe_read(STDIN_FILENO, readbuf, COMMON_BUFSIZE)",
                        "safe_read(input_fd, readbuf, COMMON_BUFSIZE)", 1)
    text = text.replace("fcntl(0, F_SETFL, flags)", "fcntl(input_fd, F_SETFL, flags)", 1)
    text = text.replace("pfd[0].fd = STDIN_FILENO;", "pfd[0].fd = input_fd;", 1)
    path.write_text(text, encoding="utf-8")


def patch_ls_colors_for_leonos(source: Path) -> None:
    """Use the classic green directory colour in the LeonOS terminal."""
    path = source / "coreutils/ls.c"
    text = path.read_text(encoding="utf-8")
    colors_before = r'"\037\043\043\045\042\045\043\043\000\045\044\045\043\045\045\040"'
    colors_after = r'"\037\043\043\045\040\045\043\043\000\045\044\045\043\045\045\040"'
    if colors_after in text:
        return
    if colors_before not in text:
        raise SystemExit("unsupported BusyBox ls source revision: LeonOS color patch did not apply")
    path.write_text(text.replace(colors_before, colors_after, 1), encoding="utf-8")


def patch_time_for_leonos(source: Path) -> None:
    """Keep monotonic helpers in the LeonOS shim as the single definition."""
    path = source / "libbb/time.c"
    text = path.read_text(encoding="utf-8")
    start_marker = "#if ENABLE_MONOTONIC_SYSCALL\n"
    end_marker = "\n#endif"
    if "/* LeonOS supplies monotonic_* through leonos_shim.c. */" in text:
        return
    start = text.find(start_marker)
    if start < 0:
        raise SystemExit("unsupported BusyBox time source revision: monotonic marker missing")
    end = text.rfind(end_marker)
    if end < 0:
        raise SystemExit("unsupported BusyBox time source revision: monotonic footer missing")
    replacement = "/* LeonOS supplies monotonic_* through leonos_shim.c. */\n"
    text = text[:start] + replacement + text[end + len("\n#endif\n"):]
    path.write_text(text, encoding="utf-8")


def patch_ash_for_leonos(source: Path) -> None:
    """Resolve LeonOS image applications before Ash searches Linux paths."""
    path = source / "shell/ash.c"
    text = path.read_text(encoding="utf-8")
    declaration = (
        "/* LeonOS has no Unix-style applet links or program directories. */\n"
        "extern const char *leonos_shell_command_path(const char *name);\n\n"
    )
    declaration_marker = "/* ============ Hashing commands */\n"
    command_lookup = """#endif

	if (leonos_shell_command_path(name) != NULL) {
		/* The image resolver will execute the matching .elf in shellexec(). */
		entry->cmdtype = CMDNORMAL;
		entry->u.index = -1;
		return;
	}

	/* We have to search path. */
"""
    command_lookup_marker = """#endif

	/* We have to search path. */
"""
    exec_lookup = """\tenvp = listvars(VEXPORT, VUNSET, /*strlist:*/ NULL, /*end:*/ NULL);
	if (strchr(prog, '/') == NULL) {
		const char *leonos_path = leonos_shell_command_path(prog);
		if (leonos_path != NULL)
			tryexec(IF_FEATURE_SH_STANDALONE(-1,) leonos_path, argv, envp);
	}
	if (strchr(prog, '/') != NULL
"""
    exec_lookup_marker = """\tenvp = listvars(VEXPORT, VUNSET, /*strlist:*/ NULL, /*end:*/ NULL);
	if (strchr(prog, '/') != NULL
"""
    if declaration not in text:
        if declaration_marker not in text:
            raise SystemExit("unsupported BusyBox ash source revision: declaration marker missing")
        text = text.replace(declaration_marker, declaration + declaration_marker, 1)
    if command_lookup not in text:
        if command_lookup_marker not in text:
            raise SystemExit("unsupported BusyBox ash source revision: command lookup marker missing")
        text = text.replace(command_lookup_marker, command_lookup, 1)
    if exec_lookup not in text:
        if exec_lookup_marker not in text:
            raise SystemExit("unsupported BusyBox ash source revision: exec lookup marker missing")
        text = text.replace(exec_lookup_marker, exec_lookup, 1)
    path.write_text(text, encoding="utf-8")


def read_fragment(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("# CONFIG_") and line.endswith(" is not set"):
            values[line[2 : -len(" is not set")]] = "n"
            continue
        if line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        if key.startswith("CONFIG_"):
            values[key] = value
    return values


def apply_fragment(config: Path, values: dict[str, str]) -> None:
    lines = config.read_text(encoding="utf-8").splitlines()
    seen: set[str] = set()
    result: list[str] = []
    for line in lines:
        match = re.match(r"(?:# )?(CONFIG_[A-Za-z0-9_]+)(?:=.*| is not set)$", line)
        if not match or match.group(1) not in values:
            result.append(line)
            continue
        key = match.group(1)
        if key in seen:
            continue
        value = values[key]
        result.append(f"# {key} is not set" if value == "n" else f"{key}={value}")
        seen.add(key)
    for key, value in values.items():
        if key not in seen:
            result.append(f"# {key} is not set" if value == "n" else f"{key}={value}")
    config.write_text("\n".join(result) + "\n", encoding="utf-8")


def clang_resource_headers() -> Path:
    result = subprocess.run(
        ["clang", "-print-resource-dir"], check=True, text=True, capture_output=True
    )
    headers = Path(result.stdout.strip()) / "include"
    if not headers.is_dir():
        raise SystemExit(f"Clang resource headers are missing: {headers}")
    return headers


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--picolibc-prefix", type=Path, required=True)
    parser.add_argument("--leonos-libc-include", type=Path, required=True)
    parser.add_argument("--leonos-include", type=Path, required=True)
    parser.add_argument("--linker-script", type=Path, required=True)
    parser.add_argument("--leonos-lib", type=Path, required=True)
    parser.add_argument("--picolibc-lib", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--stamp", type=Path, required=True)
    parser.add_argument("--compile-flag", action="append", default=[])
    parser.add_argument("--linker-flag", action="append", default=[])
    args = parser.parse_args()

    if os.name == "nt":
        raise SystemExit("BusyBox must be built from WSL/Linux, not Windows")
    source = args.source.resolve()
    required = [
        source / "Makefile", args.config, args.picolibc_prefix / "include",
        args.leonos_libc_include, args.leonos_include, args.linker_script,
        args.leonos_lib, args.picolibc_lib, ROOT / "userland/busybox/leonos_shim.c",
    ]
    for path in required:
        if not path.exists():
            raise SystemExit(f"required BusyBox input is missing: {path}")

    revision = source_revision(source)
    source_dir = cached_source(source, source_cache_key(revision))
    trim_libbb(source_dir)
    patch_optional_config_macros(source_dir)
    patch_ps_for_leonos(source_dir)
    patch_less_for_leonos(source_dir)
    patch_ls_colors_for_leonos(source_dir)
    patch_ash_for_leonos(source_dir)
    work_root = source_dir.parent
    output_dir = work_root / "output"
    sdk_dir = work_root / "sdk"
    lib_dir = work_root / "lib"
    for path in (output_dir, sdk_dir, lib_dir):
        if path.exists():
            shutil.rmtree(path)
    output_dir.mkdir()
    sdk_dir.mkdir()
    lib_dir.mkdir()

    # BusyBox cannot handle the repository's space-containing path in O=.
    copy_tree(args.picolibc_prefix, sdk_dir / "picolibc")
    copy_tree(args.leonos_libc_include, sdk_dir / "leonos-libc")
    copy_tree(args.leonos_include, sdk_dir / "include")
    copy_tree(ROOT / "userland/busybox/include", sdk_dir / "busybox-include")
    linker_script = sdk_dir / "linker.ld"
    shutil.copyfile(args.linker_script, linker_script)
    adapter_lib = lib_dir / "libleonos-adapter.a"
    shutil.copyfile(args.leonos_lib, adapter_lib)
    shutil.copyfile(args.picolibc_lib, lib_dir / "libc.a")
    merge_static_archives(lib_dir / "libleonos.a",
                          [adapter_lib, args.picolibc_lib.resolve()],
                          work_root / "archive-merge")
    # BusyBox's generic link recipe always probes crypt, m, and rt. The
    # selected applets need none of them; keep empty archives so that probe
    # does not accidentally link a second copy of Picolibc (and its syscall
    # fallbacks) through libm.
    for name in ("libcrypt.a", "libm.a", "librt.a"):
        run(["llvm-ar", "rcs", str(lib_dir / name)])

    run(["make", "-C", str(source_dir), f"O={output_dir}", "allnoconfig"])
    generated_config = output_dir / ".config"
    apply_fragment(generated_config, read_fragment(args.config.resolve()))
    run(["make", "-C", str(source_dir), f"O={output_dir}", "oldconfig"], cwd=source_dir)
    # scripts/gen_build_files appends generic Linux-oriented libbb helpers to
    # the generated Kbuild. Replace the generated copy after configuration as
    # well, otherwise those objects override the source-copy profile above.
    write_minimal_libbb_kbuild(output_dir / "libbb/Kbuild", True)

    headers = clang_resource_headers()
    cflags = " ".join([
        "-target", "x86_64-unknown-none", *(args.compile_flag or ["-O2"]), "-std=gnu11", "-ffreestanding",
        "-D_POSIX_C_SOURCE=200809L",
        # LeonOS's native stat/fstat use a compact private structure. Keep
        # BusyBox on the Picolibc POSIX ABI through the port's adapter layer.
        "-Dstat=leonos_posix_stat", "-Dfstat=leonos_posix_fstat",
        "-fno-stack-protector", "-fno-pic", "-fno-pie", "-mno-red-zone",
        "-mgeneral-regs-only", "-ffunction-sections", "-fdata-sections",
        "-nostdinc", "-isystem", str(headers),
        "-I" + str(sdk_dir / "busybox-include"),
        "-I" + str(sdk_dir / "picolibc/include"),
        "-I" + str(sdk_dir / "leonos-libc"),
        "-I" + str(sdk_dir / "include"),
    ])
    ldflags = " ".join([
        "-target", "x86_64-unknown-none", "-nostdlib", "-fuse-ld=lld",
        "-Wl,-u,_start", "-Wl,--gc-sections", "-Wl,-T," + str(linker_script),
        "-L" + str(lib_dir),
        *["-Wl," + flag for flag in args.linker_flag],
    ])
    run([
        "make", "-C", str(source_dir), f"O={output_dir}", "CC=clang", "ARCH=x86_64",
        "CFLAGS=" + cflags, "LDFLAGS=" + ldflags, "busybox_unstripped",
    ])

    built = output_dir / "busybox_unstripped"
    if not built.is_file():
        raise SystemExit(f"BusyBox build did not produce {built}")
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(built, output)
    args.stamp.parent.mkdir(parents=True, exist_ok=True)
    args.stamp.write_text(
        "{\n"
        f"  \"busybox_commit\": \"{revision}\",\n"
        f"  \"config_sha256\": \"{hashlib.sha256(args.config.read_bytes()).hexdigest()}\"\n"
        "}\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
