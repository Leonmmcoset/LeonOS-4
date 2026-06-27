#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/pty.h>
#include <leonos/stdio.h>
#include <leonos/system.h>
#include <leonos/syscall.h>
#include <stdarg.h>

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

long read(int fd, void *buf, size_t len)
{
    return syscall3(SYS_read, fd, (long)buf, (long)len);
}

long write(int fd, const void *buf, size_t len)
{
    return syscall3(SYS_write, fd, (long)buf, (long)len);
}

int open(const char *path, int flags, int mode)
{
    return (int)syscall3(SYS_open, (long)path, flags, mode);
}

int close(int fd)
{
    return (int)syscall1(SYS_close, fd);
}

long lseek(int fd, long offset, int whence)
{
    return syscall3(SYS_lseek, fd, offset, whence);
}

void exit(int code)
{
    syscall1(SYS_exit, code);
    for (;;) {
    }
}

int chdir(const char *path)
{
    return (int)syscall1(SYS_chdir, (long)path);
}

char *getcwd(char *buf, size_t len)
{
    long ret = syscall2(SYS_getcwd, (long)buf, (long)len);
    return ret < 0 ? (char *)0 : (char *)ret;
}

int ioctl(int fd, unsigned long request, void *arg)
{
    return (int)syscall3(SYS_ioctl, fd, (long)request, (long)arg);
}

int sched_yield(void)
{
    return (int)syscall0(SYS_sched_yield);
}

int sleep_ms(unsigned long ms)
{
    return (int)syscall2(SYS_nanosleep, (long)ms, 0);
}

int getpid(void)
{
    return (int)syscall0(SYS_getpid);
}

int stat(const char *path, struct leonos_stat *st)
{
    return (int)syscall2(SYS_stat, (long)path, (long)st);
}

int fstat(int fd, struct leonos_stat *st)
{
    return (int)syscall2(SYS_fstat, fd, (long)st);
}

int wait4(int pid, int *status, int options, void *rusage)
{
    return (int)syscall6(SYS_wait4, pid, (long)status, options, (long)rusage, 0, 0);
}

int execve(const char *path, char *const argv[], char *const envp[])
{
    return (int)syscall3(SYS_execve, (long)path, (long)argv, (long)envp);
}

int mkdir(const char *path, int mode)
{
    return (int)syscall2(SYS_mkdir, (long)path, mode);
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

int printf(const char *fmt, ...)
{
    char buf[512];
    size_t pos = 0;
    va_list ap;
    va_start(ap, fmt);
    for (const char *p = fmt; *p && pos + 1 < sizeof(buf); ++p) {
        if (*p != '%') {
            buf[pos++] = *p;
            continue;
        }
        ++p;
        switch (*p) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            while (s && *s && pos + 1 < sizeof(buf)) {
                buf[pos++] = *s++;
            }
            break;
        }
        case 'd': {
            long v = va_arg(ap, int);
            if (v < 0) {
                buf[pos++] = '-';
                v = -v;
            }
            print_num(buf, &pos, (unsigned long)v, 10);
            break;
        }
        case 'x':
            print_num(buf, &pos, va_arg(ap, unsigned int), 16);
            break;
        case '%':
            buf[pos++] = '%';
            break;
        default:
            buf[pos++] = '%';
            buf[pos++] = *p;
            break;
        }
    }
    va_end(ap);
    write(1, buf, pos);
    return (int)pos;
}

int leonos_gui_connect(void)
{
    int fb = open("0:/dev/fb0", 0, 0);
    if (fb < 0) {
        return fb;
    }
    int version = ioctl(fb, LEONOS_GUI_IOCTL_VERSION, 0);
    close(fb);
    return version;
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
    return ioctl(3, LEONOS_GUI_IOCTL_FB_INFO, info);
}

int leonos_fb_fill(uint32_t color)
{
    return ioctl(3, LEONOS_GUI_IOCTL_FB_FILL, (void *)(long)color);
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
    return ioctl(3, LEONOS_GUI_IOCTL_FB_RECT, &rect);
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
    return ioctl(3, LEONOS_GUI_IOCTL_FB_TEXT, &cmd);
}

uint32_t leonos_fb_pixel(uint32_t x, uint32_t y)
{
    unsigned long packed = ((unsigned long)y << 32) | x;
    return (uint32_t)ioctl(3, LEONOS_GUI_IOCTL_FB_PIXEL, (void *)packed);
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
    return ioctl(3, LEONOS_GUI_IOCTL_FB_BLIT, &cmd);
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
    return ioctl(3, LEONOS_GUI_IOCTL_CREATE_WINDOW, &cmd);
}

int leonos_gui_destroy_app_window(uint32_t window_id)
{
    return ioctl(3, LEONOS_GUI_IOCTL_DESTROY_WINDOW, (void *)(unsigned long)window_id);
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

int leonos_gui_poll_app_event(struct leonos_gui_app_event *event)
{
    return ioctl(3, LEONOS_GUI_IOCTL_WINDOW_EVENT, event);
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

int leonos_task_kill(uint32_t pid)
{
    return ioctl(3, LEONOS_GUI_IOCTL_TASK_KILL, (void *)(uintptr_t)pid);
}

int leonos_list_dir(const char *path, struct leonos_dir_entry *entries,
                    uint32_t capacity, uint32_t *out_count)
{
    struct leonos_dir_list query = {
        .path = path,
        .capacity = capacity,
        .count = 0,
        .entries = entries,
    };
    int ret = ioctl(3, LEONOS_IOCTL_LIST_DIR, &query);
    if (out_count) {
        *out_count = query.count;
    }
    return ret;
}

int leonos_system_info(struct leonos_system_info *info)
{
    if (!info) {
        return -1;
    }
    return ioctl(3, LEONOS_IOCTL_SYSTEM_INFO, info);
}

int leonos_system_reboot(void)
{
    return ioctl(3, LEONOS_GUI_IOCTL_REBOOT, 0);
}

int leonos_system_shutdown(void)
{
    return ioctl(3, LEONOS_GUI_IOCTL_SHUTDOWN, 0);
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

int leonos_pty_create(void)
{
    return ioctl(3, LEONOS_PTY_IOCTL_CREATE, 0);
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
    struct leonos_pty_spawn spawn = {
        .pty_id = pty_id,
        .path = path,
        .argv = argv,
        .envp = envp,
    };
    return ioctl(3, LEONOS_PTY_IOCTL_SPAWN, &spawn);
}

int leonos_pty_self(void)
{
    return ioctl(3, LEONOS_PTY_IOCTL_SELF, 0);
}
