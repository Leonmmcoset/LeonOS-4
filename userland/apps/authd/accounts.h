#ifndef AUTHD_ACCOUNTS_H
#define AUTHD_ACCOUNTS_H

#include <leonos/auth_db.h>

int authd_username_valid(const char *name, unsigned capacity);
int authd_export_accounts(const char *directory,
                          const struct leonos_auth_record *records, unsigned count);

#endif
