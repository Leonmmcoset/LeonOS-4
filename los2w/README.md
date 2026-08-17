# los2w

`los2w` runs LeonOS 4 x86_64 ELF applications directly on Windows Python. It
supports legacy `ET_EXEC` images and the LeonOS ABI-v1 dynamic `ET_DYN` format,
including `0:/system/lib/ld-leonos.elf` and `libleonos.so.1`. A process table
provides independent Unicorn address spaces, PID/PPID tracking, round-robin
execution, guest `execve`, and `wait4` child reaping. The mapped Windows
directory is exposed as guest `0:/` and shared GUI, network, and logging state
is available to all guest processes.

## Install

```powershell
py -m pip install los2w
```

## Run

To run from a source checkout, install the development dependencies:

```powershell
py -m pip install -r los2w\requirements.txt
```

Open the GUI:

```powershell
py -m los2w
```

Run a smoke test:

```powershell
py -m los2w --elf build\userland\oshlp.elf --root build\esp --arg 0:/docs/leonos.hlp --smoke
```

Run multiple programs concurrently by repeating `--program`:

```powershell
py -m los2w --elf build\userland\terminal.elf --root build\esp `
  --program build\userland\fastfetch.elf
```

`mmap`, `munmap`, `mprotect`, descriptor duplication, `fcntl`, `ftruncate`,
`execve`, `wait4`, and the current LeonOS filesystem syscall numbers are
emulated. This is a LeonOS compatibility runner, not a Linux ABI emulator;
unsupported syscalls are reported in the diagnostic compatibility snapshot.

The GUI records the ten most recent ELF and root-directory choices in its host
configuration. Use **App UI style** to start the selected application with the
modern Metro UI or classic Win95 UI; Metro is the default and the choice is
saved for later launches. The selected style is exposed only for the running
guest through `0:/system/config/display.conf`, so the mapped image directory is not
modified. Command-line launches can select the same style with
`--ui-theme metro` or `--ui-theme win95`.

Select **Export report** to write the current diagnostic JSON.
An unsupported ABI or guest fault automatically writes a report in the local
`los2w` reports directory. Command-line runs can use `--report path.json` or
`--compat-report` for the same data.

## Package

Install packaging dependencies and build the default fast-starting folder
distribution. PyInstaller's PySide6 hook collects only the Qt runtime pieces
used by the runner; Unicorn is collected explicitly for its native emulator
library:

```powershell
py -m pip install -r los2w\requirements-release.txt
py -m los2w.build_release
```

The executable is written to `dist\los2w\los2w.exe`. Add `--onefile` only
when a single executable is more important than startup time.
