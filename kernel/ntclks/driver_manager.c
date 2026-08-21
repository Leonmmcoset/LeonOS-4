/*
 * LeonOS kernel driver manager: owns built-in and loadable driver lifecycle.
 * Provides registration, probing, initialization, and device event dispatch.
 */
#include <ntclks/console.h>
#include <ntclks/driver_manager.h>
#include <ntclks/e1000.h>
#include <ntclks/framebuffer.h>
#include <ntclks/input.h>
#include <ntclks/mm.h>
#include <ntclks/net.h>
#include <ntclks/pci.h>
#include <ntclks/storage.h>
#include <ntclks/time.h>

#include "arch/x86_64/port.h"

#define EARLY_SERIAL_COM1 0x3f8u
#define EARLY_SERIAL_LSR  (EARLY_SERIAL_COM1 + 5u)

/* Kernel diagnostics must be available before serial.drv is loaded.  Keep a
 * tiny COM1 backend here; the loadable driver later replaces it through
 * register_serial(), without changing the public console API. */
static int early_serial_ready;

static void early_serial_putc(char ch)
{
    uint32_t attempts = 100000u;
    if (!early_serial_ready) {
        return;
    }
    while (!(x86_64_inb((uint16_t)EARLY_SERIAL_LSR) & 0x20u) && attempts--) {
        __asm__ volatile("pause");
    }
    x86_64_outb((uint8_t)ch, (uint16_t)EARLY_SERIAL_COM1);
}

static void early_serial_write(const char *text)
{
    while (text && *text) {
        if (*text == '\n') {
            early_serial_putc('\r');
        }
        early_serial_putc(*text++);
    }
}

#define DRIVER_DIRECTORY "/drivers"
#define DRIVER_CONFIG_PATH "/system/config/drivers.conf"
#define DRIVER_CONFIG_CAP 1024U
#define DRIVER_ELF_MAX_SECTIONS 64U
#define DRIVER_ELF_MAX_IMAGE (4U * 1024U * 1024U)

#define ELF_ET_REL 1U
#define ELF_EM_X86_64 62U
#define ELF_SHT_PROGBITS 1U
#define ELF_SHT_SYMTAB 2U
#define ELF_SHT_STRTAB 3U
#define ELF_SHT_RELA 4U
#define ELF_SHT_NOBITS 8U
#define ELF_SHF_ALLOC 0x2ULL
#define ELF_SHN_UNDEF 0U
#define ELF_SHN_ABS 0xfff1U
#define ELF_R_X86_64_64 1U
#define ELF_R_X86_64_PC32 2U
#define ELF_R_X86_64_PLT32 4U
#define ELF_R_X86_64_32 10U
#define ELF_R_X86_64_32S 11U

struct elf64_ehdr {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};

struct elf64_shdr {
    uint32_t name;
    uint32_t type;
    uint64_t flags;
    uint64_t addr;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t addralign;
    uint64_t entsize;
};

struct elf64_sym {
    uint32_t name;
    uint8_t info;
    uint8_t other;
    uint16_t shndx;
    uint64_t value;
    uint64_t size;
};

struct elf64_rela {
    uint64_t offset;
    uint64_t info;
    int64_t addend;
};

struct driver_slot {
    struct leonos_driver_info info;
    const struct leonos_driver_module *module;
    uint64_t image_phys;
    uint32_t image_pages;
};

static struct driver_slot driver_slots[LEONOS_DRIVER_MAX];
static int32_t loading_slot = -1;
static const struct leonos_driver_mouse_ops *mouse_ops;
static bool mouse_visible = true;
static const struct leonos_driver_serial_ops *serial_ops;
static const struct leonos_driver_e1000_ops *e1000_ops;
static const struct leonos_driver_audio_ops *audio_ops;
static uint32_t mouse_owner;
static uint32_t serial_owner;
static uint32_t e1000_owner;
static uint32_t audio_owner;
static struct mouse_state mouse_cache;
static uint8_t e1000_empty_mac[6];

/**
 * @brief Coordinates the driver copy text operation.
 * @param dst Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param src Input or output value used by this operation.
 */
static void driver_copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t pos = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[pos] && pos + 1U < cap) {
        dst[pos] = src[pos];
        ++pos;
    }
    dst[pos] = 0;
}

/**
 * @brief Coordinates the driver text equal operation.
 * @param left Input or output value used by this operation.
 * @param right Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int driver_text_equal(const char *left, const char *right)
{
    uint32_t index = 0;
    while (left && right && left[index] == right[index]) {
        if (left[index] == 0) {
            return 1;
        }
        ++index;
    }
    return 0;
}

/**
 * @brief Coordinates the driver elf string equal operation.
 * @param left Input or output value used by this operation.
 * @param available Input or output value used by this operation.
 * @param right Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int driver_elf_string_equal(const char *left, uint64_t available,
                                   const char *right)
{
    uint64_t index = 0;
    while (right && right[index]) {
        if (index >= available || !left || left[index] != right[index]) {
            return 0;
        }
        ++index;
    }
    return right && index < available && left[index] == 0;
}

/**
 * @brief Coordinates the driver text starts with operation.
 * @param text Input or output value used by this operation.
 * @param prefix Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int driver_text_starts_with(const char *text, const char *prefix)
{
    uint32_t index = 0;
    while (text && prefix && prefix[index]) {
        if (text[index] != prefix[index]) {
            return 0;
        }
        ++index;
    }
    return prefix && prefix[index] == 0;
}

/**
 * @brief Coordinates the driver text length operation.
 * @param text Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @return Result, status, or value defined by this API.
 */
static uint32_t driver_text_length(const char *text, uint32_t cap)
{
    uint32_t length = 0;
    while (text && length < cap && text[length]) {
        ++length;
    }
    return length;
}

