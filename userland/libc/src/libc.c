#include <leonos/device.h>
#include <leonos/environment.h>
#include <leonos/driver.h>
#include <leonos/auth.h>
#include <leonos/audio.h>
#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/http.h>
#include <leonos/i18n.h>
#include <leonos/inputm.h>
#include <leonos/net.h>
#include <leonos/mouse.h>
#include <leonos/pty.h>
#include <leonos/stdio.h>
#include <leonos/system.h>
#include <leonos/syscall.h>
#include <leonos/gui.h>
#include <leonos/text.h>
#include <leonos/tls.h>
#include <leonos/ui.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <termios.h>
#include <pty.h>
#include <linux/tty.h>
#include <unistd.h>

/* TinyCC's static archive scan can leave Picolibc's environ member out when
 * the generated program only needs the CRT startup object. Keep a real
 * address available for the startup assignment; Picolibc's strong definition
 * still wins when its full environment implementation is pulled in. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
char **environ;

#ifndef LEONOS_USE_PICOLIBC
int errno;

struct leonos_file {
    int fd;
    long position;
    long length;
    int eof;
    int writable;
};

static struct leonos_file std_streams[3] = {
    {.fd = 0}, {.fd = 1, .writable = 1}, {.fd = 2, .writable = 1},
};
FILE *stdin = &std_streams[0];
FILE *stdout = &std_streams[1];
FILE *stderr = &std_streams[2];

#define HEAP_BLOCK_MAGIC 0x4c48454150424c4bULL
#define MALLOC_ALIGN 16UL
#define HEAP_PAGE_SIZE 4096UL
#define HEAP_ARENA_SIZE (64UL * 1024UL)
#define HEAP_BLOCK_FREE 0x00000001u
#define HEAP_MIN_SPLIT 16UL

struct heap_block {
    uint64_t magic;
    size_t size;
    uint32_t flags;
    uint32_t reserved;
    struct heap_block *prev;
    struct heap_block *next;
    uint64_t reserved2;
};

static struct heap_block *heap_blocks;

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s && s[n]) {
        ++n;
    }
    return n;
}

void *memcpy(void *dst, const void *src, size_t len)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < len; ++i) {
        d[i] = s[i];
    }
    return dst;
}

void *memset(void *dst, int value, size_t len)
{
    unsigned char *d = (unsigned char *)dst;
    for (size_t i = 0; i < len; ++i) {
        d[i] = (unsigned char)value;
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t len)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) {
        for (size_t i = 0; i < len; ++i) {
            d[i] = s[i];
        }
    } else if (d > s) {
        while (len) {
            --len;
            d[len] = s[len];
        }
    }
    return dst;
}

int memcmp(const void *left, const void *right, size_t len)
{
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;
    for (size_t i = 0; i < len; ++i) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

int strcmp(const char *left, const char *right)
{
    while (*left && *left == *right) {
        ++left;
        ++right;
    }
    return (unsigned char)*left - (unsigned char)*right;
}

int strcasecmp(const char *left, const char *right)
{
    while (*left && *right) {
        char a = *left >= 'A' && *left <= 'Z' ? *left + 32 : *left;
        char b = *right >= 'A' && *right <= 'Z' ? *right + 32 : *right;
        if (a != b) {
            return (unsigned char)a - (unsigned char)b;
        }
        ++left;
        ++right;
    }
    return (unsigned char)*left - (unsigned char)*right;
}

int strncasecmp(const char *left, const char *right, size_t len)
{
    while (len && *left && *right) {
        char a = *left >= 'A' && *left <= 'Z' ? *left + 32 : *left;
        char b = *right >= 'A' && *right <= 'Z' ? *right + 32 : *right;
        if (a != b) {
            return (unsigned char)a - (unsigned char)b;
        }
        ++left;
        ++right;
        --len;
    }
    return len ? (unsigned char)*left - (unsigned char)*right : 0;
}

int abs(int value)
{
    return value < 0 ? -value : value;
}

int atoi(const char *text)
{
    int sign = 1;
    int value = 0;
    while (text && (*text == ' ' || *text == '\t')) {
        ++text;
    }
    if (text && *text == '-') {
        sign = -1;
        ++text;
    }
    while (text && *text >= '0' && *text <= '9') {
        value = value * 10 + (*text++ - '0');
    }
    return value * sign;
}

double atof(const char *text)
{
    (void)text;
    return 0.0;
}

char *strdup(const char *text)
{
    size_t len = strlen(text) + 1;
    char *copy = malloc(len);
    return copy ? (char *)memcpy(copy, text, len) : 0;
}

char *getenv(const char *name)
{
    (void)name;
    return 0;
}

int strncmp(const char *left, const char *right, size_t len)
{
    while (len && *left && *left == *right) {
        ++left;
        ++right;
        --len;
    }
    return len ? (unsigned char)*left - (unsigned char)*right : 0;
}

char *strcpy(char *dst, const char *src)
{
    char *result = dst;
    while ((*dst++ = *src++) != 0) {
    }
    return result;
}

char *strncpy(char *dst, const char *src, size_t len)
{
    char *result = dst;
    while (len && *src) {
        *dst++ = *src++;
        --len;
    }
    while (len) {
        *dst++ = 0;
        --len;
    }
    return result;
}

char *strchr(const char *text, int value)
{
    char target = (char)value;
    while (*text) {
        if (*text == target) {
            return (char *)text;
        }
        ++text;
    }
    return target == 0 ? (char *)text : 0;
}

char *strstr(const char *text, const char *needle)
{
    size_t needle_len = strlen(needle);
    if (needle_len == 0) {
        return (char *)text;
    }
    while (*text) {
        if (strncmp(text, needle, needle_len) == 0) {
            return (char *)text;
        }
        ++text;
    }
    return 0;
}

char *strrchr(const char *text, int value)
{
    const char *last = 0;
    char target = (char)value;
    while (text && *text) {
        if (*text == target) {
            last = text;
        }
        ++text;
    }
    return target == 0 ? (char *)text : (char *)last;
}
#endif

long read(int fd, void *buf, size_t len)
{
    struct leonos_stat stat_info;
    struct termios termios;
    size_t done = 0;
    int pty_input = 0;
    int nonblock = 0;

    /* The kernel reports an empty PTY queue as a zero-length read.  That is
     * not EOF for a terminal: wait for input so both canonical and raw-mode
     * POSIX programs see the expected blocking read semantics. */
    if (len != 0) {
        if (tcgetattr(fd, &termios) == 0) {
            pty_input = 1;
        }
        {
            int flags = fcntl(fd, F_GETFL);
            nonblock = flags >= 0 && (flags & O_NONBLOCK) != 0;
        }
    }

    /* The kernel already bounds each file read to this size.  Avoid an
     * additional fstat/path lookup for the small reads used during startup. */
    if (len <= LEONOS_FS_READ_SLICE_BYTES) {
        long result;
        do {
            result = syscall3(SYS_read, fd, (long)buf, (long)len);
            if (result == -LEONOS_EAGAIN) {
                if (nonblock) {
                    return result;
                }
                sleep_ms(1);
                continue;
            }
            if (result != 0 || !pty_input || nonblock) {
                return result;
            }
            sleep_ms(4);
        } while (tcgetattr(fd, &termios) == 0);
        return 0;
    }
    if (leonos_fstat_legacy(fd, &stat_info) < 0 || stat_info.type != LEONOS_FS_TYPE_FILE) {
        /* Pipes, terminals, and device-like descriptors do not expose a
         * regular-file size. Keep the direct path, but preserve the same
         * transparent EAGAIN retry guarantee as regular files. */
        for (;;) {
            long result = syscall3(SYS_read, fd, (long)buf, (long)len);
            if (result != -LEONOS_EAGAIN) {
                return result;
            }
            if (nonblock) {
                return result;
            }
            sleep_ms(1);
        }
    }
    while (done < len) {
        size_t chunk = len - done;
        long ret;
        if (chunk > LEONOS_FS_READ_SLICE_BYTES) {
            chunk = LEONOS_FS_READ_SLICE_BYTES;
        }
        ret = syscall3(SYS_read, fd, (long)((uint8_t *)buf + done), (long)chunk);
        if (ret == -LEONOS_EAGAIN) {
            sleep_ms(1);
            continue;
        }
        if (ret <= 0) {
            return done ? (long)done : ret;
        }
        done += (size_t)ret;
        if ((size_t)ret < chunk) {
            break;
        }
    }
    return (long)done;
}

long write(int fd, const void *buf, size_t len)
{
    size_t done = 0;
    while (done < len) {
        size_t chunk = len - done;
        long ret;
        if (chunk > LEONOS_FS_FILE_WRITE_SLICE_BYTES) {
            chunk = LEONOS_FS_FILE_WRITE_SLICE_BYTES;
        }
        ret = syscall3(SYS_write, fd, (long)((const uint8_t *)buf + done), (long)chunk);
        if (ret == -LEONOS_EAGAIN) {
            sleep_ms(1);
            continue;
        }
        if (ret <= 0) {
            return done ? (long)done : ret;
        }
        done += (size_t)ret;
        if ((size_t)ret < chunk) {
            break;
        }
    }
    return (long)done;
}

int open(const char *path, int flags, ...)
{
    va_list args;
    int mode = 0;

    if (flags & LEONOS_O_CREAT) {
        va_start(args, flags);
        mode = va_arg(args, int);
        va_end(args);
    }
    return (int)syscall3(SYS_open, (long)path, flags, mode);
}

int openat(int dirfd, const char *path, int flags, ...)
{
    va_list args;
    int mode = 0;
    long result;

    if (flags & LEONOS_O_CREAT) {
        va_start(args, flags);
        mode = va_arg(args, int);
        va_end(args);
    }
    result = syscall6(SYS_openat, dirfd, (long)path, flags, mode, 0, 0);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return (int)result;
}

int close(int fd)
{
    return (int)syscall1(SYS_close, fd);
}

long lseek(int fd, long offset, int whence)
{
    return syscall3(SYS_lseek, fd, offset, whence);
}

int mprotect(void *addr, size_t len, int prot)
{
    return (int)syscall3(SYS_mprotect, (long)addr, (long)len, prot);
}

int chdir(const char *path)
{
    long result = syscall1(SYS_chdir, (long)path);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return 0;
}

char *getcwd(char *buf, size_t len)
{
    int allocated = 0;
    long ret;

    /* BusyBox Ash and other portable user programs use the widely supported
     * getcwd(NULL, 0) allocation extension. LeonOS paths have a fixed ABI
     * maximum, so allocate a buffer large enough for every canonical path. */
    if (!buf) {
        if (len != 0) {
            errno = EINVAL;
            return 0;
        }
        len = LEONOS_FS_PATH_LEN;
        buf = malloc(len);
        if (!buf) {
            errno = ENOMEM;
            return 0;
        }
        allocated = 1;
    } else if (len == 0) {
        errno = EINVAL;
        return 0;
    }

    ret = syscall2(SYS_getcwd, (long)buf, (long)len);
    if (ret < 0) {
        errno = (int)-ret;
        if (allocated) {
            free(buf);
        }
        return 0;
    }
    return (char *)ret;
}

int ioctl(int fd, unsigned long request, void *arg)
{
    /* File descriptor 3 was the pre-devfs control channel.  Keep accepting
     * it for old binaries, but resolve it to a real device node so every
     * hardware operation follows the /dev namespace.  The kernel currently
     * dispatches the ioctl by request code; selecting the matching node here
     * also makes descriptor ownership and diagnostics consistent. */
    if (fd == 3) {
        static int console_fd = -1;
        static int fb_fd = -1;
        static int input_method_fd = -1;
        static int audio_fd = -1;
        static int net_fd = -1;
        static int driver_fd = -1;
        static int dev_fd = -1;
        static int tty_fd = -1;
        int *slot = &console_fd;
        const char *path = LEONOS_DEV_CONSOLE;
        int open_flags = LEONOS_O_RDWR;

        if (request == LEONOS_GUI_IOCTL_VERSION ||
            request == LEONOS_GUI_IOCTL_CREATE_WINDOW ||
            request == LEONOS_GUI_IOCTL_POLL_WINDOW ||
            request == LEONOS_GUI_IOCTL_PRESENT_WINDOW ||
            request == LEONOS_GUI_IOCTL_FETCH_WINDOW ||
            request == LEONOS_GUI_IOCTL_WINDOW_EVENT ||
            request == LEONOS_GUI_IOCTL_WAIT_WINDOW_EVENT ||
            request == LEONOS_GUI_IOCTL_SEND_WINDOW_EVENT ||
            request == LEONOS_GUI_IOCTL_DESTROY_WINDOW ||
            request == LEONOS_GUI_IOCTL_UPDATE_WINDOW ||
            request == LEONOS_GUI_IOCTL_SET_TASKBAR_VISIBLE ||
            request == LEONOS_GUI_IOCTL_TASKS ||
            request == LEONOS_GUI_IOCTL_TASK_KILL ||
            request == LEONOS_GUI_IOCTL_DISPLAY_STATE ||
            request == LEONOS_GUI_IOCTL_DISPLAY_REQUEST ||
            request == LEONOS_GUI_IOCTL_POLL_DISPLAY_REQUEST ||
            request == LEONOS_GUI_IOCTL_PUBLISH_DISPLAY_STATE ||
            request == LEONOS_GUI_IOCTL_APPEARANCE_STATE ||
            request == LEONOS_GUI_IOCTL_APPEARANCE_REQUEST ||
            request == LEONOS_GUI_IOCTL_POLL_APPEARANCE_REQUEST ||
            request == LEONOS_GUI_IOCTL_PUBLISH_APPEARANCE_STATE ||
            request == LEONOS_GUI_IOCTL_REBOOT ||
            request == LEONOS_GUI_IOCTL_SHUTDOWN ||
            request == LEONOS_GUI_IOCTL_FB_INFO ||
            request == LEONOS_GUI_IOCTL_FB_CAPS ||
            request == LEONOS_GUI_IOCTL_FB_SET_MODE ||
            request == LEONOS_GUI_IOCTL_FB_FILL ||
            request == LEONOS_GUI_IOCTL_FB_RECT ||
            request == LEONOS_GUI_IOCTL_FB_TEXT ||
            request == LEONOS_GUI_IOCTL_FB_PIXEL ||
            request == LEONOS_GUI_IOCTL_FB_BLIT) {
            path = LEONOS_DEV_FB0;
            slot = &fb_fd;
            open_flags = LEONOS_O_RDWR;
        } else if (request == LEONOS_GUI_IOCTL_EVENT ||
                   request == LEONOS_GUI_IOCTL_MOUSE_STATE ||
                   request == LEONOS_GUI_IOCTL_CURSOR_REQUEST ||
            request == LEONOS_GUI_IOCTL_CURSOR_REGION ||
            request == LEONOS_GUI_IOCTL_SET_MOUSE_VISIBLE) {
            /* GUI event routing is a window-service operation. It must not
             * claim the raw evdev keyboard descriptor. */
            path = LEONOS_DEV_FB0;
            slot = &fb_fd;
            open_flags = LEONOS_O_RDWR;
        } else if (request == LEONOS_INPUTM_IOCTL_REGISTER ||
                   request == LEONOS_INPUTM_IOCTL_UNREGISTER ||
                   request == LEONOS_INPUTM_IOCTL_PROVIDER_NEXT ||
                   request == LEONOS_INPUTM_IOCTL_PROVIDER_RESULT ||
                   request == LEONOS_INPUTM_IOCTL_SUBMIT_KEY ||
                   request == LEONOS_INPUTM_IOCTL_POLL_RESULT ||
                   request == LEONOS_INPUTM_IOCTL_SET_ACTIVE ||
                   request == LEONOS_INPUTM_IOCTL_LIST ||
                   request == LEONOS_INPUTM_IOCTL_CONTEXT ||
                   request == LEONOS_INPUTM_IOCTL_GET_STATE ||
                   request == LEONOS_INPUTM_IOCTL_NOTIFY_CONFIG) {
            path = LEONOS_DEV_INPUT_METHOD;
            slot = &input_method_fd;
        } else if (request == LEONOS_IOCTL_AUDIO_CONFIGURE ||
                   request == LEONOS_IOCTL_AUDIO_WRITE ||
            request == LEONOS_IOCTL_AUDIO_GET_STATE) {
            path = LEONOS_DEV_AUDIO0;
            slot = &audio_fd;
            open_flags = LEONOS_O_RDWR;
        } else if ((request & 0xffff0000UL) == 0x4c4e0000UL) {
            path = LEONOS_DEV_NET0;
            slot = &net_fd;
        } else if (request == LEONOS_IOCTL_DRIVER_LIST ||
                   request == LEONOS_IOCTL_DRIVER_CONTROL) {
            path = LEONOS_DEV_DRIVERCTL;
            slot = &driver_fd;
        } else if (request == LEONOS_IOCTL_DEVICE_LIST) {
            path = "/dev";
            slot = &dev_fd;
            open_flags = LEONOS_O_RDONLY;
        } else if ((request >= LEONOS_PTY_IOCTL_CREATE &&
                    request <= LEONOS_PTY_IOCTL_OWNER_SET_WINSIZE) ||
                   request == LEONOS_PTY_IOCTL_GET_ATTR ||
                   request == LEONOS_PTY_IOCTL_SET_ATTR ||
                   request == LEONOS_PTY_IOCTL_GET_WINSIZE ||
                   request == LEONOS_PTY_IOCTL_SET_WINSIZE) {
            /* Legacy PTY controls operate on the caller's controlling TTY;
             * opening /dev/ptmx here would allocate an unrelated master. */
            path = LEONOS_DEV_TTY;
            slot = &tty_fd;
        }

        if (*slot < 0) {
            *slot = open(path, open_flags, 0);
        }
        if (*slot >= 0) {
            fd = *slot;
        }
        {
            int result = (int)syscall3(SYS_ioctl, fd, (long)request, (long)arg);
            /* Applications may close a cached descriptor explicitly. Retry
             * once with a fresh node if the kernel reports a stale handle. */
            if (result == -9 && fd >= 4 && *slot == fd) {
                (void)close(fd);
                *slot = open(path, open_flags, 0);
                if (*slot >= 0) {
                    result = (int)syscall3(SYS_ioctl, *slot,
                                           (long)request, (long)arg);
                }
            }
            return result;
        }
    }
    return (int)syscall3(SYS_ioctl, fd, (long)request, (long)arg);
}

