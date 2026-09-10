# Installer Accounts and Components

Fresh installations provision accounts in the target root before publishing
the EFI boot payload. There is no OOBE application or first-boot account wizard.
The graphical and TTY installers use the same account and component helpers.

- `root` is the fixed administrator identity, UID/GID 0, home `/root`.
- The chosen ordinary account has UID/GID 1000 and home `/home/<username>`.
- Both passwords require 1 to 32 UTF-8 characters and reject whitespace.
  Symbols are allowed; no mixed-case, digit or other complexity requirement is imposed.
- Password confirmations must match. Usernames cannot be `root`, `nobody`,
  `.` or `..`, or contain whitespace, `:`, `/` or `\`.
- Home directories and their private subdirectories are mode 0700 and owned
  by the corresponding UID/GID. `passwd` and `group` are root-owned mode 0644.
- The private database is root-owned mode 0600 and uses AUS2 records with
  PBKDF2-HMAC-SHA256, 100,000 iterations and a random 128-bit salt per password.
  The implementation uses the repository's Mbed TLS implementation.

The installer creates `/etc/leonos/installed`. Installed desktops require a
login even when the authentication daemon cannot load its database. Live media
and the installer runtime have no installed marker and remain live environments.
The daemon owns `/run/leonos/session-user`; UID 0 is a valid authenticated
identity, separate from the absence of a login session. Ordinary login and
desktop application launch clear supplementary groups, then set GID and UID,
and abort if dropping privileges fails. A pre-login auth socket is closed after
the credential transition so its root `SO_PEERCRED` cannot be reused.

Python and GCC/binutils are optional during a fresh install. Availability comes
from the actual payload. `/install/components.list` is generated from
`tools/leonos_layout.py` and lists owned directories, command links, examples
and licenses. Counting and copying use the same selection filter. Updates
preserve accounts and update these components only when already installed.

AUS1 databases and binaries using the previous authentication message layout
are not migrated. Rebuild native applications and SDK consumers with the current
headers; use a fresh installation for this early-development format change.
This does not change the Linux syscall ABI of Python or GCC.

## Validation Commands

```sh
python3 build.py test installer-setup
python3 tools/test_oobe.py
python3 tools/test_power.py
python3 build.py run installer
python3 tools/test_installer_accounts_qemu.py --output build/installer-accounts-none --components none
python3 tools/test_installer_accounts_qemu.py --output build/installer-accounts-all --components all
```

The historical `test_oobe.py` filename now covers authentication, reboot session
cleanup and desktop service startup regressions; it does not package OOBE.
The QEMU script creates an exclusive scratch disk and refuses to overwrite it.
It records screenshots and serial logs for the GUI installer and installed TTY
sessions. QEMU verification does not constitute VMware verification.

## Recorded Validation (2026-09-11)

- Host installer setup tests passed, including password boundaries, account
  ownership, all four component selections, and TTY input error handling.
  Authentication/session, installer input, and power regressions also passed.
- The installer build completed with zero errors. QEMU/KVM fresh installations
  completed through the GUI with both optional components disabled and through
  the TTY with both enabled. Installed root and ordinary logins reported all
  four UID/GID fields as 0 and 1000 respectively. Ordinary users could not read
  root-private or root-group-only files or write the account file. Component
  command availability matched the installation choices.
- Desktop login and Terminal were checked on a copy of the installed GUI test
  disk with the final Terminal binary replaced in place. The shell reported
  UID/GID 1000 and HOME=/home/alice. This was a targeted check; a fresh install
  of the final ISO was not repeated after the Terminal fix.
- Evidence is under build/installer-accounts-final-gui,
  build/installer-accounts-final-tty, and
  build/installer-accounts-terminal-check. These generated files are not
  committed. VMware and extended concurrency testing remain unverified.
