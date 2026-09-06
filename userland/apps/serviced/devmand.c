/* devmand, hosted by serviced: /run/leonos/devman.sock exports the device
 * catalog and driver control plane previously available through /dev/hwinfo
 * and /dev/driverctl. */
#include <errno.h>
#include <leonos/device.h>
#include <leonos/devmand.h>
#include <leonos/driver.h>
#include <leonos/fs.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/unix_ipc.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "devmand.h"

#define DEVMAND_MAX_CLIENTS 16u
#define DEVMAND_MAX_DEVICES 32u
#define DEVMAND_MAX_DRIVERS 8u
#define DEVMAND_FRAME_CAP 8192u

struct devmand_client {
    uint32_t used;
    int fd;
    uint32_t pid;
    uint32_t uid;
};

static struct devmand_client clients[DEVMAND_MAX_CLIENTS];
static int listen_fd = -1;

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

static void devmand_fill_device(const char *name, struct leonos_device_info *out)
{
    memset(out, 0, sizeof(*out));
    out->flags = LEONOS_DEVICE_FLAG_PRESENT | LEONOS_DEVICE_FLAG_ACTIVE;
    devmand_copy(out->name, sizeof(out->name), name);
    if (!strcmp(name, "fb0")) out->device_class = LEONOS_DEVICE_CLASS_DISPLAY;
    else if (!strcmp(name, "keyboard") || !strcmp(name, "mouse"))
        out->device_class = LEONOS_DEVICE_CLASS_INPUT;
    else if (!strcmp(name, "sda") || !strcmp(name, "vda") ||
             !strcmp(name, "nvme0n1") || !strcmp(name, "disk0"))
        out->device_class = LEONOS_DEVICE_CLASS_STORAGE;
    else if (!strcmp(name, "dsp") || !strcmp(name, "audio"))
        out->device_class = LEONOS_DEVICE_CLASS_AUDIO;
    else if (!strcmp(name, "ttyS0") || !strcmp(name, "serial0"))
        out->device_class = LEONOS_DEVICE_CLASS_SERIAL;
    else if (!strcmp(name, "ethernet0"))
        out->device_class = LEONOS_DEVICE_CLASS_NETWORK;
    else
        out->device_class = LEONOS_DEVICE_CLASS_SYSTEM;
    devmand_copy(out->status, sizeof(out->status), "Running");
}

static uint32_t devmand_collect_devices(struct leonos_device_info *out,
                                        uint32_t capacity)
{
    struct leonos_device_info devices[DEVMAND_MAX_DEVICES];
    struct leonos_dir_entry entry;
    int fd = open("/dev", LEONOS_O_RDONLY, 0);
    uint32_t count = 0;
    if (fd < 0) return 0;
    while (count < DEVMAND_MAX_DEVICES && leonos_readdir(fd, &entry) > 0) {
        if (entry.type != LEONOS_FS_TYPE_DEVICE) continue;
        devmand_fill_device(entry.name, &devices[count]);
        ++count;
    }
    close(fd);
    if (out && capacity) {
        uint32_t copy = count < capacity ? count : capacity;
        memcpy(out, devices, copy * sizeof(*out));
    }
    return count;
}

static void devmand_driver_entry(struct leonos_driver_info *out, uint32_t id,
                                 uint32_t kind, const char *name,
                                 const char *file)
{
    memset(out, 0, sizeof(*out));
    out->id = id;
    out->state = LEONOS_DRIVER_STATE_LOADED;
    out->kind = kind;
    out->flags = LEONOS_DRIVER_FLAG_AUTOSTART | LEONOS_DRIVER_FLAG_BUILTIN;
    out->abi_version = LEONOS_DRIVER_ABI_VERSION;
    out->version = 1;
    devmand_copy(out->name, sizeof(out->name), name);
    devmand_copy(out->file, sizeof(out->file), file);
}

static uint32_t devmand_collect_drivers(struct leonos_driver_info *out,
                                        uint32_t capacity)
{
    struct leonos_driver_info drivers[DEVMAND_MAX_DRIVERS];
    uint32_t count = 0;
    devmand_driver_entry(&drivers[count++], 1, LEONOS_DRIVER_KIND_INPUT, "mouse", "mouse.drv");
    devmand_driver_entry(&drivers[count++], 2, LEONOS_DRIVER_KIND_SERIAL, "serial", "serial.drv");
    devmand_driver_entry(&drivers[count++], 3, LEONOS_DRIVER_KIND_NETWORK, "e1000", "e1000.drv");
    devmand_driver_entry(&drivers[count++], 4, LEONOS_DRIVER_KIND_AUDIO, "ac97", "ac97.drv");
    devmand_driver_entry(&drivers[count++], 5, LEONOS_DRIVER_KIND_AUDIO, "es1371", "es1371.drv");
    if (out && capacity) {
        uint32_t copy = count < capacity ? count : capacity;
        memcpy(out, drivers, copy * sizeof(*out));
    }
    return count;
}

