#ifndef NTCLKS_MULTIBOOT2_H
#define NTCLKS_MULTIBOOT2_H

#include <ntclks/types.h>

#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36d76289u
#define MULTIBOOT2_TAG_TYPE_END 0
#define MULTIBOOT2_TAG_TYPE_CMDLINE 1
#define MULTIBOOT2_TAG_TYPE_BOOT_LOADER_NAME 2
#define MULTIBOOT2_TAG_TYPE_MODULE 3
#define MULTIBOOT2_TAG_TYPE_BASIC_MEMINFO 4
#define MULTIBOOT2_TAG_TYPE_MMAP 6
#define MULTIBOOT2_TAG_TYPE_FRAMEBUFFER 8
#define MULTIBOOT2_TAG_TYPE_EFI64 12
#define MULTIBOOT2_TAG_TYPE_ACPI_OLD 14
#define MULTIBOOT2_TAG_TYPE_ACPI_NEW 15
#define MULTIBOOT2_TAG_TYPE_EFI_MMAP 17

struct multiboot2_info {
    uint32_t total_size;
    uint32_t reserved;
};

struct multiboot2_tag {
    uint32_t type;
    uint32_t size;
};

struct multiboot2_tag_string {
    uint32_t type;
    uint32_t size;
    char string[];
};

struct multiboot2_tag_module {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
    char string[];
};

struct multiboot2_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
};

struct multiboot2_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    struct multiboot2_mmap_entry entries[];
};

struct multiboot2_tag_framebuffer {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint16_t reserved;
};

#define MULTIBOOT2_FRAMEBUFFER_TYPE_INDEXED 0u
#define MULTIBOOT2_FRAMEBUFFER_TYPE_RGB 1u
#define MULTIBOOT2_FRAMEBUFFER_TYPE_EGA_TEXT 2u

#define MULTIBOOT2_FRAMEBUFFER_RGB_INFO_SIZE 6u

struct multiboot2_tag_efi_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t descr_size;
    uint32_t descr_vers;
    uint8_t efi_mmap[];
};

struct efi_memory_descriptor {
    uint32_t type;
    uint32_t pad;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
};

struct boot_module {
    uint64_t start;
    uint64_t end;
    const char *name;
};

struct boot_info {
    uint32_t magic;
    uint64_t multiboot_info;
    const char *cmdline;
    const char *bootloader;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint8_t framebuffer_red_field_position;
    uint8_t framebuffer_red_mask_size;
    uint8_t framebuffer_green_field_position;
    uint8_t framebuffer_green_mask_size;
    uint8_t framebuffer_blue_field_position;
    uint8_t framebuffer_blue_mask_size;
    uint64_t memory_lower_kib;
    uint64_t memory_upper_kib;
    uint64_t mmap_addr;
    uint32_t mmap_entry_size;
    uint32_t mmap_entry_count;
    uint64_t efi_mmap_addr;
    uint32_t efi_mmap_entry_size;
    uint32_t efi_mmap_entry_count;
    uint64_t rsdp_addr;
    uint64_t efi_system_table;
    struct boot_module modules[16];
    uint32_t module_count;
};

void multiboot2_parse(uint32_t magic, uintptr_t info_addr, struct boot_info *out);

#endif
