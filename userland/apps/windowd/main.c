/* windowd: userspace window registry and input-routing daemon.
 * SO_PEERCRED is the connection trust boundary.
 *
 * Apps open /run/leonos/windowd.sock through libwind and exchange window
 * metadata/events over AF_UNIX; pixel buffers are /dev/shm0 segments passed
 * with SCM_RIGHTS. Desktop remains the shell renderer and connects with the
 * policy handshake below. */
#include <errno.h>
#include <fcntl.h>
#include <leonos/device.h>
#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/unix_ipc.h>
#include <leonos/windowd.h>
#include <linux/input.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define WINDOWD_MAX_CLIENTS 24u
#define WINDOWD_MAX_WINDOWS 32u
#define WINDOWD_FRAME_CAP 4096u
#define WINDOWD_MAX_BYTES (1920ULL * 1080ULL * 4ULL)

struct windowd_client {
    uint32_t used;
    int fd;
    uint32_t pid;
    uint32_t uid;
    uint32_t role;
};

struct windowd_window {
    uint32_t used;
    uint32_t id;
    uint32_t owner_pid;
    uint32_t width;
    uint32_t height;
    uint32_t flags;
    uint32_t stride;
    uint64_t bytes;
    int shm_fd;
    void *mapping;
    char title[48];
    char text[1024];
};

static struct windowd_client clients[WINDOWD_MAX_CLIENTS];
static struct windowd_window windows[WINDOWD_MAX_WINDOWS];
static uint32_t next_window_id = 1u;
static int policy_slot = -1;
static uint32_t mouse_visible = 1u;
static struct leonos_display_state display_state;
static struct leonos_appearance_state appearance_state = {
    .theme = 1u,
    .wallpaper_mode = LEONOS_WALLPAPER_MODE_FILL,
};
static int keyboard_fd = -1;
static int mouse_fd = -1;
static int32_t cursor_x = 320;
static int32_t cursor_y = 240;
static uint8_t cursor_buttons;

static int copy_text(char *dst, uint32_t capacity, const char *src)
{
    uint32_t i = 0;
    if (!dst || !capacity) return -1;
    while (src && src[i] && i + 1u < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
    return 0;
}

static int text_eq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        ++a; ++b;
    }
    return *a == *b;
}

static int client_slot_by_pid(uint32_t pid)
{
    for (uint32_t i = 0; i < WINDOWD_MAX_CLIENTS; ++i) {
        if (clients[i].used && clients[i].pid == pid) return (int)i;
    }
    return -1;
}

static int client_slot_by_window(uint32_t window_id)
{
    for (uint32_t i = 0; i < WINDOWD_MAX_WINDOWS; ++i) {
        if (windows[i].used && windows[i].id == window_id) {
            return client_slot_by_pid(windows[i].owner_pid);
        }
    }
    return -1;
}

static struct windowd_window *find_window(uint32_t window_id)
{
    for (uint32_t i = 0; i < WINDOWD_MAX_WINDOWS; ++i) {
        if (windows[i].used && windows[i].id == window_id) return &windows[i];
    }
    return 0;
}

static void close_client(int slot)
{
    if (slot < 0 || slot >= (int)WINDOWD_MAX_CLIENTS || !clients[slot].used) return;
    close(clients[slot].fd);
    memset(&clients[slot], 0, sizeof(clients[slot]));
    clients[slot].fd = -1;
    if (policy_slot == slot) policy_slot = -1;
}

static void release_window(struct windowd_window *window)
{
    if (!window || !window->used) return;
    if (window->mapping && window->bytes) munmap(window->mapping, window->bytes);
    if (window->shm_fd >= 0) close(window->shm_fd);
    memset(window, 0, sizeof(*window));
    window->shm_fd = -1;
}

static int send_to_policy(uint32_t type, const void *payload, uint32_t length)
{
    if (policy_slot < 0) return -1;
    return leonos_ipc_send(clients[policy_slot].fd, type, payload, length);
}

static void notify_window_msg(struct leonos_gui_window_msg *message)
{
    (void)send_to_policy(LEONOS_WIN_MSG_WINDOW_NOTIFY, message, sizeof(*message));
}

static void window_msg_from_window(struct leonos_gui_window_msg *message,
                                   uint32_t type, const struct windowd_window *window)
{
    memset(message, 0, sizeof(*message));
    message->type = type;
    if (window) {
        message->pid = window->owner_pid;
        message->window_id = window->id;
        message->width = window->width;
        message->height = window->height;
        message->flags = window->flags;
        copy_text(message->title, sizeof(message->title), window->title);
        copy_text(message->text, sizeof(message->text), window->text);
    }
}

