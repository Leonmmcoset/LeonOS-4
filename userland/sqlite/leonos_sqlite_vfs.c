/*
 * LeonOS VFS adapter for SQLite.
 *
 * SQLite is built with SQLITE_OS_OTHER, so this file is the complete file
 * and clock boundary.  LeonOS currently exposes no cross-process advisory
 * locking primitive to userland; locking callbacks therefore serialize only
 * within SQLite's single-threaded connection and WAL is disabled by the
 * build.  Do not use this VFS for concurrent writers until that contract is
 * extended.
 */

#include "sqlite3.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct LeonosStatRaw {
    uint32_t type;
    uint32_t reserved;
    uint64_t size;
};

extern int leonos_stat_raw_call(const char *path, struct LeonosStatRaw *status)
    __asm__("stat");
extern int leonos_fstat_raw_call(int fd, struct LeonosStatRaw *status)
    __asm__("fstat");
extern int leonos_ftruncate_call(int fd, long length) __asm__("ftruncate");
extern int sleep_ms(unsigned long milliseconds);

typedef struct LeonosFile {
    sqlite3_file base;
    int fd;
    int readonly;
} LeonosFile;

static int leonos_close(sqlite3_file *file)
{
    LeonosFile *handle = (LeonosFile *)file;
    int result = close(handle->fd);
    handle->fd = -1;
    return result == 0 ? SQLITE_OK : SQLITE_IOERR_CLOSE;
}

static int leonos_read(sqlite3_file *file, void *buffer, int amount, sqlite3_int64 offset)
{
    LeonosFile *handle = (LeonosFile *)file;
    long position;
    long received;
    position = lseek(handle->fd, (long)offset, SEEK_SET);
    if (position < 0) {
        return SQLITE_IOERR_SEEK;
    }
    received = read(handle->fd, buffer, (size_t)amount);
    if (received < 0) {
        return SQLITE_IOERR_READ;
    }
    if (received < amount) {
        memset((unsigned char *)buffer + received, 0, (size_t)amount - (size_t)received);
        return SQLITE_IOERR_SHORT_READ;
    }
    return SQLITE_OK;
}

static int leonos_write(sqlite3_file *file, const void *buffer, int amount, sqlite3_int64 offset)
{
    LeonosFile *handle = (LeonosFile *)file;
    long position;
    long written;
    if (handle->readonly) {
        return SQLITE_READONLY;
    }
    position = lseek(handle->fd, (long)offset, SEEK_SET);
    if (position < 0) {
        return SQLITE_IOERR_SEEK;
    }
    written = write(handle->fd, buffer, (size_t)amount);
    return written == amount ? SQLITE_OK : SQLITE_IOERR_WRITE;
}

static int leonos_truncate(sqlite3_file *file, sqlite3_int64 size)
{
    LeonosFile *handle = (LeonosFile *)file;
    if (handle->readonly || leonos_ftruncate_call(handle->fd, (long)size) != 0) {
        return handle->readonly ? SQLITE_READONLY : SQLITE_IOERR_TRUNCATE;
    }
    return SQLITE_OK;
}

static int leonos_sync(sqlite3_file *file, int flags)
{
    (void)file;
    (void)flags;
    /* FAT32 commits metadata as part of the write syscall. */
    return SQLITE_OK;
}

static int leonos_size(sqlite3_file *file, sqlite3_int64 *size)
{
    struct LeonosStatRaw status;
    LeonosFile *handle = (LeonosFile *)file;
    if (leonos_fstat_raw_call(handle->fd, &status) != 0) {
        return SQLITE_IOERR_FSTAT;
    }
    *size = (sqlite3_int64)status.size;
    return SQLITE_OK;
}

static int leonos_lock(sqlite3_file *file, int lock)
{
    (void)file;
    (void)lock;
    return SQLITE_OK;
}

static int leonos_unlock(sqlite3_file *file, int lock)
{
    (void)file;
    (void)lock;
    return SQLITE_OK;
}

static int leonos_reserved(sqlite3_file *file, int *reserved)
{
    (void)file;
    *reserved = 0;
    return SQLITE_OK;
}

static int leonos_file_control(sqlite3_file *file, int operation, void *argument)
{
    (void)file;
    (void)operation;
    (void)argument;
    return SQLITE_NOTFOUND;
}

