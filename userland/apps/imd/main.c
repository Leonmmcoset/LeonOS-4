/* imd: userspace input-method manager daemon.
 * Provider (oschinpt) keeps one persistent connection; applications submit
 * focused-key events and receive committed text over the same socket path. */
#include <errno.h>
#include <leonos/inputm.h>
#include <leonos/inputmd.h>
#include <leonos/stdio.h>
#include <leonos/unix_ipc.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define IMD_MAX_CLIENTS 24u
#define IMD_MAX_PROVIDERS 8u
#define IMD_MAX_CONTEXTS 64u
#define IMD_MAX_USERS 8u
#define IMD_FRAME_CAP 4096u

struct imd_client {
    uint32_t used;
    int fd;
    uint32_t pid;
    uint32_t uid;
    uint32_t role;
};

struct imd_provider {
    uint32_t used;
    uint32_t uid;
    uint32_t pid;
    int client_slot;
    struct leonos_inputm_provider provider;
};

struct imd_context {
    uint32_t used;
    uint32_t pid;
    struct leonos_inputm_context context;
};

struct imd_user {
    uint32_t used;
    struct leonos_inputm_state state;
};

static struct imd_client clients[IMD_MAX_CLIENTS];
static struct imd_provider providers[IMD_MAX_PROVIDERS];
static struct imd_context contexts[IMD_MAX_CONTEXTS];
static struct imd_user users[IMD_MAX_USERS];

static void imd_copy(char *dst, uint32_t capacity, const char *src)
{
    uint32_t i = 0;
    if (!dst || !capacity) return;
    while (src && src[i] && i + 1u < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int imd_text_valid(const char *text, uint32_t capacity)
{
    uint32_t i = 0;
    if (!text || !text[0]) return 0;
    while (text[i]) {
        if (i + 1u >= capacity) return 0;
        ++i;
    }
    return 1;
}

static int imd_text_eq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        ++a; ++b;
    }
    return *a == *b;
}

static void imd_close_client(int slot)
{
    if (slot < 0 || slot >= (int)IMD_MAX_CLIENTS || !clients[slot].used) return;
    close(clients[slot].fd);
    for (uint32_t i = 0; i < IMD_MAX_PROVIDERS; ++i) {
        if (providers[i].used && providers[i].client_slot == slot) {
            providers[i].used = 0;
        }
    }
    memset(&clients[slot], 0, sizeof(clients[slot]));
    clients[slot].fd = -1;
}

static int imd_send_ack(int client_slot, int32_t code)
{
    struct leonos_imd_ack ack = {.code = code};
    if (client_slot < 0 || client_slot >= (int)IMD_MAX_CLIENTS ||
        !clients[client_slot].used) return -1;
    return leonos_ipc_send(clients[client_slot].fd, LEONOS_IMD_MSG_ACK,
                           &ack, sizeof(ack));
}

static int imd_client_by_pid(uint32_t pid)
{
    for (uint32_t i = 0; i < IMD_MAX_CLIENTS; ++i) {
        if (clients[i].used && clients[i].pid == pid) return (int)i;
    }
    return -1;
}

static struct imd_user *imd_find_user(uint32_t uid)
{
    for (uint32_t i = 0; i < IMD_MAX_USERS; ++i) {
        if (users[i].used && users[i].state.uid == uid) return &users[i];
    }
    for (uint32_t i = 0; i < IMD_MAX_USERS; ++i) {
        if (!users[i].used) {
            users[i].used = 1;
            users[i].state.uid = uid;
            imd_copy(users[i].state.active_id, sizeof(users[i].state.active_id), "en");
            return &users[i];
        }
    }
    return 0;
}

static struct imd_provider *imd_find_provider(uint32_t uid, const char *id)
{
    for (uint32_t i = 0; i < IMD_MAX_PROVIDERS; ++i) {
        if (providers[i].used && providers[i].uid == uid &&
            imd_text_eq(providers[i].provider.id, id)) return &providers[i];
    }
    return 0;
}

static struct imd_context *imd_find_context(uint32_t pid, uint32_t window_id,
                                            int create)
{
    for (uint32_t i = 0; i < IMD_MAX_CONTEXTS; ++i) {
        if (contexts[i].used && contexts[i].pid == pid &&
            contexts[i].context.window_id == window_id) return &contexts[i];
    }
    if (!create) return 0;
    for (uint32_t i = 0; i < IMD_MAX_CONTEXTS; ++i) {
        if (!contexts[i].used) {
            contexts[i].used = 1;
            contexts[i].pid = pid;
            contexts[i].context.window_id = window_id;
            return &contexts[i];
        }
    }
    return 0;
}

