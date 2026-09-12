# sudoers / PAM implementation and evidence

Status: INCOMPLETE. This is the current main agent's running record for
`SUDOERS_PAM_AGENT_PROMPT.md`. The production broker is now removed; historical
broker results below are not evidence for the replacement. No commit, push, host account mutation,
subagent or separate task is authorized or used.

## Integration steering: 2026-09-12

### sudo su RLIMIT_CORE warning

Upstream sudo's core-dump protection and default sudoers `rlimit_core=0,0`
policy call getrlimit/setrlimit. Resource 4 previously reached ENOSYS, causing
a warning even when authentication and command execution succeeded.

CORE now uses the process-wide limit state, with Linux v6.12 INIT_RLIMITS
default `{0, RLIM_INFINITY}`. getrlimit(97), setrlimit(160), and prlimit64(302)
use the existing size, privilege, target-process, aliasing and copy-fault
contracts. Threads share it; fork copies it and exec retains it. No sudo,
sudoers or PAM policy is changed to hide the warning.

LeonOS does not generate core files. This is the configuration-equivalent
behavior of Linux with CONFIG_COREDUMP disabled, where RLIMIT_CORE still
exists: see pinned `build/linux-6.12/include/asm-generic/resource.h:INIT_RLIMITS`
and `build/linux-6.12/include/linux/coredump.h`. A positive limit is an upper
bound, not a request or guarantee to create a core. This change does not claim
an ELF core writer, core_pattern, pipe helpers or core-dump format support.

`tools/test_linux_resources.py` failed on the new CORE case before the fix and
passes under ASan/UBSan after it, including shared state, CAP_SYS_RESOURCE,
soft/hard ordering and commit-before-output-EFAULT.
`tools/tests/core_limit_runtime_probe.c` passes on host Linux and QEMU: raw
get/set/prlimit, pthread sharing, independent fork limits, exec inheritance,
and unprivileged hard-limit enforcement. The GUI runner's `--core` option
also authenticates the public test account through upstream sudo and executes
upstream `su -c` with a child that verifies UID 0 and CORE `{0,0}`; captured
output must have no ENOSYS/setrlimit warning.

Evidence: `build/pam-desktop-debug/core-host.log`,
`build/core-limit-verified/{serial.log,core-sudo,flock-result}`.
The final automated run is `build/core-limit-complete/`, with
`core-sudo` also asserting `CORE_EXIT=0` and a completed post-sudo cat/sync;
runner output is `build/pam-desktop-debug/core-complete-qemu.log`.
Two intermediate runner attempts failed on premature keyboard input and an
incorrect serial-log wait pattern; the guest command itself succeeded. The
runner now waits for the actual sudo parent to be reaped before typing again.
VMware is not verified. Other resource types remain outside this narrow fix.

The formal installer was rebuilt with 0 errors:
`build/pam-desktop-debug/core-installer-build.log`.
Current `build/images/leonos4-installer.iso` is 813201408 bytes, SHA256
`52d6e835eeae76335a89ccaef81993fc6d92818ec30baa489bbb887e06b4b5b2`.
It supersedes the earlier flock-repair ISO digest recorded below. Existing
installed disks need their kernel updated before this behavior changes.

### Desktop stalled after successful PAM login

The VMware serial report ended after two successful unix_chkpwd helpers. The
same failure was reproduced on a standalone ext2 QEMU disk. Added non-secret
PAM phase messages proved authentication, account checks, credential setup,
session modules and marker publication all succeeded. Desktop then called
`flock(marker, LOCK_EX | LOCK_NB)` to recognize the live PAM-owned marker. Its
correct EWOULDBLOCK result was treated as storage EAGAIN by the syscall return
path, so the compositor retried forever instead of observing a live session.

The syscall boundary now returns nonblocking flock conflicts to userspace.
Linux v6.12 `fs/locks.c:SYSCALL_DEFINE2(flock)` similarly derives its wait policy
from LOCK_NB, independently of O_NONBLOCK. The new raw syscall regression also
exposed stale flock owner pointers when an inline file description was promoted
for dup/fork/I/O pinning. Promotion now retargets both exclusive and shared lock
owners; allocation failure leaves the original object and locks intact.

Separately, serviced was launched through the ordinary application launcher
before a PAM session existed, causing a child exit with code 126. Desktop now
forks/execs the fixed system-service path as root and observes/retries failures.
The ordinary application launcher's session credential checks are unchanged.

Verified locally:

- `tools/test_linux_descriptors.py`: production helpers under ASan/UBSan,
  including both lock owner types, ENOMEM rollback and final close.
- `tools/test_record_locks.py` and `tools/test_pam_login_case.py`: PASS; the
  latter still checks wrong passwords and actual versus spurious cancellation.
- `tools/tests/flock_nonblock_runtime_probe.c`: identical static musl raw
  syscall probe passes on host Linux and LeonOS; readonly OFD conflicts,
  dup/fork lifetime, blocking EINTR, unlock and reacquisition are exercised.
- `build/pam-desktop-final/`: QEMU desktop login, serviced startup, Terminal
  UID quartet 1000/1000/1000/1000, raw probe failures=0, and subsequent cat/sync.
  Guest results are read back from the scratch ext2 filesystem; screenshots
  alone are not used as the pass condition.
- `build/pam-desktop-debug/installer-build.log`: formal installer-image build
  completed with 0 errors.
- `build/pam-installer-verified/`: fresh ext2 installation from that ISO via the
  TTY installer, hard-disk boot, GUI login with alice's mixed-case/symbol
  password, Terminal UID quartet 1000/1000/1000/1000, raw flock failures=0,
  and subsequent cat/sync all PASS. Summary:
  `build/pam-desktop-debug/installer-qemu.log`.

Installer artifact: `build/images/leonos4-installer.iso`, 813201408 bytes,
SHA256 `64a9c6b511551dab29db3c42aea3a5b669c89411f0b3df227b5026ea9d2aac72`.
QEMU was configured with two vCPUs, but the guest reports one online CPU because
AP scheduler participation is disabled. This is not an SMP stress result.
VMware has not been rerun after this fix; neither the entire flock ABI nor all
PAM/session/security behavior is certified by these targeted regressions.

Reproduce after building the installer (only disposable build-directory disks
are created or changed):

```sh
python3 build.py run installer-image
python3 tools/test_pam_desktop_qemu.py --iso build/images/leonos4-installer.iso --output build/pam-login-check
```

The installer route uses the existing alice/root test fixture, with a 32-byte
mixed-case/symbol password for alice. `--disk` instead accepts a standalone raw
image with the standard test/test fixture. Neither route alters host accounts
or injects the regression executable into the production ISO.

### Correct-password GUI login failure repair

The production GUI adapter and real musl PAM DSOs reproduced the reported
mixed-case-password failure: authentication succeeds, then pam_limits probes
all resources. Missing kernel resources return ENOSYS, so upstream init_limits
aborts the session with PAM_ABORT (26). The adapter incorrectly translated
PAM_ABORT into ECANCELED (125). A wrong lowercase variant instead returns
PAM_AUTH_ERR/EACCES. This evidence does not implicate uppercase input handling.

The explicit platform patch
`patches/linux-pam/0003-limits-handle-unimplemented-resources.patch` recognizes
ENOSYS during resource enumeration, while rejecting session setup if a matching
limits policy requests an unavailable resource. Other probe errors still abort.
This is a documented PAM adaptation, not implementation or certification of
the missing Linux rlimits. The module remains required in common-session.
The GUI maps only an actual canceled conversation to ECANCELED; PAM_ABORT and
internal conversation errors remain failures. Safe diagnostics record the
failed phase and numeric PAM status, never the password or authentication token.

`python3 tools/test_pam_login_case.py` passes on the host with actual target
musl libpam, pam_unix, pam_limits and yescrypt: correct mixed-case login,
wrong-case rejection, real NOFILE soft limit 64, unavailable CPU policy denial,
unexpected resource error and conversation cancellation distinction. The test
injects the kernel resource results at getrlimit and substitutes group-setting
calls because unprivileged user namespaces prohibit setgroups. It therefore
does not validate kernel credentials, keyboard events or a VMware GUI session.
The initial test reproduced open-session status 26 / errno 125 before repair.
Full replacement QEMU/VMware acceptance and missing kernel limits remain open.
The final `python3 build.py run userland` completed with zero errors; its log is
`build/auth-upstream/pam-login-userland-build.log`. The post-build regression
passed in `build/auth-upstream/pam-login-case.log`. No ISO/VMDK was rebuilt for
this repair.

Subsequent installer policy change: the user explicitly requested automatic
wheel membership for the initial installer account. Fresh account seeding now
writes that membership to group and gshadow, and the default sudoers wheel rule
requires the caller's password. UID/GID remain 1000. Later-created accounts and
existing memberships are not changed. The focused host regression is
`tools/test_installer_wheel.py`; no replacement guest acceptance or ISO rebuild
is claimed for this policy change. Earlier empty-wheel descriptions below are
historical.

The user explicitly paused further tests and required replacement of every old
authd entry. New integration changes therefore remain **runtime unverified**.
Builds and source review continue; earlier successful guest runs exercised an
older integration and must not be presented as validation of this replacement.

