# Kernel Debug Tool

LeonOS 4 includes an optional Ring-0 diagnostic mode. Open **About LeonOS**
(`osver`) and click the Logo five times within two seconds. The enabled state
is stored at `/system/state/kerneldebug.enabled`.

When enabled, the Start menu exposes **Restart into kernel debugger**. That
action writes a one-shot marker to the boot ESP at
`/boot/system/state/kerneldebug.next`. The loader consumes and deletes the marker
before entering the kernel, so an interrupted debug session cannot create a
permanent boot loop.

The kernel then validates `/system/kerneldebug.sys` as an x86_64 `ET_REL`
module and enters its `ostui` diagnostic interface before starting Ring-3
userland. The module must contain the `LEONKDBG` ELF note (ABI 1 and the fixed
entry-name hash), have no dynamic segment or TLS, and use only the supported
x86_64 relocations. Undefined symbols are rejected; the module receives a
small fixed API table instead of arbitrary kernel symbol access.

The TUI mirrors output to the framebuffer and serial console, accepts common
VT100/xterm control sequences, and provides safe syscall/ioctl timing, reboot,
shutdown, and normal-start options. Dangerous tests are never run
automatically. If the module is missing or fails validation, the kernel uses a
minimal built-in recovery menu with the same normal-start, reboot, and shutdown
choices.

Choosing **Continue normal startup** clears the persistent debug flag and
continues the already initialized kernel through the ordinary init/desktop
path. If the ESP cannot be written, the Start menu reports the error and does
not reboot.

## TTY display diagnostic

While the kernel-created TTY is active, press `Ctrl+Alt+Shift+F12`. The
kernel writes `Test message` directly to the framebuffer TTY and the serial
console without a timestamp. If the serial log contains the message but the
screen does not, the TTY renderer or framebuffer path is stuck; if neither
contains it, keyboard interrupt delivery or the kernel is not responding.

## API diagnostics

The first menu item runs the registered syscall and ioctl probes. The current
registry covers 41 syscall entries and 120 ioctl entries (161 entries total),
including process, filesystem, memory, IPC, terminal, GUI, device, and
security interfaces. Results are shown in pages of 16 entries so the report
remains readable on a small console. Each entry reports its API kind, name,
outcome, and minimum/average/maximum TSC cycles when it was executed; the
final summary reports executed, skipped, failed, and total counts.

Probes use bounded arguments and run for a fixed number of iterations. Calls
with persistent or destructive side effects are registered but reported as
`SKIPPED (side effects)`; they are never invoked by the safe test. Invalid
arguments which are expected to be rejected are reported as `EXPECTED ERROR`,
not as failures. The dangerous-test menu remains an explicit-confirmation
placeholder and does not run anything automatically.

## System identity

The `uname` implementation in BusyBox and the command shim now obtains
`sysname`, `release`, and `version` from `leonos_system_info()` and derives a
machine-specific node name from `leonos_machine_identity()`. The kernel target
architecture is also published in `leonos_system_info()` and copied into the
machine field. These values therefore follow the kernel build and platform
identity instead of repeating the product name in every field.
