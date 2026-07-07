#include <leonos/device.h>
#include <leonos/auth.h>
#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/http.h>
#include <leonos/i18n.h>
#include <leonos/net.h>
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

int leonos_net_config(struct leonos_net_config *config)
{
    if (!config) {
        return -1;
    }
    return ioctl(3, LEONOS_IOCTL_NET_CONFIG, config);
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
    uint32_t port = 80;
    if (!url || !out || !http_starts_with_ignore_case(url, "http://")) {
        return 0;
    }
    p = url + 7;
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
                           uint32_t port, const char *path)
{
    uint32_t pos = 0;
    if (!dst || cap == 0) {
        return;
    }
    dst[0] = 0;
    http_append_text(dst, &pos, cap, "http://");
    http_append_text(dst, &pos, cap, host);
    if (port != 80U) {
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
        http_append_text(out, &pos, capacity, "http:");
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
        http_build_url(out, capacity, base.host, base.port, location_text);
        return 0;
    }
    http_parent_path(base.path, dir, sizeof(dir));
    out[0] = 0;
    http_append_text(out, &pos, capacity, "http://");
    http_append_text(out, &pos, capacity, base.host);
    if (base.port != 80U) {
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
    if (url->port != 80U) {
        http_append_char(dst, &pos, cap, ':');
        http_append_u32(dst, &pos, cap, url->port);
    }
    http_append_text(dst, &pos, cap,
                     "\r\nUser-Agent: LeonOS/4\r\nAccept: */*\r\n"
                     "Accept-Encoding: identity\r\nConnection: close\r\n");
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
    if (!http_starts_with_ignore_case(url_text, "http://")) {
        response->net_status = LEONOS_NET_STATUS_PROTOCOL_UNSUPPORTED;
        return 0;
    }
    if (!http_parse_url(url_text, &url)) {
        response->net_status = LEONOS_NET_STATUS_BAD_ARGUMENT;
        return 0;
    }
    request_len = http_build_request_text(request_text, sizeof(request_text),
                                          &url, request);
    if (!request_len) {
        response->net_status = LEONOS_NET_STATUS_BAD_ARGUMENT;
        return 0;
    }
    socket = leonos_socket_tcp();
    if (socket < 0) {
        response->net_status = LEONOS_NET_STATUS_SOCKET_LIMIT;
        return 0;
    }
    ret = leonos_socket_connect(socket, url.host, url.port, timeout_ms, &conn);
    if (ret < 0 || conn.status != LEONOS_NET_STATUS_OK) {
        leonos_socket_close(socket);
        response->net_status = ret < 0 ? LEONOS_NET_STATUS_TCP_FAILED
                                       : conn.status;
        return 0;
    }
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
    leonos_socket_close(socket);
    request->response_body[raw_len] = 0;
    response->net_status = net_status;
    if (raw_len + 1U >= request->response_body_capacity) {
        response->flags |= LEONOS_HTTP_FLAG_TRUNCATED;
    }
    if (net_status != LEONOS_NET_STATUS_OK) {
        return 0;
    }
    body_offset = http_find_body_offset(request->response_body, raw_len);
    if (!body_offset) {
        response->net_status = LEONOS_NET_STATUS_HTTP_FAILED;
        return 0;
    }
    header_len = body_offset;
    response->http_status = http_parse_status_code(request->response_body,
                                                   header_len);
    if (!response->http_status) {
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
    max_redirects = request->max_redirects ? request->max_redirects
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
        if (response->net_status != LEONOS_NET_STATUS_OK ||
            !http_is_redirect(response->http_status) ||
            !location[0]) {
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
