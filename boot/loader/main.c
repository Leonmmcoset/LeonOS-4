#include <leonos/boot_handoff.h>
#include <stdint.h>
#include <stddef.h>

#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36d76289u
#define MULTIBOOT2_TAG_TYPE_END 0
#define MULTIBOOT2_TAG_TYPE_CMDLINE 1
#define MULTIBOOT2_TAG_TYPE_BOOT_LOADER_NAME 2
#define MULTIBOOT2_TAG_TYPE_BASIC_MEMINFO 4
#define MULTIBOOT2_TAG_TYPE_MMAP 6
#define MULTIBOOT2_TAG_TYPE_FRAMEBUFFER 8
#define MULTIBOOT2_TAG_TYPE_EFI64 12
#define MULTIBOOT2_TAG_TYPE_ACPI_OLD 14
#define MULTIBOOT2_TAG_TYPE_ACPI_NEW 15
#define MULTIBOOT2_TAG_TYPE_EFI_MMAP 17

#define EFI_BY_PROTOCOL 2ULL
#define EFI_FILE_MODE_READ 0x0000000000000001ULL
#define EFI_SUCCESS 0ULL
#define EFI_BUFFER_TOO_SMALL 0x8000000000000005ULL

#define EI_NIDENT 16
#define ET_EXEC 2
#define EM_X86_64 62
#define PT_LOAD 1

#define KERNEL_PATH "0:/system/kernel.sys"
#define MIDDLELAYER_PATH "0:/system/middlelayer.sys"
#define READ_BUFFER_SIZE (1024u * 1024u)

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

struct multiboot2_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    uint8_t entries[];
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

struct multiboot2_tag_efi_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t descr_size;
    uint32_t descr_vers;
    uint8_t efi_mmap[];
};

struct efi_guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};

struct efi_table_header {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
};

typedef uint64_t efi_status_t;
typedef void *efi_handle_t;

typedef efi_status_t (__attribute__((ms_abi)) *efi_handle_protocol_fn)(
    efi_handle_t handle,
    struct efi_guid *protocol,
    void **interface);
typedef efi_status_t (__attribute__((ms_abi)) *efi_locate_handle_buffer_fn)(
    uint64_t search_type,
    struct efi_guid *protocol,
    void *search_key,
    uint64_t *no_handles,
    efi_handle_t **buffer);

struct efi_boot_services {
    struct efi_table_header hdr;
    efi_status_t (*raise_tpl)(uint64_t tpl);
    void (*restore_tpl)(uint64_t tpl);
    char _pad1[88];
    efi_status_t (*install_protocol_interface)(void);
    efi_status_t (*reinstall_protocol_interface)(void);
    efi_status_t (*uninstall_protocol_interface)(void);
    efi_handle_protocol_fn handle_protocol;
    void *_reserved;
    efi_status_t (*register_protocol_notify)(void);
    efi_status_t (*locate_handle)(void);
    efi_status_t (*locate_device_path)(void);
    efi_status_t (*install_configuration_table)(void);
    efi_status_t (*load_image)(void);
    efi_status_t (*start_image)(void);
    efi_status_t (*exit)(void);
    efi_status_t (*unload_image)(void);
    efi_status_t (*exit_boot_services)(void);
    efi_status_t (*get_next_monotonic_count)(void);
    efi_status_t (*stall)(void);
    efi_status_t (*set_watchdog_timer)(void);
    efi_status_t (*connect_controller)(void);
    efi_status_t (*disconnect_controller)(void);
    efi_status_t (*open_protocol)(void);
    efi_status_t (*close_protocol)(void);
    efi_status_t (*open_protocol_information)(void);
    efi_status_t (*protocols_per_handle)(void);
    efi_locate_handle_buffer_fn locate_handle_buffer;
};

struct efi_system_table {
    struct efi_table_header hdr;
    uint16_t *firmware_vendor;
    uint32_t firmware_revision;
    void *console_in_handle;
    void *con_in;
    void *console_out_handle;
    void *con_out;
    void *standard_error_handle;
    void *std_err;
    void *runtime_services;
    struct efi_boot_services *boot_services;
};

