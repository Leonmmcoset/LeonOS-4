#include <assert.h>
#include <sys/un.h>

#include "../../userland/libc/src/wind.c"

static unsigned frame_index;
static int flood_mode;

int leonos_ipc_send(int fd, uint32_t type, const void *payload, uint32_t length)
{
    (void)fd; (void)type; (void)payload; (void)length;
    errno = EAGAIN;
    return -1;
}
int leonos_ipc_connect(const char *path) { (void)path; assert(0); return -1; }
int leonos_ipc_set_nonblock(int fd, int enabled) { (void)fd; (void)enabled; return 0; }
int leonos_ipc_close(int fd) { (void)fd; return 0; }

int poll(struct pollfd *fds, nfds_t count, int timeout)
{
    (void)timeout;
    if (!count) return 0;
    assert(count == 1 && fds[0].fd == 4);
    fds[0].revents = POLLIN;
    return 1;
}

int leonos_ipc_recv_fd(int fd, uint32_t *type, void *payload, uint32_t capacity,
                       uint32_t *length, int *received_fd)
{
    (void)fd;
    if (received_fd) *received_fd = -1;
    if (flood_mode) {
        struct leonos_gui_window_msg present = {.type = 2, .window_id = 1};
        assert(++frame_index <= 128); /* An unbounded pump starves the caller. */
        assert(capacity >= sizeof(present));
        memcpy(payload, &present, sizeof(present));
        *type = LEONOS_WIN_MSG_WINDOW_NOTIFY;
        *length = sizeof(present);
        return 0;
    }
    if (frame_index++ == 0) {
        struct leonos_input_event input = {
            .type = LEONOS_INPUT_MOUSE, .x = 420, .y = 250,
        };
        assert(capacity >= sizeof(input));
        *type = LEONOS_WIN_MSG_INPUT;
        *length = sizeof(input);
        memcpy(payload, &input, sizeof(input));
        /* LeonOS queues descriptors separately from bytes: a later FETCH_ACK
         * can already have attached its fd when this input frame is read. */
        if (received_fd) *received_fd = 42;
    } else {
        struct leonos_win_fetch_ack ack = {
            .window_id = 1, .width = 640, .height = 480, .stride = 2560,
        };
        assert(capacity >= sizeof(ack));
        *type = LEONOS_WIN_MSG_FETCH_ACK;
        *length = sizeof(ack);
        memcpy(payload, &ack, sizeof(ack));
    }
    return 0;
}

int main(void)
{
    struct leonos_win_fetch_ack ack = {0};
    uint32_t length = 0;
    int fd = -1;
    assert(wind_wait_type(4, LEONOS_WIN_MSG_FETCH_ACK, &ack, sizeof(ack),
                          &length, &fd) == 0);
    assert(ack.window_id == 1 && ack.width == 640 && length == sizeof(ack));
    if (fd != 42) {
        fprintf(stderr, "FETCH_ACK lost shared buffer descriptor: fd=%d\n", fd);
        return 1;
    }
    assert(wind_input_head != wind_input_tail);
    assert(wind_inputs[wind_input_tail].x == 420 && wind_inputs[wind_input_tail].y == 250);
    struct leonos_gui_window_msg present = {.type = 2, .window_id = 1};
    for (unsigned i = 0; i < 128; ++i) {
        present.width = 640 + i;
        wind_route_frame(LEONOS_WIN_MSG_WINDOW_NOTIFY, (const uint8_t *)&present,
                          sizeof(present));
    }
    assert(wind_msg_head != wind_msg_tail);
    assert((wind_msg_head + WIND_MSG_QUEUE - wind_msg_tail) % WIND_MSG_QUEUE == 1);
    wind_policy_fd = 4;
    assert(leonos_gui_mouse_visible() == 1);
    struct leonos_win_mouse_visible visible = {.window_id = 0xffffffffu, .visible = 0};
    wind_route_frame(LEONOS_WIN_MSG_MOUSE_VISIBLE, (const uint8_t *)&visible, sizeof(visible));
    assert(leonos_gui_mouse_visible() == 0);
    visible.visible = 1;
    wind_route_frame(LEONOS_WIN_MSG_MOUSE_VISIBLE, (const uint8_t *)&visible, sizeof(visible));
    assert(leonos_gui_mouse_visible() == 1);
    assert(wind_msgs[wind_msg_tail].width == 767);
    flood_mode = 1;
    frame_index = 0;
    assert(wind_pump_fd(4) == 1);
    assert(frame_index > 0 && frame_index < 128);
    assert((wind_msg_head + WIND_MSG_QUEUE - wind_msg_tail) % WIND_MSG_QUEUE == 1);
    wind_msg_head = wind_msg_tail = 0;
    struct leonos_gui_window_msg create = {.type = 1, .window_id = 2};
    wind_route_frame(LEONOS_WIN_MSG_WINDOW_NOTIFY, (const uint8_t *)&create,
                      sizeof(create));
    for (unsigned i = 0; i < 128; ++i) {
        struct leonos_gui_window_msg region = {
            .type = LEONOS_GUI_WINDOW_MSG_CURSOR_REGION, .window_id = 2,
            .cursor_region_id = 1, .cursor_operation = LEONOS_GUI_CURSOR_REGION_SET,
            .width = i + 1,
        };
        wind_route_frame(LEONOS_WIN_MSG_WINDOW_NOTIFY, (const uint8_t *)&region,
                          sizeof(region));
        present.window_id = 2;
        wind_route_frame(LEONOS_WIN_MSG_WINDOW_NOTIFY, (const uint8_t *)&present,
                          sizeof(present));
    }
    assert(wind_msgs[wind_msg_tail].type == 1);
    assert(wind_msgs[wind_msg_tail].window_id == 2);
    /* A queued lifecycle notification must be returned without reading a
     * further batch of frames over it. */
    frame_index = 0;
    struct leonos_gui_window_msg next;
    assert(leonos_gui_poll_window(&next) == 1 && next.type == 1);
    assert(frame_index == 0);
    puts("Window reply tests passed: interleaved input preserves reply descriptor");
    return 0;
}