static void imd_handle_register(int slot, const uint8_t *buffer, uint32_t length)
{
    struct leonos_inputm_provider input;
    struct imd_provider *slot_provider = 0;
    if (length < sizeof(input) || clients[slot].uid == 0) {
        imd_send_ack(slot, -1);
        return;
    }
    memcpy(&input, buffer, sizeof(input));
    if (!imd_text_valid(input.id, LEONOS_INPUTM_ID_LEN) ||
        !imd_text_valid(input.name, LEONOS_INPUTM_NAME_LEN) ||
        !imd_text_valid(input.abbreviation, LEONOS_INPUTM_ABBREV_LEN) ||
        input.startup_mode > LEONOS_INPUTM_START_ON_DEMAND ||
        (input.render_flags & ~(LEONOS_INPUTM_RENDER_CONTROLS | LEONOS_INPUTM_RENDER_PIXELS))) {
        imd_send_ack(slot, -1);
        return;
    }
    for (uint32_t i = 0; i < IMD_MAX_PROVIDERS; ++i) {
        if (providers[i].used && providers[i].uid == clients[slot].uid &&
            imd_text_eq(providers[i].provider.id, input.id)) {
            slot_provider = &providers[i];
            break;
        }
    }
    if (!slot_provider) {
        for (uint32_t i = 0; i < IMD_MAX_PROVIDERS; ++i) {
            if (!providers[i].used) { slot_provider = &providers[i]; break; }
        }
    }
    if (!slot_provider) {
        imd_send_ack(slot, -1);
        return;
    }
    slot_provider->used = 1;
    slot_provider->uid = clients[slot].uid;
    slot_provider->pid = clients[slot].pid;
    slot_provider->client_slot = slot;
    slot_provider->provider = input;
    slot_provider->provider.enabled = 1;
    clients[slot].role = LEONOS_IMD_ROLE_PROVIDER;
    imd_send_ack(slot, 1);
}

static void imd_handle_submit(int slot, const uint8_t *buffer, uint32_t length)
{
    struct leonos_inputm_key_event input;
    struct leonos_inputm_key_event event;
    struct imd_context *context;
    struct imd_user *user;
    struct imd_provider *provider;
    if (length < sizeof(input) || clients[slot].uid == 0) {
        imd_send_ack(slot, -1);
        return;
    }
    memcpy(&input, buffer, sizeof(input));
    context = imd_find_context(clients[slot].pid, input.window_id, 0);
    if (!context || !(context->context.flags & LEONOS_INPUTM_CONTEXT_FOCUSED) ||
        (context->context.flags & LEONOS_INPUTM_CONTEXT_SECURE)) {
        imd_send_ack(slot, 0);
        return;
    }
    user = imd_find_user(clients[slot].uid);
    if (!user || imd_text_eq(user->state.active_id, "en")) {
        imd_send_ack(slot, 0);
        return;
    }
    provider = imd_find_provider(clients[slot].uid, user->state.active_id);
    if (!provider || provider->client_slot < 0 ||
        provider->client_slot >= (int)IMD_MAX_CLIENTS ||
        !clients[provider->client_slot].used) {
        imd_copy(user->state.active_id, sizeof(user->state.active_id), "en");
        user->state.composition[0] = 0;
        user->state.candidate_count = 0;
        imd_send_ack(slot, 0);
        return;
    }
    event = input;
    event.client_pid = clients[slot].pid;
    event.context_flags = context->context.flags;
    event.caret_x = context->context.caret_x;
    event.caret_y = context->context.caret_y;
    event.caret_w = context->context.caret_w;
    event.caret_h = context->context.caret_h;
    if (leonos_ipc_send(clients[provider->client_slot].fd,
                        LEONOS_IMD_MSG_KEY_EVENT, &event, sizeof(event)) < 0) {
        imd_send_ack(slot, -1);
        return;
    }
    imd_send_ack(slot, 1);
}

