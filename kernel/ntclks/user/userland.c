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
#include <ntclks/random.h>
#include <ntclks/sched.h>
#include <ntclks/storage.h>
#include <ntclks/uts.h>
#include <ntclks/syscall.h>
#include <ntclks/lock.h>
#include <ntclks/smp.h>
#include <ntclks/userland.h>
#include <ntclks/svga.h>
#include <leonos/layout.h>
#include <ntclks/permissions.h>
#include <linux/capability.h>
#include <linux/securebits.h>
#include <linux/mount.h>

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
static uint32_t windowd_pid;
static uint32_t imd_pid;
static uint32_t tty_pid;
static bool autospawn_hello;
static bool autospawn_uidemo;
static bool autospawn_terminal;
static bool autospawn_memtest;
static bool autospawn_installer;
static bool autospawn_linuxabi;
static bool autospawn_ltp;
static bool autospawn_gcc;
static bool autospawn_vim;
/* Diagnostic-only hooks used by the Linux ABI regression ISOs.  They spawn a
 * program with fixed argv on the kernel console so the serial log captures the
 * program's own stdout, exit code and syscall trace without GUI input. */
static bool autospawn_ioctlcloexec;
static bool autospawn_python315;
static bool autospawn_inventory;
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
    return path_eq_ignore_case(path, LEONOS_LAYOUT_LEONOS_APPS "/desktop/desktop.elf");
}

/**
 * @brief Return 1 if path is the serviced.elf daemon path.
 */
static int path_is_system_service_daemon(const char *path)
{
    return path_eq_ignore_case(path, LEONOS_LAYOUT_LEONOS_APPS "/serviced/serviced.elf");
}

/**
 * @brief Return 1 if path is the userspace windowd daemon.
 */
static int path_is_windowd(const char *path)
{
    return path_eq_ignore_case(path, LEONOS_LAYOUT_LEONOS_APPS "/windowd/windowd.elf");
}

