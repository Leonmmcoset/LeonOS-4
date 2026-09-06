/* Versioned device/driver-management service client. The old /dev/hwinfo and
 * /dev/driverctl nodes are gone; requests now go to devmand. */
#include <leonos/device.h>
#include <leonos/devmgr_service.h>
#include <leonos/driver.h>

int system_device_list(system_device_info_t *devices, uint32_t capacity,
                       uint32_t *out_count)
{
    if (!devices && capacity) return -1;
    return leonos_device_list(devices, capacity, out_count);
}

int system_driver_list(system_driver_info_t *drivers, uint32_t capacity,
                       uint32_t *out_count)
{
    if (!drivers && capacity) return -1;
    return leonos_driver_list(drivers, capacity, out_count);
}

int system_driver_control(uint32_t action, const char *file)
{
    if (action != SYSTEM_DRIVER_CONTROL_RESCAN && (!file || !file[0])) return -1;
    return leonos_driver_control(action, file);
}
