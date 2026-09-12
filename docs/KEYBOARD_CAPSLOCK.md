# Caps Lock state

The input layer owns the global Caps Lock state. PS/2 and USB key reports enter
`input_push_key`; only a released-to-pressed transition changes the lock.
Typematic repeats do not change it. The console reads the same state.

The keyboard evdev stream includes an absolute `EV_LED/LED_CAPSL` snapshot before
each key event. Repeating the snapshot lets late readers and readers recovering
from ring overflow recover the state without counting key presses. This is a
LeonOS stream policy, not a claim that Linux emits redundant LED events.
`EVIOCGLED` also exposes the current bitmap, with native x86-64 bitmap sizing.
The complete Linux evdev queue-flushing/SYN_DROPPED semantics remain outside this
change.

Windowd copies the lock into each input event. Desktop forwards that snapshot
with the app event; libwind applies it before dispatch. UI controls only read it.
Queued characters retain the state at the time of input, and a newly started
application does not need to have received the Caps Lock key itself.

The new `modifiers` byte occupies existing padding/reserved space: input messages
remain 24 bytes and app events remain 36 bytes. Rebuild and deploy kernel,
windowd, desktop, apps and `libleonos.so.2` together. The old process-local
`leonos_ui_caps_lock_event` API is removed; SDK headers are updated. Mixing old
applications with the new runtime is unsupported.

PS/2 LED updates use a nonblocking command/ACK state machine with bounded retries.
The keyboard lamp is output controlled by the OS, not an independently readable
source of truth. USB input shares the lock state, but USB HID LED output and
host/guest lock synchronization on VMware have not been implemented/verified by
this change.

## Verification (2026-09-12)

- `python3 tools/test_installer_input.py`: 9 tests pass, including real input and
  windowd routing, late readers, repeats, historical snapshots, Shift XOR,
  unchanged protocol sizes, PS/2 ACK/RESEND/timeout handling, and the real
  `EVIOCGLED` handler under ASan/UBSan (bitmap length, output bounds and EFAULT).
- `python3 tools/test_linux_pty.py`: ASan/UBSan pass, including console state sync.
- `python3 tools/test_linux_ioctl_cloexec.py`: descriptor tests and host Linux
  reference pass (38 checks); this is a regression check, not certification of
  all evdev ioctls.
- Userland, kernel/loader and installer builds succeed.
- `python3 tools/test_capslock_qemu.py --disk build/pam-desktop-debug/core-system.raw
  --output build/capslock-qemu-final`: QEMU/KVM login with Caps Lock plus Shift,
  new Terminal with Caps Lock already enabled, and focus changes pass. Guest
  files read back from ext2 contain exactly `ABCabc` and `XYZxyz` with newlines.
  Evidence: `build/capslock-qemu-final/serial.log`, `capslock-results.png`, and
  `capslock-{first,second}.txt` in that directory.
- Earlier QEMU attempts exposed test harness issues (old app package path and
  Escape not dismissing the Start menu); neither was counted as a pass.
- VMware and physical keyboard LEDs remain unverified.
