# Linux ABI implementation ledger

Scope remains all 375 native Linux v6.12 syscall rows and all B01-B46 groups.
The original 17 Linux reserved/ni numbers remain excluded from new functionality.
No syscall is certified from a dispatch entry, compilation, or a libc wrapper.

Status vocabulary: `unprocessed` (outstanding implementation remains),
`implemented_pending_validation`, `verified`, `blocked` (a concrete external
blocker must be named). A group with partial fixes and known missing behavior
remains `unprocessed`; its completed subparts are recorded separately below.

## 2026-09-09 futex2 and clone3 checkpoint

- The native futex2 wake/wait/requeue entry points use Linux 6.12's exact
  4/6/4 arguments, U32 size flags, 24-byte vector descriptors and independent
  private/shared keys. Zero-count wake, int argument truncation, user-copy errors,
  EAGAIN delivery and signal cancellation/restart were repaired.
- `tools/tests/futex2_abi_test.c` runs unchanged with upstream Linux headers on
  Linux 6.12 (`build/musl/futex2-linux-reference-serial.log`) and in static/dynamic
  LeonOS musl probes (`build/musl/futex2-probes-serial.log`). It covers real queued
  threads, masked wakes, private-to-shared requeue, absolute clock deadlines and
  SA_RESTART. The host queue regression first failed on strict zero wake, then
  passed under ASan/UBSan. Realtime clock changes, mapping lifetime and SMP remain.
- `clone3` accepts Linux's size-versioned argument block and validates extension
  bytes, flags, exit_signal and stack arithmetic before cloning the live trap
  frame. CLEAR_SIGHAND resets caught handlers while preserving SIG_IGN. Existing
  MM/files/fs/sighand/TLS/TID behavior is reused; unsupported requests are rejected.
- `tools/tests/clone3_abi_test.c` passes on Linux 6.12 in
  `build/musl/clone3-linux-reference-serial.log` and static/dynamic LeonOS in
  `build/musl/clone3-probes-serial.log`. It verifies real children, private stacks,
  TLS, TID writeback/clear, handler inheritance and wait/reap. pidfd, requested
  PIDs, cgroups, namespaces, vfork and inherited clone limits remain outstanding.
- All 375 rows remain tracked: 159 missing dispatches, 5 timer rows pending
  validation, 110 new implementations pending complete validation, 84 preexisting
  uncertified routes, 17 reserved/ni.
  These are implementation states, not a compatibility percentage.
- The post-fix QEMU run `build/musl/prctl-after-serial-2.log` reports static and
  dynamic `process_prctl PASS`, `thread_futex2 PASS`, `thread_clone3 PASS`, and
  `END failed=0`. The dumpable state now persists in the shared address-space
  object, while the remaining `prctl` command set is still partial.
- `sethostname(2)` and `setdomainname(2)` now enforce the Linux root, length,
  pointer, and exact-copy contract; `uname(2)` exposes the stored names. The
  Linux 6.12 reference probe and static/dynamic LeonOS probe both pass in
  `build/musl/utsname-linux-reference-serial.log` and
  `build/musl/utsname-after-serial.log`.
- `membarrier(2)` now implements the advertised Linux command subset with
  shared-MM registration state and real memory barriers. LeonOS static/dynamic
  probes pass in `build/musl/membarrier-serial-2.log`; the Linux 6.12 reference
  image reports `ENOSYS` because its `CONFIG_MEMBARRIER` is disabled.
- `openat2(2)` now validates the Linux `open_how` size/version, extension bytes,
  flags and mode before sharing the existing openat allocator. Linux reference
  and static/dynamic LeonOS probes pass in `build/musl/openat2-linux-reference-serial.log`
  and `build/musl/openat2-serial.log`; path-resolution flags remain explicit gaps.

## 2026-09-08 musl default / PTY and process-session checkpoint

- Picolibc source, port, old CRT/loader and static/dynamic libc wrappers have been
  removed from the active tree. Default build and SDK use musl+mimalloc. Native
  C/POSIX consumers use musl; old private ABI binaries must be rebuilt.
- `ctty-normal-terminal-test.log` completes fresh ordinary OOBE/login, dual PTY,
  background/pipeline and nano save/readback. Before-fix failure is retained in
  `default-musl-terminal-test.log`; missing TIOCSCTTY and 64-bit comparisons of
  Linux's 32-bit ioctl request prevented musl forkpty from starting Terminal.
- `session-notify-probes-serial.log`: static/dynamic each 49 groups pass, including
  public musl forkpty, sign-extended raw TIOCGPTN, getsid/getpgid pid_t width,
  cross-user queries, zombie identity until wait and ESRCH after reap. The new
  SIGCHLD failure is preserved in `session-probes-serial.log`. The last thread's
  retirement now queues the configured parent signal once, retaining process
  identity independently of resource release. `session-notify-threads-host.log`
  checks final-thread ordering and duplicate notification with ASan/UBSan.
- `session-linux-reference-after-serial.log`: the same 43 selected groups pass
  on Linux 6.12, using its installed UAPI headers. Reference init can inherit
  session 0; the test now correctly permits that upstream behavior.
- Installer update payload validation and refresh lists now include `/lib`
  (musl loader/libc/mimalloc) and `libleonos.so.2`; old mandatory loader paths
  would reject a correct musl image. This is implemented pending install/update
  execution; reaching Thanks does not validate disk installation.
- TCC first QEMU failure is preserved in `default-musl-tcc-failed.png`: a normal
  user cannot write `/programs/tcc/examples`. The regression now compiles into
  `/tmp` and requires the generated program's actual zero exit.
  `default-musl-tcc-after-test.log` passes; `default-musl-tcc-after-serial.log`
  records compilation and generated ELF exit 0, with `default-musl-tcc-pass.png`.
  Full TCC language/runtime coverage remains unverified.
- Remaining: complete siginfo, SA_NOCLDWAIT/SIG_IGN autoreap, clone wait options,
  reparenting, thread-group-wide setsid/setpgid, TTY lock/detach/hangup and all
  previously recorded Linux ABI gaps. AP user scheduling remains disabled;
  these QEMU checks do not certify SMP or VMware behavior.

## 2026-09-08 raw filesystem, memory and poll checkpoint

- PTY masters now start with Linux devpts' locked state. Slave opens fail until
  `TIOCSGPTLCK` clears it; `TIOCGPTLCK` reads the state. The master remains alive
  until its last duplicated/SCM reference closes. `tools/test_linux_pty.py` passes;
  full controlling-terminal detach, termios queues and hangup semantics remain.
- Raw `creat`, old `getdents`, `renameat` and zero-flag `renameat2` now have native
  x86-64 dispatch and descriptor-relative path handling. The musl guest's
  `proc_directories` test parses both Linux dirent layouts and completes the
  create/rename/old-getdents round trip. Link/symlink/readlink still return their
  real unsupported error because the storage layer has no link target API.
