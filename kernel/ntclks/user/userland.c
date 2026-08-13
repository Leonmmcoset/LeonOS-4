/*
 * LeonOS userland launcher: prepares process images, stacks, and arguments.
 * Creates user tasks, maps executables, and enters the Ring-3 scheduler path.
 */
#include <ntclks/arch.h>
#include <ntclks/console.h>
#include <ntclks/elf.h>
#include <ntclks/kernel.h>
#include <ntclks/mm.h>
#include <ntclks/pty.h>
#include <ntclks/sched.h>
#include <ntclks/storage.h>
#include <ntclks/syscall.h>
#include <ntclks/userland.h>

#define USER_STACK_TOP (NTCLKS_USER_TOP - 0x1000ULL)
#define EXEC_STACK_ALIGN 16ULL

struct exec_launch {
    uint32_t argc;
    uint32_t envc;
    uint32_t data_len;
    const char *argv[SCHED_EXEC_ARG_MAX + 1];
    const char *envp[SCHED_EXEC_ENV_MAX + 1];
    char data[SCHED_EXEC_DATA_MAX];
};

static uint32_t init_pid;
static uint32_t desktop_pid;
static bool autospawn_hello;
static bool autospawn_uidemo;
static bool autospawn_terminal;
static bool autospawn_memtest;
static bool autospawn_installer;

/**
 * @brief Coordinates the name contains operation.
 * @param name Input or output value used by this operation.
 * @param needle Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
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

/**
 * @brief Coordinates the path eq operation.
 * @param a Input or output value used by this operation.
 * @param b Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
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

/**
 * @brief Coordinates the ascii tolower operation.
 * @param ch Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static char ascii_tolower(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

/**
 * @brief Coordinates the path eq ignore case operation.
 * @param a Input or output value used by this operation.
 * @param b Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int path_eq_ignore_case(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b && ascii_tolower(*a) == ascii_tolower(*b)) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

/**
 * @brief Copies text.
 * @param dst Input or output value used by this operation.
 * @param dst_len Length, size, or element count associated with the operation.
 * @param src Input or output value used by this operation.
 */
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

/**
 * @brief Coordinates the task name from path operation.
 * @param path LeonOS path consumed by this operation.
 * @param dst Input or output value used by this operation.
 * @param dst_len Length, size, or element count associated with the operation.
 */
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

/**
 * @brief Coordinates the path is system desktop operation.
 * @param path LeonOS path consumed by this operation.
 * @return Result, status, or value defined by this API.
 */
static int path_is_system_desktop(const char *path)
{
    return path_eq_ignore_case(path, "0:/system/apps/desktop/desktop.elf");
}

/**
 * @brief Coordinates the path is system service daemon operation.
 * @param path LeonOS path consumed by this operation.
 * @return Result, status, or value defined by this API.
 */
static int path_is_system_service_daemon(const char *path)
{
    return path_eq_ignore_case(path, "0:/system/apps/serviced/serviced.elf");
}

/**
 * @brief Coordinates the dir add operation.
 * @param entries Input or output value used by this operation.
 * @param capacity Capacity, in elements or bytes, of the related output buffer.
 * @param count Length, size, or element count associated with the operation.
 * @param type Input or output value used by this operation.
 * @param name Input or output value used by this operation.
 */
static void dir_add(struct leonos_dir_entry *entries, uint32_t capacity, uint32_t *count,
                    uint32_t type, const char *name)
{
    if (*count < capacity && entries) {
        entries[*count].type = type;
        copy_text(entries[*count].name, sizeof(entries[*count].name), name);
    }
    ++(*count);
}

/**
 * @brief Releases image buffer.
 * @param image Input or output value used by this operation.
 * @param image_len Length, size, or element count associated with the operation.
 */
static void free_image_buffer(const void *image, size_t image_len)
{
    uint32_t pages;
    if (!image || !image_len) {
        return;
    }
    pages = (uint32_t)((image_len + 4095ULL) / 4096ULL);
    mm_free_pages((uint64_t)(uintptr_t)image, pages);
}

/**
 * @brief Copies exec vector.
 * @param dst Input or output value used by this operation.
 * @param dst_cap Capacity, in elements or bytes, of the related output buffer.
 * @param src Input or output value used by this operation.
 * @param data Input or output value used by this operation.
 * @param data_cap Capacity, in elements or bytes, of the related output buffer.
 * @param data_len Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
 */