struct efi_simple_file_system_protocol;
struct efi_file_protocol;

typedef efi_status_t (__attribute__((ms_abi)) *efi_open_volume_fn)(
    struct efi_simple_file_system_protocol *self,
    struct efi_file_protocol **root);
typedef efi_status_t (__attribute__((ms_abi)) *efi_file_open_fn)(
    struct efi_file_protocol *self,
    struct efi_file_protocol **new_handle,
    uint16_t *file_name,
    uint64_t open_mode,
    uint64_t attributes);
typedef efi_status_t (__attribute__((ms_abi)) *efi_file_close_fn)(
    struct efi_file_protocol *self);
typedef efi_status_t (__attribute__((ms_abi)) *efi_file_read_fn)(
    struct efi_file_protocol *self,
    uint64_t *buffer_size,
    void *buffer);
typedef efi_status_t (__attribute__((ms_abi)) *efi_file_get_info_fn)(
    struct efi_file_protocol *self,
    struct efi_guid *information_type,
    uint64_t *buffer_size,
    void *buffer);

struct efi_simple_file_system_protocol {
    uint64_t revision;
    efi_open_volume_fn open_volume;
};

struct efi_file_protocol {
    uint64_t revision;
    efi_file_open_fn open;
    efi_file_close_fn close;
    void *delete_file;
    efi_file_read_fn read;
    void *write;
    void *get_position;
    void *set_position;
    efi_file_get_info_fn get_info;
    void *set_info;
    void *flush;
    void *open_ex;
    void *read_ex;
    void *write_ex;
    void *flush_ex;
};

struct efi_time {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t pad1;
    uint32_t nanosecond;
    int16_t timezone;
    uint8_t daylight;
    uint8_t pad2;
};

struct efi_file_info {
    uint64_t size;
    uint64_t file_size;
    uint64_t physical_size;
    struct efi_time create_time;
    struct efi_time last_access_time;
    struct efi_time modification_time;
    uint64_t attribute;
    uint16_t file_name[1];
};

struct elf64_ehdr {
    unsigned char e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

extern void loader_outb(uint8_t value, uint16_t port);
extern uint8_t __loader_start[];
extern uint8_t __loader_end[];
void loader_main(uint32_t magic, uint32_t multiboot_info);

static uint8_t read_buffer[READ_BUFFER_SIZE] __attribute__((aligned(4096)));
static struct leonos_boot_handoff handoff;
static struct efi_boot_services *boot_services;
static struct efi_file_protocol *root_dir;

static struct efi_guid sfs_guid = {
    0x964e5b22,
    0x6459,
    0x11d2,
    {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b},
};

static struct efi_guid file_info_guid = {
    0x09576e92,
    0x6d3f,
    0x11d2,
    {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b},
};

static uintptr_t align8(uintptr_t value)
{
    return (value + 7u) & ~(uintptr_t)7u;
}

static void *memset_local(void *dst, int value, size_t len)
{
    uint8_t *p = (uint8_t *)dst;
    while (len--) {
        *p++ = (uint8_t)value;
    }
    return dst;
}

static void *memcpy_local(void *dst, const void *src, size_t len)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (len--) {
        *d++ = *s++;
    }
    return dst;
}

static void serial_putc(char ch)
{
    loader_outb((uint8_t)ch, 0x3f8);
}

static void serial_write(const char *s)
{
    while (s && *s) {
        if (*s == '\n') {
            serial_putc('\r');
        }
        serial_putc(*s++);
    }
}

static void serial_write_hex(uint64_t value)
{
    const char *digits = "0123456789abcdef";
    serial_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        serial_putc(digits[(value >> (uint32_t)shift) & 0xf]);
    }
}

