#ifndef LEONOS_PAM_SESSION_H
#define LEONOS_PAM_SESSION_H
#include <leonos/auth.h>
int leonos_session_initialize(void);
int leonos_session_current(struct leonos_user_info *user);
int leonos_session_apply(void);
int leonos_pam_session_wait(void);
int leonos_pam_login(const char *name, char *password, struct leonos_user_info *user);
#endif