int leonos_gui_set_mouse_visible(uint32_t window_id, uint32_t visible)
{
    unsigned long value = ((unsigned long)window_id << 32) | (visible ? 1UL : 0UL);
    return ioctl(3, LEONOS_GUI_IOCTL_SET_MOUSE_VISIBLE, (void *)value);
}

int leonos_gui_mouse_visible(void)
{
    return ioctl(3, LEONOS_GUI_IOCTL_SET_MOUSE_VISIBLE, 0);
}

int leonos_mouse_hide(uint32_t window_id)
{
    return leonos_gui_set_mouse_visible(window_id, 0);
}

int leonos_mouse_show(uint32_t window_id)
{
    return leonos_gui_set_mouse_visible(window_id, 1);
}

int leonos_mouse_is_visible(void)
{
    return leonos_gui_mouse_visible();
}

int leonos_gui_cursor_request(const struct leonos_gui_cursor_request *request)
{
    return request ? ioctl(3, LEONOS_GUI_IOCTL_CURSOR_REQUEST, (void *)request) : -1;
}

int leonos_gui_set_cursor_position(uint32_t window_id, int32_t x, int32_t y)
{
    struct leonos_gui_cursor_request request = {
        .window_id = window_id,
        .x = x,
        .y = y,
        .style = LEONOS_GUI_CURSOR_ARROW,
        .flags = LEONOS_GUI_CURSOR_REQUEST_POSITION,
    };
    return leonos_gui_cursor_request(&request);
}

int leonos_gui_set_cursor_style(uint32_t window_id, uint32_t style)
{
    struct leonos_gui_cursor_request request = {
        .window_id = window_id,
        .x = 0,
        .y = 0,
        .style = style,
        .flags = LEONOS_GUI_CURSOR_REQUEST_STYLE,
    };
    return leonos_gui_cursor_request(&request);
}

int leonos_gui_set_cursor_auto(uint32_t window_id)
{
    struct leonos_gui_cursor_request request = {
        .window_id = window_id,
        .x = 0,
        .y = 0,
        .style = LEONOS_GUI_CURSOR_ARROW,
        .flags = LEONOS_GUI_CURSOR_REQUEST_AUTO,
    };
    return leonos_gui_cursor_request(&request);
}

int leonos_mouse_set_position(uint32_t window_id, int32_t x, int32_t y)
{
    return leonos_gui_set_cursor_position(window_id, x, y);
}

int leonos_mouse_set_style(uint32_t window_id, uint32_t style)
{
    return leonos_gui_set_cursor_style(window_id, style);
}

int leonos_mouse_set_auto(uint32_t window_id)
{
    return leonos_gui_set_cursor_auto(window_id);
}

int leonos_mouse_get_state(struct leonos_mouse_state *state)
{
    return state ? ioctl(3, LEONOS_GUI_IOCTL_MOUSE_STATE, state) : -1;
}

int leonos_mouse_get_position(int32_t *x, int32_t *y)
{
    struct leonos_mouse_state state;
    int ret;
    if (!x || !y) {
        return -1;
    }
    ret = leonos_mouse_get_state(&state);
    if (ret > 0) {
        *x = state.x;
        *y = state.y;
    }
    return ret;
}

int leonos_mouse_set_region(const struct leonos_gui_cursor_region_request *region)
{
    return region ? ioctl(3, LEONOS_GUI_IOCTL_CURSOR_REGION, (void *)region) : -1;
}

int leonos_mouse_clear_regions(uint32_t window_id)
{
    struct leonos_gui_cursor_region_request region = {
        .window_id = window_id,
        .operation = LEONOS_GUI_CURSOR_REGION_CLEAR,
    };
    return window_id ? leonos_mouse_set_region(&region) : -1;
}

int sleep_ms(unsigned long ms)
{
    return (int)syscall2(SYS_nanosleep, (long)ms, 0);
}

int leonos_stat_legacy(const char *path, struct leonos_stat *st)
{
    return (int)syscall2(SYS_stat, (long)path, (long)st);
}

int leonos_fstat_legacy(int fd, struct leonos_stat *st)
{
    return (int)syscall2(SYS_fstat, fd, (long)st);
}

int mkdir(const char *path, unsigned int mode)
{
    long result = syscall2(SYS_mkdir, (long)path, mode);
    /* The native syscall ABI returns negative errno values, while the POSIX
     * mkdir contract is -1 with errno set.  Returning the raw value makes
     * callers such as BusyBox print only their generic message (errno remains
     * stale), hiding whether the parent is missing, the filesystem is full,
     * or the operation hit an I/O/read-only failure. */
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return 0;
}

int unlink(const char *path)
{
    return (int)syscall1(SYS_unlink, (long)path);
}

int rmdir(const char *path)
{
    return (int)syscall1(SYS_rmdir, (long)path);
}

int rename(const char *old_path, const char *new_path)
{
    return (int)syscall2(SYS_rename, (long)old_path, (long)new_path);
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, long offset)
{
    long ret = syscall6(SYS_mmap, (long)addr, (long)len, prot, flags, fd, offset);
    return ret < 0 ? LEONOS_MAP_FAILED : (void *)ret;
}

int munmap(void *addr, size_t len)
{
    return (int)syscall2(SYS_munmap, (long)addr, (long)len);
}

#define SBRK_PAGE_SIZE 4096U
#define SBRK_MIN_RESERVE (64U * 1024U)

static uint8_t *sbrk_break;
static uint8_t *sbrk_mapped_end;

static int sbrk_reserve(size_t minimum)
{
    size_t length = minimum < SBRK_MIN_RESERVE ? SBRK_MIN_RESERVE : minimum;
    uint32_t flags = LEONOS_MAP_PRIVATE | LEONOS_MAP_ANONYMOUS;
    void *base;

    if (length > (size_t)-1 - (SBRK_PAGE_SIZE - 1U)) {
        return 0;
    }
    length = (length + SBRK_PAGE_SIZE - 1U) & ~(size_t)(SBRK_PAGE_SIZE - 1U);
    if (sbrk_mapped_end) {
        flags |= LEONOS_MAP_FIXED;
    }
    base = mmap(sbrk_mapped_end, length, LEONOS_PROT_READ | LEONOS_PROT_WRITE,
                flags, -1, 0);
    if (base == LEONOS_MAP_FAILED ||
        (sbrk_mapped_end && base != (void *)sbrk_mapped_end)) {
        if (base != LEONOS_MAP_FAILED) {
            (void)munmap(base, length);
        }
        return 0;
    }
    if ((uintptr_t)base > (uintptr_t)-1 - length) {
        (void)munmap(base, length);
        return 0;
    }
    if (!sbrk_break) {
        sbrk_break = (uint8_t *)base;
    }
    sbrk_mapped_end = (uint8_t *)base + length;
    return 1;
}

void *sbrk(ptrdiff_t increment)
{
    uint8_t *previous;
    size_t amount;
    uintptr_t target;

    if (increment == 0) {
        return sbrk_break;
    }
    if (increment < 0) {
        amount = (size_t)(-(increment + 1)) + 1U;
        if (!sbrk_break || amount > (size_t)(uintptr_t)sbrk_break) {
            return (void *)-1;
        }
        previous = sbrk_break;
        sbrk_break -= amount;
        return previous;
    }
    amount = (size_t)increment;
    if ((ptrdiff_t)amount != increment) {
        return (void *)-1;
    }
    if (!sbrk_break && !sbrk_reserve(amount)) {
        return (void *)-1;
    }
    previous = sbrk_break;
    if ((uintptr_t)previous > (uintptr_t)-1 - amount) {
        return (void *)-1;
    }
    target = (uintptr_t)previous + amount;
    if (target > (uintptr_t)sbrk_mapped_end &&
        !sbrk_reserve((size_t)(target - (uintptr_t)sbrk_mapped_end))) {
        return (void *)-1;
    }
    sbrk_break = (uint8_t *)target;
    return previous;
}

#ifndef LEONOS_USE_PICOLIBC
void exit(int code)
{
    _exit(code);
}

static size_t malloc_align_up(size_t value)
{
    return (value + MALLOC_ALIGN - 1) & ~(MALLOC_ALIGN - 1);
}

static size_t page_align_up(size_t value)
{
    return (value + HEAP_PAGE_SIZE - 1) & ~(HEAP_PAGE_SIZE - 1);
}

static uint8_t *block_payload(struct heap_block *block)
{
    return (uint8_t *)(block + 1);
}

static int blocks_adjacent(struct heap_block *left, struct heap_block *right)
{
    return left && right &&
           block_payload(left) + left->size == (uint8_t *)right;
}

static int block_is_page_aligned(struct heap_block *block)
{
    return (((uint64_t)(uintptr_t)block) & (HEAP_PAGE_SIZE - 1UL)) == 0;
}

static void heap_insert_sorted(struct heap_block *block)
{
    struct heap_block *cur = heap_blocks;
    struct heap_block *prev = 0;
    while (cur && cur < block) {
        prev = cur;
        cur = cur->next;
    }
    block->prev = prev;
    block->next = cur;
    if (prev) {
        prev->next = block;
    } else {
        heap_blocks = block;
    }
    if (cur) {
        cur->prev = block;
    }
}

static void split_block(struct heap_block *block, size_t size)
{
    if (!block || block->size < size + sizeof(struct heap_block) + HEAP_MIN_SPLIT) {
        return;
    }
    struct heap_block *rest =
        (struct heap_block *)(void *)(block_payload(block) + size);
    rest->magic = HEAP_BLOCK_MAGIC;
    rest->size = block->size - size - sizeof(struct heap_block);
    rest->flags = HEAP_BLOCK_FREE;
    rest->reserved = 0;
    rest->reserved2 = 0;
    rest->prev = block;
    rest->next = block->next;
    if (rest->next) {
        rest->next->prev = rest;
    }
    block->next = rest;
    block->size = size;
}

static struct heap_block *coalesce_block(struct heap_block *block)
{
    if (!block) {
        return 0;
    }
    for (;;) {
        if (block->prev && (block->prev->flags & HEAP_BLOCK_FREE) &&
            blocks_adjacent(block->prev, block)) {
            struct heap_block *prev = block->prev;
            prev->size += sizeof(struct heap_block) + block->size;
            prev->next = block->next;
            if (prev->next) {
                prev->next->prev = prev;
            }
            block->magic = 0;
            block = prev;
            continue;
        }
        if (block->next && (block->next->flags & HEAP_BLOCK_FREE) &&
            blocks_adjacent(block, block->next)) {
            struct heap_block *next = block->next;
            block->size += sizeof(struct heap_block) + next->size;
            block->next = next->next;
            if (block->next) {
                block->next->prev = block;
            }
            next->magic = 0;
            continue;
        }
        break;
    }
    return block;
}

static void heap_release_block(struct heap_block *block)
{
    if (!block || !(block->flags & HEAP_BLOCK_FREE) || !block_is_page_aligned(block)) {
        return;
    }
    if (block->size > (size_t)-1 - sizeof(struct heap_block)) {
        return;
    }
    size_t len = block->size + sizeof(struct heap_block);
    if (len < HEAP_ARENA_SIZE || (len & (HEAP_PAGE_SIZE - 1UL)) != 0) {
        return;
    }

    struct heap_block *prev = block->prev;
    struct heap_block *next = block->next;
    if (munmap(block, len) < 0) {
        return;
    }
    if (prev) {
        prev->next = next;
    } else {
        heap_blocks = next;
    }
    if (next) {
        next->prev = prev;
    }
}

static struct heap_block *heap_find_free(size_t size)
{
    for (struct heap_block *block = heap_blocks; block; block = block->next) {
        if ((block->flags & HEAP_BLOCK_FREE) && block->size >= size) {
            return block;
        }
    }
    return 0;
}

static struct heap_block *heap_expand(size_t size)
{
    if (size > (size_t)-1 - sizeof(struct heap_block)) {
        return 0;
    }
    size_t need = size + sizeof(struct heap_block);
    size_t arena_len = need < HEAP_ARENA_SIZE ? HEAP_ARENA_SIZE : page_align_up(need);
    if (arena_len < need) {
        return 0;
    }
    void *base = mmap(0, arena_len,
                      LEONOS_PROT_READ | LEONOS_PROT_WRITE,
                      LEONOS_MAP_PRIVATE | LEONOS_MAP_ANONYMOUS,
                      -1, 0);
    if (base == LEONOS_MAP_FAILED) {
        return 0;
    }
    struct heap_block *block = (struct heap_block *)base;
    block->magic = HEAP_BLOCK_MAGIC;
    block->size = arena_len - sizeof(struct heap_block);
    block->flags = HEAP_BLOCK_FREE;
    block->reserved = 0;
    block->reserved2 = 0;
    block->prev = 0;
    block->next = 0;
    heap_insert_sorted(block);
    return coalesce_block(block);
}

void *malloc(size_t size)
{
    if (size == 0) {
        size = 1;
    }
    size_t aligned = malloc_align_up(size);
    if (aligned < size) {
        return 0;
    }
    struct heap_block *block = heap_find_free(aligned);
    if (!block) {
        block = heap_expand(aligned);
        if (!block) {
            return 0;
        }
        block = heap_find_free(aligned);
        if (!block) {
            return 0;
        }
    }
    split_block(block, aligned);
    block->flags &= ~HEAP_BLOCK_FREE;
    return block_payload(block);
}

void *calloc(size_t nmemb, size_t size)
{
    if (size && nmemb > (size_t)-1 / size) {
        return 0;
    }
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *realloc(void *ptr, size_t size)
{
    if (!ptr) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return 0;
    }

    struct heap_block *block = ((struct heap_block *)ptr) - 1;
    if (block->magic != HEAP_BLOCK_MAGIC || (block->flags & HEAP_BLOCK_FREE)) {
        return 0;
    }

    size_t aligned = malloc_align_up(size);
    if (aligned < size) {
        return 0;
    }
    if (block->size >= aligned) {
        split_block(block, aligned);
        return ptr;
    }
    if (block->next && (block->next->flags & HEAP_BLOCK_FREE) &&
        blocks_adjacent(block, block->next) &&
        block->size + sizeof(struct heap_block) + block->next->size >= aligned) {
        struct heap_block *next = block->next;
        block->size += sizeof(struct heap_block) + next->size;
        block->next = next->next;
        if (block->next) {
            block->next->prev = block;
        }
        next->magic = 0;
        split_block(block, aligned);
        block->flags &= ~HEAP_BLOCK_FREE;
        return ptr;
    }

    void *new_ptr = malloc(size);
    if (!new_ptr) {
        return 0;
    }
    memcpy(new_ptr, ptr, block->size < size ? block->size : size);
    free(ptr);
    return new_ptr;
}

void free(void *ptr)
{
    if (!ptr) {
        return;
    }
    struct heap_block *block = ((struct heap_block *)ptr) - 1;
    if (block->magic != HEAP_BLOCK_MAGIC || (block->flags & HEAP_BLOCK_FREE)) {
        return;
    }
    block->flags |= HEAP_BLOCK_FREE;
    heap_release_block(coalesce_block(block));
}

int puts(const char *s)
{
    size_t len = strlen(s);
    write(1, s, len);
    write(1, "\n", 1);
    return (int)len + 1;
}

static void print_num(char *buf, size_t *pos, unsigned long value, unsigned base)
{
    char tmp[32];
    const char *digits = "0123456789abcdef";
    size_t i = 0;
    if (value == 0) {
        buf[(*pos)++] = '0';
        return;
    }
    while (value) {
        tmp[i++] = digits[value % base];
        value /= base;
    }
    while (i) {
        buf[(*pos)++] = tmp[--i];
    }
}

static size_t format_text(char *buf, size_t cap, const char *fmt, va_list ap);