- [x] Remove authd startup/PID tracking and its component from production.
- [x] Move its modified source/protocol to `tools/tests/legacy_authd` solely to
  preserve historical regressions and working-tree changes; not a production build input.
- [x] Replace sudo/su application entries with exec of the official full CLI.
- [x] Replace Fileman broker calls with upstream sudo -A and a fixed worker;
  every command receives a fresh sudoers policy decision; results use a pipe.
- [x] Replace account queries with musl passwd/shadow lookup; root management
  invokes upstream useradd/usermod/chpasswd. Remove kernel password operations.
- [x] New installer writes passwd/shadow/group/gshadow using the transaction
  helper and random yescrypt salts. Populated AUS2 updates fail before overlay.
- [x] TTY enters upstream shadow login; GUI calls real PAM authenticate/account,
  expired-password change, credentials and session operations. Login retains
  its PAM handle through logout; desktop children receive session environment
  and available resource limits through root-owned state.
- [x] Replace password-change dialogs with a terminal running official passwd.
- [x] Add production sudoers/PAM configuration and official payload staging.
- [x] Complete kernel/userland compilation, installer and SDK packaging; remove stale SDK headers.
- [x] Finish current source review and repair integration defects found (records 59-66).
- [ ] Run replacement acceptance tests: paused by explicit user instruction.

Build logs and final artifacts are listed in the delivery section below.
Full runtime compatibility remains incomplete, including the CPU and kernel
gaps already recorded below. Older leonos4.iso/raw/vmdk images have not been
regenerated for this integration and must not be used as the new delivery.

CPU evidence correction: historical LeonOS commands requested `-smp 2`, but
`kernel/ntclks/arch/x86_64/smp.c` sets `SMP_USER_SCHEDULER_ENABLED` to zero.
Their serial logs report one online CPU, including credentials guest02 and
tty-session guest09. Every such result below is single-CPU, including musl's
thread coordination tests. Actual LeonOS SMP synchronization and validation
remain required. The fixed Linux reference now independently asserts two online
CPUs; that does not establish LeonOS SMP coverage.

## Delivery, 2026-09-12

All paths in this record are relative to
`/home/xiaobai/Projects/Projects/LeonOS-4`. No commit or push was performed.
The final installer target also regenerates the SDK from its current runtime;
the hashes below refer to that final packaging, not an earlier standalone SDK.

| Command | Actual result | Log under build/auth-upstream |
| --- | --- | --- |
| `python3 build.py run kernel` | Exit 0; 0 errors; 0.3 s final run | integration-kernel-final.log |
| `python3 build.py run userland` | Exit 0; 0 errors; 124.8 s; installer subsequently rebuilt for explicit retired-path cleanup | integration-userland-final.log |
| `python3 build.py run installer` | Exit 0; 0 errors; 156.2 s; includes final installer code and SDK | integration-installer-final.log |
| `python3 build.py run sdk` | Exit 0; 0 errors; also regenerated in final installer target | integration-sdk-build.log |
| `git diff --check` | Exit 0, no whitespace errors | Current source inspection |
| Behavioral tests after pause | NOT RUN, as explicitly requested | No new runtime acceptance evidence |

Build commands used the project-local `build/auth-upstream/tmp` as TMPDIR and
`http_proxy`/`https_proxy` set to `http://127.0.0.1:12334`. Logs contain compiler
warnings; zero errors does not mean zero warnings. Initial libxcrypt header,
installer import and read-only staging failures remain in their earlier logs.

| Final artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| build/images/leonos4-installer.iso | 813201408 | ad19a107ad3113fdd878be1cc9875ccfbe65b2abf21dc4dd191b35a0eeef31d4 |
| LeonOS4-Developer-SDK.zip | 16266991 | 11d4ebbe604b5016fc1bfa783796fcb5100938bc4347cfe8ab6fb1f80a31831b |
| build/system/kernel.sys | 1134112 | 519d6872788501a3f027c04e19d3b4eb56bd1b18758137ffbd8659767f21c44c |

Installer ext2 content is `build/install/root.fat` (the suffix is historical).
The unpacked installed payload is `build/install/root/install/root`; userland
binaries are under `build/userland`, and the shared runtime is
`build/system/lib/libleonos.so.2`. Older `leonos4.iso`, raw and VMDK images are
not refreshed deliverables for this change.

Read-only artifact inspection after the final build established:

- `debugfs -R 'stat /install/root/...' build/install/root.fat` reports root:root
  ownership, sudo/su/passwd mode 4755, sudoers 0440, shadow/gshadow 0600. The
  retired usr/bin/authd and usr/lib/leonos/apps/authd paths are absent. This is
  image metadata evidence, not a runtime set-ID acceptance test.
- `readelf -d build/system/lib/libleonos.so.2` reports actual dependencies on
  libpam.so.0, libcrypt.so.2, libmimalloc.so.3 and libc.so.
- `zipinfo -1 LeonOS4-Developer-SDK.zip` includes PAM application/module
  headers, the new pam_session.h, libpam and libcrypt; no authd/auth_db headers.
  The two SDK libleonos.a members were extracted under
  `build/auth-upstream/integration-sdk-inspect` and inspected with llvm-nm:
  leonos_pam_login/session_wait exist, and retired authd/login/elevation/password
  entry definitions do not. Fresh archive generation also removes stale members.
- Production source/config search finds only explicit retired-path cleanup and
  historical documentation mentions, with no daemon component, startup call,
  socket constant or retired public API. Active PAM/sudo configuration and
  account authority are described in `SUDO_AND_ELEVATION.md`.

The full task remains INCOMPLETE. Required kernel work and replacement runtime
acceptance are listed in the matrix and final self-review answers. In particular,
the user's test pause is not evidence that GUI/TTY login, FILEOP, SS12 startup,
new installation or update installation has passed on the new image.

## Implementation order

- [x] Read the task specification and record the dirty workspace baseline.
- [x] Check repository and ancestor AGENTS.md: none applies to this work.
- [x] Reconcile production login, password, account, GUI, startup and SDK consumers.
- [x] Pin official sudo, Linux-PAM, util-linux and libxcrypt sources, checksums and licenses.
- [ ] P01/P03: credentials, capability checks and filesystem identities.
- [ ] P02: held executable objects, set-ID, NNP/nosuid, secure auxv, proc isolation.
- [ ] P04/P05: message identity, descriptor lifetime and terminal sessions.
- [ ] P06: real module runtime, random source, clocks, locks and enforced limits.
- [x] Build upstream sudo/PAM/su against the existing pinned musl and stage them in production.
- [x] Replace account authority and production login/password/elevation consumers in code; runtime acceptance remains pending.
- [ ] Run upstream, host sanitizer, raw syscall, LTP and isolated guest tests.
- [x] Build kernel, userland, installer and SDK; record final image digest.
- [ ] Re-read final functions and diff, repair findings and rerun affected tests.

## Initial source reconciliation (historical baseline)

The following records what was found before production replacement. Any mention
of an active broker or pending policy activation here describes that baseline;
the integration section and current matrix below describe the final source.

- `include/leonos/auth_db.h` in the prompt is actually
  `include/uapi/leonos/auth_db.h`.
- `syscall_process.c` already implements saved and filesystem ID fields,
  capget/capset and dumpable changes. Their existence does not establish Linux
  semantics: initially setuid/setgid used real UID privilege checks, and capset
  permitted UID 0 to bypass permitted/effective subset checks. Both are now
  repaired. The main syscall dispatcher also omitted capget/capset routing;
  the first real guest probe found this and the second passed after repair.
- `permissions.c` initially treated fsuid/fsgid zero as an unset value and
  fell back to effective IDs. Zero is now used as a valid identity. Creation,
  chmod, chown and sticky checks now use fs IDs and separate capabilities.
- `accounts.c` uses AUS2 and custom PBKDF2. The old permissions document's
  AUS1 description and acceptance of a nonzero-UID root account are obsolete.
- Current `sudo_policy.h` permits administrator execution by target password
  or UID/session cache, without command policy. Existing broker tests cannot
  prove upstream sudoers behavior.
- Existing rlimits initially implemented NOFILE, AS, SIGPENDING and STACK.
  NPROC now counts live threads and unreaped children by real UID, enforces
  fork/clone, and defers over-limit UID changes to exec. Other resources still
  return ENOSYS; complete pam_limits enforcement is not established.
- AT_UID/AT_EUID/AT_GID/AT_EGID/AT_SECURE already exist in
  `kernel/ntclks/user/userland.c:prepare_user_exec_stack`. AT_SECURE currently
  includes exec capability transitions and unequal IDs. Ordinary exec now resets
  saved/fs IDs and applies bounding/ambient/securebits/NNP transitions; actual
  held ext2 ELF set-ID transitions and nosuid are now implemented and the five-case
  Linux/LeonOS probe passes. The same dynamic probe also verifies malicious
  LD_PRELOAD/LD_LIBRARY_PATH rejection under set-ID, and ordinary loader behavior
  when NNP/nosuid suppress elevation; this is focused evidence, not a full audit.
- `/dev/random` and `/dev/urandom` previously returned zero bytes and getrandom
  used PID/time fallback. Both now require successful RDRAND and fail on entropy
  failure. This is a hardware-backed source, not Linux's entropy pool/CRNG;
  machines without RDRAND cannot start userland with the current implementation.
