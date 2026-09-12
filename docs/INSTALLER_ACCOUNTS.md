# Installer Accounts and Components

Fresh installations provision accounts in the target root before publishing
the EFI boot payload. There is no OOBE application or first-boot account wizard.
The graphical and TTY installers use the same account and component helpers.

- `root` is the fixed administrator identity, UID/GID 0, home `/root`.
- The chosen ordinary account has UID/GID 1000 and home `/home/<username>`.
- Both passwords require 1 to 32 UTF-8 characters and reject whitespace.
  Symbols are allowed; no mixed-case, digit or other complexity requirement is imposed.
- Password confirmations must match. Usernames cannot be `root`, `nobody`,
  `wheel`, `.` or `..`, or contain whitespace, `:`, `,`, `/` or `\`.
- Home directories and their private subdirectories are mode 0700 and owned
  by the corresponding UID/GID. `passwd` and `group` are root-owned mode 0644.
- `/etc/shadow` and `/etc/gshadow` are root-owned mode 0600. The installer
  commits the four standard account files using the shared transaction helper;
  passwords use libxcrypt yescrypt and random salts. There is no private account
  database in a new installation. The initial ordinary account is added to wheel
  in both group and gshadow. It retains UID/GID 1000 and authenticates with its
  own password to use the default `%wheel ALL=(ALL:ALL) ALL` sudo rule.

The installer creates `/etc/leonos/installed`. Installed desktops require a
login even when account lookup or PAM fails. The installer runtime has no
installed marker and remains a live environment. Standalone VMDK and ordinary
ISO test images have their own preseeded `test`/`test` and `root`/`root` accounts
and installed marker; the test account joins wheel. These image-only fixtures
do not enter the installer's source payload.
The PAM login process owns `/run/leonos/session-user`; UID 0 is a valid authenticated
identity, separate from the absence of a login session. Ordinary login and
desktop application launch apply the PAM session's supplementary groups,
environment and available resource limits, then set GID and UID and abort if
dropping privileges fails. The retired authentication socket is not created.

Python and GCC/binutils are optional during a fresh install. Availability comes
from the actual payload. `/install/components.list` is generated from
`tools/leonos_layout.py` and lists owned directories, command links, examples
and licenses. Counting and copying use the same selection filter. Updates
preserve accounts and update these components only when already installed.

Populated AUS2/older private account databases are rejected before update;
passwords are never silently reset or converted. Retain the old disk/backup,
explicitly install with chosen new passwords, then restore personal files with
their intended ownership. Rebuild native applications and SDK consumers.
This does not change the Linux syscall ABI of Python or GCC.

## Elevation

`sudo` and `su` execute the official set-ID programs. Fileman executes a fixed
worker through sudo, with a new policy decision for every command. sudo normally
authenticates the caller; knowing root's password does not grant an unlisted
ordinary user sudo permission. `leonos_admin_elevate()` launches a new application
instance through the same sudo policy and never grants the original process root.
See `docs/SUDO_AND_ELEVATION.md` for configuration and limited grants.

The initial installer account has wheel authorization. Accounts subsequently
created through Settings or useradd are not automatically added to wheel.
Updates preserve existing group membership; rebuild the installer ISO to include
the new installation default.

The focused `python3 tools/test_installer_wheel.py` regression executes the real
account seed and transaction code in an isolated host namespace under ASan/UBSan.
It verifies group/gshadow membership, ordinary UID/GID, protected gshadow and
reserved names; it does not certify guest PAM or sudo execution.

## Validation Commands

These are historical regression entry points. Some still require conversion
from the retired broker fixtures. They have not been rerun for the replacement:
the user explicitly paused behavioral tests on 2026-09-12. The recorded results
below certify only the earlier AUS2 integration.

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
