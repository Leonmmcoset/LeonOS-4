#include <leonos/boot_handoff.h>
#include <leonos/psf_font.h>
#include <generated/loader_integrity.h>
#include <generated/boot_logo.h>
#include <stdint.h>
#include <stddef.h>

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

#define EFI_BY_PROTOCOL 2ULL
#define EFI_FILE_MODE_READ 0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE 0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE 0x8000000000000000ULL
#define EFI_SUCCESS 0ULL
#define EFI_BUFFER_TOO_SMALL 0x8000000000000005ULL
#define EFI_NOT_READY 0x8000000000000006ULL
#define EFI_ALLOCATE_MAX_ADDRESS 1U
#define EFI_LOADER_DATA 2U

#define EI_NIDENT 16
#define ET_EXEC 2
#define EM_X86_64 62
#define PT_LOAD 1

#define KERNEL_PATH "/system/kernel.sys"
#define MIDDLELAYER_PATH "/system/middlelayer.sys"
#define READ_BUFFER_SIZE (1024u * 1024u)
#define EFI_MEMORY_MAP_BYTES (256u * 1024u)
#define LOADER_LOG_MAX_COLUMNS 512u
#define LOADER_LOG_MAX_ROWS 192u
#define LOADER_SPLASH_BACKGROUND 0x00ffffffu
#define LOADER_SPLASH_TRACK 0x00e6f2fbu
#define LOADER_SPLASH_PROGRESS 0x000078d4u
#define LOADER_PAGE_SIZE 4096ULL
#define LOADER_IDENTITY_MAP_LIMIT (4ULL * 1024ULL * 1024ULL * 1024ULL)

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

#define MULTIBOOT2_FRAMEBUFFER_TYPE_RGB 1u
#define MULTIBOOT2_FRAMEBUFFER_RGB_INFO_SIZE 6u

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
typedef efi_status_t (__attribute__((ms_abi)) *efi_stall_fn)(
    uint64_t microseconds);
typedef efi_status_t (__attribute__((ms_abi)) *efi_get_memory_map_fn)(
    uint64_t *memory_map_size,
    void *memory_map,
    uint64_t *map_key,
    uint64_t *descriptor_size,
    uint32_t *descriptor_version);
typedef efi_status_t (__attribute__((ms_abi)) *efi_allocate_pages_fn)(
    uint32_t allocation_type,
    uint32_t memory_type,
    uint64_t pages,
    uint64_t *memory);

