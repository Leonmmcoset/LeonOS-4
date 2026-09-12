# Authentication and Privileged Execution

Production entry points now use sudo 1.9.17p2, Linux-PAM 1.7.2,
util-linux su 2.41.6, shadow 4.20.2 and libxcrypt 4.5.2. Source URLs,
checksums, licenses and platform patches are listed in
[SUDOERS_PAM_UPSTREAM.md](SUDOERS_PAM_UPSTREAM.md).

The user paused additional tests during integration. The following describes
the code and packaging contract, not a claim of completed runtime acceptance.
See [SUDOERS_PAM_STATUS.md](SUDOERS_PAM_STATUS.md) for verification boundaries.

## Entry Points

| Workflow | Production path | Authority |
| --- | --- | --- |
| sudo and sudoedit | `/usr/bin/sudo`, `/usr/bin/sudoedit` | Official sudoers policy, PAM, timestamp and execution plugins |
| su and login shell | `/bin/su`, `/usr/bin/su` | Official util-linux target authentication and session handling |
| TTY login | LeonOS console setup execs `/bin/login` | Official shadow login and PAM `login` |
| GUI login | `userland/libc/src/pam_session.c` | PAM `leonos-gui`; authentication, account, expired-password update, setcred and session |
| Password modification | `/usr/bin/passwd` | PAM `passwd`; Settings opens it in Terminal |
| Account and group tools | `/usr/sbin/useradd`, `usermod`, `userdel`, `groupadd`, `groupmod`, `groupdel`; `/usr/bin/gpasswd` | Official shadow tools and their file locks |
| Settings account creation | `auth_accounts.c` invokes useradd then chpasswd | Root-only; initially expired until the PAM password update succeeds |
| Fileman privileged operation | Caller forks `/usr/bin/sudo -A -u root -- /usr/lib/leonos/apps/sudod/sudod.elf ...` | A new sudoers decision for each exact command and arguments |

No authd process, socket, RUN/VERIFY/WAIT/FILEOP protocol, private target-password
cache, elevation flag or account-writing kernel operation remains in production.
Historical sources are kept only under `tools/tests/legacy_authd` to preserve
previous working-tree changes and password regressions. They are not packaged.
Update installation explicitly removes the retired command and application
directory even if the old package metadata is missing.

## Account Store

`/etc/passwd`, `/etc/shadow`, `/etc/group` and `/etc/gshadow` are the sole local
authority. passwd/group are root-owned 0644; shadow/gshadow are root-owned 0600.
The installer creates UID/GID 0 root and an ordinary UID/GID 1000 account;
the initial ordinary account joins wheel. Passwords use random yescrypt salts. The shared product
rule is 1-32 UTF-8 scalar values, no Unicode whitespace, no added complexity rule.

The installer uses `userland/auth/account_store.c` for a locked, staged four-file
commit and recovery. Login startup performs pending recovery. Normal changes use
the upstream tools; complete multi-tool crash and power-loss acceptance remains
pending. An interrupted Settings creation can leave an expired account for root
to inspect and finish; it is not reported as successfully created.

Nonempty AUS2 or older `accounts.db` data is refused before an update overlays
system files. There is no passwordless conversion of PBKDF2 to yescrypt and no
silent reset. The controlled path is to retain the old disk/backup, explicitly
perform a fresh installation with chosen passwords, and restore personal files
with their intended ownership. An automatic migration utility is not provided.

## Default Policy

`/etc/sudoers` is root-owned 0440; `/etc/sudoers.d` is root-owned 0750. Root and
wheel have default grants. Fresh installations add their initial ordinary user
to wheel in group and gshadow; sudo authenticates that user's own password.
The account still runs as UID/GID 1000 until it explicitly invokes sudo.
Later account creation does not automatically add users to wheel, and updates
preserve existing memberships. Root can edit policy with `/usr/sbin/visudo`.

For example, root can grant one command with no arguments in an included file:

```sudoers
alice ALL=(root) /usr/bin/id ""
```

A restricted Fileman listing can be granted using the official argument regex:

```sudoers
alice ALL=(root) /usr/lib/leonos/apps/sudod/sudod.elf ^--op 1 --path1 /srv/reports --path2 $
```

The final space before `$` matches the empty final argument. Fileman verbs are
1=list, 2=mkdir, 3=rename and 4=unlink. Do not turn a restricted example into an
unbounded argument wildcard: sudoers matches the complete argument string.
Recursive deletion additionally requires `--path2 DELETE`. Every path is checked
again by the worker, and every traversed parent uses held dirfds and O_NOFOLLOW.
Results use the caller's anonymous pipe; the worker rejects output files and
does not accept a `--result` pathname.

Askpass is the non-set-ID worker's dialog mode. sudo launches it as the invoking
user and consumes its secret over its own pipe. No password is passed in argv,
environment, logs or a persistent result file. sudo performs caller authentication
by default; rootpw/targetpw/runaspw remain official configurable policy.

The old in-process elevation helper cannot grant an identity. It launches the
calling application's actual command through sudo and returns without allowing
the original process's privileged operation. The new application instance must
have a real root identity. This workflow requires an explicit matching sudoers
grant; a cached authentication result is insufficient.

## PAM and GUI Sessions

Service files live in `system/rootfs/etc/pam.d`: sudo, sudo-i, su, su-l, login,
leonos-gui, passwd, chpasswd, newusers, chsh, chfn, runuser and runuser-l.
`other` denies all four stacks. `common-password` applies the shared product
validator before pam_unix's yescrypt update. The common session stack contains
pam_unix, pam_env, pam_limits and pam_umask. The packaged modules include the
required upstream module set; configurable modules such as pam_faillock are not
silently enabled with a new lockout policy.

The GUI login process owns its PAM handle until logout. It publishes a root-only
session marker and environment/limit state after successful authentication,
account and session checks. A held flock on that exact marker distinguishes a
live session from a stale file without relying on a reused PID. Only the desktop
and session launcher explicitly opt into applying this identity; ordinary root
tools do not unexpectedly drop to the desktop user's UID.

Startup entries use SS12 records with an owning UID. The launcher verifies
per-message credentials, restricts management to the owner or root, and drops
to a live PAM session before checking that the final UID still matches the
entry. A logout/login race therefore cannot run an ordinary entry as root.
Nonempty SS11 databases lack trustworthy ownership and require explicit root
recovery; the service preserves and refuses them instead of assigning an owner.

Desktop children receive the PAM environment, supplementary groups, umask and
resource values available from the kernel, then drop GID/UID and enter HOME.
Failures prevent exec. Unsupported resource limits remain documented kernel
gaps; they are not represented as enforced. TTY login, sudo and su retain their
upstream credential, terminal, environment and session lifecycle.

GUI account lists are allocated dynamically. Username/home fields in the existing
GUI structures and Fileman path/result bounds remain explicit interface limits;
the command-line sudo/su entries no longer have the old 8-argument/191-byte limit.

## Verification

No new behavioral tests or QEMU runs were performed after the user's pause.
Build results, image hashes, source-review findings and the exact remaining
kernel/runtime work are recorded in `SUDOERS_PAM_STATUS.md`. Earlier broker,
isolated PAM and kernel tests remain historical evidence at their stated scope.
