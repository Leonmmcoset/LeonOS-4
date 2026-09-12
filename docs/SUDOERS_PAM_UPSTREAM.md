# Fixed upstream inventory and integration differences

This inventory describes the pinned components now selected by the production
build. The replacement has not been runtime-tested since the user paused tests.
Historical isolated verification levels and remaining prerequisites are in
[SUDOERS_PAM_STATUS.md](SUDOERS_PAM_STATUS.md). Source archives, checksums and
official URLs are authoritative in `configs/auth-upstream.json`. Build commands,
compiler flags and patches are recorded in `build/auth-upstream/*-build.json`.

## Sources and adaptation

| Component | Fixed version | License record | Local patch |
| --- | --- | --- | --- |
| sudo | 1.9.17p2 | LICENSE.md, ISC and file-specific exceptions | None |
| Linux-PAM | 1.7.2 | Copyright, BSD/GPL alternatives and file-specific notices | Reject unavailable salt entropy; use sized libcrypt entry; reject password-history hash failure; handle unavailable resource enumeration without ignoring configured limits |
| util-linux su/runuser | 2.41.6 | README.licensing and per-file GPL/LGPL notices | None |
| libxcrypt | 4.5.2 | COPYING.LIB and LICENSING, LGPL-2.1-or-later plus file exceptions | None |
| shadow-utils | 4.20.2 | COPYING, BSD-3-Clause and file-specific SPDX notices | None; PAM passwd/account tools built, runtime coverage in progress |
| libbsd | 0.12.2 | COPYING, BSD/ISC/MIT and file exceptions | None; readpassphrase dependency |
| libmd | 1.2.0 | COPYING, BSD/ISC/Beerware/public-domain and file exceptions | None; libbsd dependency |
| musl | 9fa28ece75d8a2191de7c5bb53bed224c5947417 | COPYRIGHT, MIT | Replace no-op lckpwdf/ulckpwdf with an actual POSIX password-file lock |

The PAM patch is `patches/linux-pam/0001-reject-unavailable-salt-entropy.patch`,
SHA256 `a20cfb866f1924e40ce5a43a37322425dc000e3a2043957e7a941c945eccc68e`.
The official extracted tree stays unchanged. Every build reconstructs the expected
adapted tree and checks the reused build source against it. Salt generation never
substitutes PID/time for unavailable entropy. PAM's existing NULL-hash error is
PAM_BUF_ERR, and the tested failure preserves the shadow file exactly.

`patches/linux-pam/0002-use-sized-libcrypt-entry-point.patch`, SHA256
`4bcb2585a0b79594818b1e8303f3d1d9915c3b1bfcafcca31f2294650f102a7a`,
selects libxcrypt's `crypt_rn` when available. Late-loaded PAM modules otherwise
bind `crypt_r` to musl's already loaded implementation, which lacks yescrypt,
even though libcrypt is a module dependency. The regression consumer deliberately
links only libpam. The same patch makes pam_pwhistory reject a NULL hash result
instead of treating it as a different password. Both patches apply with fuzz
disabled; the original archive remains unchanged.

libxcrypt builds all its supported hashes and disables its obsolete encryption
API. `crypt.h` and libcrypt replace the musl SDK crypt entry points for consumers
that link libcrypt; the musl crypt implementation itself is unchanged. Test passwords
and hashes are synthetic fixtures in disposable namespaces or guest disks.

`patches/linux-pam/0003-limits-handle-unimplemented-resources.patch`, SHA256
`e265712a4942319d6514a2dc6ec5de0f7fd71a92224cfc57615ac4622b345d82`, adapts
pam_limits enumeration to the kernel's ENOSYS resource results. Unconfigured
missing resources no longer abort every authenticated session. Configured
unavailable resources cause session failure; EIO and other unexpected errors
still abort initialization. Available configured limits still use setrlimit.
This intentionally documented platform difference does not provide missing
kernel limits or claim full Linux compatibility. The real-module host regression
is `tools/test_pam_login_case.py`; guest GUI acceptance remains pending.

shadow-utils uses Linux-PAM for passwd (`lib/pam_pass.c`) and builds its local
account tools. Its su is disabled because util-linux supplies the selected su;
logind integration is disabled because this system has no logind service.
The initial shadow configure failed on missing readpassphrase. Official libbsd
and libmd releases now supply that dependency; their SHA256 values match the
maintainer's fixed-version release announcements recorded in the manifest.
Build and runtime completion remain separate acceptance requirements.
The successful build is `build/auth-upstream/shadow-build-05.log`. libbsd's
header overlay precedes musl headers for libbsd and shadow; target-only libtool
archives are omitted to prevent absolute `/lib/*.la` paths selecting host files.
Shadow's cmocka tests were not built because the target dependency is absent.

