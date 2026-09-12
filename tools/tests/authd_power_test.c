#include <assert.h>
#include <setjmp.h>
#include <sys/un.h>
#define main authd_program_main
#include "../../userland/apps/authd/main.c"
#undef main

static unsigned power_calls, sync_calls;
static int32_t reply_code;
static int command, succeed;
static jmp_buf transition;

void sync(void) { ++sync_calls; }
int reboot(int cmd)
{
    ++power_calls;
    command = cmd;
    if (succeed) longjmp(transition, 1);
    errno = EIO;
    return -1;
}
int leonos_ipc_send(int fd, uint32_t type, const void *payload, uint32_t length)
{
    assert(fd == 10 && type == LEONOS_AUTHD_MSG_ACK && length == sizeof(struct leonos_authd_ack));
    reply_code = ((const struct leonos_authd_ack *)payload)->code;
    return 0;
}

int main(void)
{
    clients[0] = (struct authd_client){.used = 1, .fd = 10, .uid = 1000};
    session_active = 1;
    user_count = 1;
    users[0].user.uid = 1000;
    struct leonos_authd_power request = {.command = RB_AUTOBOOT};
    authd_handle_power(0, (const uint8_t *)&request, sizeof(request));
    assert(reply_code == -EPERM && !power_calls && !sync_calls);
    current_uid = 1001;
    authd_handle_power(0, (const uint8_t *)&request, sizeof(request));
    assert(reply_code == -EPERM && !power_calls);
    current_uid = 1000;
    users[0].user.flags = LEONOS_AUTH_USER_DISABLED;
    authd_handle_power(0, (const uint8_t *)&request, sizeof(request));
    assert(reply_code == -EPERM && !power_calls);
    users[0].user.flags = 0;
    authd_handle_power(0, (const uint8_t *)&request, sizeof(request) - 1);
    assert(reply_code == -EINVAL && !power_calls);
    request.command = RB_ENABLE_CAD;
    authd_handle_power(0, (const uint8_t *)&request, sizeof(request));
    assert(reply_code == -EINVAL && !power_calls);
    request.command = RB_AUTOBOOT;
    request.reserved = 1;
    authd_handle_power(0, (const uint8_t *)&request, sizeof(request));
    assert(reply_code == -EINVAL && !power_calls);
    request.reserved = 0;
    authd_handle_power(0, (const uint8_t *)&request, sizeof(request));
    assert(reply_code == -EIO && power_calls == 1 && sync_calls == 1 && command == RB_AUTOBOOT);
    succeed = 1;
    request.command = RB_POWER_OFF;
    if (!setjmp(transition)) {
        authd_handle_power(0, (const uint8_t *)&request, sizeof(request));
        assert(0);
    }
    assert(power_calls == 2 && sync_calls == 2 && command == RB_POWER_OFF);
    clients[0].uid = 0;
    current_uid = 0;
    if (!setjmp(transition)) {
        authd_handle_power(0, (const uint8_t *)&request, sizeof(request));
        assert(0);
    }
    assert(power_calls == 3 && sync_calls == 3);
    puts("PASS authd power: active peer UID, logged-out/different/disabled users, malformed requests, sync and real error propagation");
}