static int path_is_imd(const char *path)
{
    return path_eq_ignore_case(path, LEONOS_LAYOUT_LEONOS_APPS "/imd/imd.elf");
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
static int prepare_user_exec_stack(struct task *task, const char *execfn)
{
    uint64_t sp;
    uint64_t argc_base;
    uint64_t argv_base;
    uint64_t envp_base;
    uint64_t auxv_base;
    uint64_t random_base;
    uint64_t strings_base;
    uint64_t execfn_base;
    uint32_t argc;
    size_t path_len;
    uint64_t argv_bytes;
    uint64_t envp_bytes;
    const uint64_t auxv_count = 16;
    const uint64_t auxv_bytes = auxv_count * 2 * sizeof(uint64_t);
    enum {
        AT_NULL = 0,
        AT_PHDR = 3,
        AT_PHENT = 4,
        AT_PHNUM = 5,
        AT_PAGESZ = 6,
        AT_BASE = 7,
        AT_ENTRY = 9,
        AT_UID = 11,
        AT_EUID = 12,
        AT_GID = 13,
        AT_EGID = 14,
        AT_SECURE = 23,
        AT_RANDOM = 25,
        AT_RSEQ_FEATURE_SIZE = 27,
        AT_RSEQ_ALIGN = 28,
        AT_EXECFN = 31,
    };
    uint64_t launch_base = 0;
    if (!task) {
        return -22;
    }
    sp = task->stack_top;
    if (task->dynamic_launch.abi_major) {
        sp = (sp - sizeof(task->dynamic_launch)) & ~(EXEC_STACK_ALIGN - 1ULL);
        launch_base = sp;
    }
    argc = task->exec_argc ? task->exec_argc : 1;
    if (!execfn) execfn = task->path;
    path_len = __builtin_strlen(execfn);
    strings_base = (sp - task->exec_data_len - path_len - 2) & ~(EXEC_STACK_ALIGN - 1ULL);
    execfn_base = strings_base + task->exec_data_len;
    random_base = (strings_base - 16ULL) & ~(EXEC_STACK_ALIGN - 1ULL);
    envp_bytes = (uint64_t)(task->exec_envc + 1) * sizeof(uint64_t);
    argv_bytes = (uint64_t)(argc + 1) * sizeof(uint64_t);
    /* Align the entire vector once. Linux crt scans consecutive words. */
    argc_base = (random_base - auxv_bytes - envp_bytes - argv_bytes - sizeof(uint64_t)) &
                ~(EXEC_STACK_ALIGN - 1ULL);
    argv_base = argc_base + sizeof(uint64_t);
    envp_base = argv_base + argv_bytes;
    auxv_base = envp_base + envp_bytes;
    if (argc_base < task->stack_top - (uint64_t)NTCLKS_USER_STACK_PAGES * 4096ULL) {
        return -12;
    }

    for (uint32_t i = 0; i < task->exec_data_len; ++i) {
        char *dst = (char *)user_ptr_for_phys(sched_task_as(task), strings_base + i);
        if (!dst) {
            return -12;
        }
        *dst = task->exec_data[i];
    }

    for (size_t i = 0; i < path_len + 2; ++i) {
        char *dst = (char *)user_ptr_for_phys(sched_task_as(task), execfn_base + i);
        if (!dst) return -12;
        *dst = i < path_len ? execfn[i] : 0;
    }
    /* Linux supplies an empty argv[0] when execve receives an empty vector. */
    if (!task->exec_argc &&
        write_user_u64(sched_task_as(task), argv_base, execfn_base + path_len + 1) < 0) return -12;

    for (uint32_t i = 0; i < task->exec_argc; ++i) {
        uintptr_t ptr = (uintptr_t)task->exec_argv[i];
        uintptr_t data_begin = (uintptr_t)task->exec_data;
        uintptr_t data_end = data_begin + task->exec_data_len;
        if (!task->exec_argv[i] || ptr < data_begin || ptr >= data_end) {
            return -22;
        }
        uint64_t offset = (uint64_t)(ptr - data_begin);
        if (write_user_u64(sched_task_as(task), argv_base + (uint64_t)i * sizeof(uint64_t),
                           strings_base + offset) < 0) {
            return -12;
        }
    }
    if (write_user_u64(sched_task_as(task), argv_base + (uint64_t)argc * sizeof(uint64_t), 0) < 0) {
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
        if (write_user_u64(sched_task_as(task), envp_base + (uint64_t)i * sizeof(uint64_t),
                           strings_base + offset) < 0) {
            return -12;
        }
    }
    if (write_user_u64(sched_task_as(task), envp_base + (uint64_t)task->exec_envc * sizeof(uint64_t), 0) < 0) {
        return -12;
    }

    for (uint32_t i = 0; i < 16; ++i) {
        uint8_t *dst = (uint8_t *)user_ptr_for_phys(sched_task_as(task), random_base + i);
        if (!dst) return -12;
        *dst = task->dynamic_launch.random[i];
    }
    {
        uint64_t auxv[auxv_count * 2] = {
            AT_PHDR, task->dynamic_launch.main_phdr,
            AT_PHENT, 56,
            AT_PHNUM, task->exec_phnum,
            AT_PAGESZ, 4096,
            AT_BASE, task->dynamic_launch.interp_base,
            AT_ENTRY, task->dynamic_launch.main_entry,
            AT_UID, task->uid,
            AT_EUID, task->euid,
            AT_GID, task->gid,
            AT_EGID, task->egid,
            AT_SECURE, task->secure_exec || task->uid != task->euid || task->gid != task->egid,
            AT_RANDOM, random_base,
            AT_RSEQ_FEATURE_SIZE, 32,
            AT_RSEQ_ALIGN, 32,
            AT_EXECFN, execfn_base,
            AT_NULL, 0,
        };
        for (uint32_t i = 0; i < sizeof(auxv) / sizeof(auxv[0]); ++i) {
            if (write_user_u64(sched_task_as(task), auxv_base + (uint64_t)i * sizeof(uint64_t), auxv[i]) < 0) {
                return -12;
            }
        }
    }
    if (write_user_u64(sched_task_as(task), argc_base, argc) < 0) return -12;

    if (launch_base) {
        for (uint32_t i = 0; i < sizeof(task->dynamic_launch); ++i) {
            uint8_t *dst = (uint8_t *)user_ptr_for_phys(sched_task_as(task), launch_base + i);
            if (!dst) {
                return -12;
            }
            *dst = ((const uint8_t *)&task->dynamic_launch)[i];
        }
    }

    struct task_address_space_state *mm=sched_task_mm(task);
    mm->arg_start=task->exec_argc? strings_base+(uint64_t)(task->exec_argv[0]-task->exec_data):execfn_base+path_len+1;
    mm->arg_end=task->exec_argc? strings_base+(uint64_t)(task->exec_argv[task->exec_argc-1]-task->exec_data)+
        __builtin_strlen(task->exec_argv[task->exec_argc-1])+1:mm->arg_start+1;
    mm->env_start=task->exec_envc?strings_base+(uint64_t)(task->exec_envp[0]-task->exec_data):mm->arg_end;
    mm->env_end=task->exec_envc?strings_base+(uint64_t)(task->exec_envp[task->exec_envc-1]-task->exec_data)+
        __builtin_strlen(task->exec_envp[task->exec_envc-1])+1:mm->env_start;
    task->frame.rsp = argc_base;
    task->frame.rdi = argc;
    task->frame.rsi = argv_base;
    task->frame.rdx = envp_base;
    task->frame.r8 = launch_base;
    return 0;
}

/**
 * @brief Map or load the task's pending executable, set the entry frame and exec stack, and mark it started.
 */
static int userland_prepare_exec_credentials(struct task *next, const struct task *old)
{
    struct leonos_permissions file;
    uint64_t mount_flags;
    int ret = fs_permissions_get(next->path, &next->image_node, &file);
    if (ret < 0) return ret;
    ret = storage_node_mount_flags(&next->image_node, &mount_flags);
    if (ret < 0) return ret;
    /* bprm_fill_uid reads the held ELF inode under the execution/storage lock.
     * An interpreter's set-ID bits never replace the main ELF's authority. */
    if (!old->no_new_privs && !(mount_flags & MS_NOSUID)) {
        if ((file.mode & 06000) && !sched_task_mm(next)->executable_inode) return -95;
        if (file.mode & 04000) next->euid = file.uid;
        if ((file.mode & 02010) == 02010) next->egid = file.gid;
    }
    /* Linux commoncap.c:cap_bprm_creds_from_file, without file capabilities. */
    bool effective = !(old->securebits & SECBIT_NOROOT) && !next->euid;
    bool setid = next->euid != old->uid || next->egid != old->gid;
    uint64_t permitted = 0;
    if (!(old->securebits & SECBIT_NOROOT) && (!next->uid || !next->euid))
        permitted = old->cap_bset | old->cap_inheritable;
    if (old->no_new_privs && (setid || (permitted & ~old->cap_permitted))) {
        next->euid = next->uid;
        next->egid = next->gid;
        permitted &= old->cap_permitted;
    }
    next->suid = next->fsuid = next->euid;
    next->sgid = next->fsgid = next->egid;
    if (setid) next->cap_ambient = 0;
    next->cap_permitted = permitted | next->cap_ambient;
    next->cap_effective = effective ? next->cap_permitted : next->cap_ambient;
    next->securebits &= ~SECBIT_KEEP_CAPS;
    next->secure_exec = setid || (next->uid &&
        (effective || (next->cap_permitted & ~next->cap_ambient)));
    /* setup_new_exec resets dumpability, then commit_creds may lower it. */
    if (old->uid != old->euid || old->gid != old->egid ||
        old->euid != next->euid || old->egid != next->egid ||
        old->fsuid != next->fsuid || old->fsgid != next->fsgid ||
        (next->cap_permitted & ~old->cap_permitted))
        sched_task_mm(next)->nondumpable = true;
    return 0;
}

static int userland_load_task_image_locked(struct task *task, const struct task *old, const char *execfn)
{
    struct elf_image_info loaded;

    if (!task) {
        return -22;
    }
    if ((task->flags & TASK_FLAG_STARTED) && task->frame.rip != 0) {
        return 0;
    }
    /* A started task must have a complete saved frame. Rebuilding a zero RIP
     * from the ELF entry would hide a scheduler ownership bug and lose the
     * rest of the register state, so fail it instead of changing semantics. */
    if ((task->flags & TASK_FLAG_STARTED) && task->entry != 0 && task->frame.rip == 0) {
        console_printf("[ntclks] refusing started task with zero RIP pid=%u entry=0x%llx\n",
                       task->pid, (unsigned long long)task->entry);
        return -8;
    }
    int entropy_result = kernel_random_fill(task->dynamic_launch.random, sizeof(task->dynamic_launch.random));
    if (entropy_result < 0) return entropy_result;
    if (task->flags & TASK_FLAG_PENDING_LOAD) {
        if (task->image_node.type != LEONOS_FS_TYPE_FILE) {
            int ret = storage_lookup_path(task->path, &task->image_node);
            if (ret < 0 || task->image_node.type != LEONOS_FS_TYPE_FILE) {
                console_printf("[ntclks] executable lookup failed path=%s ret=%d\n",
                               task->path, ret < 0 ? ret : -21);
                return ret < 0 ? ret : -13;
            }
        }
        int ret = storage_inode_get(&task->image_node, &sched_task_mm(task)->executable_inode);
        if (ret < 0) return ret;
        ret = elf64_map_task_image(task, &task->image_node, &loaded);
        if (ret < 0) {
            console_printf("[ntclks] failed to map executable %s\n", task->name);
            return ret;
        }
        task->program_break_base = loaded.program_break;
        if (!task->program_break_base) {
            task->program_break_base = (loaded.high_vaddr + loaded.load_bias + 4095ULL) & ~4095ULL;
        }
        task->program_break = task->program_break_base;
        task->flags &= ~TASK_FLAG_PENDING_LOAD;
    } else if (task->image && task->image_len) {
        if (!elf64_load_address_space(sched_task_as(task), task->image, task->image_len, &loaded)) {
            console_printf("[ntclks] failed to load %s into private address space\n", task->name);
            return -8;
        }
        free_image_buffer(task->image, task->image_len);
        task->image = NULL;
        task->image_len = 0;
    } else {
        return -8;
    }

    task->entry = loaded.dynamic ? loaded.interpreter_entry : loaded.entry;
    task->exec_phnum = loaded.phnum;
    task->dynamic_launch.main_entry = loaded.entry;
    task->dynamic_launch.main_phdr = loaded.phdr_vaddr;
    task->dynamic_launch.main_base = loaded.load_bias;
    task->frame.rip = task->entry;
    task->frame.rsp = task->stack_top;
    task->frame.rflags = 0x202;
    task->frame.cs = NTCLKS_USER_CS;
    task->frame.ss = NTCLKS_USER_DS;
    if (old) {
        int ret = userland_prepare_exec_credentials(task, old);
        if (ret < 0) return ret;
    }
    int ret = prepare_user_exec_stack(task, execfn);
    if (ret < 0) {
        console_printf("[ntclks] failed to prepare argv/envp for %s\n", task->name);
        return ret;
    }
    task->flags |= TASK_FLAG_STARTED;

    console_printf("[ntclks] %s prepared lazy Ring-3 image entry=0x%llx cr3=0x%llx\n",
                   task->name,
                   (unsigned long long)task->entry,
                   (unsigned long long)(*sched_task_as(task)).cr3);
    return 0;
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
    result = userland_load_task_image_locked(task, NULL, NULL) == 0;
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
        /* The desktop is a privileged service for window-server IPC, but it
         * is also the session's identity root.  Keep the window-server bit so
         * auth_apply_session_login/sched_set_session_identity can update its
         * uid, role, home, and session along with the login task. */
        flags |= TASK_FLAG_SERVICE | TASK_FLAG_WINDOW_SERVER;
    } else if (path_is_windowd(path) || path_is_imd(path) ||
               path_is_system_service_daemon(path)) {
        flags |= TASK_FLAG_SERVICE;
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
            arch_fpu_save(current->fpu_state);
            (void)kernel_signal_deliver_pending(current, frame);
            if (current->state != TASK_EXITED && !sched_capture_current_user_frame(frame)) {
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
        uint64_t cleanup_flags;
        kernel_execution_lock_irqsave(&cleanup_flags);
        /* Stop using the retiring page tables before releasing the last mm
         * reference. Other threads retain their own reference to shared mm. */
        paging_load_cr3(paging_kernel_cr3());
        sched_quiesce_exited_current();
        sched_release_task_resources(current);
        kernel_execution_unlock_irqrestore(cleanup_flags);
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
    if (next->state == TASK_EXITED) return userland_schedule_from_frame(NULL);
    if (next->state == TASK_STOPPED || next->state == TASK_BLOCKED) {
        (void)sched_capture_current_user_frame(&next->frame);
        return userland_schedule_from_frame(NULL);
    }
    if (!userland_load_task_image(next)) {
        sched_exit(next->pid, 127);
        return userland_schedule_from_frame(NULL);
    }
    arch_set_user_fs(next->fs_base);
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
                   (unsigned long long)(*sched_task_as(task)).cr3);
    smp_mark_bsp_user_entry();
    arch_enter_user_frame(&task->frame, (*sched_task_as(task)).cr3);
}

/**
 * @brief Empty an early-boot transient tree without following links or mounts.
 * @param root Absolute directory on the root volume; no userspace exists yet.
 * @return Zero or negative errno. A failure prevents stale state being reused.
 * The path itself is the traversal stack. Directory enumeration restarts after
 * deletion because ext2 entries can coalesce. No recursion or fixed entry cap.
 */
static int userland_clear_transient_tree(const char *root)
{
    char path[LEONOS_FS_PATH_LEN];
    struct storage_node base;
    uint32_t root_length = (uint32_t)__builtin_strlen(root);
    int ret = storage_lookup_path(root, &base);
    if (ret < 0) return ret;
    if (base.type != LEONOS_FS_TYPE_DIR || !(base.flags & STORAGE_NODE_FLAG_EXT2))
        return -20;
    copy_text(path, sizeof(path), root);
    for (;;) {
        struct storage_node directory;
        struct leonos_dir_entry entry;
        uint64_t cursor = 0;
        uint32_t length = (uint32_t)__builtin_strlen(path);
        ret = storage_lookup_path(path, &directory);
        if (ret < 0) return ret;
        if (directory.volume_id != base.volume_id) return -18;
        if (directory.type != LEONOS_FS_TYPE_DIR) return -20;
        do {
            ret = storage_readdir_node(&directory, &cursor, &entry);
        } while (ret > 0 && entry.name[0] == '.' &&
                 (!entry.name[1] || (entry.name[1] == '.' && !entry.name[2])));
        if (ret < 0) return ret;
        if (ret == 0) {
            if (length == root_length) return 0;
            ret = storage_rmdir(path);
            if (ret < 0) return ret;
            while (length > root_length && path[length - 1] != '/') --length;
            path[length - 1] = 0;
            continue;
        }
        uint32_t name_length = 0;
        while (name_length < sizeof(entry.name) && entry.name[name_length]) {
            if (entry.name[name_length] == '/') return -5;
            ++name_length;
        }
        if (!name_length || name_length == sizeof(entry.name)) return -5;
        if (length + 1 + name_length >= sizeof(path)) return -36;
        path[length] = '/';
        copy_text(path + length + 1, sizeof(path) - length - 1, entry.name);
        struct storage_node child;
        ret = storage_lookup_path(path, &child);
        if (ret < 0) return ret;
        if (child.volume_id != base.volume_id) return -18;
        if (child.type == LEONOS_FS_TYPE_DIR) continue;
        ret = storage_unlink(path); /* literal link/socket/file, never its target */
        if (ret < 0) return ret;
        path[length] = 0;
    }
}

/** @brief Reset boot-scoped state before any process can open an IPC endpoint. */
static int userland_prepare_runtime(void)
{
    static const struct { const char *path; uint32_t mode; } directories[] = {
        {"/run", 0755}, {"/run/lock", 0755}, {"/run/leonos", 0755},
        {"/dev/shm", 01777},
    };
    int ret = userland_clear_transient_tree("/run");
    if (ret < 0) return ret;
    ret = userland_clear_transient_tree("/dev/shm");
    if (ret < 0) return ret;
    for (uint32_t i = 0; i < sizeof(directories) / sizeof(directories[0]); ++i) {
        struct storage_node node;
        struct leonos_permissions mode = {directories[i].mode, 0, 0};
        ret = storage_lookup_path(directories[i].path, &node);
        if (ret == -2) {
            ret = storage_mkdir(directories[i].path);
            if (ret < 0) return ret;
            ret = storage_lookup_path(directories[i].path, &node);
        }
        if (ret < 0) return ret;
        if (node.type != LEONOS_FS_TYPE_DIR) return -20;
        ret = storage_inode_permissions(&node, &mode, true);
        if (ret < 0) return ret;
    }
    ret = linux_uts_load_hostname();
    if (ret < 0)
        console_printf("[ntclks] /etc/hostname load failed ret=%d; keeping default hostname\n", ret);
    return 0;
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
    autospawn_linuxabi = boot && name_contains(boot->cmdline, "autospawn=linuxabi");
    autospawn_ltp = boot && name_contains(boot->cmdline, "autospawn=ltp");
    autospawn_gcc = boot && name_contains(boot->cmdline, "autospawn=gcc");
    autospawn_vim = boot && name_contains(boot->cmdline, "autospawn=vim");
    autospawn_ioctlcloexec = boot && name_contains(boot->cmdline, "autospawn=ioctlcloexec");
    autospawn_python315 = boot && name_contains(boot->cmdline, "autospawn=python315");
    autospawn_inventory = boot && name_contains(boot->cmdline, "autospawn=inventory");
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

    int runtime_ret = userland_prepare_runtime();
    if (runtime_ret < 0) {
        console_printf("[ntclks] runtime directory initialization failed ret=%d\n", runtime_ret);
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
        static const char *vim_argv[] = {
            "vim", "-u", "NONE", "-n", 0
        };
        static const char *advanced_envp[] = {
            "PATH=" LEONOS_DEFAULT_PATH,
            "HOME=/root", "PWD=/", "PS1=\\w \\$ ",
            "TERM=xterm-256color", "COLORTERM=truecolor", 0
        };
        struct exec_launch advanced_launch = {0};
        struct exec_launch vim_launch = {0};
        int32_t pty_id;
        if (autospawn_vim &&
            build_exec_launch(&vim_launch, "/usr/bin/vim",
                              vim_argv, 0) < 0) {
            console_printf("[ntclks] failed to prepare Linux Vim arguments\n");
            kernel_idle_loop();
        }
        if (installer_advanced &&
            build_exec_launch(&advanced_launch, "/bin/busybox",
                              advanced_argv, advanced_envp) < 0) {
            console_printf("[ntclks] failed to prepare advanced installer shell arguments\n");
            kernel_idle_loop();
        }
        pid = autospawn_vim
                  ? spawn_path_internal_deferred("/usr/bin/vim",
                                                  "vim.elf Linux binary", &vim_launch,
                                                  0, 0, 0, -1, -1, -1)
                  : installer_advanced
                  ? spawn_path_internal_deferred("/bin/busybox",
                                                  "busybox.elf installer advanced", &advanced_launch,
                                                  0, 0, 0, -1, -1, -1)
                  : spawn_path_internal_deferred(LEONOS_LAYOUT_LEONOS_APPS "/installer/installer.elf",
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
                       autospawn_vim ? "Linux Vim" :
                       (installer_advanced ? "advanced shell" : "application"),
                       tty_pid, (int)pty_id);
        sched_mark_ready(tty_pid);
        return;
    }

    if (installer_mode) {
        pid = spawn_path_internal(LEONOS_LAYOUT_LEONOS_APPS "/imd/imd.elf", "imd.elf input method",
                                  0, 0, TASK_FLAG_SERVICE, 0, -1, -1, -1);
        if (pid <= 0) {
            console_printf("[ntclks] failed to load installer imd.elf ret=%lld\n", (long long)pid);
            kernel_idle_loop();
        }
        imd_pid = (uint32_t)pid;
        pid = spawn_path_internal(LEONOS_LAYOUT_LEONOS_APPS "/windowd/windowd.elf", "windowd.elf window server",
                                  0, 0, TASK_FLAG_SERVICE, 0, -1, -1, -1);
        if (pid <= 0) {
            console_printf("[ntclks] failed to load installer windowd.elf ret=%lld\n", (long long)pid);
            kernel_idle_loop();
        }
        windowd_pid = (uint32_t)pid;
        pid = spawn_path_internal(LEONOS_LAYOUT_LEONOS_APPS "/desktop/desktop.elf", "desktop.elf shell",
                                  0, 0, TASK_FLAG_SERVICE, 0, -1, -1, -1);
        if (pid <= 0) {
            console_printf("[ntclks] failed to load installer desktop.elf ret=%lld\n", (long long)pid);
            kernel_idle_loop();
        }
        desktop_pid = (uint32_t)pid;
        console_printf("[ntclks] installer mode windowd+desktop selected\n");
        return;
    }

    pid = spawn_path_internal(LEONOS_LAYOUT_LEONOS_APPS "/init/init.elf", "init.elf", 0, 0, 0, 0, -1, -1, -1);
    if (pid <= 0) {
        console_printf("[ntclks] failed to load init.elf ret=%lld\n", (long long)pid);
        kernel_idle_loop();
    }
    init_pid = (uint32_t)pid;

    if (tty_mode) {
        static const char *tty_argv[] = {
            "login", 0
        };
        static const char *tty_envp[] = {
            "PATH=" LEONOS_DEFAULT_PATH,
            "HOME=/root", "PWD=/", "PS1=\\w \\$ ",
            "TERM=xterm-256color", "COLORTERM=truecolor", 0
        };
        struct exec_launch launch = {0};
        int32_t pty_id;
        if (build_exec_launch(&launch, LEONOS_LAYOUT_LEONOS_APPS "/login/login.elf",
                              tty_argv, tty_envp) < 0) {
            console_printf("[ntclks] failed to prepare TTY shell arguments\n");
            kernel_idle_loop();
        }
        pid = spawn_path_internal_deferred(LEONOS_LAYOUT_LEONOS_APPS "/login/login.elf", "login.elf tty",
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

    pid = spawn_path_internal(LEONOS_LAYOUT_LEONOS_APPS "/imd/imd.elf", "imd.elf input method",
                              0, init_pid, TASK_FLAG_SERVICE, 0, -1, -1, -1);
    if (pid <= 0) {
        console_printf("[ntclks] failed to load imd.elf ret=%lld\n", (long long)pid);
        kernel_idle_loop();
    }
    imd_pid = (uint32_t)pid;
    pid = spawn_path_internal(LEONOS_LAYOUT_LEONOS_APPS "/windowd/windowd.elf", "windowd.elf window server",
                              0, init_pid, TASK_FLAG_SERVICE, 0, -1, -1, -1);
    if (pid <= 0) {
        console_printf("[ntclks] failed to load windowd.elf ret=%lld\n", (long long)pid);
        kernel_idle_loop();
    }
    windowd_pid = (uint32_t)pid;
    pid = spawn_path_internal(LEONOS_LAYOUT_LEONOS_APPS "/desktop/desktop.elf", "desktop.elf shell",
                              0, init_pid, TASK_FLAG_SERVICE, 0, -1, -1, -1);
    if (pid <= 0) {
        console_printf("[ntclks] failed to load desktop.elf ret=%lld\n", (long long)pid);
        kernel_idle_loop();
    }
    desktop_pid = (uint32_t)pid;
    console_printf("[ntclks] windowd.elf + desktop.elf selected for Ring-3 GUI\n");
}

/**
 * @brief Enter the first runnable user task; falls to the idle loop if userland failed to load.
 */
void userland_enter_first(void)
{
    struct task *first;
    if (!init_pid && !desktop_pid && !windowd_pid && !imd_pid &&
        !tty_pid) {
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
 * @brief Prepare a replacement image and stack before committing exec.
 * @param path Canonical name for the executable object.
 * @param held Resolved object held stable under the execution transaction.
 * @param execfn Original exec filename for AT_EXECFN, including fd-relative names.
 * @param argc Number of entries in argv.
 * @param argv Kernel-owned argv pointers into data.
 * @param envc Number of entries in envp.
 * @param envp Kernel-owned envp pointers into data.
 * @param data Packed argument/environment storage copied from user memory.
 * @param data_len Number of valid data bytes.
 * @return Zero after committing the new image, or a negative errno-style value with no change.
 */
int userland_exec_current_node(const char *path, const struct storage_node *held, const char *execfn,
                               uint32_t argc, char *const argv[],
                               uint32_t envc, char *const envp[],
                               const char *data, uint32_t data_len)
{
    struct task *task = sched_current_task();
    struct storage_node node;
    struct task *prepared = NULL;
    char task_name[SCHED_TASK_NAME_LEN];
    uint32_t preserved_flags;
    int ret;
    if (!task || task->kind != TASK_KIND_USER || !path || !path[0]) {
        return -22;
    }
    if (task->nproc_exceeded) {
        if (sched_user_task_count(task->uid) > sched_task_limits(task)->nproc.rlim_cur) return -11;
        task->nproc_exceeded = false;
    }
    if (held) { node = *held; ret = storage_inode_refresh(&node); }
    else ret = storage_lookup_path(path, &node);
    if (ret < 0 || node.type != LEONOS_FS_TYPE_FILE) {
        return ret < 0 ? ret : -13;
    }
    ret = fs_permissions_check_node(task, path, &node, FS_ACCESS_EXEC, false);
    if (ret < 0) return ret;
    if (argc > SCHED_EXEC_ARG_MAX || envc > SCHED_EXEC_ENV_MAX || data_len > SCHED_EXEC_DATA_MAX)
        return -7;
    task_name_from_path(path, task_name, sizeof(task_name));
    prepared = kernel_malloc(sizeof(*prepared));
    if (!prepared) return -12;
    __builtin_memset(prepared, 0, sizeof(*prepared));
    prepared->credentials = task->credentials;
    prepared->limits = *sched_task_limits(task);
    prepared->stack_top = USER_STACK_TOP;
    prepared->stack_low = USER_STACK_TOP - (uint64_t)NTCLKS_USER_STACK_PAGES * 4096ULL;
    prepared->address_space.initial_stack_top = prepared->stack_top;
    prepared->address_space.initial_stack_low = prepared->stack_low;
    prepared->image_node = node;
    prepared->name = task_name;
    prepared->flags = TASK_FLAG_PENDING_LOAD;
    prepared->address_space.nondumpable = fs_permissions_check_node(task, path, &node, FS_ACCESS_READ, false) < 0;
    copy_text(prepared->path, sizeof(prepared->path), path);
    sched_copy_task_exec_params(prepared, argc, argv, envc, envp, data, data_len);
    ret = -12;
    if (!task_name[0] || !address_space_create(sched_task_as(prepared)) ||
        !address_space_map_user_stack(sched_task_as(prepared), USER_STACK_TOP)) goto failed;

    uint64_t loader_flags;
    userland_loader_lock(&loader_flags);
    ret = userland_load_task_image_locked(prepared, task, execfn);
    userland_loader_unlock(loader_flags);
    if (ret < 0) goto failed;

    ret = sched_prepare_exec_current(task);
    if (ret < 0) goto failed;
    /* The ELF mappings and initial stack are complete. Only now retire the
     * old mm, close CLOEXEC descriptors and reset process execution state. */
    svga_gpu_release_owner(task->pid);
    sched_exec_replace_mm(task, sched_task_as(prepared));
    task->address_space = prepared->address_space;
    task->credentials = prepared->credentials;
    task->entry = prepared->entry;
    task->stack_top = prepared->stack_top;
    task->stack_low = prepared->stack_low;
    task->program_break_base = prepared->program_break_base;
    task->program_break = prepared->program_break;
    task->loader_state = prepared->loader_state;
    sched_copy_task_exec_params(task, argc, argv, envc, envp, data, data_len);
    /* POSIX execve preserves ignored signals but resets caught dispositions. */
    kernel_signal_reset_handlers(task);
    task->frame = prepared->frame;
    /**
 * @brief exec replaces the image and its authority. A child of the desktop is never allowed to retain window-server/service privileges across exec.
 */
    preserved_flags = task->flags & (TASK_FLAG_ELEVATED_ADMIN | TASK_FLAG_WAITABLE_CHILD);
    if (path_is_system_service_daemon(path)) {
        preserved_flags |= TASK_FLAG_SERVICE;
    }
    task->flags = preserved_flags | TASK_FLAG_STARTED;
    copy_text(task->name_storage, sizeof(task->name_storage), task_name);
    task->name = task->name_storage;
    syscall_close_cloexec_files(task);

    arch_fpu_task_init(task->fpu_state);
    arch_fpu_restore(task->fpu_state);
    kernel_free(prepared);
    console_printf("[ntclks] exec pid=%u path=%s pty=%u committed cr3=0x%llx\n",
                   task->pid, path, task->pty_id,
                   (unsigned long long)(*sched_task_as(task)).cr3);
    return 0;
failed:
    address_space_destroy(sched_task_as(prepared));
    sched_task_vma_release(prepared);
    kernel_free(prepared);
    return ret;
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
    if (autospawn_gcc && sched_current_pid() == desktop_pid) {
        autospawn_gcc = false;
        int64_t pid = userland_spawn_path(LEONOS_LAYOUT_LEONOS_TESTS "/gcc-probe.elf");
        console_printf("[ntclks] GCC probe runner pid=%lld\n", (long long)pid);
    }
    if (autospawn_ltp && sched_current_pid() == desktop_pid) {
        autospawn_ltp = false;
        int64_t pid = userland_spawn_path(LEONOS_LAYOUT_LEONOS_TESTS "/ltp-runner.elf");
        console_printf("[ntclks] LTP musl runner pid=%lld\n", (long long)pid);
    }
    if (autospawn_linuxabi && sched_current_pid() == desktop_pid) {
        autospawn_linuxabi = false;
        int64_t dynamic_pid = userland_spawn_path(LEONOS_LAYOUT_LEONOS_TESTS "/musl-abi-dynamic.elf");
        int64_t static_pid = userland_spawn_path(LEONOS_LAYOUT_LEONOS_TESTS "/musl-abi-static.elf");
        console_printf("[ntclks] musl ABI probes dynamic=%lld static=%lld\n",
                       (long long)dynamic_pid, (long long)static_pid);
    }
    if (autospawn_hello && sched_current_pid() == desktop_pid) {
        autospawn_hello = false;
        int64_t pid = userland_spawn_path(LEONOS_LAYOUT_LEONOS_APPS "/hello/hello.elf");
        console_printf("[ntclks] debug autospawn hello pid=%lld\n", (long long)pid);
    }
    if (autospawn_uidemo && sched_current_pid() == desktop_pid) {
        autospawn_uidemo = false;
        int64_t pid = userland_spawn_path(LEONOS_LAYOUT_LEONOS_APPS "/uidemo/uidemo.elf");
        console_printf("[ntclks] debug autospawn uidemo pid=%lld\n", (long long)pid);
    }
    if (autospawn_terminal && sched_current_pid() == desktop_pid) {
        autospawn_terminal = false;
        int64_t pid = userland_spawn_path(LEONOS_LAYOUT_LEONOS_APPS "/terminal/terminal.elf");
        console_printf("[ntclks] debug autospawn terminal pid=%lld\n", (long long)pid);
    }
    if (autospawn_memtest && sched_current_pid() == desktop_pid) {
        autospawn_memtest = false;
        int64_t pid = userland_spawn_path(LEONOS_LAYOUT_LEONOS_APPS "/memtest/memtest.elf");
        console_printf("[ntclks] debug autospawn memtest pid=%lld\n", (long long)pid);
    }
    if (autospawn_installer && sched_current_pid() == desktop_pid) {
        autospawn_installer = false;
        int64_t pid = userland_spawn_path(LEONOS_LAYOUT_LEONOS_APPS "/installer/installer.elf");
        console_printf("[ntclks] installer autospawn pid=%lld\n", (long long)pid);
    }
    if (autospawn_inventory && sched_current_pid() == desktop_pid) {
        autospawn_inventory = false;
        static const char *const argv[] = {"linux-inventory", 0};
        int64_t pid = userland_spawn_path_argv(LEONOS_LAYOUT_LEONOS_TESTS "/linux-inventory.elf", argv, 0, 0);
        console_printf("[ntclks] Linux inventory regression pid=%lld\n", (long long)pid);
    }
    if (autospawn_ioctlcloexec && sched_current_pid() == desktop_pid) {
        autospawn_ioctlcloexec = false;
        /* Diagnostic hook: run the Linux ioctl close-on-exec regression on the
         * kernel console so its own stdout, exit code and syscall trace land
         * in the serial log without any GUI interaction. */
        static const char *const probe_argv[] = {"linux-ioctl-cloexec", 0};
        int64_t pid = userland_spawn_path_argv(LEONOS_LAYOUT_LEONOS_TESTS "/linux-ioctl-cloexec.elf",
                                               probe_argv, 0, 0);
        console_printf("[ntclks] ioctl CLOEXEC regression pid=%lld\n", (long long)pid);
    }
    if (autospawn_python315 && sched_current_pid() == desktop_pid) {
        autospawn_python315 = false;
        /* Diagnostic hook: the unmodified static musl CPython build with the
         * same argv a shell passes for "python3 /bin/hello.py".  The real
         * path is used so syscall-trace=<prefix> can select this binary. */
        static const char *const python_argv[] = {"python3", "/bin/hello.py", 0};
        static const char *const python_envp[] = {
            "PATH=" LEONOS_DEFAULT_PATH,
            "HOME=/root", "PWD=/", "TERM=xterm-256color", 0
        };
        int64_t pid = userland_spawn_path_argv("/opt/python/bin/python3.15", python_argv,
                                               python_envp, 0);
        console_printf("[ntclks] Python 3.15 script runner pid=%lld\n", (long long)pid);
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