The local `userland/pam/pam_leonos_password.c` module enforces the existing
1-32 UTF-8 scalar value, no-whitespace product rule through PAM's password stack.
It does not implement authentication or storage. pam_unix owns the update and
libxcrypt produces yescrypt hashes. The rule also applies to root; no complexity
requirement is added. The fixture stack uses requisite pam_leonos_password and
required pam_unix with use_authtok/yescrypt. Production service activation is
still pending. Linux 6.12 reference password-policy run02 verifies accepted and
rejected inputs, cancellation, history, protected shadow and actual root/nonroot
passwd changes; LeonOS verification is tracked separately.

musl's fixed `src/passwd/lckpwdf.c` returns success without locking. This is a
deliberate upstream stub, insufficient for the required concurrent account
updates. `patches/musl/0001-enforce-password-file-lock.patch` supplies the actual
persistent `/etc/.pwd.lock` POSIX lock, a monotonic 15-second deadline, CLOEXEC,
no-follow and ownership checks, and propagates syscall errors. Its SHA256 is
`e336ed20b2d338dbbf88588dff043e8ce349d0d0044277d31be229d600bb93ad`.
The build exports the pinned git commit to an isolated source tree and verifies
the patched tree before reuse. The submodule is unchanged; the installed runtime
metadata and SDK must identify this platform difference.

## sudo CLI

Inventory source: `sudo-1.9.17p2/src/parse_args.c`, `docs/sudo.man.in`.
"Built" means the official option handler is present. It does not certify the
underlying LeonOS syscall or the normal image entry point. Focused nonroot sudo
entry tests now pass on fixed Linux v6.12 and LeonOS; normal account/consumer
migration and full CLI coverage remain incomplete.

| Options | Staging state / verification gap |
| --- | --- |
| `-A`, `--askpass` | Built; actual helper conversation and cancellation not verified |
| `-a`, `--auth-type` | BSD authentication absent; handler not compiled, usage error |
| `-B`, `--bell` | Built; audible terminal behavior not verified |
| `-b`, `--background` | Built; descendant/terminal lifecycle not verified |
| `-C`, `--close-from` | Built; policy-gated FD preservation not fully verified |
| `-c`, `--login-class` | BSD login capabilities absent; handler not compiled, usage error |
| `-D`, `--chdir` | Built; policy and directory-race coverage pending |
| `-E`, `--preserve-env[=list]` | Built; SETENV and dangerous-environment guest matrix pending |
| `-e`, `--edit`, sudoedit invocation | Built; caller editor, temp/copyback and path-race tests pending |
| `-g`, `--group` | Built; target/supplementary groups guest matrix pending |
| `-H`, `--set-home` | Built; target account environment verification pending |
| `-h`, `--help`; `-h host`, `--host` | Built; help/remote policy-query matrix pending |
| `-i`, `--login` | Built, PAM service sudo-i configured in build; login-shell workflow pending |
| `-K`, `--remove-timestamp` | Built; real timestamp deletion and cross-session tests pending |
| `-k`, `--reset-timestamp` | Built; reset vs command/no-command tests pending |
| `-l`, `--list` (including repeated list) | Built; complete ordinary-user listing matrix pending |
| `-N`, `--no-update` | Built; timestamp suppression not verified |
| `-n`, `--non-interactive` | Root and ordinary-user NOPASSWD commands verified; account denial still prevents execution; broader timestamp/refusal matrix pending |
| `-P`, `--preserve-groups` | Built; policy and group lifecycle tests pending |
| `-p`, `--prompt` | Built; prompt substitutions and conversation tests pending |
| `-R`, `--chroot` | Built, deprecated by this upstream version; kernel/chroot workflow pending |
| `-r`, `--role`; `-t`, `--type` | SELinux absent; handlers not compiled, usage error |
| `-S`, `--stdin` | Caller password through pipe verified, including correct root-password rejection when caller authentication is required; EOF/cancel matrix pending |
| `-s`, `--shell` | Built; restricted-shell and environment matrix pending |
| `-T`, `--command-timeout` | Built; enforcement, signals and descendants pending |
| `-U`, `--other-user` | Built; listing authorization matrix pending |
| `-u`, `--user` | Built; root target guest command verified, nonroot/numeric targets pending |
| `-V`, `--version` | Official version and loaded plugin versions verified on LeonOS |
| `-v`, `--validate` | Built; caller password and timestamp refresh matrix pending |
| `--`, environment assignments, command argv | Official parser built; inherited kernel exec length limits remain |

sudoedit has upstream's own restricted option set, rather than all sudo modes:
`+Aa:BC:c:D:g:h::KkNnp:R:r:ST:t:u:V`. Platform-conditional handlers above still
apply. Its implementation is `src/sudo_edit.c`, not a root editor wrapper.

