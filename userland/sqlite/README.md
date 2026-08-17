# LeonOS SQLite port

This port builds SQLite 3.46.1 as the ABI-v1 shared library
`0:/system/lib/sqlite.so.3`. It uses the upstream amalgamation generator and
the LeonOS VFS in `leonos_sqlite_vfs.c`.

The VFS uses LeonOS file syscalls and deliberately disables WAL, loadable
extensions, and SQLite threads. File locking is currently a single-process
no-op; applications must not use one database for concurrent writers from
different processes until kernel file locks are available.