- `msync`, `mincore` and `madvise` validate page/range/user pointers and Linux
  flags; anonymous mappings pass the raw calls in both static and dynamic musl.
  File-backed writeback and unsupported advice are explicitly not claimed.
  `fadvise64` now distinguishes negative offset/length (`EINVAL`) from bad fd
  (`EBADF`), while its cache advice remains a no-op pending a storage cache.
- `brk` now records the writable end of the main ELF, grows and shrinks private
  anonymous pages, and preserves the current break when a request crosses the
  task limit or the initial heap boundary. Raw static and dynamic musl probes
  pass the query/grow/shrink/failure cases in
  `build/musl/abi-probes-qemu-20260908-11.log`; full heap/VMA/RLIMIT stress is
  still pending.
- `waitid` accepts P_ALL/P_PID/P_PGID and WEXITED/WSTOPPED/WCONTINUED subsets and
  writes native siginfo for reaped children. WNOWAIT, exact uid/code fields,
  stopped/continued queues and complete wait4 rusage are still incomplete.
- `ppoll` now validates the Linux timespec and supports the no-signal-mask path;
  a non-null mask returns `EOPNOTSUPP` until the scheduler can atomically install
  and restore it across a parked wait. The raw probe covers valid, invalid and
  masked calls. `build/musl/abi-probes-qemu-20260908-11.log` records static and
  dynamic `memory PASS`, `poll_files PASS`, and `END failed=0`.
- `select` and no-mask `pselect6` now translate the native x86-64 fd_set and
  timeval/timespec layouts through the same poll readiness engine. Static and
  dynamic raw musl probes pass regular-file read/write readiness in
  `build/musl/abi-probes-qemu-20260908-12.log`; pselect signal-mask swapping
  and exact timeout remainder updates remain incomplete.
- `getrusage`, `sysinfo` and `times` now expose Linux x86-64 structure layouts
  using scheduler CPU ticks, resident memory, physical memory totals and task
  counts. Static and dynamic raw musl probes pass these fields in
  `build/musl/abi-probes-qemu-20260908-13.log`; child accounting, swap/load
  counters and detailed I/O/page-fault fields remain incomplete.
- `fallocate` now validates native mode, signed offset/length and fd type; mode
  zero extends the file through the storage truncate contract and refreshes all
  open-fd metadata. Static and dynamic raw musl probes pass extension, stat,
  unsupported-mode and invalid-range cases in
  `build/musl/abi-probes-qemu-20260908-16.log`; true preallocation and punch
  hole/keep-size modes remain unsupported.
- `eventfd` and `eventfd2` now create shared Linux event-counter descriptions,
  validate `EFD_NONBLOCK`, `EFD_SEMAPHORE` and `EFD_CLOEXEC`, implement native
  eight-byte read/write semantics and report counter readiness through `poll`.
  Static and dynamic raw musl probes pass in
  `build/musl/abi-probes-qemu-20260909-11.log`; complete wait-queue wakeups,
  overflow blocking and all resource/lifetime edges remain pending.
- The raw identity family `setreuid`, `setregid`, `setresuid`, `getresuid`,
  `setresgid` and `getresgid` now has Linux-numbered dispatch, three-field
  real/effective/saved ID handling, `-1` preservation and non-root checks. Static
  and dynamic musl probes pass in
  `build/musl/abi-probes-qemu-20260909-14.log`; complete exec/thread credential
  sharing and saved-ID edge cases remain pending. The same run exercises
  non-root EPERM cases from the permissions children.
- `sched_get_priority_max` and `sched_get_priority_min` now validate Linux
  scheduling policies and return the native FIFO/RR and OTHER/BATCH/IDLE bounds;
  invalid policies return `EINVAL`. Static and dynamic probes pass in
  `build/musl/abi-probes-qemu-20260909-13.log`; policy setters, real-time
  scheduling and SMP behavior remain pending.
- `sched_getparam` and `sched_getscheduler` now have native x86-64 dispatch,
  target-task validation, writable `sched_param` priority output and the current
  `SCHED_OTHER` query. Static and dynamic probes pass in
  `build/musl/abi-probes-qemu-20260909-15.log`; policy state changes, permission
  checks and concurrent scheduler semantics remain pending.
- `setfsuid` and `setfsgid` now maintain separate filesystem credentials in each
  task, return the previous ID, enforce the Linux root/real/effective/saved-ID
  rule, and feed permission checks plus `/proc/PID/status`. Static and dynamic
  raw probes pass in `build/musl/abi-probes-qemu-20260909-17.log`; exec and
  thread credential transitions and the complete permission matrix remain pending.
- `sched_setparam` and `sched_setscheduler` now dispatch the native x86-64
  setters for the implemented `SCHED_OTHER`/priority-zero model, with explicit
  `EINVAL` for unsupported real-time policies or priorities. Static and dynamic
  probes pass in `build/musl/abi-probes-qemu-20260909-17.log`; real-time policy,
  priority inheritance and SMP scheduling remain pending.
- `mlock`, `munlock`, `mlockall`, `munlockall` and `mlock2` now dispatch native
  memory-lock operations. VMAs retain locked state, `MCL_CURRENT`/`MCL_FUTURE`/
  `MCL_ONFAULT` and `MLOCK_ONFAULT` are validated, and unmapped ranges fail.
  Static and dynamic musl probes pass in
  `build/musl/abi-probes-qemu-20260909-27.log`; RLIMIT_MEMLOCK, unaligned
  ranges, fault-time locking and fork/exec inheritance remain pending.
- `clock_settime` now dispatches native `CLOCK_REALTIME` updates through the
  existing wall-clock backend with root and timespec validation; invalid clocks
  and monotonic clocks return `EINVAL`. Static and dynamic musl probes pass in
  `build/musl/abi-probes-qemu-20260909-26.log`; RTC persistence, capabilities
  and full adjustment interactions remain pending.
- `prctl` now dispatches the native process-name subset: `PR_SET_NAME`/
  `PR_GET_NAME` use Linux's 16-byte contract, and dumpable query/set return
  explicit values. Static and dynamic musl probes pass in
  `build/musl/abi-probes-qemu-20260909-25.log`; other commands and inheritance
  semantics remain pending.
- `readahead` now validates native fd, signed offset and regular-file/block-device
  objects, returning `ESPIPE` for non-seekable objects. Static and dynamic musl
  probes pass in `build/musl/abi-probes-qemu-20260909-22.log`; backend cache
  population and the full error matrix remain pending.
