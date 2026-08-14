#!/usr/bin/env python3
"""Build the ChenPi11/cmd interpreter for the static LeonOS user ABI."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
EXCLUDED_SOURCES = frozenset({"lexec.c", "llinenoise.c", "lpath.c", "lreadline.c", "lsysport.c"})


def run(command: list[str]) -> None:
    subprocess.run(command, check=True)


def clang_resource_headers() -> Path:
    result = subprocess.run(
        ["clang", "-print-resource-dir"], check=True, text=True, capture_output=True
    )
    headers = Path(result.stdout.strip()) / "include"
    if not headers.is_dir():
        raise SystemExit(f"Clang resource headers are missing: {headers}")
    return headers


def source_revision(source: Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "HEAD"],
        check=True, text=True, capture_output=True,
    )
    return result.stdout.strip()


def prepare_source(source: Path, work_source: Path) -> None:
    shutil.copytree(
        source, work_source,
        ignore=shutil.ignore_patterns(".git", "*.o", "cmd.exe", "COMMAND.COM"),
    )

    cinterp = work_source / "cinterp.c"
    text = cinterp.read_text(encoding="utf-8")
    before = """        char *result;
        char *prompt = NULL;
        size_t prompt_len = 0;
"""
    if before not in text:
        raise SystemExit("unsupported cmd source revision: prompt declarations changed")
    text = text.replace(before, "        char *result;\n", 1)
    old_prompt = """        /* Render the prompt and hand it to the line editor, so it knows where
         * the input area starts (fixes backspace eating the prompt) */
        if (is_tty)
        {
            FILE *m = libcmd_open_memstream(&prompt, &prompt_len);
            if (m)
            {
                render_prompt(ctx, m);
                libcmd_memstream_close(m);
            }
        }
"""
    new_prompt = """        /* LeonOS exposes canonical PTY input. Render the prompt directly and
         * let the small port reader consume the completed line. */
        if (is_tty)
        {
            render_prompt(ctx, stdout);
            fflush(stdout);
        }
"""
    if old_prompt not in text:
        raise SystemExit("unsupported cmd source revision: prompt rendering changed")
    text = text.replace(old_prompt, new_prompt, 1)
    old_read = "            result = libcmd_readline(is_tty && prompt ? prompt : \"\", line, sizeof(line));\n"
    if old_read not in text or "        free(prompt);\n\n" not in text:
        raise SystemExit("unsupported cmd source revision: interactive reader changed")
    text = text.replace(old_read, "            result = libcmd_readline(\"\", line, sizeof(line));\n", 1)
    cinterp.write_text(text.replace("        free(prompt);\n\n", "", 1), encoding="utf-8")

    # The upstream job-control implementation assumes fork, process groups,
    # and SIGCHLD.  LeonOS tracks controlled-spawn jobs in cparser instead.
    # Publish the small asynchronous pipeline API used by that adapter.
    glibcmd = work_source / "glibcmd.h"
    text = glibcmd.read_text(encoding="utf-8")
    pipeline_marker = """int libcmd_exec_pipeline(char *const *const *cmds,
                         const char *const *paths,
                         int n,
                         char *const envp[],
                         int stdin_fd,
                         int stdout_fd,
                         libcmd_exit_info_t *exit_info);
"""
    pipeline_addition = pipeline_marker + """
/* Starts an external pipeline without waiting. pids receives one child PID
 * per stage and the return value is the number of started stages. */
int libcmd_exec_pipeline_async(char *const *const *cmds,
                               const char *const *paths,
                               int n,
                               char *const envp[],
                               int stdin_fd,
                               int stdout_fd,
                               int pids[],
                               int pids_capacity);

