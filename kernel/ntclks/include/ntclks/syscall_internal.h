/* Internal syscall helpers shared by the syscall category translation units. */
#ifndef NTCLKS_SYSCALL_INTERNAL_H
#define NTCLKS_SYSCALL_INTERNAL_H

#include <ntclks/sched.h>

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
void task_socket_release(struct task_file *file);
int task_socket_read(struct task_file *file, void *buffer, uint32_t length);
int task_socket_write(struct task_file *file, const void *buffer, uint32_t length);
short task_socket_poll(const struct task_file *file, short events);
int64_t syscall_socket_dispatch(uint64_t number, uint64_t a0, uint64_t a1,
                                uint64_t a2, uint64_t a3, uint64_t a4);
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
int64_t syscall_dispatch_regs_legacy(uint64_t number, uint64_t a0, uint64_t a1,
                                     uint64_t a2, uint64_t a3, uint64_t a4,
                                     uint64_t a5);
int task_can_allocate_fd(const struct task *task);
struct task_pty_fd *task_pty_fd_for_fd(struct task *task, int fd);
void clear_task_file(struct task_file *file);

#endif