/**
 * @brief Coordinates the driver has drv suffix operation.
 * @param name Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int driver_has_drv_suffix(const char *name)
{
    uint32_t length = driver_text_length(name, LEONOS_DRIVER_FILE_LEN);
    return length > 4U && name[length - 4U] == '.' &&
           name[length - 3U] == 'd' && name[length - 2U] == 'r' &&
           name[length - 1U] == 'v';
}

/**
 * @brief Coordinates the driver file name valid operation.
 * @param file Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int driver_file_name_valid(const char *file)
{
    uint32_t length = driver_text_length(file, LEONOS_DRIVER_FILE_LEN);
    if (!driver_has_drv_suffix(file) || length == 0 || length >= LEONOS_DRIVER_FILE_LEN) {
        return 0;
    }
    for (uint32_t index = 0; index < length; ++index) {
        char ch = file[index];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-')) {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Coordinates the driver name compare operation.
 * @param left Input or output value used by this operation.
 * @param right Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int driver_name_compare(const char *left, const char *right)
{
    uint32_t index = 0;
    while (left && right && left[index] == right[index]) {
        if (left[index] == 0) {
            return 0;
        }
        ++index;
    }
    if (!left || !right) {
        return left ? 1 : (right ? -1 : 0);
    }
    return (uint8_t)left[index] < (uint8_t)right[index] ? -1 : 1;
}

/**
 * @brief Coordinates the driver load order compare operation.
 * @param left Input or output value used by this operation.
 * @param right Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int driver_load_order_compare(const char *left, const char *right)
{
    int left_is_serial = driver_text_equal(left, "serial.drv");
    int right_is_serial = driver_text_equal(right, "serial.drv");
    if (left_is_serial != right_is_serial) {
        return left_is_serial ? -1 : 1;
    }
    return driver_name_compare(left, right);
}

/**
 * @brief Coordinates the driver make path operation.
 * @param dst Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param file Input or output value used by this operation.
 */
static void driver_make_path(char *dst, uint32_t cap, const char *file)
{
    uint32_t pos = 0;
    const char *prefix = DRIVER_DIRECTORY "/";
    if (!dst || cap == 0) {
        return;
    }
    while (prefix[pos] && pos + 1U < cap) {
        dst[pos] = prefix[pos];
        ++pos;
    }
    for (uint32_t index = 0; file && file[index] && pos + 1U < cap; ++index) {
        dst[pos++] = file[index];
    }
    dst[pos] = 0;
}

/**
 * @brief Coordinates the driver set error operation.
 * @param slot Input or output value used by this operation.
 * @param status Input or output value used by this operation.
 * @param error Input or output value used by this operation.
 */
static void driver_set_error(struct driver_slot *slot, int status, const char *error)
{
    if (!slot) {
        return;
    }
    slot->info.state = LEONOS_DRIVER_STATE_FAILED;
    driver_copy_text(slot->info.error, sizeof(slot->info.error), error);
    if (status < 0) {
        console_printf("[driver] %s failed status=%d: %s\n", slot->info.file,
                       status, slot->info.error);
    }
}

/**
 * @brief Coordinates the driver align up operation.
 * @param value Input or output value used by this operation.
 * @param alignment Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static uint64_t driver_align_up(uint64_t value, uint64_t alignment)
{
    if (alignment < 1U) {
        alignment = 1U;
    }
    if (alignment > 4096U) {
        alignment = 4096U;
    }
    return (value + alignment - 1U) & ~(alignment - 1U);
}

/**
 * @brief Coordinates the driver range valid operation.
 * @param offset Input or output value used by this operation.
 * @param size Length, size, or element count associated with the operation.
 * @param total Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int driver_range_valid(uint64_t offset, uint64_t size, uint64_t total)
{
    return offset <= total && size <= total - offset;
}

/**
 * @brief Coordinates the driver memzero operation.
 * @param address Address used by this operation; its address-space interpretation follows the API.
 * @param size Length, size, or element count associated with the operation.
 */
static void driver_memzero(void *address, uint64_t size)
{
    uint8_t *bytes = (uint8_t *)address;
    while (size--) {
        *bytes++ = 0;
    }
}

/**
 * @brief Coordinates the driver memcpy operation.
 * @param dst Input or output value used by this operation.
 * @param src Input or output value used by this operation.
 * @param size Length, size, or element count associated with the operation.
 */
static void driver_memcpy(void *dst, const void *src, uint64_t size)
{
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;
    while (size--) {
        *out++ = *in++;
    }
}

/**
 * @brief Coordinates the driver config disabled operation.
 * @param file Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int driver_config_disabled(const char *file)
{
    struct storage_node node;
    char config[DRIVER_CONFIG_CAP];
    uint32_t got = 0;
    uint32_t pos = 0;
    if (!file || storage_lookup_path(DRIVER_CONFIG_PATH, &node) < 0 ||
        node.type != LEONOS_FS_TYPE_FILE || node.size == 0) {
        return 0;
    }
    if (storage_read_node(&node, 0, config,
                          node.size >= sizeof(config) ? sizeof(config) - 1U : (uint32_t)node.size,
                          &got) < 0) {
        return 0;
    }
    config[got < sizeof(config) ? got : sizeof(config) - 1U] = 0;
    while (pos < got) {
        uint32_t start = pos;
        while (pos < got && config[pos] != '\n' && config[pos] != '\r') {
            ++pos;
        }
        if (pos > start && driver_text_starts_with(config + start, "disabled=") &&
            driver_text_equal(config + start + 9U, file)) {
            return 1;
        }
        while (pos < got && (config[pos] == '\n' || config[pos] == '\r')) {
            ++pos;
        }
    }
    return 0;
}

/**
 * @brief Coordinates the driver write config operation.
 * @return Result, status, or value defined by this API.
 */
static int driver_write_config(void)
{
    char config[DRIVER_CONFIG_CAP];
    uint32_t pos = 0;
    const char *header = "version=1\n";
    for (uint32_t index = 0; header[index] && pos + 1U < sizeof(config); ++index) {
        config[pos++] = header[index];
    }
    for (uint32_t index = 0; index < LEONOS_DRIVER_MAX; ++index) {
        const struct driver_slot *slot = &driver_slots[index];
        const char *prefix = "disabled=";
        if (!slot->info.file[0] || !(slot->info.flags & LEONOS_DRIVER_FLAG_DISABLED)) {
            continue;
        }
        for (uint32_t j = 0; prefix[j] && pos + 1U < sizeof(config); ++j) {
            config[pos++] = prefix[j];
        }
        for (uint32_t j = 0; slot->info.file[j] && pos + 1U < sizeof(config); ++j) {
            config[pos++] = slot->info.file[j];
        }
        if (pos + 1U >= sizeof(config)) {
            return -28;
        }
        config[pos++] = '\n';
    }
    config[pos] = 0;
    return storage_write_file(DRIVER_CONFIG_PATH, config, pos);
}

