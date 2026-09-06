/* authd: LeonOS authentication daemon. Runs as uid==0 and owns
 * /system/config/users.db. SO_PEERCRED is the trust boundary for every
 * mutating request. */
#include <errno.h>
#include <leonos/auth.h>
#include <leonos/authd.h>
#include <leonos/fs.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/unix_ipc.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define AUTHD_USERS_DB "/system/config/users.db"
#define AUTHD_MAGIC 0x41555331U /* AUS1 */
#define AUTHD_HASH_LEN 16u
#define AUTHD_MAX_CLIENTS 16u
#define AUTHD_FRAME_CAP 4096u

struct authd_record {
    struct leonos_user_info user;
    uint8_t password_hash[AUTHD_HASH_LEN];
};

static struct authd_record users[LEONOS_AUTH_MAX_USERS];
static uint32_t user_count;
static uint32_t current_uid;
static int listen_fd = -1;

struct authd_client {
    uint32_t used;
    int fd;
    uint32_t pid;
    uint32_t uid;
};

static struct authd_client clients[AUTHD_MAX_CLIENTS];

static void authd_copy(char *dst, uint32_t capacity, const char *src)
{
    uint32_t i = 0;
    if (!dst || !capacity) return;
    while (src && src[i] && i + 1u < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int authd_text_eq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == 0 && *b == 0;
}

static uint32_t authd_text_len(const char *text)
{
    uint32_t n = 0;
    while (text && text[n]) ++n;
    return n;
}

static int authd_text_valid(const char *text, uint32_t capacity)
{
    uint32_t len = authd_text_len(text);
    return text && text[0] && len + 1u <= capacity;
}

static uint64_t authd_hash_mix(uint64_t hash, const char *text)
{
    while (text && *text) {
        hash ^= (uint8_t)*text++;
        hash *= 1099511628211ULL;
    }
    return hash;
}

static void authd_password_hash(const char *username, const char *password,
                                uint8_t out[AUTHD_HASH_LEN])
{
    uint64_t hash = 1469598103934665603ULL;
    hash = authd_hash_mix(hash, username);
    hash = authd_hash_mix(hash, ":");
    hash = authd_hash_mix(hash, password);
    for (uint32_t i = 0; i < AUTHD_HASH_LEN; ++i) {
        out[i] = (uint8_t)(hash >> ((i % 8u) * 8u));
    }
}

static int authd_load(void)
{
    uint32_t magic = 0;
    uint32_t count = 0;
    int fd = open(AUTHD_USERS_DB, LEONOS_O_RDONLY, 0);
    uint32_t got = 0;
    if (fd < 0) return fd;
    (void)read(fd, &magic, sizeof(magic));
    (void)read(fd, &count, sizeof(count));
    if (magic != AUTHD_MAGIC || count > LEONOS_AUTH_MAX_USERS) {
        close(fd);
        return -1;
    }
    while (got < count) {
        long n = read(fd, &users[got], sizeof(users[got]));
        if (n != (long)sizeof(users[got])) { close(fd); return -1; }
        ++got;
    }
    close(fd);
    user_count = count;
    return 0;
}

static int authd_save(void)
{
    uint32_t magic = AUTHD_MAGIC;
    int fd = open(AUTHD_USERS_DB,
                  LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    if (fd < 0) return fd;
    if (write(fd, &magic, sizeof(magic)) != (long)sizeof(magic) ||
        write(fd, &user_count, sizeof(user_count)) != (long)sizeof(user_count)) {
        close(fd);
        return -1;
    }
    for (uint32_t i = 0; i < user_count; ++i) {
        if (write(fd, &users[i], sizeof(users[i])) != (long)sizeof(users[i])) {
            close(fd);
            return -1;
        }
    }
    close(fd);
    return 0;
}

static struct authd_record *authd_find_uid(uint32_t uid)
{
    for (uint32_t i = 0; i < user_count; ++i) {
        if (users[i].user.uid == uid) return &users[i];
    }
    return 0;
}

static struct authd_record *authd_find_name(const char *name)
{
    for (uint32_t i = 0; i < user_count; ++i) {
        if (authd_text_eq(users[i].user.username, name)) return &users[i];
    }
    return 0;
}

static void authd_fill_status(struct leonos_auth_status *status)
{
    status->user_count = user_count;
    status->has_admin = 0;
    for (uint32_t i = 0; i < user_count; ++i) {
        if ((users[i].user.flags & LEONOS_AUTH_USER_DISABLED) == 0 &&
            users[i].user.role == LEONOS_AUTH_ROLE_ADMIN) {
            status->has_admin = 1;
        }
    }
}

static int authd_verify(const struct authd_record *record, const char *password)
{
    uint8_t hash[AUTHD_HASH_LEN];
    authd_password_hash(record->user.username, password, hash);
    for (uint32_t i = 0; i < AUTHD_HASH_LEN; ++i) {
        if (hash[i] != record->password_hash[i]) return 0;
    }
    return 1;
}

static void authd_send_ack(int slot, int32_t code)
{
    struct leonos_authd_ack ack = {.code = code};
    if (slot >= 0 && slot < AUTHD_MAX_CLIENTS && clients[slot].used) {
        (void)leonos_ipc_send(clients[slot].fd, LEONOS_AUTHD_MSG_ACK, &ack,
                              sizeof(ack));
    }
}

static void authd_handle_login(int slot, const uint8_t *buffer, uint32_t length)
{
    struct leonos_auth_login login;
    struct authd_record *record;
    if (length < sizeof(login)) { authd_send_ack(slot, -1); return; }
    memcpy(&login, buffer, sizeof(login));
    record = authd_find_name(login.username);
    if (!record || (record->user.flags & LEONOS_AUTH_USER_DISABLED) ||
        !authd_verify(record, login.password)) {
        memset(login.password, 0, sizeof(login.password));
        authd_send_ack(slot, -1);
        return;
    }
    memset(login.password, 0, sizeof(login.password));
    current_uid = record->user.uid;
    (void)leonos_ipc_send(clients[slot].fd, LEONOS_AUTHD_MSG_LOGIN,
                          &record->user, sizeof(record->user));
}

static void authd_handle_elevate(int slot, const uint8_t *buffer, uint32_t length)
{
    struct leonos_auth_login login;
    struct authd_record *record;
    if (length < sizeof(login) || clients[slot].uid != 0) { authd_send_ack(slot, -1); return; }
    memcpy(&login, buffer, sizeof(login));
    record = authd_find_name(login.username);
    memset(login.password, 0, sizeof(login.password));
    if (!record || record->user.role != LEONOS_AUTH_ROLE_ADMIN ||
        (record->user.flags & LEONOS_AUTH_USER_DISABLED) ||
        !authd_verify(record, login.password)) {
        authd_send_ack(slot, -1);
        return;
    }
    (void)leonos_ipc_send(clients[slot].fd, LEONOS_AUTHD_MSG_ELEVATE,
                          &record->user, sizeof(record->user));
}

static void authd_handle_create(int slot, const uint8_t *buffer, uint32_t length)
{
    struct leonos_authd_create create;
    struct authd_record *record;
    uint32_t uid = 1;
    uint8_t hash[AUTHD_HASH_LEN];
    if (length < sizeof(create)) { authd_send_ack(slot, -1); return; }
    memcpy(&create, buffer, sizeof(create));
    if (!authd_text_valid(create.username, sizeof(create.username)) ||
        !authd_text_valid(create.password, sizeof(create.password)) ||
        create.role > LEONOS_AUTH_ROLE_ADMIN) {
        memset(create.password, 0, sizeof(create.password));
        authd_send_ack(slot, -1);
        return;
    }
    if (user_count == LEONOS_AUTH_MAX_USERS || authd_find_name(create.username)) {
        memset(create.password, 0, sizeof(create.password));
        authd_send_ack(slot, -1);
        return;
    }
    if (clients[slot].uid != 0) {
        memset(create.password, 0, sizeof(create.password));
        authd_send_ack(slot, -1);
        return;
    }
    record = &users[user_count];
    memset(record, 0, sizeof(*record));
    while (authd_find_uid(uid)) ++uid;
    record->user.uid = uid;
    record->user.role = create.role;
    authd_copy(record->user.username, sizeof(record->user.username), create.username);
    {
        char home[LEONOS_AUTH_HOME_LEN];
        (void)snprintf(home, sizeof(home), "/home/%s", create.username);
        authd_copy(record->user.home, sizeof(record->user.home), home);
    }
    authd_password_hash(create.username, create.password, hash);
    memcpy(record->password_hash, hash, sizeof(hash));
    memset(create.password, 0, sizeof(create.password));
    ++user_count;
    if (authd_save() < 0) { --user_count; authd_send_ack(slot, -1); return; }
    (void)leonos_ipc_send(clients[slot].fd, LEONOS_AUTHD_MSG_CREATE,
                          &record->user, sizeof(record->user));
}

static void authd_handle_update(int slot, const uint8_t *buffer, uint32_t length)
{
    struct leonos_authd_update update;
    struct authd_record *record;
    if (length < sizeof(update)) { authd_send_ack(slot, -1); return; }
    memcpy(&update, buffer, sizeof(update));
    if (clients[slot].uid != 0 && clients[slot].uid != update.uid) {
        authd_send_ack(slot, -1);
        return;
    }
    record = authd_find_uid(update.uid);
    if (!record) { authd_send_ack(slot, -1); return; }
    if (update.mask & LEONOS_AUTH_UPDATE_ROLE) {
        if (update.role > LEONOS_AUTH_ROLE_ADMIN || clients[slot].uid != 0) {
            authd_send_ack(slot, -1);
            return;
        }
        record->user.role = update.role;
    }
    if (update.mask & LEONOS_AUTH_UPDATE_FLAGS) {
        record->user.flags = update.flags & LEONOS_AUTH_USER_DISABLED;
    }
    if (authd_save() < 0) { authd_send_ack(slot, -1); return; }
    authd_send_ack(slot, 1);
}

static void authd_handle_password(int slot, const uint8_t *buffer, uint32_t length)
{
    struct leonos_authd_password password;
    struct authd_record *record;
    uint8_t hash[AUTHD_HASH_LEN];
    if (length < sizeof(password)) { authd_send_ack(slot, -1); return; }
    memcpy(&password, buffer, sizeof(password));
    record = authd_find_uid(password.uid);
    if (!record || (clients[slot].uid != 0 && clients[slot].uid != password.uid)) {
        memset(password.old_password, 0, sizeof(password.old_password));
        memset(password.new_password, 0, sizeof(password.new_password));
        authd_send_ack(slot, -1);
        return;
    }
    if (clients[slot].uid != 0 && !authd_verify(record, password.old_password)) {
        memset(password.old_password, 0, sizeof(password.old_password));
        memset(password.new_password, 0, sizeof(password.new_password));
        authd_send_ack(slot, -1);
        return;
    }
    authd_password_hash(record->user.username, password.new_password, hash);
    memcpy(record->password_hash, hash, sizeof(hash));
    memset(password.old_password, 0, sizeof(password.old_password));
    memset(password.new_password, 0, sizeof(password.new_password));
    if (authd_save() < 0) { authd_send_ack(slot, -1); return; }
    authd_send_ack(slot, 1);
}

static void authd_handle_client(int slot)
{
    struct authd_client *client = &clients[slot];
    uint8_t buffer[AUTHD_FRAME_CAP];
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
        if (type == LEONOS_AUTHD_MSG_HELLO) {
            struct leonos_authd_hello hello;
            if (length < sizeof(hello)) { close(client->fd); memset(client,0,sizeof(*client)); client->fd=-1; return; }
            memcpy(&hello, buffer, sizeof(hello));
            if (hello.pid != client->pid) { close(client->fd); memset(client,0,sizeof(*client)); client->fd=-1; return; }
            authd_send_ack(slot, 1);
            continue;
        }
        if (type == LEONOS_AUTHD_MSG_STATUS) {
            struct leonos_auth_status status = {0};
            authd_fill_status(&status);
            (void)leonos_ipc_send(client->fd, LEONOS_AUTHD_MSG_STATUS,
                                  &status, sizeof(status));
            continue;
        }
        if (type == LEONOS_AUTHD_MSG_LIST) {
            struct leonos_authd_list request;
            struct leonos_authd_list_ack ack;
            uint8_t payload[AUTHD_FRAME_CAP];
            uint32_t offset = sizeof(ack);
            uint32_t count = 0;
            if (length < sizeof(request)) continue;
            memcpy(&request, buffer, sizeof(request));
            memset(&ack, 0, sizeof(ack));
            for (uint32_t i = 0; i < user_count; ++i) {
                struct leonos_user_info *user = &users[i].user;
                if ((user->flags & LEONOS_AUTH_USER_DISABLED) && !request.include_disabled) continue;
                if (request.capacity > 0 && count < request.capacity &&
                    offset + sizeof(*user) <= sizeof(payload)) {
                    memcpy(payload + offset, user, sizeof(*user));
                    offset += sizeof(*user);
                }
                ++count;
            }
            ack.count = count;
            memcpy(payload, &ack, sizeof(ack));
            (void)leonos_ipc_send(client->fd, LEONOS_AUTHD_MSG_LIST,
                                  payload, offset);
            continue;
        }
        if (type == LEONOS_AUTHD_MSG_LOGIN) { authd_handle_login(slot, buffer, length); continue; }
        if (type == LEONOS_AUTHD_MSG_ELEVATE) { authd_handle_elevate(slot, buffer, length); continue; }
        if (type == LEONOS_AUTHD_MSG_CURRENT) {
            struct authd_record *record;
            uint32_t uid = client->uid ? client->uid : current_uid;
            record = authd_find_uid(uid);
            if (record) {
                (void)leonos_ipc_send(client->fd, LEONOS_AUTHD_MSG_CURRENT,
                                      &record->user, sizeof(record->user));
            } else {
                authd_send_ack(slot, -1);
            }
            continue;
        }
        if (type == LEONOS_AUTHD_MSG_LOGOUT) {
            if (client->uid == 0 || client->uid == current_uid) current_uid = 0;
            authd_send_ack(slot, 0);
            continue;
        }
        if (type == LEONOS_AUTHD_MSG_CREATE) { authd_handle_create(slot, buffer, length); continue; }
        if (type == LEONOS_AUTHD_MSG_UPDATE) { authd_handle_update(slot, buffer, length); continue; }
        if (type == LEONOS_AUTHD_MSG_CHANGE_PASSWORD) { authd_handle_password(slot, buffer, length); continue; }
    }
}

int main(void)
{
    printf("[authd.elf] starting pid=%d uid=%d\n", getpid(), getuid());
    memset(users, 0, sizeof(users));
    memset(clients, 0, sizeof(clients));
    for (uint32_t i = 0; i < AUTHD_MAX_CLIENTS; ++i) clients[i].fd = -1;
    if (authd_load() < 0) {
        user_count = 0;
        (void)authd_save();
    }
    printf("[authd.elf] users.db loaded count=%u current_uid=%u\n",
           user_count, current_uid);
    listen_fd = leonos_ipc_bind_listen(LEONOS_IPC_SOCK_AUTH, 8);
    if (listen_fd < 0) {
        printf("[authd.elf] bind failed errno=%d\n", errno);
        return 1;
    }
    (void)leonos_ipc_set_nonblock(listen_fd, 1);
    printf("[authd.elf] listening on %s\n", LEONOS_IPC_SOCK_AUTH);
    for (;;) {
        struct pollfd descriptor = {.fd = listen_fd, .events = POLLIN, .revents = 0};
        if (poll(&descriptor, 1, 4) > 0 && (descriptor.revents & POLLIN)) {
            int fd;
            while ((fd = leonos_ipc_accept(listen_fd, 0)) >= 0) {
                struct ucred credentials;
                int slot = -1;
                for (uint32_t i = 0; i < AUTHD_MAX_CLIENTS; ++i) {
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
                printf("[authd.elf] client pid=%u uid=%u\n",
                       clients[slot].pid, clients[slot].uid);
            }
        }
        for (uint32_t i = 0; i < AUTHD_MAX_CLIENTS; ++i) {
            if (clients[i].used) authd_handle_client(i);
        }
    }
}
