/* Versioned device/driver-management service client. */
#include <leonos/device.h>
#include <leonos/devmgr_service.h>
#include <leonos/driver.h>
#include <leonos/syscall.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static int open_service(const char *path)
{
    int fd = open(path, O_RDWR, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    return fd;
}

static int service_ioctl(const char *path, unsigned long request, void *arg)
{
    int fd = open_service(path);
    long result;
    if (fd < 0) return -1;
    result = syscall3(SYS_ioctl, fd, (long)request, (long)arg);
    close(fd);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return (int)result;
}

int system_device_list(system_device_info_t *devices, uint32_t capacity,
                       uint32_t *out_count)
{
    system_device_list_t query = {
        .capacity = capacity,
        .count = 0,
        .devices = devices,
    };
    int ret;
    if (!devices && capacity) {
        errno = EINVAL;
        return -1;
    }
    ret = service_ioctl(LEONOS_DEV_HWINFO, LEONOS_IOCTL_DEVICE_LIST, &query);
    if (out_count) *out_count = query.count;
    return ret;
}

int system_driver_list(system_driver_info_t *drivers, uint32_t capacity,
                       uint32_t *out_count)
{
    system_driver_list_t query = {
        .capacity = capacity,
        .count = 0,
        .drivers = drivers,
    };
    int ret;
    if (!drivers && capacity) {
        errno = EINVAL;
        return -1;
    }
    ret = service_ioctl(LEONOS_DEV_DRIVERCTL, LEONOS_IOCTL_DRIVER_LIST, &query);
    if (out_count) *out_count = query.count;
    return ret;
}

int system_driver_control(uint32_t action, const char *file)
{
    system_driver_control_t request = {
        .action = action,
        .flags = 0,
        .status = -1,
        .reserved = 0,
        .file = {0},
    };
    uint32_t index = 0;
    int ret;
    while (file && file[index] && index + 1U < sizeof(request.file)) {
        request.file[index] = file[index];
        ++index;
    }
    request.file[index] = 0;
    if (action != SYSTEM_DRIVER_CONTROL_RESCAN && !request.file[0]) {
        errno = EINVAL;
        return -1;
    }
    ret = service_ioctl(LEONOS_DEV_DRIVERCTL, LEONOS_IOCTL_DRIVER_CONTROL,
                        &request);
    if (ret < 0) {
        errno = request.status < 0 ? -request.status : errno;
        return -1;
    }
    return request.status;
}
