/* Exercise the production password adapter, not a different mock convention. */
#define main policy_suite_main
#define authd_check_password unused_password_stub
#include "authd_sudo_test.c"
#undef authd_check_password
#undef main
#include "../../userland/apps/authd/accounts.h"

int main(void)
{
    struct leonos_authd_run run;
    struct authd_sudo_channel channel = {.send = channel_send};
    records_init();
    assert(authd_set_password(&record_storage[0], "actual-root-password") == 0);
    assert(authd_check_password(&record_storage[0], "wrong-password") == 0);
    reset_logs();
    fill_run(&run, "root", "wrong-password", "/bin/id");
    assert(authd_sudo_run_from_peer(1000, (const uint8_t *)&run, sizeof(run),
        -1, &channel, &records, spawn_record, authd_sudo_verify,
        NULL, &records) == 0);
    assert(spawned.calls == 0);
    reset_logs();
    fill_run(&run, "root", "actual-root-password", "/bin/id");
    assert(authd_sudo_run_from_peer(1000, (const uint8_t *)&run, sizeof(run),
        -1, &channel, &records, spawn_record, authd_sudo_verify,
        NULL, &records) == 0);
    assert(spawned.calls == 1 && spawned.uid == 0);
    drain();
    authd_sudo_cache_revoke_all();
    reset_logs();
    fill_run_ex(&run, "alice", "alice-pw", "/bin/id", 0);
    assert(run_request(1000, &run, -1) == 0);
    assert(spawned.calls == 1 && spawned.uid == 1000);
    reset_logs();
    fill_run(&run, "root", NULL, "/bin/id");
    assert(run_request(1000, &run, -1) == 0);
    assert(spawned.calls == 0);
    assert(!sudo_path_allowed("//proc/self"));
    assert(!sudo_path_allowed("/./dev"));
    struct sudo_cache cache = {0};
    sudo_cache_grant_session(&cache, 1000, 11, 100);
    assert(sudo_cache_valid_session(&cache, 1000, 11, 101));
    assert(!sudo_cache_valid_session(&cache, 1000, 12, 101));
    puts("sudo security: production password and cache boundaries PASS");
    return 0;
}