static void imd_handle_result(int slot, const uint8_t *buffer, uint32_t length)
{
    struct leonos_inputm_result result;
    int target;
    struct imd_user *user;
    if (length < sizeof(result) || clients[slot].role != LEONOS_IMD_ROLE_PROVIDER) {
        imd_send_ack(slot, -1);
        return;
    }
    memcpy(&result, buffer, sizeof(result));
    user = imd_find_user(clients[slot].uid);
    if (user && result.type == LEONOS_INPUTM_RESULT_COMPOSITION) {
        imd_copy(user->state.composition, sizeof(user->state.composition), result.text);
        user->state.candidate_count = result.candidate_count;
        user->state.selected_candidate = result.selected_candidate;
        for (uint32_t i = 0; i < result.candidate_count && i < LEONOS_INPUTM_MAX_CANDIDATES; ++i) {
            imd_copy(user->state.candidates[i], sizeof(user->state.candidates[i]),
                     result.candidates[i]);
        }
    } else if (user && result.type == LEONOS_INPUTM_RESULT_COMMIT) {
        user->state.composition[0] = 0;
        user->state.candidate_count = 0;
    }
    target = imd_client_by_pid(result.client_pid);
    if (target >= 0 && clients[target].role == LEONOS_IMD_ROLE_APP) {
        (void)leonos_ipc_send(clients[target].fd, LEONOS_IMD_MSG_RESULT,
                              &result, sizeof(result));
    }
    imd_send_ack(slot, 1);
}

static void imd_handle_set_active(int slot, const uint8_t *buffer, uint32_t length)
{
    struct leonos_inputm_active_request input;
    struct imd_user *user;
    if (length < sizeof(input) || !input.uid ||
        (clients[slot].uid != input.uid && clients[slot].uid != 0) ||
        !imd_text_valid(input.id, LEONOS_INPUTM_ID_LEN)) {
        imd_send_ack(slot, -1);
        return;
    }
    user = imd_find_user(input.uid);
    if (!user) { imd_send_ack(slot, -1); return; }
    if (!imd_text_eq(input.id, "en") && !imd_find_provider(input.uid, input.id)) {
        imd_send_ack(slot, -1);
        return;
    }
    imd_copy(user->state.active_id, sizeof(user->state.active_id), input.id);
    user->state.composition[0] = 0;
    user->state.candidate_count = 0;
    imd_send_ack(slot, 1);
}

static void imd_handle_list(int slot, const uint8_t *buffer, uint32_t length)
{
    struct leonos_imd_list request;
    struct leonos_imd_list_ack ack;
    uint8_t payload[IMD_FRAME_CAP];
    uint32_t offset = sizeof(ack);
    uint32_t count = 0;
    if (length < sizeof(request) || !request.uid ||
        (clients[slot].uid != request.uid && clients[slot].uid != 0)) {
        imd_send_ack(slot, -1);
        return;
    }
    memcpy(&request, buffer, sizeof(request));
    ack.uid = request.uid;
    ack.count = 0;
    {
        struct leonos_inputm_provider builtin = {0};
        imd_copy(builtin.id, sizeof(builtin.id), "en");
        imd_copy(builtin.name, sizeof(builtin.name), "English");
        imd_copy(builtin.abbreviation, sizeof(builtin.abbreviation), "EN");
        builtin.enabled = 1;
        if (request.capacity > 0 && offset + sizeof(builtin) <= sizeof(payload)) {
            memcpy(payload + offset, &builtin, sizeof(builtin));
            offset += sizeof(builtin);
        }
        ++count;
    }
    for (uint32_t i = 0; i < IMD_MAX_PROVIDERS; ++i) {
        if (!providers[i].used || providers[i].uid != request.uid) continue;
        if (request.capacity > 0 && count < request.capacity &&
            offset + sizeof(providers[i].provider) <= sizeof(payload)) {
            memcpy(payload + offset, &providers[i].provider,
                   sizeof(providers[i].provider));
            offset += sizeof(providers[i].provider);
        }
        ++count;
    }
    ack.count = count;
    memcpy(payload, &ack, sizeof(ack));
    (void)leonos_ipc_send(clients[slot].fd, LEONOS_IMD_MSG_LIST_ACK,
                          payload, offset);
}

static void imd_handle_get_state(int slot, const uint8_t *buffer, uint32_t length)
{
    struct leonos_imd_get_state request;
    struct imd_user *user;
    if (length < sizeof(request) || !request.uid ||
        (clients[slot].uid != request.uid && clients[slot].uid != 0)) {
        imd_send_ack(slot, -1);
        return;
    }
    memcpy(&request, buffer, sizeof(request));
    user = imd_find_user(request.uid);
    if (!user) { imd_send_ack(slot, -1); return; }
    (void)leonos_ipc_send(clients[slot].fd, LEONOS_IMD_MSG_STATE_ACK,
                          &user->state, sizeof(user->state));
}