static void serial_init(void)
{
    loader_outb(0x00, 0x3f9);
    loader_outb(0x80, 0x3fb);
    loader_outb(0x01, 0x3f8);
    loader_outb(0x00, 0x3f9);
    loader_outb(0x03, 0x3fb);
    loader_outb(0xc7, 0x3fa);
    loader_outb(0x0b, 0x3fc);
}

static int build_efi_path(const char *path, uint16_t *out, uint32_t cap)
{
    uint32_t pos = 0;
    if (!path || !out || cap < 2) {
        return -1;
    }
    if (path[0] == '0' && path[1] == ':' && path[2] == '/') {
        path += 3;
    }
    out[pos++] = '\\';
    while (*path) {
        if (pos + 1 >= cap) {
            return -1;
        }
        out[pos++] = (*path == '/') ? '\\' : (uint8_t)*path;
        ++path;
    }
    out[pos] = 0;
    return 0;
}

static int efi_open_root(uint64_t system_table_addr)
{
    struct efi_system_table *st = (struct efi_system_table *)(uintptr_t)system_table_addr;
    uint64_t handle_count = 0;
    efi_handle_t *handles = 0;
    efi_status_t status;

    if (!st || !st->boot_services || !st->boot_services->locate_handle_buffer ||
        !st->boot_services->handle_protocol) {
        serial_write("[loader] EFI boot services unavailable\n");
        return -1;
    }
    boot_services = st->boot_services;
    status = boot_services->locate_handle_buffer(EFI_BY_PROTOCOL, &sfs_guid, 0,
                                                 &handle_count, &handles);
    if (status != EFI_SUCCESS || !handles || handle_count == 0) {
        serial_write("[loader] SimpleFS locate failed\n");
        return -1;
    }
    for (uint64_t i = 0; i < handle_count; ++i) {
        struct efi_simple_file_system_protocol *fs = 0;
        struct efi_file_protocol *volume = 0;
        status = boot_services->handle_protocol(handles[i], &sfs_guid, (void **)&fs);
        if (status != EFI_SUCCESS || !fs || !fs->open_volume) {
            continue;
        }
        status = fs->open_volume(fs, &volume);
        if (status == EFI_SUCCESS && volume) {
            root_dir = volume;
            serial_write("[loader] EFI SimpleFS root ready\n");
            return 0;
        }
    }
    serial_write("[loader] no readable EFI FAT volume\n");
    return -1;
}

static int efi_read_file(const char *path, void *buffer, uint64_t cap, uint64_t *out_len)
{
    uint16_t efi_path[256];
    struct efi_file_protocol *file = 0;
    uint8_t info_buf[512];
    uint64_t info_size = sizeof(info_buf);
    uint64_t read_size;
    efi_status_t status;

    if (!root_dir || !root_dir->open || !out_len ||
        build_efi_path(path, efi_path, 256) < 0) {
        return -1;
    }
    *out_len = 0;
    status = root_dir->open(root_dir, &file, efi_path, EFI_FILE_MODE_READ, 0);
    if (status != EFI_SUCCESS || !file) {
        serial_write("[loader] open failed ");
        serial_write(path);
        serial_write(" status=");
        serial_write_hex(status);
        serial_write("\n");
        return -1;
    }
    if (!file->get_info || !file->read) {
        if (file->close) {
            file->close(file);
        }
        return -1;
    }
    status = file->get_info(file, &file_info_guid, &info_size, info_buf);
    if (status == EFI_BUFFER_TOO_SMALL || status != EFI_SUCCESS) {
        if (file->close) {
            file->close(file);
        }
        serial_write("[loader] get_info failed ");
        serial_write(path);
        serial_write("\n");
        return -1;
    }
    struct efi_file_info *info = (struct efi_file_info *)info_buf;
    if (info->file_size > cap) {
        if (file->close) {
            file->close(file);
        }
        serial_write("[loader] file too large ");
        serial_write(path);
        serial_write("\n");
        return -1;
    }
    read_size = info->file_size;
    status = file->read(file, &read_size, buffer);
    if (file->close) {
        file->close(file);
    }
    if (status != EFI_SUCCESS || read_size != info->file_size) {
        serial_write("[loader] read failed ");
        serial_write(path);
        serial_write("\n");
        return -1;
    }
    *out_len = read_size;
    serial_write("[loader] read ");
    serial_write(path);
    serial_write(" bytes=");
    serial_write_hex(read_size);
    serial_write("\n");
    return 0;
}