static uint32_t copy_exec_vector(char *dst[], uint32_t dst_cap,
                                 const char *const src[], char *data,
                                 uint32_t data_cap, uint32_t *data_len)
{
    uint32_t count = 0;
    if (!dst || !dst_cap || !data || !data_len) {
        return 0;
    }
    if (src) {
        while (src[count] && count + 1 < dst_cap) {
            uint32_t start = *data_len;
            uint32_t len = 0;
            while (src[count][len]) {
                if (*data_len + len + 1 >= data_cap) {
                    return 0xffffffffu;
                }
                data[*data_len + len] = src[count][len];
                ++len;
            }
            data[*data_len + len] = 0;
            dst[count] = data + start;
            *data_len += len + 1;
            ++count;
        }
        if (src[count]) {
            return 0xffffffffu;
        }
    }
    dst[count] = 0;
    return count;
}

/**
 * @brief Coordinates the build exec launch operation.
 * @param launch Input or output value used by this operation.
 * @param path LeonOS path consumed by this operation.
 * @param argv Input or output value used by this operation.
 * @param envp Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int build_exec_launch(struct exec_launch *launch, const char *path,
                             const char *const argv[], const char *const envp[])
{
    uint32_t data_len = 0;
    uint32_t argc;
    uint32_t envc;
    if (!launch || !path || !path[0]) {
        return -22;
    }
    for (uint32_t i = 0; i < SCHED_EXEC_ARG_MAX + 1; ++i) {
        launch->argv[i] = 0;
    }
    for (uint32_t i = 0; i < SCHED_EXEC_ENV_MAX + 1; ++i) {
        launch->envp[i] = 0;
    }
    if (!argv || !argv[0]) {
        argv = (const char *const[]){path, 0};
    }
    argc = copy_exec_vector((char **)launch->argv, SCHED_EXEC_ARG_MAX + 1,
                            argv, launch->data, sizeof(launch->data), &data_len);
    if (argc == 0xffffffffu) {
        return -7;
    }
    envc = copy_exec_vector((char **)launch->envp, SCHED_EXEC_ENV_MAX + 1,
                            envp, launch->data, sizeof(launch->data), &data_len);
    if (envc == 0xffffffffu) {
        return -7;
    }
    launch->argc = argc;
    launch->envc = envc;
    launch->data_len = data_len;
    return 0;
}

/**
 * @brief Coordinates the user ptr for phys operation.
 * @param as Input or output value used by this operation.
 * @param vaddr Address used by this operation; its address-space interpretation follows the API.
 * @return Result, status, or value defined by this API.
 */
static void *user_ptr_for_phys(const struct address_space *as, uint64_t vaddr)
{
    uint64_t phys = address_space_user_page_phys(as, vaddr);
    if (!phys) {
        return 0;
    }
    return (void *)(uintptr_t)(phys + (vaddr & 0xfffULL));
}

/**
 * @brief Writes user u64.
 * @param as Input or output value used by this operation.
 * @param vaddr Address used by this operation; its address-space interpretation follows the API.
 * @param value Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int write_user_u64(const struct address_space *as, uint64_t vaddr, uint64_t value)
{
    for (uint32_t i = 0; i < sizeof(value); ++i) {
        uint8_t *dst = (uint8_t *)user_ptr_for_phys(as, vaddr + i);
        if (!dst) {
            return -12;
        }
        *dst = (uint8_t)((value >> (i * 8)) & 0xffu);
    }
    return 0;
}

/**
 * @brief Coordinates the prepare user exec stack operation.
 * @param task Task whose state or authority is inspected or updated.
 * @return Result, status, or value defined by this API.
 */
