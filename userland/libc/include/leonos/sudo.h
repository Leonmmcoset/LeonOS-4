#ifndef LEONOS_SUDO_H
#define LEONOS_SUDO_H
#include <leonos/fs.h>
#include <stdint.h>

#define LEONOS_FILEOP_LIST 1U
#define LEONOS_FILEOP_MKDIR 2U
#define LEONOS_FILEOP_RENAME 3U
#define LEONOS_FILEOP_UNLINK 4U
#define LEONOS_FILEOP_CONFIRM "DELETE"
#define LEONOS_SUDO_CACHED 1
#define LEONOS_SUDO_NEEDS_PASSWORD 0

/* These adapters invoke upstream sudo/su with dynamically allocated argv.
 * Password arguments must be NULL/empty: the executable owns PAM conversation.
 * Use the actual CLI for its complete options and shell/login semantics. */
int leonos_sudo_run(const char *username, const char *password,
                    char *const argv[], uint32_t *out_pid);
int leonos_sudo_run_switch(const char *username, const char *password,
                           char *const argv[], uint32_t *out_pid);
int leonos_sudo_run_login(const char *username, const char *password,
                         char *const argv[], uint32_t *out_pid);
/* The returned PID is a direct child. wait uses WNOHANG, wait_command blocks. */
int leonos_sudo_wait(uint32_t child_pid, int *out_status);
int leonos_sudo_wait_command(uint32_t child_pid, int *out_status);
/* UI hint / sudo -v only. Neither confers authority for a later operation. */
int leonos_sudo_check(void);
int leonos_sudo_verify(const char *username, const char *password);
int leonos_sudo_kill(void);

/* Each operation runs sudo -A with a fixed root-owned worker and exact args.
 * username/password must be NULL/empty. Results arrive through a private pipe. */
int leonos_fileop(uint32_t op, const char *path1, const char *path2,
                  const char *username, const char *password,
                  struct leonos_dir_entry *entries, uint32_t capacity,
                  uint32_t *out_count);
int leonos_read_password(const char *prompt, char *buffer, uint32_t capacity);
#endif