struct efi_boot_services {
    struct efi_table_header hdr;
    efi_status_t (*raise_tpl)(uint64_t tpl);
    void (*restore_tpl)(uint64_t tpl);
    efi_allocate_pages_fn allocate_pages;
    void *free_pages;
    efi_get_memory_map_fn get_memory_map;
    void *allocate_pool;
    void *free_pool;
    void *create_event;
    void *set_timer;
    void *wait_for_event;
    void *signal_event;
    void *close_event;
    void *check_event;
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
    efi_stall_fn stall;
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
typedef efi_status_t (__attribute__((ms_abi)) *efi_file_delete_fn)(
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
typedef efi_status_t (__attribute__((ms_abi)) *efi_file_write_fn)(
    struct efi_file_protocol *self,
    uint64_t *buffer_size,
    const void *buffer);
typedef efi_status_t (__attribute__((ms_abi)) *efi_file_flush_fn)(
    struct efi_file_protocol *self);
typedef efi_status_t (__attribute__((ms_abi)) *efi_text_output_string_fn)(
    void *self,
    uint16_t *string);
typedef efi_status_t (__attribute__((ms_abi)) *efi_text_input_read_key_fn)(
    void *self,
    void *key);

struct efi_simple_file_system_protocol {
    uint64_t revision;
    efi_open_volume_fn open_volume;
};

struct efi_file_protocol {
    uint64_t revision;
    efi_file_open_fn open;
    efi_file_close_fn close;
    efi_file_delete_fn delete_file;
    efi_file_read_fn read;
    efi_file_write_fn write;
    void *get_position;
    void *set_position;
    efi_file_get_info_fn get_info;
    void *set_info;
    efi_file_flush_fn flush;
    void *open_ex;
    void *read_ex;
    void *write_ex;
    void *flush_ex;
};

struct efi_simple_text_output_protocol {
    void *reset;
    efi_text_output_string_fn output_string;
};

struct efi_simple_input_key {
    uint16_t scan_code;
    uint16_t unicode_char;
};

struct efi_simple_text_input_protocol {
    void *reset;
    efi_text_input_read_key_fn read_key_stroke;
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
extern uint8_t loader_inb(uint16_t port);
extern uint8_t __loader_start[];
extern uint8_t __loader_end[];
void loader_main(uint32_t magic, uint32_t multiboot_info);

static uint8_t read_buffer[READ_BUFFER_SIZE] __attribute__((aligned(4096)));
/* Captured after all EFI file reads.  The buffer lives inside the loader
 * image, which the kernel reserves before reclaiming usable pages. */
static uint8_t efi_memory_map[EFI_MEMORY_MAP_BYTES] __attribute__((aligned(4096)));
static struct leonos_boot_handoff handoff;
static struct efi_boot_services *boot_services;
static uint8_t loader_log_line_start = 1u;
static uint64_t loader_tsc_start;
static uint64_t loader_tsc_hz;
static uint8_t loader_tsc_ready;
static struct efi_file_protocol *root_dir;
static struct efi_simple_text_output_protocol *text_out;
static struct efi_simple_text_input_protocol *text_in;

struct loader_framebuffer_console {
    uint8_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bytes_per_pixel;
    uint32_t columns;
    uint32_t rows;
    uint32_t column;
    uint32_t row;
    uint32_t log_x;
    uint32_t log_y;
    uint32_t panel;
    uint32_t text;
    uint32_t line_count;
    uint8_t enabled;
};

static struct loader_framebuffer_console framebuffer_console;
static char framebuffer_log_lines[LOADER_LOG_MAX_ROWS][LOADER_LOG_MAX_COLUMNS + 1u];
static uint8_t loader_boot_log_screen;

struct loader_module {
    uint64_t start;
    uint64_t end;
    const char *name;
};

static struct loader_module loader_modules[16];
static uint32_t loader_module_count;

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

struct sha256_ctx {
    uint8_t data[64];
    uint32_t data_len;
    uint64_t bit_len;
    uint32_t state[8];
};

static const uint32_t sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static uint32_t rotr32(uint32_t value, uint32_t count)
{
    return (value >> count) | (value << (32u - count));
}

static void sha256_transform(struct sha256_ctx *ctx, const uint8_t data[64])
{
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t m[64];

    for (uint32_t i = 0; i < 16; ++i) {
        m[i] = ((uint32_t)data[i * 4u] << 24) |
               ((uint32_t)data[i * 4u + 1u] << 16) |
               ((uint32_t)data[i * 4u + 2u] << 8) |
               (uint32_t)data[i * 4u + 3u];
    }
    for (uint32_t i = 16; i < 64; ++i) {
        uint32_t s0 = rotr32(m[i - 15u], 7) ^ rotr32(m[i - 15u], 18) ^ (m[i - 15u] >> 3);
        uint32_t s1 = rotr32(m[i - 2u], 17) ^ rotr32(m[i - 2u], 19) ^ (m[i - 2u] >> 10);
        m[i] = m[i - 16u] + s0 + m[i - 7u] + s1;
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (uint32_t i = 0; i < 64; ++i) {
        uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + sha256_k[i] + m[i];
        uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(struct sha256_ctx *ctx)
{
    ctx->data_len = 0;
    ctx->bit_len = 0;
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
}

static void sha256_update(struct sha256_ctx *ctx, const void *input, uint64_t len)
{
    const uint8_t *data = (const uint8_t *)input;
    for (uint64_t i = 0; i < len; ++i) {
        ctx->data[ctx->data_len++] = data[i];
        if (ctx->data_len == 64u) {
            sha256_transform(ctx, ctx->data);
            ctx->bit_len += 512u;
            ctx->data_len = 0;
        }
    }
}

static void sha256_final(struct sha256_ctx *ctx, uint8_t hash[32])
{
    uint32_t i = ctx->data_len;

    ctx->data[i++] = 0x80u;
    if (i > 56u) {
        while (i < 64u) {
            ctx->data[i++] = 0;
        }
        sha256_transform(ctx, ctx->data);
        i = 0;
    }
    while (i < 56u) {
        ctx->data[i++] = 0;
    }

    ctx->bit_len += (uint64_t)ctx->data_len * 8u;
    ctx->data[56] = (uint8_t)(ctx->bit_len >> 56);
    ctx->data[57] = (uint8_t)(ctx->bit_len >> 48);
    ctx->data[58] = (uint8_t)(ctx->bit_len >> 40);
    ctx->data[59] = (uint8_t)(ctx->bit_len >> 32);
    ctx->data[60] = (uint8_t)(ctx->bit_len >> 24);
    ctx->data[61] = (uint8_t)(ctx->bit_len >> 16);
    ctx->data[62] = (uint8_t)(ctx->bit_len >> 8);
    ctx->data[63] = (uint8_t)ctx->bit_len;
    sha256_transform(ctx, ctx->data);

    for (i = 0; i < 4u; ++i) {
        hash[i] = (uint8_t)(ctx->state[0] >> (24u - i * 8u));
        hash[i + 4u] = (uint8_t)(ctx->state[1] >> (24u - i * 8u));
        hash[i + 8u] = (uint8_t)(ctx->state[2] >> (24u - i * 8u));
        hash[i + 12u] = (uint8_t)(ctx->state[3] >> (24u - i * 8u));
        hash[i + 16u] = (uint8_t)(ctx->state[4] >> (24u - i * 8u));
        hash[i + 20u] = (uint8_t)(ctx->state[5] >> (24u - i * 8u));
        hash[i + 24u] = (uint8_t)(ctx->state[6] >> (24u - i * 8u));
        hash[i + 28u] = (uint8_t)(ctx->state[7] >> (24u - i * 8u));
    }
}

static void sha256_digest(const void *input, uint64_t len, uint8_t hash[32])
{
    struct sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, input, len);
    sha256_final(&ctx, hash);
}

static int bytes_eq(const uint8_t *a, const uint8_t *b, uint64_t len)
{
    uint8_t diff = 0;
    for (uint64_t i = 0; i < len; ++i) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

static int loader_framebuffer_color_format_valid(void)
{
    if (handoff.framebuffer_type != MULTIBOOT2_FRAMEBUFFER_TYPE_RGB ||
        !handoff.framebuffer_red_mask_size ||
        !handoff.framebuffer_green_mask_size ||
        !handoff.framebuffer_blue_mask_size ||
        handoff.framebuffer_red_mask_size > 32u ||
        handoff.framebuffer_green_mask_size > 32u ||
        handoff.framebuffer_blue_mask_size > 32u) {
        return 0;
    }
    return handoff.framebuffer_red_field_position <=
               32u - handoff.framebuffer_red_mask_size &&
           handoff.framebuffer_green_field_position <=
               32u - handoff.framebuffer_green_mask_size &&
           handoff.framebuffer_blue_field_position <=
               32u - handoff.framebuffer_blue_mask_size;
}

static uint32_t loader_framebuffer_component_to_native(uint8_t component,
                                                        uint8_t field_position,
                                                        uint8_t mask_size)
{
    uint64_t max_value = mask_size == 32u ? 0xffffffffULL :
                         ((1ULL << mask_size) - 1ULL);
    uint64_t scaled = ((uint64_t)component * max_value + 127ULL) / 255ULL;
    return (uint32_t)(scaled << field_position);
}

static uint32_t loader_framebuffer_native_color(uint32_t color)
{
    uint8_t red_position = 16u;
    uint8_t red_size = 8u;
    uint8_t green_position = 8u;
    uint8_t green_size = 8u;
    uint8_t blue_position = 0u;
    uint8_t blue_size = 8u;

    if (loader_framebuffer_color_format_valid()) {
        red_position = handoff.framebuffer_red_field_position;
        red_size = handoff.framebuffer_red_mask_size;
        green_position = handoff.framebuffer_green_field_position;
        green_size = handoff.framebuffer_green_mask_size;
        blue_position = handoff.framebuffer_blue_field_position;
        blue_size = handoff.framebuffer_blue_mask_size;
    }

    return loader_framebuffer_component_to_native((uint8_t)(color >> 16),
                                                  red_position, red_size) |
           loader_framebuffer_component_to_native((uint8_t)(color >> 8),
                                                  green_position, green_size) |
           loader_framebuffer_component_to_native((uint8_t)color,
                                                  blue_position, blue_size);
}

static uint8_t loader_framebuffer_bytes_per_pixel(uint32_t pitch, uint32_t width,
                                                   uint8_t bpp)
{
    uint32_t expected = ((uint32_t)bpp + 7u) / 8u;

    if (!width) {
        return 0;
    }
    if ((expected == 3u || expected == 4u) &&
        (uint64_t)width * expected <= pitch) {
        return (uint8_t)expected;
    }
    if (pitch % width == 0u) {
        uint32_t actual = pitch / width;
        if (actual == 3u || actual == 4u) {
            return (uint8_t)actual;
        }
    }
    return 0;
}

static void loader_framebuffer_write_native(uint8_t *pixel, uint32_t color)
{
    for (uint8_t byte = 0; byte < framebuffer_console.bytes_per_pixel; ++byte) {
        pixel[byte] = (uint8_t)(color >> (byte * 8u));
    }
}

static void loader_framebuffer_fill(uint32_t x, uint32_t y, uint32_t width,
                                    uint32_t height, uint32_t color)
{
    uint32_t native_color;
    if (!framebuffer_console.enabled || x >= framebuffer_console.width ||
        y >= framebuffer_console.height) {
        return;
    }
    if (width > framebuffer_console.width - x) {
        width = framebuffer_console.width - x;
    }
    if (height > framebuffer_console.height - y) {
        height = framebuffer_console.height - y;
    }
    native_color = loader_framebuffer_native_color(color);
    for (uint32_t row = 0; row < height; ++row) {
        uint8_t *line = framebuffer_console.pixels +
                        (uint64_t)(y + row) * framebuffer_console.pitch +
                        (uint64_t)x * framebuffer_console.bytes_per_pixel;
        for (uint32_t column = 0; column < width; ++column) {
            loader_framebuffer_write_native(line +
                                                (uint64_t)column * framebuffer_console.bytes_per_pixel,
                                            native_color);
        }
    }
}

static uint32_t loader_framebuffer_splash_bar_height(void)
{
    uint32_t height;

    if (!framebuffer_console.height) {
        return 0;
    }
    height = framebuffer_console.height / 90u;
    if (height < 4u) {
        height = 4u;
    }
    if (height > 10u) {
        height = 10u;
    }
    return height > framebuffer_console.height ? framebuffer_console.height : height;
}

static void loader_framebuffer_draw_boot_splash(uint32_t percent)
{
    uint32_t bar_height;
    uint32_t available_height;
    uint32_t logo_x;
    uint32_t logo_y;
    uint32_t draw_width;
    uint32_t draw_height;
    uint32_t progress_width;

    if (loader_boot_log_screen || !framebuffer_console.enabled) {
        return;
    }
    if (percent > 100u) {
        percent = 100u;
    }

    bar_height = loader_framebuffer_splash_bar_height();
    available_height = framebuffer_console.height - bar_height;
    logo_x = framebuffer_console.width > LEONOS_BOOT_LOGO_WIDTH
                 ? (framebuffer_console.width - LEONOS_BOOT_LOGO_WIDTH) / 2u
                 : 0u;
    logo_y = available_height > LEONOS_BOOT_LOGO_HEIGHT
                 ? (available_height - LEONOS_BOOT_LOGO_HEIGHT) / 2u
                 : 0u;
    draw_width = LEONOS_BOOT_LOGO_WIDTH;
    draw_height = LEONOS_BOOT_LOGO_HEIGHT;
    if (draw_width > framebuffer_console.width - logo_x) {
        draw_width = framebuffer_console.width - logo_x;
    }
    if (draw_height > available_height - logo_y) {
        draw_height = available_height - logo_y;
    }

    loader_framebuffer_fill(0, 0, framebuffer_console.width,
                            framebuffer_console.height, LOADER_SPLASH_BACKGROUND);
    for (uint32_t row = 0; row < draw_height; ++row) {
        uint8_t *line = framebuffer_console.pixels +
                        (uint64_t)(logo_y + row) * framebuffer_console.pitch +
                        (uint64_t)logo_x * framebuffer_console.bytes_per_pixel;
        const uint32_t *pixels = leonos_boot_logo_pixels +
                                 (uint64_t)row * LEONOS_BOOT_LOGO_WIDTH;
        for (uint32_t column = 0; column < draw_width; ++column) {
            loader_framebuffer_write_native(
                line + (uint64_t)column * framebuffer_console.bytes_per_pixel,
                loader_framebuffer_native_color(pixels[column]));
        }
    }
    loader_framebuffer_fill(0, framebuffer_console.height - bar_height,
                            framebuffer_console.width, bar_height, LOADER_SPLASH_TRACK);
    progress_width = (uint32_t)(((uint64_t)framebuffer_console.width * percent) / 100u);
    loader_framebuffer_fill(0, framebuffer_console.height - bar_height,
                            progress_width, bar_height, LOADER_SPLASH_PROGRESS);
}

static void loader_framebuffer_char(uint32_t x, uint32_t y, char ch,
                                    uint32_t foreground, uint32_t background)
{
    const uint8_t *glyph;
    uint32_t native_foreground;
    uint32_t native_background;
    if (!framebuffer_console.enabled || x + LEONOS_FONT_W > framebuffer_console.width ||
        y + LEONOS_FONT_H > framebuffer_console.height) {
        return;
    }
    glyph = leonos_psf_glyph(ch);
    native_foreground = loader_framebuffer_native_color(foreground);
    native_background = loader_framebuffer_native_color(background);
    for (uint32_t row = 0; row < LEONOS_FONT_H; ++row) {
        uint8_t *line = framebuffer_console.pixels +
                        (uint64_t)(y + row) * framebuffer_console.pitch +
                        (uint64_t)x * framebuffer_console.bytes_per_pixel;
        for (uint32_t column = 0; column < LEONOS_FONT_W; ++column) {
            loader_framebuffer_write_native(line +
                                                (uint64_t)column * framebuffer_console.bytes_per_pixel,
                                            (glyph[row] & (uint8_t)(0x80u >> column))
                                                ? native_foreground : native_background);
        }
    }
}

static void loader_framebuffer_text(uint32_t x, uint32_t y, const char *text,
                                    uint32_t foreground, uint32_t background)
{
    uint32_t column = 0;
    while (text && text[column] &&
           x + (column + 1U) * LEONOS_FONT_W <= framebuffer_console.width) {
        loader_framebuffer_char(x + column * LEONOS_FONT_W, y, text[column],
                                foreground, background);
        ++column;
    }
}

static void loader_framebuffer_redraw_log(void)
{
    if (!framebuffer_console.enabled) {
        return;
    }
    loader_framebuffer_fill(0, 0, framebuffer_console.width,
                            framebuffer_console.height, framebuffer_console.panel);
    for (uint32_t row = 0; row < framebuffer_console.line_count; ++row) {
        loader_framebuffer_text(framebuffer_console.log_x,
                                framebuffer_console.log_y + row * LEONOS_FONT_H,
                                framebuffer_log_lines[row],
                                framebuffer_console.text,
                                framebuffer_console.panel);
    }
}

static void loader_framebuffer_clear_log(void)
{
    if (!framebuffer_console.enabled) {
        return;
    }
    memset_local(framebuffer_log_lines, 0, sizeof(framebuffer_log_lines));
    framebuffer_console.line_count = 1U;
    framebuffer_console.column = 0;
    framebuffer_console.row = 0;
    loader_framebuffer_redraw_log();
}

static void loader_framebuffer_reset(void)
{
    if (!framebuffer_console.enabled) {
        return;
    }
    loader_framebuffer_clear_log();
}

static void loader_framebuffer_set_theme(uint32_t theme)
{
    if (!framebuffer_console.enabled || !loader_boot_log_screen) {
        return;
    }
    if (theme == 0U) {
        framebuffer_console.panel = 0x00000000U;
        framebuffer_console.text = 0x0000ff00U;
    } else {
        framebuffer_console.panel = 0x001b2a3aU;
        framebuffer_console.text = 0x00ffffffU;
    }
    loader_framebuffer_reset();
}

static void loader_framebuffer_switch_to_log(void)
{
    if (loader_boot_log_screen) {
        return;
    }
    loader_boot_log_screen = 1u;
    loader_framebuffer_set_theme(handoff.ui_theme);
}

static void loader_framebuffer_init(void)
{
    uint8_t bytes_per_pixel;

    framebuffer_console = (struct loader_framebuffer_console){0};
    bytes_per_pixel = loader_framebuffer_bytes_per_pixel(handoff.framebuffer_pitch,
                                                         handoff.framebuffer_width,
                                                         handoff.framebuffer_bpp);
    if (!handoff.framebuffer_addr ||
        (handoff.framebuffer_bpp != 24U && handoff.framebuffer_bpp != 32U) ||
        !bytes_per_pixel ||
        handoff.framebuffer_width < LEONOS_FONT_W * 4U ||
        handoff.framebuffer_height < LEONOS_FONT_H * 4U ||
        handoff.framebuffer_pitch <
            (uint64_t)handoff.framebuffer_width * bytes_per_pixel) {
        return;
    }
    framebuffer_console.pixels = (uint8_t *)(uintptr_t)handoff.framebuffer_addr;
    framebuffer_console.width = handoff.framebuffer_width;
    framebuffer_console.height = handoff.framebuffer_height;
    framebuffer_console.pitch = handoff.framebuffer_pitch;
    framebuffer_console.bytes_per_pixel = bytes_per_pixel;
    framebuffer_console.columns = framebuffer_console.width / LEONOS_FONT_W;
    framebuffer_console.rows = framebuffer_console.height / LEONOS_FONT_H;
    if (framebuffer_console.columns > LOADER_LOG_MAX_COLUMNS) {
        framebuffer_console.columns = LOADER_LOG_MAX_COLUMNS;
    }
    if (framebuffer_console.rows > LOADER_LOG_MAX_ROWS) {
        framebuffer_console.rows = LOADER_LOG_MAX_ROWS;
    }
    framebuffer_console.log_x = 0;
    framebuffer_console.log_y = 0;
    framebuffer_console.enabled = framebuffer_console.columns && framebuffer_console.rows;
    loader_framebuffer_set_theme(handoff.ui_theme);
}

static void loader_framebuffer_save_state(void)
{
    if (!loader_boot_log_screen || !framebuffer_console.enabled) {
        handoff.boot_log = (struct leonos_boot_log_state){0};
        return;
    }

    handoff.boot_log.log_x = framebuffer_console.log_x;
    handoff.boot_log.log_y = framebuffer_console.log_y;
    handoff.boot_log.columns = framebuffer_console.columns;
    handoff.boot_log.rows = framebuffer_console.rows;
    handoff.boot_log.column = framebuffer_console.column;
    handoff.boot_log.row = framebuffer_console.row;
    handoff.boot_log.line_count = framebuffer_console.line_count;
}

static void loader_framebuffer_newline(void)
{
    framebuffer_console.column = 0;
    if (framebuffer_console.line_count < framebuffer_console.rows) {
        ++framebuffer_console.line_count;
        ++framebuffer_console.row;
        memset_local(framebuffer_log_lines[framebuffer_console.line_count - 1U],
                     0, sizeof(framebuffer_log_lines[0]));
        return;
    }
    for (uint32_t line = 1U; line < framebuffer_console.rows; ++line) {
        memcpy_local(framebuffer_log_lines[line - 1U],
                     framebuffer_log_lines[line],
                     sizeof(framebuffer_log_lines[0]));
    }
    memset_local(framebuffer_log_lines[framebuffer_console.rows - 1U],
                 0, sizeof(framebuffer_log_lines[0]));
    framebuffer_console.row = framebuffer_console.rows - 1U;
    loader_framebuffer_redraw_log();
}

static void loader_framebuffer_putc(char ch)
{
    uint32_t x;
    uint32_t y;
    if (!framebuffer_console.enabled || ch == '\r') {
        return;
    }
    if (ch == '\n') {
        loader_framebuffer_newline();
        return;
    }
    if (ch == '\t') {
        for (uint32_t count = 0; count < 4U; ++count) {
            loader_framebuffer_putc(' ');
        }
        return;
    }
    if ((uint8_t)ch < 32U) {
        ch = '?';
    }
    if (framebuffer_console.column >= framebuffer_console.columns) {
        loader_framebuffer_newline();
    }
    x = framebuffer_console.log_x + framebuffer_console.column * LEONOS_FONT_W;
    y = framebuffer_console.log_y + framebuffer_console.row * LEONOS_FONT_H;
    framebuffer_log_lines[framebuffer_console.row][framebuffer_console.column] = ch;
    framebuffer_log_lines[framebuffer_console.row][framebuffer_console.column + 1U] = 0;
    loader_framebuffer_char(x, y, ch, framebuffer_console.text,
                            framebuffer_console.panel);
    ++framebuffer_console.column;
}

static uint64_t loader_rdtsc(void)
{
    uint32_t low;
    uint32_t high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

static void loader_clock_calibrate(void)
{
    uint64_t sample_start;
    uint64_t sample_end;
    struct efi_system_table *st;

    if (!loader_tsc_start) {
        loader_tsc_start = loader_rdtsc();
    }
    if (!boot_services) {
        if (!handoff.efi_system_table) {
            return;
        }
        st = (struct efi_system_table *)(uintptr_t)handoff.efi_system_table;
        boot_services = st->boot_services;
    }
    if (!boot_services || !boot_services->stall) {
        return;
    }
    sample_start = loader_rdtsc();
    if (boot_services->stall(100000ULL) != EFI_SUCCESS) {
        return;
    }
    sample_end = loader_rdtsc();
    if (sample_end <= sample_start) {
        return;
    }
    loader_tsc_hz = (sample_end - sample_start) * 10ULL;
    loader_tsc_ready = loader_tsc_hz != 0;
}

static uint64_t loader_uptime_us(void)
{
    uint64_t now;
    uint64_t elapsed;

    if (!loader_tsc_ready || !loader_tsc_start) {
        return 0;
    }
    now = loader_rdtsc();
    if (now <= loader_tsc_start) {
        return 0;
    }
    elapsed = now - loader_tsc_start;
    return (elapsed * 1000000ULL) / loader_tsc_hz;
}

static void serial_putc_raw(char ch)
{
    loader_outb((uint8_t)ch, 0x3f8);
    if (loader_boot_log_screen) {
        loader_framebuffer_putc(ch);
    }
}

static void loader_emit_timestamp(void)
{
    uint64_t uptime_us = loader_uptime_us();
    uint64_t seconds = uptime_us / 1000000ULL;
    uint64_t micros = uptime_us % 1000000ULL;
    uint64_t divisor = 1ULL;
    uint32_t digits = 1U;

    while (seconds >= divisor * 10ULL && digits < 20U) {
        divisor *= 10ULL;
        ++digits;
    }
    serial_putc_raw('[');
    for (uint32_t i = digits; i < 5U; ++i) {
        serial_putc_raw(' ');
    }
    if (seconds == 0) {
        serial_putc_raw('0');
    } else {
        char digits_buf[20];
        uint32_t count = 0;
        while (seconds && count < sizeof(digits_buf)) {
            digits_buf[count++] = (char)('0' + seconds % 10ULL);
            seconds /= 10ULL;
        }
        while (count) {
            serial_putc_raw(digits_buf[--count]);
        }
    }
    serial_putc_raw('.');
    for (uint64_t divisor_us = 100000ULL; divisor_us; divisor_us /= 10ULL) {
        serial_putc_raw((char)('0' + (micros / divisor_us) % 10ULL));
    }
    serial_putc_raw(']');
    serial_putc_raw(' ');
}

static void serial_putc(char ch)
{
    if (loader_log_line_start) {
        loader_emit_timestamp();
    }
    serial_putc_raw(ch);
    loader_log_line_start = ch == '\n';
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

static int serial_poll_char(void)
{
    if ((loader_inb(0x3fdu) & 0x01u) == 0) {
        return 0;
    }
    return (int)loader_inb(0x3f8u);
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

static void efi_console_init(void)
{
    struct efi_system_table *st =
        (struct efi_system_table *)(uintptr_t)handoff.efi_system_table;
    text_out = 0;
    text_in = 0;
    if (!st) {
        return;
    }
    if (st->boot_services) {
        boot_services = st->boot_services;
    }
    text_out = (struct efi_simple_text_output_protocol *)st->con_out;
    text_in = (struct efi_simple_text_input_protocol *)st->con_in;
}

static void efi_console_write(const char *s)
{
    uint16_t buf[96];
    uint32_t pos = 0;

    if (!text_out || !text_out->output_string || !s) {
        return;
    }
    while (*s) {
        char ch = *s++;
        if (ch == '\n') {
            buf[pos++] = '\r';
            if (pos + 2u >= 96u) {
                buf[pos] = 0;
                text_out->output_string(text_out, buf);
                pos = 0;
            }
        }
        buf[pos++] = (uint8_t)ch;
        if (pos + 2u >= 96u) {
            buf[pos] = 0;
            text_out->output_string(text_out, buf);
            pos = 0;
        }
    }
    if (pos) {
        buf[pos] = 0;
        text_out->output_string(text_out, buf);
    }
}

static void boot_write(const char *s)
{
    serial_write(s);
    /* EFI text output shares the top-left console area with the framebuffer.
     * Once the graphical log is active, duplicating every line there would
     * paint over the boot UI. */
    if (!framebuffer_console.enabled) {
        efi_console_write(s);
    }
}

static void boot_write_sha256(const uint8_t digest[32])
{
    const char *digits = "0123456789abcdef";
    char text[65];
    for (uint32_t i = 0; i < 32u; ++i) {
        text[i * 2u] = digits[(digest[i] >> 4) & 0xfu];
        text[i * 2u + 1u] = digits[digest[i] & 0xfu];
    }
    text[64] = 0;
    boot_write(text);
}

static int efi_poll_char(void)
{
    struct efi_simple_input_key key;
    efi_status_t status;

    if (!text_in || !text_in->read_key_stroke) {
        return 0;
    }
    key.scan_code = 0;
    key.unicode_char = 0;
    status = text_in->read_key_stroke(text_in, &key);
    if (status == EFI_NOT_READY) {
        return 0;
    }
    if (status != EFI_SUCCESS || key.unicode_char == 0) {
        return 0;
    }
    return (int)key.unicode_char;
}

static int ascii_lower(int ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return ch + ('a' - 'A');
    }
    return ch;
}

static void wait_input_tick(void)
{
    if (boot_services && boot_services->stall) {
        boot_services->stall(10000);
    } else {
        for (volatile uint32_t i = 0; i < 1000000u; ++i) {
            __asm__ volatile("pause");
        }
    }
}

static int confirm_integrity_bypass(void)
{
    boot_write("Continue boot? Press Y to continue, N to stop: ");
    for (;;) {
        int ch = efi_poll_char();
        if (!ch) {
            ch = serial_poll_char();
        }
        ch = ascii_lower(ch);
        if (ch == 'y') {
            boot_write("Y\n");
            return 1;
        }
        if (ch == 'n' || ch == 27) {
            boot_write("N\n");
            return 0;
        }
        wait_input_tick();
    }
}

static int verify_image_integrity(const char *label, const void *image,
                                  uint64_t len, const uint8_t expected[32])
{
    uint8_t actual[32];

    sha256_digest(image, len, actual);
    if (bytes_eq(actual, expected, 32u)) {
        serial_write("[loader] integrity ok ");
        serial_write(label);
        serial_write("\n");
        return 0;
    }

    loader_framebuffer_switch_to_log();
    boot_write("\n[loader] WARNING: boot component hash mismatch: ");
    boot_write(label);
    boot_write("\n[loader] Expected SHA256: ");
    boot_write_sha256(expected);
    boot_write("\n[loader] Actual   SHA256: ");
    boot_write_sha256(actual);
    boot_write("\n[loader] The kernel files may have been changed after this loader was built.\n");
    if (confirm_integrity_bypass()) {
        boot_write("[loader] User confirmed boot with changed component.\n");
        return 0;
    }
    boot_write("[loader] Boot stopped by integrity policy.\n");
    return -1;
}

static int text_eq(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static int loader_cmdline_has(const char *needle)
{
    const char *cursor = handoff.cmdline;
    size_t needle_len = 0;

    if (!cursor || !needle || !*needle) {
        return 0;
    }
    while (needle[needle_len]) {
        ++needle_len;
    }
    while (*cursor) {
        const char *token;
        size_t index = 0;

        while (*cursor == ' ' || *cursor == '\t') {
            ++cursor;
        }
        token = cursor;
        while (token[index] && token[index] != ' ' && token[index] != '\t') {
            ++index;
        }
        if (index == needle_len) {
            size_t match = 0;
            while (match < needle_len && token[match] == needle[match]) {
                ++match;
            }
            if (match == needle_len) {
                return 1;
            }
        }
        cursor = token + index;
    }
    return 0;
}

static struct loader_module *find_loader_module(const char *name)
{
    for (uint32_t i = 0; i < loader_module_count; ++i) {
        if (text_eq(loader_modules[i].name, name)) {
            return &loader_modules[i];
        }
    }
    return 0;
}

/** @brief Returns nonzero when two half-open physical ranges intersect. */
static int loader_ranges_intersect(uint64_t left_start, uint64_t left_end,
                                   uint64_t right_start, uint64_t right_end)
{
    return left_start < left_end && right_start < right_end &&
           left_start < right_end && right_start < left_end;
}

/**
 * @brief Tests whether any loadable segment of an executable overlaps a physical range.
 *
 * This is intentionally a preflight only. elf_load_exec() remains the
 * authoritative ELF validator and reports malformed images through its
 * established boot failure path.
 */
static int elf_load_range_overlaps(const void *image, uint64_t len,
                                   uint64_t range_start, uint64_t range_end)
{
    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)image;

    if (!image || range_end <= range_start || len < sizeof(*eh) ||
        eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize > len) {
        return 0;
    }
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const struct elf64_phdr *ph =
            (const struct elf64_phdr *)((const uint8_t *)image + eh->e_phoff +
                                        (uint64_t)i * eh->e_phentsize);
        uint64_t start;
        uint64_t end;

        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) {
            continue;
        }
        start = ph->p_paddr ? ph->p_paddr : ph->p_vaddr;
        end = start + ph->p_memsz;
        if (!start || end < start) {
            continue;
        }
        if (loader_ranges_intersect(start, end, range_start, range_end)) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Moves the installer FAT module to firmware-owned pages outside all ELF destinations.
 *
 * GRUB is free to place a large Multiboot module anywhere in conventional
 * memory. The kernel and middlelayer are linked at fixed physical addresses,
 * so retaining an overlapping module would silently corrupt its FAT contents
 * while the loader copies either ELF image. The kernel later replaces the
 * original Multiboot range with this handoff range before reserving modules.
 */
static int loader_relocate_installer_root(struct loader_module *module)
{
    uint64_t length;
    uint64_t pages;
    uint64_t relocated_start;
    efi_status_t status;

    if (!module || module->end <= module->start || !boot_services ||
        !boot_services->allocate_pages) {
        return -1;
    }
    length = module->end - module->start;
    pages = (length + LOADER_PAGE_SIZE - 1ULL) / LOADER_PAGE_SIZE;
    if (!pages || pages > (LOADER_IDENTITY_MAP_LIMIT / LOADER_PAGE_SIZE)) {
        return -1;
    }
    /* Multiboot module addresses are 32-bit and GRUB's Loader mapping only
     * promises that low physical window is directly accessible. Keep the
     * replacement below 4 GiB; it remains inside the kernel's 16 GiB
     * supervisor direct map. AllocatePages will not reuse an active module
     * or the fixed kernel destination pages. */
    relocated_start = LOADER_IDENTITY_MAP_LIMIT - LOADER_PAGE_SIZE;
    status = boot_services->allocate_pages(EFI_ALLOCATE_MAX_ADDRESS,
                                           EFI_LOADER_DATA, pages,
                                           &relocated_start);
    if (status != EFI_SUCCESS || relocated_start == 0 ||
        relocated_start + length < relocated_start ||
        relocated_start + length > LOADER_IDENTITY_MAP_LIMIT) {
        serial_write("[loader] installer root relocation allocation failed status=");
        serial_write_hex(status);
        serial_write(" bytes=");
        serial_write_hex(length);
        serial_write("\n");
        return -1;
    }

    memcpy_local((void *)(uintptr_t)relocated_start,
                 (const void *)(uintptr_t)module->start, (size_t)length);
    serial_write("[loader] installer root relocated old=");
    serial_write_hex(module->start);
    serial_write(" new=");
    serial_write_hex(relocated_start);
    serial_write(" bytes=");
    serial_write_hex(length);
    serial_write("\n");
    module->start = relocated_start;
    module->end = relocated_start + length;
    handoff.installer_root.start = module->start;
    handoff.installer_root.end = module->end;
    return 0;
}

/** @brief Relocates an installer module only when fixed ELF destinations would overwrite it. */
static int loader_protect_installer_root(struct loader_module *installer_root,
                                         const struct loader_module *kernel,
                                         const struct loader_module *middlelayer)
{
    uint64_t start;
    uint64_t end;
    int overlaps = 0;

    if (!installer_root || installer_root->end <= installer_root->start) {
        return 0;
    }
    start = installer_root->start;
    end = installer_root->end;
    if (kernel && kernel->end > kernel->start) {
        overlaps |= elf_load_range_overlaps((const void *)(uintptr_t)kernel->start,
                                            kernel->end - kernel->start,
                                            start, end);
    }
    if (middlelayer && middlelayer->end > middlelayer->start) {
        overlaps |= elf_load_range_overlaps((const void *)(uintptr_t)middlelayer->start,
                                            middlelayer->end - middlelayer->start,
                                            start, end);
    }
    if (!overlaps) {
        return 0;
    }
    serial_write("[loader] installer root overlaps fixed ELF load range; relocating\n");
    return loader_relocate_installer_root(installer_root);
}

static int build_efi_path(const char *path, uint16_t *out, uint32_t cap)
{
    uint32_t pos = 0;
    if (!path || !out || cap < 2) {
        return -1;
    }
    if (path[0] != '/') {
        return -1;
    }
    out[pos++] = '\\';
    ++path;
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

static int efi_root_has_file(struct efi_file_protocol *volume, const char *path)
{
    uint16_t efi_path[256];
    struct efi_file_protocol *file = 0;
    efi_status_t status;
    if (!volume || !volume->open || build_efi_path(path, efi_path, 256) < 0) {
        return 0;
    }
    status = volume->open(volume, &file, efi_path, EFI_FILE_MODE_READ, 0);
    if (status != EFI_SUCCESS || !file) {
        return 0;
    }
    if (file->close) {
        file->close(file);
    }
    return 1;
}

static int efi_open_root(uint64_t system_table_addr)
{
    struct efi_system_table *st = (struct efi_system_table *)(uintptr_t)system_table_addr;
    uint64_t handle_count = 0;
    efi_handle_t *handles = 0;
    struct efi_file_protocol *fallback = 0;
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
            if (efi_root_has_file(volume, KERNEL_PATH)) {
                if (fallback && fallback->close) {
                    fallback->close(fallback);
                }
                root_dir = volume;
                serial_write("[loader] EFI SimpleFS LeonOS root ready\n");
                return 0;
            }
            if (!fallback) {
                fallback = volume;
            } else if (volume->close) {
                volume->close(volume);
            }
        }
    }
    if (fallback) {
        root_dir = fallback;
        serial_write("[loader] EFI SimpleFS fallback root ready\n");
        return 0;
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

static int efi_delete_file(const char *path)
{
    uint16_t efi_path[256];
    struct efi_file_protocol *file = 0;
    efi_status_t status;
    if (!root_dir || !root_dir->open || build_efi_path(path, efi_path, 256) < 0) return -1;
    status = root_dir->open(root_dir, &file, efi_path,
                            EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (status != EFI_SUCCESS || !file || !file->delete_file) {
        if (file && file->close) file->close(file);
        return -1;
    }
    status = file->delete_file(file);
    return status == EFI_SUCCESS ? 0 : -1;
}

/* Kept in the loader protocol layer for recovery tools that need to repair a
 * one-shot control file.  Normal debug-mode arming uses the kernel storage
 * path, so this helper is deliberately not part of the boot decision. */
static int __attribute__((unused)) efi_write_file(const char *path, const void *buffer, uint64_t length)
{
    uint16_t efi_path[256];
    struct efi_file_protocol *file = 0;
    efi_status_t status;
    uint64_t written;
    if (!root_dir || !root_dir->open || !buffer || build_efi_path(path, efi_path, 256) < 0) return -1;
    status = root_dir->open(root_dir, &file, efi_path,
                            EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
    if (status != EFI_SUCCESS || !file || !file->write) {
        if (file && file->close) file->close(file);
        return -1;
    }
    written = length;
    status = file->write(file, &written, buffer);
    if (status == EFI_SUCCESS && file->flush) status = file->flush(file);
    if (file->close) file->close(file);
    return status == EFI_SUCCESS && written == length ? 0 : -1;
}

static int loader_consume_kernel_debug_marker(void)
{
    static const char marker[] = "LEONOS-KDBG-1\n";
    char value[sizeof(marker) + 8U];
    uint64_t len = 0;
    if (efi_read_file("/system/state/kerneldebug.next", value, sizeof(value) - 1U, &len) < 0) {
        return 0;
    }
    if (efi_delete_file("/system/state/kerneldebug.next") < 0) {
        serial_write("[loader] kernel debug marker could not be consumed\n");
        return 0;
    }
    if (len != sizeof(marker) - 1U) return 0;
    for (uint32_t i = 0; i + 1U < sizeof(marker); ++i) {
        if (value[i] != marker[i]) return 0;
    }
    handoff.kernel_debug_mode = 1U;
    serial_write("[loader] kernel debug one-shot marker consumed\n");
    return 1;
}

static int efi_capture_memory_map(void)
{
    uint64_t map_size = EFI_MEMORY_MAP_BYTES;
    uint64_t map_key = 0;
    uint64_t descriptor_size = 0;
    uint32_t descriptor_version = 0;
    efi_status_t status;

    if (!boot_services || !boot_services->get_memory_map) {
        serial_write("[loader] EFI GetMemoryMap unavailable\n");
        return -1;
    }
    status = boot_services->get_memory_map(&map_size, efi_memory_map,
                                           &map_key, &descriptor_size,
                                           &descriptor_version);
    if (status != EFI_SUCCESS || !descriptor_size ||
        map_size > EFI_MEMORY_MAP_BYTES ||
        (map_size % descriptor_size) != 0) {
        serial_write("[loader] EFI GetMemoryMap failed status=");
        serial_write_hex(status);
        serial_write(" bytes=");
        serial_write_hex(map_size);
        serial_write(" descriptor=");
        serial_write_hex(descriptor_size);
        serial_write("\n");
        return -1;
    }

    handoff.efi_mmap_addr = (uint64_t)(uintptr_t)efi_memory_map;
    handoff.efi_mmap_entry_size = (uint32_t)descriptor_size;
    handoff.efi_mmap_entry_count = (uint32_t)(map_size / descriptor_size);
    serial_write("[loader] EFI memory map captured entries=");
    serial_write_hex(handoff.efi_mmap_entry_count);
    serial_write(" descriptor=");
    serial_write_hex(descriptor_size);
    serial_write(" version=");
    serial_write_hex(descriptor_version);
    serial_write("\n");
    return 0;
}

static void loader_load_ui_theme(void)
{
    char config[160];
    uint64_t len = 0;
    static const char win95[] = "theme=win95";
    handoff.ui_theme = 1u;
    if (efi_read_file("/system/config/display.conf", config, sizeof(config), &len) < 0) {
        return;
    }
    for (uint64_t index = 0; index + sizeof(win95) - 1u <= len; ++index) {
        uint64_t matched = 0;
        while (matched < sizeof(win95) - 1u && config[index + matched] == win95[matched]) {
            ++matched;
        }
        if (matched == sizeof(win95) - 1u) {
            handoff.ui_theme = 0u;
            return;
        }
    }
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
    loader_module_count = 0;
    handoff.magic = LEONOS_BOOT_HANDOFF_MAGIC;
    handoff.version = LEONOS_BOOT_HANDOFF_VERSION;
    handoff.multiboot_magic = magic;
    handoff.multiboot_info = info_addr;
    handoff.loader.start = (uint64_t)(uintptr_t)__loader_start;
    handoff.loader.end = (uint64_t)(uintptr_t)__loader_end;
    handoff.loader.entry = (uint64_t)(uintptr_t)loader_main;
    handoff.loader.path = "/boot/loader.elf";
    handoff.ui_theme = 1u;
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
        case MULTIBOOT2_TAG_TYPE_MODULE: {
            const struct multiboot2_tag_module *mod =
                (const struct multiboot2_tag_module *)tag;
            if (loader_module_count < 16) {
                loader_modules[loader_module_count].start = mod->mod_start;
                loader_modules[loader_module_count].end = mod->mod_end;
                loader_modules[loader_module_count].name = mod->string;
                ++loader_module_count;
            }
            break;
        }
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
            handoff.framebuffer_type = fb->framebuffer_type;
            if (fb->framebuffer_type == MULTIBOOT2_FRAMEBUFFER_TYPE_RGB &&
                tag->size >= sizeof(*fb) + MULTIBOOT2_FRAMEBUFFER_RGB_INFO_SIZE) {
                const uint8_t *rgb = (const uint8_t *)fb + sizeof(*fb);
                handoff.framebuffer_red_field_position = rgb[0];
                handoff.framebuffer_red_mask_size = rgb[1];
                handoff.framebuffer_green_field_position = rgb[2];
                handoff.framebuffer_green_mask_size = rgb[3];
                handoff.framebuffer_blue_field_position = rgb[4];
                handoff.framebuffer_blue_mask_size = rgb[5];
            }
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
    const struct loader_module *kernel_module;
    const struct loader_module *middlelayer_module;
    struct loader_module *installer_root_module;

    loader_tsc_start = loader_rdtsc();
    serial_init();
    serial_write("[loader] LeonOS two-stage loader starting\n");
    parse_multiboot2(magic, multiboot_info);
    /* TTY startup is itself a text-console request.  Treat it like an
     * explicit bootlog request so the graphical splash never owns the
     * framebuffer while the kernel is preparing the shell. */
    loader_boot_log_screen = loader_cmdline_has("bootlog=1") ||
                             loader_cmdline_has("startup=tty");
    installer_root_module = find_loader_module("leonos-installer-root");
    if (installer_root_module && installer_root_module->end > installer_root_module->start) {
        handoff.installer_root.start = installer_root_module->start;
        handoff.installer_root.end = installer_root_module->end;
        handoff.installer_root.path = "leonos-installer-root";
        serial_write("[loader] installer root module bytes=");
        serial_write_hex(installer_root_module->end - installer_root_module->start);
        serial_write("\n");
    }
    loader_framebuffer_init();
    efi_console_init();
    loader_clock_calibrate();
    if (handoff.efi_system_table && efi_open_root(handoff.efi_system_table) == 0) {
        (void)loader_consume_kernel_debug_marker();
        loader_load_ui_theme();
        loader_framebuffer_set_theme(handoff.ui_theme);
    }
    kernel_module = find_loader_module("leonos-kernel");
    middlelayer_module = find_loader_module("leonos-middlelayer");
    if (loader_protect_installer_root(installer_root_module,
                                      kernel_module, middlelayer_module) < 0) {
        serial_write("[loader] unable to protect installer root module\n");
        for (;;) {
            __asm__ volatile("hlt");
        }
    }
    if (loader_boot_log_screen) {
        boot_write("[loader] framebuffer boot log active\n");
    } else {
        loader_framebuffer_draw_boot_splash(12u);
        serial_write("[loader] graphical boot splash active\n");
    }

    if (kernel_module) {
        len = kernel_module->end - kernel_module->start;
        serial_write("[loader] using module leonos-kernel bytes=");
        serial_write_hex(len);
        serial_write("\n");
        if (verify_image_integrity("kernel.sys",
                                   (const void *)(uintptr_t)kernel_module->start,
                                   len,
                                   LEONOS_LOADER_KERNEL_SHA256) < 0) {
            for (;;) {
                __asm__ volatile("hlt");
            }
        }
        if (elf_load_exec((const void *)(uintptr_t)kernel_module->start, len,
                          &handoff.kernel) < 0) {
            serial_write("[loader] kernel module load failed\n");
            for (;;) {
                __asm__ volatile("hlt");
            }
        }
    } else {
        if (!handoff.efi_system_table || efi_open_root(handoff.efi_system_table) < 0) {
            serial_write("[loader] unable to open EFI filesystem\n");
            for (;;) {
                __asm__ volatile("hlt");
            }
        }
        if (efi_read_file(KERNEL_PATH, read_buffer, sizeof(read_buffer), &len) < 0 ||
            verify_image_integrity("kernel.sys",
                                   read_buffer,
                                   len,
                                   LEONOS_LOADER_KERNEL_SHA256) < 0 ||
            elf_load_exec(read_buffer, len, &handoff.kernel) < 0) {
            serial_write("[loader] kernel.sys load failed\n");
            for (;;) {
                __asm__ volatile("hlt");
            }
        }
    }
    if (!handoff.kernel.entry) {
        serial_write("[loader] kernel.sys load failed\n");
        for (;;) {
            __asm__ volatile("hlt");
        }
    }
    handoff.kernel.path = "/boot/system/kernel.sys";
    serial_write("[loader] kernel loaded entry=");
    serial_write_hex(handoff.kernel.entry);
    serial_write(" range=");
    serial_write_hex(handoff.kernel.start);
    serial_write("-");
    serial_write_hex(handoff.kernel.end);
    serial_write("\n");
    loader_framebuffer_draw_boot_splash(50u);

    if (middlelayer_module) {
        len = middlelayer_module->end - middlelayer_module->start;
        serial_write("[loader] using module leonos-middlelayer bytes=");
        serial_write_hex(len);
        serial_write("\n");
        if (verify_image_integrity("middlelayer.sys",
                                   (const void *)(uintptr_t)middlelayer_module->start,
                                   len,
                                   LEONOS_LOADER_MIDDLELAYER_SHA256) < 0) {
            for (;;) {
                __asm__ volatile("hlt");
            }
        }
        if (elf_load_exec((const void *)(uintptr_t)middlelayer_module->start, len,
                          &handoff.middlelayer) < 0) {
            serial_write("[loader] middlelayer module load failed\n");
            for (;;) {
                __asm__ volatile("hlt");
            }
        }
    } else {
        if (!root_dir && (!handoff.efi_system_table || efi_open_root(handoff.efi_system_table) < 0)) {
            serial_write("[loader] unable to open EFI filesystem\n");
            for (;;) {
                __asm__ volatile("hlt");
            }
        }
        if (efi_read_file(MIDDLELAYER_PATH, read_buffer, sizeof(read_buffer), &len) < 0 ||
            verify_image_integrity("middlelayer.sys",
                                   read_buffer,
                                   len,
                                   LEONOS_LOADER_MIDDLELAYER_SHA256) < 0 ||
            elf_load_exec(read_buffer, len, &handoff.middlelayer) < 0) {
            serial_write("[loader] middlelayer.sys load failed\n");
            for (;;) {
                __asm__ volatile("hlt");
            }
        }
    }
    if (!handoff.middlelayer.entry) {
        serial_write("[loader] middlelayer.sys load failed\n");
        for (;;) {
            __asm__ volatile("hlt");
        }
    }
    handoff.middlelayer.path = "/boot/system/middlelayer.sys";
    serial_write("[loader] middlelayer loaded entry=");
    serial_write_hex(handoff.middlelayer.entry);
    serial_write(" range=");
    serial_write_hex(handoff.middlelayer.start);
    serial_write("-");
    serial_write_hex(handoff.middlelayer.end);
    serial_write("\n");
    loader_framebuffer_draw_boot_splash(78u);

    if (!handoff.mmap_entry_count && !handoff.efi_mmap_entry_count) {
        (void)efi_capture_memory_map();
    }

    serial_write("[loader] jumping to kernel\n");
    loader_framebuffer_draw_boot_splash(80u);
    handoff.boot_uptime_us = loader_uptime_us();
    loader_framebuffer_save_state();
    void (*entry)(const struct leonos_boot_handoff *) =
        (void (*)(const struct leonos_boot_handoff *))(uintptr_t)handoff.kernel.entry;
    entry(&handoff);
    for (;;) {
        __asm__ volatile("hlt");
    }
}
