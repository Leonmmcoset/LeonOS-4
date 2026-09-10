/* Internal syscall helpers shared by the syscall category translation units. */
#ifndef NTCLKS_SYSCALL_INTERNAL_H
#define NTCLKS_SYSCALL_INTERNAL_H

void task_socket_unlink_path(const char *path);
void task_socket_rename_path(const char *old_path, const char *new_path);
struct task;
struct task_file;
struct task_file *task_descriptor_for_fd(struct task *task, int fd);
int task_allocate_fd(struct task *task, int minimum, struct task_file **slot);
void task_discard_file_fd(struct task *task, int fd);
int task_file_reference(struct task_file *destination, struct task_file *source);
struct task_file *task_file_get(struct task_file *source);
void task_file_put(struct task_file *description);
struct task_file *task_file_for_io(struct task *task, int fd);

#include <ntclks/sched.h>

/** @brief Execute native SysV semaphore calls under the kernel execution lock. */
int64_t syscall_sysv_sem(uint64_t number, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3);
/** @brief Cancel an in-flight semaphore operation without applying persistent undo records. */
void task_sysv_sem_cancel(struct task *task);
/** @brief Check whether completion or timeout precedes signal delivery. */
bool task_sysv_sem_ready(const struct task *task);

/** @brief Execute native SysV message queue operations under the execution lock. */
int64_t syscall_sysv_msg(uint64_t number, uint64_t a0, uint64_t a1,
                         uint64_t a2, uint64_t a3, uint64_t a4);
/** @brief Release a task's pending SysV operation on completion, signal, exec or exit. */
void task_sysv_msg_cancel(struct task *task);

/** @brief Execute process_vm_readv/writev against a task pinned by the execution lock. */
int64_t syscall_process_vm(int32_t pid, uint64_t local, uint64_t local_count,
                           uint64_t remote, uint64_t remote_count, uint64_t flags, bool write);

int64_t syscall_signalfd(int32_t fd, uint64_t mask, uint64_t size, uint32_t flags);
short task_signalfd_poll(struct task *task, const struct task_file *file);
int64_t task_signalfd_read(struct task *task, struct task_file *file, uint64_t buffer, uint64_t count);
int64_t task_signalfd_readv(struct task *task, struct task_file *file, uint64_t vectors,
                           uint64_t count, uint32_t flags);

struct socket_message_result {
    uint64_t requested;
    uint32_t flags;
};
/** @brief Run one socket message with captured length/flags for batch iteration. */
int64_t task_socket_message(struct task *task, struct task_file *file, uint64_t message,
                            uint32_t flags, bool receiving, bool batch,
                            struct socket_message_result *result);
/** @brief Read and clear, or set, a socket's deferred receive error. */
int task_socket_message_error(struct task_file *file, int error, bool setting);
/** @brief Execute native sendmmsg/recvmmsg, retaining progress across internal blocking. */
int64_t syscall_socket_mmsg(bool receiving, int fd, uint64_t vector,
                            uint32_t length, uint32_t flags, uint64_t timeout);
/** @brief Complete an interrupted message batch using the target task's address space. */
int64_t task_socket_mmsg_interrupt(struct task *task, uint64_t received);

int64_t syscall_nanosleep(int32_t clock, uint32_t flags, uint64_t request, uint64_t remaining);
int64_t syscall_itimer(bool setting, int32_t which, uint64_t value, uint64_t old_value);
int64_t syscall_timer_create(uint64_t clockid, uint64_t sevp, uint64_t timerid);
int64_t syscall_timer_delete(uint64_t timerid);
int64_t syscall_timer_settime(uint64_t timerid, uint64_t flags, uint64_t value, uint64_t old_value);
int64_t syscall_timer_gettime(uint64_t timerid, uint64_t value);
int64_t syscall_timer_getoverrun(uint64_t timerid);
int64_t syscall_rt_sigtimedwait(uint64_t mask, uint64_t info, uint64_t timeout, uint64_t sigset_size);