static int create_window(struct windowd_client *client,
                         const struct leonos_win_create *request)
{
    struct windowd_window *window = 0;
    struct leonos_win_create_ack ack;
    struct leonos_gui_window_msg message;
    uint64_t bytes;
    if (!client || !request || !request->width || !request->height ||
        request->width > LEONOS_GUI_MAX_WINDOW_WIDTH ||
        request->height > LEONOS_GUI_MAX_WINDOW_HEIGHT) {
        printf("[windowd.elf] create reject: bad geometry %ux%u\n",
               request ? request->width : 0, request ? request->height : 0);
        return -1;
    }
    for (uint32_t i = 0; i < WINDOWD_MAX_WINDOWS; ++i) {
        if (!windows[i].used) { window = &windows[i]; break; }
    }
    if (!window) {
        printf("[windowd.elf] create reject: window table full\n");
        return -1;
    }
    bytes = (uint64_t)request->width * request->height * 4ULL;
    if (bytes > WINDOWD_MAX_BYTES) {
        printf("[windowd.elf] create reject: too large\n");
        return -1;
    }
    memset(window, 0, sizeof(*window));
    window->shm_fd = -1;
    window->used = 1;
    window->id = next_window_id++;
    window->owner_pid = client->pid;
    window->width = request->width;
    window->height = request->height;
    window->flags = request->flags;
    window->stride = request->width * 4u;
    window->bytes = bytes;
    copy_text(window->title, sizeof(window->title), request->title);
    copy_text(window->text, sizeof(window->text), request->text);
    window->shm_fd = open(LEONOS_DEV_SHM0, LEONOS_O_RDWR, 0);
    if (window->shm_fd < 0 || ftruncate(window->shm_fd, (long)bytes) < 0) {
        printf("[windowd.elf] create fail: shm open/ftruncate errno=%d bytes=%llu\n",
               errno, (unsigned long long)bytes);
        release_window(window);
        return -1;
    }
    window->mapping = mmap(0, (size_t)bytes, PROT_READ | PROT_WRITE,
                           MAP_SHARED, window->shm_fd, 0);
    if (window->mapping == MAP_FAILED || !window->mapping) {
        printf("[windowd.elf] create fail: shm mmap errno=%d\n", errno);
        window->mapping = 0;
        release_window(window);
        return -1;
    }
    ack.window_id = window->id;
    ack.width = window->width;
    ack.height = window->height;
    ack.stride = window->stride;
    if (leonos_ipc_send_fd(client->fd, LEONOS_WIN_MSG_CREATE_ACK, &ack,
                           sizeof(ack), window->shm_fd) < 0) {
        printf("[windowd.elf] create fail: ack send errno=%d\n", errno);
        release_window(window);
        return -1;
    }
    window_msg_from_window(&message, 1u, window);
    notify_window_msg(&message);
    return 0;
}

static uint16_t evdev_to_legacy_keycode(uint16_t code)
{
    switch (code) {
    case KEY_HOME: return 71;
    case KEY_UP: return 72;
    case KEY_PAGEUP: return 73;
    case KEY_LEFT: return 75;
    case KEY_RIGHT: return 77;
    case KEY_END: return 79;
    case KEY_DOWN: return 80;
    case KEY_PAGEDOWN: return 81;
    case KEY_INSERT: return 82;
    case KEY_DELETE: return 83;
    case KEY_LEFTMETA: return 112;
    case KEY_RIGHTMETA: return 113;
    case KEY_COMPOSE: return 114;
    case KEY_RIGHTALT: return 115;
    case KEY_RIGHTCTRL: return 116;
    default: return (uint16_t)code;
    }
}

static void send_input_event(const struct leonos_input_event *event)
{
    (void)send_to_policy(LEONOS_WIN_MSG_INPUT, event, sizeof(*event));
}

