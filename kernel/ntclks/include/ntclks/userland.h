/*
 * LeonOS userland interface: declares process-image and userspace services.
 * Coordinates executable loading, launch arguments, and user task startup.
 */
#ifndef NTCLKS_USERLAND_H
#define NTCLKS_USERLAND_H

#include <leonos/fs.h>
#include <leonos/auth.h>
#include <ntclks/multiboot2.h>
#include <ntclks/trap.h>
#include <ntclks/types.h>

struct task;

/**
 * @brief Coordinates the userland init operation.
 * @param boot Boot information supplied by the loader.
 */
void userland_init(const struct boot_info *boot);
void userland_enter_first(void) __attribute__((noreturn));
/**
 * @brief Coordinates the userland process exit operation.
 * @param code Input or output value used by this operation.
 */
void userland_process_exit(uint64_t code);
/**
 * @brief Replaces the current process image while preserving its PID and process attributes.
 * @param path Resolved executable path.
 * @param argc Number of kernel-copied argv entries.
 * @param argv Kernel-owned argv pointers into data.
 * @param envc Number of kernel-copied envp entries.
 * @param envp Kernel-owned envp pointers into data.
 * @param data Packed NUL-terminated argument and environment strings.
 * @param data_len Number of valid data bytes.
 * @return Zero on success, or a negative errno-style failure with the old image intact.
 */
int userland_exec_current_path(const char *path, uint32_t argc, char *const argv[],
                               uint32_t envc, char *const envp[],
                               const char *data, uint32_t data_len);
/**
 * @brief Coordinates the userland spawn path operation.
 * @param path LeonOS path consumed by this operation.
 * @return Result, status, or value defined by this API.
 */
int64_t userland_spawn_path(const char *path);
/**
 * @brief Coordinates the userland spawn path with pty operation.
 * @param path LeonOS path consumed by this operation.
 * @param pty_id Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int64_t userland_spawn_path_with_pty(const char *path, uint32_t pty_id);
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
                                  uint32_t pty_id);
/**
 * @brief Spawns a user process with caller-selected standard descriptors.
 * @param path NUL-terminated executable path in LeonOS drive syntax.
 * @param argv Optional NUL-terminated argument vector copied to the child.
 * @param envp Optional NUL-terminated environment vector copied to the child.
 * @param pty_id Active PTY inherited by the child.
 * @param stdin_fd Descriptor assigned to child standard input.
 * @param stdout_fd Descriptor assigned to child standard output.
 * @param stderr_fd Descriptor assigned to child standard error.
 * @return Positive child PID on success or a negative errno value on failure.
 */
int64_t userland_spawn_path_argv_with_fds(const char *path,
                                          const char *const argv[],
                                          const char *const envp[],
                                          uint32_t pty_id,
                                          int stdin_fd, int stdout_fd,
                                          int stderr_fd);
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
                                          uint32_t session_id);
/**
 * @brief Coordinates the userland yield if runnable operation.
 */
void userland_yield_if_runnable(void);
struct task *userland_schedule_from_frame(struct trap_frame *frame);
/**
 * @brief Coordinates the userland list dir operation.
 * @param path LeonOS path consumed by this operation.
 * @param entries Input or output value used by this operation.
 * @param capacity Capacity, in elements or bytes, of the related output buffer.
 * @param out_count Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
int userland_list_dir(const char *path, struct leonos_dir_entry *entries,
                      uint32_t capacity, uint32_t *out_count);

#endif