#define TASK_PIPE_CAP 4096u

struct task_file *task_file_for_fd(struct task *task, int fd);
int file_can_read(const struct task_file *file);
int file_can_write(const struct task_file *file);
int storage_errno(int ret);
void task_pipe_retain(struct task_file *file);
void task_pipe_release(struct task_file *file);
int task_pipe_read(struct task_file *file, void *buffer, uint32_t length);
int task_pipe_write(struct task_file *file, const void *buffer, uint32_t length);
short task_pipe_poll(const struct task_file *file, short events);
int task_inet_read(struct task_file *file, void *buffer, uint32_t length);
int task_inet_write(struct task_file *file, const void *buffer, uint32_t length);
short task_inet_poll(const struct task_file *file, short events);
void task_inet_retain(struct task_file *file);
void task_inet_release(struct task_file *file);
void task_socket_retain(struct task_file *file);
void task_socket_collect(void);
void task_socket_release(struct task_file *file);
int task_socket_read(struct task_file *file, void *buffer, uint32_t length);
int task_socket_write(struct task_file *file, const void *buffer, uint32_t length);
struct iovec;
int task_socket_vector(struct task_file *file, const struct iovec *vectors,
                       uint32_t count, uint64_t total, bool writing);
short task_socket_poll(const struct task_file *file, short events);
/**
 * @brief Handle Unix socket ioctls with native usercopy and error semantics.
 * @param file Retained Unix socket open file description.
 * @param request Linux ioctl number.
 * @param address User result pointer.
 * @return Zero or a negative Linux errno.
 */
int task_socket_ioctl(struct task_file *file, uint64_t request, uint64_t address);
int64_t syscall_socket_dispatch(uint64_t number, uint64_t a0, uint64_t a1,
                                uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
int syscall_ipc_pipe(uint64_t user_ptr);
int syscall_ipc_pipe2(uint64_t user_ptr, uint64_t flags);
void task_shm_retain(struct task_file *file);
void task_shm_release(struct task_file *file);
int task_shm_attach(struct task_file *file);
int task_shm_truncate(struct task_file *file, uint64_t size);
int task_shm_read(struct task_file *file, void *buffer, uint32_t length);
int task_shm_write(struct task_file *file, const void *buffer, uint32_t length);
int task_shm_map(const struct task_file *file, uint64_t offset, uint64_t length,
                 uint64_t *physical);
int syscall_fs_owns(uint64_t number);
int64_t syscall_fs_dispatch(uint64_t number, uint64_t a0, uint64_t a1,
                            uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
int syscall_ipc_owns(uint64_t number);
int64_t syscall_ipc_dispatch(uint64_t number, uint64_t a0, uint64_t a1,
                             uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
int syscall_gui_owns(uint64_t number, uint64_t a1);
int64_t syscall_gui_dispatch(uint64_t number, uint64_t a0, uint64_t a1,
                             uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
int syscall_device_owns(uint64_t number, uint64_t a1);
int64_t syscall_device_dispatch(uint64_t number, uint64_t a0, uint64_t a1,
                                uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
int syscall_security_owns(uint64_t number, uint64_t a1);
int64_t syscall_security_dispatch(uint64_t number, uint64_t a0, uint64_t a1,
                                  uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
int proc_lookup(const char *path, struct storage_node *out);
int proc_read(const char *path, uint64_t offset, void *buffer, uint32_t length,
              uint32_t *out_read);
int proc_readlink(const char *path, char *buffer, uint32_t capacity);
int proc_readdir(const char *path, uint64_t *offset,
                 struct leonos_dir_entry *entry);
int64_t syscall_dispatch_regs_legacy(uint64_t number, uint64_t a0, uint64_t a1,
                                     uint64_t a2, uint64_t a3, uint64_t a4,
                                     uint64_t a5);
int task_can_allocate_fd(const struct task *task);
struct task_pty_fd *task_pty_fd_for_fd(struct task *task, int fd);
void clear_task_file(struct task_file *file);

#endif