static void pump_input_device(int fd, uint32_t type)
{
    for (;;) {
        struct input_event event;
        struct leonos_input_event out;
        long got = syscall3(SYS_read, fd, (long)&event, (long)sizeof(event));
        if (got != (long)sizeof(event)) break;
        memset(&out, 0, sizeof(out));
        if (type == LEONOS_INPUT_KEYBOARD && event.type == EV_KEY) {
            out.type = LEONOS_INPUT_KEYBOARD;
            out.keycode = (uint8_t)evdev_to_legacy_keycode(event.code);
            out.pressed = event.value ? 1 : 0;
            send_input_event(&out);
        } else if (type == LEONOS_INPUT_MOUSE) {
            if (event.type == EV_REL && event.code == REL_X) {
                cursor_x += event.value;
                if (cursor_x < 0) cursor_x = 0;
                if (cursor_x > 1920) cursor_x = 1920;
                out.type = LEONOS_INPUT_MOUSE;
                out.x = cursor_x;
                out.y = cursor_y;
                out.buttons = cursor_buttons;
                send_input_event(&out);
            } else if (event.type == EV_REL && event.code == REL_Y) {
                cursor_y += event.value;
                if (cursor_y < 0) cursor_y = 0;
                if (cursor_y > 1080) cursor_y = 1080;
                out.type = LEONOS_INPUT_MOUSE;
                out.x = cursor_x;
                out.y = cursor_y;
                out.buttons = cursor_buttons;
                send_input_event(&out);
            } else if (event.type == EV_REL && event.code == REL_WHEEL) {
                out.type = LEONOS_INPUT_MOUSE_WHEEL;
                out.x = cursor_x;
                out.y = cursor_y;
                out.dy = event.value;
                out.buttons = cursor_buttons;
                send_input_event(&out);
            } else if (event.type == EV_KEY &&
                       (event.code == BTN_LEFT || event.code == BTN_RIGHT ||
                        event.code == BTN_MIDDLE)) {
                uint8_t bit = event.code == BTN_LEFT ? 1u :
                              event.code == BTN_RIGHT ? 2u : 4u;
                if (event.value) cursor_buttons |= bit;
                else cursor_buttons &= (uint8_t)~bit;
                out.type = LEONOS_INPUT_MOUSE;
                out.x = cursor_x;
                out.y = cursor_y;
                out.buttons = cursor_buttons;
                send_input_event(&out);
            }
        }
    }
}

