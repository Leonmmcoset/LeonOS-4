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
#include <ntclks/lock.h>
#include <ntclks/smp.h>
#include <ntclks/userland.h>
#include <ntclks/svga.h>

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
static uint32_t tty_pid;
static bool autospawn_hello;
static bool autospawn_uidemo;
static bool autospawn_terminal;
static bool autospawn_memtest;
static bool autospawn_installer;
/* elf.c keeps a bounded header scratch buffer and ASLR state at file scope.
 * Serialize lazy image construction so APs cannot overwrite that state while
 * the BSP (or another AP) is mapping a different executable. */
static struct kernel_spinlock image_load_lock = KERNEL_SPINLOCK_INIT;

void userland_loader_lock(uint64_t *flags)
{
    /* ELF header probing uses one scratch buffer, but loading may perform
     * storage IO. Preserve interrupt delivery while waiting for the scratch
     * buffer so AP timers continue to account progress. */
    __asm__ volatile("pushfq; popq %0" : "=r"(*flags) : : "memory");
    kernel_spin_lock(&image_load_lock);
}

void userland_loader_unlock(uint64_t flags)
{
    kernel_spin_unlock(&image_load_lock);
    kernel_irq_restore(flags);
}

/**
 * @brief Return 1 if name contains the substring needle, else 0.
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
 * @brief Return 1 if the two NUL-terminated strings are exactly equal, else 0.
 */
/**
 * @brief Lowercase an ASCII letter, otherwise return the character unchanged.
 */
