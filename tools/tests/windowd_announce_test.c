#include <assert.h>
#include <stdarg.h>
#include <sys/un.h>
#define ioctl leonos_test_ioctl
#include <leonos/syscall.h>
#undef ioctl

#define main windowd_main
#define open test_open
#define ftruncate test_ftruncate
#define mmap test_mmap
#define munmap test_munmap
#include "../../userland/apps/windowd/main.c"
#undef main
#undef open
#undef ftruncate
#undef mmap
#undef munmap

static uint32_t backing[64];
static unsigned sends;
static unsigned delivered;
static unsigned incoming;
static uint32_t delivered_types[4];

int test_open(const char *path, int flags, ...) { (void)path; (void)flags; return 42; }
int test_ftruncate(int fd, off_t size) { (void)fd; (void)size; return 0; }
void *test_mmap(void *addr, size_t size, int prot, int flags, int fd, off_t off)
{
    (void)addr; (void)size; (void)prot; (void)flags; (void)fd; (void)off;
    return backing;
}
int test_munmap(void *addr, size_t size) { (void)addr; (void)size; return 0; }
int leonos_ipc_send_fd(int fd, uint32_t type, const void *p, uint32_t n, int sent_fd)
{
    (void)fd; (void)type; (void)p; (void)n; (void)sent_fd;
    return 0;
}
int leonos_ipc_send(int fd, uint32_t type, const void *payload, uint32_t length)
{
    (void)fd;
    assert(type == LEONOS_WIN_MSG_WINDOW_NOTIFY);
    assert(length == sizeof(struct leonos_gui_window_msg));
    if (++sends == 1) { errno = EAGAIN; return -1; }
    assert(delivered < 4);
    delivered_types[delivered++] = ((const struct leonos_gui_window_msg *)payload)->type;
    return 0;
}
int poll(struct pollfd *fds, nfds_t n, int timeout)
{
    (void)fds; (void)n; (void)timeout;
    return incoming == 0;
}
int leonos_ipc_recv_fd(int fd, uint32_t *type, void *payload, uint32_t capacity,
                       uint32_t *length, int *received_fd)
{
    (void)fd; (void)received_fd;
    struct leonos_win_present present = {.window_id = 1, .width = 8, .height = 8};
    assert(capacity >= sizeof(present));
    *type = LEONOS_WIN_MSG_PRESENT;
    *length = sizeof(present);
    memcpy(payload, &present, sizeof(present));
    ++incoming;
    return 0;
}

int main(void)
{
    clients[0] = (struct windowd_client){.used = 1, .fd = 4, .pid = 5,
        .role = LEONOS_WIN_ROLE_POLICY};
    clients[1] = (struct windowd_client){.used = 1, .fd = 5, .pid = 11,
        .role = LEONOS_WIN_ROLE_APP};
    policy_slot = 0;
    struct leonos_win_create request = {.width = 8, .height = 8, .title = "Terminal"};
    assert(create_window(&clients[1], &request) == 0);
    assert(sends == 1 && delivered == 0);
    handle_client(1);
    assert(delivered == 2);
    assert(delivered_types[0] == 1 && delivered_types[1] == 2);
    puts("Window creation survives a full policy socket");
    return 0;
}