/**
 * @brief Coordinates the driver find file operation.
 * @param file Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static struct driver_slot *driver_find_file(const char *file)
{
    for (uint32_t index = 0; index < LEONOS_DRIVER_MAX; ++index) {
        if (driver_slots[index].info.file[0] &&
            driver_text_equal(driver_slots[index].info.file, file)) {
            return &driver_slots[index];
        }
    }
    return 0;
}

/**
 * @brief Coordinates the driver get slot operation.
 * @param file Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static struct driver_slot *driver_get_slot(const char *file)
{
    struct driver_slot *slot = driver_find_file(file);
    if (slot) {
        return slot;
    }
    for (uint32_t index = 0; index < LEONOS_DRIVER_MAX; ++index) {
        slot = &driver_slots[index];
        if (!slot->info.file[0]) {
            driver_memzero(slot, sizeof(*slot));
            slot->info.id = index;
            slot->info.state = driver_config_disabled(file)
                                   ? LEONOS_DRIVER_STATE_DISABLED
                                   : LEONOS_DRIVER_STATE_UNLOADED;
            slot->info.flags = LEONOS_DRIVER_FLAG_AUTOSTART |
                               (slot->info.state == LEONOS_DRIVER_STATE_DISABLED
                                    ? LEONOS_DRIVER_FLAG_DISABLED : 0U);
            driver_copy_text(slot->info.file, sizeof(slot->info.file), file);
            return slot;
        }
    }
    return 0;
}

/**
 * @brief Coordinates the driver read file operation.
 * @param path LeonOS path consumed by this operation.
 * @param out_data Caller-provided storage that receives output from this operation.
 * @param out_len Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
static int driver_read_file(const char *path, const uint8_t **out_data, uint64_t *out_len)
{
    const void *data = 0;
    size_t length = 0;
    int ret = storage_read_file(path, &data, &length);
    if (ret < 0 || !data || length == 0) {
        return ret < 0 ? ret : -5;
    }
    *out_data = (const uint8_t *)data;
    *out_len = (uint64_t)length;
    return 0;
}

/**
 * @brief Coordinates the driver release file operation.
 * @param data Input or output value used by this operation.
 * @param length Length, size, or element count associated with the operation.
 */
static void driver_release_file(const void *data, uint64_t length)
{
    uint32_t pages = (uint32_t)((length + 4095U) / 4096U);
    if (data && pages) {
        mm_free_pages((uint64_t)(uintptr_t)data, pages);
    }
}

/**
 * @brief Coordinates the driver symbol value operation.
 * @param symbol Input or output value used by this operation.
 * @param shnum Input or output value used by this operation.
 * @param sections Input or output value used by this operation.
 * @param section_addresses Address used by this operation; its address-space interpretation follows the API.
 * @param out Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
static int driver_symbol_value(const struct elf64_sym *symbol, uint16_t shnum,
                               const struct elf64_shdr *sections,
                               const uint64_t section_addresses[], uint64_t *out)
{
    if (!symbol || !out) {
        return -22;
    }
    if (symbol->shndx == ELF_SHN_ABS) {
        *out = symbol->value;
        return 0;
    }
    if (symbol->shndx == ELF_SHN_UNDEF || symbol->shndx >= shnum ||
        !sections || symbol->value > sections[symbol->shndx].size ||
        !section_addresses[symbol->shndx]) {
        return -8;
    }
    *out = section_addresses[symbol->shndx] + symbol->value;
    return 0;
}

/**
 * @brief Coordinates the driver apply relocations operation.
 * @param data Input or output value used by this operation.
 * @param length Length, size, or element count associated with the operation.
 * @param header Input or output value used by this operation.
 * @param sections Input or output value used by this operation.
 * @param section_addresses Address used by this operation; its address-space interpretation follows the API.
 * @return Result, status, or value defined by this API.
 */
static int driver_apply_relocations(const uint8_t *data, uint64_t length,
                                    const struct elf64_ehdr *header,
                                    const struct elf64_shdr *sections,
                                    const uint64_t section_addresses[])
{
    for (uint16_t index = 0; index < header->shnum; ++index) {
        const struct elf64_shdr *rela_section = &sections[index];
        const struct elf64_shdr *target;
        const struct elf64_shdr *symbols;
        const struct elf64_shdr *strings;
        uint64_t count;
        if (rela_section->type != ELF_SHT_RELA) {
            continue;
        }
        if (rela_section->info >= header->shnum || rela_section->link >= header->shnum ||
            rela_section->entsize != sizeof(struct elf64_rela) ||
            !driver_range_valid(rela_section->offset, rela_section->size, length)) {
            return -8;
        }
        target = &sections[rela_section->info];
        symbols = &sections[rela_section->link];
        if (symbols->type != ELF_SHT_SYMTAB || symbols->link >= header->shnum ||
            symbols->entsize != sizeof(struct elf64_sym) ||
            !driver_range_valid(symbols->offset, symbols->size, length) ||
            !section_addresses[rela_section->info]) {
            return -8;
        }
        strings = &sections[symbols->link];
        if (strings->type != ELF_SHT_STRTAB ||
            !driver_range_valid(strings->offset, strings->size, length)) {
            return -8;
        }
        count = rela_section->size / sizeof(struct elf64_rela);
        for (uint64_t rel_index = 0; rel_index < count; ++rel_index) {
            const struct elf64_rela *rela =
                (const struct elf64_rela *)(data + rela_section->offset +
                                            rel_index * sizeof(struct elf64_rela));
            uint32_t symbol_index = (uint32_t)(rela->info >> 32);
            uint32_t type = (uint32_t)rela->info;
            uint64_t symbol_count = symbols->size / sizeof(struct elf64_sym);
            const struct elf64_sym *symbol;
            uint64_t symbol_value;
            uint64_t patch;
            int64_t value;
            if (symbol_index >= symbol_count || rela->offset >= target->size ||
                !driver_range_valid(rela->offset, type == ELF_R_X86_64_64 ? 8U : 4U,
                                    target->size)) {
                return -8;
            }
            symbol = (const struct elf64_sym *)(data + symbols->offset +
                                                symbol_index * sizeof(struct elf64_sym));
            if (driver_symbol_value(symbol, header->shnum, sections, section_addresses,
                                    &symbol_value) < 0) {
                return -8;
            }
            patch = section_addresses[rela_section->info] + rela->offset;
            if (type == ELF_R_X86_64_64) {
                *(uint64_t *)(uintptr_t)patch = symbol_value + (uint64_t)rela->addend;
            } else if (type == ELF_R_X86_64_PC32 || type == ELF_R_X86_64_PLT32) {
                value = (int64_t)symbol_value + rela->addend - (int64_t)patch;
                if (value < -2147483648LL || value > 2147483647LL) {
                    return -8;
                }
                *(uint32_t *)(uintptr_t)patch = (uint32_t)value;
            } else if (type == ELF_R_X86_64_32 || type == ELF_R_X86_64_32S) {
                value = (int64_t)symbol_value + rela->addend;
                if ((type == ELF_R_X86_64_32 &&
                     (value < 0 || (uint64_t)value > 0xffffffffULL)) ||
                    (type == ELF_R_X86_64_32S &&
                     (value < -2147483648LL || value > 2147483647LL))) {
                    return -8;
                }
                *(uint32_t *)(uintptr_t)patch = (uint32_t)value;
            } else {
                return -95;
            }
        }
    }
    return 0;
}