- `mremap` now has native x86-64 dispatch for anonymous VMAs: shrink, adjacent
  in-place growth and `MREMAP_MAYMOVE` relocation preserve page contents.
  Static and dynamic musl probes pass in
  `build/musl/abi-probes-qemu-20260909-20.log`; file/device mappings, overlapping
  `MREMAP_FIXED` targets and full resource-limit interaction remain pending.
- `personality` now has native x86-64 dispatch. The all-ones query and
  `PER_LINUX` setting return the Linux values, while unsupported persona flags
  return `EINVAL`. Static and dynamic musl probes pass in
  `build/musl/abi-probes-qemu-20260909-19.log`; exec inheritance, ASLR
  interaction and other persona modes remain pending.
- `sched_rr_get_interval` now returns the actual `NTCLKS_TICK_HZ` scheduler tick
  for valid targets and validates the native timespec output and PID errors.
  Static and dynamic probes pass in
  `build/musl/abi-probes-qemu-20260909-18.log`; policy-specific time slices and
  SMP scheduler behavior remain pending.
- `time`, `getcpu`, `close_range`, `preadv`, `pwritev`, `preadv2` and
  `pwritev2` now have native x86-64 dispatch. Positional vector I/O combines
  Linux's split 32-bit offset arguments and restores the shared open-file
  description offset; non-seekable descriptors return `ESPIPE`, and
  unsupported `RWF_*` flags return `EOPNOTSUPP`. `close_range` implements close,
  `CLOSE_RANGE_CLOEXEC` and file-table `UNSHARE` paths with the Linux flag
  values. Static and dynamic raw musl probes pass all new paths, including
  pointer/range errors, in `build/musl/abi-probes-qemu-20260909-1.log` with
  `END failed=0`. Full vector short-I/O/device behavior, RWF flags, CPU
  topology, and close-on-exec inheritance across every descriptor kind remain
  pending.
- `sync_file_range` now validates the Linux signed offset/length and the
  `WAIT_BEFORE|WRITE|WAIT_AFTER` flag set; `sync` and `syncfs` are recorded as
  routed-but-uncertified rather than missing. Raw regular-file checks pass in
  `build/musl/abi-probes-qemu-20260909-2.log` for static and dynamic musl;
  mount-wide writeback and storage error ordering remain incomplete.
- `statx` now has a native x86-64 path for regular/proc paths and
  `AT_EMPTY_PATH`, with the Linux 256-byte result layout, requested basic
  fields, mask-zero `EINVAL`, and invalid-pointer checks. Static and dynamic
  raw musl probes pass in `build/musl/abi-probes-qemu-20260909-4.log` with
  `END failed=0`; timestamp, birth-time, mount-ID, symlink and extended
  attribute fields remain unsupported and are not reported in `stx_mask`.

### Final source/header cleanup checks

`musl-only-release-build.log` rebuilds the full normal/installer/SDK release
with `-nostdinc`, musl headers and Clang builtin headers. All 26 retired
standard-header overrides in `userland/libc/include` and the non-musl syscall
prototype branch are deleted. Canonical LeonOS extensions and curses remain.
`musl-only-release-closure.log` validates ordinary ESP/ISO (78 ELF each) and
installer staging (95 ELF). The extracted full SDK builds dynamic/static
examples in `sdk-musl-only-{dynamic,static}.log`; source SDK snapshots are
refreshed from that archive. `musl-only-input-after.log` runs all six input/IPC
fixtures successfully after giving host test doubles explicit declarations.

`musl-removal-glx-serial.log` and `glxgears-qmp-smoke.png` show software gear
rendering and Escape exit 0. The original QMP harness reported failure because
it searched only an obsolete task-creation log format; its PID extraction now
accepts the actual exec record and requires exit 0 for that PID. The corrected
assertion passes the captured log and rejects failed-exit/wrong-PID controls;
a fresh end-to-end run of that revised harness remains pending.
`musl-removal-stardust-test.log` cannot validate Stardust: the selected profile
has build/image/entry/sdk all false for stardusthello. No runtime pass is
claimed; its consumer was migrated to musl stat in source.

## Group Status