int printf(const char *fmt, ...)
{
    char buffer[1024];
    va_list args;
    size_t length;
    va_start(args, fmt);
    length = format_text(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    write(1, buffer, length);
    return (int)length;
}

static size_t format_text(char *buf, size_t cap, const char *fmt, va_list ap)
{
    size_t pos = 0;
    for (const char *p = fmt; p && *p && pos + 1 < cap; ++p) {
        if (*p != '%') {
            buf[pos++] = *p;
            continue;
        }
        ++p;
        int zero_pad = 0;
        int width = 0;
        int precision = -1;
        if (*p == '0') {
            zero_pad = 1;
            ++p;
        }
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            ++p;
        }
        if (*p == '.') {
            precision = 0;
            ++p;
            while (*p >= '0' && *p <= '9') {
                precision = precision * 10 + (*p - '0');
                ++p;
            }
        }
        int long_value = 0;
        if (*p == 'l') {
            long_value = 1;
            ++p;
        }
        switch (*p) {
        case 's': {
            const char *text = va_arg(ap, const char *);
            while (text && *text && pos + 1 < cap) {
                buf[pos++] = *text++;
            }
            break;
        }
        case 'm': {
            /* GNU/POSIX shells use %m to format the current errno without
             * consuming an argument.  Keep it available to BusyBox and
             * other portable applications using the shared formatter. */
            const char *text = strerror(errno);
            if (!text) {
                text = "Unknown error";
            }
            while (text && *text && pos + 1 < cap) {
                buf[pos++] = *text++;
            }
            break;
        }
        case 'd':
        case 'i': {
            long value = long_value ? va_arg(ap, long) : (long)va_arg(ap, int);
            size_t digits_start = pos;
            if (value < 0 && pos + 1 < cap) {
                buf[pos++] = '-';
                digits_start = pos;
                value = -value;
            }
            print_num(buf, &pos, (unsigned long)value, 10);
            while (zero_pad && width > (int)(pos - digits_start) && pos + 1 < cap) {
                for (size_t i = pos; i > digits_start; --i) {
                    buf[i] = buf[i - 1];
                }
                buf[digits_start] = '0';
                ++pos;
            }
            while (precision > (int)(pos - digits_start) && pos + 1 < cap) {
                for (size_t i = pos; i > digits_start; --i) {
                    buf[i] = buf[i - 1];
                }
                buf[digits_start] = '0';
                ++pos;
            }
            break;
        }
        case 'u': {
            size_t start = pos;
            print_num(buf, &pos, long_value ? (unsigned long)va_arg(ap, unsigned long)
                                             : (unsigned long)va_arg(ap, unsigned int), 10);
            while (zero_pad && width > (int)(pos - start) && pos + 1 < cap) {
                for (size_t i = pos; i > start; --i) {
                    buf[i] = buf[i - 1];
                }
                buf[start] = '0';
                ++pos;
            }
            while (precision > (int)(pos - start) && pos + 1 < cap) {
                for (size_t i = pos; i > start; --i) {
                    buf[i] = buf[i - 1];
                }
                buf[start] = '0';
                ++pos;
            }
            break;
        }
        case 'x':
        case 'X': {
            size_t start = pos;
            print_num(buf, &pos, long_value ? (unsigned long)va_arg(ap, unsigned long)
                                             : (unsigned long)va_arg(ap, unsigned int), 16);
            while (zero_pad && width > (int)(pos - start) && pos + 1 < cap) {
                for (size_t i = pos; i > start; --i) {
                    buf[i] = buf[i - 1];
                }
                buf[start] = '0';
                ++pos;
            }
            while (precision > (int)(pos - start) && pos + 1 < cap) {
                for (size_t i = pos; i > start; --i) {
                    buf[i] = buf[i - 1];
                }
                buf[start] = '0';
                ++pos;
            }
            break;
        }
        case 'f': {
            (void)va_arg(ap, double);
            if (precision != 0 && pos + 1 < cap) {
                buf[pos++] = '0';
                buf[pos++] = '.';
                if (pos + 1 < cap) {
                    buf[pos++] = '0';
                }
            } else if (pos + 1 < cap) {
                buf[pos++] = '0';
            }
            break;
        }
        case 'p':
            if (pos + 2 < cap) {
                buf[pos++] = '0';
                buf[pos++] = 'x';
            }
            print_num(buf, &pos, (unsigned long)(uintptr_t)va_arg(ap, void *), 16);
            break;
        case 'c':
            if (pos + 1 < cap) {
                buf[pos++] = (char)va_arg(ap, int);
            }
            break;
        case '%':
            buf[pos++] = '%';
            break;
        default:
            if (pos + 2 < cap) {
                buf[pos++] = '%';
                buf[pos++] = *p;
            }
            break;
        }
    }
    if (cap) {
        buf[pos < cap ? pos : cap - 1] = 0;
    }
    return pos;
}

int vsnprintf(char *buffer, size_t capacity, const char *fmt, va_list args)
{
    va_list copy;
    size_t length;
    if (!buffer || capacity == 0) {
        return 0;
    }
    va_copy(copy, args);
    length = format_text(buffer, capacity, fmt, copy);
    va_end(copy);
    return (int)length;
}

int snprintf(char *buffer, size_t capacity, const char *fmt, ...)
{
    va_list args;
    int result;
    va_start(args, fmt);
    result = vsnprintf(buffer, capacity, fmt, args);
    va_end(args);
    return result;
}

int vfprintf(FILE *stream, const char *fmt, va_list args)
{
    char buffer[1024];
    va_list copy;
    size_t len;
    if (!stream) {
        return -1;
    }
    va_copy(copy, args);
    len = format_text(buffer, sizeof(buffer), fmt, copy);
    va_end(copy);
    return write(stream->fd, buffer, len) < 0 ? -1 : (int)len;
}

int fprintf(FILE *stream, const char *fmt, ...)
{
    va_list args;
    int result;
    va_start(args, fmt);
    result = vfprintf(stream, fmt, args);
    va_end(args);
    return result;
}

int sscanf(const char *text, const char *format, ...)
{
    va_list args;
    int converted = 0;
    va_start(args, format);
    while (text && format && *format) {
        if (*format != '%') {
            if (*format == ' ' || *format == '\t') {
                while (*text == ' ' || *text == '\t') {
                    ++text;
                }
            } else if (*text != *format) {
                break;
            } else {
                ++text;
            }
            ++format;
            continue;
        }
        ++format;
        if (*format == 'd' || *format == 'i') {
            int *out = va_arg(args, int *);
            int sign = 1;
            int value = 0;
            while (*text == ' ' || *text == '\t') {
                ++text;
            }
            if (*text == '-') {
                sign = -1;
                ++text;
            }
            while (*text >= '0' && *text <= '9') {
                value = value * 10 + (*text++ - '0');
            }
            if (out) {
                *out = value * sign;
                ++converted;
            }
        } else if (*format == 'x' || *format == 'X') {
            unsigned int *out = va_arg(args, unsigned int *);
            unsigned int value = 0;
            while (*text == ' ' || *text == '\t') {
                ++text;
            }
            while ((*text >= '0' && *text <= '9') ||
                   (*text >= 'a' && *text <= 'f') ||
                   (*text >= 'A' && *text <= 'F')) {
                unsigned int digit = *text >= '0' && *text <= '9'
                                         ? (unsigned int)(*text - '0')
                                         : (unsigned int)((*text | 32) - 'a' + 10);
                value = value * 16U + digit;
                ++text;
            }
            if (out) {
                *out = value;
                ++converted;
            }
        }
        ++format;
    }
    va_end(args);
    return converted;
}

int putchar(int ch)
{
    char value = (char)ch;
    return write(1, &value, 1) == 1 ? ch : -1;
}

FILE *fopen(const char *path, const char *mode)
{
    struct leonos_file *file;
    int flags = LEONOS_O_RDONLY;
    if (!path || !mode) {
        return 0;
    }
    if (mode[0] == 'w') {
        flags = LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC;
    } else if (mode[0] == 'a') {
        flags = LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_APPEND;
    } else if (mode[0] == 'r' && mode[1] == '+') {
        flags = LEONOS_O_RDWR;
    }
    int fd = open(path, flags, 0);
    if (fd < 0) {
        errno = -fd;
        return 0;
    }
    file = malloc(sizeof(*file));
    if (!file) {
        close(fd);
        return 0;
    }
    struct leonos_stat info;
    file->fd = fd;
    file->position = 0;
    file->length = leonos_stat_legacy(path, &info) == 0 ? (long)info.size : 0;
    file->eof = 0;
    file->writable = (flags & LEONOS_O_ACCMODE) != LEONOS_O_RDONLY;
    if (flags & LEONOS_O_APPEND) {
        file->position = file->length;
    }
    return file;
}

size_t fread(void *buffer, size_t size, size_t count, FILE *stream)
{
    size_t bytes = size * count;
    long got;
    if (!stream || !buffer || !size) {
        return 0;
    }
    got = read(stream->fd, buffer, bytes);
    if (got < 0) {
        return 0;
    }
    stream->position += got;
    if ((size_t)got < bytes) {
        stream->eof = 1;
    }
    return (size_t)got / size;
}

size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream)
{
    size_t bytes = size * count;
    long wrote;
    if (!stream || !buffer || !size || !stream->writable) {
        return 0;
    }
    wrote = write(stream->fd, buffer, bytes);
    if (wrote < 0) {
        return 0;
    }
    stream->position += wrote;
    return (size_t)wrote / size;
}

int fclose(FILE *stream)
{
    int result;
    if (!stream || stream == stdin || stream == stdout || stream == stderr) {
        return -1;
    }
    result = close(stream->fd);
    free(stream);
    return result;
}

int fseek(FILE *stream, long offset, int whence)
{
    long result;
    if (!stream) {
        return -1;
    }
    result = lseek(stream->fd, offset, whence);
    if (result < 0) {
        return -1;
    }
    stream->position = result;
    stream->eof = 0;
    return 0;
}

long ftell(FILE *stream)
{
    return stream ? stream->position : -1;
}

int feof(FILE *stream)
{
    return stream ? stream->eof : 1;
}

char *fgets(char *buffer, int size, FILE *stream)
{
    int pos = 0;
    char ch;
    if (!buffer || size <= 1 || !stream) {
        return 0;
    }
    while (pos + 1 < size && fread(&ch, 1, 1, stream) == 1) {
        buffer[pos++] = ch;
        if (ch == '\n') {
            break;
        }
    }
    buffer[pos] = 0;
    return pos ? buffer : 0;
}

int fflush(FILE *stream)
{
    (void)stream;
    return 0;
}

int fileno(FILE *stream)
{
    return stream ? stream->fd : -1;
}

int remove(const char *path)
{
    return unlink(path);
}

#endif

int system(const char *command)
{
    (void)command;
    return -1;
}

int isatty(int fd)
{
    struct termios termios;
    return tcgetattr(fd, &termios) == 0;
}

/* Keep one descriptor per process so per-frame drawing does not repeatedly
 * allocate and release a device fd.  The kernel still accepts the legacy
 * control descriptor for old statically linked applications. */
static int leonos_framebuffer_fd(void)
{
    static int fd = -1;
    if (fd < 0) {
        fd = open(LEONOS_DEV_FB0, LEONOS_O_RDWR, 0);
        if (fd < 0) {
            fd = 3;
        }
    }
    return fd;
}

int leonos_gui_connect(void)
{
    return ioctl(leonos_framebuffer_fd(), LEONOS_GUI_IOCTL_VERSION, 0);
}

int leonos_gui_create_window(const struct leonos_gui_window *window)
{
    if (!window || !window->width || !window->height || !window->title || !window->text) {
        return -1;
    }
    return leonos_gui_create_app_window_ex(window->title, window->text,
                                           window->width, window->height, window->flags);
}

int leonos_gui_next_event(struct leonos_input_event *event)
{
    return ioctl(3, LEONOS_GUI_IOCTL_EVENT, event);
}

unsigned long leonos_uptime_ms(void)
{
    return (unsigned long)ioctl(3, LEONOS_GUI_IOCTL_UPTIME_MS, 0);
}

int leonos_fb_info(struct leonos_fb_info *info)
{
    return ioctl(leonos_framebuffer_fd(), LEONOS_GUI_IOCTL_FB_INFO, info);
}

int leonos_fb_capabilities(struct leonos_fb_capabilities *caps)
{
    return ioctl(leonos_framebuffer_fd(), LEONOS_GUI_IOCTL_FB_CAPS, caps);
}

int leonos_fb_set_mode(uint32_t width, uint32_t height)
{
    struct leonos_fb_mode mode = {
        .width = width,
        .height = height,
    };
    return ioctl(leonos_framebuffer_fd(), LEONOS_GUI_IOCTL_FB_SET_MODE, &mode);
}

int leonos_fb_fill(uint32_t color)
{
    return ioctl(leonos_framebuffer_fd(), LEONOS_GUI_IOCTL_FB_FILL, (void *)(long)color);
}

int leonos_fb_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color)
{
    struct leonos_fb_rect rect = {
        .x = x,
        .y = y,
        .width = width,
        .height = height,
        .color = color,
    };
    return ioctl(leonos_framebuffer_fd(), LEONOS_GUI_IOCTL_FB_RECT, &rect);
}

int leonos_fb_text(uint32_t x, uint32_t y, const char *text, uint32_t fg, uint32_t bg)
{
    struct leonos_fb_text cmd = {
        .x = x,
        .y = y,
        .fg = fg,
        .bg = bg,
        .text = text,
    };
    return ioctl(leonos_framebuffer_fd(), LEONOS_GUI_IOCTL_FB_TEXT, &cmd);
}

uint32_t leonos_fb_pixel(uint32_t x, uint32_t y)
{
    unsigned long packed = ((unsigned long)y << 32) | x;
    return (uint32_t)ioctl(leonos_framebuffer_fd(), LEONOS_GUI_IOCTL_FB_PIXEL, (void *)packed);
}

int leonos_fb_blit(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t stride, const uint32_t *pixels)
{
    struct leonos_fb_blit cmd = {
        .x = x,
        .y = y,
        .width = width,
        .height = height,
        .stride = stride,
        .pixels = pixels,
    };
    return ioctl(leonos_framebuffer_fd(), LEONOS_GUI_IOCTL_FB_BLIT, &cmd);
}

int leonos_gui_create_app_window(const char *title, const char *text, uint32_t width, uint32_t height)
{
    return leonos_gui_create_app_window_ex(title, text, width, height, 0);
}

int leonos_gui_create_app_window_ex(const char *title, const char *text,
                                    uint32_t width, uint32_t height, uint32_t flags)
{
    struct leonos_gui_create cmd = {
        .width = width,
        .height = height,
        .title = title,
        .text = text,
        .flags = flags,
    };
    int ret = ioctl(3, LEONOS_GUI_IOCTL_CREATE_WINDOW, &cmd);
    if (ret > 0) {
        struct leonos_appearance_state appearance;
        leonos_inputm_note_gui_window((uint32_t)ret);
        if (ioctl(3, LEONOS_GUI_IOCTL_APPEARANCE_STATE, &appearance) > 0) {
            (void)leonos_ui_theme_set_appearance(appearance.theme,
                                                 appearance.metro_color_scheme,
                                                 appearance.win95_color_scheme);
        }
    }
    return ret;
}

int leonos_gui_destroy_app_window(uint32_t window_id)
{
    return ioctl(3, LEONOS_GUI_IOCTL_DESTROY_WINDOW, (void *)(unsigned long)window_id);
}

int leonos_gui_update_window(const struct leonos_gui_window_update *update)
{
    if (!update) {
        return -1;
    }
    return ioctl(3, LEONOS_GUI_IOCTL_UPDATE_WINDOW, (void *)update);
}

int leonos_gui_set_window_title(uint32_t window_id, const char *title)
{
    struct leonos_gui_window_update update = {
        .window_id = window_id,
        .mask = LEONOS_GUI_WINDOW_UPDATE_TITLE,
        .flags = 0,
        .title = title,
    };
    return leonos_gui_update_window(&update);
}

int leonos_gui_set_window_borderless(uint32_t window_id, uint32_t borderless)
{
    struct leonos_gui_window_update update = {
        .window_id = window_id,
        .mask = LEONOS_GUI_WINDOW_UPDATE_BORDERLESS,
        .flags = borderless ? LEONOS_GUI_WINDOW_BORDERLESS : 0,
        .title = 0,
    };
    return leonos_gui_update_window(&update);
}

int leonos_gui_set_window_taskbar_visible(uint32_t window_id, uint32_t visible)
{
    struct leonos_gui_window_update update = {
        .window_id = window_id,
        .mask = LEONOS_GUI_WINDOW_UPDATE_TASKBAR,
        .flags = visible ? 0 : LEONOS_GUI_WINDOW_HIDE_TASKBAR,
        .title = 0,
    };
    return leonos_gui_update_window(&update);
}

int leonos_gui_set_taskbar_visible(uint32_t window_id, uint32_t visible)
{
    struct leonos_gui_taskbar_request request = {
        .window_id = window_id,
        .visible = visible ? 1u : 0u,
    };
    return ioctl(3, LEONOS_GUI_IOCTL_SET_TASKBAR_VISIBLE, &request);
}

int leonos_gui_poll_window(struct leonos_gui_window_msg *message)
{
    return ioctl(3, LEONOS_GUI_IOCTL_POLL_WINDOW, message);
}

int leonos_gui_present_window(uint32_t window_id, uint32_t width, uint32_t height,
                              uint32_t stride, const uint32_t *pixels)
{
    struct leonos_gui_present cmd = {
        .window_id = window_id,
        .width = width,
        .height = height,
        .stride = stride,
        .pixels = pixels,
    };
    leonos_ui_present_for_pixels(pixels, window_id);
    return ioctl(3, LEONOS_GUI_IOCTL_PRESENT_WINDOW, &cmd);
}