static int elf_load_exec(const void *image, uint64_t len,
                         struct leonos_boot_module_info *module)
{
    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)image;
    uint64_t low = UINT64_MAX;
    uint64_t high = 0;
    if (!image || len < sizeof(*eh) || !module) {
        return -1;
    }
    if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F' ||
        eh->e_ident[4] != 2 || eh->e_type != ET_EXEC ||
        eh->e_machine != EM_X86_64 ||
        eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize > len) {
        return -1;
    }
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const struct elf64_phdr *ph =
            (const struct elf64_phdr *)((const uint8_t *)image + eh->e_phoff +
                                        (uint64_t)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD) {
            continue;
        }
        if (ph->p_offset + ph->p_filesz > len || ph->p_filesz > ph->p_memsz) {
            return -1;
        }
        uint64_t dst = ph->p_paddr ? ph->p_paddr : ph->p_vaddr;
        if (!dst || dst + ph->p_memsz < dst) {
            return -1;
        }
        memcpy_local((void *)(uintptr_t)dst,
                     (const uint8_t *)image + ph->p_offset,
                     (size_t)ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz) {
            memset_local((void *)(uintptr_t)(dst + ph->p_filesz),
                         0,
                         (size_t)(ph->p_memsz - ph->p_filesz));
        }
        if (dst < low) {
            low = dst;
        }
        if (dst + ph->p_memsz > high) {
            high = dst + ph->p_memsz;
        }
    }
    if (low == UINT64_MAX || high <= low) {
        return -1;
    }
    module->start = low;
    module->end = high;
    module->entry = eh->e_entry;
    return 0;
}

static void parse_multiboot2(uint32_t magic, uint32_t info_addr)
{
    memset_local(&handoff, 0, sizeof(handoff));
    handoff.magic = LEONOS_BOOT_HANDOFF_MAGIC;
    handoff.version = LEONOS_BOOT_HANDOFF_VERSION;
    handoff.multiboot_magic = magic;
    handoff.multiboot_info = info_addr;
    handoff.loader.start = (uint64_t)(uintptr_t)__loader_start;
    handoff.loader.end = (uint64_t)(uintptr_t)__loader_end;
    handoff.loader.entry = (uint64_t)(uintptr_t)loader_main;
    handoff.loader.path = "0:/boot/loader.elf";
    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC || !info_addr) {
        serial_write("[loader] invalid Multiboot2 handoff\n");
        return;
    }

    const struct multiboot2_info *info = (const struct multiboot2_info *)(uintptr_t)info_addr;
    uintptr_t cursor = (uintptr_t)info_addr + sizeof(*info);
    uintptr_t end = (uintptr_t)info_addr + info->total_size;
    while (cursor < end) {
        const struct multiboot2_tag *tag = (const struct multiboot2_tag *)cursor;
        if (tag->type == MULTIBOOT2_TAG_TYPE_END || tag->size == 0) {
            break;
        }
        switch (tag->type) {
        case MULTIBOOT2_TAG_TYPE_CMDLINE:
            handoff.cmdline = ((const struct multiboot2_tag_string *)tag)->string;
            break;
        case MULTIBOOT2_TAG_TYPE_BOOT_LOADER_NAME:
            handoff.bootloader = ((const struct multiboot2_tag_string *)tag)->string;
            break;
        case MULTIBOOT2_TAG_TYPE_MMAP: {
            const struct multiboot2_tag_mmap *mmap = (const struct multiboot2_tag_mmap *)tag;
            handoff.mmap_addr = (uint64_t)(uintptr_t)mmap->entries;
            handoff.mmap_entry_size = mmap->entry_size;
            handoff.mmap_entry_count =
                mmap->entry_size ? (tag->size - sizeof(*mmap)) / mmap->entry_size : 0;
            break;
        }
        case MULTIBOOT2_TAG_TYPE_FRAMEBUFFER: {
            const struct multiboot2_tag_framebuffer *fb =
                (const struct multiboot2_tag_framebuffer *)tag;
            handoff.framebuffer_addr = fb->framebuffer_addr;
            handoff.framebuffer_width = fb->framebuffer_width;
            handoff.framebuffer_height = fb->framebuffer_height;
            handoff.framebuffer_pitch = fb->framebuffer_pitch;
            handoff.framebuffer_bpp = fb->framebuffer_bpp;
            break;
        }
        case MULTIBOOT2_TAG_TYPE_EFI64: {
            const uint64_t *efi = (const uint64_t *)((const uint8_t *)tag + 8);
            handoff.efi_system_table = *efi;
            break;
        }
        case MULTIBOOT2_TAG_TYPE_EFI_MMAP: {
            const struct multiboot2_tag_efi_mmap *mmap =
                (const struct multiboot2_tag_efi_mmap *)tag;
            handoff.efi_mmap_addr = (uint64_t)(uintptr_t)mmap->efi_mmap;
            handoff.efi_mmap_entry_size = mmap->descr_size;
            handoff.efi_mmap_entry_count =
                mmap->descr_size ? (tag->size - sizeof(*mmap)) / mmap->descr_size : 0;
            break;
        }
        case MULTIBOOT2_TAG_TYPE_ACPI_OLD:
        case MULTIBOOT2_TAG_TYPE_ACPI_NEW:
            handoff.rsdp_addr = (uint64_t)(uintptr_t)((const uint8_t *)tag + 8);
            break;
        default:
            break;
        }
        cursor = align8(cursor + tag->size);
    }
}