- Linux-PAM's fallback salt generation used MD5 over PID/time and uninitialized
  memory. The recorded platform patch rejects entropy failure; official libxcrypt
  4.5.2 supplies crypt_gensalt_rn and supported hash formats, without replacing musl.
- The password validator currently counts 1-32 valid UTF-8 scalar values and
  rejects Unicode whitespace, not 1-32 bytes. This task preserves that existing
  product rule in a real PAM password-stack module. Isolated host and Linux 6.12
  runtime checks pass; production service activation remains pending.

## Capability Matrix

All rows are required unless explicitly described as external integrations.
"Not started" means 未处理; "Implemented, unverified" means 已实现待验证;
"Verified" means 已验证 at the stated layer only; "Blocked" means 存在具体阻塞.

| Requirement | Upstream contract | Current location | Test / evidence | Status and gap |
| --- | --- | --- | --- | --- |
| P01 real/effective/saved/fs IDs | Linux v6.12 kernel/sys.c, security/commoncap.c; musl src/unistd/setxid.c | syscall_process.c, sched.h, sched.c | capabilities-host.log; credentials-linux-6.12.log; auth-credentials-guest-02/serial.log | Verified for tested contracts: capability-based setters, saved IDs, fs sentinel, permanent drop; kernel account/desktop/cwd side effects removed. Exec/securebits focused tests also pass; full concurrent lifecycle audit remains |
| P01 supplementary groups and thread lifetime | Linux kernel/groups.c, kernel/cred.c; musl src/thread/synccall.c | permissions.c, syscall.c, sched.c; libc launch/authd_client | Same raw probe tests groups, fork, per-thread raw IDs and musl process-wide setuid on one online LeonOS CPU; auth-installer-guest-01 TTY/desktop | Verified for single-CPU probe and existing consumer workflow; multicore and exec/exit stress pending |
| P01 capabilities | Linux v6.12 security/commoncap.c, kernel/capability.c | syscall_process.c; syscall.c; sched.c; userland.c | capabilities-host.log; exec-credentials-linux-6.12-02.log; auth-exec-credentials-guest-02/serial.log; setid guest below | Verified subset/bounding checks, root drop, securebits locks, KEEP_CAPS, ambient lifecycle, file set-ID/fs-ID fixups and exec. File capabilities and namespaces remain absent |
| P02 privileged exec and rollback | Linux fs/exec.c, fs/binfmt_elf.c, security/commoncap.c | syscall.c, user/userland.c, user/elf.c | exec-rollback-linux-6.12-03.log; auth-exec-rollback-guest-04/serial.log; exec-credentials and setid guests | Verified invalid ELF rollback preserves credentials, CLOEXEC FDs and sibling thread across 16 attempts; valid exec succeeds. Missing/invalid/non-executable interpreters return ENOENT/ELIBBAD/EACCES. Ext2 held exec and set-ID now implemented; script behavior and concurrent commit failure stress incomplete |
| P02 no_new_privs | Linux v6.12 kernel/sys.c:PR_SET/GET_NO_NEW_PRIVS; task flag inheritance | syscall_process.c:syscall_process_prctl; sched.c; sched.h | credentials-linux-6.12.log and auth-credentials-guest-03/serial.log; setid and secure-loader probes | Verified fifth-argument, per-thread, fork/exec and irreversibility checks, plus actual set-ID suppression and ordinary loader environment under NNP |
| P02 execveat/fexecve | Linux v6.12 fs/exec.c; pinned musl src/process/fexecve.c | syscall.c; userland.c; permissions.c | fd-exec-linux-6.12-02.log; auth-fd-exec-guest-04/serial.log | Verified held unlinked ELF via O_PATH/read FD, actual musl fexecve, CLOEXEC, AT_EXECFN, permission change after open, held inode set-ID, empty argv and invalid flags/FD/symlink rejection. Renamed dirfd and script FD reopening remain incomplete |
| P02 script interpretation | Linux v6.12 fs/binfmt_script.c and fs/exec.c | syscall.c:exec_resolve_scripts | script-exec-linux-6.12.log; auth-script-exec-guest-03/serial.log; script-exec-host-02.log | Verified shebang optional argument, script set-ID ignored, final ELF interpreter set-ID honored, missing/non-executable/empty interpreter, recursion limit and CLOEXEC script rejection. Production parser sanitizer/fuzz and argument allocation rollback pass. Actual /dev/fd script reopening and full Linux argument limits remain incomplete |
| P02 secure loader | Linux binfmt_elf.c; pinned musl ldso/dynlink.c | user stack construction, musl | auth-secure-loader-01/linux-6.12.log and guest/serial.log | Verified same dynamic binary/DSOs: set-ID ignores malicious LD_PRELOAD and LD_LIBRARY_PATH and loads trusted dependency; NNP/nosuid suppress privilege and use ordinary loader environment. Broader environment/interpreter race and proc isolation coverage remains |
| P02 ptrace/proc/dumpability | Linux kernel/ptrace.c, kernel/cred.c, fs/proc | syscall_process_vm.c, procfs.c | tools/test_linux_process_vm.py plus privileged exec tests | Not started: existing partial enforcement needs full audit |
| P03 DAC and fs identities | POSIX.1-2024; Linux fs/namei.c, fs/attr.c | permissions.c | tools/test_linux_permissions.py, permissions-host.log | Verified at sanitizer layer: real vs fs IDs, separate DAC_OVERRIDE/READ_SEARCH/FOWNER/CHOWN/FSETID, zero IDs, owner/group/other, path search, sticky, umask and setgid inheritance. Guest persistent metadata coverage pending |
| P03 storage and executable lifetime | Linux v6.12 fs/open.c, fs/inode.c, kernel/fork.c:set_mm_exe_file | syscall.c, storage_inode.c, storage_ext2*.c, sched.c, syscall_mm.c | inode-lifetime-linux-6.12-02.log; auth-inode-lifetime-guest-03/serial.log; inode-storage-host.log; memory-host.log | Verified four cases: held unlink, replacement, lazy mmap and executable write access; production ext2 reference/reclamation and cache invalidation tests pass. Non-ext2 references, write/mmap cache coherence, orphan recovery and reboot/fault stress remain incomplete. Linux v6.12 permits writing an executing ELF; do not restore older deny_write_access behavior |
| P02 file set-ID and mount enforcement | Linux v6.12 fs/exec.c, security/commoncap.c, fs/namespace.c | userland.c, permissions.c, storage_mounts.c, syscall.c, syscall_mm.c | setid-linux-6.12.log; auth-setid-guest-03/serial.log | Five raw cases pass: real/effective/saved/fs IDs, supplementary groups and auxv, NNP, nosuid, noexec and mount privilege. Guest02 used the wrong completion marker; corrected guest03 passes. Scripts and file capabilities incomplete |
| P03 atomic account/policy writes | Linux fs/locks.c, fs/sync.c; upstream writers | userland/auth/account_store.c; syscall.c; storage | account-store-host-02.log; account-store-linux-6.12-02.log; auth-account-store-guest-01/serial.log | New transaction helper verified for lock/concurrent writers, short-write/EINTR/ENOSPC/EIO and replay after process exits at sync/rename boundaries. Production installer/account consumers, external-tool coordination and actual power-cut persistence remain incomplete |
| P03 POSIX record locks | Linux v6.12 fs/locks.c; sudo lib/util/locking.c; password writers | syscall_locks.c; fd table lifecycle in syscall.c/sched.c; signal.c | record-locks-linux-6.12.log; auth-record-locks-guest-02/serial.log; record-locks-host-03.log | Verified range conflicts, split/merge, fork, close-any-FD release, negative length, EINTR; sanitizer verifies allocation rollback and bounded deadlock detection. Fixed 256 range limit returns ENOLCK. OFD lock commands and broader CLONE_FILES/exec/close races remain unverified |
| P03/P06 password file locking | pinned musl src/passwd/lckpwdf.c; PAM passverify.c | patches/musl/0001-enforce-password-file-lock.patch; tools/build_musl.py | password-lock-linux-6.12.log; auth-password-lock-guest-01/serial.log | Verified real lock across independent exec'd processes, persistent lock inode, 100 cycles, duplicate lock/unlock errors and symlink rejection. Upstream stub returned 0 without locking; patched musl preserves pinned source in an isolated verified build tree. Concurrent PAM update stress and fork/thread edge cases remain |
| P03 device synchronization | Linux v6.12 fs/sync.c; ATA FLUSH CACHE and NVMe Flush | storage_sync.c, storage_ide/ahci/nvme.c, syscall.c | storage-sync-host-02.log; fsync-linux-6.12.log; auth-fsync-guest-02/{serial.log,storage-trace.log} | Verified focused raw reference and one-online-CPU guest, including O_PATH errors; QEMU records eight actual ATA 0xe7 flush commands. Host injection verifies error propagation and async state restore. First failed O_PATH guest retained. Power-cut persistence, NVMe guest and device fault injection still required |
| P04 credentials and IPC | Linux v6.12 net/core/scm.c, net/unix/af_unix.c | syscall_socket.c, unix_ipc.c, authd/main.c | socket-credentials-linux-6.12.log; auth-socket-credentials-guest-02/serial.log; ipc-credentials-linux-6.12.log; auth-ipc-credentials-guest-01/serial.log; auth-authd-credentials-guest-01/serial.log | Verified connection vs message identity and separate capability checks for explicit PID/UID/GID. Production framing rejects inherited and mixed-sender requests, cleans transferred FDs and wipes buffers; actual authd rejects a child on the parent's connection and accepts fresh ordinary-user connections. Stable PID lifetime references, connection-FD transfer stress and full request authorization remain incomplete |
| P04 shared OFD lifetime | Linux v6.12 fs/file.c, fs/open.c, net/core/scm.c, drivers/tty/pty.c | syscall.c, syscall_socket.c, pty.c, sched.c | pty-ofd-linux-6.12-04.log; auth-pty-ofd-guest-03/serial.log; pty-ofd-host-04.log; pty-ofd-socket-host.log; pty-ofd-cloexec-host-04.log | Verified PTY shared status and independent descriptor flags through dup/fork/SCM, 24 repeated cycles, blocked-read FD replacement, queued owner exit, control truncation, receiver FD exhaustion/disconnection, CLONE_FILES unshare and exec CLOEXEC. Actual descriptor sanitizer covers allocation rollback and final reference release. General job control, legacy desktop rerun and broader stress remain |
| P05 job control/session | POSIX termios; Linux v6.12 drivers/tty/tty_jobctrl.c, tty_io.c, kernel/sys.c, kernel/exit.c | pty.c, sched.c, syscall.c, signal.c | tty-session-linux-6.12-03.log; auth-tty-session-guest-06/serial.log; process-group-linux-6.12-02.log; auth-process-group-guest-03/serial.log; tty-hangup-linux-6.12-02.log; auth-tty-hangup-guest-02/serial.log | Verified controlling-TTY acquisition/detach, O_NOCTTY versus dup2, /dev/tty binding, foreground session/error checks, ten background stop/ignore/block/EINTR cases, orphan EIO, cross-UID terminal signals and complete winsize fields. Six process-group cases cover thread-group identity, errors and child-after-exec refusal. Three hangup cases cover master close, leader exit and stopped orphan HUP/CONT. Broader SMP/lifetime stress, stable PGID references, complete termios and normal sudo/su interactive workflows remain; userspace group-kill permissions require repair |
| P05 devpts metadata and DAC | Linux fs/devpts/inode.c; sudo src/get_pty.c | pty.c, permissions.c, syscall.c, storage_vfs.c | pty-metadata-linux-6.12.log; auth-pty-metadata-guest-02/serial.log; production PTY/permissions sanitizer | Verified metadata, path/FD chown/chmod, owner/group rejection, inode consistency, hangup lifetime, read-only FD rejection and zero-read behavior; full devpts/O_PATH/OFD semantics pending |
| P05 password terminal cleanup | sudo conversation; Linux termios | sudo_client.c | tools/test_sudo_policy.py | Not started: preserve current echo/EINTR tests through migration |
| P06 module runtime | Linux-PAM 1.7.2 libpam, pinned musl ldso | staged official DSOs; existing ELF loader | host-runtime.log; auth-pam-guest-04/serial.log | Verified real module loading, control flow, handle environment isolation, phase failures and 144 repeated transactions with FD counts unchanged; TLS/destructor/ABI failure stress incomplete |
| P06 randomness/clocks | Linux getrandom; Linux-PAM passverify.c; libxcrypt crypt_gensalt_rn | random.c; syscall.c; userland.c; elf.c; recorded PAM patch | random-source-host.log; random-linux-6.12.log; auth-random-guest-02/serial.log; pam-entropy-failure-linux.log | Verified production hardware retry/failure/partial wipe, nonzero device bytes, getrandom flags and writable-buffer validation, password salt failure rejects update with shadow unchanged. Full Linux RNG initialization/wait/ioctl semantics and non-RDRAND sources missing; timestamp clocks pending |
| P06 resource limits | Linux kernel/fork.c, kernel/sys.c, fs/exec.c, kernel/exit.c and enforcing subsystems | process_resource_limit; sched.c; userland.c | resources-host.log; nproc-linux-6.12.log; auth-nproc-guest-02/serial.log | Verified NPROC hard-limit privilege, actual fork/thread exhaustion, zombie/reap accounting, inheritance, UID-0 exemption, deferred exec failure and retry. NOFILE/AS/SIGPENDING/STACK existed; remaining resources unimplemented |
| Sudoers language and Defaults | sudo 1.9.17p2 plugins/sudoers and docs | Official sudoers.so and system/rootfs/etc/sudoers | Historical sudo-upstream-isolated.log passes using target musl on Linux; integration build logs | Production selected and packaged; current system policy/command runtime acceptance paused |
| sudo CLI, plugins, sudoedit, visudo | sudo 1.9.17p2 | Official /usr/bin/sudo and /usr/sbin/visudo; compatibility app execs same program | Historical sudo-runtime-linux-6.12.log and auth-sudo-nonroot-guest-01/serial.log | Isolated caller-password, exact argv, NOPASSWD/account checks previously passed. Production command migration implemented; full CLI/sudoedit and current consumer runtime acceptance pending |
| PAM core/API/control stack | Linux-PAM 1.7.2 libpam and public headers | build/auth-upstream/root/lib and usr/include/security | linux-pam-upstream-isolated.log: 36 Meson tests pass, 40 skipped target tests replayed and pass; guest runtime probe | Verified tested subset; root xtests, every control form and consumer cleanup not complete |
| PAM SDK | Linux-PAM 1.7.2 installed public headers, libpam/libpam_misc/libpamc | build.py sdk; tools/package_devtools.py | sdk-build.log; sdk-pam-link.log; auth-pam-sdk-guest-01/serial.log | Verified SDK archive contains headers, DSOs, relocatable pkg-config and source/license recipe; extracted SDK compiles actual runtime probe and guest PAM transactions/pam_unix password update pass. Static PAM archives are not built |
| PAM required modules | pam_unix, deny, permit, rootok, env, limits, nologin, shells, access, wheel, succeed_if, faildelay, faillock, exec, umask | build/auth-upstream/root/lib/security | All built and Linux dlopen tests pass; auth-pam-unix-guest-04/serial.log uses patched PAM and official libxcrypt; auth-pam-helper-guest-02/serial.log verifies nonroot helper | Verified correct/wrong/empty/cancel, account, chauthtok, updated password and actual helper set-ID. Nonroot may verify only own password, protected shadow remains unreadable. Full module behavior and pam_limits resources remain incomplete |
| Official su entry | util-linux 2.41.6 login-utils/su-common.c | Official /bin/su and /usr/bin/su link | Historical sudo-runtime-linux-6.12.log; auth-sudo-su-guest-01/serial.log and pam-files.json | Isolated target password, identities/groups, environments, su-l and phase denial passed. Normal entry now replaced; full CLI and interactive job control acceptance pending |
| PAM services | Official service include/control syntax | system/rootfs/etc/pam.d; pam_session.c | Source review and integration builds | Implemented, runtime unverified: login/leonos-gui/sudo/su/passwd and account tools use real stacks; other denies all phases |
| Accounts and password migration | pam_unix, libxcrypt, shadow 4.20.2 | standard_accounts.c, account_store.c, auth_accounts.c, installer, PAM password module | Historical password-policy-host-07.log, password-policy Linux run02 and LeonOS guest03 PASS | Production standard files now authoritative; old kernel CRUD and password API removed. Normal mutation uses upstream tools/PAM. Current GUI/installer/runtime and power-cut acceptance pending |
| Installer product rules | Task section 6.6 | installer_setup.c, standard_accounts.c, auth_password.c | Historical auth-installer-guest-01 uses AUS2; integration builds inspect new code only | Standard four-file creation with yescrypt implemented; root=0, ordinary=1000, empty wheel. Old nonempty database refused before update. New installation/reboot acceptance paused |
| GUI action policy and broker cleanup | sudoers + PAM; task section 7 | fileman/elevation.c, admin.c, sudo_client.c, sudod, pam_session.c | Source review and archive/image inspection | Implemented, runtime unverified: every FILEOP invokes upstream sudo; anonymous result pipe; ordinary process never gains in-process root. No production authd socket/process/client |
| Startup session identity | PAM session, Linux SCM_CREDENTIALS and UID | serviced/sessiond.c, pam_session.c, taskmgr | Source review and compilation | Implemented, runtime unverified: SS12 per-UID entries, owner/root management, per-message identity, live session and final UID check. Populated ownerless SS11 is preserved and refused |
| Audit and timestamp | Fixed sudo logs/timestamp implementation | Upstream sudo; /var/log/sudo.log; /run/sudo/ts | Historical limited official-sudo revocation probe; full installed matrix paused | Official implementation active; old UID/session cache removed. TTY/ppid/global, lifecycle, time changes and complete log/secret audit pending |
| External service integrations | Upstream LDAP/SSSD/Kerberos/SELinux/remote logging | Fixed build metadata; SUDOERS_PAM_UPSTREAM.md option/module inventory | External services not provisioned | Build state inventoried; absent target dependencies and optional setting diagnostics/behavior remain explicitly unverified |
| Final builds and guest workflows | Task section 8C | build.py | kernel-tty-session-04.log; userland-tty-build.log; installer-tty-build.log; sdk-pty-build.log | Kernel/userland/installer rebuilt after controlling TTY, group-signal and wait repairs; auth-installer-guest-03 new install/root/user/desktop PASS. SDK last built after PTY OFD repairs; its PAM guest05 also passes pam_unix. No complete sudoers/PAM image certified |
| VMware and SMP | Task section 8C | smp.c, scheduler and shared kernel state | LeonOS serial reports CPUs=1, discovered=2, AP scheduler disabled | Not verified: all existing LeonOS probes actually ran one CPU. SMP task/mm ownership and teardown require repair before enabling AP scheduling; VMware not run |