static char ascii_tolower(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

/**
 * @brief Case-insensitive ASCII comparison of two paths; returns 1 on an exact match.
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
 * @brief Copy src into dst up to dst_len-1 chars, always NUL-terminating dst.
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
 * @brief Clears the virtual-memory-area metadata belonging to an old process image.
 * @param task Task whose replacement address space has no mappings represented by its VMA table.
 */
static void clear_task_vmas(struct task *task)
{
    if (!task) {
        return;
    }
    for (uint32_t i = 0; i < SCHED_TASK_VMA_MAX; ++i) {
        task->vmas[i] = (struct task_vma){0};
    }
    sched_task_vma_release(task);
}

/**
 * @brief Copy the final path component (after the last '/') into dst.
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
 * @brief Return 1 if path is the desktop.elf window-server path.
 */
static int path_is_system_desktop(const char *path)
{
    return path_eq_ignore_case(path, "/system/apps/desktop/desktop.elf");
}

/**
 * @brief Return 1 if path is the serviced.elf daemon path.
 */
static int path_is_system_service_daemon(const char *path)
{
    return path_eq_ignore_case(path, "/system/apps/serviced/serviced.elf");
}

/**
 * @brief Append one directory entry (type + name) to entries if it fits; always increments *count.
 */
/**
 * @brief Free the page-backed buffer allocated for a loaded executable image.
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
 * @brief Pack a NUL-terminated argv/envp array into the shared data buffer and record pointers into dst; returns the entry count, or 0xffffffff on overflow.
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
 * @brief Populate launch with the packed argv/envp for path; returns 0, or a negative errno (-22 no path, -7 overflow).
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
 * @brief Return a kernel pointer to the physical page backing vaddr, or NULL if unmapped.
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
 * @brief Write value into the address space at vaddr byte by byte; returns 0, or -12 on an unmapped page.
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
 * @brief Lay out argv/envp (and the dynamic-launch record) on the task's user stack and point the initial frame at them.
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
        uintptr_t ptr = (uintptr_t)task->exec_argv[i];
        uintptr_t data_begin = (uintptr_t)task->exec_data;
        uintptr_t data_end = data_begin + task->exec_data_len;
        if (!task->exec_argv[i] || ptr < data_begin || ptr >= data_end) {
            return -22;
        }
        uint64_t offset = (uint64_t)(ptr - data_begin);
        if (write_user_u64(&task->as, argv_base + (uint64_t)i * sizeof(uint64_t),
                           strings_base + offset) < 0) {
            return -12;
        }
    }
    if (write_user_u64(&task->as, argv_base + (uint64_t)task->exec_argc * sizeof(uint64_t), 0) < 0) {
        return -12;
    }
    for (uint32_t i = 0; i < task->exec_envc; ++i) {
        uintptr_t ptr = (uintptr_t)task->exec_envp[i];
        uintptr_t data_begin = (uintptr_t)task->exec_data;
        uintptr_t data_end = data_begin + task->exec_data_len;
        if (!task->exec_envp[i] || ptr < data_begin || ptr >= data_end) {
            return -22;
        }
        uint64_t offset = (uint64_t)(ptr - data_begin);
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
 * @brief Map or load the task's pending executable, set the entry frame and exec stack, and mark it started.
 */
static bool userland_load_task_image_locked(struct task *task)
{
    struct elf_image_info loaded;

    if (!task) {
        return false;
    }
    if ((task->flags & TASK_FLAG_STARTED) && task->frame.rip != 0) {
        return true;
    }
    /* A started task must have a complete saved frame. Rebuilding a zero RIP
     * from the ELF entry would hide a scheduler ownership bug and lose the
     * rest of the register state, so fail it instead of changing semantics. */
    if ((task->flags & TASK_FLAG_STARTED) && task->entry != 0 && task->frame.rip == 0) {
        console_printf("[ntclks] refusing started task with zero RIP pid=%u entry=0x%llx\n",
                       task->pid, (unsigned long long)task->entry);
        return false;
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

static bool userland_load_task_image(struct task *task)
{
    bool result;
    uint64_t execution_flags;
    uint64_t loader_flags;
    /* Keep the lock order consistent with page-fault lazy mapping:
     * execution transaction -> ELF scratch lock -> storage.  Taking the ELF
     * lock first here used to deadlock with a CPU handling a file-backed page
     * fault: the scheduler-side loader then waited for storage's execution
     * lock while the fault path already owned that lock and waited for the
     * ELF scratch buffer.  The execution lock is reentrant, so storage calls
     * below safely join this transaction. */
    kernel_execution_lock_irqsave(&execution_flags);
    userland_loader_lock(&loader_flags);
    result = userland_load_task_image_locked(task);
    userland_loader_unlock(loader_flags);
    kernel_execution_unlock_irqrestore(execution_flags);
    return result;
}

/**
 * @brief Create the user task for path, attach image/exec/fd parameters, and return its PID or a negative errno.
 */
static int64_t spawn_pending_image_ex(const char *path, const char *task_name,
                                      const struct storage_node *node,
                                      const struct exec_launch *launch,
                                      uint32_t parent, uint32_t flags, uint32_t pty_id,
                                      int stdin_fd, int stdout_fd, int stderr_fd,
                                      int make_ready)
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
    if (stdin_fd >= 0 || stdout_fd >= 0 || stderr_fd >= 0) {
        struct task *child = sched_find(pid);
        struct task *parent_task = sched_find(parent);
        if (!child || syscall_inherit_task_fds(parent_task, child,
                                               stdin_fd, stdout_fd, stderr_fd) < 0) {
            sched_exit(pid, 127);
            return -9;
        }
    }
    /* Make the task runnable only after every field used by the loader and
     * scheduler has been published.  Bootstrap callers may defer this one
     * final transition while they attach the controlling PTY. */
    if (make_ready) {
        sched_mark_ready(pid);
    }
    return pid;
}

/**
 * @brief Tag desktop/service-daemon paths with their flags, reject duplicate instances, then create the task.
 */
static int64_t spawn_path_internal_ex(const char *path, const char *task_name,
                                      const struct exec_launch *launch,
                                      uint32_t parent, uint32_t flags, uint32_t pty_id,
                                      int stdin_fd, int stdout_fd, int stderr_fd,
                                      int make_ready)
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
        return spawn_pending_image_ex(path, task_name, &node, launch, parent, flags, pty_id,
                                      stdin_fd, stdout_fd, stderr_fd, make_ready);
    }
    return spawn_pending_image_ex(path, task_name, NULL, launch, parent, flags, pty_id,
                                  stdin_fd, stdout_fd, stderr_fd, make_ready);
}

