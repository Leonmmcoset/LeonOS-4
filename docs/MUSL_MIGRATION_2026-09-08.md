# Default libc migration status — 2026-09-08

The user requires complete removal of Picolibc. Its source worktree, build
script/targets, custom loader and POSIX wrappers have been removed. Default
core and third-party programs now link musl+mimalloc. The installer live root
and installation payload use the same ABI, with separate installer policy
configuration in libleonos.so.2. Old dynamic binaries requiring libleonos.so.1
or ld-leonos.elf are unsupported and must be rebuilt; old static programs
with the former private stat/termios ABI also require rebuilding.

Actual current results:

- `musl-only-release-build.log`: full release succeeds (normal images,
  installer and SDK). Old private CRT/ABI-note/linker-script arguments are
  removed, along with obsolete build output directories and legacy stat
  aliases; StardustUI now calls musl stat directly.
- `musl-only-release-closure.log`: ordinary ESP/ISO each 78 ELF files and
  installer staging 95 ELF files have a complete musl dependency closure;
  no retired libc headers/libraries/loaders are shipped.
- `session-notify-probes-serial.log`: static/dynamic each 49 groups pass;
  Linux 6.12 reference passes 43 selected groups. Native forkpty, getsid,
  getpgid and SIGCHLD regressions are included. Full Linux ABI remains open.
- `ctty-normal-terminal-test.log`: OOBE/login, two PTYs, pipeline and nano
  save/readback. `default-musl-tcc-after-test.log` and serial evidence show the
  on-device compiler builds and runs a musl program with exit 0.
- `musl-removal-installer-thanks.png`: current default installer reaches
  Language and Thanks, lists musl/mimalloc and accepts keyboard navigation.
  No disk installation/update or VMware runtime verification is claimed.
- `sdk-musl-only-{dynamic,static}.log`: extracted full SDK builds both
  modes. `musl-only-input-after.log`: all six input/IPC fixtures pass.
  Broader app, permission, graphics and pthread/socket edge coverage remains
  in the full ABI ledger.

The following sections record earlier checkpoints. Their Picolibc mentions
are historical evidence, not active build paths.

# musl + mimalloc migration

The system is migrating from Picolibc to musl 1.2.6 and mimalloc 3.5.1.
This migration does not certify the outstanding Linux v6.12 ABI audit items.

## Ownership and binary policy

- musl owns ISO C/POSIX interfaces, errno, FILE, pthread TLS, startup and the
  dynamic interpreter `/lib/ld-musl-x86_64.so.1`.
- mimalloc supplies the public allocation family together: malloc, calloc,
  realloc, free, aligned allocation and malloc_usable_size. Its upstream musl
  integration is used. musl's loader-private allocations stay paired inside musl.
- libleonos owns LeonOS GUI, devices and service APIs. It must not embed a
  second libc or override standard POSIX entry points in musl applications.
- Linux wire definitions belong in `include/uapi/linux`; LeonOS extensions
  belong in `include/uapi/leonos`. Implementation-only headers remain private.
  musl's upstream headers are checked against UAPI at compile time, not patched
  to accommodate an incompatible kernel.
- Picolibc binaries require rebuilding. ABI 1 objects, FILE pointers, errno and
  heap objects must not cross into the musl runtime. The old source is retained
  during migration; a successful library build alone does not switch the image.

## Status

Use `unprocessed`, `implemented_pending_validation`, `verified`, or
`blocked: <concrete reason>`. Host verification is distinct from LeonOS/QEMU.

