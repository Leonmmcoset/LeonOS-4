#ifndef LEONOS_BOOT_HANDOFF_H
#define LEONOS_BOOT_HANDOFF_H

#include <stdint.h>

#define LEONOS_BOOT_HANDOFF_MAGIC 0x4c424f54u
#define LEONOS_BOOT_HANDOFF_VERSION 1u
#define LEONOS_KERNEL_SERVICES_VERSION 1u
#define LEONOS_MIDDLELAYER_API_VERSION 3u

#define LEONOS_MOUNT_POLICY_VERSION 1u
#define LEONOS_MOUNT_MAX_ENTRIES 8u
#define LEONOS_MOUNT_PATH_LEN 16u
#define LEONOS_MOUNT_SOURCE_LEN 64u

#define LEONOS_MOUNT_KIND_NONE 0u
#define LEONOS_MOUNT_KIND_FAT32_BOOT 1u
#define LEONOS_MOUNT_KIND_FAT32_RAMDISK 2u
#define LEONOS_MOUNT_KIND_DEVFS 3u
#define LEONOS_MOUNT_KIND_TARGET_ESP 4u

#define LEONOS_MOUNT_FLAG_READONLY 0x00000001u
#define LEONOS_MOUNT_FLAG_RUNTIME_ROOT 0x00000002u
#define LEONOS_MOUNT_FLAG_DEVICE_TREE 0x00000004u
#define LEONOS_MOUNT_FLAG_OPTIONAL 0x00000008u

struct leonos_boot_module_info {
    uint64_t start;
    uint64_t end;
    uint64_t entry;
    const char *path;
};

struct leonos_boot_handoff {
    uint32_t magic;
    uint32_t version;
    uint32_t multiboot_magic;
    uint32_t reserved;
    uint64_t multiboot_info;
    const char *cmdline;
    const char *bootloader;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint8_t framebuffer_bpp;
    uint8_t reserved_fb[7];
    uint64_t mmap_addr;
    uint32_t mmap_entry_size;
    uint32_t mmap_entry_count;
    uint64_t efi_mmap_addr;
    uint32_t efi_mmap_entry_size;
    uint32_t efi_mmap_entry_count;
    uint64_t rsdp_addr;
    uint64_t efi_system_table;
    struct leonos_boot_module_info loader;
    struct leonos_boot_module_info kernel;
    struct leonos_boot_module_info middlelayer;
    uint64_t middlelayer_api;
};

struct leonos_kernel_services {
    uint32_t version;
    uint32_t reserved;
    void (*log)(const char *message);
    void (*log_len)(const char *message, uint64_t len);
};

struct leonos_mount_entry {
    uint32_t drive;
    uint32_t kind;
    uint32_t flags;
    uint32_t reserved;
    uint64_t module_start;
    uint64_t module_len;
    char path[LEONOS_MOUNT_PATH_LEN];
    char source[LEONOS_MOUNT_SOURCE_LEN];
};

struct leonos_mount_policy {
    uint32_t version;
    uint32_t count;
    uint32_t root_drive;
    uint32_t flags;
    struct leonos_mount_entry entries[LEONOS_MOUNT_MAX_ENTRIES];
};

struct leonos_middlelayer_api {
    uint32_t version;
    uint32_t reserved;
    void *init;
    void *syscall;
    void *selftest;
    void *mount_policy;
    void *unicode_op;
};

typedef const struct leonos_middlelayer_api *(*leonos_middlelayer_module_init_fn)(
    const struct leonos_kernel_services *services,
    const struct leonos_boot_handoff *handoff);

#endif