static int64_t spawn_path_internal(const char *path, const char *task_name,
                                   const struct exec_launch *launch,
                                   uint32_t parent, uint32_t flags, uint32_t pty_id,
                                   int stdin_fd, int stdout_fd, int stderr_fd)
{
    return spawn_path_internal_ex(path, task_name, launch, parent, flags, pty_id,
                                  stdin_fd, stdout_fd, stderr_fd, 1);
}

static int64_t spawn_path_internal_deferred(const char *path, const char *task_name,
                                            const struct exec_launch *launch,
                                            uint32_t parent, uint32_t flags, uint32_t pty_id,
                                            int stdin_fd, int stdout_fd, int stderr_fd)
{
    return spawn_path_internal_ex(path, task_name, launch, parent, flags, pty_id,
                                  stdin_fd, stdout_fd, stderr_fd, 0);
}

/**
 * @brief Halt with interrupts enabled until a user task is ready to run.
 */
static struct task *wait_for_runnable_task(void)
{
    struct task *next;
    while (!(next = sched_select_next_user())) {
        __asm__ volatile("sti; hlt; cli");
    }
    return next;
}

/**
 * @brief Save the current task's frame/FPU, pick and prepare the next user task, and return it (or NULL).
 */
struct task *userland_schedule_from_frame(struct trap_frame *frame)
{
    struct task *current = sched_current_task();
    if (current && current->kind == TASK_KIND_USER && frame &&
        current->state != TASK_EXITED) {
        /* A malformed interrupt frame must not erase a live task context.
         * RIP=0 is never a valid LeonOS user entry and would make the next
         * iretq fault while fetching its first instruction. */
        if (frame->rip != 0 && (frame->cs & 3ULL) == 3ULL) {
            /* Install a pending user signal handler on this live return
             * frame before it is published to the scheduler. */
            (void)kernel_signal_deliver_pending(current, frame);
            arch_fpu_save(current->fpu_state);
            if (!sched_capture_current_user_frame(frame)) {
                console_printf("[ntclks] rejected scheduler frame pid=%u rip=0x%llx cs=0x%llx\n",
                               current->pid,
                               (unsigned long long)frame->rip,
                               (unsigned long long)frame->cs);
                return NULL;
            }
        } else {
            console_printf("[ntclks] ignored invalid scheduler frame pid=%u rip=0x%llx cs=0x%llx\n",
                           current->pid,
                           (unsigned long long)frame->rip,
                           (unsigned long long)frame->cs);
            return NULL;
        }
    }
    if (current && current->kind == TASK_KIND_USER && current->state == TASK_EXITED) {
        sched_quiesce_exited_current();
    }

    struct task *next = sched_select_next_user();
    if (!next && current && current->kind == TASK_KIND_USER && current->state == TASK_READY) {
        next = sched_reclaim_current_user();
    }
    if (!next) {
        next = wait_for_runnable_task();
    }
    if (!next) {
        return NULL;
    }
    /* A task that was woken for a pending signal never passed through the
     * live-frame path above; prepare its saved frame before entering it. */
    (void)kernel_signal_deliver_pending(next, &next->frame);
    if (!userland_load_task_image(next)) {
        sched_exit(next->pid, 127);
        return userland_schedule_from_frame(NULL);
    }
    userland_yield_if_runnable();
    arch_fpu_restore(next->fpu_state);
    return next;
}

static void userland_enter_task(struct task *task) __attribute__((noreturn));

/**
 * @brief Log and enter the task's user frame at its CR3; falls into the idle loop if none.
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
    smp_mark_bsp_user_entry();
    arch_enter_user_frame(&task->frame, task->as.cr3);
}

/**
 * @brief Parse autospawn cmdline flags, then seed the installer desktop or the
 * normal init plus the configured desktop/TTY interface.
 */
