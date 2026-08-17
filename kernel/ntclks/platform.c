/*
 * LeonOS platform support: handles firmware-provided platform information.
 * Converts EFI tables and platform services into kernel boot abstractions.
 */
#include <ntclks/console.h>
#include <ntclks/platform.h>

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

struct efi_configuration_table {
    struct efi_guid vendor_guid;
    void *vendor_table;
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
    void *boot_services;
    uint64_t number_of_table_entries;
    struct efi_configuration_table *configuration_table;
};

struct __attribute__((packed)) smbios3_entry {
    char anchor[5];
    uint8_t checksum;
    uint8_t length;
    uint8_t major;
    uint8_t minor;
    uint8_t docrev;
    uint8_t revision;
    uint8_t reserved;
    uint32_t table_max_size;
    uint64_t table_address;
};

struct __attribute__((packed)) smbios2_entry {
    char anchor[4];
    uint8_t checksum;
    uint8_t length;
    uint8_t major;
    uint8_t minor;
    uint16_t max_structure_size;
    uint8_t entry_point_revision;
    uint8_t formatted_area[5];
    char intermediate_anchor[5];
    uint8_t intermediate_checksum;
    uint16_t table_length;
    uint32_t table_address;
    uint16_t structure_count;
    uint8_t bcd_revision;
};

struct __attribute__((packed)) smbios_header {
    uint8_t type;
    uint8_t length;
    uint16_t handle;
};

static struct leonos_machine_identity platform_identity;

/**
 * @brief Coordinates the guid equal operation.
 * @param a Input or output value used by this operation.
 * @param b Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int guid_equal(const struct efi_guid *a, const struct efi_guid *b)
{
    if (!a || !b) {
        return 0;
    }
    if (a->data1 != b->data1 || a->data2 != b->data2 || a->data3 != b->data3) {
        return 0;
    }
    for (uint32_t i = 0; i < 8U; ++i) {
        if (a->data4[i] != b->data4[i]) {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Coordinates the bytes eq operation.
 * @param a Input or output value used by this operation.
 * @param b Input or output value used by this operation.
 * @param len Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
 */
static int bytes_eq(const char *a, const char *b, uint32_t len)
{
    for (uint32_t i = 0; i < len; ++i) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Copies text.
 * @param dst Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param src Input or output value used by this operation.
 */
static void copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[i] && i + 1U < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

/**
 * @brief Copies efi text.
 * @param dst Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param src Input or output value used by this operation.
 */
static void copy_efi_text(char *dst, uint32_t cap, const uint16_t *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[i] && i + 1U < cap) {
        uint16_t ch = src[i];
        dst[i] = (ch >= 32U && ch < 127U) ? (char)ch : '?';
        ++i;
    }
    dst[i] = 0;
}

/**
 * @brief Coordinates the uuid valid operation.
 * @param uuid Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int uuid_valid(const uint8_t uuid[16])
{
    uint8_t or_all = 0;
    uint8_t and_all = 0xffU;
    for (uint32_t i = 0; i < 16U; ++i) {
        or_all |= uuid[i];
        and_all &= uuid[i];
    }
    return or_all != 0 && and_all != 0xffU;
}

/**
 * @brief Appends hex2.
 * @param dst Input or output value used by this operation.
 * @param pos Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param value Input or output value used by this operation.
 */
static void append_hex2(char *dst, uint32_t *pos, uint32_t cap, uint8_t value)
{
    static const char hex[] = "0123456789abcdef";
    if (!dst || !pos || *pos + 2U >= cap) {
        return;
    }
    dst[(*pos)++] = hex[value >> 4];
    dst[(*pos)++] = hex[value & 0x0fU];
    dst[*pos] = 0;
}

/**
 * @brief Coordinates the format uuid raw operation.
 * @param uuid Input or output value used by this operation.
 * @param out Caller-provided storage that receives output from this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 */
static void format_uuid_raw(const uint8_t uuid[16], char *out, uint32_t cap)
{
    static const uint8_t order[16] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    };
    uint32_t pos = 0;
    if (!out || cap < LEONOS_MACHINE_IDENTITY_UUID_LEN) {
        if (out && cap) {
            out[0] = 0;
        }
        return;
    }
    out[0] = 0;
    for (uint32_t i = 0; i < 16U; ++i) {
        if (i == 4U || i == 6U || i == 8U || i == 10U) {
            out[pos++] = '-';
            out[pos] = 0;
        }
        append_hex2(out, &pos, cap, uuid[order[i]]);
    }
}

/**
 * @brief Coordinates the checksum8 operation.
 * @param data Input or output value used by this operation.
 * @param len Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
 */
static uint8_t checksum8(const void *data, uint32_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; ++i) {
        sum = (uint8_t)(sum + bytes[i]);
    }
    return sum;
}

/**
 * @brief Coordinates the smbios next operation.
 * @param ptr Input or output value used by this operation.
 * @param end Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static const uint8_t *smbios_next(const uint8_t *ptr, const uint8_t *end)
{
    const struct smbios_header *hdr = (const struct smbios_header *)ptr;
    const uint8_t *p;
    if (!ptr || ptr + sizeof(*hdr) > end || hdr->length < sizeof(*hdr) ||
        ptr + hdr->length > end) {
        return end;
    }
    p = ptr + hdr->length;
    while (p + 1U < end) {
        if (p[0] == 0 && p[1] == 0) {
            return p + 2U;
        }
        ++p;
    }
    return end;
}

/**
 * @brief Parses smbios table.
 * @param table_addr Address used by this operation; its address-space interpretation follows the API.
 * @param table_len Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
 */