## Verification and self-review log

Evidence paths below are relative to `build/` and were produced during this
task. Successful host and kernel builds do not establish system compatibility.

| Check | Result / evidence |
| --- | --- |
| Official source pin and extraction | configs/auth-upstream.json; seven fixed package archives/trees including shadow/libbsd/libmd; five tamper/reuse tests pass |
| musl upstream builds | auth-upstream/{linux-pam,sudo,util-linux}.log and corresponding -build.json; all built and installed in isolated staging |
| sudo upstream regression | auth-upstream/sudo-upstream-isolated.log; exit 0 |
| Linux-PAM upstream regression | auth-upstream/linux-pam-upstream-isolated.log; 36 pass plus 40 direct target replays pass; root xtests not run |
| PAM fixture on Linux | auth-upstream/host-runtime.log; pinned official pam_unix AND unix_chkpwd, correct/wrong/empty/cancel, account and password update; failures=0. Earlier helper-contaminated host result is superseded |
| PAM runtime on LeonOS | auth-pam-guest-04/serial.log; failures=0, FD count 3 before/after; this guest does not run pam_unix fixture yet |
| PAM Unix on LeonOS | auth-pam-unix-guest-03/serial.log; smp=2, official unix_chkpwd, all password cases/account/update pass and FD counts unchanged; root consumer only |
| P01 raw Linux reference | auth-upstream/credentials-linux-6.12.log; kernel 6.12.0, failures=0 |
| P01 raw LeonOS reference | auth-credentials-guest-02/serial.log; smp=2, four cases pass including raw per-thread and musl coordinated identity changes |
| Existing sudo regressions | auth-upstream/sudo-host.log; pass, legacy broker only |
| Existing sudo guest regressions | auth-sudo-guest-01/serial.log and auth-upstream/sudo-guest.log; PASS legacy correct/wrong password, cache boundaries, redirection/pipe/cwd, su login |
| New installation/desktop | auth-installer-guest-01; auth-upstream/installer-guest.log PASS; desktop/terminal-identity.png visually inspected: Uid/Gid 1000 and HOME=/home/alice |
| Existing socket and PTY regressions | auth-upstream/socket-host.log and pty-host.log; pass |
| process_vm production sanitizer | auth-upstream/process-vm-host.log; production tests pass, subsequent host raw reference fails at oversized local iovec |
| process_vm v6.12 reference | auth-upstream/process-vm-linux-6.12.log; pass after enabling CROSS_MEMORY_ATTACH in reference kernel configuration |
| Host kernel difference | Host is 7.2.2-1-cachyos-bore-lto; its oversized local iovec behavior differs from v6.12. Existing assertion retained, host run remains FAILED |
| LTP/POSIX selected subset | auth-ltp-guest-02/serial.log; 7 syscall plus 14 POSIX thread/semaphore programs exit 0. Runner rebuilt from current source; staged probe hashes recorded. Not full LTP; these 21 binaries were existing fixed-source build outputs, not newly rebuilt in this run |
| Patched PAM upstream suite | auth-upstream/linux-pam-upstream-isolated.log; 36 Meson + 40 direct target replays pass after salt failure patch and libxcrypt integration |
| Crypt-entry/history patch rerun | auth-upstream/pam-libcrypt-upstream-rerun.log: 36 Meson + 40 replays PASS; pam-entropy-crypt-rerun.log PASS; root xtests still unrun |
| Product password policy, Linux | auth-upstream/password-policy-host-07.log PASS root-only namespace; password-policy-linux-6.12.log and password-policy-linux-run-02.log PASS on 6.12.0 with two online CPUs, including official root/nonroot passwd and unchanged shadow on rejection. First Linux run failed missing /etc; retained as password-policy-linux-6.12-01.log |
| Product password policy, LeonOS | auth-password-policy-guest-01 failed startup fixture race and passwd abort; guest02 fixed fixture but reached 90-second watchdog, so remains FAILED. guest03 uses bounded 300-second probe and console-FD repair: all password/history/root and nonroot passwd checks PASS, including wrong-old-password unchanged shadow |
| Console descriptor regression | auth-upstream/console-fd-host-01.log negative baseline; host02 exposed test-fixture reference leaks, host03 PASS after complete fixture teardown; console-fd-linux-6.12.log and auth-console-fd-guest-01/serial.log PASS. PTY and 38 ioctl/CLOEXEC checks pass |
| Console unlock integrated rerun | auth-installer-guest-05 new installation/root/user/desktop PASS; terminal-identity.png visually checked UID/GID 1000 HOME=/home/alice. auth-sudo-guest-05 legacy sudo/su input, cache boundaries, pipe/redirection/cwd PASS |
| Current SDK PAM rerun | auth-upstream/sdk-pam-crypt-patch-link.log PASS extracted SDK; auth-pam-sdk-guest-06/serial.log PASS real PAM transactions and pam_unix password update with patches 0001/0002 |
| Current interim build | auth-upstream/installer-console-unlock-build.log PASS (includes userland and SDK dependencies); kernel-console-fd-build.log PASS newer kernel. ISO /home/xiaobai/Projects/Projects/LeonOS-4/build/images/leonos4-installer.iso SHA256 62fea7e8cc487ad8213e465f6aacc4a1a72a43cbd04e78f77fe88ef537129516 contains console unlock, but predates console-FD materialization and account transaction helper. Still uses AUS2 |
| Account transaction helper | auth-upstream/account-store-host-02.log ASan/UBSan PASS, account-store-linux-6.12-02.log (two CPUs) and auth-account-store-guest-01/serial.log (one CPU) PASS. Twenty-four chosen crash checkpoints, four concurrent writer processes with eight commits each, ENOSPC/EIO/short-write/EINTR and symlink/writable-directory refusal. Process crashes are not device power cuts |
| PAM entropy failure | auth-upstream/pam-entropy-failure-linux.log; production PAM/libxcrypt in isolated namespace, getrandom forced EIO and /dev/urandom forced EOF, PAM_BUF_ERR returned, shadow byte-for-byte unchanged and old password still authenticates |
| Ordinary exec capabilities | auth-upstream/exec-credentials-linux-6.12-02.log and auth-exec-credentials-guest-02/serial.log; all 6 cases pass on fixed Linux and smp=2 LeonOS |
| Patched-musl PAM and sudo rerun | auth-pam-unix-guest-05/serial.log and auth-sudo-nonroot-guest-02/serial.log; actual current loader copied and hashed; all focused cases pass |
| FD execution | auth-upstream/fd-exec-linux-6.12-02.log; auth-fd-exec-guest-03/serial.log; raw execveat and actual musl fexecve, saved IDs, permission refusal and empty argv pass. Pre-implementation guest01 failed as expected and is retained |
| Script execution and rollback | auth-script-exec-guest-03/serial.log; auth-exec-rollback-guest-05/serial.log; all focused cases pass after shebang support. First script guest failed before implementation and is retained |
| Upstream sudo after script support | auth-sudo-nonroot-guest-03/serial.log; root and ordinary caller authentication/policy/account tests pass |
| IPC credentials | auth-upstream/socket-credentials-host.log and ipc-credentials-host.log; fixed Linux references and actual LeonOS guests in P04 row pass. Initial root-without-capability host and guest tests failed before repair |
| Actual authd consumer | auth-authd-credentials-guest-01/serial.log; new daemon and runtime from installer staging, valid status requests and inherited connection refusal pass; authentication policy still legacy |
| Nonroot pam_unix helper | auth-upstream/pam-helper-linux-6.12-02.log and auth-pam-helper-guest-02/serial.log; production unix_chkpwd mode 4755, own correct password passes, wrong password and other-user verification denied, shadow read denied. Previous 0755 helper fails both Linux and LeonOS probes; negative logs retained |
| Exec pathname and reboot capability repairs | auth-fd-exec-guest-04/serial.log, auth-script-exec-guest-04/serial.log, auth-credentials-guest-06/serial.log; all focused cases pass after single-copy exec pathname and CAP_SYS_BOOT repairs; corresponding fixed Linux probes pass |
| PTY OFD and cleanup | auth-upstream/pty-ofd-linux-6.12-04.log confirms two online CPUs; auth-pty-ofd-guest-03/serial.log passes 24 cycles and lifecycle/refusal cases. Pre-repair guest01 fails shared flags and blocked FD replacement. Descriptor, ioctl, socket and PTY core sanitizer suites pass |
| Latest installer retry | auth-upstream/installer-pty-build.log failed because host /tmp was full during grub-mkstandalone. installer-pty-build-02.log passes with TMPDIR=build/auth-upstream/tmp; no source or user temporary data was removed. Interim ISO build/images/leonos4-installer.iso SHA256 61c50f7bf8fa626b7eb62caacb3623c02ea71394ab35853af120ff762e0d2e53 |
| Latest installation and legacy regressions | auth-installer-guest-02 and auth-upstream/installer-guest-02.log PASS. Desktop terminal-identity.png visually confirms all four UID/GID fields 1000 and HOME=/home/alice. auth-sudo-guest-02/serial.log and sudo-repair-pty-02.log PASS legacy passwords/cache/pipe/redirection/cwd/su. These use AUS2, not standard account integration |
| Latest official sudo/su and SDK | auth-sudo-su-guest-02/serial.log PASS after PTY OFD changes. sdk-pty-build.log PASS; extracted SDK link passes; auth-pam-sdk-guest-04/serial.log verifies actual module transactions, FD counts 3 before/after, without pam_unix fixture |
| Initial TTY session repair | tty-session-linux-6.12.log PASS with two online CPUs. auth-tty-session-guest-01 fails non-controlling tcgetpgrp before repair; guest02 PASS after repair. Production PTY/descriptor/ioctl sanitizer regressions pass. Broader job-control tests remain |
| Expanded TTY and group verification | Fixed two-CPU references tty-session-linux-6.12-03.log, process-group-linux-6.12-02.log and tty-hangup-linux-6.12-02.log PASS. LeonOS tty-session guest06, process-group guest03 and tty-hangup guest02 PASS. tty-hangup-host-01.log, tty-hangup-descriptors-host.log, tty-hangup-ioctl-host.log, resources-host-tty-session.log and sudo-host-tty-session.log PASS production host regressions. Latest kernel build kernel-tty-hangup-01.log PASS, installer still predates these TTY changes |
| Latest extracted SDK pam_unix | auth-pam-sdk-guest-05/serial.log PASS with --unix: actual SDK-linked runtime, pinned module/helper, password/account/update phases. guest04 tested module core without the scratch pam_unix fixture |
| Latest TTY/wait installation | userland-tty-build.log and installer-tty-build.log PASS; auth-installer-guest-03 and installer-guest-03.log PASS. Desktop terminal-identity.png visually inspected: four UID/GID fields 1000, HOME=/home/alice. Current interim ISO SHA256 6bcb2fc3942db4a1b1cb74cd68533738391e1ec79d31d28261944210ff320e49; still AUS2 |
| Latest official sudo/su | auth-sudo-su-guest-04/serial.log PASS on the TTY/wait kernel. guest03 was a failed runner invocation, not a full pass |
| Standard account tools dependencies | Official shadow 4.20.2, libbsd 0.12.2, libmd 1.2.0 archives pinned and hashes verified. shadow-build-01 fails missing readpassphrase; shadow-build-02 builds libmd then fails libbsd header-overlay order. Builder order repair and build03 in progress; normal passwd integration still pending |

