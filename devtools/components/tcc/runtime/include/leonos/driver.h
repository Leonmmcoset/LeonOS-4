#ifndef LEONOS_DRIVER_H
#define LEONOS_DRIVER_H

#include <stdint.h>
#include <leonos/audio.h>

#define LEONOS_DRIVER_ABI_VERSION 1U
#define LEONOS_DRIVER_MODULE_MAGIC 0x4c445256U
#define LEONOS_DRIVER_MAX 16U
#define LEONOS_DRIVER_FILE_LEN 64U
#define LEONOS_DRIVER_NAME_LEN 32U
#define LEONOS_DRIVER_ERROR_LEN 96U

#define LEONOS_IOCTL_DRIVER_LIST 0x4c44524cUL
#define LEONOS_IOCTL_DRIVER_CONTROL 0x4c445243UL

#define LEONOS_DRIVER_KIND_INPUT 1U
#define LEONOS_DRIVER_KIND_SERIAL 2U
#define LEONOS_DRIVER_KIND_NETWORK 3U
#define LEONOS_DRIVER_KIND_AUDIO 4U

#define LEONOS_DRIVER_STATE_UNLOADED 0U
#define LEONOS_DRIVER_STATE_LOADING 1U
#define LEONOS_DRIVER_STATE_LOADED 2U
#define LEONOS_DRIVER_STATE_DISABLED 3U
#define LEONOS_DRIVER_STATE_FAILED 4U

#define LEONOS_DRIVER_FLAG_AUTOSTART 0x00000001U
#define LEONOS_DRIVER_FLAG_DISABLED 0x00000002U
#define LEONOS_DRIVER_FLAG_BUILTIN 0x00000004U

#define LEONOS_DRIVER_CONTROL_LOAD 1U
#define LEONOS_DRIVER_CONTROL_UNLOAD 2U
#define LEONOS_DRIVER_CONTROL_FORCE_UNLOAD 3U
#define LEONOS_DRIVER_CONTROL_RESCAN 4U
#define LEONOS_DRIVER_CONTROL_ENABLE_BOOT 5U
#define LEONOS_DRIVER_CONTROL_DISABLE_BOOT 6U

struct leonos_driver_mouse_state {
    int32_t x;
    int32_t y;
    uint8_t buttons;
    uint8_t present;
    uint8_t absolute;
    uint8_t reserved;
    uint32_t event_count;
    uint8_t last_status;
    uint8_t last_data;
    uint8_t last_ack;
    uint8_t reserved2;
};

struct leonos_driver_pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t class_code;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t header_type;
    uint8_t reserved;
};

struct leonos_driver_e1000_info {
    uint32_t present;
    uint32_t active;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t reserved;
    uint8_t mac[6];
    uint8_t reserved2[2];
};

struct leonos_driver_mouse_ops {
    void (*poll)(void);
    void (*get_state)(struct leonos_driver_mouse_state *out);
};

struct leonos_driver_serial_ops {
    int (*is_ready)(void);
    void (*write)(const char *text);
};

struct leonos_driver_e1000_ops {
    int (*is_ready)(void);
    const uint8_t *(*mac)(void);
    int (*send)(const void *frame, uint32_t len);
    int (*poll)(void *frame, uint32_t capacity, uint32_t *out_len);
    void (*get_info)(struct leonos_driver_e1000_info *out);
};

struct leonos_driver_audio_ops {
    int (*is_ready)(void);
    int (*configure)(const struct leonos_audio_format *format);
    long (*write)(const void *data, uint32_t length, uint32_t *out_status);
    void (*get_state)(struct leonos_audio_state *out);
};

struct leonos_driver_kernel_api {
    uint32_t abi_version;
    uint32_t struct_size;
    uint8_t (*inb)(uint16_t port);
    void (*outb)(uint16_t port, uint8_t value);
    uint32_t (*inl)(uint16_t port);
    void (*outl)(uint16_t port, uint32_t value);
    uint64_t (*alloc_pages)(uint32_t page_count);
    void (*free_pages)(uint64_t address, uint32_t page_count);
    void (*console_write)(const char *text);
    void (*input_push_mouse)(int32_t x, int32_t y, int32_t dx, int32_t dy,
                             uint8_t buttons);
    void (*input_push_mouse_wheel)(int32_t x, int32_t y, int32_t wheel,
                                   uint8_t buttons);
    int (*framebuffer_size)(uint32_t *width, uint32_t *height);
    int (*pci_find)(uint16_t vendor_id, uint16_t device_id,
                    struct leonos_driver_pci_device *out);
    uint16_t (*pci_read16)(uint8_t bus, uint8_t slot, uint8_t function,
                           uint8_t offset);
    void (*pci_write16)(uint8_t bus, uint8_t slot, uint8_t function,
                        uint8_t offset, uint16_t value);
    uint32_t (*pci_read32)(uint8_t bus, uint8_t slot, uint8_t function,
                           uint8_t offset);
    uint64_t (*ticks)(void);
    void (*sleep_ms)(uint64_t ms);
    int (*register_mouse)(const struct leonos_driver_mouse_ops *ops);
    int (*register_serial)(const struct leonos_driver_serial_ops *ops);
    int (*register_e1000)(const struct leonos_driver_e1000_ops *ops);
    int (*register_audio)(const struct leonos_driver_audio_ops *ops);
};

struct leonos_driver_module {
    uint32_t magic;
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t kind;
    char name[LEONOS_DRIVER_NAME_LEN];
    uint32_t version;
    uint32_t reserved;
    int (*init)(const struct leonos_driver_kernel_api *api);
    void (*fini)(void);
};

struct leonos_driver_info {
    uint32_t id;
    uint32_t state;
    uint32_t kind;
    uint32_t flags;
    uint32_t abi_version;
    uint32_t version;
    uint64_t load_address;
    uint64_t image_size;
    char file[LEONOS_DRIVER_FILE_LEN];
    char name[LEONOS_DRIVER_NAME_LEN];
    char error[LEONOS_DRIVER_ERROR_LEN];
};

struct leonos_driver_list {
    uint32_t capacity;
    uint32_t count;
    struct leonos_driver_info *drivers;
};

struct leonos_driver_control {
    uint32_t action;
    uint32_t flags;
    int32_t status;
    uint32_t reserved;
    char file[LEONOS_DRIVER_FILE_LEN];
};

int leonos_driver_list(struct leonos_driver_info *drivers, uint32_t capacity,
                       uint32_t *out_count);
int leonos_driver_control(uint32_t action, const char *file);

#endif
