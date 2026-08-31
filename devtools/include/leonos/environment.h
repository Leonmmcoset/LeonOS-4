#ifndef LEONOS_ENVIRONMENT_H
#define LEONOS_ENVIRONMENT_H

#include <stdint.h>

#define LEONOS_ENV_SCOPE_GLOBAL 1U
#define LEONOS_ENV_SCOPE_USER 2U

#define LEONOS_ENV_MAX_ENTRIES 64U
#define LEONOS_ENV_MAX_ENTRY_LEN 256U
#define LEONOS_ENV_MAX_FILE_BYTES 8192U

/* Build the environment inherited by a newly spawned process. */
int leonos_environment_build(char *const overrides[], char ***out_envp);
void leonos_environment_free(char **envp);

/* Update the persistent global or current-user environment file. */
int leonos_environment_set(uint32_t scope, const char *name, const char *value);
int leonos_environment_unset(uint32_t scope, const char *name);

#endif
