/* GPU device-UAPI syscall boundary. Historical GUI ioctls were removed in
 * favour of the windowd AF_UNIX protocol. */
#include <ntclks/gpu.h>
#include <ntclks/sched.h>
#include <ntclks/storage.h>
#include <ntclks/syscall.h>
#include <ntclks/syscall_internal.h>

int syscall_gui_owns(uint64_t number, uint64_t a1)
{
    return number == LINUX_SYS_IOCTL && syscall_gpu_owns(a1);
}

int64_t syscall_gui_dispatch(uint64_t number, uint64_t a0, uint64_t a1,
                             uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a2; (void)a3; (void)a4; (void)a5;
    if (number != LINUX_SYS_IOCTL || !syscall_gpu_owns(a1)) {
        return -LEONOS_ENOSYS;
    }
    {
        struct task *task = sched_current_task();
        struct task_file *file = task_file_for_fd(task, (int)a0);
        if ((uint64_t)a0 != 3 &&
            !(file && (file->flags & TASK_FILE_FLAG_DEV_NODE) &&
              file->node.first_cluster == STORAGE_DEV_KIND_GPU)) {
            return -LEONOS_ENOTTY;
        }
        return syscall_gpu_dispatch(a1, a2);
    }
}
