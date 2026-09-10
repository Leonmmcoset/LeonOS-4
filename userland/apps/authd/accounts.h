#ifndef AUTHD_ACCOUNTS_H
#define AUTHD_ACCOUNTS_H

#include <leonos/auth_db.h>
#include <leonos/auth.h>

int authd_username_valid(const char *name, unsigned capacity);
int authd_account_valid(const struct leonos_user_info *user);
int authd_publish_session(const char *path, const struct leonos_user_info *user);
int authd_set_password(struct leonos_auth_record *record, const char *password);
int authd_check_password(const struct leonos_auth_record *record, const char *password);
int authd_store_database(const char *path, const struct leonos_auth_record *records,
                         unsigned count);
int authd_export_accounts(const char *directory,
                          const struct leonos_auth_record *records, unsigned count);

#endif
