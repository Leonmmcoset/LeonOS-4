#ifndef LEONOS_STANDARD_ACCOUNTS_H
#define LEONOS_STANDARD_ACCOUNTS_H
#include <leonos/auth.h>
#include <pwd.h>

int leonos_account_info(const struct passwd *account, struct leonos_user_info *out);
int leonos_account_name_valid(const char *name, unsigned capacity);
int leonos_account_seed(const char *target, const char *name,
                        const char *password, const char *root_password);
int leonos_account_legacy_check(const char *target);
#endif