/* Returns non-zero when the controlled-spawn child is SIGSTOPped. */
int libcmd_job_is_stopped(int pid);
"""
    if "libcmd_exec_pipeline_async" not in text:
        if pipeline_marker not in text:
            raise SystemExit("unsupported cmd source revision: pipeline API declaration changed")
        glibcmd.write_text(text.replace(pipeline_marker, pipeline_addition, 1), encoding="utf-8")

    cparser = work_source / "cparser.c"
    text = cparser.read_text(encoding="utf-8")
    start = text.find("    case NODE_PIPE: {")
    end = text.find("\n    case NODE_AND:", start)
    if start < 0 or end < 0:
        raise SystemExit("unsupported cmd source revision: pipeline executor changed")
    replacement = """    case NODE_PIPE: {
        /* LeonOS runs external pipeline stages concurrently through the
         * controlled spawn ABI. Builtins and stage redirections remain
         * unsupported here because they require fork-like shell state. */
        cmd_node_t *stages[64];
        char *const *argvs[64];
        const char *paths[64];
        /* 64 x CMD_MAX_PATH is about 256 KiB.  Keep it out of cmd's
         * bounded user stack; only one command interpreter thread executes
         * this dispatcher at a time. */
        static char resolved[64][CMD_MAX_PATH];
        libcmd_exit_info_t pipeline_status;
        int n = collect_pipe_stages(node, stages, 64);
        int i;
        if (n <= 0) {
            ret = 1;
            break;
        }
        for (i = 0; i < n; ++i) {
            if (stages[i]->type != NODE_SIMPLE || stages[i]->argc == 0 ||
                stages[i]->redirs || cmd_find_builtin(stages[i]->argv[0]) ||
                libcmd_find_exec(stages[i]->argv[0], libcmd_getenv("PATH"),
                                 resolved[i], sizeof(resolved[i])) < 0) {
                fputs("cmd: pipelines require external commands without redirection\\n", stderr);
                ret = 1;
                break;
            }
            argvs[i] = stages[i]->argv;
            paths[i] = resolved[i];
        }
        if (i == n && libcmd_exec_pipeline(argvs, paths, n,
                                           libcmd_get_environ(), stdin_fd,
                                           stdout_fd, &pipeline_status) == 0)
            ret = pipeline_status.exit_code;
        else if (i == n)
            ret = 1;
        break;
    }
"""
    text = text[:start] + replacement + text[end:]
    simple_marker = "    cparser.write_text(text, encoding=\"utf-8\")\n"
    if simple_marker in text:
        raise SystemExit("unsupported cmd source revision: duplicate job patch marker")
    simple_before = """static int exec_simple(cmd_context_t *ctx, cmd_node_t *node,
                       int stdin_fd, int stdout_fd, int stderr_fd)
{
    if (node == NULL || node->argc == 0)
        return 0;
    return cmd_dispatch(ctx, node->argc, node->argv,
                        stdin_fd, stdout_fd, stderr_fd);
}
"""
    simple_after = """static int exec_simple(cmd_context_t *ctx, cmd_node_t *node,
                       int stdin_fd, int stdout_fd, int stderr_fd)
{
    int handled = 0;
    int result;
    if (node == NULL || node->argc == 0)
        return 0;
    result = leonos_cmd_builtin(node->argc, node->argv, &handled);
    if (handled)
        return result;
    return cmd_dispatch(ctx, node->argc, node->argv,
                        stdin_fd, stdout_fd, stderr_fd);
}
"""
    job_decls = """\n/* LeonOS controlled-spawn job adapter. */
extern int leonos_cmd_builtin(int argc, char **argv, int *handled);
extern int leonos_cmd_register_job(const int pids[], int count, int last_pid,
                                   const char *text);
extern void leonos_cmd_job_append_word(char *out, size_t cap, const char *word);
\n"""
    declarations = """static int exec_simple(cmd_context_t *ctx, cmd_node_t *node,
                       int stdin_fd, int stdout_fd, int stderr_fd);
static int exec_node_fds(cmd_context_t *ctx, cmd_node_t *node,
                         int stdin_fd, int stdout_fd, int stderr_fd);
"""
    declarations_after = declarations + """static int leonos_start_background(cmd_node_t *node,
                                   int stdin_fd, int stdout_fd, int stderr_fd);
"""
    if declarations not in text or simple_before not in text:
        raise SystemExit("unsupported cmd source revision: job executor insertion changed")
    text = text.replace(declarations, declarations_after + job_decls, 1)
    text = text.replace(simple_before, simple_after, 1)
    seq_before = """    case NODE_SEQ:
        exec_node_fds(ctx, node->left, stdin_fd, stdout_fd, stderr_fd);
        ret = exec_node_fds(ctx, node->right, stdin_fd, stdout_fd, stderr_fd);
        break;