void loader_main(uint32_t magic, uint32_t multiboot_info)
{
    uint64_t len;

    serial_init();
    serial_write("[loader] LeonOS two-stage loader starting\n");
    parse_multiboot2(magic, multiboot_info);
    if (!handoff.efi_system_table || efi_open_root(handoff.efi_system_table) < 0) {
        serial_write("[loader] unable to open EFI filesystem\n");
        for (;;) {
            __asm__ volatile("hlt");
        }
    }

    if (efi_read_file(KERNEL_PATH, read_buffer, sizeof(read_buffer), &len) < 0 ||
        elf_load_exec(read_buffer, len, &handoff.kernel) < 0) {
        serial_write("[loader] kernel.sys load failed\n");
        for (;;) {
            __asm__ volatile("hlt");
        }
    }
    handoff.kernel.path = KERNEL_PATH;
    serial_write("[loader] kernel loaded entry=");
    serial_write_hex(handoff.kernel.entry);
    serial_write(" range=");
    serial_write_hex(handoff.kernel.start);
    serial_write("-");
    serial_write_hex(handoff.kernel.end);
    serial_write("\n");

    if (efi_read_file(MIDDLELAYER_PATH, read_buffer, sizeof(read_buffer), &len) < 0 ||
        elf_load_exec(read_buffer, len, &handoff.middlelayer) < 0) {
        serial_write("[loader] middlelayer.sys load failed\n");
        for (;;) {
            __asm__ volatile("hlt");
        }
    }
    handoff.middlelayer.path = MIDDLELAYER_PATH;
    serial_write("[loader] middlelayer loaded entry=");
    serial_write_hex(handoff.middlelayer.entry);
    serial_write(" range=");
    serial_write_hex(handoff.middlelayer.start);
    serial_write("-");
    serial_write_hex(handoff.middlelayer.end);
    serial_write("\n");

    serial_write("[loader] jumping to kernel\n");
    void (*entry)(const struct leonos_boot_handoff *) =
        (void (*)(const struct leonos_boot_handoff *))(uintptr_t)handoff.kernel.entry;
    entry(&handoff);
    for (;;) {
        __asm__ volatile("hlt");
    }
}
