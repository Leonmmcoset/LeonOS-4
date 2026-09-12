/* sessiond, hosted by serviced: startup approval and session launch policy
 * over /run/leonos/session.sock. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <leonos/fs.h>
#include <leonos/launch.h>
#include <leonos/pam_session.h>
#include <leonos/sessiond.h>
#include <leonos/startup.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/unix_ipc.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "sessiond.h"
#include <leonos/layout.h>

#define SESSIOND_DB_PATH LEONOS_PATH_STARTUP_DB
#define SESSIOND_MAGIC 0x53533132U /* SS12: entries have an owning UID. */
#define SESSIOND_MAX_CLIENTS 16u
#define SESSIOND_MAX_ENTRIES LEONOS_STARTUP_MAX_ENTRIES
#define SESSIOND_FRAME_CAP 4096u

struct sessiond_client {
    uint32_t used;
    int fd;
    uint32_t pid;
    uint32_t uid;
    struct ucred credentials;
};

struct sessiond_entry {
    uint32_t id;
    uint32_t enabled;
    uint32_t uid;
    struct leonos_startup_command command;
};

struct sessiond_db {
    uint32_t magic;
    uint32_t count;
    uint32_t next_id;
    struct sessiond_entry entries[SESSIOND_MAX_ENTRIES];
};

static struct sessiond_client clients[SESSIOND_MAX_CLIENTS];
static struct sessiond_db db;
static int listen_fd = -1;
static int database_failed;

static int sessiond_io(int fd, void *buffer, size_t size, int writing)
{
    size_t done = 0;
    while (done < size) {
        ssize_t n = writing ? write(fd, (char *)buffer + done, size - done) :
                              read(fd, (char *)buffer + done, size - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { if (!n) errno = EIO; return -1; }
        done += (size_t)n;
    }
    return 0;
}

static int sessiond_command_valid(const struct leonos_startup_command *command)
{
    if (command->argc > LEONOS_STARTUP_MAX_ARGS || command->path[0] != '/' ||
        !memchr(command->path, 0, sizeof(command->path))) return 0;
    for (uint32_t i = 0; i < command->argc; ++i)
        if (!memchr(command->args[i], 0, sizeof(command->args[i]))) return 0;
    return 1;
}

static int sessiond_load(void)
{
    int fd = open(SESSIOND_DB_PATH, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        if (errno != ENOENT) return -1;
        db = (struct sessiond_db){.magic = SESSIOND_MAGIC, .next_id = 1};
        return 0;
    }
    struct stat st;
    int result = -1;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_uid || st.st_mode & 0022 ||
        sessiond_io(fd, &db, 3 * sizeof(uint32_t), 0) < 0) goto out;
    /* SS11 has no trustworthy owner; only its empty seed can be adopted. */
    if (db.magic == 0x53533131U && !db.count && st.st_size == 12) db.magic = SESSIOND_MAGIC;
    if (db.magic != SESSIOND_MAGIC || db.count > SESSIOND_MAX_ENTRIES || !db.next_id ||
        st.st_size != (off_t)(12 + db.count * sizeof(db.entries[0])) ||
        sessiond_io(fd, db.entries, db.count * sizeof(db.entries[0]), 0) < 0) goto out;
    for (uint32_t i = 0; i < db.count; ++i) {
        if (!db.entries[i].id || db.entries[i].id >= db.next_id ||
            !sessiond_command_valid(&db.entries[i].command)) goto out;
        for (uint32_t j = 0; j < i; ++j) if (db.entries[j].id == db.entries[i].id) goto out;
    }
    result = 0;
out:
    close(fd);
    if (result < 0) errno = EINVAL;
    return result;
}

static int sessiond_save(void)
{
    char temporary[] = LEONOS_LAYOUT_VAR_LIB_LEONOS "/.startup.XXXXXX";
    int fd = mkstemp(temporary);
    if (fd < 0) return -1;
    int result = -1;
    if (fchown(fd, 0, 0) < 0 || fchmod(fd, 0600) < 0 ||
        sessiond_io(fd, &db, 12 + db.count * sizeof(db.entries[0]), 1) < 0 ||
        fsync(fd) < 0 || rename(temporary, SESSIOND_DB_PATH) < 0) goto out;
    int directory = open(LEONOS_LAYOUT_VAR_LIB_LEONOS, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory >= 0) { result = fsync(directory); close(directory); }
out:;
    int error = errno;
    close(fd); unlink(temporary); errno = error;
    return result;
}

static void sessiond_send_ack(int slot, int32_t code, uint32_t value)
{
    struct leonos_sessiond_ack ack = {.code = code, .value = value};
    if (slot >= 0 && slot < SESSIOND_MAX_CLIENTS && clients[slot].used) {
        (void)leonos_ipc_send(clients[slot].fd, LEONOS_SESSIOND_MSG_ACK,
                              &ack, sizeof(ack));
    }
}