| ID | Status | Implemented or observed evidence | Remaining contract |
| --- | --- | --- | --- |
| B01 | implemented_pending_validation | Native syscall/LSTAR used by static/dynamic musl probes and GUI services | Exhaustive register, entry/return and signal interaction tests |
| B02 | implemented_pending_validation | Contiguous argc/argv/envp/auxv; independent non-null AT_EXECFN; empty argv normalization; guest startup passes | Argument/environment limits and complete exec error-path tests |
| B03 | unprocessed | Native clone shares MM/files/fs/sighand as requested; per-thread TLS with Linux FS/clone address validation, deferred clear_child_tid registration and shared-mm exit rules, futex queues, robust owner death, create/join/detach/cancel and mimalloc contention pass static/dynamic guest probes; 14 unmodified LTP/Open POSIX pthread and synchronization tests exit 0 | clone3, PI futexes, complete clone flags and errors; AP user scheduling and TLB shootdown; broader cancellation/restart coverage |
| B04 | unprocessed | Actual upstream musl interpreter loads five GUI services; shared ELF file-page zero-fill collision fixed | Full ET_EXEC+interpreter/static PIE coverage, malformed ELF/lifetime tests, all application ports |
| B05 | implemented_pending_validation | pause=34; nice moved to private extension; shared number table | Native signal-driven pause interruption test |
| B06 | unprocessed | Native 144-byte stat; real mode/UID/GID in guest; ext2 inode/nlink/blocks/timestamps read from disk and debugfs cross-checked | FAT/exFAT inode identity and timestamps; stable fd metadata after rename/unlink; complete device metadata |
| B07 | unprocessed | Native musl proc getdents64 passes: 19-byte header, aligned records, writable usercopy, short-buffer cursor retention and task traversal | Ordinary directory read semantics, offsets, seek, inode/type identity and lifecycle |
| B08 | verified | Actual Picolibc sysroots and musl use O_NONBLOCK=0x800; raw fcntl/accept EAGAIN pass in guest; installer Logo regression reproduced and repaired | Verification is limited to this constant-drift item |
| B09 | unprocessed | Shared fcntl commands; flag paths exercised by musl probe | Locks, ownership, every command/flag, shared status flags and fd table limits |
| B10 | unprocessed | Guest creation mode/umask/DAC and directory-relative openat pass; no temporary cwd mutation; component traversal checks precede dot-dot normalization | Full open flags, symlinks, O_PATH/O_TMPFILE, trailing slashes and descriptor-relative ancestor semantics |
| B11 | unprocessed | Reference-counted OFDs shared across fork/dup/SCM_RIGHTS; CLONE_FILES and exec detachment; blocked Unix read/recv/readv retain their original OFD across another thread closing and reusing the fd; common lowest-free allocation includes fd 0/3 | PTY/INET ownership and remaining blocking operations |
| B12 | unprocessed | Common open/pipe/socket/SCM/dup/PTMX allocator; fd 3 is available; dup2 grows the table after retaining its source, preserves the target on failure and honors the numeric fd limit; native and sanitizer tests cover fd 700, lowered limits, stdio reuse and allocation failure | Complete races and fd object ownership; remaining fcntl commands and flags |
| B13 | unprocessed | Repeated close returns EBADF; closing implicit PTY stdio now releases its fd; raw close truncates its unsigned-int argument; ordinary desktop background command regression repaired | Master/INET duplicate and inherited references, last-close semantics and cleanup |
| B14 | implemented_pending_validation | ftruncate no longer assigns file offset | Independent native syscall regression for offsets and failure paths |
| B15 | implemented_pending_validation | Raw getcwd byte count and libc pointer conversion; LTP getcwd01 all five assertions pass with exit 0 | Deleted/renamed cwd, path limits and full size/error coverage |
| B16 | unprocessed | Actual owner/group/other DAC; 32-bit UID/GID; chmod/chown/fchmod/fchown; sticky/setgid inheritance; native musl probes and LTP fchmod01/chown01 pass; desktop controls and commands exercised | FAT/exFAT ctime; fd identity after rename/unlink; setuid/setgid exec; fsuid/capabilities; atomic metadata operations and full special-bit semantics |
| B17 | unprocessed | Same-directory replacement on FAT32/exFAT/ext2; real Unix socket nodes with DAC/umask and unlink/rename namespace updates; ext2 socket create/replace/unlink independently checked by debugfs/e2fsck | Cross-directory rename; stable inode/dentry lifecycle across unlink/rename/open descriptors; error/crash transactions; append atomicity |
| B18 | implemented_pending_validation | pipe2 validates flags before allocation; writable usercopy; common fd allocation and rollback including stdio; native descriptor-exhaustion tests retain the user's output array and release the partial allocation | Broader aliases/fork, concurrent close and endpoint lifetime tests |
| B19 | unprocessed | Small nonblocking pipe writes checked for atomicity; EPIPE/SIGPIPE path; QEMU shell pipeline returns 5 | Blocking writer queues, partial writes, interruptions, endpoint references and concurrency |
| B20 | unprocessed | PROT_NONE and MAP_FIXED_NOREPLACE; guest MAP_SHARED anonymous memory retains identity at fork; anonymous fd ignored; devzero backed by real RAM | 256 MiB user VA layout, eager commit, full flags, shared file writeback, ranges/offsets and backing-object references |
| B21 | unprocessed | Guest NONE/RW data preservation, unmapped ENOMEM and shared-readonly EACCES; latest LTP mprotect01 has three TPASS and exit 0 after native signal repair | Cross-VMA transactions, Linux W+X policy, complete rollback and SMP TLB coherence |
| B22 | implemented_pending_validation | Unmap supports holes and owned PROT_NONE pages; host ownership tests pass | Complete range splitting, backing references and SMP tests |
| B23 | unprocessed | 64-bit pending/mask storage; native signal 33 cancellation and high-number signal tests; tkill/tgkill permission and TGID checks; fatal signals terminate the group | Realtime signals still coalesce in a bitset; siginfo source/queues and complete group stop/continue/default semantics |
| B24 | unprocessed | Canonical Linux sigaction flags and record; query no longer resets action; CLONE_SIGHAND shares actions | Premature restorer rejection; SA_NOCLDWAIT/SA_NOCLDSTOP and complete action/error semantics |
| B25 | unprocessed | Full 8-byte stored mask, NULL-mask query and writable usercopy repaired; shared process pending is separate from TID pending; eligible thread wakeup and blocked/pending/unblock tests pass | Realtime signal queues, complete siginfo and exhaustive mask/error tests |
| B26 | unprocessed | Native rt_sigframe/ucontext/siginfo, 128-byte red zone, altstack, FXSAVE/FXRSTOR and register/mask restoration; musl header offsets independently checked; guest altstack/SIMD/cancellation pass | Complete siginfo contents; bad-frame forced SIGSEGV; MXCSR CPU mask; XSAVE is disabled by current CPU configuration |
| B27 | unprocessed | Interruptible sigsuspend blocks with temporary mask, delivers handler and returns EINTR; saved pre-suspend mask restored from native frame | Nested/default-action and concurrent signal edge cases |
| B28 | unprocessed | Shared-resource exit, group wait, worker exec/CLOEXEC isolation; read/futex restart; nanosleep and timed futex/socket waits return EINTR despite SA_RESTART; partial WAITALL returns its bytes | True vfork; full wait4 rusage/options; complete stop/continue and restart_syscall semantics |
| B29 | implemented_pending_validation | Native timespec/pointer validation, zero sleep, retained deadline, saturation/rounding and relative remaining-time copy; actual implementation passes host sanitizer tests and native guest interruption tests | Exhaustive signal/stop/restart and extreme-duration scheduling tests |
| B30 | unprocessed | Realtime uses wall-clock subsecond state; monotonic uses ticks; reported resolution is actual 10 ms; clock_nanosleep interruption and native alarm/getitimer/setitimer ITIMER_REAL with periodic rearm and exec preservation tested | CPU/dynamic clocks, ITIMER_VIRTUAL/ITIMER_PROF, other timers, clock adjustment wakeups, settime privilege/precision and complete restart behavior |
| B31 | unprocessed | Actual NOFILE/AS soft/hard limits, prlimit64, shared pthread and fork/exec inheritance pass native tests; existing higher fds survive lowering NOFILE | Resources other than NOFILE/AS, full enforcement, capabilities, reaped PID and exec AS limits |
| B32 | implemented_pending_validation | Raw getpriority encoding adjusted | Native process/group/user selection and permission/range tests |
| B33 | unprocessed | Raw getaffinity returns 8 copied bytes; 32-bit PID/length, oversized and short masks, writable output, ESRCH/EFAULT order and cross-user EPERM pass native tests; musl pthread_get/setaffinity_np pass in the native musl SDK | Capability/cpuset/hotplug rules, exited-task lifetime, AP scheduling and full scheduler policy/flag semantics |
| B34 | unprocessed | Audit distinguishes libc reboot API from raw syscall | Linux magic values, command, privilege and argument contracts |
| B35 | unprocessed | Refcounted supplementary groups inherited at fork; getgroups/setgroups widths/order/privilege and group DAC checked by native musl probes | Complete UID/GID/fsuid/capability rules; saved IDs; setuid side effects; signal target permissions and lifetime |
| B36 | unprocessed | Unix STREAM/DGRAM/SEQPACKET, pathname/abstract/autobind, packet boundaries, MSG_TRUNC/PEEK/WAITALL, credentials, blocked fd pinning, readv/writev, shutdown/readiness, FIONREAD and peer-reset behavior pass native guest probes; accepted address metadata no longer shadows a listener binding | Complete flags, options, low-water marks and resource/concurrency cases; UDP/IPv6/INET server behavior |
| B37 | unprocessed | Unix SO_TYPE/ERROR/PEERCRED/PASSCRED/ACCEPTCONN/DOMAIN/PROTOCOL/RCVLOWAT, short buffers, raw write SIGPIPE; OLD/NEW timeouts, WAITALL/PEEK/lowwater and EINTR; SO_ERROR consumes peer reset once; FIONBIO updates the shared OFD | Socket buffer limits and remaining options; INET unsupported options still need repair |
| B38 | unprocessed | Length-delimited abstract addresses and 108-byte paths; native output truncation and cached peer name; accept allocates before dequeue with rollback | All address errors, concurrent namespace changes, INET address contracts |
| B39 | unprocessed | SCM_RIGHTS OFD references/CLOEXEC/CTRUNC/PEEK/discard, Linux stream ancillary barriers and 80 cycles; SCM_CREDENTIALS/SO_PASSCRED, sender validation, stream credential boundaries and zero-byte datagrams; IPC framing/partial writes and explicit service socket modes | All resource exhaustion/concurrent close cases; partial IPC state with direct close(fd); concurrent same-fd consumer sends |
| B40 | unprocessed | Shared native termios 36/44-byte records and flags/cc indexes; raw PTY, TIOCSCTTY and forkpty pass; ioctl request width and TCSETSF input flush repaired | Separate master/slave termios, locking, controlling tty ioctls, VMIN/VTIME, canonical EOF, echo/output processing, queues and complete hangup semantics |
| B41 | unprocessed | QEMU installer framebuffer renders and keyboard events reach GUI | fbdev exact layouts, pan/variable mode errors and reported capabilities |
| B42 | unprocessed | Historical BLKROGET encoding audit retained | Correct request encoding, native block ioctls, usercopy and error tests |
| B43 | unprocessed | Historical EVIOCGRAB argument audit retained | Value-vs-pointer correction and native evdev behavior tests |
| B44 | unprocessed | Real /proc/PID and /proc/self parent directories fix task traversal; getdents64 enumerates live tasks; MemAvailable enables LTP startup; /proc/PID/status provides real IDs and resident RAM, and task snapshots resolve usernames from passwd | Linux stat/cmdline format, remaining status fields, thread/self links, access control, offsets and lifecycle |
| B45 | unprocessed | Native poll validates writable revents, 32-bit nfds, regular-file EOF readiness, POLLNVAL, signal interruption and poll(NULL,0,-1); no-mask ppoll timespec path also passes static/dynamic musl probes | ppoll atomic signal masks, pselect, epoll and complete wait queue/event API semantics |
| B46 | unprocessed | Existing mount subset retained | Filesystem-specific flags/errors, privilege, mount lifetime, busy checks, umount2 semantics |