| Item | Status | Required evidence |
| --- | --- | --- |
| Pinned musl/mimalloc source | verified (host build) | musl v1.2.6 / 9fa28ece; mimalloc v3.5.1 / 34fbd7e7; upstream source unchanged |
| Shared UAPI and header drift checks | verified (listed definitions only) | 375 native syscall numbers, fcntl and stat checked against installed musl; kernel and legacy installer build |
| Linux stack, auxv and interpreter | verified (tested startup subset) | Static/dynamic guest startup and initial TLS pass; complete ELF/exec edge coverage remains |
| Public mimalloc allocation family | verified (tested guest paths) | Static/dynamic allocation, alignment, realloc and per-thread TLS/allocator contention pass; concurrent multi-core execution is not enabled |
| libleonos and core applications | implemented_pending_validation | libleonos.so.2 has no POSIX export collisions; 49 core apps link; five musl installer GUI services run in QEMU; remaining app behavior pending |
| Third-party ports and SDK | implemented_pending_validation | musl SDK dynamic/static probes pass on host; special third-party application ports remain |
| Installer Logo regression | verified (QEMU, legacy checkpoint) | build 3029 windowd handshake, desktop heartbeat and visible LeonOS Setup language page; build/installer-logo-after.log and .png |
| Normal and installer images | implemented_pending_validation | Legacy installer built and booted; musl diagnostic live processes only, installation payload still legacy; normal image migration pending |
| VMware | unprocessed | Actual VMware boot and UI checks |
| Full Linux syscall audit | unprocessed | All rows retained in the existing audit CSV |

Targets currently available: `musl`, `test-musl-abi`, `musl-runtime`,
`musl-userland`, `musl-sdk`, `musl-probes`, `musl-ltp`, `musl-installer`,
`musl-installer-probes`, `musl-installer-ltp` (via `python3 build.py run TARGET`). These targets
are independent of the default Picolibc image while guest validation proceeds.
The audit retains 231 missing dispatches, 82 routed but uncertified calls,
45 newly implemented but not fully certified calls and 17 Linux reserved/ni entries.
Generating all syscall constants does not implement those dispatches.

Kernel/Picolibc UAPI consumers use common Linux wire values. Upstream musl's
O_ACCMODE additionally includes O_SEARCH for libc classification; comparisons
check this intentional expression separately from the wire value 3. SDK
exports preserve the `linux/` and `leonos/` directory boundaries.

## Reproduced migration hazards

- Build 3014 and a QEMU reproduction stop after windowd accepts desktop and
  installer clients. Actual Picolibc sysroot headers define O_NONBLOCK as
  0x4000; the Linux-compatible kernel expects 0x800. The compiled IPC helper
  therefore leaves the listener blocking and windowd waits in its accept loop.
- The legacy stat adapter passes a 16-byte object to the now 144-byte raw
  Linux stat syscall. A host Linux canary test reproduces the overwrite.
- Per-vector initial-stack alignment inserts words between argc, argv, envp
  and auxv. Linux startup requires these vectors to be contiguous; only the
  final stack pointer needs 16-byte alignment.
- A zero-argument kernel service launch supplied a present AT_EXECFN entry
  with a NULL value; musl dynlink.c passed it to strncmp. Exec filename storage
  is now independent of argv, and empty argv is normalized to one empty string.
- Anonymous mmap(PROT_NONE) recorded maximum protection as zero, preventing
  musl mallocng's metadata mprotect(RW). Its permission ceiling is now RWX;
  the existing simultaneous W+X policy remains a separate audit difference.
  PROT_NONE also previously left pages readable. Non-present owned PTEs now
  preserve backing pages across protect/fork/unmap/destroy; the real paging
  implementation passes `tools/tests/paging_protection_test.c` on the host.
- Native permission enforcement exposed legacy callers passing creation mode
  zero and BusyBox chmod/umask shims returning fake success. Those callers now
  supply explicit modes and the shims call the native syscall. UID/GID and
  rwx metadata are shared with the property dialog, authd and installer copies.
- Normal images lacked /tmp. Fresh images now contain it, and init creates it
  with sticky mode 1777 when upgrading an image where it is absent. Existing
  explicit metadata is preserved. Picolibc open errors now set errno; its
  environment loader was updated with the change.
- authd now publishes standard passwd/group files while retaining existing
  UIDs. BusyBox name-based chown succeeds in the normal QEMU desktop. Updating
  these files exposed rename's EEXIST-on-replacement defect; same-directory
  replacement and permission-record transfer are repaired on FAT32/exFAT/ext2.
  Open-descriptor lifetime and cross-directory rename remain incomplete.