int leonos_gui_fetch_window(uint32_t window_id, uint32_t capacity_width, uint32_t capacity_height,
                            uint32_t stride, uint32_t *pixels,
                            uint32_t *out_width, uint32_t *out_height)
{
    struct leonos_gui_fetch cmd = {
        .window_id = window_id,
        .capacity_width = capacity_width,
        .capacity_height = capacity_height,
        .stride = stride,
        .out_width = 0,
        .out_height = 0,
        .pixels = pixels,
    };
    int ret = ioctl(3, LEONOS_GUI_IOCTL_FETCH_WINDOW, &cmd);
    if (out_width) {
        *out_width = cmd.out_width;
    }
    if (out_height) {
        *out_height = cmd.out_height;
    }
    return ret;
}

static int leonos_gui_inputm_commit_event(struct leonos_gui_app_event *event)
{
    uint32_t window_id;
    if (!event || !event->window_id) {
        return 0;
    }
    window_id = event->window_id;
    if (leonos_inputm_poll_gui_commit(window_id) <= 0) {
        return 0;
    }
    *event = (struct leonos_gui_app_event){0};
    event->window_id = window_id;
    event->type = LEONOS_GUI_APP_EVENT_KEY_DOWN;
    event->pressed = 1;
    if (leonos_inputm_take_key(&event->keycode, &event->pressed)) {
        event->type = event->pressed ? LEONOS_GUI_APP_EVENT_KEY_DOWN :
                                     LEONOS_GUI_APP_EVENT_KEY_UP;
    }
    return 1;
}

static void leonos_gui_observe_inputm_key(struct leonos_gui_app_event *event)
{
    if (!event || (event->type != LEONOS_GUI_APP_EVENT_KEY_DOWN &&
                   event->type != LEONOS_GUI_APP_EVENT_KEY_UP)) {
        return;
    }
    (void)leonos_inputm_observe_gui_key(event->window_id, &event->keycode,
                                         event->pressed);
}

int leonos_gui_poll_app_event(struct leonos_gui_app_event *event)
{
    int ret;
    if (leonos_gui_inputm_commit_event(event)) {
        return 1;
    }
    ret = ioctl(3, LEONOS_GUI_IOCTL_WINDOW_EVENT, event);
    if (ret > 0 && event && event->type == LEONOS_GUI_APP_EVENT_THEME_CHANGED) {
        (void)leonos_ui_theme_set_appearance((uint32_t)event->x,
                                             (uint32_t)event->y,
                                             (uint32_t)event->dx);
        event->type = LEONOS_GUI_APP_EVENT_RESIZE;
    }
    if (ret > 0) {
        leonos_inputm_note_gui_window(event->window_id);
        leonos_gui_observe_inputm_key(event);
    }
    return ret;
}

int leonos_gui_wait_app_event(struct leonos_gui_app_event *event, uint32_t timeout_ms)
{
    struct leonos_gui_wait_app_event wait;
    int ret;
    if (!event) {
        return -1;
    }
    if (leonos_gui_inputm_commit_event(event)) {
        return 1;
    }
    wait.event = *event;
    wait.timeout_ms = timeout_ms;
    ret = ioctl(3, LEONOS_GUI_IOCTL_WAIT_WINDOW_EVENT, &wait);
    if (ret == 0 && leonos_gui_inputm_commit_event(event)) {
        return 1;
    }
    if (ret == 0) {
        ret = ioctl(3, LEONOS_GUI_IOCTL_WINDOW_EVENT, &wait.event);
    }
    if (ret > 0) {
        *event = wait.event;
        if (event->type == LEONOS_GUI_APP_EVENT_THEME_CHANGED) {
            (void)leonos_ui_theme_set_appearance((uint32_t)event->x,
                                                 (uint32_t)event->y,
                                                 (uint32_t)event->dx);
            event->type = LEONOS_GUI_APP_EVENT_RESIZE;
        }
        leonos_inputm_note_gui_window(event->window_id);
        leonos_gui_observe_inputm_key(event);
    }
    return ret;
}

int leonos_gui_send_app_event(const struct leonos_gui_app_event *event)
{
    return ioctl(3, LEONOS_GUI_IOCTL_SEND_WINDOW_EVENT, (void *)event);
}

int leonos_task_snapshot(struct leonos_task_info *tasks, uint32_t capacity, uint64_t *tick)
{
    struct leonos_task_snapshot snapshot = {
        .capacity = capacity,
        .count = 0,
        .tick = 0,
        .tasks = tasks,
    };
    int ret = ioctl(3, LEONOS_GUI_IOCTL_TASKS, &snapshot);
    if (tick) {
        *tick = snapshot.tick;
    }
    return ret < 0 ? ret : (int)snapshot.count;
}

int leonos_task_affinity_get(uint32_t pid, uint64_t *mask)
{
    struct leonos_task_affinity request = {
        .pid = pid,
        .operation = LEONOS_TASK_AFFINITY_GET,
    };
    int ret = ioctl(3, LEONOS_IOCTL_TASK_AFFINITY, &request);
    if (ret == 0 && mask) {
        *mask = request.mask;
    }
    return ret;
}

int leonos_task_affinity_set(uint32_t pid, uint64_t mask)
{
    struct leonos_task_affinity request = {
        .pid = pid,
        .operation = LEONOS_TASK_AFFINITY_SET,
        .mask = mask,
    };
    return ioctl(3, LEONOS_IOCTL_TASK_AFFINITY, &request);
}

int leonos_task_kill(uint32_t pid)
{
    return ioctl(3, LEONOS_GUI_IOCTL_TASK_KILL, (void *)(uintptr_t)pid);
}

int leonos_display_get_state(struct leonos_display_state *state)
{
    if (!state) {
        return -1;
    }
    return ioctl(3, LEONOS_GUI_IOCTL_DISPLAY_STATE, state);
}

int leonos_display_request(const struct leonos_display_request *request)
{
    if (!request) {
        return -1;
    }
    return ioctl(3, LEONOS_GUI_IOCTL_DISPLAY_REQUEST, (void *)request);
}

int leonos_display_poll_request(struct leonos_display_request *request)
{
    if (!request) {
        return -1;
    }
    return ioctl(3, LEONOS_GUI_IOCTL_POLL_DISPLAY_REQUEST, request);
}

int leonos_display_publish_state(const struct leonos_display_state *state)
{
    if (!state) {
        return -1;
    }
    return ioctl(3, LEONOS_GUI_IOCTL_PUBLISH_DISPLAY_STATE, (void *)state);
}

int leonos_appearance_get_state(struct leonos_appearance_state *state)
{
    if (!state) {
        return -1;
    }
    return ioctl(3, LEONOS_GUI_IOCTL_APPEARANCE_STATE, state);
}

int leonos_appearance_request_theme(const struct leonos_appearance_request *request)
{
    if (!request) {
        return -1;
    }
    return ioctl(3, LEONOS_GUI_IOCTL_APPEARANCE_REQUEST, (void *)request);
}

int leonos_appearance_poll_request(struct leonos_appearance_request *request)
{
    if (!request) {
        return -1;
    }
    return ioctl(3, LEONOS_GUI_IOCTL_POLL_APPEARANCE_REQUEST, request);
}

int leonos_appearance_publish_state(const struct leonos_appearance_state *state)
{
    if (!state) {
        return -1;
    }
    return ioctl(3, LEONOS_GUI_IOCTL_PUBLISH_APPEARANCE_STATE, (void *)state);
}

int leonos_list_dir(const char *path, struct leonos_dir_entry *entries,
                    uint32_t capacity, uint32_t *out_count)
{
    uint32_t count = 0;
    int fd;
    if (out_count) {
        *out_count = 0;
    }
    if (!path || (capacity && !entries)) {
        return -1;
    }
    if (capacity > LEONOS_FS_MAX_ENTRIES) {
        capacity = LEONOS_FS_MAX_ENTRIES;
    }
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return fd;
    }
    while (count < capacity) {
        /* Directory reads already return one fixed-size entry per syscall. */
        long got = syscall3(SYS_read, fd, (long)&entries[count],
                            sizeof(entries[count]));
        if (got < 0) {
            (void)close(fd);
            return (int)got;
        }
        if (got == 0) {
            break;
        }
        if (got != (long)sizeof(entries[count])) {
            (void)close(fd);
            return -1;
        }
        ++count;
    }
    if (close(fd) < 0) {
        return -1;
    }
    if (out_count) {
        *out_count = count;
    }
    return 0;
}

static int leonos_fs_acl_ioctl(unsigned long request, const char *path,
                               const struct leonos_fs_acl *in_acl,
                               struct leonos_fs_acl *out_acl)
{
    struct leonos_fs_acl_request query;
    uint32_t i = 0;
    query = (struct leonos_fs_acl_request){0};
    while (path && path[i] && i + 1 < sizeof(query.path)) {
        query.path[i] = path[i];
        ++i;
    }
    query.path[i] = 0;
    if (in_acl) {
        query.acl = *in_acl;
    }
    int ret = ioctl(3, request, &query);
    if (ret == 0 && out_acl) {
        *out_acl = query.acl;
    }
    return ret;
}

int leonos_fs_acl_get(const char *path, struct leonos_fs_acl *acl)
{
    if (!path || !acl) {
        return -1;
    }
    return leonos_fs_acl_ioctl(LEONOS_FS_IOCTL_ACL_GET, path, 0, acl);
}

int leonos_fs_acl_set(const char *path, const struct leonos_fs_acl *acl)
{
    if (!path || !acl) {
        return -1;
    }
    return leonos_fs_acl_ioctl(LEONOS_FS_IOCTL_ACL_SET, path, acl, 0);
}

int leonos_fs_acl_take_ownership(const char *path, struct leonos_fs_acl *acl)
{
    if (!path) {
        return -1;
    }
    return leonos_fs_acl_ioctl(LEONOS_FS_IOCTL_ACL_TAKE_OWNERSHIP, path, 0, acl);
}

int leonos_fs_acl_repair(const char *path, struct leonos_fs_acl *acl)
{
    if (!path) {
        return -1;
    }
    return leonos_fs_acl_ioctl(LEONOS_FS_IOCTL_ACL_REPAIR, path, 0, acl);
}

int leonos_text_layout_utf8(const char *text, uint32_t byte_len,
                            struct leonos_text_glyph *glyphs,
                            uint32_t capacity,
                            struct leonos_text_layout *out_layout)
{
    struct leonos_text_layout query = {
        .text = text,
        .byte_len = byte_len,
        .capacity = capacity,
        .count = 0,
        .total_cells = 0,
        .total_px = 0,
        .glyphs = glyphs,
    };
    int ret = ioctl(3, LEONOS_TEXT_IOCTL_LAYOUT_UTF8, &query);
    if (out_layout) {
        *out_layout = query;
    }
    return ret;
}

int leonos_device_list(struct leonos_device_info *devices,
                       uint32_t capacity, uint32_t *out_count)
{
    struct leonos_device_list query = {
        .capacity = capacity,
        .count = 0,
        .devices = devices,
    };
    int ret = ioctl(3, LEONOS_IOCTL_DEVICE_LIST, &query);
    if (out_count) {
        *out_count = query.count;
    }
    return ret;
}

int leonos_driver_list(struct leonos_driver_info *drivers, uint32_t capacity,
                       uint32_t *out_count)
{
    struct leonos_driver_list query = {
        .capacity = capacity,
        .count = 0,
        .drivers = drivers,
    };
    int ret = ioctl(3, LEONOS_IOCTL_DRIVER_LIST, &query);
    if (out_count) {
        *out_count = query.count;
    }
    return ret;
}

int leonos_driver_control(uint32_t action, const char *file)
{
    struct leonos_driver_control request = {
        .action = action,
        .flags = 0,
        .status = -1,
        .reserved = 0,
        .file = {0},
    };
    uint32_t index = 0;
    while (file && file[index] && index + 1U < sizeof(request.file)) {
        request.file[index] = file[index];
        ++index;
    }
    request.file[index] = 0;
    if (action != LEONOS_DRIVER_CONTROL_RESCAN && !request.file[0]) {
        return -1;
    }
    if (ioctl(3, LEONOS_IOCTL_DRIVER_CONTROL, &request) < 0) {
        return request.status < 0 ? request.status : -1;
    }
    return request.status;
}

int leonos_audio_configure(const struct leonos_audio_format *format)
{
    static int audio_fd = -1;
    if (!format) {
        return -1;
    }
    if (audio_fd < 0) {
        audio_fd = open(LEONOS_DEV_AUDIO0, LEONOS_O_RDWR, 0);
        if (audio_fd < 0) audio_fd = 3;
    }
    return ioctl(audio_fd, LEONOS_IOCTL_AUDIO_CONFIGURE, (void *)format);
}

long leonos_audio_write(const void *data, uint32_t length,
                        uint32_t *out_status)
{
    static int audio_fd = -1;
    uint32_t done = 0;
    uint32_t status = LEONOS_AUDIO_STATUS_OK;
    if (length && !data) {
        return -1;
    }
    if (audio_fd < 0) {
        audio_fd = open(LEONOS_DEV_AUDIO0, LEONOS_O_RDWR, 0);
        if (audio_fd < 0) audio_fd = 3;
    }
    while (done < length) {
        uint32_t chunk = length - done;
        struct leonos_audio_write request;
        int ret;
        if (chunk > LEONOS_AUDIO_IO_SLICE_BYTES) {
            chunk = LEONOS_AUDIO_IO_SLICE_BYTES;
        }
        request = (struct leonos_audio_write){
            .data = (const uint8_t *)data + done,
            .length = chunk,
            .transferred = 0,
            .status = LEONOS_AUDIO_STATUS_PLAYBACK_FAILED,
        };
        ret = ioctl(audio_fd, LEONOS_IOCTL_AUDIO_WRITE, &request);
        status = request.status;
        if (ret < 0) {
            if (out_status) {
                *out_status = status;
            }
            return done ? (long)done : ret;
        }
        if (request.transferred == 0 &&
            request.status == LEONOS_AUDIO_STATUS_WOULD_BLOCK) {
            break;
        }
        if (request.transferred == 0 || request.transferred > chunk) {
            if (out_status) {
                *out_status = LEONOS_AUDIO_STATUS_PLAYBACK_FAILED;
            }
            return done ? (long)done : -1;
        }
        done += request.transferred;
        if (request.transferred < chunk) {
            break;
        }
    }
    if (out_status) {
        *out_status = status;
    }
    return (long)done;
}

int leonos_audio_get_state(struct leonos_audio_state *state)
{
    static int audio_fd = -1;
    if (!state) {
        return -1;
    }
    if (audio_fd < 0) {
        audio_fd = open(LEONOS_DEV_AUDIO0, LEONOS_O_RDWR, 0);
        if (audio_fd < 0) audio_fd = 3;
    }
    return ioctl(audio_fd, LEONOS_IOCTL_AUDIO_GET_STATE, state);
}

int leonos_net_config(struct leonos_net_config *config)
{
    if (!config) {
        return -1;
    }
    return ioctl(3, LEONOS_IOCTL_NET_CONFIG, config);
}

int leonos_net_get_dns_policy(struct leonos_net_dns_policy *result)
{
    struct leonos_net_dns_policy query = {
        .mode = LEONOS_NET_DNS_MODE_QUERY,
        .status = LEONOS_NET_STATUS_BAD_ARGUMENT,
    };
    int ret;
    if (!result) {
        return -1;
    }
    ret = ioctl(3, LEONOS_IOCTL_NET_DNS_POLICY, &query);
    *result = query;
    return ret;
}

int leonos_net_set_dns_policy(uint32_t mode, uint32_t custom_dns_ip,
                              struct leonos_net_dns_policy *result)
{
    struct leonos_net_dns_policy query = {
        .mode = mode,
        .custom_dns_ip = custom_dns_ip,
        .status = LEONOS_NET_STATUS_BAD_ARGUMENT,
    };
    int ret;
    if (!result) {
        return -1;
    }
    ret = ioctl(3, LEONOS_IOCTL_NET_DNS_POLICY, &query);
    *result = query;
    return ret;
}

int leonos_net_dhcp_renew(uint32_t timeout_ms, struct leonos_net_dhcp *result)
{
    struct leonos_net_dhcp query = {
        .timeout_ms = timeout_ms,
        .status = LEONOS_NET_STATUS_DHCP_FAILED,
    };
    int ret;
    if (!result) {
        return -1;
    }
    ret = ioctl(3, LEONOS_IOCTL_NET_DHCP, &query);
    *result = query;
    return ret;
}

int leonos_net_ping(uint32_t target_ip, uint32_t timeout_ms,
                    struct leonos_net_ping *result)
{
    struct leonos_net_ping query = {
        .target_ip = target_ip,
        .timeout_ms = timeout_ms,
        .sequence = 0,
        .status = LEONOS_NET_STATUS_BAD_ARGUMENT,
    };
    int ret;
    if (!result) {
        return -1;
    }
    ret = ioctl(3, LEONOS_IOCTL_NET_PING, &query);
    *result = query;
    return ret;
}

int leonos_net_dns_resolve(const char *name, uint32_t timeout_ms,
                           struct leonos_net_dns *result)
{
    struct leonos_net_dns query;
    uint32_t i = 0;
    int ret;
    if (!name || !result) {
        return -1;
    }
    query = (struct leonos_net_dns){0};
    query.timeout_ms = timeout_ms;
    query.status = LEONOS_NET_STATUS_DNS_FAILED;
    while (name[i] && i + 1 < sizeof(query.name)) {
        query.name[i] = name[i];
        ++i;
    }
    query.name[i] = 0;
    ret = ioctl(3, LEONOS_IOCTL_NET_DNS, &query);
    *result = query;
    return ret;
}

