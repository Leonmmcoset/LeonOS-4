#include <leonos/admin.h>
#include <leonos/auth.h>
#include <leonos/i18n.h>
#include <leonos/ui.h>
#include <string.h>

#define T(en, zh) leonos_i18n((en), (zh))

static void admin_clear_secret(char *text, uint32_t len)
{
    volatile char *p = (volatile char *)text;
    while (len) {
        *p++ = 0;
        --len;
    }
}

int leonos_admin_elevate(void)
{
    struct leonos_user_info user;
    char username[LEONOS_AUTH_USERNAME_LEN];
    char password[LEONOS_AUTH_PASSWORD_LEN];
    int ret;

    if (leonos_auth_current(&user) == 0 &&
        user.role == LEONOS_AUTH_ROLE_ADMIN) {
        return 1;
    }

    memset(username, 0, sizeof(username));
    memset(password, 0, sizeof(password));

    if (leonos_ui_show_input_dialog(
            T("Administrator Elevation", "管理员权限提升"),
            T("Enter an administrator username:",
              "请输入管理员用户名："),
            username, sizeof(username)) != 1 || !username[0]) {
        admin_clear_secret(username, sizeof(username));
        admin_clear_secret(password, sizeof(password));
        return 0;
    }
    if (leonos_ui_show_password_dialog(
            T("Administrator Elevation", "管理员权限提升"),
            T("Enter the administrator password:",
              "请输入管理员密码："),
            password, sizeof(password)) != 1 || !password[0]) {
        admin_clear_secret(username, sizeof(username));
        admin_clear_secret(password, sizeof(password));
        return 0;
    }

    ret = leonos_auth_elevate_admin(username, password, &user);
    admin_clear_secret(password, sizeof(password));
    admin_clear_secret(username, sizeof(username));
    return ret == 0 && user.role == LEONOS_AUTH_ROLE_ADMIN ? 1 : 0;
}
