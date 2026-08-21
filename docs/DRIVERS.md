# LeonOS 4 Driver Modules

## Layout

All driver source code lives in the repository-root `drivers/` directory.

- `drivers/bootstrap`: console, framebuffer, VGA, EFI filesystem, storage, and
  USB UHCI/HID implementations that are linked into `kernel.sys`.
- `drivers/mouse`, `drivers/serial`, and `drivers/e1000`: loadable driver
  implementations built as `mouse.drv`, `serial.drv`, and `e1000.drv`.

The normal image, normal ISO, installer runtime root, and installed ESP place
loadable modules directly in `/drivers`.

## Module ABI

A `.drv` is an unsigned x86_64 ELF64 `ET_REL` module. The kernel accepts only
the LeonOS driver descriptor symbol `leonos_driver_module`, ABI version
`LEONOS_DRIVER_ABI_VERSION`, bounded allocatable sections, and supported local
relocations. Modules receive only `struct leonos_driver_kernel_api`; they do
not link directly against arbitrary kernel symbols.

The descriptor identifies the module, declares its driver kind, and supplies
`init` and optional `fini` callbacks. The kernel uses the API table to bind a
mouse input provider, serial console provider, or e1000 link provider.

## Startup and Configuration

After `/` is mounted, the kernel scans the direct files in `/drivers`.
Every valid, enabled `.drv` is loaded in deterministic directory order. A
failed module is retried once, then recorded as failed while boot continues.

`/system/config/drivers.conf` is optional. It uses UTF-8 text with a `version=1` line
and one `disabled=<file>.drv` line per module excluded from automatic startup.
Absent entries are enabled by default.

## USB HID

The bootstrap USB layer scans PCI UHCI (USB 1.1) controllers during kernel
startup. It resets each root port, enumerates standard HID boot-protocol
interfaces, and polls interrupt endpoints for keyboards and mice. A single
level of USB hub is also configured so multiple devices can share a root port.
Keyboard usages are translated to the existing set-1 keycodes, while mouse
reports are published to the existing relative pointer event queue. EHCI,
xHCI, USB storage, generic (non-boot) HID report parsing, and runtime hot-plug
are not implemented yet.

## Management and Trust Boundary

`drvmgr.elf` lists driver files, ABI versions, loading state, errors, and
boot-disable state. Every logged-in user may read this state. Loading,
unloading, forced unloading, rescanning, and changing boot enablement require
an administrator session; the kernel enforces this before dispatching control
requests.

Forced unloading removes the module's service binding, runs its cleanup
callback, and frees its loaded image. Removing `mouse.drv` stops mouse input;
removing `serial.drv` removes serial console output; removing `e1000.drv`
stops network links and clears active socket state. A module can be loaded
again from its file without restarting.

`.drv` files execute in Ring 0 and are intentionally not signed or hashed in
this version. Only trusted administrators should be allowed to write
`/drivers` or modify its contents.