void userland_init(const struct boot_info *boot)
{
    int64_t pid;
    int tty_mode;
    int installer_mode;
    int installer_advanced;

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
        console_printf("[ntclks] no block-backed root filesystem available for userland\n");
        kernel_idle_loop();
    }

    installer_mode = boot && name_contains(boot->cmdline, "mode=installer");
    installer_advanced = boot && name_contains(boot->cmdline, "installer_advanced=1");

#ifdef CONFIG_STARTUP_TTY
    tty_mode = 1;
#else
    tty_mode = 0;
#endif
    if (boot && name_contains(boot->cmdline, "startup=tty")) {
        tty_mode = 1;
    } else if (boot && name_contains(boot->cmdline, "startup=desktop")) {
        tty_mode = 0;
    }

    if (installer_mode && tty_mode) {
        static const char *advanced_argv[] = {
            "busybox", "sh", 0
        };
        static const char *advanced_envp[] = {
            "PATH=/programs/busybox:/bin:/sbin:/usr/bin:/usr/sbin",
            "HOME=/root", "PWD=/", "PS1=\\w \\$ ",
            "TERM=xterm-256color", "COLORTERM=truecolor", 0
        };
        struct exec_launch advanced_launch = {0};
        int32_t pty_id;
        if (installer_advanced &&
            build_exec_launch(&advanced_launch, "/programs/busybox/busybox.elf",
                              advanced_argv, advanced_envp) < 0) {
            console_printf("[ntclks] failed to prepare advanced installer shell arguments\n");
            kernel_idle_loop();
        }
        pid = installer_advanced
                  ? spawn_path_internal_deferred("/programs/busybox/busybox.elf",
                                                  "busybox.elf installer advanced", &advanced_launch,
                                                  0, 0, 0, -1, -1, -1)
                  : spawn_path_internal_deferred("/system/apps/installer/installer.elf",
                                                  "installer.elf tty", 0, 0, 0, 0, -1, -1, -1);
        if (pid <= 0) {
            console_printf("[ntclks] failed to load installer TTY environment ret=%lld\n",
                           (long long)pid);
            kernel_idle_loop();
        }
        tty_pid = (uint32_t)pid;
        pty_id = pty_create(tty_pid);
        if (pty_id <= 0 || pty_bind_console((uint32_t)pty_id, tty_pid) < 0) {
            console_printf("[ntclks] failed to bind installer console PTY ret=%d\n",
                           (int)pty_id);
            sched_exit(tty_pid, 127);
            tty_pid = 0;
            kernel_idle_loop();
        }
        console_printf("[ntclks] installer %s TTY selected; pid=%u pty=%d\n",
                       installer_advanced ? "advanced shell" : "application",
                       tty_pid, (int)pty_id);
        sched_mark_ready(tty_pid);
        return;
    }

    if (installer_mode) {
        pid = spawn_path_internal("/system/apps/desktop/desktop.elf", "desktop.elf window server",
                                  0, 0, TASK_FLAG_SERVICE | TASK_FLAG_WINDOW_SERVER, 0, -1, -1, -1);
        if (pid <= 0) {
            console_printf("[ntclks] failed to load installer desktop.elf ret=%lld\n", (long long)pid);
            kernel_idle_loop();
        }
        desktop_pid = (uint32_t)pid;
        console_printf("[ntclks] installer mode desktop.elf window server selected\n");
        return;
    }

    pid = spawn_path_internal("/system/apps/init/init.elf", "init.elf", 0, 0, 0, 0, -1, -1, -1);
    if (pid <= 0) {
        console_printf("[ntclks] failed to load init.elf ret=%lld\n", (long long)pid);
        kernel_idle_loop();
    }
    init_pid = (uint32_t)pid;

    if (tty_mode) {
        static const char *tty_argv[] = {
            "busybox", "sh", "-c",
            "/system/apps/oobe/oobe.elf; /system/apps/login/login.elf; exec /programs/busybox/busybox.elf sh", 0
        };
        static const char *tty_envp[] = {
            "PATH=/programs/busybox:/bin:/sbin:/usr/bin:/usr/sbin",
            "HOME=/root", "PWD=/", "PS1=\\w \\$ ",
            "TERM=xterm-256color", "COLORTERM=truecolor", 0
        };
        struct exec_launch launch = {0};
        int32_t pty_id;
        if (build_exec_launch(&launch, "/programs/busybox/busybox.elf",
                              tty_argv, tty_envp) < 0) {
            console_printf("[ntclks] failed to prepare TTY shell arguments\n");
            kernel_idle_loop();
        }
        pid = spawn_path_internal_deferred("/programs/busybox/busybox.elf", "busybox.elf tty",
                                          &launch, init_pid, 0, 0, -1, -1, -1);
        if (pid <= 0) {
            console_printf("[ntclks] failed to load busybox.elf for TTY ret=%lld\n",
                           (long long)pid);
            kernel_idle_loop();
        }
        tty_pid = (uint32_t)pid;
        pty_id = pty_create(tty_pid);
        if (pty_id <= 0 || pty_bind_console((uint32_t)pty_id, tty_pid) < 0) {
            console_printf("[ntclks] failed to bind console PTY ret=%d\n", (int)pty_id);
            sched_exit(tty_pid, 127);
            tty_pid = 0;
            kernel_idle_loop();
        }
        console_printf("[ntclks] TTY startup selected; busybox shell pid=%u pty=%d\n",
                       tty_pid, (int)pty_id);
        sched_mark_ready(tty_pid);
        return;
    }

    pid = spawn_path_internal("/system/apps/desktop/desktop.elf", "desktop.elf window server",
                              0, init_pid, TASK_FLAG_SERVICE | TASK_FLAG_WINDOW_SERVER, 0, -1, -1, -1);
    if (pid <= 0) {
        console_printf("[ntclks] failed to load desktop.elf ret=%lld\n", (long long)pid);
        kernel_idle_loop();
    }
    desktop_pid = (uint32_t)pid;
    console_printf("[ntclks] desktop.elf window server selected for Ring-3 GUI\n");
}