/**
 * @brief Coordinates the driver find module operation.
 * @param data Input or output value used by this operation.
 * @param length Length, size, or element count associated with the operation.
 * @param header Input or output value used by this operation.
 * @param sections Input or output value used by this operation.
 * @param section_addresses Address used by this operation; its address-space interpretation follows the API.
 * @param out Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
static int driver_find_module(const uint8_t *data, uint64_t length,
                              const struct elf64_ehdr *header,
                              const struct elf64_shdr *sections,
                              const uint64_t section_addresses[],
                              const struct leonos_driver_module **out)
{
    for (uint16_t index = 0; index < header->shnum; ++index) {
        const struct elf64_shdr *symbols = &sections[index];
        const struct elf64_shdr *strings;
        uint64_t count;
        if (symbols->type != ELF_SHT_SYMTAB || symbols->link >= header->shnum ||
            symbols->entsize != sizeof(struct elf64_sym) ||
            !driver_range_valid(symbols->offset, symbols->size, length)) {
            continue;
        }
        strings = &sections[symbols->link];
        if (strings->type != ELF_SHT_STRTAB ||
            !driver_range_valid(strings->offset, strings->size, length)) {
            return -8;
        }
        count = symbols->size / sizeof(struct elf64_sym);
        for (uint64_t symbol_index = 0; symbol_index < count; ++symbol_index) {
            const struct elf64_sym *symbol =
                (const struct elf64_sym *)(data + symbols->offset +
                                            symbol_index * sizeof(struct elf64_sym));
            uint64_t value;
            const char *name;
            if (symbol->name >= strings->size) {
                return -8;
            }
            name = (const char *)(data + strings->offset + symbol->name);
            if (!driver_range_valid(symbols->offset + symbol_index * sizeof(struct elf64_sym),
                                    sizeof(struct elf64_sym), length) ||
                !driver_elf_string_equal(name, strings->size - symbol->name,
                                         "leonos_driver_module")) {
                continue;
            }
            if (symbol->shndx == ELF_SHN_UNDEF || symbol->shndx >= header->shnum ||
                symbol->value > sections[symbol->shndx].size ||
                sizeof(struct leonos_driver_module) >
                    sections[symbol->shndx].size - symbol->value ||
                driver_symbol_value(symbol, header->shnum, sections, section_addresses,
                                    &value) < 0) {
                return -8;
            }
            *out = (const struct leonos_driver_module *)(uintptr_t)value;
            return 0;
        }
    }
    return -2;
}

/**
 * @brief Coordinates the driver register mouse operation.
 * @param ops Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int driver_register_mouse(const struct leonos_driver_mouse_ops *ops)
{
    if (loading_slot < 0 || !ops || !ops->poll || !ops->get_state) {
        return -22;
    }
    mouse_ops = ops;
    mouse_owner = (uint32_t)loading_slot;
    return 0;
}

/**
 * @brief Coordinates the driver register serial operation.
 * @param ops Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int driver_register_serial(const struct leonos_driver_serial_ops *ops)
{
    if (loading_slot < 0 || !ops || !ops->is_ready || !ops->write) {
        return -22;
    }
    serial_ops = ops;
    serial_owner = (uint32_t)loading_slot;
    return 0;
}

/**
 * @brief Coordinates the driver register e1000 operation.
 * @param ops Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int driver_register_e1000(const struct leonos_driver_e1000_ops *ops)
{
    if (loading_slot < 0 || !ops || !ops->is_ready || !ops->mac || !ops->send ||
        !ops->poll || !ops->get_info) {
        return -22;
    }
    e1000_ops = ops;
    e1000_owner = (uint32_t)loading_slot;
    return 0;
}

/**
 * @brief Coordinates the driver register audio operation.
 * @param ops Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int driver_register_audio(const struct leonos_driver_audio_ops *ops)
{
    if (loading_slot < 0 || !ops || !ops->is_ready || !ops->configure ||
        !ops->write || !ops->get_state) {
        return -22;
    }
    audio_ops = ops;
    audio_owner = (uint32_t)loading_slot;
    return 0;
}

/**
 * @brief Coordinates the driver api inb operation.
 * @param port Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static uint8_t driver_api_inb(uint16_t port)
{
    return x86_64_inb(port);
}

/**
 * @brief Coordinates the driver api outb operation.
 * @param port Input or output value used by this operation.
 * @param value Input or output value used by this operation.
 */
static void driver_api_outb(uint16_t port, uint8_t value)
{
    x86_64_outb(value, port);
}