## Verification Evidence

- Latest normal/UI validation: `proc-status-normal-build-test.log` completes
  the fresh ordinary image, OOBE/login, two Terminal PTYs, pipeline result 5,
  background sleep and nano save/readback. `proc-status-normal-nano.png` retains
  the real unsupported `jobs` diagnostic. `proc-status-ui-serial.log` and
  `proc-status-taskmgr.png` confirm nonzero resident memory and passwd-resolved
  names for live tasks; exited/released tasks retain 0 B. The pre-fix screenshot
  is `tls-scm-taskmgr.png`. `proc-status-glx-1.png` and `proc-status-glx-2.png`
  differ at 13963 pixels within the gear region, and glxgears exits with code 0.
  Rendering is PortableGL software; SVGA3D is unavailable in this QEMU setup.
  `proc-status-default-installer-build.log`, its serial log and screenshot
  confirm the default installer builds and reaches Thanks through keyboard
  navigation. `proc-status-musl-installer.png` covers the musl live services.
  Neither run performs a disk installation or VMware verification.
- Proc status checkpoint: `proc-status-before-serial.log` reproduces the
  missing status file in both musl programs. `proc-status-after-serial.log`
  passes 45 groups per program, including native status PID/TGID, four 32-bit
  UID/GID fields and VmRSS in kB. Residency counts actual user RAM page
  mappings, includes owned PROT_NONE pages and excludes device PFN mappings.
  The paging fixture verifies this distinction. `proc-status-after-host.log`
  passes seven tests, including actual procfs output and the task-snapshot
  consumer's passwd lookup, seven-byte reads and descriptor cleanup.
  `proc-status-musl-installer.png` shows the rebuilt live GUI responding to
  keyboard input. This implements a subset of status; private stat/cmdline,
  fsuid/fsgid setters, complete status fields, thread links and permissions
  remain. Legacy and musl task-snapshot consumers were rebuilt together.
- Additional Linux-only investigation: `tid-wake-linux-reference-serial.log`
  rejects the exploratory assumption that a shared FUTEX_WAIT on a read-only
  anonymous mapping can queue: Linux returns EFAULT. The corresponding kernel
  source is kernel/futex/core.c get_futex_key's read-only anonymous-page check.
  That invalid exploratory expectation was removed before LeonOS testing;
  the established Linux-validated exit tests and their expectations are unchanged.
  Shared futex backing-type validation and file-backed exit-write-fault wakeups
  still need implementation and independent regression tests.
- Affinity checkpoint: `affinity-before-serial.log` reproduces the raw return
  value/length failure; `affinity-after-serial.log` passes static and dynamic
  probes. `affinity-linux-reference-serial.log` validates 33 selected groups
  against Linux v6.12, including another pthread and a different-UID child.
  `tools/test_picolibc_abi.py` passes five adapter tests, including affinity and fcntl in both actual sysroots;
  raw affinity returns a byte count while the libc API returns 0 and clears
  the remaining output buffer. CPU-mask storage is eight bytes under the
  tested kernel configurations; LeonOS still schedules users only on the BSP.
- TLS/TID checkpoint: `tls-before-serial.log` fails the unchanged TLS and TID
  registration tests; `tid-before-serial.log` passes those fixes but reproduces
  unaligned exit-clear failure. `tls-after-serial.log` passes static/dynamic
  probes, including native arch_prctl command width, read-only/cross-page
  output, invalid FS bases, legal unmapped bases and CLONE_SETTLS. FS validation
  uses Linux's four-level TASK_SIZE_MAX (2^47 - 4096), independently of LeonOS's
  256 MiB mapping region. ARCH_SET_GS/GET_GS and other arch_prctl commands remain
  outstanding. set_tid_address registers without touching its pointer; only a
  shared mm with another user clears it at exit, including unaligned words
  crossing physical pages. Invalid/read-only pointers do not fault the kernel.
  `tls-linux-reference-serial.log` validates the same 36 selected groups on
  Linux v6.12. The futex host fixture now models actual shared-mm references
  and verifies that the last mm user leaves the word intact; this corrects
  its former incomplete ownership setup rather than changing a Linux result.