Self-review findings repaired so far:

1. Root capset bypass allowed restoring dropped permitted capabilities. Enforce
   Linux subset constraints even for UID 0; add positive/negative tests.
2. Credential handler coverage hid missing capget/capset top-level routing.
   The first guest failed with ENOSYS; the repaired second guest passes.
3. Raw setuid changed account metadata, other tasks and cwd. It now changes
   only the calling thread's credentials. Userland login/launch performs group
   initialization and cwd setup; new-install TTY/desktop and old sudo/su guest
   workflow regressions passed after the change.
4. DAC treated zero fs IDs as unset and treated UID 0 as capability authority.
   Use explicit fs IDs and operation-specific capability bits; sanitizer passes.
5. AUS2 enlarged ACL stack usage above its existing 32 KiB test ceiling. Split
   explicit ACL lookup and account fallback lifetimes; measured peak is 30032.
6. Meson cached a host libaudit dependency during an ad hoc test rebuild. Set
   machine-file pkg-config scope and clear reconfiguration cache; rebuilt target
   PAM and reran host/module suites successfully.
7. Source fetch verified archives but reused altered extracted files. Now
   compare extracted file contents/types/links and reject added source files;
   Linux header source permits generated files but verifies archive members.
8. Initial PAM guest FD instrumentation required nonexistent /proc/self/fd.
   Replace the measurement with exhaustive F_GETFD checks below RLIMIT_NOFILE,
   including unexpected-error rejection. The leak assertion remains enforced;
   procfs FD support remains an explicit P02/P03 gap.