/**
 * @brief Coordinates the driver api inl operation.
 * @param port Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static uint32_t driver_api_inl(uint16_t port)
{
    return x86_64_inl(port);
}

/**
 * @brief Coordinates the driver api outl operation.
 * @param port Input or output value used by this operation.
 * @param value Input or output value used by this operation.
 */
static void driver_api_outl(uint16_t port, uint32_t value)
{
    x86_64_outl(value, port);
}

/**
 * @brief Coordinates the driver api framebuffer size operation.
 * @param width Input or output value used by this operation.
 * @param height Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int driver_api_framebuffer_size(uint32_t *width, uint32_t *height)
{
    const struct framebuffer *framebuffer = framebuffer_get();
    if (!framebuffer || !framebuffer->available) {
        return -2;
    }
    if (width) {
        *width = framebuffer->width;
    }
    if (height) {
        *height = framebuffer->height;
    }
    return 0;
}

/**
 * @brief Coordinates the driver api pci find operation.
 * @param vendor_id Input or output value used by this operation.
 * @param device_id Input or output value used by this operation.
 * @param out Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
static int driver_api_pci_find(uint16_t vendor_id, uint16_t device_id,
                               struct leonos_driver_pci_device *out)
{
    struct pci_device device;
    int ret = pci_find_device(vendor_id, device_id, &device);
    if (ret < 0 || !out) {
        return ret < 0 ? ret : -22;
    }
    *out = (struct leonos_driver_pci_device){
        .bus = device.bus,
        .slot = device.slot,
        .function = device.function,
        .class_code = device.class_code,
        .vendor_id = device.vendor_id,
        .device_id = device.device_id,
        .subclass = device.subclass,
        .prog_if = device.prog_if,
        .header_type = 0,
        .reserved = 0,
    };
    return 0;
}

static const struct leonos_driver_kernel_api driver_kernel_api = {
    .abi_version = LEONOS_DRIVER_ABI_VERSION,
    .struct_size = sizeof(struct leonos_driver_kernel_api),
    .inb = driver_api_inb,
    .outb = driver_api_outb,
    .inl = driver_api_inl,
    .outl = driver_api_outl,
    .alloc_pages = mm_alloc_pages,
    .free_pages = mm_free_pages,
    .console_write = console_write,
    .input_push_mouse = input_push_mouse,
    .input_push_mouse_wheel = input_push_mouse_wheel,
    .framebuffer_size = driver_api_framebuffer_size,
    .pci_find = driver_api_pci_find,
    .pci_read16 = pci_config_read16,
    .pci_write16 = pci_config_write16,
    .pci_read32 = pci_config_read32,
    .ticks = time_ticks,
    .sleep_ms = time_sleep_ms,
    .register_mouse = driver_register_mouse,
    .register_serial = driver_register_serial,
    .register_e1000 = driver_register_e1000,
    .register_audio = driver_register_audio,
};

/**
 * @brief Coordinates the driver clear services operation.
 * @param slot_id Input or output value used by this operation.
 */
static void driver_clear_services(uint32_t slot_id)
{
    if (mouse_ops && mouse_owner == slot_id) {
        mouse_ops = 0;
        mouse_owner = 0;
        mouse_cache = (struct mouse_state){0};
    }
    if (serial_ops && serial_owner == slot_id) {
        serial_ops = 0;
        serial_owner = 0;
    }
    if (e1000_ops && e1000_owner == slot_id) {
        net_driver_detached();
        e1000_ops = 0;
        e1000_owner = 0;
    }
    if (audio_ops && audio_owner == slot_id) {
        audio_ops = 0;
        audio_owner = 0;
    }
}