/**
 * @brief Enter the first runnable user task; falls to the idle loop if userland failed to load.
 */
void userland_enter_first(void)
{
    struct task *first;
    if (!init_pid && !desktop_pid && !tty_pid) {
        console_printf("[ntclks] no Ring-3 userland loaded\n");
        kernel_idle_loop();
    }
    /* Load and reserve the first task on the BSP before releasing APs. This
     * avoids concurrent lazy ELF mapping and gives every AP a stable initial
     * task table/address space to observe. */
    first = userland_schedule_from_frame(NULL);
    userland_enter_task(first);
}

/**
 * @brief Log and terminate the current task with the given exit code.
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
 * @brief Replaces the calling task's user address space with a pending executable image.
 * @param path Canonical executable path already authorized by the syscall layer.
 * @param argc Number of entries in argv.
 * @param argv Kernel-owned argv pointers into data.
 * @param envc Number of entries in envp.
 * @param envp Kernel-owned envp pointers into data.
 * @param data Packed argument/environment storage copied from user memory.
 * @param data_len Number of valid data bytes.
 * @return Zero after committing the new image, or a negative errno-style value with no change.
 */
int userland_exec_current_path(const char *path, uint32_t argc, char *const argv[],
                               uint32_t envc, char *const envp[],
                               const char *data, uint32_t data_len)
{
    struct task *task = sched_current_task();
    struct storage_node node;
    struct address_space replacement = {0};
    struct address_space old_as;
    char task_name[SCHED_TASK_NAME_LEN];
    uint32_t preserved_flags;
    int ret;
    if (!task || task->kind != TASK_KIND_USER || !path || !path[0]) {
        return -22;
    }
    ret = storage_lookup_path(path, &node);
    if (ret < 0 || node.type != LEONOS_FS_TYPE_FILE) {
        return ret < 0 ? ret : -2;
    }
    task_name_from_path(path, task_name, sizeof(task_name));
    if (!task_name[0] || !address_space_create(&replacement) ||
        !address_space_map_user_stack(&replacement, USER_STACK_TOP)) {
        address_space_destroy(&replacement);
        return -12;
    }

    /* No operation after this point can fail.  Keep all old process identity,
     * cwd, PTY association, limits, process parentage and waitability intact. */
    svga_gpu_release_owner(task->pid);
    old_as = task->as;
    task->as = replacement;
    task->entry = 0;
    task->stack_top = USER_STACK_TOP;
    task->stack_low = USER_STACK_TOP - (uint64_t)NTCLKS_USER_STACK_PAGES * 4096ULL;
    task->image = NULL;
    task->image_len = 0;
    task->image_node = node;
    /**
 * @brief A fork child inherits its parent's VMA records. Its replacement page tables are blank, so retaining those records would make the ELF mapper reject valid PIE ranges as overlaps with the discarded image.
 */
    clear_task_vmas(task);
    /* POSIX execve preserves ignored signals but resets caught dispositions. */
    kernel_signal_reset_handlers(task);
    task->frame = (struct trap_frame){0};
    task->frame.cs = NTCLKS_USER_CS;
    task->frame.ss = NTCLKS_USER_DS;
    task->frame.rflags = 0x202;
    task->frame.rsp = USER_STACK_TOP;
    task->dynamic_launch = (struct leonos_dynamic_launch){0};
    /**
 * @brief exec replaces the image and its authority. A child of the desktop is never allowed to retain window-server/service privileges across exec.
 */
    preserved_flags = task->flags & (TASK_FLAG_ELEVATED_ADMIN | TASK_FLAG_WAITABLE_CHILD);
    if (path_is_system_service_daemon(path)) {
        preserved_flags |= TASK_FLAG_SERVICE;
    }
    task->flags = preserved_flags | TASK_FLAG_PENDING_LOAD;
    copy_text(task->name_storage, sizeof(task->name_storage), task_name);
    task->name = task->name_storage;
    copy_text(task->path, sizeof(task->path), path);
    sched_set_task_exec_params(task->pid, argc, argv, envc, envp, data, data_len);
    syscall_close_cloexec_files(task);

    /* The int 0x80 handler is still executing with the old process CR3 at
     * this point.  Freeing that page-table tree while it is active is a
     * use-after-free: on SMP another CPU can immediately reuse a released
     * table page, corrupting this CPU's instruction/stack translation before
     * the interrupt return path installs the replacement CR3.  Continue the
     * kernel half of exec on the permanent kernel address space first.  The
     * scheduler will install task->as.cr3 when it next returns to Ring 3. */
    paging_load_cr3(paging_kernel_cr3());
    address_space_destroy(&old_as);
    arch_fpu_task_init(task->fpu_state);
    console_printf("[ntclks] exec pid=%u path=%s pty=%u pending cr3=0x%llx\n",
                   task->pid, path, task->pty_id,
                   (unsigned long long)task->as.cr3);
    return 0;
}