- SCM stream checkpoint: `scm-boundaries-before-serial.log` reproduces the
  wrong receive count on static/dynamic musl; `scm-boundaries-after-serial.log`
  passes all 44 groups per program (including startup). The native test sends
  ordinary `ab`, SCM_RIGHTS plus `cde`, then ordinary `fg`: recvmsg returns
  `abcde` and the fd, retaining `fg`. MSG_WAITALL, MSG_PEEK, partial consumption,
  read and readv obey the same ancillary boundary. Linux v6.12's
  net/unix/af_unix.c unix_stream_read_generic is the source reference; the
  matching independent reference test passes. Large fragmented SCM sends and
  remaining resource/concurrency semantics are not certified by this case.
  `scm-musl-installer.png` shows the final musl live installer at Thanks after
  keyboard input. Its installation payload still uses Picolibc.
- Descriptor checkpoint: `build/musl/fd-boundaries-after-serial.log` records static and
  dynamic `END failed=0`. `stdio-before-serial.log` reproduces closing an
  implicit PTY fd failing; `stdio-after-serial.log` passes the unchanged test.
  `fd-before-serial.log` reproduces the fd-3 allocation failures; the corrected
  implementation passes allocation, dup2 expansion, SCM_RIGHTS lowest-fd
  reception, PTMX at fd 0, pipe failure rollback and lowered-limit tests.
  `tools/test_linux_descriptors.py` compiles the actual syscall helpers and
  scheduler descriptor storage under ASan/UBSan, including injected ENOMEM
  during table growth and OFD reference-count checks.
  `fd-boundaries-before-serial.log` reproduces an unsigned-int fcntl command
  incorrectly retaining register high bits; the final run also covers native
  widths for dup3/pipe2/socket/socketpair, read-only output pointers and a zero
  descriptor limit. `fd-boundaries-linux-reference-serial.log` validates the
  same 32 selected cases on Linux 6.12.0, all exit 0.
- `fd-normal-build-test.log`, `fd-normal-serial.log`, and `fd-normal-nano.png`
  record a fresh ordinary image completing OOBE, two Terminal PTYs, the
  pipeline result 5, a background command without the old /dev/null error,
  and nano save/readback. `jobs` remains unavailable; the screenshot retains
  its real diagnostic. `fd-default-installer-build.log` and
  `fd-default-installer-serial.log` record a rebuilt default installer;
  `fd-default-installer.png` shows keyboard navigation to Thanks. These are
  QEMU results; VMware and actual disk installation remain unverified here.
- The legacy ipctest consumer previously assumed every fd was at least 4.
  It now checks descriptor validity with F_GETFD, distinct socketpair ends,
  and actual I/O/SCM behavior. The independent native allocation tests require
  exact Linux fd values (including 0 and 3); their expectations were unchanged.
- Current checkpoint: `build/musl/proc-status-after-serial.log` records static
  and dynamic probe `END failed=0`. This supersedes the socket unlink and
  pthread EAGAIN failures retained in the historical checkpoints below.
  Coverage includes thread contention, robust owner death, cancellation,
  signal altstack/SIMD/restart, worker exec, descriptor transfer and cycles,
  Unix packet types and 40000-byte multi-iovec MSG_WAITALL. A partial receive
  interrupted by a handler returns its byte count even with SA_RESTART.
  Added coverage includes SCM_CREDENTIALS, short socket options, raw SIGPIPE,
  nanosleep/timed-futex interruption, blocked fd close/reuse, vector packet
  boundaries, timeout expiry and the accepted-socket namespace regression.
  The same checkpoint additionally covers Unix shutdown/poll masks, queued
  byte counts (`FIONREAD`), peer reset after unread data, poll signal
  interruption, 32-bit `nfds` and timeout, read-only regular-file readiness,
  writable `revents` usercopy and FIONBIO across dup aliases.
  Process-directed pending signals are shared independently of TID signals;
  ITIMER_REAL, raw alarm and worker exec timer preservation pass. Futex tests
  include WAKE_OP signed operands, shift encoding, writable second word,
  realtime deadlines and saturation.
  Earlier timeout runs retained genuine failures: an expired EAGAIN was
  mistakenly retried by generic dispatch, then slot reuse exposed address
  metadata shadowing the listener. Both were repaired without relaxing tests.
- `build/musl/proc-status-linux-reference-serial.log`: the identical static musl
  program runs 38 selected heap/thread/signal/Unix/poll/fd/proc groups on an unmodified
  Linux 6.12.0 reference kernel; all exit 0, DONE failures=0. The reference
  kernel has two active vCPUs. This validates the selected expectations,
  not LeonOS multi-core support. Host Linux 7.2 instead fails the new
  SO_PASSCRED-at-accept assertion: current upstream copies flags at connect,
  while v6.12 `unix_sock_inherit_flags` runs at accept. The v6.12 test is kept.
- `build/musl/namespace-installer/thanks.png`: the same musl live image starts
  the desktop/installer and advances from Language to Thanks by keyboard.
- After host power loss interrupted ISO output, `resume-itimer-probe-build.log`
  completes packaging and `resume-itimer-installer.png` again shows Thanks.
  `socket-state-before-serial.log` reproduces three Unix failures; they pass
  after repair. `poll-final-serial.log` reproduces a kernel write fault on
  read-only pollfd memory; `poll-safe-serial.log` verifies EFAULT and no fault.
  `ioctl-before-serial.log` reproduces missing FIONBIO and 64-bit timeout
  interpretation; the current checkpoint passes both without test changes.
- `tools/test_linux_threads.py` exercises the actual futex queue implementation
  under ASan/UBSan. `tools/test_unix_ipc.py` runs the actual IPC consumer on
  host Linux, including partial framing, SCM association and stale bind paths.
  `tools/test_storage_rename.py` checks real ext2 socket nodes on volumes with
  and without dirent filetype, using debugfs and e2fsck as independent readers.
- Native syscall stacks are allocated per enabled CPU (128 KiB), replacing
  the overflowing 16 KiB budget. A static 64-CPU allocation initially collided
  with the fixed middlelayer image; the reproduced boot failure was fixed by
  allocating pages and adding a linker overlap assertion.
- QEMU has four virtual CPUs, but `SMP_USER_SCHEDULER_ENABLED` remains 0.
  These are BSP preemptive thread tests, not simultaneous multi-core user
  execution. AP scheduling and TLB shootdown remain unresolved implementation.