/**
 * @brief Coordinates the driver load slot operation.
 * @param slot Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int driver_load_slot(struct driver_slot *slot)
{
    char path[LEONOS_DRIVER_FILE_LEN + 16U];
    const uint8_t *data = 0;
    uint64_t length = 0;
    const struct elf64_ehdr *header;
    const struct elf64_shdr *sections;
    uint64_t section_addresses[DRIVER_ELF_MAX_SECTIONS];
    uint64_t image_size = 0;
    uint64_t image_phys = 0;
    uint32_t image_pages = 0;
    const struct leonos_driver_module *module = 0;
    int ret = -5;
    if (!slot || slot->info.state == LEONOS_DRIVER_STATE_LOADED) {
        return slot ? 0 : -22;
    }
    if (slot->info.flags & LEONOS_DRIVER_FLAG_DISABLED) {
        return -13;
    }
    driver_make_path(path, sizeof(path), slot->info.file);
    slot->info.state = LEONOS_DRIVER_STATE_LOADING;
    slot->info.error[0] = 0;
    if ((ret = driver_read_file(path, &data, &length)) < 0) {
        driver_set_error(slot, ret, "Cannot read module file");
        return ret;
    }
    if (length < sizeof(*header)) {
        ret = -8;
        driver_set_error(slot, ret, "ELF header is truncated");
        goto out;
    }
    header = (const struct elf64_ehdr *)data;
    if (header->ident[0] != 0x7f || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F' ||
        header->ident[4] != 2U || header->ident[5] != 1U ||
        header->type != ELF_ET_REL || header->machine != ELF_EM_X86_64 ||
        header->ehsize != sizeof(*header) || header->shnum == 0 ||
        header->shnum > DRIVER_ELF_MAX_SECTIONS ||
        header->shentsize != sizeof(struct elf64_shdr) ||
        !driver_range_valid(header->shoff,
                            (uint64_t)header->shnum * sizeof(struct elf64_shdr), length)) {
        ret = -8;
        driver_set_error(slot, ret, "Unsupported ELF64 relocatable module");
        goto out;
    }
    sections = (const struct elf64_shdr *)(data + header->shoff);
    driver_memzero(section_addresses, sizeof(section_addresses));
    for (uint16_t index = 0; index < header->shnum; ++index) {
        const struct elf64_shdr *section = &sections[index];
        if (section->type != ELF_SHT_NOBITS &&
            !driver_range_valid(section->offset, section->size, length)) {
            ret = -8;
            driver_set_error(slot, ret, "ELF section exceeds module file");
            goto out;
        }
        if (!(section->flags & ELF_SHF_ALLOC)) {
            continue;
        }
        image_size = driver_align_up(image_size, section->addralign);
        if (section->size > DRIVER_ELF_MAX_IMAGE || image_size > DRIVER_ELF_MAX_IMAGE - section->size) {
            ret = -12;
            driver_set_error(slot, ret, "Module image is too large");
            goto out;
        }
        image_size += section->size;
    }
    if (image_size == 0) {
        ret = -8;
        driver_set_error(slot, ret, "Module has no allocatable sections");
        goto out;
    }
    image_pages = (uint32_t)((image_size + 4095U) / 4096U);
    image_phys = mm_alloc_pages(image_pages);
    if (!image_phys) {
        ret = -12;
        driver_set_error(slot, ret, "No memory for module image");
        goto out;
    }
    driver_memzero((void *)(uintptr_t)image_phys, (uint64_t)image_pages * 4096U);
    image_size = 0;
    for (uint16_t index = 0; index < header->shnum; ++index) {
        const struct elf64_shdr *section = &sections[index];
        if (!(section->flags & ELF_SHF_ALLOC)) {
            continue;
        }
        image_size = driver_align_up(image_size, section->addralign);
        section_addresses[index] = image_phys + image_size;
        if (section->type != ELF_SHT_NOBITS && section->size) {
            driver_memcpy((void *)(uintptr_t)section_addresses[index],
                          data + section->offset, section->size);
        }
        image_size += section->size;
    }
    if ((ret = driver_apply_relocations(data, length, header, sections, section_addresses)) < 0) {
        driver_set_error(slot, ret, "Unsupported or invalid module relocation");
        goto out;
    }
    if ((ret = driver_find_module(data, length, header, sections, section_addresses, &module)) < 0 ||
        !module || module->magic != LEONOS_DRIVER_MODULE_MAGIC ||
        module->abi_version != LEONOS_DRIVER_ABI_VERSION ||
        module->struct_size != sizeof(*module) || !module->name[0] || !module->init) {
        ret = ret < 0 ? ret : -8;
        driver_set_error(slot, ret, "Driver descriptor ABI is invalid");
        goto out;
    }
    loading_slot = (int32_t)slot->info.id;
    ret = module->init(&driver_kernel_api);
    loading_slot = -1;
    if (ret < 0) {
        if (module->fini) {
            module->fini();
        }
        driver_clear_services(slot->info.id);
        driver_set_error(slot, ret, "Driver initialization failed");
        goto out;
    }
    slot->module = module;
    slot->image_phys = image_phys;
    slot->image_pages = image_pages;
    slot->info.state = LEONOS_DRIVER_STATE_LOADED;
    slot->info.kind = module->kind;
    slot->info.abi_version = module->abi_version;
    slot->info.version = module->version;
    slot->info.load_address = image_phys;
    slot->info.image_size = image_size;
    driver_copy_text(slot->info.name, sizeof(slot->info.name), module->name);
    slot->info.error[0] = 0;
    console_printf("[driver] loaded %s as %s abi=%u\n", slot->info.file,
                   slot->info.name, slot->info.abi_version);
    ret = 0;
out:
    loading_slot = -1;
    driver_release_file(data, length);
    if (ret < 0 && image_phys) {
        mm_free_pages(image_phys, image_pages);
    }
    return ret;
}

/**
 * @brief Coordinates the driver unload slot operation.
 * @param slot Input or output value used by this operation.
 * @param force Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int driver_unload_slot(struct driver_slot *slot, uint32_t force)
{
    if (!slot || slot->info.state != LEONOS_DRIVER_STATE_LOADED || !slot->module) {
        return -2;
    }
    if (!force && slot->info.kind == LEONOS_DRIVER_KIND_NETWORK) {
        return -16;
    }
    if (slot->module->fini) {
        slot->module->fini();
    }
    driver_clear_services(slot->info.id);
    if (slot->image_phys && slot->image_pages) {
        mm_free_pages(slot->image_phys, slot->image_pages);
    }
    slot->module = 0;
    slot->image_phys = 0;
    slot->image_pages = 0;
    slot->info.state = (slot->info.flags & LEONOS_DRIVER_FLAG_DISABLED)
                           ? LEONOS_DRIVER_STATE_DISABLED
                           : LEONOS_DRIVER_STATE_UNLOADED;
    slot->info.load_address = 0;
    slot->info.image_size = 0;
    slot->info.error[0] = 0;
    console_printf("[driver] unloaded %s%s\n", slot->info.file,
                   force ? " (forced)" : "");
    return 0;
}

/**
 * @brief Coordinates the driver scan operation.
 */