int leonos_net_http_get(const char *host, const char *path,
                        uint32_t port, uint32_t timeout_ms,
                        struct leonos_net_http_get *result)
{
    struct leonos_net_http_get query;
    uint32_t i = 0;
    int ret;
    if (!host || !result) {
        return -1;
    }
    query = (struct leonos_net_http_get){0};
    query.port = port;
    query.timeout_ms = timeout_ms;
    query.status = LEONOS_NET_STATUS_HTTP_FAILED;
    while (host[i] && i + 1 < sizeof(query.host)) {
        query.host[i] = host[i];
        ++i;
    }
    query.host[i] = 0;
    i = 0;
    while (path && path[i] && i + 1 < sizeof(query.path)) {
        query.path[i] = path[i];
        ++i;
    }
    query.path[i] = 0;
    ret = ioctl(3, LEONOS_IOCTL_NET_HTTP_GET, &query);
    *result = query;
    return ret;
}

int leonos_socket_tcp(void)
{
    struct leonos_net_socket_open query = {
        .domain = LEONOS_NET_AF_INET,
        .type = LEONOS_NET_SOCK_STREAM,
        .protocol = LEONOS_NET_IPPROTO_TCP,
        .timeout_ms = 0,
        .status = LEONOS_NET_STATUS_TCP_FAILED,
        .socket = -1,
    };
    int ret = ioctl(3, LEONOS_IOCTL_NET_SOCKET_OPEN, &query);
    if (ret < 0) {
        return ret;
    }
    return query.status == LEONOS_NET_STATUS_OK ? query.socket : -1;
}

int leonos_socket_connect(int socket, const char *host,
                          uint32_t port, uint32_t timeout_ms,
                          struct leonos_net_socket_connect *result)
{
    struct leonos_net_socket_connect query;
    uint32_t i = 0;
    int ret;
    if (!host || !result) {
        return -1;
    }
    query = (struct leonos_net_socket_connect){0};
    query.socket = socket;
    query.port = port;
    query.timeout_ms = timeout_ms;
    query.status = LEONOS_NET_STATUS_TCP_FAILED;
    while (host[i] && i + 1 < sizeof(query.host)) {
        query.host[i] = host[i];
        ++i;
    }
    query.host[i] = 0;
    ret = ioctl(3, LEONOS_IOCTL_NET_SOCKET_CONNECT, &query);
    *result = query;
    return ret;
}

long leonos_socket_send(int socket, const void *buffer, uint32_t length,
                        uint32_t timeout_ms, uint32_t *status)
{
    struct leonos_net_socket_io query = {
        .socket = socket,
        .buffer = (void *)buffer,
        .length = length,
        .timeout_ms = timeout_ms,
        .status = LEONOS_NET_STATUS_TCP_FAILED,
        .transferred = 0,
    };
    int ret;
    if (length && !buffer) {
        if (status) {
            *status = LEONOS_NET_STATUS_BAD_ARGUMENT;
        }
        return -1;
    }
    ret = ioctl(3, LEONOS_IOCTL_NET_SOCKET_SEND, &query);
    if (status) {
        *status = query.status;
    }
    return ret < 0 ? ret : (long)query.transferred;
}

long leonos_socket_recv(int socket, void *buffer, uint32_t length,
                        uint32_t timeout_ms, uint32_t *status)
{
    struct leonos_net_socket_io query = {
        .socket = socket,
        .buffer = buffer,
        .length = length,
        .timeout_ms = timeout_ms,
        .status = LEONOS_NET_STATUS_TCP_FAILED,
        .transferred = 0,
    };
    int ret;
    if (length && !buffer) {
        if (status) {
            *status = LEONOS_NET_STATUS_BAD_ARGUMENT;
        }
        return -1;
    }
    ret = ioctl(3, LEONOS_IOCTL_NET_SOCKET_RECV, &query);
    if (status) {
        *status = query.status;
    }
    return ret < 0 ? ret : (long)query.transferred;
}

int leonos_socket_close(int socket)
{
    struct leonos_net_socket_close query = {
        .socket = socket,
        .status = LEONOS_NET_STATUS_SOCKET_CLOSED,
    };
    int ret = ioctl(3, LEONOS_IOCTL_NET_SOCKET_CLOSE, &query);
    if (ret < 0) {
        return ret;
    }
    return query.status == LEONOS_NET_STATUS_OK ? 0 : -1;
}

int leonos_net_connections(struct leonos_net_connection_info *entries,
                           uint32_t capacity, uint32_t *out_count)
{
    struct leonos_net_connection_list query = {
        .capacity = capacity,
        .count = 0,
        .entries = entries,
    };
    int ret = ioctl(3, LEONOS_IOCTL_NET_CONNECTIONS, &query);
    if (out_count) {
        *out_count = query.count;
    }
    return ret;
}

#define HTTP_REQUEST_MAX 1024U

struct libc_http_url {
    char host[LEONOS_NET_HOSTNAME_LEN];
    char path[LEONOS_NET_HTTP_PATH_LEN];
    uint32_t port;
    uint8_t secure;
};

static char http_tolower(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

static int http_starts_with_ignore_case(const char *text, const char *prefix)
{
    uint32_t i = 0;
    if (!text || !prefix) {
        return 0;
    }
    while (prefix[i]) {
        if (http_tolower(text[i]) != http_tolower(prefix[i])) {
            return 0;
        }
        ++i;
    }
    return 1;
}

static int http_text_eq_ignore_case_n(const char *a, const char *b,
                                      uint32_t len)
{
    for (uint32_t i = 0; i < len; ++i) {
        if (http_tolower(a[i]) != http_tolower(b[i])) {
            return 0;
        }
    }
    return 1;
}

static int http_is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static int http_is_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

static int http_is_hex(char ch)
{
    return (ch >= '0' && ch <= '9') ||
           (ch >= 'a' && ch <= 'f') ||
           (ch >= 'A' && ch <= 'F');
}

static uint32_t http_hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return (uint32_t)(ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
        return (uint32_t)(ch - 'a' + 10);
    }
    if (ch >= 'A' && ch <= 'F') {
        return (uint32_t)(ch - 'A' + 10);
    }
    return 0;
}

static void http_copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[i] && i + 1U < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void http_copy_bytes(char *dst, uint32_t cap,
                            const char *src, uint32_t len)
{
    uint32_t n = len;
    if (!dst || cap == 0) {
        return;
    }
    if (n + 1U > cap) {
        n = cap - 1U;
    }
    for (uint32_t i = 0; i < n; ++i) {
        dst[i] = src ? src[i] : 0;
    }
    dst[n] = 0;
}

static void http_append_char(char *dst, uint32_t *pos, uint32_t cap, char ch)
{
    if (dst && pos && *pos + 1U < cap) {
        dst[*pos] = ch;
        ++(*pos);
        dst[*pos] = 0;
    }
}

static void http_append_text(char *dst, uint32_t *pos, uint32_t cap,
                             const char *src)
{
    while (src && *src) {
        http_append_char(dst, pos, cap, *src++);
    }
}

