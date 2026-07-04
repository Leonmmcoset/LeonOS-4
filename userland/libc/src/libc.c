#include <leonos/device.h>
#include <leonos/auth.h>
#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/pty.h>
#include <leonos/stdio.h>
#include <leonos/system.h>
#include <leonos/syscall.h>
#include <leonos/text.h>
#include <stdarg.h>

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

void *mmap(void *addr, size_t len, int prot, int flags, int fd, long offset)
{
    long ret = syscall6(SYS_mmap, (long)addr, (long)len, prot, flags, fd, offset);
    return ret < 0 ? LEONOS_MAP_FAILED : (void *)ret;
}

int munmap(void *addr, size_t len)
{
    return (int)syscall2(SYS_munmap, (long)addr, (long)len);
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

int leonos_install_list_disks(struct leonos_install_disk *disks,
                              uint32_t capacity, uint32_t *out_count)
{
    struct leonos_install_disk_list query = {
        .capacity = capacity,
        .count = 0,
        .disks = disks,
    };
    int ret = ioctl(3, LEONOS_INSTALL_IOCTL_LIST_DISKS, &query);
    if (out_count) {
        *out_count = query.count;
    }
    return ret;
}

int leonos_install_format_esp(uint32_t disk_id)
{
    return ioctl(3, LEONOS_INSTALL_IOCTL_FORMAT_ESP, (void *)(uintptr_t)disk_id);
}

int leonos_install_mount_target(uint32_t disk_id)
{
    return ioctl(3, LEONOS_INSTALL_IOCTL_MOUNT_TARGET, (void *)(uintptr_t)disk_id);
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
    return ret;
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
    return ioctl(3, LEONOS_AUTH_IOCTL_CHANGE_PASSWORD, &password);
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