static int prepare_user_exec_stack(struct task *task)
{
    uint64_t sp;
    uint64_t argv_base;
    uint64_t envp_base;
    uint64_t strings_base;
    uint64_t argv_bytes;
    uint64_t envp_bytes;
    uint64_t launch_base = 0;
    if (!task) {
        return -22;
    }
    sp = task->stack_top;
    if (task->dynamic_launch.abi_major) {
        sp = (sp - sizeof(task->dynamic_launch)) & ~(EXEC_STACK_ALIGN - 1ULL);
        launch_base = sp;
    }
    strings_base = (sp - task->exec_data_len) & ~(EXEC_STACK_ALIGN - 1ULL);
    argv_bytes = (uint64_t)(task->exec_argc + 1) * sizeof(uint64_t);
    argv_base = (strings_base - argv_bytes) & ~(EXEC_STACK_ALIGN - 1ULL);
    envp_bytes = (uint64_t)(task->exec_envc + 1) * sizeof(uint64_t);
    envp_base = (argv_base - envp_bytes) & ~(EXEC_STACK_ALIGN - 1ULL);
    if (envp_base < task->stack_top - (uint64_t)NTCLKS_USER_STACK_PAGES * 4096ULL) {
        return -12;
    }

    for (uint32_t i = 0; i < task->exec_data_len; ++i) {
        char *dst = (char *)user_ptr_for_phys(&task->as, strings_base + i);
        if (!dst) {
            return -12;
        }
        *dst = task->exec_data[i];
    }

    for (uint32_t i = 0; i < task->exec_argc; ++i) {
        uint64_t offset = (uint64_t)(uintptr_t)task->exec_argv[i] - (uint64_t)(uintptr_t)task->exec_data;
        if (write_user_u64(&task->as, argv_base + (uint64_t)i * sizeof(uint64_t),
                           strings_base + offset) < 0) {
            return -12;
        }
    }
    if (write_user_u64(&task->as, argv_base + (uint64_t)task->exec_argc * sizeof(uint64_t), 0) < 0) {
        return -12;
    }
    for (uint32_t i = 0; i < task->exec_envc; ++i) {
        uint64_t offset = (uint64_t)(uintptr_t)task->exec_envp[i] - (uint64_t)(uintptr_t)task->exec_data;
        if (write_user_u64(&task->as, envp_base + (uint64_t)i * sizeof(uint64_t),
                           strings_base + offset) < 0) {
            return -12;
        }
    }
    if (write_user_u64(&task->as, envp_base + (uint64_t)task->exec_envc * sizeof(uint64_t), 0) < 0) {
        return -12;
    }

    if (launch_base) {
        for (uint32_t i = 0; i < sizeof(task->dynamic_launch); ++i) {
            uint8_t *dst = (uint8_t *)user_ptr_for_phys(&task->as, launch_base + i);
            if (!dst) {
                return -12;
            }
            *dst = ((const uint8_t *)&task->dynamic_launch)[i];
        }
    }

    task->frame.rsp = envp_base;
    task->frame.rdi = task->exec_argc;
    task->frame.rsi = argv_base;
    task->frame.rdx = envp_base;
    task->frame.r8 = launch_base;
    return 0;
}

/**
 * @brief Coordinates the userland load task image operation.
 * @param task Task whose state or authority is inspected or updated.
 * @return Result, status, or value defined by this API.
 */
static bool userland_load_task_image(struct task *task)
{
    struct elf_image_info loaded;

    if (!task || (task->flags & TASK_FLAG_STARTED)) {
        return task != NULL;
    }
    if (task->flags & TASK_FLAG_PENDING_LOAD) {
        if (task->image_node.type != LEONOS_FS_TYPE_FILE) {
            int ret = storage_lookup_path(task->path, &task->image_node);
            if (ret < 0 || task->image_node.type != LEONOS_FS_TYPE_FILE) {
                console_printf("[ntclks] executable lookup failed path=%s ret=%d\n",
                               task->path, ret < 0 ? ret : -21);
                return false;
            }
        }
        if (!elf64_map_task_image(task, &task->image_node, &loaded)) {
            console_printf("[ntclks] failed to map executable %s\n", task->name);
            return false;
        }
        task->flags &= ~TASK_FLAG_PENDING_LOAD;
    } else if (task->image && task->image_len) {
        if (!elf64_load_address_space(&task->as, task->image, task->image_len, &loaded)) {
            console_printf("[ntclks] failed to load %s into private address space\n", task->name);
            return false;
        }
        free_image_buffer(task->image, task->image_len);
        task->image = NULL;
        task->image_len = 0;
    } else {
        return false;
    }

    task->entry = loaded.dynamic ? loaded.interpreter_entry : loaded.entry;
    task->frame.rip = task->entry;
    task->frame.rsp = task->stack_top;
    task->frame.rflags = 0x202;
    task->frame.cs = NTCLKS_USER_CS;
    task->frame.ss = NTCLKS_USER_DS;
    if (prepare_user_exec_stack(task) < 0) {
        console_printf("[ntclks] failed to prepare argv/envp for %s\n", task->name);
        return false;
    }
    task->flags |= TASK_FLAG_STARTED;

    console_printf("[ntclks] %s prepared lazy Ring-3 image entry=0x%llx cr3=0x%llx\n",
                   task->name,
                   (unsigned long long)task->entry,
                   (unsigned long long)task->as.cr3);
    return true;
}