static void sessiond_request(int slot, const uint8_t *buffer, uint32_t length)
{
    struct leonos_startup_command command;
    uint32_t uid = clients[slot].uid;
    if (length != sizeof(command) || db.count >= SESSIOND_MAX_ENTRIES || db.next_id == UINT32_MAX) {
        sessiond_send_ack(slot, LEONOS_STARTUP_STATUS_FAILED, 0);
        return;
    }
    memcpy(&command, buffer, sizeof(command));
    if (!sessiond_command_valid(&command)) { sessiond_send_ack(slot, -EINVAL, 0); return; }
    db.entries[db.count].id = db.next_id++;
    db.entries[db.count].enabled = 1;
    db.entries[db.count].uid = uid;
    db.entries[db.count].command = command;
    ++db.count;
    if (sessiond_save() < 0) {
        --db.count;
        sessiond_send_ack(slot, LEONOS_STARTUP_STATUS_FAILED, 0);
        return;
    }
    sessiond_send_ack(slot, LEONOS_STARTUP_STATUS_APPROVED,
                      db.entries[db.count - 1u].id);
}

static void sessiond_list(int slot, const uint8_t *buffer, uint32_t length)
{
    struct leonos_startup_list request;
    uint8_t payload[SESSIOND_FRAME_CAP];
    struct leonos_sessiond_list_ack ack;
    uint32_t offset = sizeof(ack);
    uint32_t count = 0;
    if (length != sizeof(request)) { sessiond_send_ack(slot, -EINVAL, 0); return; }
    memcpy(&request, buffer, sizeof(request));
    if (clients[slot].uid && clients[slot].uid != request.uid) {
        sessiond_send_ack(slot, -EACCES, 0); return;
    }
    memset(&ack, 0, sizeof(ack));
    ack.uid = request.uid;
    for (uint32_t i = 0; i < db.count; ++i) {
        if (db.entries[i].uid != request.uid) continue;
        struct leonos_startup_entry entry = {
            .id = db.entries[i].id,
            .enabled = db.entries[i].enabled,
            .command = db.entries[i].command,
        };
        if (request.capacity > 0 && count < request.capacity &&
            offset + sizeof(struct leonos_startup_entry) <= sizeof(payload)) {
            memcpy(payload + offset, &entry, sizeof(entry));
            offset += sizeof(entry);
            ++count;
        }
    }
    ack.count = count;
    memcpy(payload, &ack, sizeof(ack));
    (void)leonos_ipc_send(clients[slot].fd, LEONOS_SESSIOND_MSG_LIST,
                          payload, offset);
}

static int sessiond_launch_current(void)
{
    struct leonos_user_info user;
    if (leonos_session_current(&user) < 0) return -1;
    for (uint32_t i = 0; i < db.count; ++i) {
        char *argv[LEONOS_STARTUP_MAX_ARGS + 2u];
        uint32_t argc = db.entries[i].command.argc;
        if (!db.entries[i].enabled || db.entries[i].uid != user.uid ||
            !sessiond_command_valid(&db.entries[i].command)) continue;
        argv[0] = db.entries[i].command.path;
        for (uint32_t j = 0; j < argc; ++j) {
            argv[j + 1u] = db.entries[i].command.args[j];
        }
        argv[argc + 1u] = 0;
        pid_t child = fork();
        if (child < 0) return -1;
        if (!child) {
            /* A logout/login race must never execute another user's entry. */
            if (leonos_session_apply() < 0 || getuid() != db.entries[i].uid ||
                syscall(SYS_close_range, 3u, ~0u, 0u) < 0) _exit(126);
            execv(argv[0], argv);
            _exit(127);
        }
    }
    return 0;
}

