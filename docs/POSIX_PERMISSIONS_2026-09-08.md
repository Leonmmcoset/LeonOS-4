# POSIX file permissions

## Source reconciliation, 2026-09-12

The retired authd database was AUS2, not AUS1; its source now lives exclusively
under `tools/tests/legacy_authd`. Standard passwd/shadow/group/gshadow files and
Linux-PAM are now the production authority. The administrator is UID/GID 0,
named root, with home /root. The kernel has
saved/filesystem ID and capability fields and some handlers, but their Linux
contracts are incomplete. In particular, the existence of these fields is not
evidence for set-ID exec support. See `SUDOERS_PAM_STATUS.md` for the active
implementation and verification matrix. Historical verification below does
not certify the new sudoers/PAM scope.

The three octal digits select permissions for the file owner, members of its
group, and other users, in that order. Each digit adds read=4, write=2 and
execute/search=1. For example, 640 means owner read/write, group read, others
no access; 755 means owner read/write/execute and group/others read/execute.
Directory write and search permission govern creating/removing names, while
directory read permission governs enumeration.

```sh
chmod 640 /home/alice/document.txt
chmod 700 /home/alice/private
chown alice:alice /home/alice/document.txt
chown 1001:1002 /home/alice/document.txt
umask 027
```

`chown` to another owner requires effective CAP_CHOWN.
An owner can change the group to one of its primary/supplementary groups;
unauthorized changes return EPERM. An inaccessible directory component returns
EACCES before `..` is folded. Changing a mode to 000 removes normal access;
it does not invalidate an already-open descriptor.

## Implementation and migration

- Kernel file access, stat, chmod/fchmod and chown/fchown use independent
  32-bit UID/GID and shared Linux mode bits. The supported *at variants use
  the same checks. File creation applies the process umask, setgid-directory
  group inheritance and sticky-directory deletion restrictions.
- ext2 stores mode and ownership in its native inode, including high UID/GID
  bits. FAT32/exFAT persist explicit values in versioned LEONACL.SYS records.
  Existing ACLs remain readable. No kernel account-database lookup supplies
  implicit home-directory ownership; native/explicit metadata is authoritative.
- File Manager properties now show owner/group/other rwx controls, octal mode
  and numeric owner/group. Save calls the real chmod/chown API and reloads the
  actual state; denied operations are reported. BusyBox and cmd permission
  commands call the native kernel rather than success-only shims.
- `/etc/passwd` and `/etc/group` are root-owned 0644; shadow/gshadow are
  root-owned 0600. New root is UID/GID 0 and the ordinary account is 1000.
  Populated private account databases are explicitly refused before update.
  Standard passwd entries contain `x`, never password hashes.
- New names reject path separators, control characters, whitespace and colon.
  Bounded request validation rejects unterminated fields. These restrictions
  prevent path traversal and extra passwd/group records during publication.
- The installer uses the staged four-file transaction/recovery helper; routine
  account changes use upstream shadow tools and PAM. Actual power-cut and
  concurrent multi-tool acceptance remain incomplete.
- Old binaries must be rebuilt. A zero creation mode now actually means 000;
  legacy application callers and SDK libc adapters have been migrated.
  The distribution runtime is now musl+mimalloc. Existing Picolibc binaries
  must be rebuilt against the current SDK.

## Verification boundary

The Linux permission host fixture runs the real implementation under
ASan/UBSan. Independent static/dynamic musl guest probes exercise owner/group/
other, supplementary groups, umask, sticky directories, chmod/chown and error
paths. Selected unmodified LTP fchmod01 and chown01 cases exit 0. See the full
result matrix in LINUX_ABI_PROGRESS_2026-09-08.md; failed cases remain failures.

Same-directory rename replacement now preserves source contents and permission
metadata on FAT32/exFAT; the musl guest test covers type conflicts and empty vs
nonempty directories. The ext2 backend test uses a real mke2fs image and checks
results with debugfs and e2fsck, with and without the filetype feature.

The earlier unfinished list is superseded by the detailed matrix in
`SUDOERS_PAM_STATUS.md`: focused held-inode, set-ID, saved/fs-ID and capability
probes now pass, but full concurrent lifetime, power-cut persistence and
FAT/exFAT timestamp/special-bit contracts remain incomplete. Synthetic device
metadata is volatile. The FAT/exFAT metadata format
still has a 64-record per-directory limit and reports exhaustion; it must be
extended before claiming Linux-scale directory coverage. VMware is unverified.

Unix pathname sockets are real filesystem nodes: mode/UID/GID and directory
search/write checks apply, close leaves the node, and unlink/rename update
the binding namespace. ext2 stores S_IFSOCK in its inode; FAT/exFAT retain
the type in POSIX metadata. Public services explicitly set mode 0666 because
ordinary users must connect before application-level peer authorization.
The generic IPC listen helper defaults to 0600. The regression that made
windowd's 0755 socket reject user Terminal connections was reproduced and
repaired; normal QEMU OOBE/Terminal/task-manager/graphics checks pass.

Sources: Linux v6.12 [namei.c](https://github.com/torvalds/linux/blob/v6.12/fs/namei.c)
and the pinned upstream musl `src/passwd/getpw_a.c`/`getgr_a.c` in third_party.
Network retrieval used http://127.0.0.1:12334.