static void handle_client(int slot)
{
    struct windowd_client *client = &clients[slot];
    uint8_t buffer[WINDOWD_FRAME_CAP];
    uint32_t type = 0;
    uint32_t length = 0;
    for (;;) {
        struct pollfd descriptor = {.fd = client->fd, .events = POLLIN, .revents = 0};
        if (poll(&descriptor, 1, 0) <= 0) return;
        if (leonos_ipc_recv_fd(client->fd, &type, buffer, sizeof(buffer),
                               &length, 0) < 0) {
            if (errno == EAGAIN) return;
            close_client(slot);
            return;
        }
        if (type == LEONOS_WIN_MSG_HELLO) {
            struct leonos_win_hello hello;
            struct leonos_win_hello_ack ack = {.version = 1};
            if (length < sizeof(hello)) { close_client(slot); return; }
            memcpy(&hello, buffer, sizeof(hello));
            if (hello.pid != client->pid) { close_client(slot); return; }
            client->role = LEONOS_WIN_ROLE_APP;
            (void)leonos_ipc_send(client->fd, LEONOS_WIN_MSG_HELLO_ACK,
                                  &ack, sizeof(ack));
            continue;
        }
        if (type == LEONOS_WIN_MSG_POLICY_HELLO) {
            struct leonos_win_policy_hello hello;
            struct leonos_win_hello_ack ack = {.version = 1};
            if (length < sizeof(hello)) { close_client(slot); return; }
            memcpy(&hello, buffer, sizeof(hello));
            if (client->uid != 0 || !text_eq(hello.token, LEONOS_WIN_POLICY_TOKEN) ||
                hello.pid != client->pid) { close_client(slot); return; }
            if (policy_slot >= 0) { close_client(slot); return; }
            client->role = LEONOS_WIN_ROLE_POLICY;
            policy_slot = slot;
            (void)leonos_ipc_send(client->fd, LEONOS_WIN_MSG_HELLO_ACK,
                                  &ack, sizeof(ack));
            continue;
        }
        if (type == LEONOS_WIN_MSG_CREATE) {
            struct leonos_win_create request;
            if (length < sizeof(request) || client->role != LEONOS_WIN_ROLE_APP) {
                printf("[windowd.elf] create reject: role=%u length=%u\n",
                       client->role, length);
                continue;
            }
            memcpy(&request, buffer, sizeof(request));
            if (create_window(client, &request) < 0) {
                printf("[windowd.elf] create_window failed for pid=%u\n",
                       client->pid);
            }
            continue;
        }
        if (type == LEONOS_WIN_MSG_DESTROY) {
            struct leonos_win_destroy request;
            struct windowd_window *window;
            struct leonos_gui_window_msg message;
            if (length < sizeof(request)) continue;
            memcpy(&request, buffer, sizeof(request));
            window = find_window(request.window_id);
            if (window && window->owner_pid == client->pid) {
                window_msg_from_window(&message, 3u, window);
                release_window(window);
                notify_window_msg(&message);
            }
            continue;
        }
        if (type == LEONOS_WIN_MSG_PRESENT) {
            struct leonos_win_present request;
            struct windowd_window *window;
            struct leonos_gui_window_msg message;
            if (length < sizeof(request)) continue;
            memcpy(&request, buffer, sizeof(request));
            window = find_window(request.window_id);
            if (window && window->owner_pid == client->pid) {
                window_msg_from_window(&message, 2u, window);
                notify_window_msg(&message);
            }
            continue;
        }
        if (type == LEONOS_WIN_MSG_UPDATE) {
            struct leonos_win_update request;
            struct windowd_window *window;
            struct leonos_gui_window_msg message;
            if (length < sizeof(request)) continue;
            memcpy(&request, buffer, sizeof(request));
            window = find_window(request.window_id);
            if (window && window->owner_pid == client->pid) {
                if (request.mask & LEONOS_GUI_WINDOW_UPDATE_TITLE) {
                    copy_text(window->title, sizeof(window->title), request.title);
                }
                if (request.mask & (LEONOS_GUI_WINDOW_UPDATE_BORDERLESS |
                                    LEONOS_GUI_WINDOW_UPDATE_TASKBAR)) {
                    window->flags &= ~(LEONOS_GUI_WINDOW_BORDERLESS |
                                       LEONOS_GUI_WINDOW_HIDE_TASKBAR);
                    window->flags |= request.flags & (LEONOS_GUI_WINDOW_BORDERLESS |
                                                      LEONOS_GUI_WINDOW_HIDE_TASKBAR);
                }
                window_msg_from_window(&message, 4u, window);
                notify_window_msg(&message);
            }
            continue;
        }
        if (type == LEONOS_WIN_MSG_FETCH) {
            struct leonos_win_fetch request;
            struct windowd_window *window;
            struct leonos_win_fetch_ack ack;
            if (client->role != LEONOS_WIN_ROLE_POLICY || length < sizeof(request)) continue;
            memcpy(&request, buffer, sizeof(request));
            window = find_window(request.window_id);
            if (!window) continue;
            ack.window_id = window->id;
            ack.width = window->width;
            ack.height = window->height;
            ack.stride = window->stride;
            (void)leonos_ipc_send_fd(client->fd, LEONOS_WIN_MSG_FETCH_ACK,
                                     &ack, sizeof(ack), window->shm_fd);
            continue;
        }
        if (type == LEONOS_WIN_MSG_EVENT) {
            struct leonos_gui_app_event event;
            int target;
            if (client->role != LEONOS_WIN_ROLE_POLICY || length < sizeof(event)) continue;
            memcpy(&event, buffer, sizeof(event));
            target = client_slot_by_window(event.window_id);
            if (target >= 0) {
                (void)leonos_ipc_send(clients[target].fd, LEONOS_WIN_MSG_EVENT,
                                      &event, sizeof(event));
            }
            continue;
        }
        if (type == LEONOS_WIN_MSG_MOUSE_VISIBLE) {
            struct leonos_win_mouse_visible request;
            if (length < sizeof(request)) continue;
            memcpy(&request, buffer, sizeof(request));
            if (request.window_id == 0xffffffffu) {
                request.visible = mouse_visible;
                (void)leonos_ipc_send(client->fd, LEONOS_WIN_MSG_MOUSE_VISIBLE,
                                      &request, sizeof(request));
            } else {
                mouse_visible = request.visible;
            }
            continue;
        }
        if (type == LEONOS_WIN_MSG_CURSOR_REQUEST) {
            struct leonos_gui_cursor_request request;
            struct leonos_gui_window_msg message;
            if (length < sizeof(request)) continue;
            memcpy(&request, buffer, sizeof(request));
            memset(&message, 0, sizeof(message));
            message.type = 6u;
            message.window_id = request.window_id;
            message.width = (uint32_t)request.x;
            message.height = (uint32_t)request.y;
            message.data = request.style;
            message.flags = request.flags;
            notify_window_msg(&message);
            continue;
        }
        if (type == LEONOS_WIN_MSG_CURSOR_REGION) {
            struct leonos_gui_cursor_region_request request;
            struct leonos_gui_window_msg message;
            if (length < sizeof(request)) continue;
            memcpy(&request, buffer, sizeof(request));
            memset(&message, 0, sizeof(message));
            message.type = LEONOS_GUI_WINDOW_MSG_CURSOR_REGION;
            message.window_id = request.window_id;
            message.cursor_region_id = request.region_id;
            message.cursor_x = request.x;
            message.cursor_y = request.y;
            message.width = request.width;
            message.height = request.height;
            message.cursor_style = request.style;
            message.cursor_flags = request.flags;
            message.cursor_operation = request.operation;
            notify_window_msg(&message);
            continue;
        }
        if (type == LEONOS_WIN_MSG_TASKBAR) {
            struct leonos_win_taskbar request;
            struct leonos_gui_window_msg message;
            if (length < sizeof(request) || client->role != LEONOS_WIN_ROLE_POLICY) continue;
            memcpy(&request, buffer, sizeof(request));
            memset(&message, 0, sizeof(message));
            message.type = 5u;
            message.window_id = request.window_id;
            message.data = request.visible;
            notify_window_msg(&message);
            continue;
        }
        if (type == LEONOS_WIN_MSG_DISPLAY_STATE) {
            if (length == 0) {
                (void)leonos_ipc_send(client->fd, LEONOS_WIN_MSG_DISPLAY_STATE,
                                      &display_state, sizeof(display_state));
            } else if (client->role == LEONOS_WIN_ROLE_POLICY && length >= sizeof(display_state)) {
                memcpy(&display_state, buffer, sizeof(display_state));
            }
            continue;
        }
        if (type == LEONOS_WIN_MSG_APPEARANCE_STATE) {
            if (length == 0) {
                (void)leonos_ipc_send(client->fd, LEONOS_WIN_MSG_APPEARANCE_STATE,
                                      &appearance_state, sizeof(appearance_state));
            } else if (client->role == LEONOS_WIN_ROLE_POLICY && length >= sizeof(appearance_state)) {
                memcpy(&appearance_state, buffer, sizeof(appearance_state));
            }
            continue;
        }
        if (type == LEONOS_WIN_MSG_DISPLAY_REQUEST ||
            type == LEONOS_WIN_MSG_APPEARANCE_REQUEST) {
            if (client->role == LEONOS_WIN_ROLE_APP && length > 0) {
                (void)send_to_policy(type, buffer, length);
            }
            continue;
        }
    }
}