static void sessiond_handle_client(int slot)
{
    struct sessiond_client *client = &clients[slot];
    uint8_t buffer[SESSIOND_FRAME_CAP];
    uint32_t type = 0;
    uint32_t length = 0;
    for (;;) {
        struct pollfd descriptor = {.fd = client->fd, .events = POLLIN, .revents = 0};
        if (poll(&descriptor, 1, 0) <= 0) return;
        if (leonos_ipc_recv_cred_fd(client->fd, &type, buffer, sizeof(buffer), &length,
                                   NULL, &client->credentials) < 0) {
            if (errno == EAGAIN) return;
            leonos_ipc_close(client->fd);
            memset(client, 0, sizeof(*client));
            client->fd = -1;
            return;
        }
        if (type == LEONOS_SESSIOND_MSG_HELLO) {
            struct leonos_sessiond_hello hello;
            if (length < sizeof(hello)) { leonos_ipc_close(client->fd); memset(client,0,sizeof(*client)); client->fd=-1; return; }
            memcpy(&hello, buffer, sizeof(hello));
            if (hello.pid != client->pid || hello.uid != client->uid) {
                leonos_ipc_close(client->fd);
                memset(client, 0, sizeof(*client));
                client->fd = -1;
                return;
            }
            sessiond_send_ack(slot, 1, 0);
            continue;
        }
        if (type == LEONOS_SESSIOND_MSG_REQUEST) { sessiond_request(slot, buffer, length); continue; }
        if (type == LEONOS_SESSIOND_MSG_REQUEST_STATUS) {
            struct leonos_startup_request_status request;
            if (length < sizeof(request)) continue;
            memcpy(&request, buffer, sizeof(request));
            request.status = LEONOS_STARTUP_STATUS_DENIED;
            for (uint32_t i = 0; i < db.count; ++i)
                if (db.entries[i].id == request.request_id &&
                    (!client->uid || client->uid == db.entries[i].uid))
                    request.status = LEONOS_STARTUP_STATUS_APPROVED;
            (void)leonos_ipc_send(client->fd, LEONOS_SESSIOND_MSG_REQUEST_STATUS,
                                  &request, sizeof(request));
            continue;
        }
        if (type == LEONOS_SESSIOND_MSG_DIALOG_GET) {
            sessiond_send_ack(slot, 0, 0);
            continue;
        }
        if (type == LEONOS_SESSIOND_MSG_DIALOG_RESOLVE) {
            sessiond_send_ack(slot, -ENOTSUP, 0);
            continue;
        }
        if (type == LEONOS_SESSIOND_MSG_LIST) { sessiond_list(slot, buffer, length); continue; }
        if (type == LEONOS_SESSIOND_MSG_SET_ENABLED) {
            struct leonos_startup_update update;
            if (length < sizeof(update)) continue;
            memcpy(&update, buffer, sizeof(update));
            if (client->uid && client->uid != update.uid) { sessiond_send_ack(slot, -EACCES, 0); continue; }
            int result = -ENOENT;
            for (uint32_t i = 0; i < db.count; ++i) {
                if (db.entries[i].id == update.entry_id && db.entries[i].uid == update.uid) {
                    uint32_t previous = db.entries[i].enabled;
                    db.entries[i].enabled = update.enabled ? 1u : 0u;
                    result = sessiond_save();
                    if (result < 0) db.entries[i].enabled = previous;
                    break;
                }
            }
            sessiond_send_ack(slot, result < 0 ? result : 1, 0);
            continue;
        }
        if (type == LEONOS_SESSIOND_MSG_REMOVE) {
            struct leonos_startup_update update;
            if (length < sizeof(update)) continue;
            memcpy(&update, buffer, sizeof(update));
            if (client->uid && client->uid != update.uid) { sessiond_send_ack(slot, -EACCES, 0); continue; }
            int result = -ENOENT;
            for (uint32_t i = 0; i < db.count; ++i) {
                if (db.entries[i].id == update.entry_id && db.entries[i].uid == update.uid) {
                    struct sessiond_db previous = db;
                    for (uint32_t j = i + 1; j < db.count; ++j) {
                        db.entries[j - 1u] = db.entries[j];
                    }
                    --db.count;
                    result = sessiond_save();
                    if (result < 0) db = previous;
                    break;
                }
            }
            sessiond_send_ack(slot, result < 0 ? result : 1, 0);
            continue;
        }
        if (type == LEONOS_SESSIOND_MSG_LAUNCH_CURRENT) {
            if (client->uid) sessiond_send_ack(slot, -EACCES, 0);
            else sessiond_send_ack(slot, sessiond_launch_current() < 0 ? -EIO : 1, 0);
            continue;
        }
    }
}

void sessiond_poll(void)
{
    if (database_failed) return;
    if (listen_fd < 0) {
        if (sessiond_load() < 0) {
            fputs("[sessiond] invalid or ownerless legacy startup database; root recovery required\n", stderr);
            database_failed = 1;
            return;
        }
        listen_fd = leonos_ipc_bind_listen_mode(LEONOS_IPC_SOCK_SESSION, 8, 0666);
        if (listen_fd < 0) {
            printf("[sessiond] bind failed errno=%d\n", errno);
            return;
        }
        int passcred = 1;
        if (setsockopt(listen_fd, SOL_SOCKET, SO_PASSCRED, &passcred, sizeof(passcred)) < 0) {
            close(listen_fd); listen_fd = -1; return;
        }
        (void)leonos_ipc_set_nonblock(listen_fd, 1);
        printf("[sessiond] listening on %s\n", LEONOS_IPC_SOCK_SESSION);
    }
    {
        struct pollfd descriptor = {.fd = listen_fd, .events = POLLIN, .revents = 0};
        if (poll(&descriptor, 1, 0) > 0 && (descriptor.revents & POLLIN)) {
            int fd;
            while ((fd = leonos_ipc_accept(listen_fd, 0)) >= 0) {
                struct ucred credentials;
                int passcred = 1;
                int slot = -1;
                for (uint32_t i = 0; i < SESSIOND_MAX_CLIENTS; ++i) {
                    if (!clients[i].used) { slot = (int)i; break; }
                }
                if (slot < 0 || leonos_ipc_peer_credentials(fd, &credentials) < 0 ||
                    setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &passcred, sizeof(passcred)) < 0) {
                    close(fd);
                    continue;
                }
                (void)leonos_ipc_set_nonblock(fd, 1);
                clients[slot].used = 1;
                clients[slot].fd = fd;
                clients[slot].pid = (uint32_t)credentials.pid;
                clients[slot].uid = credentials.uid;
                clients[slot].credentials = credentials;
            }
        }
    }
    for (uint32_t i = 0; i < SESSIOND_MAX_CLIENTS; ++i) {
        if (clients[i].used) sessiond_handle_client(i);
    }
}