/**
 * @brief Build the exec vector and spawn path as a child of the current task; returns the child PID or a negative errno.
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

    pid = spawn_path_internal(path, task_name, &launch, parent, 0, pty_id, -1, -1, -1);
    if (pid > 0) {
        console_printf("[ntclks] spawn path=%s pid=%u parent=%u\n",
                       path,
                       (unsigned)pid,
                       parent);
    }
    return pid;
}

/**
 * @brief Spawns a user executable with explicitly inherited standard streams.
 * @param path NUL-terminated executable path in LeonOS Unix syntax.
 * @param argv Optional NUL-terminated argument vector copied into the child.
 * @param envp Optional NUL-terminated environment vector copied into the child.
 * @param pty_id Active PTY inherited by the child; it must belong to the caller.
 * @param stdin_fd Caller file descriptor used as the child's standard input.
 * @param stdout_fd Caller file descriptor used as the child's standard output.
 * @param stderr_fd Caller file descriptor used as the child's standard error.
 * @return Positive child PID on success, or a negative errno value on failure.
 */
int64_t userland_spawn_path_argv_with_fds(const char *path,
                                          const char *const argv[],
                                          const char *const envp[],
                                          uint32_t pty_id,
                                          int stdin_fd, int stdout_fd,
                                          int stderr_fd)
{
    char task_name[SCHED_TASK_NAME_LEN];
    struct exec_launch launch;
    uint32_t parent = sched_current_pid();
    int64_t pid;
    int ret;
    if (!path || !path[0]) return -22;
    task_name_from_path(path, task_name, sizeof(task_name));
    ret = build_exec_launch(&launch, path, argv, envp);
    if (ret < 0) return ret;
    pid = spawn_path_internal(path, task_name, &launch, parent, 0, pty_id,
                              stdin_fd, stdout_fd, stderr_fd);
    if (pid > 0) {
        console_printf("[ntclks] spawn path=%s pid=%u parent=%u fds=%d,%d,%d\n",
                       path, (unsigned)pid, parent, stdin_fd, stdout_fd, stderr_fd);
    }
    return pid;
}