## sudoers and plugins

The complete fixed parser and policy engine are built without a local replacement.
Canonical grammar is `plugins/sudoers/gram.y`, lexer `toke.l`; matching and Defaults
are in the same directory and documented in `docs/sudoers.man.in`.

| Surface | Build / evidence / remaining work |
| --- | --- |
| User/Host/Runas/Cmnd aliases; ALL, negation, numeric IDs and groups | Official parser; upstream make check passes; complete installed guest cases pending |
| Commands/arguments, escaping, globs, regex, digests | Official matching; upstream suite passes; held execution object and guest race tests pending |
| include/includedir and global/user/host/runas/command Defaults | Official parser and settings; upstream suite passes; concurrent policy replacement tests pending |
| PASSWD/NOPASSWD, SETENV/NOSETENV and environment tables | Official policy; caller-password service integration pending |
| rootpw/targetpw/runaspw and timestamps | Official implementation built; legacy normal-image behavior differs and remains active |
| NOEXEC | sudo_noexec.so built; preload interposition not a guarantee for static binaries; guest effectiveness pending |
| INTERCEPT | sudo_intercept.so built; DSO/trace paths require additional kernel verification; no sandbox claim |
| Logging, I/O logging, timestamps, command timeout | Built; official default PTY root command verified; full logging/time/TTY/error matrix pending |
| sudoers.so policy/I/O/audit, libsudo_util | Built; real guest load and root policy revocation verified |
| group_file.so, system_group.so, audit_json.so | Built; plugin behavior/security tests pending |
| visudo | Built; actual valid and malformed policy check verified; safe editing/locks/atomic replacement pending |
| cvtsudoers, sudoreplay, sudo_logsrvd, sudo_sendlog | Built; complete CLI/runtime verification pending |

## PAM public interfaces

All installed headers come from Linux-PAM 1.7.2, and all three upstream libraries
are linked from the extracted SDK in its verification test. Static PAM libraries
are not currently built. The public API inventory is based on installed headers:

| Header / library | Interface inventory | Verification |
| --- | --- | --- |
| pam_appl.h / libpam | pam_start, pam_start_confdir, pam_end, pam_authenticate, pam_setcred, pam_acct_mgmt, pam_open_session, pam_close_session, pam_chauthtok | Actual guest transaction and phase failure probes; full fault matrix pending |
| _pam_types.h / libpam | pam_set_item, pam_get_item, pam_strerror, pam_putenv, pam_getenv, pam_getenvlist, pam_fail_delay; conversation/messages and return codes | Guest item/env isolation and failures tested; complete multi-message and timing matrix pending |
| pam_modules.h / libpam | pam_set_data, pam_get_data, pam_get_user; pam_sm_authenticate/setcred/acct_mgmt/open_session/close_session/chauthtok entry points | Official DSOs loaded; all entry points exist, exhaustive data cleanup/fork tests pending |
| pam_ext.h / libpam | pam_syslog, pam_vsyslog, pam_prompt, pam_vprompt, pam_error/pam_verror, pam_info/pam_vinfo, pam_get_authtok, pam_get_authtok_noverify, pam_get_authtok_verify | Built; pam_unix update/conversation runs; full extension tests pending |
| pam_modutil.h / libpam | check_user_in_passwd, getpwnam/getpwuid/getgrnam/getgrgid/getspnam/getlogin, user_in_group_nam_nam/nam_gid/uid_nam/uid_gid, read/write, audit_write, drop_priv/regain_priv, sanitize_helper_fds, search_key (all prefixed pam_modutil_) | Built and exercised indirectly; ERANGE/identity/concurrency matrix pending |
| pam_misc.h / libpam_misc | misc_conv, pam_misc_paste_env, pam_misc_drop_env, pam_misc_setenv, conversation timeout/binary-handler globals | SDK links, guest env operations; timeout/TTY cases pending |
| pam_client.h / libpamc | pamc_start/end/load/converse/disable/list_agents, PAM_BP and PAM_BPC macros | SDK links and guest handle creation/end; external binary agents not provisioned |

The four management stacks and extended control grammar are the upstream core.
Current probes cover required, sufficient, jump, include, substack, missing module,
other fallback and phase failures over 144 transactions with stable FD counts.
Requisite/optional/reset, deep/cyclic configuration and allocation/ABI failure
coverage remain incomplete. Core upstream results are 36 Meson passes and 40
cross-build skips replayed directly as target binaries with passes; root xtests
remain unrun and the disabled xtests build option is not an exemption.

## PAM modules and optional integrations

All 39 staged upstream modules and the local product-rule module are listed here.
A built DSO is not proof of behavior.

