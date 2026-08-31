#ifndef LEONOS_ENVIRONMENT_H
#define LEONOS_ENVIRONMENT_H

#include <stdint.h>

#define LEONOS_ENV_SCOPE_GLOBAL 1U
#define LEONOS_ENV_SCOPE_USER 2U

#define LEONOS_ENV_MAX_ENTRIES 64U
#define LEONOS_ENV_MAX_ENTRY_LEN 256U
#define LEONOS_ENV_MAX_FILE_BYTES 8192U

/* Build a NULL-terminated environment for a newly spawned process.
 * Values from the current process are used as a base, then the optional
 * overrides are applied last. The returned vector must be released with
 * leonos_environment_free(). */
int leonos_environment_build(char *const overrides[], char ***out_envp);
void leonos_environment_free(char **envp);

/* Update the persistent global or current-user environment file. Global
 * updates require an administrator session; user updates require a logged-in
 * account with a home directory. */
int leonos_environment_set(uint32_t scope, const char *name, const char *value);
int leonos_environment_unset(uint32_t scope, const char *name);

#endif