/**
 * @brief Spawn path as a child of parent_pid and tag it with the given user identity/session; returns the PID or a negative errno.
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
    pid = spawn_path_internal(path, task_name, &launch, parent_pid, 0, 0, -1, -1, -1);
    if (pid > 0) {
        sched_set_task_identity((uint32_t)pid, user, session_id);
        console_printf("[ntclks] spawn trusted path=%s pid=%u parent=%u user=%u\n",
                       path, (unsigned)pid, parent_pid, user->uid);
    }
    return pid;
}

/**
 * @brief Spawn path attached to pty_id with no argv/envp.
 */
int64_t userland_spawn_path_with_pty(const char *path, uint32_t pty_id)
{
    return userland_spawn_path_argv(path, 0, 0, pty_id);
}

/**
 * @brief Spawn path with no argv/envp or PTY.
 */
int64_t userland_spawn_path(const char *path)
{
    return userland_spawn_path_with_pty(path, 0);
}

/**
 * @brief One-shot autospawn: launch each requested debug/installer program when the desktop first runs.
 */
void userland_yield_if_runnable(void)
{
    if (autospawn_hello && sched_current_pid() == desktop_pid) {
        autospawn_hello = false;
        int64_t pid = userland_spawn_path("/programs/hello/hello.elf");
        console_printf("[ntclks] debug autospawn hello pid=%lld\n", (long long)pid);
    }
    if (autospawn_uidemo && sched_current_pid() == desktop_pid) {
        autospawn_uidemo = false;
        int64_t pid = userland_spawn_path("/programs/uidemo/uidemo.elf");
        console_printf("[ntclks] debug autospawn uidemo pid=%lld\n", (long long)pid);
    }
    if (autospawn_terminal && sched_current_pid() == desktop_pid) {
        autospawn_terminal = false;
        int64_t pid = userland_spawn_path("/system/apps/terminal/terminal.elf");
        console_printf("[ntclks] debug autospawn terminal pid=%lld\n", (long long)pid);
    }
    if (autospawn_memtest && sched_current_pid() == desktop_pid) {
        autospawn_memtest = false;
        int64_t pid = userland_spawn_path("/programs/memtest/memtest.elf");
        console_printf("[ntclks] debug autospawn memtest pid=%lld\n", (long long)pid);
    }
    if (autospawn_installer && sched_current_pid() == desktop_pid) {
        autospawn_installer = false;
        int64_t pid = userland_spawn_path("/system/apps/installer/installer.elf");
        console_printf("[ntclks] installer autospawn pid=%lld\n", (long long)pid);
    }
}

/**
 * @brief List directory entries into entries (special-casing /dev) and set *out_count; returns count or a negative errno.
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

    ret = storage_list_dir(path, entries, capacity, &count);
    *out_count = count;
    return ret;
}