- Directory search checks exposed missing /proc/PID parent directories.
  These now resolve and enumerate; native getdents64 error/cursor/layout tests
  pass and the ordinary task manager lists processes again.

## Actual Guest Evidence

`build/musl/proc-status-after-serial.log` records `END failed=0` for static and
dynamic probes. The earlier AF_UNIX unlink and pthread_create failures are
fixed. Native clone/futex/robust lists, shared resource ownership, cancellation,
Linux signal frames/altstack/SIMD and worker-thread exec are exercised with
unmodified musl. Unix stream/packet sockets, SCM_RIGHTS reference ownership
and cycles, credentials, vector I/O, blocked fd close/reuse, timeouts and
multi-buffer MSG_WAITALL are exercised through Linux headers. Later cases
cover process-directed signals, periodic ITIMER_REAL, FUTEX_WAKE_OP,
lowwater/peek, shutdown/reset, FIONREAD/FIONBIO and raw poll pointer/width rules.
Lowest-free descriptor allocation now includes fd 0/3 for files, pipes, PTYs,
Unix sockets and SCM_RIGHTS. dup2 table expansion retains OFDs across relocation;
RLIMIT_NOFILE applies to descriptor numbers. Native and sanitizer regressions
exercise rollback, widths, read-only user pointers and zero/lowered limits.
Closing implicit PTY fd 0 no longer breaks ordinary shell background commands.
Proc status now exposes actual IDs and resident RAM; both libc consumers read
VmRSS and resolve account names from /etc/passwd. The legacy stat format and
remaining status fields are still outstanding.
The latest native cases also cover pthread CPU affinity, raw TLS/clone address
validation, TID registration/exit behavior and SCM_RIGHTS stream boundaries.
Picolibc affinity wrappers now preserve the distinction between raw copied-byte
counts and the libc 0 return; affinity/fcntl are checked in both legacy sysroots.
`scm-musl-installer.png` shows the latest musl live GUI accepting keyboard input.
`build/musl/proc-status-linux-reference-serial.log` verifies the same 38 selected core groups
against a real Linux 6.12.0 kernel, including its version-specific accept flags.
`namespace-installer/thanks.png` shows the musl installer advancing by keyboard
after probe exit. The latest LTP matrix is recorded in
`docs/LINUX_ABI_PROGRESS_2026-09-08.md`: 14 Open POSIX pthread/synchronization
tests and five other selected tests exit 0; fstat02 and chmod01 still fail on
missing syscalls.
`proc-status-default-installer.png` shows the rebuilt default installer at Thanks;
`proc-status-normal-nano.png` shows ordinary OOBE/desktop, two PTYs, a background command,
the pipeline result 5 and nano save/readback. BusyBox job control remains disabled.
`proc-status-taskmgr.png` now shows resident memory and passwd-resolved accounts.
`proc-status-glx-1.png` / `proc-status-glx-2.png` show animation, followed by exit
code 0; this is PortableGL software rendering, not VMware/SVGA3D verification.
User threads currently run with preemption on the BSP; AP user scheduling
remains disabled and no multi-core execution claim is made.
Normal image OOBE, desktop, Terminal and native chmod/chown UI workflows were
also exercised in QEMU. This does not switch the installed distribution to
musl, certify all third-party ports, or constitute VMware testing.
Earlier application-specific evidence is `service-mode-normal-serial.log` and its
screenshots: OOBE, unprivileged Terminal/PTY/pipeline, nano save/readback,
Task Manager and moving glxgears. Service socket modes are now explicit in
both libc builds; broadening Unix DAC itself would have hidden that regression.

Upstream references: [musl 1.2.6](https://git.musl-libc.org/cgit/musl/tag/?h=v1.2.6),
[mimalloc 3.5.1](https://github.com/microsoft/mimalloc/tree/v3.5.1).
All network source retrieval uses http://127.0.0.1:12334.
