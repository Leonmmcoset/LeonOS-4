#include <assert.h>
#include <sys/un.h>
#include <leonos/syscall.h>

#define main authd_program_main
#include "../../userland/apps/authd/main.c"
#undef main

static uint32_t reply_type;
static int32_t reply_code;

int leonos_ipc_send(int fd, uint32_t type, const void *payload, uint32_t length)
{
    assert(fd == 10);
    reply_type = type;
    if (type == LEONOS_AUTHD_MSG_ACK) {
        assert(length == sizeof(struct leonos_authd_ack));
        reply_code = ((const struct leonos_authd_ack *)payload)->code;
    } else {
        assert(length == sizeof(struct leonos_user_info));
        assert(((const struct leonos_user_info *)payload)->uid == 0);
    }
    return 0;
}

int main(void)
{
    clients[0] = (struct authd_client){.used = 1, .fd = 10, .uid = 0};
    user_count = 1;
    users[0].user.uid = 0;
    users[0].user.role = LEONOS_AUTH_ROLE_ADMIN;
    strcpy(users[0].user.username, "root");
    assert(authd_set_password(&users[0], "correct-password") == 0);
    struct leonos_auth_login login = {0};
    strcpy(login.username, "root");
    strcpy(login.password, "correct-password");
    authd_handle_elevate(0, (const uint8_t *)&login, sizeof(login));
    assert(reply_type == LEONOS_AUTHD_MSG_ELEVATE);
    strcpy(login.password, "wrong-password");
    authd_handle_elevate(0, (const uint8_t *)&login, sizeof(login));
    assert(reply_type == LEONOS_AUTHD_MSG_ACK && reply_code < 0);
    memset(login.password, 'a', sizeof(login.password));
    authd_handle_login(0, (const uint8_t *)&login, sizeof(login));
    assert(reply_type == LEONOS_AUTHD_MSG_ACK && reply_code < 0);
    memset(login.username, 'a', sizeof(login.username));
    authd_handle_elevate(0, (const uint8_t *)&login, sizeof(login));
    assert(reply_type == LEONOS_AUTHD_MSG_ACK && reply_code < 0);
    puts("authd: elevation verifies supplied password; unterminated request fields rejected PASS");
    return 0;
}
