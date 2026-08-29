# VS Code development support

The C/C++ configurations in `.vscode/c_cpp_properties.json` are split by
compilation boundary: kernel, UEFI loader, LeonOS libc, ordinary userland, and
the exported devtools SDK.  They all consume the generated database at
`build/vscode/compile_commands.json`, so completion and diagnostics follow the
same target flags as the build graph.

The database deliberately stores relative paths.  This keeps IntelliSense
working when the checkout is opened through a Windows junction (for example,
`LeonOS-4`) or from a Remote-WSL path with a different spelling.

Run `LeonOS: Generate compile database (all regions)` once after checkout or
after changing generated headers.  Region-specific generation and clang-tidy
tasks are available from the command palette.  The tasks execute in WSL using
the project path from `leonos.wslProjectRoot`.

For correct `/usr/bin/clang` and generated-header resolution, open the folder
with **Remote - WSL**.  The extension is recommended by this workspace.  Tasks
can also be started from a Windows VS Code window; in that case they still
build in WSL, but IntelliSense should use a Remote-WSL window.

`clang-tidy` must be installed inside the active WSL distribution.  The helper
also accepts `--checks`, `--fix`, and `--warnings-as-errors` when invoked from a
terminal, for example:

```sh
python3 tools/vscode/run_clang_tidy.py --region kernel --warnings-as-errors
```

The generator intentionally excludes upstream third-party source trees from
the first-party database; their headers remain available to IntelliSense.

The only absolute path kept in workspace settings is
`leonos.wslProjectRoot`, because `wsl.exe --cd` requires a Linux path and does
not translate `.` from the Windows task working directory.  All repository
paths and debugger source mapping derive from the workspace or that single
setting.