static int parse_smbios_table(uint64_t table_addr, uint32_t table_len)
{
    const uint8_t *table = (const uint8_t *)(uintptr_t)table_addr;
    const uint8_t *end;
    if (!table || table_len < sizeof(struct smbios_header)) {
        return -1;
    }
    end = table + table_len;
    for (const uint8_t *ptr = table; ptr + sizeof(struct smbios_header) <= end;) {
        const struct smbios_header *hdr = (const struct smbios_header *)ptr;
        if (hdr->length < sizeof(*hdr) || ptr + hdr->length > end) {
            break;
        }
        if (hdr->type == 1U && hdr->length >= 0x19U) {
            const uint8_t *uuid = ptr + 8U;
            if (uuid_valid(uuid)) {
                format_uuid_raw(uuid, platform_identity.platform_uuid,
                                sizeof(platform_identity.platform_uuid));
                platform_identity.flags |= LEONOS_MACHINE_IDENTITY_FLAG_PLATFORM_UUID;
                copy_text(platform_identity.source, sizeof(platform_identity.source),
                          "smbios-system-uuid");
                return 0;
            }
        }
        if (hdr->type == 127U) {
            break;
        }
        ptr = smbios_next(ptr, end);
    }
    return -1;
}

/**
 * @brief Parses smbios3.
 * @param entry Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int parse_smbios3(const void *entry)
{
    const struct smbios3_entry *smbios = (const struct smbios3_entry *)entry;
    if (!smbios || !bytes_eq(smbios->anchor, "_SM3_", 5U) ||
        smbios->length < sizeof(*smbios) ||
        checksum8(smbios, smbios->length) != 0) {
        return -1;
    }
    return parse_smbios_table(smbios->table_address, smbios->table_max_size);
}

/**
 * @brief Parses smbios2.
 * @param entry Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int parse_smbios2(const void *entry)
{
    const struct smbios2_entry *smbios = (const struct smbios2_entry *)entry;
    if (!smbios || !bytes_eq(smbios->anchor, "_SM_", 4U) ||
        smbios->length < 0x1fU ||
        checksum8(smbios, smbios->length) != 0 ||
        !bytes_eq(smbios->intermediate_anchor, "_DMI_", 5U)) {
        return -1;
    }
    return parse_smbios_table(smbios->table_address, smbios->table_length);
}

/**
 * @brief Coordinates the platform identity init operation.
 * @param boot Boot information supplied by the loader.
 */
void platform_identity_init(const struct boot_info *boot)
{
    static const struct efi_guid smbios3_guid = {
        0xf2fd1544U, 0x9794U, 0x4a2cU,
        {0x99U, 0x2eU, 0xe5U, 0xbbU, 0xcfU, 0x20U, 0xe3U, 0x94U},
    };
    static const struct efi_guid smbios2_guid = {
        0xeb9d2d31U, 0x2d88U, 0x11d3U,
        {0x9aU, 0x16U, 0x00U, 0x90U, 0x27U, 0x3fU, 0xc1U, 0x4dU},
    };
    struct efi_system_table *st;
    platform_identity = (struct leonos_machine_identity){0};
    platform_identity.version = LEONOS_MACHINE_IDENTITY_VERSION;
    copy_text(platform_identity.source, sizeof(platform_identity.source), "unavailable");
    if (!boot || !boot->efi_system_table) {
        console_printf("[ntclks] platform identity unavailable: no EFI system table\n");
        return;
    }
    st = (struct efi_system_table *)(uintptr_t)boot->efi_system_table;
    platform_identity.firmware_revision = st->firmware_revision;
    copy_efi_text(platform_identity.firmware_vendor,
                  sizeof(platform_identity.firmware_vendor),
                  st->firmware_vendor);
    if (!st->configuration_table || st->number_of_table_entries > 256ULL) {
        console_printf("[ntclks] platform identity unavailable: no EFI config table\n");
        return;
    }
    for (uint64_t i = 0; i < st->number_of_table_entries; ++i) {
        struct efi_configuration_table *table = &st->configuration_table[i];
        if (guid_equal(&table->vendor_guid, &smbios3_guid) &&
            parse_smbios3(table->vendor_table) == 0) {
            console_printf("[ntclks] platform identity source=SMBIOS3 uuid=%s\n",
                           platform_identity.platform_uuid);
            return;
        }
    }
    for (uint64_t i = 0; i < st->number_of_table_entries; ++i) {
        struct efi_configuration_table *table = &st->configuration_table[i];
        if (guid_equal(&table->vendor_guid, &smbios2_guid) &&
            parse_smbios2(table->vendor_table) == 0) {
            console_printf("[ntclks] platform identity source=SMBIOS2 uuid=%s\n",
                           platform_identity.platform_uuid);
            return;
        }
    }
    console_printf("[ntclks] platform identity unavailable: no SMBIOS UUID\n");
}

/**
 * @brief Coordinates the platform machine identity operation.
 * @param identity Input or output value used by this operation.
 */
void platform_machine_identity(struct leonos_machine_identity *identity)
{
    if (!identity) {
        return;
    }
    *identity = platform_identity;
}