"""
    seq_after = """    case NODE_SEQ:
        ret = leonos_start_background(node->left, stdin_fd, stdout_fd, stderr_fd);
        if (node->right)
            ret = exec_node_fds(ctx, node->right, stdin_fd, stdout_fd, stderr_fd);
        break;
"""
    background_helper = """static int leonos_start_background(cmd_node_t *node,
                                   int stdin_fd, int stdout_fd, int stderr_fd)
{
    static char resolved[64][CMD_MAX_PATH];
    cmd_node_t *stages[64];
    char *const *argvs[64];
    const char *paths[64];
    int pids[64];
    char text[160];
    int n;
    int i;
    if (!node) return 0;
    memset(text, 0, sizeof(text));
    if (node->type == NODE_SIMPLE) {
        if (node->argc == 0 || node->redirs || cmd_find_builtin(node->argv[0]) ||
            libcmd_find_exec(node->argv[0], libcmd_getenv(\"PATH\"), resolved[0],
                             sizeof(resolved[0])) < 0) {
            fputs(\"cmd: background jobs require an external command without redirection\\n\", stderr);
            return 1;
        }
        pids[0] = libcmd_exec_async(resolved[0], node->argv, libcmd_get_environ(),
                                    stdin_fd, stdout_fd, stderr_fd, 0);
        if (pids[0] < 0) {
            fputs(\"cmd: unable to start background command\\n\", stderr);
            return 1;
        }
        for (i = 0; i < node->argc; ++i)
            leonos_cmd_job_append_word(text, sizeof(text), node->argv[i]);
        if (leonos_cmd_register_job(pids, 1, pids[0], text) < 0) {
            (void)kill(pids[0], SIGTERM);
            fputs(\"cmd: job table is full\\n\", stderr);
            return 1;
        }
        return 0;
    }
    if (node->type != NODE_PIPE) {
        fputs(\"cmd: background jobs support external commands and pipelines only\\n\", stderr);
        return 1;
    }
    n = collect_pipe_stages(node, stages, 64);
    if (n <= 0) return 1;
    for (i = 0; i < n; ++i) {
        int j;
        if (stages[i]->type != NODE_SIMPLE || stages[i]->argc == 0 || stages[i]->redirs ||
            cmd_find_builtin(stages[i]->argv[0]) ||
            libcmd_find_exec(stages[i]->argv[0], libcmd_getenv(\"PATH\"), resolved[i],
                             sizeof(resolved[i])) < 0) {
            fputs(\"cmd: background pipelines require external commands without redirection\\n\", stderr);
            return 1;
        }
        argvs[i] = stages[i]->argv;
        paths[i] = resolved[i];
        if (i) leonos_cmd_job_append_word(text, sizeof(text), \"|\");
        for (j = 0; j < stages[i]->argc; ++j)
            leonos_cmd_job_append_word(text, sizeof(text), stages[i]->argv[j]);
    }
    n = libcmd_exec_pipeline_async(argvs, paths, n, libcmd_get_environ(),
                                   stdin_fd, stdout_fd, pids, 64);
    if (n < 0) {
        fputs(\"cmd: unable to start background pipeline\\n\", stderr);
        return 1;
    }
    if (leonos_cmd_register_job(pids, n, pids[n - 1], text) < 0) {
        for (i = 0; i < n; ++i) (void)kill(pids[i], SIGTERM);
        fputs(\"cmd: job table is full\\n\", stderr);
        return 1;
    }
    return 0;
}

"""
    if seq_before not in text:
        raise SystemExit("unsupported cmd source revision: sequential executor changed")
    text = text.replace(seq_before, seq_after, 1)
    insert_before = "/* Apply redirections and execute */\n"
    if insert_before not in text:
        raise SystemExit("unsupported cmd source revision: background helper insertion changed")
    text = text.replace(insert_before, background_helper + insert_before, 1)
    cparser.write_text(text, encoding="utf-8")

    # cmd_expand_vars() is called from the interactive command path.  Its
    # upstream 32 KiB automatic output buffer overflows LeonOS's 64 KiB user
    # stack after the interpreter's other line-parser frames are accounted
    # for.  Keep the same bounded expansion capacity on the heap instead.
    cvars = work_source / "cvars.c"
    text = cvars.read_text(encoding="utf-8")
    stack_buffer = """char *cmd_expand_vars(cmd_context_t *ctx, const char *line)
{
    char  out[CMD_MAX_LINE * 4];
    int   opos = 0;
    int   olen = (int)(sizeof(out) - 1);
    const char *p = line;

    if (line == NULL)
        return libcmd_strdup(\"\");
"""
    heap_buffer = """char *cmd_expand_vars(cmd_context_t *ctx, const char *line)
{
    char *out;
    int   opos = 0;
    int   olen = CMD_MAX_LINE * 4 - 1;
    const char *p = line;

    if (line == NULL)
        return libcmd_strdup(\"\");
    out = (char *)malloc((size_t)CMD_MAX_LINE * 4U);
    if (out == NULL)
        return libcmd_strdup(\"\");
"""
    if stack_buffer not in text:
        raise SystemExit("unsupported cmd source revision: variable expansion stack buffer changed")
    text = text.replace(stack_buffer, heap_buffer, 1)
    stack_return = """    out[opos] = '\\0';
    return libcmd_strdup(out);
}
"""
    heap_return = """    char *result;

    out[opos] = '\\0';
    result = libcmd_strdup(out);
    free(out);
    return result;
}
"""
    if stack_return not in text:
        raise SystemExit("unsupported cmd source revision: variable expansion return changed")
    cvars.write_text(text.replace(stack_return, heap_return, 1), encoding="utf-8")

    # The upstream directory adapter assumes a complete POSIX stat surface
    # while enumerating.  LeonOS already returns a name and node type from
    # readdir(), so publish that entry first and use stat only to enrich it.
    # In particular, a failed metadata lookup must not leave the caller with
    # stale fields from the previous directory entry.
    lfs = work_source / "lfs.c"
    text = lfs.read_text(encoding="utf-8")
    start = text.find("int libcmd_readdir(libcmd_dir_t dir, libcmd_dirent_t *entry)\n{")
    end = text.find("\nvoid libcmd_closedir(libcmd_dir_t dir)", start)
    if start < 0 or end < 0:
        raise SystemExit("unsupported cmd source revision: directory adapter changed")
    replacement = """int libcmd_readdir(libcmd_dir_t dir, libcmd_dirent_t *entry)
{
    struct dirent *de;
    struct stat st;
    struct tm *tm_info;
    time_t t;

    if (dir == NULL || entry == NULL)
        return -1;

    de = readdir((DIR *)dir);
    if (de == NULL)
        return -1;

    /* A LeonOS directory read already supplies the name and type.  Reset all
     * optional fields before a best-effort stat so an error cannot reuse the
     * preceding entry's metadata or suppress this entry from DIR output. */
    memset(entry, 0, sizeof(*entry));
    strncpy(entry->name, de->d_name, LIBCMD_NAME_MAX - 1);
    entry->name[LIBCMD_NAME_MAX - 1] = '\\0';
#ifdef DT_DIR
    entry->is_dir = (de->d_type == DT_DIR) ? LIBCMD_TRUE : LIBCMD_FALSE;
    entry->is_link = (de->d_type == DT_LNK) ? LIBCMD_TRUE : LIBCMD_FALSE;
#endif

    if (stat(de->d_name, &st) != 0)
        return 0;

    entry->is_dir = S_ISDIR(st.st_mode) ? LIBCMD_TRUE : LIBCMD_FALSE;
    entry->is_link = S_ISLNK(st.st_mode) ? LIBCMD_TRUE : LIBCMD_FALSE;
    entry->size = st.st_size;
    entry->mode = (unsigned int)st.st_mode;
    entry->uid = (unsigned int)st.st_uid;

    t = st.st_mtime;
    tm_info = localtime(&t);
    if (tm_info) {
        entry->mtime.year = tm_info->tm_year + 1900;
        entry->mtime.month = tm_info->tm_mon + 1;
        entry->mtime.day = tm_info->tm_mday;
        entry->mtime.hour = tm_info->tm_hour;
        entry->mtime.minute = tm_info->tm_min;
        entry->mtime.second = tm_info->tm_sec;
        entry->mtime.wday = tm_info->tm_wday;
    }
    entry->atime = entry->mtime;
    entry->ctime = entry->mtime;
    return 0;
}
"""
    lfs.write_text(text[:start] + replacement + text[end:], encoding="utf-8")


def compile_source(flags: list[str], source: Path, output: Path) -> None:
    run(["clang", *flags, "-c", str(source), "-o", str(output)])


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--port", type=Path, required=True)
    parser.add_argument("--picolibc-prefix", type=Path, required=True)
    parser.add_argument("--leonos-libc-include", type=Path, required=True)
    parser.add_argument("--leonos-include", type=Path, required=True)
    parser.add_argument("--linker-script", type=Path, required=True)
    parser.add_argument("--leonos-lib", type=Path, required=True)
    parser.add_argument("--picolibc-lib", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--stamp", type=Path, required=True)
    parser.add_argument("--compile-flag", action="append", default=[])
    parser.add_argument("--linker-flag", action="append", default=[])
    args = parser.parse_args()

    if os.name == "nt":
        raise SystemExit("cmd must be built from WSL/Linux, not Windows")

    source = args.source.resolve()
    port = args.port.resolve()
    picolibc_prefix = args.picolibc_prefix.resolve()
    leonos_libc_include = args.leonos_libc_include.resolve()
    leonos_include = args.leonos_include.resolve()
    linker_script = args.linker_script.resolve()
    leonos_lib = args.leonos_lib.resolve()
    picolibc_lib = args.picolibc_lib.resolve()
    work_dir = args.work_dir.resolve()
    output = args.output.resolve()
    stamp = args.stamp.resolve()
    required = (
        source / "cmain.c", source / "cparser.c", source / "LICENSE",
        port / "leonos_cmd_shim.c", port / "README.md", picolibc_prefix / "include",
        leonos_libc_include, leonos_include, linker_script, leonos_lib, picolibc_lib,
    )
    for path in required:
        if not path.exists():
            raise SystemExit(f"required cmd build input is missing: {path}")
    if ROOT not in work_dir.parents:
        raise SystemExit(f"cmd work directory must be inside the project: {work_dir}")

    if work_dir.exists():
        shutil.rmtree(work_dir)
    work_source = work_dir / "source"
    prepare_source(source, work_source)
    object_dir = work_dir / "objects"
    object_dir.mkdir()

    headers = clang_resource_headers()
    flags = [
        "-target", "x86_64-unknown-none", *(args.compile_flag or ["-O2"]), "-std=c99",
        "-ffreestanding", "-fno-stack-protector", "-fno-pic", "-fno-pie", "-mno-red-zone",
        "-mgeneral-regs-only", "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra",
        "-Wno-unused-parameter", "-DLEONOS_USE_PICOLIBC", "-D_POSIX_C_SOURCE=200809L",
        "-D_DEFAULT_SOURCE", "-Dstat=leonos_posix_stat", "-Dfstat=leonos_posix_fstat",
        "-Dlstat=leonos_posix_lstat", "-nostdinc", "-isystem", str(headers),
        "-I" + str(port / "include"), "-I" + str(picolibc_prefix / "include"),
        "-I" + str(leonos_libc_include), "-I" + str(leonos_include),
        "-I" + str(work_source), "-I" + str(port),
    ]

    objects: list[Path] = []
    for source_file in sorted(work_source.glob("*.c")):
        if source_file.name in EXCLUDED_SOURCES:
            continue
        object_file = object_dir / source_file.with_suffix(".o").name
        compile_source(flags, source_file, object_file)
        objects.append(object_file)
    shim_object = object_dir / "leonos_cmd_shim.o"
    shim_flags = [*flags, "-Ustat", "-Ufstat", "-Ulstat"]
    compile_source(shim_flags, port / "leonos_cmd_shim.c", shim_object)
    objects.append(shim_object)

    output.parent.mkdir(parents=True, exist_ok=True)
    run([
        "ld.lld", "-nostdlib", "--gc-sections", *args.linker_flag,
        "-z", "max-page-size=0x1000", "-T", str(linker_script), "-o", str(output),
        *map(str, objects), "--start-group", str(leonos_lib), str(picolibc_lib), "--end-group",
    ])
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.write_text(json.dumps({
        "cmd_commit": source_revision(source),
        "upstream": "https://github.com/ChenPi11/cmd",
        "port": "canonical-pty-input, controlled spawn/wait, multi-stage external pipelines",
        "port_sha256": hashlib.sha256((port / "leonos_cmd_shim.c").read_bytes()).hexdigest(),
    }, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