/**
 * @brief Starts pending image.
 * @param path LeonOS path consumed by this operation.
 * @param task_name Input or output value used by this operation.
 * @param node Input or output value used by this operation.
 * @param launch Input or output value used by this operation.
 * @param parent Input or output value used by this operation.
 * @param flags Input or output value used by this operation.
 * @param pty_id Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int64_t spawn_pending_image(const char *path, const char *task_name,
                                   const struct storage_node *node,
                                   const struct exec_launch *launch,
                                   uint32_t parent, uint32_t flags, uint32_t pty_id)
{
    uint32_t pid = sched_create_user_task(task_name, 0, USER_STACK_TOP, parent, flags);
    if (!pid) {
        return -12;
    }
    sched_set_task_path(pid, path);
    sched_set_task_image_node(pid, node);
    if (launch) {
        sched_set_task_exec_params(pid, launch->argc, (char *const *)launch->argv,
                                   launch->envc, (char *const *)launch->envp,
                                   launch->data, launch->data_len);
    }
    if (pty_id) {
        struct task *task = sched_find(pid);
        if (task) {
            task->pty_id = pty_id;
        }
    }
    return pid;
}

/**
 * @brief Starts path internal.
 * @param path LeonOS path consumed by this operation.
 * @param task_name Input or output value used by this operation.
 * @param launch Input or output value used by this operation.
 * @param parent Input or output value used by this operation.
 * @param flags Input or output value used by this operation.
 * @param pty_id Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int64_t spawn_path_internal(const char *path, const char *task_name,
                                   const struct exec_launch *launch,
                                   uint32_t parent, uint32_t flags, uint32_t pty_id)
{
    struct storage_node node;
    int ret;
    if (path_is_system_desktop(path)) {
        flags |= TASK_FLAG_SERVICE | TASK_FLAG_WINDOW_SERVER;
    } else if (path_is_system_service_daemon(path)) {
        flags |= TASK_FLAG_SERVICE;
    }
    if ((flags & TASK_FLAG_WINDOW_SERVER) && sched_find_window_server()) {
        console_printf("[ntclks] refusing second desktop instance path=%s\n", path);
        return -LEONOS_EEXIST;
    }
    if (path_is_system_service_daemon(path) && sched_find_by_path(path)) {
        console_printf("[ntclks] refusing second service daemon path=%s\n", path);
        return -LEONOS_EEXIST;
    }
    if (flags & TASK_FLAG_SERVICE) {
        ret = storage_lookup_path(path, &node);
        if (ret < 0 || node.type != LEONOS_FS_TYPE_FILE) {
            console_printf("[ntclks] spawn lookup failed path=%s ret=%d\n", path,
                           ret < 0 ? ret : -21);
            return ret < 0 ? ret : -21;
        }
        return spawn_pending_image(path, task_name, &node, launch, parent, flags, pty_id);
    }
    return spawn_pending_image(path, task_name, NULL, launch, parent, flags, pty_id);
}

/**
 * @brief Waits for for runnable task.
 */
static void wait_for_runnable_task(void)
{
    while (!sched_select_next_user()) {
        __asm__ volatile("sti; hlt; cli");
    }
}

/**
 * @brief Coordinates the userland schedule from frame operation.
 * @param frame Trap or syscall frame supplied by the architecture layer.
 * @return Result, status, or value defined by this API.
 */
