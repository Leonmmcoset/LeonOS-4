# Privilege elevation: sudo, su and the Fileman prompt

## Upstream integration work, 2026-09-11

The normal image still uses the broker described below. Fixed official sudo,
Linux-PAM, util-linux and libxcrypt now build into isolated staging; PAM development
files are also packaged in the SDK. Kernel ID setters now follow capability
checks and no longer change account metadata or cwd. Login/launch consumers
perform group and cwd setup explicitly. Ordinary exec applies saved/fs-ID and
capability transitions. Ext2 set-ID, NNP/nosuid, secure musl loader environment,
and focused nonroot official sudo probes now pass on Linux v6.12 and LeonOS.
Standard shadow account authority and normal consumers remain unfinished.
Current evidence and required gaps are maintained
in [SUDOERS_PAM_STATUS.md](SUDOERS_PAM_STATUS.md). The earlier statement that no
account migration is needed describes only the old broker repair, not the
required upstream integration.

## Security repair, 2026-09-11

The initial implementation described below contained security defects; its old
host tests did not exercise the production password adapter. The repaired
adapter explicitly converts `authd_check_password()`'s boolean success into the
broker's zero-on-success convention. Only authentication of an administrator
can create an administrator cache entry. Root callers need no password.

The cache is now keyed by requester UID and the kernel session ID obtained
using the accepted peer PID. It is not a sudoers per-TTY timestamp or a
per-command authorization policy. `sudo -k` still revokes all entries for the
UID. Account changes and logout revoke all entries.

RUN now requires three preceding RUN_FD messages on a private connection,
carrying stdin/stdout/stderr independently, plus cwd and TERM in RUN. Old RUN
layouts are rejected; rebuild and deploy authd and all libc consumers together.
No persistent account database migration is required. Connections are
CLOEXEC; the child closes all descriptors above stderr except a CLOEXEC error
pipe, which reports setup/exec failures before a successful RUN reply. Slots
are reserved before spawn. WAIT and RUN_SIGNAL require the originating UID
and PID. Signal forwarding currently targets the direct command process;
full process-group job control and descendant supervision remain unverified.

Guest testing exposed a kernel prerequisite missed by the original audit:
SCM_RIGHTS rejected PTY and implicit terminal descriptors because they are not
stored as task_file entries. The socket layer now captures explicit PTY
endpoints (including legacy standard-stream bindings), holds a PTY reference
while queued, and installs a receiver endpoint with its own CLOEXEC flag.
PTY reclaim and master reference accounting include queued transfers. Kernel
PTY and socket tests cover queued lifetime, receive and discard paths. These
changes do not claim complete Linux shared-OFD semantics for all PTY status
flags, which remain a broader kernel ABI limitation.

Commands without slashes are searched in a fixed system PATH. Normal commands
preserve cwd; `su -` enters HOME and invokes `/bin/sh` as `-sh`. TERM and SHELL
are initialized. Password input uses the controlling terminal, rejects
overflow, and restores echo before propagating interruption signals.

FILEOP argv is NULL terminated. Its root-private result file is opened by
authd, passed read-only to the authorized client using SCM_RIGHTS, then
unlinked after a successful send. The client never opens that private path.
The worker pins parent directories using openat/O_DIRECTORY/O_NOFOLLOW and
uses mkdirat/renameat/unlinkat for mutations; redundant path components and
protected tree spellings are rejected. This relies on the kernel's dirfd and
O_NOFOLLOW implementation; it is not protection against privileged mount or
rename operations performed concurrently by another root process.

`python3 tools/test_sudo_policy.py` includes the real PBKDF2 adapter, cache
isolation, slot exhaustion, exec fd isolation, three streams/cwd/TERM/PATH,
private socket lifecycle, result fd lifetime, path traversal and real PTY
password interruption tests under ASan/UBSan. See the current run evidence in
`build/sudo-host-tests.log`. Guest evidence is recorded separately; host tests
are not a claim of complete Linux sudo/su compatibility.

Remaining intentional differences include target-account password policy,
no sudoers/PAM policy, sudo's administrator-only target restriction, fixed
argument limits, and no full login-shell selection from arbitrary passwd
shells. The historical sections below describe the original design and must
not override the repaired protocol and security rules in this section.

