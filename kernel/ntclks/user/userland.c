#include <ntclks/arch.h>
#include <ntclks/console.h>
#include <ntclks/efi_fs.h>
#include <ntclks/elf.h>
#include <ntclks/kernel.h>
#include <ntclks/mm.h>
#include <ntclks/pty.h>
#include <ntclks/sched.h>
#include <ntclks/userland.h>

#define USER_STACK_TOP 0x0000000000fff000ULL

static uint32_t init_pid;
static uint32_t desktop_pid;
static bool autospawn_hello;
static bool autospawn_uidemo;
static bool autospawn_terminal;

static int name_contains(const char *name, const char *needle)
{
    if (!name || !needle) {
        return 0;
    }
    for (const char *p = name; *p; ++p) {
        const char *a = p;
        const char *b = needle;
        while (*a && *b && *a == *b) {
            ++a;
            ++b;
        }
        if (*b == 0) {
            return 1;
        }
    }
    return 0;
}

static int path_eq(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static void copy_text(char *dst, uint32_t dst_len, const char *src)
{
    uint32_t i = 0;
    if (!dst || dst_len == 0) {
        return;
    }
    if (src) {
        while (i + 1 < dst_len && src[i]) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = 0;
}

static void task_name_from_path(const char *path, char *dst, uint32_t dst_len)
{
    const char *name = path;
    if (!path) {
        copy_text(dst, dst_len, "");
        return;
    }
    for (const char *p = path; *p; ++p) {
        if (*p == '/') {
            name = p + 1;
        }
    }
    copy_text(dst, dst_len, name);
}

static void dir_add(struct leonos_dir_entry *entries, uint32_t capacity, uint32_t *count,
                    uint32_t type, const char *name)
{
    if (*count < capacity && entries) {
        entries[*count].type = type;
        copy_text(entries[*count].name, sizeof(entries[*count].name), name);
    }
    ++(*count);
}

static void free_image_buffer(const void *image, size_t image_len)
{
    uint32_t pages;
    if (!image || !image_len) {
        return;
    }
    pages = (uint32_t)((image_len + 4095ULL) / 4096ULL);
    mm_free_pages((uint64_t)(uintptr_t)image, pages);
}

static bool userland_load_task_image(struct task *task)
{
    struct elf_image_info loaded;

    if (!task || (task->flags & TASK_FLAG_STARTED)) {
        return task != NULL;
    }
    if (!task->image || !task->image_len) {
        return false;
    }
    if (!elf64_load_address_space(&task->as, task->image, task->image_len, &loaded)) {
        console_printf("[ntclks] failed to load %s into private address space\n", task->name);
        return false;
    }

    task->entry = loaded.entry;
    task->frame.rip = loaded.entry;
    task->frame.rsp = task->stack_top;
    task->frame.rflags = 0x202;
    task->frame.cs = NTCLKS_USER_CS;
    task->frame.ss = NTCLKS_USER_DS;
    free_image_buffer(task->image, task->image_len);
    task->image = NULL;
    task->image_len = 0;
    task->flags |= TASK_FLAG_STARTED;

    console_printf("[ntclks] %s mapped private Ring-3 image entry=0x%llx cr3=0x%llx\n",
                   task->name,
                   (unsigned long long)task->entry,
                   (unsigned long long)task->as.cr3);
    return true;
}

static int64_t spawn_loaded_image(const char *task_name, const void *image, size_t image_len,
                                  uint32_t parent, uint32_t flags, uint32_t pty_id)
{
    uint32_t pid = sched_create_user_task(task_name, 0, USER_STACK_TOP, parent, flags);
    if (!pid) {
        free_image_buffer(image, image_len);
        return -12;
    }
    sched_set_task_image(pid, image, image_len);
    if (pty_id) {
        struct task *task = sched_find(pid);
        if (task) {
            task->pty_id = pty_id;
        }
    }
    return pid;
}

static int64_t spawn_path_internal(const char *path, const char *task_name,
                                   uint32_t parent, uint32_t flags, uint32_t pty_id)
{
    const void *image = NULL;
    size_t image_len = 0;
    int ret = efi_fs_read_file(path, &image, &image_len);
    if (ret < 0) {
        console_printf("[ntclks] spawn read failed path=%s ret=%d\n", path, ret);
        return ret;
    }
    return spawn_loaded_image(task_name, image, image_len, parent, flags, pty_id);
}

static void wait_for_runnable_task(void)
{
    while (!sched_select_next_user()) {
        __asm__ volatile("sti; hlt; cli");
    }
}

struct task *userland_schedule_from_frame(struct trap_frame *frame)
{
    struct task *current = sched_current_task();
    if (current && current->kind == TASK_KIND_USER && frame) {
        current->frame = *frame;
        if (current->state == TASK_RUNNING) {
            current->state = TASK_READY;
        }
    }

    struct task *next = sched_select_next_user();
    if (!next && current && current->kind == TASK_KIND_USER && current->state == TASK_READY) {
        next = current;
    }
    if (!next) {
        wait_for_runnable_task();
        next = sched_select_next_user();
    }
    if (!next) {
        return NULL;
    }
    if (!userland_load_task_image(next)) {
        sched_exit(next->pid, 127);
        return userland_schedule_from_frame(NULL);
    }
    sched_set_running(next->pid);
    return next;
}

static void userland_enter_task(struct task *task) __attribute__((noreturn));

static void userland_enter_task(struct task *task)
{
    if (!task) {
        console_printf("[ntclks] no runnable Ring-3 task, entering idle\n");
        kernel_idle_loop();
    }
    console_printf("[ntclks] scheduler entering pid=%u name=%s rip=0x%llx rsp=0x%llx cr3=0x%llx\n",
                   task->pid,
                   task->name,
                   (unsigned long long)task->frame.rip,
                   (unsigned long long)task->frame.rsp,
                   (unsigned long long)task->as.cr3);
    arch_enter_user_frame(&task->frame, task->as.cr3);
}

void userland_init(const struct boot_info *boot)
{
    int64_t pid;

    console_printf("[ntclks] userland FAT32 load started modules=%u\n",
                   boot ? boot->module_count : 0);
    autospawn_hello = boot && name_contains(boot->cmdline, "autospawn=hello");
    autospawn_uidemo = boot && name_contains(boot->cmdline, "autospawn=uidemo");
    autospawn_terminal = boot && name_contains(boot->cmdline, "autospawn=terminal");
    if (autospawn_hello) {
        console_printf("[ntclks] debug autospawn hello enabled\n");
    }
    if (autospawn_uidemo) {
        console_printf("[ntclks] debug autospawn uidemo enabled\n");
    }
    if (autospawn_terminal) {
        console_printf("[ntclks] debug autospawn terminal enabled\n");
    }

    efi_fs_init(boot ? boot->efi_system_table : 0);
    if (!efi_fs_ready()) {
        console_printf("[ntclks] no EFI FAT32 filesystem available for userland\n");
        kernel_idle_loop();
    }

    pid = spawn_path_internal("0:/userland/init.elf", "init.elf", 0, 0, 0);
    if (pid <= 0) {
        console_printf("[ntclks] failed to load init.elf ret=%lld\n", (long long)pid);
        kernel_idle_loop();
    }
    init_pid = (uint32_t)pid;

    pid = spawn_path_internal("0:/userland/desktop.elf", "desktop.elf window server",
                              init_pid, TASK_FLAG_SERVICE, 0);
    if (pid <= 0) {
        console_printf("[ntclks] failed to load desktop.elf ret=%lld\n", (long long)pid);
        kernel_idle_loop();
    }
    desktop_pid = (uint32_t)pid;
    console_printf("[ntclks] desktop.elf window server selected for Ring-3 GUI\n");
}

void userland_enter_first(void)
{
    if (!init_pid) {
        console_printf("[ntclks] no Ring-3 init.elf loaded\n");
        kernel_idle_loop();
    }
    userland_enter_task(userland_schedule_from_frame(NULL));
}

void userland_process_exit(uint64_t code)
{
    uint32_t pid = sched_current_pid();
    console_printf("[ntclks] Ring-3 pid=%u exited code=%llu\n",
                   pid,
                   (unsigned long long)code);
    sched_exit(pid, code);
}

int64_t userland_spawn_path_with_pty(const char *path, uint32_t pty_id)
{
    char task_name[SCHED_TASK_NAME_LEN];
    uint32_t parent = sched_current_pid();
    int64_t pid;

    if (!path || !path[0]) {
        return -22;
    }
    task_name_from_path(path, task_name, sizeof(task_name));
    if (!task_name[0]) {
        return -22;
    }

    pid = spawn_path_internal(path, task_name, parent, 0, pty_id);
    if (pid > 0) {
        console_printf("[ntclks] spawn path=%s pid=%u parent=%u\n",
                       path,
                       (unsigned)pid,
                       parent);
    }
    return pid;
}

int64_t userland_spawn_path(const char *path)
{
    return userland_spawn_path_with_pty(path, 0);
}

void userland_yield_if_runnable(void)
{
    if (autospawn_hello && sched_current_pid() == desktop_pid) {
        autospawn_hello = false;
        int64_t pid = userland_spawn_path("0:/userland/hello.elf");
        console_printf("[ntclks] debug autospawn hello pid=%lld\n", (long long)pid);
    }
    if (autospawn_uidemo && sched_current_pid() == desktop_pid) {
        autospawn_uidemo = false;
        int64_t pid = userland_spawn_path("0:/userland/uidemo.elf");
        console_printf("[ntclks] debug autospawn uidemo pid=%lld\n", (long long)pid);
    }
    if (autospawn_terminal && sched_current_pid() == desktop_pid) {
        autospawn_terminal = false;
        int64_t pid = userland_spawn_path("0:/userland/terminal.elf");
        console_printf("[ntclks] debug autospawn terminal pid=%lld\n", (long long)pid);
    }
}

int userland_list_dir(const char *path, struct leonos_dir_entry *entries,
                      uint32_t capacity, uint32_t *out_count)
{
    uint32_t count = 0;
    int ret;

    if (!path || !out_count) {
        return -22;
    }
    if (capacity > LEONOS_FS_MAX_ENTRIES) {
        capacity = LEONOS_FS_MAX_ENTRIES;
    }

    if (path_eq(path, "0:/dev")) {
        dir_add(entries, capacity, &count, LEONOS_FS_TYPE_DEVICE, "fb0");
        *out_count = count;
        return (int)count;
    }

    ret = efi_fs_list_dir(path, entries, capacity, &count);
    if (ret < 0) {
        *out_count = 0;
        return ret;
    }
    if (path_eq(path, "0:/") && count < capacity) {
        if (entries) {
            entries[count].type = LEONOS_FS_TYPE_DIR;
            copy_text(entries[count].name, sizeof(entries[count].name), "dev");
        }
        ++count;
    }
    if (count > capacity) {
        count = capacity;
    }
    *out_count = count;
    return (int)count;
}