static void driver_scan(void)
{
    struct leonos_dir_entry entries[LEONOS_FS_MAX_ENTRIES];
    uint32_t count = 0;
    if (storage_list_dir(DRIVER_DIRECTORY, entries, LEONOS_FS_MAX_ENTRIES, &count) < 0) {
        console_printf("[driver] no %s directory\n", DRIVER_DIRECTORY);
        return;
    }
    for (uint32_t index = 0; index < count; ++index) {
        for (uint32_t next = index + 1U; next < count; ++next) {
            if (driver_load_order_compare(entries[next].name, entries[index].name) < 0) {
                struct leonos_dir_entry entry = entries[index];
                entries[index] = entries[next];
                entries[next] = entry;
            }
        }
    }
    for (uint32_t index = 0; index < count; ++index) {
        if (entries[index].type == LEONOS_FS_TYPE_FILE &&
            driver_file_name_valid(entries[index].name)) {
            /**
 * @brief Coordinates the driver get slot operation.
 * @param name Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
            (void)driver_get_slot(entries[index].name);
        }
    }
}

/**
 * @brief Coordinates the driver manager init operation.
 */
void driver_manager_init(void)
{
    driver_memzero(driver_slots, sizeof(driver_slots));
    mouse_cache = (struct mouse_state){0};
    console_printf("[driver] manager ready abi=%u\n", LEONOS_DRIVER_ABI_VERSION);
}

/**
 * @brief Coordinates the driver manager autoload operation.
 */
void driver_manager_autoload(void)
{
    driver_scan();
    for (uint32_t index = 0; index < LEONOS_DRIVER_MAX; ++index) {
        struct driver_slot *slot = &driver_slots[index];
        if (!slot->info.file[0] || slot->info.state == LEONOS_DRIVER_STATE_DISABLED ||
            slot->info.state == LEONOS_DRIVER_STATE_LOADED) {
            continue;
        }
        if (driver_load_slot(slot) < 0) {
            console_printf("[driver] retrying %s\n", slot->info.file);
            /**
 * @brief Coordinates the driver load slot operation.
 * @param slot Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
            (void)driver_load_slot(slot);
        }
    }
}

/**
 * @brief Coordinates the driver manager list operation.
 * @param query Request structure consumed and, where defined, updated by this operation.
 * @return Result, status, or value defined by this API.
 */
int driver_manager_list(struct leonos_driver_list *query)
{
    uint32_t count = 0;
    if (!query) {
        return -22;
    }
    if (query->capacity > LEONOS_DRIVER_MAX) {
        query->capacity = LEONOS_DRIVER_MAX;
    }
    for (uint32_t index = 0; index < LEONOS_DRIVER_MAX; ++index) {
        if (!driver_slots[index].info.file[0]) {
            continue;
        }
        if (query->drivers && count < query->capacity) {
            query->drivers[count] = driver_slots[index].info;
        }
        ++count;
    }
    query->count = count;
    return 0;
}

/**
 * @brief Coordinates the driver manager control operation.
 * @param request Request structure consumed and, where defined, updated by this operation.
 * @return Result, status, or value defined by this API.
 */
int driver_manager_control(struct leonos_driver_control *request)
{
    struct driver_slot *slot;
    int ret;
    if (!request) {
        return -22;
    }
    if (request->action == LEONOS_DRIVER_CONTROL_RESCAN) {
        driver_manager_autoload();
        request->status = 0;
        return 0;
    }
    if (!driver_file_name_valid(request->file)) {
        request->status = -22;
        return -22;
    }
    slot = driver_get_slot(request->file);
    if (!slot) {
        request->status = -28;
        return -28;
    }
    if (request->action == LEONOS_DRIVER_CONTROL_LOAD) {
        ret = driver_load_slot(slot);
    } else if (request->action == LEONOS_DRIVER_CONTROL_UNLOAD) {
        ret = driver_unload_slot(slot, 0);
    } else if (request->action == LEONOS_DRIVER_CONTROL_FORCE_UNLOAD) {
        ret = driver_unload_slot(slot, 1);
    } else if (request->action == LEONOS_DRIVER_CONTROL_ENABLE_BOOT ||
               request->action == LEONOS_DRIVER_CONTROL_DISABLE_BOOT) {
        uint32_t disable = request->action == LEONOS_DRIVER_CONTROL_DISABLE_BOOT;
        if (disable && slot->info.state == LEONOS_DRIVER_STATE_LOADED) {
            ret = driver_unload_slot(slot, 1);
            if (ret < 0) {
                request->status = ret;
                return ret;
            }
        }
        if (disable) {
            slot->info.flags |= LEONOS_DRIVER_FLAG_DISABLED;
            slot->info.state = LEONOS_DRIVER_STATE_DISABLED;
        } else {
            slot->info.flags &= ~LEONOS_DRIVER_FLAG_DISABLED;
            if (slot->info.state == LEONOS_DRIVER_STATE_DISABLED) {
                slot->info.state = LEONOS_DRIVER_STATE_UNLOADED;
            }
        }
        ret = driver_write_config();
    } else {
        ret = -22;
    }
    request->status = ret;
    return ret;
}

/**
 * @brief Coordinates the mouse init operation.
 */
void mouse_init(void)
{
}

/**
 * @brief Coordinates the mouse poll operation.
 */
void mouse_poll(void)
{
    if (mouse_ops && mouse_ops->poll) {
        mouse_ops->poll();
    }
}

/**
 * @brief Coordinates the mouse get state operation.
 * @return Result, status, or value defined by this API.
 */
const struct mouse_state *mouse_get_state(void)
{
    struct leonos_driver_mouse_state state;
    if (mouse_ops && mouse_ops->get_state) {
        state = (struct leonos_driver_mouse_state){0};
        mouse_ops->get_state(&state);
        mouse_cache = (struct mouse_state){
            .x = state.x,
            .y = state.y,
            .buttons = state.buttons,
            .present = state.present != 0,
            .absolute = state.absolute != 0,
        };
    }
    return &mouse_cache;
}

/**
 * @brief Coordinates the mouse set visible operation.
 * @param visible Input or output value used by this operation.
 */
void mouse_set_visible(bool visible)
{
    mouse_visible = visible;
}

/**
 * @brief Coordinates the mouse is visible operation.
 * @return Result, status, or value defined by this API.
 */
bool mouse_is_visible(void)
{
    return mouse_visible;
}

/**
 * @brief Coordinates the mouse event count operation.
 * @return Result, status, or value defined by this API.
 */
uint32_t mouse_event_count(void)
{
    struct leonos_driver_mouse_state state;
    if (!mouse_ops || !mouse_ops->get_state) {
        return 0;
    }
    mouse_ops->get_state(&state);
    return state.event_count;
}

/**
 * @brief Coordinates the mouse last status operation.
 * @return Result, status, or value defined by this API.
 */
uint8_t mouse_last_status(void)
{
    struct leonos_driver_mouse_state state;
    if (!mouse_ops || !mouse_ops->get_state) {
        return 0;
    }
    mouse_ops->get_state(&state);
    return state.last_status;
}

/**
 * @brief Coordinates the mouse last data operation.
 * @return Result, status, or value defined by this API.
 */
uint8_t mouse_last_data(void)
{
    struct leonos_driver_mouse_state state;
    if (!mouse_ops || !mouse_ops->get_state) {
        return 0;
    }
    mouse_ops->get_state(&state);
    return state.last_data;
}

/**
 * @brief Coordinates the mouse last ack operation.
 * @return Result, status, or value defined by this API.
 */
uint8_t mouse_last_ack(void)
{
    struct leonos_driver_mouse_state state;
    if (!mouse_ops || !mouse_ops->get_state) {
        return 0;
    }
    mouse_ops->get_state(&state);
    return state.last_ack;
}

/**
 * @brief Coordinates the driver manager mouse state operation.
 * @param out Caller-provided storage that receives output from this operation.
 */
void driver_manager_mouse_state(struct mouse_state *out)
{
    if (out) {
        *out = *mouse_get_state();
    }
}

/**
 * @brief Coordinates the driver manager mouse event count operation.
 * @return Result, status, or value defined by this API.
 */
uint32_t driver_manager_mouse_event_count(void)
{
    return mouse_event_count();
}

/**
 * @brief Coordinates the driver manager mouse last status operation.
 * @return Result, status, or value defined by this API.
 */
uint8_t driver_manager_mouse_last_status(void)
{
    return mouse_last_status();
}

/**
 * @brief Coordinates the driver manager mouse last data operation.
 * @return Result, status, or value defined by this API.
 */
uint8_t driver_manager_mouse_last_data(void)
{
    return mouse_last_data();
}

/**
 * @brief Coordinates the driver manager mouse last ack operation.
 * @return Result, status, or value defined by this API.
 */
uint8_t driver_manager_mouse_last_ack(void)
{
    return mouse_last_ack();
}

/**
 * @brief Coordinates the driver manager mouse poll operation.
 */
void driver_manager_mouse_poll(void)
{
    mouse_poll();
}

/**
 * @brief Coordinates the serial init operation.
 */
void serial_init(void)
{
    x86_64_outb(0x00u, (uint16_t)(EARLY_SERIAL_COM1 + 1u));
    x86_64_outb(0x80u, (uint16_t)(EARLY_SERIAL_COM1 + 3u));
    x86_64_outb(0x03u, (uint16_t)(EARLY_SERIAL_COM1 + 0u));
    x86_64_outb(0x00u, (uint16_t)(EARLY_SERIAL_COM1 + 1u));
    x86_64_outb(0x03u, (uint16_t)(EARLY_SERIAL_COM1 + 3u));
    x86_64_outb(0xc7u, (uint16_t)(EARLY_SERIAL_COM1 + 2u));
    x86_64_outb(0x0bu, (uint16_t)(EARLY_SERIAL_COM1 + 4u));
    early_serial_ready = 1;
}

/**
 * @brief Coordinates the serial is ready operation.
 * @return Result, status, or value defined by this API.
 */
int serial_is_ready(void)
{
    return serial_ops && serial_ops->is_ready ? serial_ops->is_ready() : 0;
}

/**
 * @brief Coordinates the serial write operation.
 * @param text Input or output value used by this operation.
 */
void serial_write(const char *text)
{
    if (serial_ops && serial_ops->write) {
        serial_ops->write(text);
        return;
    }
    early_serial_write(text);
}

/**
 * @brief Coordinates the e1000 init operation.
 */
void e1000_init(void)
{
}

/**
 * @brief Coordinates the e1000 is ready operation.
 * @return Result, status, or value defined by this API.
 */
int e1000_is_ready(void)
{
    return e1000_ops && e1000_ops->is_ready ? e1000_ops->is_ready() : 0;
}

/**
 * @brief Coordinates the e1000 mac operation.
 * @return Result, status, or value defined by this API.
 */
const uint8_t *e1000_mac(void)
{
    return e1000_is_ready() ? e1000_ops->mac() : e1000_empty_mac;
}

/**
 * @brief Coordinates the e1000 send operation.
 * @param frame Trap or syscall frame supplied by the architecture layer.
 * @param len Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
 */
int e1000_send(const void *frame, uint32_t len)
{
    return e1000_is_ready() ? e1000_ops->send(frame, len) : -19;
}

/**
 * @brief Coordinates the e1000 poll operation.
 * @param frame Trap or syscall frame supplied by the architecture layer.
 * @param capacity Capacity, in elements or bytes, of the related output buffer.
 * @param out_len Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
int e1000_poll(void *frame, uint32_t capacity, uint32_t *out_len)
{
    return e1000_is_ready() ? e1000_ops->poll(frame, capacity, out_len) : -19;
}

/**
 * @brief Coordinates the e1000 get info operation.
 * @param info Input or output value used by this operation.
 */
void e1000_get_info(struct e1000_info *info)
{
    struct leonos_driver_e1000_info source;
    if (!info) {
        return;
    }
    *info = (struct e1000_info){0};
    if (!e1000_is_ready()) {
        return;
    }
    e1000_ops->get_info(&source);
    info->present = source.present;
    info->active = source.active;
    info->vendor_id = source.vendor_id;
    info->device_id = source.device_id;
    info->bus = source.bus;
    info->slot = source.slot;
    info->function = source.function;
    for (uint32_t index = 0; index < 6; ++index) {
        info->mac[index] = source.mac[index];
    }
}

/**
 * @brief Coordinates the driver manager audio configure operation.
 * @param format Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int driver_manager_audio_configure(const struct leonos_audio_format *format)
{
    if (!format || !audio_ops || !audio_ops->is_ready || !audio_ops->configure ||
        !audio_ops->is_ready()) {
        return -19;
    }
    return audio_ops->configure(format);
}

/**
 * @brief Coordinates the driver manager audio write operation.
 * @param data Input or output value used by this operation.
 * @param length Length, size, or element count associated with the operation.
 * @param out_status Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
long driver_manager_audio_write(const void *data, uint32_t length,
                                uint32_t *out_status)
{
    if (out_status) {
        *out_status = LEONOS_AUDIO_STATUS_NO_DEVICE;
    }
    if (!audio_ops || !audio_ops->is_ready || !audio_ops->write ||
        !audio_ops->is_ready()) {
        return -19;
    }
    return audio_ops->write(data, length, out_status);
}

/**
 * @brief Coordinates the driver manager audio get state operation.
 * @param out Caller-provided storage that receives output from this operation.
 */
void driver_manager_audio_get_state(struct leonos_audio_state *out)
{
    if (!out) {
        return;
    }
    *out = (struct leonos_audio_state){0};
    if (audio_ops && audio_ops->is_ready && audio_ops->is_ready() &&
        audio_ops->get_state) {
        audio_ops->get_state(out);
    }
}
