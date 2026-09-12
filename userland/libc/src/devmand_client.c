/* devmand client: device/driver management over /run/leonos/devman.sock. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <errno.h>
#include <leonos/device.h>
#include <leonos/devmand.h>
#include <leonos/driver.h>
#include <leonos/unix_ipc.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEVMAND_FRAME_CAP 8192u
/* Short per-call retry plus backoff so a missing devmand never turns a
 * query into a multi-second stall. */
#define DEVMAND_CONNECT_ATTEMPT_MS 50u
#define DEVMAND_CONNECT_BACKOFF_MS 1000u

static int devmand_fd = -1;
static uint32_t devmand_retry_after_ms;

static uint32_t devmand_now_ms(void)
{
    struct timespec ts;
    (void)clock_gettime(1, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

static void devmand_copy(char *dst, uint32_t capacity, const char *src)
{
    uint32_t i = 0;
    if (!dst || !capacity) return;
    while (src && src[i] && i + 1u < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int devmand_wait(uint32_t expected, void *payload, uint32_t capacity,
                        uint32_t *length)
{
    uint32_t deadline = devmand_now_ms() + 3000u;
    for (;;) {
        uint8_t buffer[DEVMAND_FRAME_CAP];
        uint32_t type = 0;
        uint32_t got = 0;
        if (leonos_ipc_recv(devmand_fd, &type, buffer, sizeof(buffer), &got) == 0) {
            if (type == expected) {
                if (got > capacity) got = capacity;
                if (got) memcpy(payload, buffer, got);
                if (length) *length = got;
                return 0;
            }
        }
        if (devmand_now_ms() >= deadline) return -1;
        (void)poll(0, 0, 2);
    }
}

static int devmand_open(void)
{
    struct leonos_devmand_hello hello;
    struct leonos_devmand_ack ack;
    uint32_t deadline;
    if (devmand_fd >= 0) return devmand_fd;
    if (devmand_now_ms() < devmand_retry_after_ms) return -1;
    deadline = devmand_now_ms() + DEVMAND_CONNECT_ATTEMPT_MS;
    while (devmand_fd < 0 && devmand_now_ms() < deadline) {
        devmand_fd = leonos_ipc_connect(LEONOS_IPC_SOCK_DEVICE);
        if (devmand_fd < 0) (void)poll(0, 0, 10);
    }
    if (devmand_fd < 0) {
        devmand_retry_after_ms = devmand_now_ms() + DEVMAND_CONNECT_BACKOFF_MS;
        return -1;
    }
    (void)leonos_ipc_set_nonblock(devmand_fd, 1);
    hello.pid = (uint32_t)getpid();
    hello.uid = (uint32_t)getuid();
    if (leonos_ipc_send(devmand_fd, LEONOS_DEVMAND_MSG_HELLO, &hello,
                        sizeof(hello)) < 0 ||
        devmand_wait(LEONOS_DEVMAND_MSG_ACK, &ack, sizeof(ack), 0) < 0) {
        leonos_ipc_close(devmand_fd);
        devmand_fd = -1;
        devmand_retry_after_ms = devmand_now_ms() + DEVMAND_CONNECT_BACKOFF_MS;
        return -1;
    }
    return devmand_fd;
}

int leonos_device_list(struct leonos_device_info *devices,
                       uint32_t capacity, uint32_t *out_count)
{
    struct leonos_devmand_list_request request = {.capacity = capacity};
    struct leonos_devmand_ack ack;
    uint8_t buffer[DEVMAND_FRAME_CAP];
    uint32_t length = 0;
    if (out_count) *out_count = 0;
    if (devmand_open() < 0) return -1;
    if (leonos_ipc_send(devmand_fd, LEONOS_DEVMAND_MSG_DEVICE_LIST, &request,
                        sizeof(request)) < 0) return -1;
    if (devmand_wait(LEONOS_DEVMAND_MSG_DEVICE_LIST, buffer, sizeof(buffer),
                     &length) < 0) return -1;
    if (length < sizeof(ack)) return -1;
    memcpy(&ack, buffer, sizeof(ack));
    if (out_count) *out_count = ack.count;
    if (devices && capacity) {
        uint32_t count = ack.count < capacity ? ack.count : capacity;
        if (length - sizeof(ack) >= count * sizeof(*devices)) {
            memcpy(devices, buffer + sizeof(ack), count * sizeof(*devices));
        }
    }
    return 0;
}

int leonos_driver_list(struct leonos_driver_info *drivers, uint32_t capacity,
                       uint32_t *out_count)
{
    struct leonos_devmand_list_request request = {.capacity = capacity};
    struct leonos_devmand_ack ack;
    uint8_t buffer[DEVMAND_FRAME_CAP];
    uint32_t length = 0;
    if (out_count) *out_count = 0;
    if (devmand_open() < 0) return -1;
    if (leonos_ipc_send(devmand_fd, LEONOS_DEVMAND_MSG_DRIVER_LIST, &request,
                        sizeof(request)) < 0) return -1;
    if (devmand_wait(LEONOS_DEVMAND_MSG_DRIVER_LIST, buffer, sizeof(buffer),
                     &length) < 0) return -1;
    if (length < sizeof(ack)) return -1;
    memcpy(&ack, buffer, sizeof(ack));
    if (out_count) *out_count = ack.count;
    if (drivers && capacity) {
        uint32_t count = ack.count < capacity ? ack.count : capacity;
        if (length - sizeof(ack) >= count * sizeof(*drivers)) {
            memcpy(drivers, buffer + sizeof(ack), count * sizeof(*drivers));
        }
    }
    return 0;
}

int leonos_driver_control(uint32_t action, const char *file)
{
    struct leonos_driver_module request;
    struct leonos_devmand_ack ack;
    if (!file && action != LEONOS_DRIVER_CONTROL_RESCAN) { errno = EINVAL; return -1; }
    memset(&request, 0, sizeof(request));
    request.magic = LEONOS_DRIVER_MODULE_MAGIC;
    request.abi_version = LEONOS_DRIVER_ABI_VERSION;
    request.kind = action;
    if (file) devmand_copy(request.name, sizeof(request.name), file);
    if (devmand_open() < 0) return -1;
    if (leonos_ipc_send(devmand_fd, LEONOS_DEVMAND_MSG_DRIVER_CONTROL, &request,
                        sizeof(request)) < 0) return -1;
    if (devmand_wait(LEONOS_DEVMAND_MSG_ACK, &ack, sizeof(ack), 0) < 0) return -1;
    return ack.code;
}
