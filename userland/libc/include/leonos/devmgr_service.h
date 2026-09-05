#ifndef LEONOS_DEVMGR_SERVICE_H
#define LEONOS_DEVMGR_SERVICE_H

/* Versioned device/driver-management service SDK.
 *
 * system_device_list uses the registered /dev/hwinfo catalog service; driver
 * list/control use /dev/driverctl. Applications never use fd 3 or the
 * pre-migration leonos_device_* / leonos_driver_* ABI.
 */
#include <leonos/device.h>
#include <leonos/driver.h>
#include <stdint.h>

#define DEVMGR_SERVICE_ABI_VERSION 1U

typedef struct leonos_device_info system_device_info_t;
typedef struct leonos_device_list system_device_list_t;
typedef struct leonos_driver_info system_driver_info_t;
typedef struct leonos_driver_list system_driver_list_t;
typedef struct leonos_driver_control system_driver_control_t;

#define SYSTEM_DEVICE_MAX LEONOS_DEVICE_MAX
#define SYSTEM_DEVICE_DETAIL_LEN LEONOS_DEVICE_DETAIL_LEN

#define SYSTEM_DEVICE_CLASS_SYSTEM LEONOS_DEVICE_CLASS_SYSTEM
#define SYSTEM_DEVICE_CLASS_INPUT LEONOS_DEVICE_CLASS_INPUT
#define SYSTEM_DEVICE_CLASS_DISPLAY LEONOS_DEVICE_CLASS_DISPLAY
#define SYSTEM_DEVICE_CLASS_STORAGE LEONOS_DEVICE_CLASS_STORAGE
#define SYSTEM_DEVICE_CLASS_SERIAL LEONOS_DEVICE_CLASS_SERIAL
#define SYSTEM_DEVICE_CLASS_NETWORK LEONOS_DEVICE_CLASS_NETWORK
#define SYSTEM_DEVICE_CLASS_AUDIO LEONOS_DEVICE_CLASS_AUDIO

#define SYSTEM_DEVICE_FLAG_PRESENT LEONOS_DEVICE_FLAG_PRESENT
#define SYSTEM_DEVICE_FLAG_ACTIVE LEONOS_DEVICE_FLAG_ACTIVE
#define SYSTEM_DEVICE_FLAG_BOOT LEONOS_DEVICE_FLAG_BOOT

#define SYSTEM_DRIVER_MAX LEONOS_DRIVER_MAX
#define SYSTEM_DRIVER_STATE_LOADING LEONOS_DRIVER_STATE_LOADING
#define SYSTEM_DRIVER_STATE_LOADED LEONOS_DRIVER_STATE_LOADED
#define SYSTEM_DRIVER_STATE_DISABLED LEONOS_DRIVER_STATE_DISABLED
#define SYSTEM_DRIVER_STATE_FAILED LEONOS_DRIVER_STATE_FAILED
#define SYSTEM_DRIVER_FLAG_DISABLED LEONOS_DRIVER_FLAG_DISABLED

#define SYSTEM_DRIVER_CONTROL_RESCAN LEONOS_DRIVER_CONTROL_RESCAN
#define SYSTEM_DRIVER_CONTROL_LOAD LEONOS_DRIVER_CONTROL_LOAD
#define SYSTEM_DRIVER_CONTROL_UNLOAD LEONOS_DRIVER_CONTROL_UNLOAD
#define SYSTEM_DRIVER_CONTROL_FORCE_UNLOAD LEONOS_DRIVER_CONTROL_FORCE_UNLOAD
#define SYSTEM_DRIVER_CONTROL_ENABLE_BOOT LEONOS_DRIVER_CONTROL_ENABLE_BOOT
#define SYSTEM_DRIVER_CONTROL_DISABLE_BOOT LEONOS_DRIVER_CONTROL_DISABLE_BOOT

int system_device_list(system_device_info_t *devices, uint32_t capacity,
                       uint32_t *out_count);
int system_driver_list(system_driver_info_t *drivers, uint32_t capacity,
                       uint32_t *out_count);
int system_driver_control(uint32_t action, const char *file);

#endif