- `tools/test_uapi.py`: shared syscall ownership and 30 standalone C/C++ UAPI
  headers; `tools/test_musl_abi.py`: installed upstream musl constants/layouts
  including native signal context offsets, and static/dynamic allocation,
  TLS and pthread behavior on the host.
- `tools/test_linux_time.py` and `test_runtime_responsiveness.py`: actual
  timespec/deadline code and signal copy to the destination address space,
  including interrupted nanosleep remaining time, pass ASan/UBSan checks.
- `build/musl/service-mode-normal-serial.log` and `service-mode-normal/*.png`:
  fresh normal exFAT image completes OOBE, opens a user Terminal, returns 5
  from `printf hello | wc -c`, opens another PTY, and saves/reads a nano file.
  Task Manager lists 12 tasks. glxgears renders and two frames differ in
  8608 content pixels. RSS=0 and missing usernames remain known limitations.
  This exposed real Unix socket nodes created 0755 denying user connections.
  Public services now explicitly chmod their sockets 0666 and retain peer
  credential authorization; the generic helper defaults to 0600.
- Empty AHCI CD-ROM boot stalled with TFES set and PxCI uncleared. The
  completion loop now reports the hardware error promptly; the real driver
  fixture passes both synchronous/asynchronous cases, and the same normal
  image boots with an empty drive. This is separate from the original
  O_NONBLOCK installer Logo failure. No VMware execution has been performed.
- `build/musl/namespace-installer-fixed-build.log`: ordinary installer build
  succeeds after ordering TCC runtime replacement before manifest/icon
  staging, fixing a reproduced Directory-not-empty race.
- `build/musl/tls-scm-ltp-serial.log`: rerun after time/OFD/socket changes
  has 19 exit-0 tests and two real failures (link and timestamp setup);
  runner exits 1. Source tests and expected outcomes remain unchanged.
- `tools/test_picolibc_abi.py`: both installed fcntl sysroots, raw stat canary
  and termios conversion against actual host Linux. This is not guest stat
  or pthread certification.
- `tools/test_linux_memory.py`: real paging and ELF file-fault/page-cache
  implementation under ASan/UBSan. `tools/test_linux_pty.py`: real PTY raw
  mode and controlling-session isolation. Host Terminal/OOBE/input tests pass.
- `build/musl/probes-pty-serial.log`: both static and dynamic probes report
  startup, heap, memory and PTY PASS. Both report nonblocking-group FAIL only
  at final socket-path unlink (ENOENT), and threads FAIL (pthread_create EAGAIN).
  Their nonblocking fcntl/accept checks preceding unlink passed. Overall exit
  remains failure, not suppressed or reclassified.
- `build/musl/probes-pty.png`: musl live installer remains at its language
  screen after both failing probes exit. `build/musl/installer-musl-enter.png`
  records the earlier keyboard transition to the Thanks screen.
- `build/musl/legacy-installer-uapi-build.log`: full installer build and SDK
  archive succeed. ZIP membership and canonical header content were checked.
- `build/musl/normal-terminal-uapi.log`, `build/qmp-nano-serial.log` and
  `build/images/nano-qmp-smoke.png`: normal SMP QEMU desktop, PTY tabs,
  `printf hello | wc -c` output 5, nano save/readback/exit. The old harness
  reported success despite `jobs: not found`; job control is disabled in the
  BusyBox configuration, and `&` injection was missing. The key mapping is
  corrected; background job control is NOT certified by that run.

## Permission Integration Checkpoint

- `build/musl/permissions-final-probe-build.log`: 247 files built, 202 generated,
  zero errors. Normal userland, installer and musl diagnostic images included.
- `build/musl/permissions-final-probe-serial.log`: both static and dynamic
  upstream-musl probes pass startup, mimalloc heap, memory, PTY and permissions.
  The permission group covers creation/umask, independent UID/GID, supplementary
  groups, owner/group/other selection, EPERM/EACCES, sticky deletion, relative
  and empty-path metadata calls, mkdirat/unlinkat, device DAC, and rejecting
  nonexistent/non-directory/inaccessible components before dot-dot folding.
  Both overall probes still fail: AF_UNIX final unlink returns ENOENT and
  pthread_create returns EAGAIN. Exit status remains 1, with two failed groups.
- `tools/test_linux_permissions.py`: ASan/UBSan real permission implementation
  and persistent OSM metadata tests pass; maximum nested ACL stack use 32080
  bytes, below the 32768-byte budget. Old ACL records and explicit modes survive.
- `tools/test_storage_metadata.py`: actual ext2 image generated by mke2fs;
  mode 6750, UID 70001, GID 90002 and ctime written via the inode adapter and
  independently read by debugfs. Real ext2 allocation counts and FAT/exFAT
  bitmap/ISO read-only accounting are checked. This is a host disk fixture,
  not an ext2 guest or VMware write certification.
- `tools/test_picolibc_abi.py`: four tests pass, including an open() error test
  that failed before converting raw negative errors to -1/errno. Its environment
  configuration consumer was migrated with it.
- `build/musl/permissions-normal-fixed.log`: normal QEMU OOBE/login, desktop,
  Terminal PTY tabs, pipeline result 5 and nano save/readback. This exposed and
  fixed the zero-byte users.db factory seed incompatibility; corrupt nonempty
  databases still fail without being overwritten.
- `build/musl/permissions-fileman/saved.png` and `chown-denied.png`: actual
  property dialog saves mode 640; unauthorized UID:GID change is rejected and
  original identity is reloaded. `permissions-terminal-results.png` records
  BusyBox chmod 000 denying read, chmod 600 restoring it, and chown EPERM.
  BusyBox chmod/umask/chown/geteuid/getgroups shims now make real syscalls.
- Existing application create calls with a legacy mode of zero were migrated
  to explicit file/directory modes; authd homes and account database use 0700
  and 0600. Installer copies file ownership/mode; fileman/tar preserve file
  permission bits. Fresh ordinary images now include /tmp; init creates it
  with 1777 on older installations only when it is absent.

## Account and Rename Checkpoint

- `build/musl/permissions-rename-probe-serial.log`: static and dynamic musl
  startup/heap/memory/PTY/permissions/proc_directories/rename_replacement PASS.
  Both overall probes still exit 1 for the same socket unlink and pthread
  failures. The directory test checks real proc traversal, EINVAL/EFAULT and
  cursor preservation. The rename test checks replacement data, mode, UID/GID,
  file/directory mismatch and nonempty-directory rejection on FAT32.
- `tools/test_storage_rename.py`: real ext2 backend on two mke2fs images, with
  and without filetype; both replacement data checks and e2fsck pass. This
  exposed and fixed writing filetype into a volume without that feature.