| Modules | Current evidence and requirements |
| --- | --- |
| pam_unix | Official module/helper/libxcrypt isolated guest auth/account/password update and nonroot helper tests pass; latest crypt-entry patch passes host, Linux 6.12 and LeonOS password-policy guest03. Standard production accounts are now selected; current integration runtime acceptance paused |
| pam_leonos_password (local) | Product password rule with PAM-owned token cleanup; isolated host, Linux 6.12 and LeonOS guest03 policy/history/official passwd tests pass. Enabled in production common-password; current consumer acceptance paused |
| pam_deny, pam_permit, pam_debug | Actual guest stack and phase-control probes pass; these upstream test/control modules do not replace authentication |
| pam_rootok, pam_env, pam_limits, pam_nologin, pam_shells, pam_access, pam_wheel, pam_succeed_if, pam_faildelay, pam_faillock, pam_exec, pam_umask | All required modules built and host-load checked; exhaustive runtime/guest behavior pending; pam_limits cannot enforce unimplemented resources |
| pam_canonicalize_user, pam_echo, pam_filter, pam_ftp, pam_group, pam_issue, pam_keyinit, pam_listfile, pam_localuser, pam_loginuid, pam_mail, pam_mkhomedir, pam_motd, pam_namespace, pam_pwhistory, pam_securetty, pam_setquota, pam_stress, pam_time, pam_timestamp, pam_usertype, pam_warn, pam_xauth | Built; runtime dependency/permission/lifecycle verification pending. keyrings, quotas, namespaces, loginuid/audit and xauth are not thereby supported |
| pam_lastlog | Upstream Meson default disabled; not built or verified |
| pam_userdb | Auto-detection cannot find target DB/GDBM/NDBM, not built; configuration fails module loading |
| pam_selinux, pam_sepermit, pam_tty_audit | Target SELinux/audit libraries absent, not built; configuration fails module loading |

PAM docs publishing is disabled (XML toolchain), vendordir is empty (/etc is the
service configuration authority), openssl is disabled by upstream default, and
PAM_DEBUG is off. libcrypt is 4.5.2, libdl/intl are available through musl; target
audit, libeconf, libpwaccess, libselinux, systemd/elogind, libnsl/tirpc, DB/GDBM/NDBM
are absent. These are the resolved target results, not Meson's unexpanded `auto`
settings. Module load errors retain upstream PAM error/control semantics; an
administrator can intentionally ignore failures in a stack, so missing modules
are not an unconditional deny guarantee for arbitrary administrator configuration.

sudo LDAP, SSSD, GSSAPI/Kerberos, SELinux, AppArmor, BSD authentication/login
classes, Linux audit and TLS libraries are not built. Local file policy remains
built. Configuration for unbuilt backends cannot make those backends available;
precise diagnostics/fallback for each optional setting still need runtime probes.
sudo_logsrvd exists, but no remote service or TLS transport is certified. The
build lacks target zlib headers; compressed I/O log behavior remains unverified.

## Runtime layout and authority

Staging root is `build/auth-upstream/root`: libraries `/lib`, PAM modules
`/lib/security`, PAM helpers `/sbin`, PAM headers `/usr/include/security`, sudo
`/usr/bin`, visudo `/usr/sbin`, sudo plugins `/usr/lib/sudo`, service configs
`/etc/pam.d`, sudo runtime `/run/sudo`, persistent state `/var/lib/sudo`.
util-linux su is configured for `/bin/su` and runuser for `/sbin/runuser`.
The official su runtime probe passes on fixed Linux and an isolated LeonOS disk:
target password, distinct UID/GID, supplementary groups, normal/login environment,
separate su-l service, and rejection by PAM account/session/setcred phases.
This is historical focused runtime coverage. The normal system entry is now
replaced, but its integrated interactive workflow has not been rerun.
`/sbin/unix_chkpwd` uses root:root mode 4755, as permitted by the pinned helper
manual, to read root:root mode 0600 shadow. The nonroot helper probe proves own
password verification and denial of verification for another account.
Staging filesystem ownership belongs to the builder; normal installer ext2
packaging explicitly supplies root ownership and preserves set-ID modes.

Normal login, passwd, sudo, su and Fileman now use the official programs/PAM
stacks with standard passwd/shadow/group/gshadow authority. authd is removed
from production. The installer commits standard files, and GUI file operations
invoke sudo for every command. Production service files are under
`system/rootfs/etc/pam.d`; the historical `/etc/pam.d/unix` fixture is not a
production service. Current integration acceptance is paused by the user.

A root-managed limited sudoers rule can be checked with official visudo:

```sudoers
root ALL=(ALL:ALL) ALL
alice ALL=(root) /usr/bin/id -u
```

This example permits only that command/argument pair, authenticating alice by
default. It is documentation, not an active grant. The installer must not grant
wheel membership or unrestricted sudo merely by creating a normal account.