static void imd_handle_client(int slot)
{
    struct imd_client *client = &clients[slot];
    uint8_t buffer[IMD_FRAME_CAP];
    uint32_t type = 0;
    uint32_t length = 0;
    for (;;) {
        struct pollfd descriptor = {.fd = client->fd, .events = POLLIN, .revents = 0};
        if (poll(&descriptor, 1, 0) <= 0) return;
        if (leonos_ipc_recv(client->fd, &type, buffer, sizeof(buffer), &length) < 0) {
            if (errno == EAGAIN) return;
            imd_close_client(slot);
            return;
        }
        if (type == LEONOS_IMD_MSG_HELLO) {
            struct leonos_imd_hello hello;
            if (length < sizeof(hello)) { imd_close_client(slot); return; }
            memcpy(&hello, buffer, sizeof(hello));
            if (hello.pid != client->pid) { imd_close_client(slot); return; }
            client->role = hello.role;
            imd_send_ack(slot, 1);
            continue;
        }
        if (type == LEONOS_IMD_MSG_REGISTER) { imd_handle_register(slot, buffer, length); continue; }
        if (type == LEONOS_IMD_MSG_UNREGISTER) {
            for (uint32_t i = 0; i < IMD_MAX_PROVIDERS; ++i) {
                if (providers[i].used && providers[i].client_slot == slot) {
                    providers[i].used = 0;
                }
            }
            imd_send_ack(slot, 0);
            continue;
        }
        if (type == LEONOS_IMD_MSG_SUBMIT_KEY) { imd_handle_submit(slot, buffer, length); continue; }
        if (type == LEONOS_IMD_MSG_RESULT) { imd_handle_result(slot, buffer, length); continue; }
        if (type == LEONOS_IMD_MSG_SET_CONTEXT) {
            struct leonos_inputm_context context;
            if (length < sizeof(context) || !context.window_id) { imd_send_ack(slot, -1); continue; }
            memcpy(&context, buffer, sizeof(context));
            {
                struct imd_context *slot_context = imd_find_context(client->pid,
                                                                    context.window_id, 1);
                if (!slot_context) { imd_send_ack(slot, -1); continue; }
                slot_context->context = context;
                imd_send_ack(slot, 1);
            }
            continue;
        }
        if (type == LEONOS_IMD_MSG_SET_ACTIVE) { imd_handle_set_active(slot, buffer, length); continue; }
        if (type == LEONOS_IMD_MSG_LIST) { imd_handle_list(slot, buffer, length); continue; }
        if (type == LEONOS_IMD_MSG_GET_STATE) { imd_handle_get_state(slot, buffer, length); continue; }
        if (type == LEONOS_IMD_MSG_NOTIFY_CONFIG) {
            struct leonos_inputm_config_request request;
            struct imd_user *user;
            if (length < sizeof(request) || !request.uid) { imd_send_ack(slot, -1); continue; }
            memcpy(&request, buffer, sizeof(request));
            user = imd_find_user(request.uid);
            if (!user) { imd_send_ack(slot, -1); continue; }
            ++user->state.config_generation;
            if (!user->state.config_generation) user->state.config_generation = 1;
            imd_send_ack(slot, 1);
            continue;
        }
    }
}

int main(void)
{
    int listen_fd;
    printf("[imd.elf] starting pid=%d uid=%d\n", getpid(), getuid());
    memset(clients, 0, sizeof(clients));
    memset(providers, 0, sizeof(providers));
    memset(contexts, 0, sizeof(contexts));
    memset(users, 0, sizeof(users));
    for (uint32_t i = 0; i < IMD_MAX_CLIENTS; ++i) clients[i].fd = -1;
    listen_fd = leonos_ipc_bind_listen(LEONOS_IPC_SOCK_INPUT_METHOD, 8);
    if (listen_fd < 0) {
        printf("[imd.elf] bind failed errno=%d\n", errno);
        return 1;
    }
    (void)leonos_ipc_set_nonblock(listen_fd, 1);
    printf("[imd.elf] listening on %s\n", LEONOS_IPC_SOCK_INPUT_METHOD);
    for (;;) {
        struct pollfd descriptor = {.fd = listen_fd, .events = POLLIN, .revents = 0};
        if (poll(&descriptor, 1, 4) > 0 && (descriptor.revents & POLLIN)) {
            int fd;
            while ((fd = leonos_ipc_accept(listen_fd, 0)) >= 0) {
                struct ucred credentials;
                int slot = -1;
                for (uint32_t i = 0; i < IMD_MAX_CLIENTS; ++i) {
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
                printf("[imd.elf] client pid=%u uid=%u\n", clients[slot].pid,
                       clients[slot].uid);
            }
        }
        for (uint32_t i = 0; i < IMD_MAX_CLIENTS; ++i) {
            if (clients[i].used) imd_handle_client(i);
        }
    }
}
