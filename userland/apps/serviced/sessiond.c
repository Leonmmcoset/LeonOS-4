/* sessiond, hosted by serviced: startup approval and session launch policy
 * over /run/leonos/session.sock. */
#include <errno.h>
#include <leonos/fs.h>
#include <leonos/launch.h>
#include <leonos/sessiond.h>
#include <leonos/startup.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/unix_ipc.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "sessiond.h"

#define SESSIOND_DB_PATH "/system/state/startup.db"
#define SESSIOND_MAGIC 0x53533131U /* SS11 */
#define SESSIOND_MAX_CLIENTS 16u
#define SESSIOND_MAX_ENTRIES LEONOS_STARTUP_MAX_ENTRIES
#define SESSIOND_FRAME_CAP 4096u
#define SESSIOND_SESSION_FILE "/run/leonos/session-user"

struct sessiond_client {
    uint32_t used;
    int fd;
    uint32_t pid;
    uint32_t uid;
};

struct sessiond_entry {
    uint32_t id;
    uint32_t enabled;
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

static uint32_t sessiond_read_uid(void)
{
    char text[16] = {0};
    int fd = open(SESSIOND_SESSION_FILE, LEONOS_O_RDONLY, 0);
    uint32_t value = 0;
    if (fd < 0) return 0;
    {
        long got = read(fd, text, sizeof(text) - 1u);
        (void)got;
    }
    close(fd);
    for (uint32_t i = 0; text[i] >= '0' && text[i] <= '9'; ++i) {
        value = value * 10u + (uint32_t)(text[i] - '0');
    }
    return value;
}

static void sessiond_copy(char *dst, uint32_t capacity, const char *src)
{
    uint32_t i = 0;
    if (!dst || !capacity) return;
    while (src && src[i] && i + 1u < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int sessiond_load(void)
{
    int fd = open(SESSIOND_DB_PATH, LEONOS_O_RDONLY, 0);
    uint32_t got = 0;
    if (fd < 0) return fd;
    (void)read(fd, &db.magic, sizeof(db.magic));
    (void)read(fd, &db.count, sizeof(db.count));
    (void)read(fd, &db.next_id, sizeof(db.next_id));
    if (db.magic != SESSIOND_MAGIC || db.count > SESSIOND_MAX_ENTRIES ||
        !db.next_id) {
        close(fd);
        return -1;
    }
    while (got < db.count) {
        long n = read(fd, &db.entries[got], sizeof(db.entries[got]));
        if (n != (long)sizeof(db.entries[got])) { close(fd); return -1; }
        ++got;
    }
    close(fd);
    return 0;
}

static int sessiond_save(void)
{
    int fd = open(SESSIOND_DB_PATH,
                  LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    if (fd < 0) return fd;
    if (write(fd, &db.magic, sizeof(db.magic)) != (long)sizeof(db.magic) ||
        write(fd, &db.count, sizeof(db.count)) != (long)sizeof(db.count) ||
        write(fd, &db.next_id, sizeof(db.next_id)) != (long)sizeof(db.next_id)) {
        close(fd);
        return -1;
    }
    for (uint32_t i = 0; i < db.count; ++i) {
        if (write(fd, &db.entries[i], sizeof(db.entries[i])) !=
            (long)sizeof(db.entries[i])) {
            close(fd);
            return -1;
        }
    }
    close(fd);
    return 0;
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
    uint32_t uid = clients[slot].uid ? clients[slot].uid : sessiond_read_uid();
    if (length < sizeof(command) || !uid || db.count >= SESSIOND_MAX_ENTRIES) {
        sessiond_send_ack(slot, LEONOS_STARTUP_STATUS_FAILED, 0);
        return;
    }
    memcpy(&command, buffer, sizeof(command));
    db.entries[db.count].id = db.next_id++;
    db.entries[db.count].enabled = 1;
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
    if (length < sizeof(request)) return;
    memcpy(&request, buffer, sizeof(request));
    memset(&ack, 0, sizeof(ack));
    ack.uid = request.uid;
    for (uint32_t i = 0; i < db.count; ++i) {
        struct leonos_startup_entry entry = {
            .id = db.entries[i].id,
            .enabled = db.entries[i].enabled,
            .command = db.entries[i].command,
        };
        /* uid is embedded in the database only through the session file for
         * this bootstrap implementation; store entries globally. */
        (void)entry;
        if (request.capacity > 0 && count < request.capacity &&
            offset + sizeof(struct leonos_startup_entry) <= sizeof(payload)) {
            memcpy(payload + offset, &entry, sizeof(entry));
            offset += sizeof(entry);
        }
        ++count;
    }
    ack.count = count;
    memcpy(payload, &ack, sizeof(ack));
    (void)leonos_ipc_send(clients[slot].fd, LEONOS_SESSIOND_MSG_LIST,
                          payload, offset);
}

static void sessiond_launch_current(void)
{
    uint32_t uid = sessiond_read_uid();
    if (!uid) return;
    for (uint32_t i = 0; i < db.count; ++i) {
        char *argv[LEONOS_STARTUP_MAX_ARGS + 2u];
        uint32_t argc = db.entries[i].command.argc;
        if (!db.entries[i].enabled || !db.entries[i].command.path[0] ||
            argc > LEONOS_STARTUP_MAX_ARGS) continue;
        argv[0] = db.entries[i].command.path;
        for (uint32_t j = 0; j < argc; ++j) {
            argv[j + 1u] = db.entries[i].command.args[j];
        }
        argv[argc + 1u] = 0;
        (void)leonos_spawn_argv(argv[0], argv);
    }
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
        if (leonos_ipc_recv(client->fd, &type, buffer, sizeof(buffer), &length) < 0) {
            if (errno == EAGAIN) return;
            close(client->fd);
            memset(client, 0, sizeof(*client));
            client->fd = -1;
            return;
        }
        if (type == LEONOS_SESSIOND_MSG_HELLO) {
            struct leonos_sessiond_hello hello;
            if (length < sizeof(hello)) { close(client->fd); memset(client,0,sizeof(*client)); client->fd=-1; return; }
            memcpy(&hello, buffer, sizeof(hello));
            if (hello.pid != client->pid || hello.uid != client->uid) {
                close(client->fd);
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
            sessiond_send_ack(slot, 1, 0);
            continue;
        }
        if (type == LEONOS_SESSIOND_MSG_LIST) { sessiond_list(slot, buffer, length); continue; }
        if (type == LEONOS_SESSIOND_MSG_SET_ENABLED) {
            struct leonos_startup_update update;
            if (length < sizeof(update)) continue;
            memcpy(&update, buffer, sizeof(update));
            for (uint32_t i = 0; i < db.count; ++i) {
                if (db.entries[i].id == update.entry_id) {
                    db.entries[i].enabled = update.enabled ? 1u : 0u;
                    (void)sessiond_save();
                }
            }
            sessiond_send_ack(slot, 1, 0);
            continue;
        }
        if (type == LEONOS_SESSIOND_MSG_REMOVE) {
            struct leonos_startup_update update;
            if (length < sizeof(update)) continue;
            memcpy(&update, buffer, sizeof(update));
            for (uint32_t i = 0; i < db.count; ++i) {
                if (db.entries[i].id == update.entry_id) {
                    for (uint32_t j = i + 1; j < db.count; ++j) {
                        db.entries[j - 1u] = db.entries[j];
                    }
                    --db.count;
                    (void)sessiond_save();
                    break;
                }
            }
            sessiond_send_ack(slot, 1, 0);
            continue;
        }
        if (type == LEONOS_SESSIOND_MSG_LAUNCH_CURRENT) {
            sessiond_launch_current();
            sessiond_send_ack(slot, 1, 0);
            continue;
        }
    }
}

void sessiond_poll(void)
{
    if (listen_fd < 0) {
        if (sessiond_load() < 0) {
            memset(&db, 0, sizeof(db));
            db.magic = SESSIOND_MAGIC;
            db.next_id = 1;
            (void)sessiond_save();
        }
        listen_fd = leonos_ipc_bind_listen(LEONOS_IPC_SOCK_SESSION, 8);
        if (listen_fd < 0) {
            printf("[sessiond] bind failed errno=%d\n", errno);
            return;
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
                int slot = -1;
                for (uint32_t i = 0; i < SESSIOND_MAX_CLIENTS; ++i) {
                    if (!clients[i].used) { slot = (int)i; break; }
                }
                if (slot < 0 || leonos_ipc_peer_credentials(fd, &credentials) < 0) {
                    close(fd);
                    continue;
                }
                (void)leonos_ipc_set_nonblock(fd, 1);
                clients[slot].used = 1;
                clients[slot].fd = fd;
                clients[slot].pid = (uint32_t)credentials.pid;
                clients[slot].uid = credentials.uid;
            }
        }
    }
    for (uint32_t i = 0; i < SESSIOND_MAX_CLIENTS; ++i) {
        if (clients[i].used) sessiond_handle_client(i);
    }
}