static void http_append_u32(char *dst, uint32_t *pos, uint32_t cap,
                            uint32_t value)
{
    char tmp[12];
    uint32_t n = 0;
    if (value == 0) {
        http_append_char(dst, pos, cap, '0');
        return;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (n) {
        http_append_char(dst, pos, cap, tmp[--n]);
    }
}

static int http_parse_url(const char *url, struct libc_http_url *out)
{
    const char *p;
    uint32_t host_pos = 0;
    uint32_t path_pos = 0;
    uint32_t port;
    if (!url || !out) {
        return 0;
    }
    if (http_starts_with_ignore_case(url, "https://")) {
        out->secure = 1;
        port = 443;
        p = url + 8;
    } else if (http_starts_with_ignore_case(url, "http://")) {
        out->secure = 0;
        port = 80;
        p = url + 7;
    } else {
        return 0;
    }
    while (*p && *p != '/' && *p != ':' && *p != '#' && *p != '?' &&
           host_pos + 1U < sizeof(out->host)) {
        out->host[host_pos++] = *p++;
    }
    out->host[host_pos] = 0;
    if (!out->host[0]) {
        return 0;
    }
    if (*p == ':') {
        port = 0;
        ++p;
        while (http_is_digit(*p)) {
            port = port * 10U + (uint32_t)(*p - '0');
            if (port > 65535U) {
                return 0;
            }
            ++p;
        }
        if (port == 0) {
            return 0;
        }
    }
    if (*p == '/') {
        while (*p && *p != '#' && path_pos + 1U < sizeof(out->path)) {
            out->path[path_pos++] = *p++;
        }
    } else if (*p == '?') {
        out->path[path_pos++] = '/';
        while (*p && *p != '#' && path_pos + 1U < sizeof(out->path)) {
            out->path[path_pos++] = *p++;
        }
    }
    if (path_pos == 0) {
        out->path[path_pos++] = '/';
    }
    out->path[path_pos] = 0;
    out->port = port;
    return 1;
}

static void http_build_url(char *dst, uint32_t cap, const char *host,
                           uint32_t port, uint8_t secure, const char *path)
{
    uint32_t pos = 0;
    if (!dst || cap == 0) {
        return;
    }
    dst[0] = 0;
    http_append_text(dst, &pos, cap, secure ? "https://" : "http://");
    http_append_text(dst, &pos, cap, host);
    if (port != (secure ? 443U : 80U)) {
        http_append_char(dst, &pos, cap, ':');
        http_append_u32(dst, &pos, cap, port);
    }
    if (!path || path[0] != '/') {
        http_append_char(dst, &pos, cap, '/');
    }
    http_append_text(dst, &pos, cap, path && path[0] ? path : "/");
}

static void http_parent_path(const char *path, char *dst, uint32_t cap)
{
    uint32_t last_slash = 0;
    uint32_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    if (!path || !path[0]) {
        http_copy_text(dst, cap, "/");
        return;
    }
    while (path[i] && path[i] != '?' && path[i] != '#') {
        if (path[i] == '/') {
            last_slash = i;
        }
        ++i;
    }
    if (last_slash == 0) {
        http_copy_text(dst, cap, "/");
        return;
    }
    http_copy_bytes(dst, cap, path, last_slash + 1U);
}

int leonos_http_resolve_url(const char *base_url, const char *location,
                            char *out, uint32_t capacity)
{
    char base_copy[LEONOS_HTTP_URL_LEN];
    char location_copy[LEONOS_HTTP_URL_LEN];
    const char *base_text = base_url;
    const char *location_text = location;
    struct libc_http_url base;
    char dir[LEONOS_NET_HTTP_PATH_LEN];
    uint32_t pos = 0;
    if (!out || capacity == 0) {
        return -1;
    }
    if (base_url == out && base_url) {
        http_copy_text(base_copy, sizeof(base_copy), base_url);
        base_text = base_copy;
    }
    if (location == out && location) {
        http_copy_text(location_copy, sizeof(location_copy), location);
        location_text = location_copy;
    }
    out[0] = 0;
    if (!location_text || !location_text[0]) {
        http_copy_text(out, capacity, base_text);
        return 0;
    }
    if (http_starts_with_ignore_case(location_text, "http://") ||
        http_starts_with_ignore_case(location_text, "https://")) {
        http_copy_text(out, capacity, location_text);
        return 0;
    }
    if (location_text[0] == '/' && location_text[1] == '/') {
        http_append_text(out, &pos, capacity, base_text &&
                         http_starts_with_ignore_case(base_text, "https://")
                             ? "https:"
                             : "http:");
        http_append_text(out, &pos, capacity, location_text);
        return 0;
    }
    if (!http_parse_url(base_text, &base)) {
        http_copy_text(out, capacity, location_text);
        return 0;
    }
    if (location_text[0] == '#') {
        http_copy_text(out, capacity, base_text);
        return 0;
    }
    if (location_text[0] == '/') {
        http_build_url(out, capacity, base.host, base.port, base.secure,
                       location_text);
        return 0;
    }
    http_parent_path(base.path, dir, sizeof(dir));
    out[0] = 0;
    http_append_text(out, &pos, capacity,
                     base.secure ? "https://" : "http://");
    http_append_text(out, &pos, capacity, base.host);
    if (base.port != (base.secure ? 443U : 80U)) {
        http_append_char(out, &pos, capacity, ':');
        http_append_u32(out, &pos, capacity, base.port);
    }
    http_append_text(out, &pos, capacity, dir);
    http_append_text(out, &pos, capacity, location_text);
    return 0;
}

static uint32_t http_find_body_offset(const char *data, uint32_t len)
{
    for (uint32_t i = 0; i + 3U < len; ++i) {
        if (data[i] == '\r' && data[i + 1U] == '\n' &&
            data[i + 2U] == '\r' && data[i + 3U] == '\n') {
            return i + 4U;
        }
    }
    for (uint32_t i = 0; i + 1U < len; ++i) {
        if (data[i] == '\n' && data[i + 1U] == '\n') {
            return i + 2U;
        }
    }
    return 0;
}

static uint32_t http_parse_status_code(const char *headers, uint32_t len)
{
    uint32_t pos = 0;
    uint32_t status = 0;
    if (!headers || len < 12U ||
        headers[0] != 'H' || headers[1] != 'T' ||
        headers[2] != 'T' || headers[3] != 'P' ||
        headers[4] != '/') {
        return 0;
    }
    while (pos < len && headers[pos] != ' ' &&
           headers[pos] != '\r' && headers[pos] != '\n') {
        ++pos;
    }
    while (pos < len && headers[pos] == ' ') {
        ++pos;
    }
    for (uint32_t i = 0; i < 3U && pos < len; ++i, ++pos) {
        if (!http_is_digit(headers[pos])) {
            return 0;
        }
        status = status * 10U + (uint32_t)(headers[pos] - '0');
    }
    return status;
}

static int http_header_value(const char *headers, uint32_t header_len,
                             const char *name, char *out, uint32_t cap)
{
    uint32_t name_len = (uint32_t)strlen(name);
    uint32_t i = 0;
    if (out && cap) {
        out[0] = 0;
    }
    if (!headers || !name || !name[0]) {
        return 0;
    }
    while (i < header_len && headers[i] != '\n') {
        ++i;
    }
    if (i < header_len) {
        ++i;
    }
    while (i < header_len) {
        uint32_t line_start = i;
        uint32_t line_end;
        uint32_t colon = i;
        uint32_t value_start;
        uint32_t value_end;
        while (i < header_len && headers[i] != '\n' && headers[i] != '\r') {
            ++i;
        }
        line_end = i;
        while (i < header_len && (headers[i] == '\r' || headers[i] == '\n')) {
            ++i;
        }
        if (line_end == line_start) {
            break;
        }
        while (colon < line_end && headers[colon] != ':') {
            ++colon;
        }
        if (colon == line_end || colon - line_start != name_len) {
            continue;
        }
        if (!http_text_eq_ignore_case_n(headers + line_start, name, name_len)) {
            continue;
        }
        value_start = colon + 1U;
        while (value_start < line_end && http_is_space(headers[value_start])) {
            ++value_start;
        }
        value_end = line_end;
        while (value_end > value_start && http_is_space(headers[value_end - 1U])) {
            --value_end;
        }
        http_copy_bytes(out, cap, headers + value_start, value_end - value_start);
        return 1;
    }
    return 0;
}

static int http_contains_ignore_case(const char *text, const char *needle)
{
    uint32_t needle_len;
    uint32_t text_len;
    if (!text || !needle) {
        return 0;
    }
    needle_len = (uint32_t)strlen(needle);
    text_len = (uint32_t)strlen(text);
    if (needle_len == 0 || needle_len > text_len) {
        return 0;
    }
    for (uint32_t i = 0; i + needle_len <= text_len; ++i) {
        if (http_text_eq_ignore_case_n(text + i, needle, needle_len)) {
            return 1;
        }
    }
    return 0;
}

static uint32_t http_parse_decimal(const char *text, int *ok)
{
    uint32_t value = 0;
    uint32_t i = 0;
    if (ok) {
        *ok = 0;
    }
    while (text && http_is_space(text[i])) {
        ++i;
    }
    if (!text || !http_is_digit(text[i])) {
        return 0;
    }
    while (http_is_digit(text[i])) {
        value = value * 10U + (uint32_t)(text[i] - '0');
        ++i;
    }
    if (ok) {
        *ok = 1;
    }
    return value;
}

static uint32_t http_decode_chunked(char *buffer, uint32_t body_offset,
                                    uint32_t raw_len, uint32_t capacity,
                                    uint32_t *flags)
{
    uint32_t src = body_offset;
    uint32_t dst = 0;
    while (src < raw_len) {
        uint32_t chunk_size = 0;
        uint32_t saw_hex = 0;
        while (src < raw_len && (buffer[src] == '\r' || buffer[src] == '\n')) {
            ++src;
        }
        while (src < raw_len && http_is_hex(buffer[src])) {
            chunk_size = chunk_size * 16U + http_hex_value(buffer[src]);
            ++src;
            saw_hex = 1;
        }
        if (!saw_hex) {
            break;
        }
        while (src < raw_len && buffer[src] != '\n') {
            ++src;
        }
        if (src < raw_len && buffer[src] == '\n') {
            ++src;
        }
        if (chunk_size == 0) {
            break;
        }
        if (src + chunk_size > raw_len) {
            chunk_size = raw_len > src ? raw_len - src : 0;
            if (flags) {
                *flags |= LEONOS_HTTP_FLAG_TRUNCATED;
            }
        }
        for (uint32_t i = 0; i < chunk_size; ++i) {
            if (dst + 1U >= capacity) {
                if (flags) {
                    *flags |= LEONOS_HTTP_FLAG_TRUNCATED;
                }
                break;
            }
            buffer[dst++] = buffer[src + i];
        }
        src += chunk_size;
    }
    if (capacity) {
        buffer[dst < capacity ? dst : capacity - 1U] = 0;
    }
    return dst;
}

static uint32_t http_copy_body(char *buffer, uint32_t body_offset,
                               uint32_t raw_len, uint32_t capacity,
                               uint32_t wanted_len, uint32_t *flags)
{
    uint32_t available = body_offset < raw_len ? raw_len - body_offset : 0;
    uint32_t copy_len = available;
    if (wanted_len && wanted_len < copy_len) {
        copy_len = wanted_len;
    }
    if (wanted_len && available < wanted_len && flags) {
        *flags |= LEONOS_HTTP_FLAG_TRUNCATED;
    }
    if (copy_len + 1U > capacity) {
        copy_len = capacity ? capacity - 1U : 0;
        if (flags) {
            *flags |= LEONOS_HTTP_FLAG_TRUNCATED;
        }
    }
    for (uint32_t i = 0; i < copy_len; ++i) {
        buffer[i] = buffer[body_offset + i];
    }
    if (capacity) {
        buffer[copy_len] = 0;
    }
    return copy_len;
}

static int http_extra_header_present(const char *headers, const char *name)
{
    uint32_t name_len;
    uint32_t i = 0;
    if (!headers || !name || !name[0]) {
        return 0;
    }
    name_len = (uint32_t)strlen(name);
    while (headers[i]) {
        uint32_t line_start = i;
        uint32_t line_end;
        uint32_t colon;
        while (headers[i] && headers[i] != '\r' && headers[i] != '\n') {
            ++i;
        }
        line_end = i;
        colon = line_start;
        while (colon < line_end && headers[colon] != ':') {
            ++colon;
        }
        if (colon < line_end && colon - line_start == name_len &&
            http_text_eq_ignore_case_n(headers + line_start, name, name_len)) {
            return 1;
        }
        while (headers[i] == '\r' || headers[i] == '\n') {
            ++i;
        }
    }
    return 0;
}

static uint32_t http_build_request_text(char *dst, uint32_t cap,
                                        const struct libc_http_url *url,
                                        const struct leonos_http_request *request)
{
    const char *method = request->method && request->method[0]
                             ? request->method
                             : "GET";
    uint32_t pos = 0;
    if (!dst || !cap || !url) {
        return 0;
    }
    dst[0] = 0;
    http_append_text(dst, &pos, cap, method);
    http_append_char(dst, &pos, cap, ' ');
    http_append_text(dst, &pos, cap, url->path[0] ? url->path : "/");
    http_append_text(dst, &pos, cap, " HTTP/1.1\r\nHost: ");
    http_append_text(dst, &pos, cap, url->host);
    if (url->port != (url->secure ? 443U : 80U)) {
        http_append_char(dst, &pos, cap, ':');
        http_append_u32(dst, &pos, cap, url->port);
    }
    http_append_text(dst, &pos, cap, "\r\n");
    if (!http_extra_header_present(request->extra_headers, "User-Agent")) {
        http_append_text(dst, &pos, cap, "User-Agent: LeonOS/4\r\n");
    }
    if (!http_extra_header_present(request->extra_headers, "Accept")) {
        http_append_text(dst, &pos, cap, "Accept: */*\r\n");
    }
    if (!http_extra_header_present(request->extra_headers, "Accept-Encoding")) {
        http_append_text(dst, &pos, cap, "Accept-Encoding: identity\r\n");
    }
    if (!http_extra_header_present(request->extra_headers, "Connection")) {
        http_append_text(dst, &pos, cap, "Connection: close\r\n");
    }
    if (request->request_body && request->request_body_len) {
        http_append_text(dst, &pos, cap, "Content-Length: ");
        http_append_u32(dst, &pos, cap, request->request_body_len);
        http_append_text(dst, &pos, cap, "\r\n");
    }
    if (request->extra_headers && request->extra_headers[0]) {
        http_append_text(dst, &pos, cap, request->extra_headers);
        if (pos < 2U || dst[pos - 1U] != '\n') {
            http_append_text(dst, &pos, cap, "\r\n");
        }
    }
    http_append_text(dst, &pos, cap, "\r\n");
    return pos + 1U < cap ? pos : 0;
}

static int http_fetch_once(const char *url_text,
                           const struct leonos_http_request *request,
                           struct leonos_http_response *response,
                           char *location, uint32_t location_cap)
{
    struct libc_http_url url;
    struct leonos_net_socket_connect conn;
    char request_text[HTTP_REQUEST_MAX];
    char transfer_encoding[48];
    char content_length_text[32];
    uint32_t request_len;
    uint32_t net_status = LEONOS_NET_STATUS_HTTP_FAILED;
    uint32_t raw_len = 0;
    uint32_t body_offset;
    uint32_t header_len;
    uint32_t timeout_ms = request->timeout_ms ? request->timeout_ms
                                              : LEONOS_HTTP_DEFAULT_TIMEOUT_MS;
    int socket;
    int ret;

    if (location && location_cap) {
        location[0] = 0;
    }
    if (!http_starts_with_ignore_case(url_text, "http://") &&
        !http_starts_with_ignore_case(url_text, "https://")) {
        printf("[http] request rejected unsupported scheme\n");
        response->net_status = LEONOS_NET_STATUS_PROTOCOL_UNSUPPORTED;
        return 0;
    }
    if (!http_parse_url(url_text, &url)) {
        printf("[http] request rejected invalid url\n");
        response->net_status = LEONOS_NET_STATUS_BAD_ARGUMENT;
        return 0;
    }
    printf("[http] request host=%s port=%u secure=%u timeout=%u\n", url.host,
           url.port, url.secure, timeout_ms);
    request_len = http_build_request_text(request_text, sizeof(request_text),
                                          &url, request);
    if (!request_len) {
        printf("[http] request build failed host=%s\n", url.host);
        response->net_status = LEONOS_NET_STATUS_BAD_ARGUMENT;
        return 0;
    }
    socket = leonos_socket_tcp();
    if (socket < 0) {
        printf("[http] socket open failed host=%s\n", url.host);
        response->net_status = LEONOS_NET_STATUS_SOCKET_LIMIT;
        return 0;
    }
    ret = leonos_socket_connect(socket, url.host, url.port, timeout_ms, &conn);
    if (ret < 0 || conn.status != LEONOS_NET_STATUS_OK) {
        printf("[http] connect failed host=%s ret=%d status=%u\n", url.host,
               ret, conn.status);
        leonos_socket_close(socket);
        response->net_status = ret < 0 ? LEONOS_NET_STATUS_TCP_FAILED
                                       : conn.status;
        return 0;
    }
    printf("[http] connected host=%s socket=%d remote_ip=%u\n", url.host,
           socket, conn.remote_ip);
    if (url.secure) {
        if (leonos_tls_http_exchange(socket, url.host, timeout_ms,
                                     request_text, request_len,
                                     request->request_body,
                                     request->request_body_len,
                                     request->response_body,
                                     request->response_body_capacity,
                                     &raw_len) < 0) {
            printf("[http] TLS exchange failed host=%s raw=%u\n", url.host,
                   raw_len);
            leonos_socket_close(socket);
            response->net_status = LEONOS_NET_STATUS_TLS_FAILED;
            return 0;
        }
        net_status = LEONOS_NET_STATUS_OK;
        printf("[http] TLS exchange complete host=%s raw=%u\n", url.host,
               raw_len);
    } else {
        ret = (int)leonos_socket_send(socket, request_text, request_len,
                                      timeout_ms, &net_status);
        if (ret < 0 || net_status != LEONOS_NET_STATUS_OK ||
            (uint32_t)ret != request_len) {
            leonos_socket_close(socket);
            response->net_status = net_status;
            return 0;
        }
        if (request->request_body && request->request_body_len) {
            ret = (int)leonos_socket_send(socket, request->request_body,
                                          request->request_body_len,
                                          timeout_ms, &net_status);
            if (ret < 0 || net_status != LEONOS_NET_STATUS_OK ||
                (uint32_t)ret != request->request_body_len) {
                leonos_socket_close(socket);
                response->net_status = net_status;
                return 0;
            }
        }
        while (raw_len + 1U < request->response_body_capacity) {
            long got = leonos_socket_recv(socket,
                                          request->response_body + raw_len,
                                          request->response_body_capacity - raw_len - 1U,
                                          raw_len ? 1200U : timeout_ms,
                                          &net_status);
            if (got < 0) {
                printf("[http] response read failed host=%s ret=%ld status=%u raw=%u\n",
                       url.host, got, net_status, raw_len);
                leonos_socket_close(socket);
                response->net_status = LEONOS_NET_STATUS_TCP_FAILED;
                return 0;
            }
            if (got == 0) {
                if (net_status == LEONOS_NET_STATUS_OK ||
                    (net_status == LEONOS_NET_STATUS_TCP_TIMEOUT && raw_len)) {
                    net_status = LEONOS_NET_STATUS_OK;
                }
                break;
            }
            raw_len += (uint32_t)got;
        }
    }
    leonos_socket_close(socket);
    request->response_body[raw_len] = 0;
    response->net_status = net_status;
    if (raw_len + 1U >= request->response_body_capacity) {
        response->flags |= LEONOS_HTTP_FLAG_TRUNCATED;
    }
    if (net_status != LEONOS_NET_STATUS_OK) {
        printf("[http] response transport failed host=%s status=%u raw=%u\n",
               url.host, net_status, raw_len);
        return 0;
    }
    body_offset = http_find_body_offset(request->response_body, raw_len);
    if (!body_offset) {
        printf("[http] response missing header terminator host=%s raw=%u\n",
               url.host, raw_len);
        response->net_status = LEONOS_NET_STATUS_HTTP_FAILED;
        return 0;
    }
    header_len = body_offset;
    response->http_status = http_parse_status_code(request->response_body,
                                                   header_len);
    if (!response->http_status) {
        printf("[http] response status parse failed host=%s headers=%u raw=%u\n",
               url.host, header_len, raw_len);
        response->net_status = LEONOS_NET_STATUS_HTTP_FAILED;
        return 0;
    }
    response->headers_len = header_len;
    if (request->response_headers && request->response_headers_capacity) {
        if (header_len + 1U > request->response_headers_capacity) {
            response->headers_len = request->response_headers_capacity - 1U;
            response->flags |= LEONOS_HTTP_FLAG_TRUNCATED;
        }
        http_copy_bytes(request->response_headers,
                        request->response_headers_capacity,
                        request->response_body,
                        response->headers_len);
    }
    http_header_value(request->response_body, header_len, "Content-Type",
                      response->content_type, sizeof(response->content_type));
    http_header_value(request->response_body, header_len, "Location",
                      location, location_cap);
    transfer_encoding[0] = 0;
    http_header_value(request->response_body, header_len, "Transfer-Encoding",
                      transfer_encoding, sizeof(transfer_encoding));
    content_length_text[0] = 0;
    if (http_header_value(request->response_body, header_len, "Content-Length",
                          content_length_text, sizeof(content_length_text))) {
        int ok = 0;
        response->content_length = http_parse_decimal(content_length_text, &ok);
        if (ok) {
            response->flags |= LEONOS_HTTP_FLAG_CONTENT_LENGTH;
        }
    }
    if (http_contains_ignore_case(transfer_encoding, "chunked")) {
        response->flags |= LEONOS_HTTP_FLAG_CHUNKED;
        response->body_len = http_decode_chunked(request->response_body,
                                                body_offset, raw_len,
                                                request->response_body_capacity,
                                                &response->flags);
    } else {
        response->body_len = http_copy_body(request->response_body,
                                           body_offset, raw_len,
                                           request->response_body_capacity,
                                           response->content_length,
                                           &response->flags);
    }
    printf("[http] response host=%s status=%u headers=%u raw=%u body=%u length=%u flags=0x%x type=%s\n",
           url.host, response->http_status, response->headers_len, raw_len,
           response->body_len, response->content_length, response->flags,
           response->content_type[0] ? response->content_type : "(none)");
    return 0;
}

static int http_is_redirect(uint32_t status)
{
    return status == 301U || status == 302U || status == 303U ||
           status == 307U || status == 308U;
}

int leonos_http_request(const struct leonos_http_request *request,
                        struct leonos_http_response *response)
{
    char current_url[LEONOS_HTTP_URL_LEN];
    char location[LEONOS_HTTP_URL_LEN];
    char next_url[LEONOS_HTTP_URL_LEN];
    uint32_t max_redirects;
    if (!request || !response || !request->url ||
        !request->response_body || request->response_body_capacity == 0) {
        return -1;
    }
    *response = (struct leonos_http_response){0};
    max_redirects = request->max_redirects == LEONOS_HTTP_NO_REDIRECTS
                        ? 0
                        : request->max_redirects
                              ? request->max_redirects
                              : LEONOS_HTTP_DEFAULT_REDIRECTS;
    http_copy_text(current_url, sizeof(current_url), request->url);
    http_copy_text(response->final_url, sizeof(response->final_url),
                   current_url);
    for (;;) {
        location[0] = 0;
        uint32_t preserved_flags =
            response->flags & LEONOS_HTTP_FLAG_REDIRECTED;
        uint32_t redirect_count = response->redirect_count;
        response->net_status = LEONOS_NET_STATUS_HTTP_FAILED;
        response->http_status = 0;
        response->flags = preserved_flags;
        response->body_len = 0;
        response->headers_len = 0;
        response->content_length = 0;
        response->redirect_count = redirect_count;
        response->content_type[0] = 0;
        http_fetch_once(current_url, request, response,
                        location, sizeof(location));
        http_copy_text(response->final_url, sizeof(response->final_url),
                       current_url);
        printf("[http] request result url=%s status=%u net=%u body=%u redirects=%u flags=0x%x\n",
               current_url, response->http_status, response->net_status,
               response->body_len, response->redirect_count, response->flags);
        if (response->net_status != LEONOS_NET_STATUS_OK ||
            !http_is_redirect(response->http_status) ||
            !location[0]) {
            return 0;
        }
        printf("[http] redirect from=%s to=(location present) status=%u\n",
               current_url, response->http_status);
        if (max_redirects == 0) {
            return 0;
        }
        if (response->redirect_count >= max_redirects) {
            response->net_status = LEONOS_NET_STATUS_HTTP_FAILED;
            return 0;
        }
        if (leonos_http_resolve_url(current_url, location,
                                    next_url, sizeof(next_url)) < 0) {
            response->net_status = LEONOS_NET_STATUS_HTTP_FAILED;
            return 0;
        }
        http_copy_text(current_url, sizeof(current_url), next_url);
        ++response->redirect_count;
        response->flags |= LEONOS_HTTP_FLAG_REDIRECTED;
    }
}

int leonos_http_get(const char *url, uint32_t timeout_ms,
                    char *response_body, uint32_t response_body_capacity,
                    char *response_headers, uint32_t response_headers_capacity,
                    struct leonos_http_response *response)
{
    struct leonos_http_request request = {
        .url = url,
        .method = "GET",
        .extra_headers = 0,
        .request_body = 0,
        .request_body_len = 0,
        .timeout_ms = timeout_ms,
        .max_redirects = LEONOS_HTTP_DEFAULT_REDIRECTS,
        .response_body = response_body,
        .response_body_capacity = response_body_capacity,
        .response_headers = response_headers,
        .response_headers_capacity = response_headers_capacity,
    };
    return leonos_http_request(&request, response);
}

#define HTTP_DOWNLOAD_HEADER_MAX LEONOS_HTTP_HEADER_MAX
#define HTTP_DOWNLOAD_READ_SIZE 4096U

enum http_download_body_state {
    HTTP_DOWNLOAD_BODY_IDENTITY,
    HTTP_DOWNLOAD_BODY_CHUNK_SIZE,
    HTTP_DOWNLOAD_BODY_CHUNK_DATA,
    HTTP_DOWNLOAD_BODY_CHUNK_CRLF,
};

struct http_download_stream {
    char headers[HTTP_DOWNLOAD_HEADER_MAX + 1U];
    char location[LEONOS_HTTP_URL_LEN];
    struct leonos_http_response *response;
    leonos_http_download_progress_fn progress;
    void *progress_context;
    uint32_t headers_len;
    uint32_t total;
    uint32_t received;
    uint32_t chunk_remaining;
    uint8_t body_state;
    uint8_t chunk_seen_hex;
    uint8_t chunk_extension;
    uint8_t chunk_crlf_seen;
    uint8_t headers_ready;
    uint8_t complete;
    uint8_t redirect;
    uint8_t failed;
    uint8_t cancelled;
    int fd;
};

static int http_download_report(struct http_download_stream *stream)
{
    if (!stream || !stream->progress) {
        return 0;
    }
    if (stream->progress(stream->received, stream->total,
                         stream->progress_context) < 0) {
        stream->cancelled = 1;
        return -1;
    }
    return 0;
}

static int http_download_write_all(struct http_download_stream *stream,
                                   const char *data, uint32_t length)
{
    uint32_t written = 0;
    if (!stream || !data) {
        return -1;
    }
    if (stream->total && (length > stream->total ||
                          stream->received > stream->total - length)) {
        stream->failed = 1;
        return -1;
    }
    while (written < length) {
        long ret = write(stream->fd, data + written, length - written);
        if (ret <= 0) {
            stream->failed = 1;
            return -1;
        }
        written += (uint32_t)ret;
    }
    stream->received += length;
    if (http_download_report(stream) < 0) {
        return -1;
    }
    if (stream->total && stream->received == stream->total) {
        stream->complete = 1;
        return -1;
    }
    return 0;
}

static int http_download_parse_headers(struct http_download_stream *stream)
{
    char transfer_encoding[48];
    char content_length_text[32];
    int valid_length = 0;
    if (!stream || !stream->response) {
        return -1;
    }
    stream->response->headers_len = stream->headers_len;
    stream->response->http_status = http_parse_status_code(stream->headers,
                                                            stream->headers_len);
    if (!stream->response->http_status) {
        stream->failed = 1;
        return -1;
    }
    stream->response->net_status = LEONOS_NET_STATUS_OK;
    http_header_value(stream->headers, stream->headers_len, "Content-Type",
                      stream->response->content_type,
                      sizeof(stream->response->content_type));
    http_header_value(stream->headers, stream->headers_len, "Location",
                      stream->location, sizeof(stream->location));
    transfer_encoding[0] = 0;
    http_header_value(stream->headers, stream->headers_len,
                      "Transfer-Encoding", transfer_encoding,
                      sizeof(transfer_encoding));
    content_length_text[0] = 0;
    if (http_header_value(stream->headers, stream->headers_len,
                          "Content-Length", content_length_text,
                          sizeof(content_length_text))) {
        stream->response->content_length =
            http_parse_decimal(content_length_text, &valid_length);
        if (valid_length) {
            stream->response->flags |= LEONOS_HTTP_FLAG_CONTENT_LENGTH;
        }
    }
    if (http_contains_ignore_case(transfer_encoding, "chunked")) {
        stream->response->flags |= LEONOS_HTTP_FLAG_CHUNKED;
        stream->body_state = HTTP_DOWNLOAD_BODY_CHUNK_SIZE;
    } else {
        stream->body_state = HTTP_DOWNLOAD_BODY_IDENTITY;
        stream->total = valid_length ? stream->response->content_length : 0;
    }
    stream->headers_ready = 1;
    if (http_is_redirect(stream->response->http_status) && stream->location[0]) {
        stream->redirect = 1;
        return -1;
    }
    if (stream->response->http_status < 200U ||
        stream->response->http_status >= 300U) {
        stream->failed = 1;
        return -1;
    }
    if (http_download_report(stream) < 0) {
        return -1;
    }
    if (stream->total == 0 &&
        (stream->response->flags & LEONOS_HTTP_FLAG_CONTENT_LENGTH)) {
        stream->complete = 1;
        return -1;
    }
    return 0;
}

static int http_download_consume_chunked(struct http_download_stream *stream,
                                         const char *data, uint32_t length,
                                         uint32_t *position)
{
    while (*position < length) {
        char ch = data[*position];
        if (stream->body_state == HTTP_DOWNLOAD_BODY_CHUNK_SIZE) {
            ++(*position);
            if (ch == '\r') {
                continue;
            }
            if (ch == '\n') {
                if (!stream->chunk_seen_hex) {
                    stream->failed = 1;
                    return -1;
                }
                if (stream->chunk_remaining == 0) {
                    stream->complete = 1;
                    return -1;
                }
                stream->body_state = HTTP_DOWNLOAD_BODY_CHUNK_DATA;
                continue;
            }
            if (ch == ';') {
                if (!stream->chunk_seen_hex) {
                    stream->failed = 1;
                    return -1;
                }
                stream->chunk_extension = 1;
                continue;
            }
            if (!stream->chunk_extension && http_is_hex(ch)) {
                uint32_t digit = http_hex_value(ch);
                if (stream->chunk_remaining > (0xffffffffU - digit) / 16U) {
                    stream->failed = 1;
                    return -1;
                }
                stream->chunk_remaining = stream->chunk_remaining * 16U + digit;
                stream->chunk_seen_hex = 1;
                continue;
            }
            if (!stream->chunk_extension) {
                stream->failed = 1;
                return -1;
            }
            continue;
        }
        if (stream->body_state == HTTP_DOWNLOAD_BODY_CHUNK_DATA) {
            uint32_t available = length - *position;
            uint32_t take = available < stream->chunk_remaining
                                ? available : stream->chunk_remaining;
            if (http_download_write_all(stream, data + *position, take) < 0) {
                return -1;
            }
            *position += take;
            stream->chunk_remaining -= take;
            if (stream->chunk_remaining == 0) {
                stream->body_state = HTTP_DOWNLOAD_BODY_CHUNK_CRLF;
                stream->chunk_crlf_seen = 0;
            }
            continue;
        }
        ++(*position);
        if (ch == '\r' && !stream->chunk_crlf_seen) {
            stream->chunk_crlf_seen = 1;
            continue;
        }
        if (ch == '\n') {
            stream->body_state = HTTP_DOWNLOAD_BODY_CHUNK_SIZE;
            stream->chunk_remaining = 0;
            stream->chunk_seen_hex = 0;
            stream->chunk_extension = 0;
            continue;
        }
        stream->failed = 1;
        return -1;
    }
    return 0;
}

static int http_download_stream_data(const void *raw_data, uint32_t length,
                                     void *context)
{
    struct http_download_stream *stream = (struct http_download_stream *)context;
    const char *data = (const char *)raw_data;
    uint32_t position = 0;
    if (!stream) {
        return -1;
    }
    if (length == 0) {
        return http_download_report(stream);
    }
    while (position < length) {
        if (!stream->headers_ready) {
            uint32_t body_offset;
            if (stream->headers_len >= HTTP_DOWNLOAD_HEADER_MAX) {
                stream->failed = 1;
                return -1;
            }
            stream->headers[stream->headers_len++] = data[position++];
            stream->headers[stream->headers_len] = 0;
            body_offset = http_find_body_offset(stream->headers,
                                                stream->headers_len);
            if (!body_offset) {
                continue;
            }
            if (http_download_parse_headers(stream) < 0) {
                return -1;
            }
            continue;
        }
        if (stream->body_state == HTTP_DOWNLOAD_BODY_IDENTITY) {
            if (http_download_write_all(stream, data + position,
                                        length - position) < 0) {
                return -1;
            }
            position = length;
        } else if (http_download_consume_chunked(stream, data, length,
                                                  &position) < 0) {
            return -1;
        }
    }
    return 0;
}

static int http_download_fetch_once(const char *url_text,
                                    uint32_t timeout_ms,
                                    struct http_download_stream *stream)
{
    struct libc_http_url url;
    struct leonos_net_socket_connect connection;
    struct leonos_http_request request = {0};
    char request_text[HTTP_REQUEST_MAX];
    uint32_t request_len;
    uint32_t net_status = LEONOS_NET_STATUS_HTTP_FAILED;
    int socket;
    int ret = -1;
    if (!url_text || !stream || !stream->response ||
        !http_parse_url(url_text, &url)) {
        return -1;
    }
    request.method = "GET";
    request_len = http_build_request_text(request_text, sizeof(request_text),
                                          &url, &request);
    if (!request_len || http_download_report(stream) < 0) {
        return -1;
    }
    socket = leonos_socket_tcp();
    if (socket < 0) {
        stream->response->net_status = LEONOS_NET_STATUS_SOCKET_LIMIT;
        return -1;
    }
    ret = leonos_socket_connect(socket, url.host, url.port, timeout_ms,
                                &connection);
    if (ret < 0 || connection.status != LEONOS_NET_STATUS_OK) {
        stream->response->net_status = ret < 0 ? LEONOS_NET_STATUS_TCP_FAILED
                                                : connection.status;
        leonos_socket_close(socket);
        return -1;
    }
    if (url.secure) {
        ret = leonos_tls_http_stream(socket, url.host, timeout_ms,
                                     request_text, request_len, 0, 0,
                                     http_download_stream_data, stream);
    } else {
        ret = (int)leonos_socket_send(socket, request_text, request_len,
                                      timeout_ms, &net_status);
        if (ret >= 0 && net_status == LEONOS_NET_STATUS_OK &&
            (uint32_t)ret == request_len) {
            char buffer[HTTP_DOWNLOAD_READ_SIZE];
            unsigned long last_data = leonos_uptime_ms();
            ret = 0;
            for (;;) {
                long got;
                if (http_download_report(stream) < 0) {
                    ret = -1;
                    break;
                }
                got = leonos_socket_recv(socket, buffer, sizeof(buffer),
                                         200U,
                                         &net_status);
                if (got == 0) {
                    if (net_status == LEONOS_NET_STATUS_TCP_TIMEOUT) {
                        if (leonos_uptime_ms() - last_data < timeout_ms) {
                            continue;
                        }
                    }
                    break;
                }
                if (got < 0 || http_download_stream_data(buffer,
                                                          (uint32_t)got,
                                                          stream) < 0) {
                    ret = -1;
                    break;
                }
                last_data = leonos_uptime_ms();
            }
            if (net_status != LEONOS_NET_STATUS_OK &&
                !stream->complete && !stream->redirect && !stream->failed &&
                !stream->cancelled) {
                ret = -1;
            }
        } else {
            stream->response->net_status = net_status;
            ret = -1;
        }
    }
    leonos_socket_close(socket);
    if (stream->redirect || stream->complete) {
        return 0;
    }
    if (ret < 0 || stream->failed || stream->cancelled ||
        !stream->headers_ready) {
        if (stream->response->net_status == LEONOS_NET_STATUS_HTTP_FAILED) {
            stream->response->net_status = url.secure
                                               ? LEONOS_NET_STATUS_TLS_FAILED
                                               : LEONOS_NET_STATUS_TCP_FAILED;
        }
        return -1;
    }
    if (stream->body_state == HTTP_DOWNLOAD_BODY_IDENTITY &&
        (!stream->total || stream->received == stream->total)) {
        stream->complete = 1;
        return 0;
    }
    stream->response->net_status = LEONOS_NET_STATUS_HTTP_FAILED;
    return -1;
}

static int http_download_temp_path(const char *output_path, char *temp_path,
                                   uint32_t capacity)
{
    uint32_t length;
    if (!output_path || !output_path[0] || !temp_path || capacity == 0) {
        return 0;
    }
    length = (uint32_t)strlen(output_path);
    if (length + 6U >= capacity) {
        return 0;
    }
    memcpy(temp_path, output_path, length);
    memcpy(temp_path + length, ".part", 6U);
    return 1;
}

int leonos_http_download(const char *url, const char *output_path,
                         uint32_t timeout_ms,
                         leonos_http_download_progress_fn progress,
                         void *context,
                         struct leonos_http_response *response)
{
    char current_url[LEONOS_HTTP_URL_LEN];
    char next_url[LEONOS_HTTP_URL_LEN];
    char temp_path[LEONOS_FS_PATH_LEN];
    uint32_t redirects = 0;
    if (!url || !output_path || !response ||
        !http_download_temp_path(output_path, temp_path, sizeof(temp_path))) {
        return -1;
    }
    *response = (struct leonos_http_response){0};
    http_copy_text(current_url, sizeof(current_url), url);
    for (;;) {
        struct http_download_stream stream = {0};
        stream.response = response;
        stream.progress = progress;
        stream.progress_context = context;
        stream.fd = open(temp_path, LEONOS_O_WRONLY | LEONOS_O_CREAT |
                         LEONOS_O_TRUNC, 0);
        if (stream.fd < 0) {
            response->net_status = LEONOS_NET_STATUS_HTTP_FAILED;
            return -1;
        }
        response->net_status = LEONOS_NET_STATUS_HTTP_FAILED;
        response->http_status = 0;
        response->flags &= LEONOS_HTTP_FLAG_REDIRECTED;
        response->body_len = 0;
        response->headers_len = 0;
        response->content_length = 0;
        response->content_type[0] = 0;
        (void)http_download_fetch_once(current_url,
                                       timeout_ms ? timeout_ms
                                                  : LEONOS_HTTP_DEFAULT_TIMEOUT_MS,
                                       &stream);
        close(stream.fd);
        http_copy_text(response->final_url, sizeof(response->final_url),
                       current_url);
        if (stream.redirect) {
            unlink(temp_path);
            if (redirects >= LEONOS_HTTP_DEFAULT_REDIRECTS ||
                leonos_http_resolve_url(current_url, stream.location,
                                        next_url, sizeof(next_url)) < 0) {
                response->net_status = LEONOS_NET_STATUS_HTTP_FAILED;
                return -1;
            }
            http_copy_text(current_url, sizeof(current_url), next_url);
            ++redirects;
            response->redirect_count = redirects;
            response->flags |= LEONOS_HTTP_FLAG_REDIRECTED;
            continue;
        }
        if (!stream.complete || stream.failed || stream.cancelled ||
            response->net_status != LEONOS_NET_STATUS_OK) {
            unlink(temp_path);
            return -1;
        }
        response->body_len = stream.received;
        if (rename(temp_path, output_path) < 0) {
            unlink(temp_path);
            response->net_status = LEONOS_NET_STATUS_HTTP_FAILED;
            return -1;
        }
        if (http_download_report(&stream) < 0) {
            return -1;
        }
        return 0;
    }
}

static void libc_copy_fixed(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void libc_clear_secret(void *data, uint32_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)data;
    while (p && len) {
        *p++ = 0;
        --len;
    }
}

int leonos_auth_status(struct leonos_auth_status *status)
{
    if (!status) {
        return -1;
    }
    return ioctl(3, LEONOS_AUTH_IOCTL_STATUS, status);
}

int leonos_auth_current(struct leonos_user_info *user)
{
    if (!user) {
        return -1;
    }
    return ioctl(3, LEONOS_AUTH_IOCTL_CURRENT, user);
}

int leonos_auth_list_users(struct leonos_user_info *users, uint32_t capacity,
                           uint32_t include_disabled, uint32_t *out_count)
{
    struct leonos_user_list query = {
        .actor_uid = 0,
        .actor_role = 0,
        .include_disabled = include_disabled,
        .capacity = capacity,
        .count = 0,
        .reserved = 0,
        .users = users,
    };
    int ret = ioctl(3, LEONOS_AUTH_IOCTL_LIST_USERS, &query);
    if (out_count) {
        *out_count = query.count;
    }
    return ret;
}

int leonos_auth_login(const char *username, const char *password,
                      struct leonos_user_info *user)
{
    struct leonos_auth_login login;
    login = (struct leonos_auth_login){0};
    libc_copy_fixed(login.username, sizeof(login.username), username);
    libc_copy_fixed(login.password, sizeof(login.password), password);
    int ret = ioctl(3, LEONOS_AUTH_IOCTL_LOGIN, &login);
    if (ret == 0 && user) {
        *user = login.user;
    }
    libc_clear_secret(login.password, sizeof(login.password));
    return ret;
}

int leonos_auth_elevate_admin(const char *username, const char *password,
                               struct leonos_user_info *user)
{
    struct leonos_auth_login login;
    login = (struct leonos_auth_login){0};
    libc_copy_fixed(login.username, sizeof(login.username), username);
    libc_copy_fixed(login.password, sizeof(login.password), password);
    int ret = ioctl(3, LEONOS_AUTH_IOCTL_ELEVATE_ADMIN, &login);
    if (ret == 0 && user) {
        *user = login.user;
    }
    libc_clear_secret(login.password, sizeof(login.password));
    return ret;
}

int leonos_auth_delegate_elevation(uint32_t child_pid)
{
    struct leonos_auth_delegate_elevation delegation = {
        .child_pid = child_pid,
        .reserved = 0,
    };
    return ioctl(3, LEONOS_AUTH_IOCTL_DELEGATE_ELEVATION, &delegation);
}

int leonos_auth_logout(void)
{
    return ioctl(3, LEONOS_AUTH_IOCTL_LOGOUT, 0);
}

int leonos_auth_create_user(const char *username, const char *password,
                            uint32_t role, struct leonos_user_info *user)
{
    struct leonos_auth_create create;
    create = (struct leonos_auth_create){0};
    create.role = role;
    libc_copy_fixed(create.username, sizeof(create.username), username);
    libc_copy_fixed(create.password, sizeof(create.password), password);
    int ret = ioctl(3, LEONOS_AUTH_IOCTL_CREATE_USER, &create);
    if (ret == 0 && user) {
        *user = create.user;
    }
    libc_clear_secret(create.password, sizeof(create.password));
    return ret;
}

int leonos_auth_update_user(uint32_t uid, uint32_t mask, uint32_t role,
                            uint32_t flags)
{
    struct leonos_auth_update update;
    update = (struct leonos_auth_update){0};
    update.uid = uid;
    update.mask = mask;
    update.role = role;
    update.flags = flags;
    return ioctl(3, LEONOS_AUTH_IOCTL_UPDATE_USER, &update);
}

int leonos_auth_change_password(uint32_t uid, const char *old_password,
                                const char *new_password)
{
    struct leonos_auth_password password;
    password = (struct leonos_auth_password){0};
    password.uid = uid;
    libc_copy_fixed(password.old_password, sizeof(password.old_password), old_password);
    libc_copy_fixed(password.new_password, sizeof(password.new_password), new_password);
    {
        int ret = ioctl(3, LEONOS_AUTH_IOCTL_CHANGE_PASSWORD, &password);
        libc_clear_secret(password.old_password, sizeof(password.old_password));
        libc_clear_secret(password.new_password, sizeof(password.new_password));
        return ret;
    }
}

int leonos_startup_request(const struct leonos_startup_command *command,
                           uint32_t *out_request_id)
{
    struct leonos_startup_request request;
    int ret;
    if (!command) {
        return -1;
    }
    request = (struct leonos_startup_request){0};
    request.command = *command;
    ret = ioctl(3, LEONOS_STARTUP_IOCTL_REQUEST, &request);
    if (out_request_id) {
        *out_request_id = request.request_id;
    }
    return ret;
}

int leonos_startup_request_status(uint32_t request_id, uint32_t *out_status)
{
    struct leonos_startup_request_status request = {request_id, 0};
    int ret = ioctl(3, LEONOS_STARTUP_IOCTL_REQUEST_STATUS, &request);
    if (out_status) {
        *out_status = request.status;
    }
    return ret;
}

int leonos_startup_dialog_get(struct leonos_startup_dialog_request *request)
{
    return request ? ioctl(3, LEONOS_STARTUP_IOCTL_DIALOG_GET, request) : -1;
}

int leonos_startup_dialog_resolve(uint32_t request_id, uint32_t decision)
{
    struct leonos_startup_dialog_resolution resolution = {request_id, decision};
    return ioctl(3, LEONOS_STARTUP_IOCTL_DIALOG_RESOLVE, &resolution);
}

int leonos_startup_list(uint32_t uid, struct leonos_startup_entry *entries,
                        uint32_t capacity, uint32_t *out_count)
{
    struct leonos_startup_list list = {uid, capacity, 0, 0, entries};
    int ret = ioctl(3, LEONOS_STARTUP_IOCTL_LIST, &list);
    if (out_count) {
        *out_count = list.count;
    }
    return ret;
}

int leonos_startup_set_enabled(uint32_t uid, uint32_t entry_id, uint32_t enabled)
{
    struct leonos_startup_update update = {uid, entry_id, enabled ? 1U : 0U, 0};
    return ioctl(3, LEONOS_STARTUP_IOCTL_SET_ENABLED, &update);
}

int leonos_startup_remove(uint32_t uid, uint32_t entry_id)
{
    struct leonos_startup_update update = {uid, entry_id, 0, 0};
    return ioctl(3, LEONOS_STARTUP_IOCTL_REMOVE, &update);
}

int leonos_startup_launch_current_user(void)
{
    return ioctl(3, LEONOS_STARTUP_IOCTL_LAUNCH_CURRENT, 0);
}

int leonos_system_info(struct leonos_system_info *info)
{
    if (!info) {
        return -1;
    }
    return ioctl(3, LEONOS_IOCTL_SYSTEM_INFO, info);
}

int leonos_perf_info(struct leonos_perf_info *info)
{
    if (!info) {
        return -1;
    }
    return ioctl(3, LEONOS_IOCTL_PERF_INFO, info);
}

int leonos_time_info(struct leonos_time_info *info)
{
    if (!info) {
        return -1;
    }
    return ioctl(3, LEONOS_IOCTL_TIME_INFO, info);
}

int leonos_time_ntp_sync(uint32_t timeout_ms, struct leonos_time_sync *result)
{
    if (!result) {
        return -1;
    }
    *result = (struct leonos_time_sync){0};
    result->timeout_ms = timeout_ms;
    return ioctl(3, LEONOS_IOCTL_TIME_NTP_SYNC, result);
}

int leonos_machine_identity(struct leonos_machine_identity *identity)
{
    if (!identity) {
        return -1;
    }
    return ioctl(3, LEONOS_IOCTL_MACHINE_IDENTITY, identity);
}

int leonos_system_reboot(void)
{
    return ioctl(3, LEONOS_GUI_IOCTL_REBOOT, 0);
}

int leonos_system_shutdown(void)
{
    return ioctl(3, LEONOS_GUI_IOCTL_SHUTDOWN, 0);
}

int leonos_kernel_debug_get_state(uint32_t *flags)
{
    struct leonos_kernel_debug_control control = {
        .version = LEONOS_KERNEL_DEBUG_VERSION,
        .command = LEONOS_KERNEL_DEBUG_CONTROL_GET_STATE,
    };
    int ret;
    if (!flags) return -1;
    ret = ioctl(3, LEONOS_KERNEL_DEBUG_IOCTL_CONTROL, &control);
    if (ret == 0) *flags = control.result_flags;
    return ret;
}

int leonos_kernel_debug_set_enabled(int enabled)
{
    struct leonos_kernel_debug_control control = {
        .version = LEONOS_KERNEL_DEBUG_VERSION,
        .command = LEONOS_KERNEL_DEBUG_CONTROL_SET_ENABLED,
        .flags = enabled ? LEONOS_KERNEL_DEBUG_STATE_ENABLED : 0U,
    };
    return ioctl(3, LEONOS_KERNEL_DEBUG_IOCTL_CONTROL, &control);
}

int leonos_kernel_debug_arm_next_boot(void)
{
    struct leonos_kernel_debug_control control = {
        .version = LEONOS_KERNEL_DEBUG_VERSION,
        .command = LEONOS_KERNEL_DEBUG_CONTROL_ARM_NEXT_BOOT,
    };
    return ioctl(3, LEONOS_KERNEL_DEBUG_IOCTL_CONTROL, &control);
}

int leonos_kernel_debug_clear(void)
{
    struct leonos_kernel_debug_control control = {
        .version = LEONOS_KERNEL_DEBUG_VERSION,
        .command = LEONOS_KERNEL_DEBUG_CONTROL_CLEAR,
    };
    return ioctl(3, LEONOS_KERNEL_DEBUG_IOCTL_CONTROL, &control);
}

int leonos_readdir(int fd, struct leonos_dir_entry *entry)
{
    long got;
    if (!entry) {
        return -1;
    }
    got = read(fd, entry, sizeof(*entry));
    if (got < 0) {
        return (int)got;
    }
    if (got == 0) {
        return 0;
    }
    return got == (long)sizeof(*entry) ? 1 : -1;
}

static int locale_cached = -1;
static int locale_lang = LEONOS_LANG_EN;

int leonos_i18n_language(void)
{
    char buf[64];
    int fd;
    long got;
    if (locale_cached >= 0) {
        return locale_lang;
    }
    locale_cached = 1;
    locale_lang = LEONOS_LANG_EN;
    fd = open(LEONOS_LOCALE_CONFIG_PATH, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return locale_lang;
    }
    got = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (got <= 0) {
        return locale_lang;
    }
    buf[got] = 0;
    for (long i = 0; i < got; ++i) {
        if ((buf[i] == 'l' || buf[i] == 'L') &&
            i + 6 < got &&
            buf[i + 1] == 'a' && buf[i + 2] == 'n' && buf[i + 3] == 'g' &&
            buf[i + 4] == '=' && buf[i + 5] == 'z' && buf[i + 6] == 'h') {
            locale_lang = LEONOS_LANG_ZH;
            break;
        }
    }
    return locale_lang;
}

const char *leonos_i18n(const char *en, const char *zh)
{
    return leonos_i18n_language() == LEONOS_LANG_ZH && zh ? zh : en;
}

int leonos_i18n_set_language(int lang)
{
    const char *text = lang == LEONOS_LANG_ZH ? "lang=zh\n" : "lang=en\n";
    int fd = open(LEONOS_LOCALE_CONFIG_PATH,
                  LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    long wrote;
    if (fd < 0) {
        return fd;
    }
    wrote = write(fd, text, strlen(text));
    close(fd);
    if (wrote < 0) {
        return (int)wrote;
    }
    locale_lang = lang == LEONOS_LANG_ZH ? LEONOS_LANG_ZH : LEONOS_LANG_EN;
    locale_cached = 1;
    return 0;
}

int leonos_pty_create(void)
{
    return ioctl(3, LEONOS_PTY_IOCTL_CREATE, 0);
}

int leonos_pty_destroy(uint32_t pty_id)
{
    return ioctl(3, LEONOS_PTY_IOCTL_DESTROY, (void *)(uintptr_t)pty_id);
}

int leonos_pty_read_output(uint32_t pty_id, char *buffer, uint32_t length)
{
    struct leonos_pty_io io = {
        .pty_id = pty_id,
        .length = length,
        .buffer = buffer,
    };
    return ioctl(3, LEONOS_PTY_IOCTL_READ_OUTPUT, &io);
}

int leonos_pty_write_input(uint32_t pty_id, const char *buffer, uint32_t length)
{
    struct leonos_pty_io io = {
        .pty_id = pty_id,
        .length = length,
        .buffer = (char *)buffer,
    };
    return ioctl(3, LEONOS_PTY_IOCTL_WRITE_INPUT, &io);
}

int leonos_pty_spawn(const char *path, uint32_t pty_id)
{
    return leonos_pty_spawn_argv(path, pty_id, 0, 0);
}

int leonos_pty_spawn_argv(const char *path, uint32_t pty_id,
                          char *const argv[], char *const envp[])
{
    return leonos_pty_spawn_argv_with_fds(path, pty_id, argv, envp, -1, -1, -1);
}

int leonos_pty_spawn_argv_with_fds(const char *path, uint32_t pty_id,
                                   char *const argv[], char *const envp[],
                                   int stdin_fd, int stdout_fd, int stderr_fd)
{
    char **owned_envp = 0;
    char *const *effective_envp = envp;
    int result;
    if (!effective_envp) {
        result = leonos_environment_build(0, &owned_envp);
        if (result < 0) {
            return result;
        }
        effective_envp = owned_envp;
    }
    struct leonos_pty_spawn spawn = {
        .pty_id = pty_id,
        .path = path,
        .argv = argv,
        .envp = effective_envp,
        .stdin_fd = stdin_fd,
        .stdout_fd = stdout_fd,
        .stderr_fd = stderr_fd,
    };
    result = ioctl(3, LEONOS_PTY_IOCTL_SPAWN, &spawn);
    leonos_environment_free(owned_envp);
    return result;
}

int leonos_pty_self(void)
{
    return ioctl(3, LEONOS_PTY_IOCTL_SELF, 0);
}

int leonos_pty_input_available(void)
{
    return ioctl(3, LEONOS_PTY_IOCTL_INPUT_AVAILABLE, 0);
}

static int leonos_pty_error(int result);

int leonos_pty_get_termios(uint32_t pty_id, struct leonos_pty_termios *termios)
{
    struct leonos_pty_termios_io io;
    int result;
    if (!pty_id || !termios) {
        errno = EINVAL;
        return -1;
    }
    io.pty_id = pty_id;
    io.action = 0;
    result = ioctl(3, LEONOS_PTY_IOCTL_OWNER_GET_ATTR, &io);
    if (result < 0) {
        return leonos_pty_error(result);
    }
    *termios = io.termios;
    return 0;
}

int leonos_pty_set_termios(uint32_t pty_id,
                           const struct leonos_pty_termios *termios)
{
    struct leonos_pty_termios_io io;
    if (!pty_id || !termios) {
        errno = EINVAL;
        return -1;
    }
    io.pty_id = pty_id;
    io.action = 0;
    io.termios = *termios;
    return leonos_pty_error(ioctl(3, LEONOS_PTY_IOCTL_OWNER_SET_ATTR, &io));
}

int leonos_pty_get_winsize(uint32_t pty_id, struct leonos_pty_winsize *winsize)
{
    struct leonos_pty_winsize_io io;
    int result;
    if (!pty_id || !winsize) {
        errno = EINVAL;
        return -1;
    }
    io.pty_id = pty_id;
    result = ioctl(3, LEONOS_PTY_IOCTL_OWNER_GET_WINSIZE, &io);
    if (result < 0) {
        return leonos_pty_error(result);
    }
    *winsize = io.winsize;
    return 0;
}

int leonos_pty_set_winsize(uint32_t pty_id,
                            const struct leonos_pty_winsize *winsize)
{
    struct leonos_pty_winsize_io io;
    if (!pty_id || !winsize) {
        errno = EINVAL;
        return -1;
    }
    io.pty_id = pty_id;
    io.winsize = *winsize;
    return leonos_pty_error(ioctl(3, LEONOS_PTY_IOCTL_OWNER_SET_WINSIZE, &io));
}

int posix_openpt(int flags)
{
    return open("/dev/ptmx", flags, 0);
}

int grantpt(int fd)
{
    uint32_t number = 0;
    return ioctl(fd, TIOCGPTN, &number);
}

int unlockpt(int fd)
{
    uint32_t lock = 0;
    return ioctl(fd, TIOCSPTLCK, &lock);
}

char *ptsname(int fd)
{
    static char path[32];
    uint32_t number = 0;
    if (ioctl(fd, TIOCGPTN, &number) < 0) return NULL;
    if (snprintf(path, sizeof(path), "/dev/pts/%u", number) < 0) return NULL;
    return path;
}

int openpty(int *master, int *slave, char *name,
            const struct termios *termios, const struct winsize *winsize)
{
    int mfd;
    int sfd;
    char *path;
    if (!master || !slave) {
        errno = EINVAL;
        return -1;
    }
    mfd = posix_openpt(LEONOS_O_RDWR);
    if (mfd < 0) return -1;
    if (grantpt(mfd) < 0 || unlockpt(mfd) < 0 || !(path = ptsname(mfd))) {
        close(mfd);
        return -1;
    }
    sfd = open(path, LEONOS_O_RDWR, 0);
    if (sfd < 0) {
        close(mfd);
        return -1;
    }
    if (termios) (void)tcsetattr(sfd, TCSANOW, termios);
    if (winsize) (void)tcsetwinsize(sfd, winsize);
    if (name) {
        size_t i = 0;
        while (path[i] && i + 1 < 32) { name[i] = path[i]; ++i; }
        name[i] = 0;
    }
    *master = mfd;
    *slave = sfd;
    return 0;
}

pid_t forkpty(int *master, const char *name,
              const struct termios *termios, const struct winsize *winsize)
{
    int mfd;
    int sfd;
    pid_t pid;
    char path[32];
    if (openpty(&mfd, &sfd, path, termios, winsize) < 0) return -1;
    pid = fork();
    if (pid == 0) {
        (void)setsid();
        (void)dup2(sfd, 0);
        (void)dup2(sfd, 1);
        (void)dup2(sfd, 2);
        close(mfd);
        close(sfd);
        return 0;
    }
    close(sfd);
    if (master) *master = mfd;
    else close(mfd);
    (void)name;
    return pid;
}

struct leonos_linux_winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

static int leonos_pty_error(int result)
{
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return 0;
}

int tcgetattr(int fd, struct termios *termios)
{
    struct leonos_pty_termios native;
    int result;
    if (fd < 0 || !termios) {
        errno = EINVAL;
        return -1;
    }
    result = ioctl(fd, TCGETS, &native);
    if (result < 0) {
        return leonos_pty_error(result);
    }
    termios->c_iflag = native.c_iflag;
    termios->c_oflag = native.c_oflag;
    termios->c_cflag = native.c_cflag;
    termios->c_lflag = native.c_lflag;
    for (uint32_t index = 0; index < LEONOS_PTY_NCCS; ++index) {
        termios->c_cc[index] = native.c_cc[index];
    }
    termios->c_ispeed = native.c_ispeed;
    termios->c_ospeed = native.c_ospeed;
    return 0;
}

int tcsetattr(int fd, int action, const struct termios *termios)
{
    struct leonos_pty_termios_request request;
    int result;
    if (fd < 0 || !termios) {
        errno = EINVAL;
        return -1;
    }
    if (action != TCSANOW && action != TCSADRAIN && action != TCSAFLUSH) {
        errno = EINVAL;
        return -1;
    }
    request.action = (uint32_t)action;
    request.reserved = 0;
    request.termios.c_iflag = termios->c_iflag;
    request.termios.c_oflag = termios->c_oflag;
    request.termios.c_cflag = termios->c_cflag;
    request.termios.c_lflag = termios->c_lflag;
    for (uint32_t index = 0; index < LEONOS_PTY_NCCS; ++index) {
        request.termios.c_cc[index] = termios->c_cc[index];
    }
    request.termios.reserved = 0;
    request.termios.c_ispeed = termios->c_ispeed;
    request.termios.c_ospeed = termios->c_ospeed;
    result = ioctl(fd,
                   action == TCSADRAIN ? TCSETSW :
                   (action == TCSAFLUSH ? TCSETSF : TCSETS),
                   &request.termios);
    return leonos_pty_error(result);
}

int tcgetwinsize(int fd, struct winsize *winsize)
{
    struct leonos_linux_winsize native;
    int result;
    if (fd < 0 || !winsize) {
        errno = EINVAL;
        return -1;
    }
    native = (struct leonos_linux_winsize){0};
    result = ioctl(fd, TIOCGWINSZ, &native);
    if (result == 0) {
        winsize->ws_row = native.ws_row;
        winsize->ws_col = native.ws_col;
    }
    return leonos_pty_error(result);
}

int tcsetwinsize(int fd, const struct winsize *winsize)
{
    struct leonos_linux_winsize native;
    int result;
    if (fd < 0 || !winsize) {
        errno = EINVAL;
        return -1;
    }
    native = (struct leonos_linux_winsize){
        .ws_row = winsize->ws_row,
        .ws_col = winsize->ws_col,
        .ws_xpixel = 0,
        .ws_ypixel = 0,
    };
    result = ioctl(fd, TIOCSWINSZ, &native);
    return leonos_pty_error(result);
}

int ftruncate(int fd, off_t length)
{
    long result;
    if (length < 0) {
        errno = EINVAL;
        return -1;
    }
    result = syscall2(77, fd, (long)length);
    return leonos_pty_error((int)result);
}