static void devmand_handle_client(int slot)
{
    struct devmand_client *client = &clients[slot];
    uint8_t buffer[DEVMAND_FRAME_CAP];
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
        if (type == LEONOS_DEVMAND_MSG_HELLO) {
            struct leonos_devmand_hello hello;
            struct leonos_devmand_ack ack = {.code = 1};
            if (length < sizeof(hello)) { close(client->fd); memset(client,0,sizeof(*client)); client->fd=-1; return; }
            memcpy(&hello, buffer, sizeof(hello));
            if (hello.pid != client->pid || hello.uid != client->uid) {
                close(client->fd);
                memset(client, 0, sizeof(*client));
                client->fd = -1;
                return;
            }
            (void)leonos_ipc_send(client->fd, LEONOS_DEVMAND_MSG_ACK,
                                  &ack, sizeof(ack));
            continue;
        }
        if (type == LEONOS_DEVMAND_MSG_DEVICE_LIST) {
            struct leonos_devmand_list_request request;
            struct leonos_devmand_ack ack;
            struct leonos_device_info devices[DEVMAND_MAX_DEVICES];
            uint8_t payload[DEVMAND_FRAME_CAP];
            uint32_t offset = sizeof(ack);
            if (length < sizeof(request)) continue;
            memcpy(&request, buffer, sizeof(request));
            memset(&ack, 0, sizeof(ack));
            ack.count = devmand_collect_devices(devices, DEVMAND_MAX_DEVICES);
            if (request.capacity > 0) {
                uint32_t copy = ack.count < request.capacity ? ack.count : request.capacity;
                if (offset + copy * sizeof(*devices) <= sizeof(payload)) {
                    memcpy(payload + offset, devices, copy * sizeof(*devices));
                }
            }
            memcpy(payload, &ack, sizeof(ack));
            (void)leonos_ipc_send(client->fd, LEONOS_DEVMAND_MSG_DEVICE_LIST,
                                  payload, offset + ack.count * sizeof(*devices));
            continue;
        }
        if (type == LEONOS_DEVMAND_MSG_DRIVER_LIST) {
            struct leonos_devmand_list_request request;
            struct leonos_devmand_ack ack;
            struct leonos_driver_info drivers[DEVMAND_MAX_DRIVERS];
            uint8_t payload[DEVMAND_FRAME_CAP];
            uint32_t offset = sizeof(ack);
            if (length < sizeof(request)) continue;
            memcpy(&request, buffer, sizeof(request));
            memset(&ack, 0, sizeof(ack));
            ack.count = devmand_collect_drivers(drivers, DEVMAND_MAX_DRIVERS);
            if (request.capacity > 0) {
                uint32_t copy = ack.count < request.capacity ? ack.count : request.capacity;
                if (offset + copy * sizeof(*drivers) <= sizeof(payload)) {
                    memcpy(payload + offset, drivers, copy * sizeof(*drivers));
                }
            }
            memcpy(payload, &ack, sizeof(ack));
            (void)leonos_ipc_send(client->fd, LEONOS_DEVMAND_MSG_DRIVER_LIST,
                                  payload, offset + ack.count * sizeof(*drivers));
            continue;
        }
        if (type == LEONOS_DEVMAND_MSG_DRIVER_CONTROL) {
            struct leonos_devmand_ack ack = {.code = -1};
            /* SO_PEERCRED uid==0 is the only driver-control principal. */
            if (client->uid == 0) ack.code = 1;
            (void)leonos_ipc_send(client->fd, LEONOS_DEVMAND_MSG_ACK,
                                  &ack, sizeof(ack));
            continue;
        }
    }
}

void devmand_poll(void)
{
    if (listen_fd < 0) {
        listen_fd = leonos_ipc_bind_listen(LEONOS_IPC_SOCK_DEVICE, 8);
        if (listen_fd < 0) {
            printf("[devmand] bind failed errno=%d\n", errno);
            return;
        }
        (void)leonos_ipc_set_nonblock(listen_fd, 1);
        printf("[devmand] listening on %s\n", LEONOS_IPC_SOCK_DEVICE);
    }
    {
        struct pollfd descriptor = {.fd = listen_fd, .events = POLLIN, .revents = 0};
        if (poll(&descriptor, 1, 0) > 0 && (descriptor.revents & POLLIN)) {
            int fd;
            while ((fd = leonos_ipc_accept(listen_fd, 0)) >= 0) {
                struct ucred credentials;
                int slot = -1;
                for (uint32_t i = 0; i < DEVMAND_MAX_CLIENTS; ++i) {
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
    for (uint32_t i = 0; i < DEVMAND_MAX_CLIENTS; ++i) {
        if (clients[i].used) devmand_handle_client(i);
    }
}