static int leonos_sector_size(sqlite3_file *file)
{
    (void)file;
    return 512;
}

static int leonos_device_characteristics(sqlite3_file *file)
{
    (void)file;
    return 0;
}

static const sqlite3_io_methods leonos_io = {
    .iVersion = 1,
    .xClose = leonos_close,
    .xRead = leonos_read,
    .xWrite = leonos_write,
    .xTruncate = leonos_truncate,
    .xSync = leonos_sync,
    .xFileSize = leonos_size,
    .xLock = leonos_lock,
    .xUnlock = leonos_unlock,
    .xCheckReservedLock = leonos_reserved,
    .xFileControl = leonos_file_control,
    .xSectorSize = leonos_sector_size,
    .xDeviceCharacteristics = leonos_device_characteristics,
};

static int leonos_open(sqlite3_vfs *vfs, const char *path, sqlite3_file *file,
                       int flags, int *out_flags)
{
    LeonosFile *handle = (LeonosFile *)file;
    int native_flags;
    int fd;
    (void)vfs;
    memset(handle, 0, sizeof(*handle));
    handle->fd = -1;
    if (flags & SQLITE_OPEN_READWRITE) {
        native_flags = O_RDWR;
        if (flags & SQLITE_OPEN_CREATE) {
            native_flags |= O_CREAT;
        }
    } else {
        native_flags = O_RDONLY;
    }
    fd = open(path, native_flags, 0666);
    if (fd < 0 && (flags & SQLITE_OPEN_CREATE) && (flags & SQLITE_OPEN_READWRITE)) {
        return SQLITE_CANTOPEN;
    }
    if (fd < 0) {
        return SQLITE_CANTOPEN;
    }
    handle->fd = fd;
    handle->readonly = (native_flags & O_ACCMODE) == O_RDONLY;
    handle->base.pMethods = &leonos_io;
    if (out_flags) {
        *out_flags = flags;
    }
    return SQLITE_OK;
}

static int leonos_delete(sqlite3_vfs *vfs, const char *path, int sync_dir)
{
    (void)vfs;
    (void)sync_dir;
    return unlink(path) == 0 ? SQLITE_OK : SQLITE_IOERR_DELETE;
}

static int leonos_access(sqlite3_vfs *vfs, const char *path, int flags, int *result)
{
    struct LeonosStatRaw status;
    (void)vfs;
    (void)flags;
    *result = leonos_stat_raw_call(path, &status) == 0;
    return SQLITE_OK;
}

static int leonos_full_pathname(sqlite3_vfs *vfs, const char *path, int length, char *output)
{
    (void)vfs;
    if (length <= 0) {
        return SQLITE_CANTOPEN;
    }
    strncpy(output, path, (size_t)length - 1U);
    output[length - 1] = '\0';
    return SQLITE_OK;
}

static int leonos_randomness(sqlite3_vfs *vfs, int length, char *output)
{
    static uint32_t state = 0x9e3779b9U;
    int index;
    (void)vfs;
    for (index = 0; index < length; ++index) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        output[index] = (char)(state >> 24);
    }
    return length;
}

static int leonos_sleep(sqlite3_vfs *vfs, int microseconds)
{
    (void)vfs;
    sleep_ms((unsigned long)((microseconds + 999) / 1000));
    return microseconds;
}

static int leonos_current_time(sqlite3_vfs *vfs, double *now)
{
    (void)vfs;
    /* 2440587.5 is the Julian day at Unix epoch. */
    *now = 2440587.5 + ((double)time(NULL) / 86400.0);
    return SQLITE_OK;
}

static sqlite3_vfs leonos_vfs = {
    .iVersion = 1,
    .szOsFile = sizeof(LeonosFile),
    .mxPathname = 256,
    .zName = "leonos",
    .xOpen = leonos_open,
    .xDelete = leonos_delete,
    .xAccess = leonos_access,
    .xFullPathname = leonos_full_pathname,
    .xRandomness = leonos_randomness,
    .xSleep = leonos_sleep,
    .xCurrentTime = leonos_current_time,
};

int sqlite3_os_init(void)
{
    return sqlite3_vfs_register(&leonos_vfs, 1);
}

int sqlite3_os_end(void)
{
    return sqlite3_vfs_unregister(&leonos_vfs);
}