int main(void)
{
    int listen_fd;
    printf("[windowd.elf] starting pid=%d uid=%d\n", getpid(), getuid());
    memset(clients, 0, sizeof(clients));
    for (uint32_t i = 0; i < WINDOWD_MAX_CLIENTS; ++i) clients[i].fd = -1;
    for (uint32_t i = 0; i < WINDOWD_MAX_WINDOWS; ++i) windows[i].shm_fd = -1;
    listen_fd = leonos_ipc_bind_listen(LEONOS_IPC_SOCK_WINDOWD, 8);
    if (listen_fd < 0) {
        printf("[windowd.elf] bind failed errno=%d\n", errno);
        return 1;
    }
    (void)leonos_ipc_set_nonblock(listen_fd, 1);
    keyboard_fd = open(LEONOS_DEV_INPUT_EVENT0, LEONOS_O_RDONLY | O_NONBLOCK, 0);
    mouse_fd = open(LEONOS_DEV_INPUT_EVENT1, LEONOS_O_RDONLY | O_NONBLOCK, 0);
    printf("[windowd.elf] listening on %s keyboard=%d mouse=%d\n",
           LEONOS_IPC_SOCK_WINDOWD, keyboard_fd, mouse_fd);

    for (;;) {
        struct pollfd fds[3] = {
            {.fd = listen_fd, .events = POLLIN},
            {.fd = keyboard_fd, .events = POLLIN},
            {.fd = mouse_fd, .events = POLLIN},
        };
        int ready = poll(fds, 3, 8);
        if (ready < 0) continue;
        if (fds[0].revents & POLLIN) {
            int fd;
            while ((fd = leonos_ipc_accept(listen_fd, 0)) >= 0) {
                struct ucred credentials;
                int slot = -1;
                for (uint32_t i = 0; i < WINDOWD_MAX_CLIENTS; ++i) {
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
                clients[slot].role = 0;
                printf("[windowd.elf] client pid=%u uid=%u\n",
                       clients[slot].pid, clients[slot].uid);
            }
        }
        if (fds[1].revents & POLLIN) pump_input_device(keyboard_fd, LEONOS_INPUT_KEYBOARD);
        if (fds[2].revents & POLLIN) pump_input_device(mouse_fd, LEONOS_INPUT_MOUSE);
        for (uint32_t i = 0; i < WINDOWD_MAX_CLIENTS; ++i) {
            if (clients[i].used) handle_client(i);
        }
    }
}
