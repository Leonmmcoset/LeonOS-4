#ifndef LEONOS_INSTALLER_SETUP_H
#define LEONOS_INSTALLER_SETUP_H
#include <leonos/auth_user.h>
#include <stdint.h>

struct installer_setup {
    char username[LEONOS_AUTH_USERNAME_LEN];
    char password[LEONOS_AUTH_PASSWORD_LEN];
    char password_confirm[LEONOS_AUTH_PASSWORD_LEN];
    char root_password[LEONOS_AUTH_PASSWORD_LEN];
    char root_password_confirm[LEONOS_AUTH_PASSWORD_LEN];
    uint8_t component_available[2];
    uint8_t component_selected[2];
};

extern const char *const installer_component_names[2];
int installer_setup_load(struct installer_setup *setup, const char *manifest,
                         const char *payload);
int installer_setup_valid(const struct installer_setup *setup);
int installer_setup_include(const struct installer_setup *setup, const char *source);
int installer_setup_write(const struct installer_setup *setup, const char *target);
void installer_setup_existing(struct installer_setup *setup, const char *target);
#endif
