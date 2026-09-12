#include <leonos/auth.h>
#include <stddef.h>
#include <security/pam_modules.h>
#include <security/pam_ext.h>

int pam_sm_chauthtok(pam_handle_t *handle, int flags, int argc, const char **argv)
{
    (void)argv;
    if (argc) return PAM_SERVICE_ERR;
    int phase = flags & (PAM_PRELIM_CHECK | PAM_UPDATE_AUTHTOK);
    if (phase == PAM_PRELIM_CHECK) return PAM_SUCCESS;
    if (phase != PAM_UPDATE_AUTHTOK) return PAM_SERVICE_ERR;

    const char *password = NULL;
    int result = pam_get_authtok(handle, PAM_AUTHTOK, &password, NULL);
    if (result != PAM_SUCCESS) return result;
    if (leonos_auth_password_valid(password, LEONOS_AUTH_PASSWORD_LEN))
        return PAM_SUCCESS;

    /* PAM owns and erases the token; keep an invalid value out of later modules. */
    result = pam_set_item(handle, PAM_AUTHTOK, NULL);
    if (result != PAM_SUCCESS) return result;
    result = pam_error(handle, "Password must contain 1 to 32 UTF-8 characters without whitespace.");
    return result == PAM_SUCCESS ? PAM_AUTHTOK_ERR : result;
}