- `tools/test_osmlayer_acl.py`: replacement drops the destination's stale
  permission record; source mode and 32-bit UID/GID survive. Stack budget
  remains 32080 bytes. `tools/test_oobe.py` now passes seven groups, including
  passwd/group formatting, bounded credential fields and actual admin-password
  verification before clearing the local password buffer.
- `build/musl/permissions-name-success.png`: normal exFAT image completes OOBE
  and login; /etc/passwd maps root to existing UID:GID 1:1; group lookup and
  BusyBox `chown root:root` succeed. `permissions-commands-final.png` confirms
  chmod 000 denies reads, restoration to 640, unauthorized chown denied, and
  the pipe result 5. This does not make the named account UID 0.
- `build/musl/permissions-accounts-reboot.png` and its serial log: cold boot
  of a standalone persisted disk keeps mode 640, account UID:GID 1:1 and
  file contents `hello`; regenerated passwd/group still support named chown.
- `build/musl/permissions-taskmgr-fixed.png`: process listing recovers from the
  traversal regression and displays 13 tasks. This historical screenshot retains RSS=0 and missing usernames; the later
  proc-status checkpoint repairs the data path, with UI verification recorded above.
- `build/musl/permissions-rename-installer/thanks.png`: latest musl installer
  renders and advances by keyboard beyond its language page. Build logs:
  `permissions-rename-probe-build.log` (145 built, 23 generated, zero errors),
  `permissions-rename-normal-build.log` and `permissions-final-sdk.log`.
- `build/musl/permissions-final-installer-build.log`: default installer and
  SDK build succeed, five files built and 22 generated with zero errors.
  `permissions-final-installer/thanks.png` confirms the default installer
  also passes Logo and accepts keyboard navigation in QEMU. Installation onto
  a target disk and the complete installer wizard were not exercised here.
- Scope and operational limitations are also documented in
  `docs/POSIX_PERMISSIONS_2026-09-08.md`. In particular, FAT/exFAT metadata has
  64 records per directory; fd identity, cross-directory rename and crash
  transactions remain unfinished. No VMware run has been performed.

## LTP Guest Results

Unmodified LTP source revision: `3a64d78f58bdceba93ed321e91215fb969a047ed`.
Latest evidence: `build/musl/tls-scm-ltp-serial.log`. Sources and expected
results are unchanged. The 14 pthread/synchronization cases are upstream Open
POSIX 1-1.

| Test | Result | Remaining failure |
| --- | --- | --- |
| getcwd01 | PASS, exit 0, five assertions | Broader syscall coverage remains |
| fchmod01 | PASS, exit 0, eight mode cases | Not full descriptor lifecycle certification |
| chown01 | PASS, exit 0 | Only this LTP case plus separate musl matrix |
| mprotect01 | Three TPASS; exit 0 | Earlier signal 48 warning resolved |
| fcntl01 | Exit 0 | Earlier signal 48 warning resolved |
| fstat02 | TBROK, exit 2 | link ENOSYS |
| chmod01 | TBROK, exit 2 | Timestamp setup via utimes/utimensat ENOSYS |
| pthread_create_1-1 | PASS, exit 0 | Broader clone/attribute coverage remains |
| pthread_join_1-1 | PASS, exit 0 | Not all join/detach races |
| pthread_mutex_lock_1-1 | PASS, exit 0 | PI futex operations remain unsupported |
| pthread_cond_wait_1-1 | PASS, exit 0 | This does not certify alarm or timed waits |
| pthread_cancel_1-1 | PASS, exit 0 | Asynchronous cancellation case |
| pthread_key_create_1-1 | PASS, exit 0 | Key creation case, not exhaustive TLS lifecycle |
| pthread_barrier_wait_1-1 | PASS, exit 0 | Barrier lifecycle only |
| pthread_rwlock_rdlock_1-1 | PASS, exit 0 | Read/write lock edge cases remain |
| pthread_once_1-1 | PASS, exit 0 | Once recursion/error paths remain |
| pthread_mutex_timedlock_1-1 | PASS, exit 0 | Clock and cancellation edge cases remain |
| pthread_cond_timedwait_1-1 | PASS, exit 0 | Clock and cancellation edge cases remain |
| pthread_mutex_trylock_1-1 | PASS, exit 0 | Contention/error paths remain |
| sem_timedwait_1-1 | PASS, exit 0 | Semaphore signal/clock edge cases remain |
| pthread_spin_lock_1-1 | PASS, exit 0 | SMP execution remains disabled |

The latest LTP runner reports two failures and exits 1. Assertions and expected
results were not weakened. MemAvailable and unlinkat cleanup fixes removed
earlier harness startup failures, exposing these specific ABI gaps.

QEMU validation does not establish VMware behavior. OOBE guest account setup
and keyboard input were exercised by the normal desktop test. VMware remains
untested. Remaining ports, all B01-B46 contracts, 159 missing dispatches and the
five timer rows pending validation remain in scope. The CSV contains 84 originally
routed uncertified calls, 110 newly implemented but not fully certified calls and
17 excluded Linux reserved/ni entries.

## Resource checkpoint and default libc migration (2026-09-08)

- NOFILE/AS now retain actual soft/hard limits. prlimit64 validates native argument
  widths, pointer aliasing, permissions and old-limit write faults; pthreads share
  the limits, fork copies them and exec preserves them. AS charges initial/growing
  stacks and MAP_FIXED net growth, preserving old mappings when the new limit fails.
- `rlimit-as-after-serial.log`: static/dynamic 47 groups each, END failed=0.
  `rlimit-linux-reference-serial.log`: 40 selected groups, failures=0 on Linux 6.12.
  Host resource and memory helper sanitizers passed. These remain partial results:
  all other resources return ENOSYS; capabilities, dead/reaped target lookup and
  full exec/brk/mremap enforcement are unfinished.
- The user's final policy is complete removal of Picolibc. Default application
  links now use musl CRT, mimalloc and libleonos.so.2; installer policy libraries
  use that same ABI. The old libc source dependency, build script, POSIX wrappers
  and custom loader have been deleted. BusyBox and nano standard functions now
  resolve through musl instead of local libc stubs.
- `python3 build.py run release`: final ordinary/installer/SDK build completed
  with 557 compiled files, 207 generated files and 0 errors. The fresh raw musl
  QEMU checkpoints through `abi-probes-qemu-20260908-16.log` report static and
  dynamic `END failed=0`; `tools/test_uapi.py`, `tools/test_musl_abi.py`,
  `tools/test_linux_pty.py` and `tools/test_linux_memory.py` also pass.
- Picolibc resource-header tests discovered RLIMIT_NOFILE=5, RLIMIT_AS=6 and
  signed-width infinity in the retired headers. The old wrapper tests are removed
  with that runtime; native musl and Linux-reference resource cases remain.