9. Host pam_unix initially launched the host unix_chkpwd helper, and the first
   pam_unix guest images omitted that helper. Pin the target helper and target
   musl interpreter in the namespace and copy helpers into diagnostic images;
   rerun host and guest succeeds. Earlier positive host evidence is invalid.
10. capget/capset and getresuid/getresgid used naturally aligned user pointers.
    Use byte copies for valid unaligned buffers; UBSan failure becomes success.
11. AUS2 permission fallback rejected a valid UID-0 root /root record before
    reaching /home/alice. Validate and skip the root record; mixed-account and
    malformed-root regressions pass with ACL peak stack usage below 32 KiB.
12. Official sudo openpty succeeded but chown of its slave returned ENOENT.
    Add PTY node metadata tied to generation and endpoint lifetime, route
    chown/chmod/stat through real DAC and reject unauthorized slave opens.
    Linux v6.12 and LeonOS raw metadata probes pass; official sudo then runs
    its root-entry command through the default PTY and denies revoked policy.
13. Explicit PTY FDs ignored access mode. Add read/write mode checks and zero
    length read handling, plus a regression for a read-only group-authorized
    slave. Fixed Linux v6.12 and the second LeonOS PTY probe both pass.
14. sudo 1.9.17p2 `plugins/sudoers/auth/pam.c` explicitly treats setcred errors
    and PAM_SESSION_ERR as nonfatal, but other session failures as fatal.
    Task section 5.5 allows documented upstream behavior. Preserve it and
    test both branches; do not claim that every session error stops sudo.
15. The syscall epilogue retried fork/vfork EAGAIN forever, hiding real NPROC
    rejection. Return those errors and the deferred NPROC exec error to userspace;
    the first NPROC guest timed out, the second passes all three cases.
16. exec committed the replacement mm before validating ELF, killing the old
    process and its threads on invalid input. Prepare mappings and stack in an
    independent task/mm first, propagate loader errors, then commit. The first
    guest reproduces exit 127, the second preserves the original process and
    returns ENOEXEC. The initial Linux fixture wrongly used absent /run; after
    using /tmp, the same corrected source passes Linux v6.12 and LeonOS.
17. Random devices returned zeros; getrandom could use time/PID fallback and
    write into read-only userspace, crashing the kernel. Route device/syscall/
    ELF randomness through checked hardware output, fail without entropy and
    validate writable buffers. The first guest reproduces zeros and a page fault;
    the second passes. Tests do not establish statistical entropy quality.
18. Upstream PAM salt fallback violated the task's entropy requirement. Add the
    checksummed minimal patch and official libxcrypt, fail all salt call sites
    when entropy is absent, and verify no shadow mutation. The upstream returns
    PAM_BUF_ERR for a NULL password hash, not PAM_AUTHTOK_ERR; retain that result.
19. unix_chkpwd sanitizes its environment and could not find target libcrypt in
    the Linux namespace. Bind target libcrypt there as well as the target helper
    and loader. Host authentication and forced-entropy-failure reruns pass.
20. The first LTP guest used an older runner containing /system/tests and all
    21 execs failed ENOENT. Current source already uses /usr/lib/leonos/tests;
    rebuilding the runner produces 21 passes without changing assertions.
21. Ordinary exec retained saved root/fs IDs and did not implement capability
    exec transitions. Add capability bounding/ambient/securebits and prepare
    credentials after image validation, before auxv, committing only on success.
    Six fixed Linux/guest cases pass. Linux reference also corrected the initial
    dumpability expectation: changing fs IDs during exec keeps nondumpable set.
22. Extracted SDK -x c stdin compilation applied the language to appended
    crtn.o. Reset the language before internal link inputs; SDK build/link rerun
    passes, and auth-pam-sdk-guest-03 verifies real PAM/libxcrypt transactions.
23. Unlink/rename reclaimed inodes still held by FDs or lazy executable/mmap
    mappings. Hold ext2 references across descriptors, fork and VMAs, use inode
    writes/truncation, defer reclamation until last close and prevent unmount.
    The first guest failed; fixed Linux and the second/third guests pass all four
    cases. Final-close cleanup errors propagate and keep the inode reserved;
    crash recovery and fault-injection coverage are still required.
24. Page cache invalidation matched stale file size, allowing old contents after
    inode reuse. Match volume/inode identity independently of size. The new
    regression fails before the fix and passes after it. Shared-write coherence
    requires further implementation.
25. Initial executable-lifetime test incorrectly expected ETXTBSY from Linux
    v6.12. Its fixed fs/exec.c and kernel/fork.c no longer deny executable writes;
    the unmodified reference kernel permits the open. Corrected that expectation
    and documented the version distinction; this is not a weakened LeonOS-only
    assertion.
