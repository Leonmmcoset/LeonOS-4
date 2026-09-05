/*
 * LeonOS pseudo-terminal interface: declares PTY allocation and I/O helpers.
 * Connects terminal processes, shells, and the kernel's event streams.
 */
#ifndef NTCLKS_PTY_H
#define NTCLKS_PTY_H

#include <ntclks/types.h>
#include <leonos/pty.h>

/**
 * @brief Initialize the pseudo-terminal subsystem and its backing storage.
 */
void pty_init(void);
/**
 * @brief Bind a PTY to the physical console for the kernel-created TTY shell.
 */
int pty_bind_console(uint32_t pty_id, uint32_t owner_pid);
/**
 * @brief Deliver a normalized keyboard event to the console PTY.
 */
void pty_console_key_event(uint8_t keycode, uint8_t pressed);
/**
 * @brief Allocate a new PTY for owner_pid and return its id, or a negative error.
 */
int32_t pty_create(uint32_t owner_pid);
/**
 * @brief Tear down the PTY pty_id if it is owned by owner_pid; 0 on success.
 */
int pty_destroy(uint32_t owner_pid, uint32_t pty_id);
/**
 * @brief Return non-zero when owner_pid owns pty_id.
 */
int pty_is_owner(uint32_t pty_id, uint32_t owner_pid);
/**
 * @brief Return non-zero when pty_id refers to a live PTY.
 */
int pty_is_active(uint32_t pty_id);
/**
 * @brief Return non-zero when pty_id's master side has closed.
 */
int pty_is_hungup(uint32_t pty_id);
/**
 * @brief Copy up to length bytes of pty_id's terminal output into buffer; returns bytes read.
 */
int64_t pty_read_output(uint32_t owner_pid, uint32_t pty_id, char *buffer, uint32_t length);
/**
 * @brief Feed up to length bytes of buffer into pty_id as terminal input; returns bytes accepted.
 */
int64_t pty_write_input(uint32_t owner_pid, uint32_t pty_id, const char *buffer, uint32_t length);
/**
 * @brief Copy up to length bytes of pty_id's pending input into buffer; returns bytes read.
 */
int64_t pty_read_input(uint32_t pty_id, char *buffer, uint32_t length);
/**
 * @brief Return how many bytes of input are buffered for pty_id.
 */
uint32_t pty_input_available(uint32_t pty_id);
/** Return bytes buffered from the PTY slave toward its master. */
uint32_t pty_output_available(uint32_t pty_id);
/**
 * @brief Write up to length bytes of buffer to pty_id's terminal output; returns bytes written.
 */
int64_t pty_write_output(uint32_t pty_id, const char *buffer, uint32_t length);
/**
 * @brief Copy pty_id's terminal mode settings into termios; 0 on success.
 */
int pty_get_termios(uint32_t pty_id, struct leonos_pty_termios *termios);
/**
 * @brief Apply the terminal mode settings in termios to pty_id; 0 on success.
 */
int pty_set_termios(uint32_t pty_id, const struct leonos_pty_termios *termios);
/**
 * @brief Copy pty_id's terminal window size into winsize; 0 on success.
 */
int pty_get_winsize(uint32_t pty_id, struct leonos_pty_winsize *winsize);
/**
 * @brief Set pty_id's terminal window size from winsize; 0 on success.
 */
int pty_set_winsize(uint32_t pty_id, const struct leonos_pty_winsize *winsize);
/**
 * @brief Reads the foreground process group of a pseudo-terminal.
 * @param pty_id PTY identifier.
 * @param process_group Destination for the foreground process-group identifier.
 * @return Zero on success or a negative errno-style failure.
 */
int pty_get_foreground_pgid(uint32_t pty_id, uint32_t *process_group);
/**
 * @brief Changes the foreground process group of a pseudo-terminal.
 * @param pty_id PTY identifier.
 * @param caller_pid Attached process requesting the change.
 * @param process_group New foreground process-group identifier.
 * @return Zero on success or a negative errno-style failure.
 */
int pty_set_foreground_pgid(uint32_t pty_id, uint32_t caller_pid,
                            uint32_t process_group);
/**
 * @brief Adopt caller_pid's session and process group as the PTY controlling session.
 */
void pty_acquire_controlling(uint32_t pty_id, uint32_t caller_pid);
/**
 * @brief Reclaim a hung-up PTY session when no descriptor still references it.
 */
void pty_reap_hungup(uint32_t pty_id);
/**
 * @brief Detach pid from any PTY it is attached to.
 */
void pty_process_exit(uint32_t pid);

#endif
