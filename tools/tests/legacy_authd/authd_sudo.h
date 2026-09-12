/* Historical regression fixture only. Never build or stage into LeonOS. */
/* authd privileged execution entry points.
 *
 * Each *_from_peer function serves exactly one wire message and takes the
 * caller's identity as an explicit parameter, which the daemon fills from
 * SO_PEERCRED at accept time. Keeping the identity out of the request payload
 * is the whole trust boundary; the security regression audit greps for this
 * naming so a future handler cannot quietly skip the check.
 */
#ifndef AUTHD_SUDO_H
#define AUTHD_SUDO_H

#include <leonos/auth.h>
#include <leonos/authd.h>
#include <leonos/auth_db.h>
#include <stdint.h>

struct authd_run_context {
    int stdio[3];
    const struct leonos_authd_run *request;
};

/* Signature of the injected privileged spawn step. */
typedef int (*authd_spawn_fn)(void *context, const char *path,
                              char *const argv[], const char *home,
                              const char *username, uint32_t uid, int stdio_fd,
                              uint32_t *out_pid);

/* Signature of the injected password check. */
typedef int (*authd_verify_fn)(void *context, const struct leonos_user_info *user,
                               const char *password);

/* Injected reply channel. The daemon passes the Unix-IPC wrappers; the host
 * test passes a recorder, which is what makes the authorization decisions
 * testable without a kernel. */
typedef int (*authd_send_fn)(void *context, uint32_t type, const void *payload,
                             uint32_t length);

struct authd_sudo_channel {
    authd_send_fn send;
    void *context;
    uint32_t owner_pid;
    uint32_t session_id;
    int (*send_fd)(void *context, uint32_t type, const void *payload,
                   uint32_t length, int fd);
};

/* The account table plus its real length. Carrying the count is what keeps the
 * lookup from reading past a table shorter than the daemon's maximum. */
struct authd_sudo_records {
    const struct leonos_auth_record *records;
    uint32_t count;
};

/* The production implementations, used by the daemon rather than the tests. */
int authd_sudo_spawn(void *context, const char *path, char *const argv[],
                     const char *home, const char *username, uint32_t uid,
                     int stdio_fd, uint32_t *out_pid);
int authd_sudo_verify(void *context, const struct leonos_user_info *user,
                      const char *password);
int authd_sudo_channel_send(void *context, uint32_t type, const void *payload,
                            uint32_t length);
int authd_sudo_channel_send_fd(void *context, uint32_t type, const void *payload,
                             uint32_t length, int fd);

/* Collect finished children and release unclaimed slots. Called from the
 * daemon loop; never blocks. */
int authd_sudo_maintenance(void);

/* A password change or account update must not leave a live authorization
 * behind for an account whose credentials just changed. */
int authd_sudo_cache_revoke_all(void);

/* Returns 0 when the connection stays usable and -1 when the peer must be
 * dropped. A malformed request is answered with an errno ack and keeps the
 * connection; only a broken daemon drops the peer. Owns stdio_fd in every
 * case. */
int authd_sudo_run_from_peer(uint32_t requester_uid, const uint8_t *buffer,
                             uint32_t length, int stdio_fd,
                             const struct authd_sudo_channel *channel,
                             const struct authd_sudo_records *records,
                             authd_spawn_fn spawn, authd_verify_fn verify,
                             void *spawn_context, void *verify_context);

/* Same return contract: 0 keeps the connection, -1 drops the peer. */
int authd_sudo_wait_from_peer(uint32_t requester_uid, const uint8_t *buffer,
                              uint32_t length,
                              const struct authd_sudo_channel *channel);
int authd_sudo_signal_from_peer(uint32_t requester_uid, const uint8_t *buffer,
                               uint32_t length, const struct authd_sudo_channel *channel);

int authd_sudo_kill_from_peer(uint32_t requester_uid,
                              const struct authd_sudo_channel *channel);

int authd_sudo_check_from_peer(uint32_t requester_uid,
                               const struct authd_sudo_channel *channel);

/* Verify an administrator password and open this requester's window, without
 * spawning anything. Returns 0 and acks 1 on success. */
int authd_sudo_verify_from_peer(uint32_t requester_uid, const uint8_t *buffer,
                                uint32_t length,
                                const struct authd_sudo_channel *channel,
                                const struct authd_sudo_records *records,
                                authd_verify_fn verify, void *verify_context);

int authd_sudo_fileop_from_peer(uint32_t requester_uid, const uint8_t *buffer,
                                uint32_t length,
                                const struct authd_sudo_channel *channel,
                                const struct authd_sudo_records *records,
                                authd_spawn_fn spawn, authd_verify_fn verify,
                                void *spawn_context, void *verify_context);

#endif
