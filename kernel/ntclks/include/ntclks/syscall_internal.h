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