26. bwrap sets no_new_privs, so the expanded nonroot sudo host test failed before
    authentication. Preserve the failure, use fixed Linux v6.12 with the actual
    binaries/root ownership for the nonroot reference, and retain an explicit
    --host-root-only mode. Fixed Linux and LeonOS nonroot tests pass.
27. Fixed musl lckpwdf/ulckpwdf were upstream no-op stubs. Add real kernel POSIX
    record locks and a checksummed libc platform patch. Isolated process tests
    prove mutual exclusion and symlink refusal on Linux and LeonOS. Prior PAM
    update success did not establish concurrent writer safety.
28. fsync/fdatasync/syncfs only returned success after sector writes, without a
    device cache flush. Implement ATA and NVMe flush commands, error propagation
    and unmount synchronization. The first raw guest exposed an incorrect use
    of Linux O_PATH bits instead of TASK_FILE_FLAG_PATH; repaired, guest02 passes
    and records eight actual cache-flush commands. Power-cut testing remains.
29. Guest staging inherited an older musl loader from build/esp. Diagnostic
    staging now explicitly copies the current libc and allocator and records
    its actual loader digest, so PAM runs the password-lock patch under test.
30. execveat was missing, so musl fexecve could not execute a held, unlinked ELF.
    Route both exec syscalls through actual-node permission/metadata checks and
    the existing transactional loader, preserving Linux AT_EXECFN and CLOEXEC.
    Fixed Linux and LeonOS pass the extended probe. Script handling focused
    tests now also pass; neither these probes nor held inode references establish full digest
    and interpreter race safety.
31. SCM_CREDENTIALS already existed, but explicit fields bypassed all checks
    for euid zero. Replace that bypass with separate CAP_SYS_ADMIN/SETUID/SETGID
    checks from Linux v6.12 net/core/scm.c. Production sanitizer and raw Linux/
    LeonOS tests prove rejection after capabilities are dropped.
32. authd trusted only connection credentials. Its receiver now requires matching
    kernel credentials on every fragment, including partial-frame continuation.
    Missing/mismatched identities reject the frame and release all received FDs;
    send and receive temporary data are wiped. Production authd guest passes.
    Stable PID lifetime binding is still required and is not solved by this change.
33. execve copied the user pathname twice. The second read could select a different
    file from the name supplied to script argv and AT_EXECFN. Resolve the single
    kernel-owned copy for both exec syscalls; FD/script guest04 reruns pass.
34. reboot still used real UID zero instead of effective CAP_SYS_BOOT. The
    capability repair passes production tests; raw Linux reference passes,
    pre-repair LeonOS guest fails, and repaired guest06 passes. Historical
    security lint demanded the incorrect UID gate; it now runs production
    credential assertions and explicitly distinguishes lint from ABI evidence.
35. Linux-PAM installed unix_chkpwd as 0755, which cannot read root:root 0600
    shadow for an ordinary caller. Follow the pinned helper manual's setuid-root
    installation contract; record 4755 and image ownership in build metadata.
    Real helper tests fail before repair and pass on fixed Linux and LeonOS,
    including rejection of attempts to verify another user's password.
36. The diagnostic --sudo overlay omitted /bin/su and followed existing command
    aliases when copying files. Stage the official /bin tree as well, replace
    leaf aliases without following them and record the actual su digest.
    Subsequent fixed Linux and LeonOS official sudo/su runtime probes pass.
37. PTY entries copied status flags and did not pin blocked I/O. Shared descriptions
    now own endpoint references; dup/fork/SCM/unshare retain them and close/exec/exit
    release them. A blocked read retains its original object after dup2 replacement.
    Creator exit no longer destroys a transferred master, and hangup does not
    erase unrelated descriptors. Sanitizer and Linux/LeonOS lifecycle tests pass;
    auth-installer-guest-02 desktop/TTY and auth-sudo-guest-02 legacy workflows pass.
38. Descriptor/ioctl host runners had not linked the new production record-lock
    close path. Link syscall_locks.c; explicitly restrict synthetic inode stubs
    to non-ext2 nodes. Existing assertions remain, including all 38 host ioctl
    checks. Initial link/type/stub-scope failures are retained in logs.
39. The fixed Linux reference used -smp 2 but its q35 guest only brought up one
    CPU without ACPI. Enable ACPI and assert online_cpus=2 in the actual init
    probe and runner. PTY reference04 passes on two CPUs. Earlier Linux reference
    logs are one-CPU evidence. Earlier LeonOS probes requested two CPUs but
    actually ran one: see the separate correction in finding 51.
40. tcgetpgrp accepted unrelated slave FDs; tcsetpgrp relied on legacy stdio
    attachment and collapsed session/PID errors into EINVAL. Enforce actual
    controlling-TTY ownership and Linux error ordering, implement tcgetsid and
    TIOCNOTTY, bind /dev/tty at open, and acquire an eligible terminal only on
    open without O_NOCTTY or explicit TIOCSCTTY. Privileged stealing uses
    CAP_SYS_ADMIN. Add background read/write/state-change checks and kernel-origin
    terminal signals. The first reference/guest pair demonstrates the mismatch;
    guest02 verifies acquisition/detach and SIGTTIN stop/resume. Broader lifecycle
    and refusal coverage is still required and is not certified by this probe.
41. The new TTY background-disposition check initially used a zero-based signal
    index, but LeonOS actions are indexed by signal number. Extended guest03
    exposed a hang on ignored SIGTTIN. Fix the index, add production sanitizer
    assertions, and rerun: guest04 passes all ten modes and orphan EIO cases.
42. setpgid collapsed Linux errno values, incorrectly prevented a process-group
    leader from moving, and allowed a session leader's no-op setpgid. setsid and
    setpgid used the current TID and did not publish changes to all threads.
    Apply kernel/sys.c ordering and thread-group identity, retain FORK_NOEXEC until
    successful exec, and reject parent changes after exec with EACCES. All six
    cases fail on old LeonOS and pass after repair. Initial Linux probe incorrectly
    assumed the minimal init's inherited PGID was nonzero; explicitly establish
    the parent group before testing. Reference02 passes with assertions intact.
43. TIOCSWINSZ rejected zero sizes, discarded pixel dimensions and did not emit
    SIGWINCH. Preserve all four Linux fields, signal only changed dimensions,
    and retain the platform row/column interface. Guest05 exposes the zero-size
    refusal; guest06 passes cross-UID SIGINT/SIGQUIT/SIGTSTP/SIGWINCH cases.
44. Closing a native master did not detach controlling TTYs or send SIGCONT to
    the session leader. Leader exit did not notify its foreground group, and
    stopped groups orphaned by exit stayed stopped. Detach before reclaim,
    preserve the old foreground group for leader exit, add HUP/CONT delivery,
    and retire session attachment when the last thread exits. Reference02 and
    guest02 pass all three lifecycle cases; descriptor/PTY/ioctl sanitizers pass.
    The first reference revealed hung-up slave ioctl returns EIO, distinct from
    ENOTTY after detach with a live master; the probe now tests that distinction.
    Stable PID/PGID lifetime and broader concurrent exit stress remain open.
45. Userspace group kill used real UID 0 as authority, rejected mixed-owner groups
    before delivering to eligible members, and emitted kernel siginfo. Apply
    real/effective versus real/saved UID checks, CAP_KILL, the same-session
    SIGCONT exception, partial group success and SI_USER credentials. Fixed Linux
    group-signal reference and auth-group-signal-guest-02 pass eight cases; old
    guest01 fails all eight (its zombie case also exposes missing WNOWAIT).
46. waitid rejected WNOWAIT and returned neither the actual child UID nor decoded
    exit/stop/continue status. Preserve observable events with WNOWAIT, filter
    requested events, correct wait4 WCONTINUED bit, and construct native SIGCHLD
    records before reaping clears credentials. wait-linux-6.12.log and
    auth-wait-guest-02 pass lifecycle and repeated-observation cases. guest01
    exposed stale ECHILD output and incorrect copy-fault/reap ordering, both
    repaired. Production descriptor/ioctl sanitizers pass. Rusage accounting,
    pidfd waits, __WNOTHREAD, auto-reap and complete concurrent-wait stress remain.
47. Linux routes termios operations on a PTY master to the slave's job-control
    check. The initial repair exempted masters; extended tty-session guest08
    exposes missing SIGTTOU. Remove that exemption; guest09 and fixed Linux
    tty-session reference04 pass the added case.
48. The official sudo/su guest03 program passes, but its runner fails because the
    invocation incorrectly included --unix, which requires the separate PAM
    probe's completion marker. Preserve that failed runner log and repeat with
    the correct --sudo invocation: guest04 passes. The failed run remains failed.
49. libbsd's own system-header overlay was searched after the explicit musl
    include path, causing undeclared BSD functions and constants. Move the
    underlying libc headers after the overlay for this package and record its
    actual compiler invocation. No upstream implementation is replaced.
50. sudo-repair guest03 failed password input. The initial diagnosis that normal
    TTY login lacked a controlling session was incomplete: pty_bind_console
    already establishes it. Explicit login setsid/TIOCSCTTY/tcsetpgrp passes the
    installer04 regression, but sudo guest04 still fails. Actual console PTYs
    retain pty_create's locked flag, so the now-correct /dev/tty open rejects
    the endpoint. Unlock at pty_bind_console; the new production PTY assertion
    fails before repair and passes after it. installer05/sudo05 reruns pass.
