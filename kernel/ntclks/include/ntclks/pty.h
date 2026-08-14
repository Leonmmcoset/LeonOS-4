/*
 * LeonOS pseudo-terminal interface: declares PTY allocation and I/O helpers.
 * Connects terminal processes, shells, and the kernel's event streams.
 */
#ifndef NTCLKS_PTY_H
#define NTCLKS_PTY_H

#include <ntclks/types.h>
#include <leonos/pty.h>

/**
 * @brief Coordinates the pty init operation.
 */
void pty_init(void);
/**
 * @brief Coordinates the pty create operation.
 * @param owner_pid Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int32_t pty_create(uint32_t owner_pid);
/**
 * @brief Coordinates the pty destroy operation.
 * @param owner_pid Input or output value used by this operation.
 * @param pty_id Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int pty_destroy(uint32_t owner_pid, uint32_t pty_id);
/**
 * @brief Coordinates the pty is owner operation.
 * @param pty_id Input or output value used by this operation.
 * @param owner_pid Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int pty_is_owner(uint32_t pty_id, uint32_t owner_pid);
/**
 * @brief Coordinates the pty is active operation.
 * @param pty_id Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int pty_is_active(uint32_t pty_id);
/**
 * @brief Coordinates the pty read output operation.
 * @param owner_pid Input or output value used by this operation.
 * @param pty_id Input or output value used by this operation.
 * @param buffer Buffer consumed or filled by this operation.
 * @param length Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
 */
int64_t pty_read_output(uint32_t owner_pid, uint32_t pty_id, char *buffer, uint32_t length);
/**
 * @brief Coordinates the pty write input operation.
 * @param owner_pid Input or output value used by this operation.
 * @param pty_id Input or output value used by this operation.
 * @param buffer Buffer consumed or filled by this operation.
 * @param length Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
 */
int64_t pty_write_input(uint32_t owner_pid, uint32_t pty_id, const char *buffer, uint32_t length);
/**
 * @brief Coordinates the pty read input operation.
 * @param pty_id Input or output value used by this operation.
 * @param buffer Buffer consumed or filled by this operation.
 * @param length Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
 */
int64_t pty_read_input(uint32_t pty_id, char *buffer, uint32_t length);
/**
 * @brief Coordinates the pty input available operation.
 * @param pty_id Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
uint32_t pty_input_available(uint32_t pty_id);
/**
 * @brief Coordinates the pty write output operation.
 * @param pty_id Input or output value used by this operation.
 * @param buffer Buffer consumed or filled by this operation.
 * @param length Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
 */
int64_t pty_write_output(uint32_t pty_id, const char *buffer, uint32_t length);
/**
 * @brief Coordinates the pty get termios operation.
 * @param pty_id Input or output value used by this operation.
 * @param termios Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int pty_get_termios(uint32_t pty_id, struct leonos_pty_termios *termios);
/**
 * @brief Coordinates the pty set termios operation.
 * @param pty_id Input or output value used by this operation.
 * @param termios Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int pty_set_termios(uint32_t pty_id, const struct leonos_pty_termios *termios);
/**
 * @brief Coordinates the pty get winsize operation.
 * @param pty_id Input or output value used by this operation.
 * @param winsize Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int pty_get_winsize(uint32_t pty_id, struct leonos_pty_winsize *winsize);
/**
 * @brief Coordinates the pty set winsize operation.
 * @param pty_id Input or output value used by this operation.
 * @param winsize Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the pty process exit operation.
 * @param pid Input or output value used by this operation.
 */
void pty_process_exit(uint32_t pid);

#endif