struct task *userland_schedule_from_frame(struct trap_frame *frame)
{
    struct task *current = sched_current_task();
    if (current && current->kind == TASK_KIND_USER && frame) {
        current->frame = *frame;
        /* The kernel is integer-only, so the incoming user FPU state is intact. */
        arch_fpu_save(current->fpu_state);
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
    userland_yield_if_runnable();
    arch_fpu_restore(next->fpu_state);
    return next;
}

static void userland_enter_task(struct task *task) __attribute__((noreturn));

/**
 * @brief Coordinates the userland enter task operation.
 * @param task Task whose state or authority is inspected or updated.
 */
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

/**
 * @brief Coordinates the userland init operation.
 * @param boot Boot information supplied by the loader.
 */
void userland_init(const struct boot_info *boot)
{
    int64_t pid;

    console_printf("[ntclks] userland storage load started modules=%u\n",
                   boot ? boot->module_count : 0);
    autospawn_hello = boot && name_contains(boot->cmdline, "autospawn=hello");
    autospawn_uidemo = boot && name_contains(boot->cmdline, "autospawn=uidemo");
    autospawn_terminal = boot && name_contains(boot->cmdline, "autospawn=terminal");
    autospawn_memtest = boot && name_contains(boot->cmdline, "autospawn=memtest");
    autospawn_installer = boot && name_contains(boot->cmdline, "autospawn=installer");
    if (autospawn_hello) {
        console_printf("[ntclks] debug autospawn hello enabled\n");
    }
    if (autospawn_uidemo) {
        console_printf("[ntclks] debug autospawn uidemo enabled\n");
    }
    if (autospawn_terminal) {
        console_printf("[ntclks] debug autospawn terminal enabled\n");
    }
    if (autospawn_memtest) {
        console_printf("[ntclks] debug autospawn memtest enabled\n");
    }
    if (autospawn_installer) {
        console_printf("[ntclks] installer autospawn enabled\n");
    }

    if (!storage_ready()) {
        console_printf("[ntclks] no block-backed FAT32 filesystem available for userland\n");
        kernel_idle_loop();
    }

    if (boot && name_contains(boot->cmdline, "mode=installer")) {
        pid = spawn_path_internal("0:/system/apps/desktop/desktop.elf", "desktop.elf window server",
                                  0, 0, TASK_FLAG_SERVICE | TASK_FLAG_WINDOW_SERVER, 0);
        if (pid <= 0) {
            console_printf("[ntclks] failed to load installer desktop.elf ret=%lld\n", (long long)pid);
            kernel_idle_loop();
        }
        desktop_pid = (uint32_t)pid;
        console_printf("[ntclks] installer mode desktop.elf window server selected\n");
        return;
    }

    pid = spawn_path_internal("0:/system/apps/init/init.elf", "init.elf", 0, 0, 0, 0);
    if (pid <= 0) {
        console_printf("[ntclks] failed to load init.elf ret=%lld\n", (long long)pid);
        kernel_idle_loop();
    }
    init_pid = (uint32_t)pid;

    pid = spawn_path_internal("0:/system/apps/desktop/desktop.elf", "desktop.elf window server",
                              0, init_pid, TASK_FLAG_SERVICE | TASK_FLAG_WINDOW_SERVER, 0);
    if (pid <= 0) {
        console_printf("[ntclks] failed to load desktop.elf ret=%lld\n", (long long)pid);
        kernel_idle_loop();
    }
    desktop_pid = (uint32_t)pid;
    console_printf("[ntclks] desktop.elf window server selected for Ring-3 GUI\n");
}

/**
 * @brief Coordinates the userland enter first operation.
 */
void userland_enter_first(void)
{
    if (!init_pid && !desktop_pid) {
        console_printf("[ntclks] no Ring-3 userland loaded\n");
        kernel_idle_loop();
    }
    userland_enter_task(userland_schedule_from_frame(NULL));
}

/**
 * @brief Coordinates the userland process exit operation.
 * @param code Input or output value used by this operation.
 */
void userland_process_exit(uint64_t code)
{
    uint32_t pid = sched_current_pid();
    console_printf("[ntclks] Ring-3 pid=%u exited code=%llu\n",
                   pid,
                   (unsigned long long)code);
    sched_exit(pid, code);
}

/**
 * @brief Coordinates the userland spawn path argv operation.
 * @param path LeonOS path consumed by this operation.
 * @param argv Input or output value used by this operation.
 * @param envp Input or output value used by this operation.
 * @param pty_id Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int64_t userland_spawn_path_argv(const char *path,
                                 const char *const argv[],
                                 const char *const envp[],
                                 uint32_t pty_id)
{
    char task_name[SCHED_TASK_NAME_LEN];
    struct exec_launch launch;
    uint32_t parent = sched_current_pid();
    int64_t pid;
    int ret;

    if (!path || !path[0]) {
        return -22;
    }
    task_name_from_path(path, task_name, sizeof(task_name));
    if (!task_name[0]) {
        return -22;
    }

    ret = build_exec_launch(&launch, path, argv, envp);
    if (ret < 0) {
        return ret;
    }

    pid = spawn_path_internal(path, task_name, &launch, parent, 0, pty_id);
    if (pid > 0) {
        console_printf("[ntclks] spawn path=%s pid=%u parent=%u\n",
                       path,
                       (unsigned)pid,
                       parent);
    }
    return pid;
}

/**
 * @brief Coordinates the userland spawn path argv for user operation.
 * @param path LeonOS path consumed by this operation.
 * @param argv Input or output value used by this operation.
 * @param envp Input or output value used by this operation.
 * @param parent_pid Input or output value used by this operation.
 * @param user Input or output value used by this operation.
 * @param session_id Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int64_t userland_spawn_path_argv_for_user(const char *path,
                                          const char *const argv[],
                                          const char *const envp[],
                                          uint32_t parent_pid,
                                          const struct leonos_user_info *user,
                                          uint32_t session_id)
{
    char task_name[SCHED_TASK_NAME_LEN];
    struct exec_launch launch;
    int64_t pid;
    int ret;

    if (!path || !path[0] || !user || !user->uid || !session_id) {
        return -22;
    }
    task_name_from_path(path, task_name, sizeof(task_name));
    if (!task_name[0]) {
        return -22;
    }
    ret = build_exec_launch(&launch, path, argv, envp);
    if (ret < 0) {
        return ret;
    }
    pid = spawn_path_internal(path, task_name, &launch, parent_pid, 0, 0);
    if (pid > 0) {
        sched_set_task_identity((uint32_t)pid, user, session_id);
        console_printf("[ntclks] spawn trusted path=%s pid=%u parent=%u user=%u\n",
                       path, (unsigned)pid, parent_pid, user->uid);
    }
    return pid;
}

/**
 * @brief Coordinates the userland spawn path with pty operation.
 * @param path LeonOS path consumed by this operation.
 * @param pty_id Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int64_t userland_spawn_path_with_pty(const char *path, uint32_t pty_id)
{
    return userland_spawn_path_argv(path, 0, 0, pty_id);
}

/**
 * @brief Coordinates the userland spawn path operation.
 * @param path LeonOS path consumed by this operation.
 * @return Result, status, or value defined by this API.
 */
int64_t userland_spawn_path(const char *path)
{
    return userland_spawn_path_with_pty(path, 0);
}

/**
 * @brief Coordinates the userland yield if runnable operation.
 */
void userland_yield_if_runnable(void)
{
    if (autospawn_hello && sched_current_pid() == desktop_pid) {
        autospawn_hello = false;
        int64_t pid = userland_spawn_path("0:/programs/hello/hello.elf");
        console_printf("[ntclks] debug autospawn hello pid=%lld\n", (long long)pid);
    }
    if (autospawn_uidemo && sched_current_pid() == desktop_pid) {
        autospawn_uidemo = false;
        int64_t pid = userland_spawn_path("0:/programs/uidemo/uidemo.elf");
        console_printf("[ntclks] debug autospawn uidemo pid=%lld\n", (long long)pid);
    }
    if (autospawn_terminal && sched_current_pid() == desktop_pid) {
        autospawn_terminal = false;
        int64_t pid = userland_spawn_path("0:/system/apps/terminal/terminal.elf");
        console_printf("[ntclks] debug autospawn terminal pid=%lld\n", (long long)pid);
    }
    if (autospawn_memtest && sched_current_pid() == desktop_pid) {
        autospawn_memtest = false;
        int64_t pid = userland_spawn_path("0:/programs/memtest/memtest.elf");
        console_printf("[ntclks] debug autospawn memtest pid=%lld\n", (long long)pid);
    }
    if (autospawn_installer && sched_current_pid() == desktop_pid) {
        autospawn_installer = false;
        int64_t pid = userland_spawn_path("0:/system/apps/installer/installer.elf");
        console_printf("[ntclks] installer autospawn pid=%lld\n", (long long)pid);
    }
}

/**
 * @brief Coordinates the userland list dir operation.
 * @param path LeonOS path consumed by this operation.
 * @param entries Input or output value used by this operation.
 * @param capacity Capacity, in elements or bytes, of the related output buffer.
 * @param out_count Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
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

    ret = storage_list_dir(path, entries, capacity, &count);
    *out_count = count;
    return ret;
}
