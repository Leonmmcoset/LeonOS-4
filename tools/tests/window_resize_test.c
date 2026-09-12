#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

static int fail_allocation;
static int shm_open_for_test(const char *path, int flags, ...)
{
    if (fail_allocation) { errno = ENOMEM; return -1; }
    if (!strcmp(path, "/dev/shm0")) return memfd_create("window-test", MFD_CLOEXEC);
    return open(path, flags, 0600);
}

#define open shm_open_for_test
#define main windowd_main
#include "../../userland/apps/windowd/main.c"
#undef main
#include "../../userland/libc/src/wind.c"
#undef open

void leonos_ui_present_for_pixels(const uint32_t *pixels, uint32_t id)
{ (void)pixels; (void)id; }

static void fetch_and_check(uint32_t width, uint32_t height, uint32_t color)
{
    uint32_t pixels[32 * 24] = {0}, w = 0, h = 0;
    struct leonos_gui_window_msg message;
    /* Consume PRESENT before checking the compositor's retained mapping. */
    unsigned received = 0;
    for (unsigned attempt = 0; attempt < 1000 && !received; ++attempt) {
        while (leonos_gui_poll_window(&message) > 0)
            if (message.type == 2) received = 1;
        if (!received) usleep(1000);
    }
    assert(received);
    assert(leonos_gui_fetch_window(1, 32, 24, 32, pixels, &w, &h) > 0);
    if (w != width || h != height) {
        fprintf(stderr, "stale surface: got %ux%u, expected %ux%u\n", w, h, width, height);
        exit(1);
    }
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) assert(pixels[y * 32 + x] == color);
}

int main(void)
{
    int app[2], policy[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, app) == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, policy) == 0);
    int shm = memfd_create("initial", MFD_CLOEXEC);
    assert(shm >= 0 && ftruncate(shm, 8 * 8 * 4) == 0);
    void *mapping = mmap(NULL, 8 * 8 * 4, PROT_READ | PROT_WRITE, MAP_SHARED, shm, 0);
    assert(mapping != MAP_FAILED);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        close(app[0]); close(policy[0]);
        clients[0] = (struct windowd_client){.used = 1, .fd = app[1], .pid = 1, .role = LEONOS_WIN_ROLE_APP};
        clients[1] = (struct windowd_client){.used = 1, .fd = policy[1], .pid = 2, .role = LEONOS_WIN_ROLE_POLICY};
        policy_slot = 1;
        windows[0] = (struct windowd_window){.used = 1, .id = 1, .owner_pid = 1,
            .width = 8, .height = 8, .stride = 32, .bytes = 256, .mapping = mapping, .shm_fd = shm};
        for (;;) {
            handle_client(0);
            handle_client(1);
            if (!clients[0].used || !clients[1].used) _exit(0);
            usleep(1000);
        }
    }
    close(app[1]); close(policy[1]);
    wind_app_fd = app[0]; wind_policy_fd = policy[0];
    leonos_ipc_set_nonblock(app[0], 1); leonos_ipc_set_nonblock(policy[0], 1);
    wind_windows[0] = (struct wind_window){.id = 1, .fd = shm,
        .mapping = mapping, .bytes = 256, .stride = 32};
    const unsigned sizes[][2] = {{8, 8}, {32, 24}, {8, 8}, {24, 16}, {32, 24}};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        uint32_t pixels[32 * 24];
        uint32_t color = 0x120030 + i;
        for (unsigned p = 0; p < 32 * 24; ++p) pixels[p] = color;
        assert(leonos_gui_present_window(1, sizes[i][0], sizes[i][1], 32, pixels) > 0);
        fetch_and_check(sizes[i][0], sizes[i][1], color);
    }
    void *previous = wind_windows[0].mapping;
    uint32_t pixels[32 * 24] = {0};
    fail_allocation = 1;
    assert(leonos_gui_present_window(1, 16, 16, 32, pixels) < 0 && errno == ENOMEM);
    fail_allocation = 0;
    assert(wind_windows[0].mapping == previous);
    assert(leonos_gui_present_window(1, 32, 24, 16, pixels) < 0 && errno == EINVAL);
    assert(wind_windows[0].mapping == previous);
    /* Reject an undersized segment without replacing the displayed frame. */
    int invalid = memfd_create("undersized", MFD_CLOEXEC);
    assert(invalid >= 0 && ftruncate(invalid, 4) == 0);
    struct leonos_win_buffer request = {.window_id = 1, .width = 16, .height = 16, .stride = 64};
    assert(leonos_ipc_send_fd(app[0], LEONOS_WIN_MSG_BUFFER, &request, sizeof(request), invalid) == 0);
    struct leonos_win_buffer ack;
    uint32_t length;
    assert(wind_wait_type(app[0], LEONOS_WIN_MSG_BUFFER_ACK, &ack, sizeof(ack), &length, NULL) < 0);
    assert(errno == EINVAL);
    request.window_id = 2;
    assert(leonos_ipc_send_fd(app[0], LEONOS_WIN_MSG_BUFFER, &request, sizeof(request), invalid) == 0);
    assert(wind_wait_type(app[0], LEONOS_WIN_MSG_BUFFER_ACK, &ack, sizeof(ack), &length, NULL) < 0);
    assert(errno == EPERM);
    close(invalid);
    for (unsigned p = 0; p < 32 * 24; ++p) pixels[p] = 0x112233;
    assert(leonos_gui_present_window(1, 32, 24, 32, pixels) > 0);
    fetch_and_check(32, 24, 0x112233);
    close(app[0]); close(policy[0]);
    int status;
    assert(waitpid(child, &status, 0) == child && status == 0);
    puts("PASS window resize: real AF_UNIX/SCM_RIGHTS, grow/shrink/grow and every pixel");
    return 0;
}