LeonOS 4 authenticates users properly and enforces POSIX file permissions in the
kernel, but until this change there was **no path from an ordinary user to
root**. This document describes the mechanism that was added, what it defends,
and what it deliberately does not.

## Why a privileged broker, and not `setuid`

### Repair validation evidence

- Host: eight sudo suites passed under ASan/UBSan in
  `build/sudo-host-tests.log`; socket batch and kernel PTY suites passed in
  `build/sudo-socket-tests.log` and `build/sudo-pty-tests.log`.
- Build: kernel, userland, installer and SDK targets passed. Installer log:
  `build/sudo-installer-build.log`; SDK: `build/sudo-sdk-build.log`.
- QEMU/KVM: updating an isolated installed ext2 disk through the GUI completed
  successfully (`build/sudo-pty-fixed-update/update/serial.log`). The final
  TTY run (`build/sudo-pty-verified/serial.log`) rejected three wrong passwords,
  accepted the real root password with Uid 0/0/0/0, preserved redirected output,
  pipe input and cwd, entered /root for `su -`, and rejected passwordless root
  after authenticating only the ordinary account. The assertions are in
  `tools/test_sudo_repair_qemu.py`.
- Installer ISO used: `build/images/leonos4-installer.iso`, SHA256
  `17071e8b7e171e3d2cba0448e5d15222039f84f7c9b81241b6e1d7b751ec577c`.
- QEMU desktop: ordinary-account login and Terminal sudo/su completed with
  command exit status 0 (`build/sudo-desktop-verified/serial.log`). Visual
  inspection of `build/sudo-desktop-verified/desktop-sudo-su.png` confirmed
  Uid 0/0/0/0, GUI_SUDO_OK, preserved /home/alice cwd, gui-pipe output and
  /root from `su -`. The driver uses `--desktop`; its screenshot must be
  inspected, unlike the TTY run's automated output assertions.
- VMware, SMP stress and Fileman's complete graphical interaction have not
  been verified by this run. Fileop result and path contracts have host tests.

The following original design rationale is retained for context.

Three facts force the design:

- The kernel decides file access from the calling task's own credentials
  (`kernel/ntclks/permissions.c:186-201`), and uid 0 bypasses the check.
- Only a task whose **real uid is 0** may assume another identity
  (`kernel/ntclks/syscall_process.c:406`), so `setuid(0)` can never succeed for
  an ordinary user.
- Executing a file's setuid bit is not implemented
  (`docs/POSIX_PERMISSIONS_2026-09-08.md`), and this change does not add it.

Therefore elevation cannot be something a process does to itself. It has to be
something a process that is *already* uid 0 does on another's behalf. That is
`authd`, the only long-lived uid 0 process besides `init`.

## Architecture

```text
ordinary user (uid 1000)                       authd (uid 0, always running)
  sudo / su / fileman ─┐                            │
                       │ RUN / WAIT / FILEOP        │
                       ├───────────────────────────▶│ 1. verify the target
                       │                            │    account's password
                       │                            │ 2. fork, assume the
                       │◀───────────────────────────┤    target identity
                       │  ack {child_pid}           │ 3. execve on the
                       │                            │    caller's terminal
                       │ WAIT                       │
                       └───────────────────────────▶│ 4. reap and report
```

The client never gains a credential and cannot assert one. Every request carries
only *what* to run; *who is asking* comes from `SO_PEERCRED`, recorded when the
connection was accepted.

## Commands

```sh
sudo id                     # run as root after verifying an administrator
sudo -u alice id            # a non-root target is refused by sudo (see below)
sudo -k                     # discard this user's cached authorization
sudo -n id                  # never prompt; fail if a password is required
su                          # root shell
su -c 'id'                  # run one command as root
su alice                    # switch to an ordinary account
```

### Password rules

| caller | target | password required |
| --- | --- | --- |
| any user | administrator (root) | the target account's password |
| uid 0 | ordinary account | none — real Unix `su alice` semantics |
| ordinary user | ordinary account | that target account's password |
| ordinary user, within the 5 minute window | administrator | none |

Two invariants are worth stating explicitly because they are the parts a
regression would turn into a privilege escalation:

1. **A cached window never authorizes assuming a different ordinary account.**
   A user who has just run `sudo` cannot then `su` into another normal user
   without that user's password.