51. QEMU -smp 2 was incorrectly treated as proof of LeonOS SMP execution.
    Actual serial logs show AP scheduling disabled and one online CPU. Correct
    the matrix and require observed online CPU evidence in future guest runs;
    no historical LeonOS probe establishes multicore safety.
52. The new TTY alias test initially lacked sys/stat.h and did not compile.
    The immediately launched Linux reference05 and LeonOS guest10 therefore
    used the previous binary and do NOT validate the added case. Add the header,
    use a distinct tty-session-alias-runtime.elf output, and rerun: reference06
    and guest11 pass direct-device denial with successful controlling alias use.
53. Installed libbsd/libmd libtool archives embedded absolute target /lib
    dependencies, making the cross shadow link search host /lib/libmd.la.
    Omit these build-only .la files from staging, keeping ELF/static libraries
    and pkg-config files. shadow-build-05 then builds the official PAM passwd
    and account tools. cmocka unit tests were not built; the dependency is absent.
54. Late-loaded PAM modules resolved crypt_r to musl rather than libxcrypt, so
    a consumer linking only libpam could not update yescrypt passwords. Add the
    sized crypt_rn platform adaptation (PAM patch 0002), keeping upstream crypt
    algorithms intact. Host03 passes positive updates; final host07, fixed Linux
    reference02 and LeonOS guest03 pass history and real passwd workflows. Initial patch
    build failures (hunk count and private-header placement) are retained.
55. pam_pwhistory treated crypt failure as a nonmatching old password, allowing
    an update after malformed history hashing failed. Propagate the hash error
    as a history-check failure. Final root history tests reject reuse and invalid
    history and prove shadow unchanged. The reuse assertion uses upstream's
    actual PAM_MAXTRIES result with retry=1; no return-code behavior is replaced.
56. New password-policy fixtures assumed /etc existed in the minimal Linux
    initramfs and raced authd's initial passwd/group export on LeonOS. Create the
    directory and wait for the daemon socket before installing scratch accounts.
    The earlier failed logs remain evidence of fixture failures, not compatibility.
57. Boot console streams accepted writes but F_GETFL returned EBADF. Official
    shadow check_fds then opened /dev/null into a different reserved descriptor
    and aborted. Materialize inherited non-TTY stdio as real /dev/null input and
    kernel-console output descriptions, including shared OFD state across fork,
    dup and CLONE_FILES unshare. ASan found stale fixture references during this
    change; release them through production teardown. Host03, Linux6.12
    reference and console-fd guest01 pass; password-policy guest03 confirms
    official root/nonroot passwd completes the PAM workflow without aborting.
58. Add a root-owned four-file account transaction with the same persistent
    .pwd.lock POSIX lock used by the patched musl writer. A durable prepared-to-
    committed directory rename selects roll-forward; retained staged inodes make
    recovery repeatable. After all four target names are synced, rename the
    committed directory to cleanup and sync before deleting stage files. Tests
    exercise interrupted publication, cleanup and replay, faults and contention.
    It is now called by the installer, and session startup invokes recovery.
    Mixed-file visibility during an update, coordination with upstream writers
    and actual storage power-loss behavior still require verification.
59. Incremental `ar rcs` retained the removed authd_client object in libleonos.a.
    Replace the archive from its complete current member list using
    `tools/rebuild_archive.py`. Both runtime archives and SDK copies now omit
    the old client; nm shows new PAM entry points and no retired login/elevation
    definitions. This check inspects artifacts and does not execute authentication.
60. A missing session during desktop spawn could previously leave a root child.
    Installed desktop launches now require session_apply success before exec.
    Opt-in is limited to the desktop; already privileged application instances
    retain their actual root identity. Startup launch independently checks its
    final UID against the owning entry after applying the live session.
61. PID-based session liveness permits reused identities. Replace it with a
    root-only 0600 marker held with flock by the PAM-owning login process.
    A separate mutex prevents overlapping publication. Initialization acquires
    that mutex before removing stale state and preserves a live session.
62. PAM modules may change groups, umask and implemented limits before failing.
    Capture and restore these on authentication/session failure; terminate the
    login process if restoration fails. Keep the handle in the login process
    through logout, wipe both initial and follow-up conversation secrets, and
    preserve distinct account/password/conversation failures in the GUI.
63. The old startup daemon stored all users' commands globally and parsed a raw
    session UID, creating a cross-user execution risk. SS12 entries now store
    the requester UID, commands must be terminated/bounded, message credentials
    must match SO_PEERCRED, management is owner/root, and only root can request
    launch of the authenticated session. Atomic root-only DB writes replace
    world-writable truncation. Nonempty ownerless SS11 is preserved and refused.
64. Settings account creation now starts expired, installs its password through
    PAM chpasswd, then enables it. Disabling an account also sets account expiry.
    Password-input pipes clear FD_CLOEXEC after dup2 even when stdin was closed;
    secret buffers are wiped. Account lookup propagates shadow I/O failures.
    UID 0 is a valid user in Task Manager; dynamic lists replace the 32-user cap.
65. libxcrypt's sized crypt_rn declaration was hidden by musl include ordering;
    put its headers first for the extension runtime. Repair installer Python
    module import and replacement of read-only staged sudoers. Failed builds
    are retained alongside successful retry logs. Rebuild kernel/userland,
    installer and SDK; behavioral reruns remain paused by the user.
66. SDK template headers still advertised retired kernel operations/password
    APIs, and update cleanup depended on an optional old app manifest. Remove
    those declarations from both templates; exclude authd/auth_db fallback
    headers when packaging. Update explicitly removes the two retired installed
    paths even if their app metadata is missing; it first rejects populated
    legacy account data. No test assertions were weakened to validate removal.

Findings 59-66 have source/build/artifact evidence only. All affected behavioral
reruns, including session/SS12 races and update cleanup, remain explicitly
paused. Legacy test scripts still need path/ABI migration to the retired fixture
tree; no passing result is claimed for those scripts on the new integration.

The packaged SDK exposes the dynamic PAM development interface. Static PAM
archives are not built; TinyCC's static-only runtime does not provide the PAM
dependency closure for the new GUI/session/account helpers. That consumer path
requires further integration and verification. Removing old declarations from
its headers is not proof that every new authentication helper can link there.

## Required security self-review, current answers (2026-09-12)

1. Production sudo and GUI execution now invoke official sudoers policy. Default
   grants cover root only; ordinary account creation leaves wheel empty. No old
   daemon/client is linked or installed. Earlier isolated no-policy/password
   denial passed; the final integration remains runtime unverified.
2. Each GUI command starts sudo again and therefore rechecks policy and PAM
   account state. Disabling an account locks the password and expires the account,
   so an existing timestamp cannot turn a password-only lock into access. The
   upstream nonfatal PAM_SESSION_ERR behavior is documented, not overridden.
   Complete timestamp and installed-image proof remains pending.
3. Kernel raw credentials, secure auxv and malicious loader environment pass.
   The removed authd's inherited/mixed-sender tests are historical only. The
   retained startup service now binds every fragment to connection credentials
   and stores owning UIDs. PID queries still lack complete stable lifetime
   references; full input-boundary acceptance remains pending.
4. Ext2 holds the actual inode across exec/FD/mmap and failed exec preserves old
   credentials. Command digest/FD execution and concurrent policy/account change
   tests are not complete; retained inode identity alone does not solve all races.
5. GUI login retains one real PAM handle until logout, deletes credentials and
   closes sessions, wipes conversations, and restores groups/limits/umask after
   a failed transaction. A root-only marker held with flock distinguishes a live
   session without PID reuse. Signal/crash/concurrency acceptance is still
   required; source review does not certify every module's cleanup behavior.
6. Standard account files are now the only authority. TTY login and passwd are
   upstream; GUI login uses PAM; FILEOP and app elevation use sudo. Old account
   kernel operations and all production broker entry points are removed. Empty
   legacy seeds can be ignored; populated private databases are refused intact.
7. musl rejects malicious LD_PRELOAD/LD_LIBRARY_PATH under verified set-ID exec.
   Complete editor/helper/shell startup-file and sudoedit authorization review is
   outstanding. Official binaries are now packaged as normal production entries.
8. Tests distinguish production sanitizer code, fixed Linux reference, host
   namespace and actual LeonOS guests. Historical false-positive helper selection,
   stale runner/loader and NNP namespace limitations are recorded above. Selected
   passing probes, upstream suite success and compilation do not equal acceptance.

The eight security questions in task section 9 are NOT all resolved. Production
replacement is implemented, but current consumer behavior has not been tested
after the user's pause. Stable PID/PGID lifetime, ptrace/proc isolation, actual
SMP, unsupported rlimits, no-RDRAND entropy, file capabilities, full upstream
CLI/module behavior and power-cut persistence remain required work. Fixed GUI
username/home/path/result bounds also remain. Legacy test runners referencing
removed production authd paths still require fixture/consumer migration; they
were not run and must not be reported as passing. LTP coverage for all new
behavior and VMware acceptance remain incomplete. No full compatibility claim
is made.
