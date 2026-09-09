# POSIX file permissions

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

`chown` to another owner requires UID 0 under the current privilege model.
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
  Existing ACLs remain readable; explicit metadata takes precedence over
  fallback home-directory ownership derived from the existing account database.
- File Manager properties now show owner/group/other rwx controls, octal mode
  and numeric owner/group. Save calls the real chmod/chown API and reloads the
  actual state; denied operations are reported. BusyBox and cmd permission
  commands call the native kernel rather than success-only shims.
- authd owns the existing AUS1 account database (0600) and publishes public
  identity lookup files `/etc/passwd` and `/etc/group` (0644). Existing account
  names and UIDs are preserved, and each account retains primary GID=UID.
  A legacy account named `root` can have UID 1: its name and desktop admin role
  do not grant UID 0. Exported files contain `x`, never password hashes.
- New names reject path separators, control characters, whitespace and colon.
  Bounded request validation rejects unterminated fields. These restrictions
  prevent path traversal and extra passwd/group records during publication.
- Account exports are regenerated at startup and after account creation.
  Each file is replaced using rename after writing a complete temporary file.
  If export fails after users.db was committed, authd retains the committed
  account and reports failure; startup retries publication. The three files
  are not a single crash-atomic database transaction.
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

Known unfinished contracts remain: descriptor identity after rename/unlink,
cross-directory rename, crash/error transaction recovery, FAT/exFAT timestamps,
setuid/setgid execution, saved IDs/fsuid/capabilities and complete special-bit
rules. Synthetic device metadata is volatile. The FAT/exFAT metadata format
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