2. **`sudo` always elevates.** `sudo -u <ordinary user>` is refused even when
   that account's own password is supplied. Switching to an ordinary account is
   `su`'s job, where the password proves the target.

The refusal message does not distinguish "wrong password" from "that account may
not be elevated": saying which would let the dialog enumerate administrators.

### Credential window

A verified password opens a 300 second window **for that requester uid**, held
in `authd` and not in any client. It is discarded by `sudo -k`, by
`leonos_auth_logout()`, at `authd` start, and whenever the account is updated or
its password changed. The window records *that* someone authenticated, never
*what for*: every request still re-checks the target account, its role and the
requested path.

## The Fileman prompt

When Fileman cannot open a directory (`open` returns `EACCES`/`EPERM`) or a write
verb is denied, it shows the administrator username and password dialogs, then
asks the broker to perform the operation. `chdir` can never succeed for the
unprivileged process, so a verified directory is adopted as the current path and
enumerated through the broker from then on.

The broker answers every file operation with the affected directory's fresh
contents, so the list view refreshes from one reply instead of racing a second
listing.

Supported verbs: `LIST`, `MKDIR`, `RENAME`, `UNLINK`. Recursive deletion of a
directory additionally requires the confirmation word `DELETE`, checked both by
the daemon and by the worker.

**Not supported:** copying file *contents* out of a protected directory. The
existing copy/paste implementation still runs as the calling user and reports a
permission error. This is a deliberate boundary, not an oversight.

## Protocol

Private to `authd` (`userland/libc/include/leonos/authd.h`); not public ABI.

| message | value | purpose |
| --- | --- | --- |
| `RUN` | 30 | verify, then fork/exec on the caller's terminal |
| `WAIT` | 31 | collect the child's exit status |
| `SUDO_KILL` | 32 | drop this requester's window |
| `SUDO_CHECK` | 33 | is a window open for this requester? |
| `FILEOP` | 34 | one privileged file operation |
| `SUDO_VERIFY` | 35 | open a window without running anything |

`RUN` and `FILEOP` answer on their own message type with the errno in the same
`code` field a success reply uses: one request, one reply, one type.

The `RUN` frame carries the caller's terminal descriptor over `SCM_RIGHTS`, and
the child inherits it as stdin, stdout and stderr. `WAIT` exists because
`sched_wait_reap()` only lets a **parent** reap a child
(`kernel/ntclks/sched/sched.c:2880`) and the child belongs to `authd`; the daemon
therefore reports the status.

The result-file path is chosen by the daemon inside a root-only directory, so a
client cannot make the worker write where it likes.

## Security boundaries

Defended:

- A client cannot claim an identity: all identities come from `SO_PEERCRED`.
- A client cannot forge a passwordless request: the window lives only in `authd`.
- `FILEOP` cannot run an arbitrary program: the worker path is fixed.
- `FILEOP` refuses `/proc`, `/sys`, `/dev`, any `..` component, and relative
  paths — both in the daemon and again in the worker.
- Passwords are never written to disk or to a log, and are erased from the
  client stack immediately after use.

**Not** defended:

- A malicious GUI application. `FILEOP` path arguments come from Fileman and are
  in the same trust chain as the daemon; an attacker who can run GUI code and
  convince the user to type an administrator password is equivalent to a
  legitimate administrator. Real isolation needs a per-program authorization
  model.
- Denial of service: a requester holding a window can issue privileged
  operations without a rate limit.
- There is no audit log of privileged operations.

## Implementation notes

- Identity changes happen only inside `authd`'s child, between `fork` and
  `execve`. **No client may call `setuid()`**: musl's `__setxid` broadcasts to
  every thread and SIGKILLs the process when any thread fails, so a refused
  change would look like a crash.
- `authd_sudo.c` takes its reply channel, spawn step and password check as
  injected hooks. That is what lets `tools/tests/authd_sudo_test.c` drive the
  real authorization code on the host instead of only inside QEMU.
- `userland/apps/authd/sudo_policy.h` holds every gate as a pure function.
  `tools/tests/sudo_policy_test.c` asserts them under ASan/UBSan.

## Verification

```sh
python3 build.py test sudo-policy          # policy unit + authd handlers
python3 tools/test_sudo_policy.py           # same suites directly
python3 build.py test oobe                  # authentication/session regressions
python3 build.py test installer-setup       # account and PBKDF2 regressions
python3 tools/test_security_regressions.py --strict
```

The security audit greps that every privileged message is dispatched to a
`*_from_peer` handler and that the daemon passes the accept-time
`client->uid` into it, so a new message cannot quietly skip the trust boundary.

### Verified by this change

- Both host suites pass under ASan/UBSan, including: refusal without a password,
  refusal with a wrong password, elevation with the correct one, the cached
  window being per-requester and revocable, the window not authorizing a switch
  to another ordinary account, a caller unable to collect another requester's
  child status, and `FILEOP` rejecting blocked paths before spending a password
  check.
- `userland` builds and stages `sudo`, `su`, `sudod`, `authd` and `fileman`
  with no compile or link diagnostics; the `sudod` worker is staged under
  `/usr/lib/leonos/apps/sudod/`.
- `python3 build.py test sudo-policy`, `test oobe`, `test installer-setup` and
  the security audit all pass.

### Not verified here

- **No QEMU run was performed.** Image packaging currently fails on this
  machine before it reaches the guest: `fakeroot` cannot `chown`, not even to
  the caller's own uid, so `tools/make_ext2_root.py` aborts with `EINVAL`. That
  is a host environment limitation and is unrelated to these changes, but it
  means the end-to-end behaviour below is still unproven.
- Therefore these remain untested: that `sudo id` really reports `uid=0` in a
  guest, the PTY hand-off across `SCM_RIGHTS`, terminal echo suppression while
  typing a password, Ctrl+C delivery to the privileged child (the child is not
  in the caller's process group), and the Fileman dialogs end to end.
- `leonos_admin_elevate()` now routes through this channel, which is what makes
  the diskmgr/apiapp prompts work for a logged-in ordinary user; that fix is
  likewise unverified at runtime.

## Appendix: the "update existing system" mount failure (`ret=-22`)

A report of the installer's **update** flow failing with `挂载失败 ret=-22` was
investigated. Conclusion: it is not caused by the elevation change.

Evidence from the serial log:

- The first `installer_mount_targets()` call **succeeded** — the log shows
  `mount targets disk=/dev/disk0 root=/dev/disk0p2 esp=/dev/disk0p1 fresh=0
  root_fs=2` with no `mount root failed` / `mount esp failed` line.
- Immediately afterwards the storage layer reports a burst of
  `device read failed volume=1 kind=1 transport=1 ... ret=-11` (`-EAGAIN`),
  which comes from `ahci_read_lba_retry()` exhausting its retries
  (`drivers/bootstrap/storage/storage_block.c:187-196`).
- Two `挂载失败 ret=-22` lines then appear, but only **one** `mount targets`
  line and no second `block list partitions` line. So the second call returned
  `-EINVAL` from `leonos_block_list_partitions()` **before** its first print —
  that is `block_open_info()` (`userland/libc/src/blockdev.c:258-288`), whose
  only two `-EINVAL` paths are a failed `BLKGETSIZE64`/`BLKSSZGET` or a device
  that reports **zero bytes**. Both are storage-level, not userland.

The elevation change adds no block-device code path: it is userland-only, and
the kernel/ABI/devtools trees were not touched. The same ISO boots, installs and
reaches a working desktop.

What is genuinely at fault:

1. **The update path has never been exercised.** The reporter had never used it
   before this attempt, so there is no baseline showing it working.
2. **The failure was undiagnosable.** `set_status()` wrote only to the GUI
   (`userland/apps/installer/main.c:702`), so the specific check that returned
   `-EINVAL` never reached the log. That is now fixed: `set_status()` mirrors to
   the serial console, so the next attempt names the failing check and its path.

To distinguish "the update logic is broken" from "that VM's disk reads were
failing", boot the installer with the scratch disk attached and repeat the
update **in QEMU**, where storage is reliable:

```sh
python3 tools/test_sudo_e2e_qemu.py --output build/sudo-e2e --skip-install
```

If the update then succeeds, the `-22` was a transport failure in that VM. If it
still fails, the new serial line names the check, which is the information this
investigation could not obtain.
# Historical Record: Retired authd Broker

This document preserves the previous implementation and its evidence. It does
not describe the current production system and must not be used as an active
authorization or compatibility contract. See `../SUDO_AND_ELEVATION.md`.
