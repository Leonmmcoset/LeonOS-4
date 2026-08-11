#include <ntclks/console.h>
#include <ntclks/multiboot2.h>
#include <ntclks/power.h>

#include "port.h"

static void io_delay(void)
{
    for (volatile uint32_t i = 0; i < 100000; ++i) {
        __asm__ volatile("pause");
    }
}

struct __attribute__((packed)) acpi_rsdp {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
};

struct __attribute__((packed)) acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
};

struct acpi_power_state {
    uint16_t pm1a_control;
    uint16_t pm1b_control;
    uint16_t sleep_type_a;
    uint16_t sleep_type_b;
    uint32_t smi_command;
    uint8_t acpi_enable;
    uint8_t pm1_control_length;
    uint8_t available;
};

static struct acpi_power_state acpi_power;

static uint32_t acpi_read32(const uint8_t *base, uint32_t offset)
{
    uint32_t value = 0;
    if (!base) {
        return 0;
    }
    for (uint32_t i = 0; i < 4; ++i) {
        value |= (uint32_t)base[offset + i] << (i * 8U);
    }
    return value;
}

static uint64_t acpi_read64(const uint8_t *base, uint32_t offset)
{
    uint64_t value = 0;
    if (!base) {
        return 0;
    }
    for (uint32_t i = 0; i < 8; ++i) {
        value |= (uint64_t)base[offset + i] << (i * 8U);
    }
    return value;
}

static int acpi_guid_equal(const uint8_t *a, const uint8_t *b)
{
    if (!a || !b) {
        return 0;
    }
    for (uint32_t i = 0; i < 16U; ++i) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static int acpi_bytes_equal(const char *a, const char *b, uint32_t length)
{
    if (!a || !b) {
        return 0;
    }
    for (uint32_t i = 0; i < length; ++i) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static int acpi_checksum_valid(const uint8_t *table, uint32_t length)
{
    uint8_t checksum = 0;
    if (!table || !length || length > 1024U * 1024U) {
        return 0;
    }
    for (uint32_t i = 0; i < length; ++i) {
        checksum = (uint8_t)(checksum + table[i]);
    }
    return checksum == 0;
}

static int acpi_rsdp_valid(const struct acpi_rsdp *rsdp)
{
    uint8_t checksum = 0;
    if (!rsdp || !acpi_bytes_equal(rsdp->signature, "RSD PTR ", 8)) {
        return 0;
    }
    for (uint32_t i = 0; i < 20U; ++i) {
        checksum = (uint8_t)(checksum + ((const uint8_t *)rsdp)[i]);
    }
    if (checksum != 0) {
        return 0;
    }
    if (rsdp->revision >= 2 &&
        (rsdp->length < sizeof(*rsdp) || rsdp->length > 4096U ||
         !acpi_checksum_valid((const uint8_t *)rsdp, rsdp->length))) {
        return 0;
    }
    return 1;
}

static const struct acpi_rsdp *acpi_scan_rsdp_range(uint32_t start,
                                                     uint32_t end)
{
    for (uint32_t address = (start + 15U) & ~15U;
         address + sizeof(struct acpi_rsdp) <= end;
         address += 16U) {
        const struct acpi_rsdp *rsdp =
            (const struct acpi_rsdp *)(uintptr_t)address;
        if (acpi_rsdp_valid(rsdp)) {
            return rsdp;
        }
    }
    return 0;
}

static const struct acpi_rsdp *acpi_scan_rsdp(void)
{
    const struct acpi_rsdp *rsdp;
    uint16_t ebda_segment = *(const volatile uint16_t *)(uintptr_t)0x40e;
    uint32_t ebda = (uint32_t)ebda_segment << 4;
    if (ebda >= 0x400U && ebda < 0xa0000U) {
        uint32_t ebda_end = ebda + 1024U;
        if (ebda_end > 0xa0000U) {
            ebda_end = 0xa0000U;
        }
        rsdp = acpi_scan_rsdp_range(ebda, ebda_end);
        if (rsdp) {
            return rsdp;
        }
    }
    return acpi_scan_rsdp_range(0xe0000U, 0x100000U);
}

static const struct acpi_rsdp *acpi_rsdp_from_efi(uint64_t system_table)
{
    static const uint8_t acpi_guids[][16] = {
        /* EFI_ACPI_20_TABLE_GUID and EFI_ACPI_TABLE_GUID. */
        {0x71, 0xe8, 0x68, 0x88, 0xf1, 0xe4, 0xd3, 0x11,
         0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81},
        {0x30, 0x2d, 0x9d, 0xeb, 0x88, 0x2d, 0xd3, 0x11,
         0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d},
    };
    uint64_t entry_count;
    uint64_t config_table;
    if (!system_table || system_table > 0xffffffffULL) {
        return 0;
    }
    /* EFI_SYSTEM_TABLE.ConfigurationTable fields are at offsets 104/112. */
    entry_count = acpi_read64((const uint8_t *)(uintptr_t)system_table, 104);
    config_table = acpi_read64((const uint8_t *)(uintptr_t)system_table, 112);
    if (!entry_count || entry_count > 1024U || !config_table ||
        config_table > 0xffffffffULL ||
        config_table + entry_count * 24U > 0x100000000ULL) {
        return 0;
    }
    for (uint64_t i = 0; i < entry_count; ++i) {
        const uint8_t *entry = (const uint8_t *)(uintptr_t)(config_table + i * 24U);
        uint64_t table_address = acpi_read64(entry, 16);
        for (uint32_t guid = 0; guid < sizeof(acpi_guids) / sizeof(acpi_guids[0]); ++guid) {
            if (acpi_guid_equal(entry, acpi_guids[guid]) &&
                table_address <= 0xffffffffULL) {
                const struct acpi_rsdp *rsdp =
                    (const struct acpi_rsdp *)(uintptr_t)table_address;
                if (acpi_rsdp_valid(rsdp)) {
                    return rsdp;
                }
            }
        }
    }
    return 0;
}

static const struct acpi_sdt_header *acpi_find_table(const struct acpi_rsdp *rsdp,
                                                      const char signature[4])
{
    if (!rsdp || !signature) {
        return 0;
    }
    for (uint32_t root_index = 0; root_index < 2U; ++root_index) {
        const uint8_t *root;
        uint64_t root_address;
        uint32_t root_length;
        uint32_t entry_size;
        uint32_t entry_count;
        if (root_index == 0U) {
            if (rsdp->revision < 2 || !rsdp->xsdt_address) {
                continue;
            }
            root_address = rsdp->xsdt_address;
            entry_size = 8;
        } else {
            if (!rsdp->rsdt_address) {
                continue;
            }
            root_address = rsdp->rsdt_address;
            entry_size = 4;
        }
        if (root_address > 0xffffffffULL) {
            continue;
        }
        root = (const uint8_t *)(uintptr_t)root_address;
        root_length = ((const struct acpi_sdt_header *)root)->length;
        if (root_length < sizeof(struct acpi_sdt_header) ||
            !acpi_checksum_valid(root, root_length)) {
            continue;
        }
        entry_count = (root_length - sizeof(struct acpi_sdt_header)) / entry_size;
        for (uint32_t i = 0; i < entry_count; ++i) {
            uint64_t address = entry_size == 8
                                   ? acpi_read64(root, sizeof(struct acpi_sdt_header) + i * 8U)
                                   : acpi_read32(root, sizeof(struct acpi_sdt_header) + i * 4U);
            const struct acpi_sdt_header *table;
            if (!address || address > 0xffffffffULL) {
                continue;
            }
            table = (const struct acpi_sdt_header *)(uintptr_t)address;
            if (acpi_bytes_equal(table->signature, signature, 4) &&
                table->length >= sizeof(struct acpi_sdt_header) &&
                acpi_checksum_valid((const uint8_t *)table, table->length)) {
                return table;
            }
        }
    }
    return 0;
}

static uint32_t acpi_pkg_length(const uint8_t *aml, uint32_t length,
                                uint32_t *consumed)
{
    uint8_t lead;
    uint32_t follow;
    uint32_t value;
    if (!aml || !length || !consumed) {
        return 0;
    }
    lead = aml[0];
    follow = (lead >> 6) & 3U;
    if (follow + 1U > length) {
        return 0;
    }
    /* A one-byte PkgLength uses six payload bits; longer encodings use
     * the low nibble and the following bytes. */
    value = follow ? (lead & 0x0fU) : (lead & 0x3fU);
    for (uint32_t i = 0; i < follow; ++i) {
        value |= (uint32_t)aml[i + 1U] << (4U + i * 8U);
    }
    *consumed = follow + 1U;
    return value;
}

static int acpi_aml_integer(const uint8_t *aml, uint32_t length,
                            uint32_t *consumed, uint16_t *value)
{
    uint32_t result;
    if (!aml || !length || !consumed || !value) {
        return 0;
    }
    switch (aml[0]) {
    case 0x00: /* ZeroOp */
        *consumed = 1;
        *value = 0;
        return 1;
    case 0x01: /* OneOp */
        *consumed = 1;
        *value = 1;
        return 1;
    case 0x0a: /* BytePrefix */
        if (length < 2) {
            return 0;
        }
        *consumed = 2;
        *value = aml[1];
        return 1;
    case 0x0b: /* WordPrefix */
        if (length < 3) {
            return 0;
        }
        *consumed = 3;
        *value = (uint16_t)(aml[1] | ((uint16_t)aml[2] << 8));
        return 1;
    case 0x0c: /* DWordPrefix */
        if (length < 5) {
            return 0;
        }
        result = acpi_read32(aml, 1);
        *consumed = 5;
        *value = (uint16_t)result;
        return 1;
    default:
        return 0;
    }
}

static int acpi_find_sleep_types(const struct acpi_sdt_header *dsdt,
                                 uint16_t *sleep_type_a, uint16_t *sleep_type_b)
{
    const uint8_t *aml;
    uint32_t length;
    if (!dsdt || dsdt->length < sizeof(struct acpi_sdt_header) + 4U ||
        !sleep_type_a || !sleep_type_b) {
        return 0;
    }
    aml = (const uint8_t *)dsdt + sizeof(struct acpi_sdt_header);
    length = dsdt->length - sizeof(struct acpi_sdt_header);
    for (uint32_t i = 0; i + 4U < length; ++i) {
        uint32_t package_offset;
        uint32_t package_length;
        uint32_t package_header;
        uint32_t integer_length;
        uint16_t first;
        uint16_t second;
        if (aml[i] != '_' || aml[i + 1U] != 'S' ||
            aml[i + 2U] != '5' || aml[i + 3U] != '_') {
            continue;
        }
        package_offset = i + 4U;
        if (package_offset >= length || aml[package_offset] != 0x12) {
            continue;
        }
        ++package_offset;
        package_length = acpi_pkg_length(aml + package_offset,
                                         length - package_offset,
                                         &package_header);
        if (!package_length || package_length < package_header + 1U ||
            package_length > length - package_offset) {
            continue;
        }
        package_offset += package_header;
        {
            uint32_t package_end = (i + 5U) + package_length;
            if (package_offset >= package_end) {
                continue;
            }
            /* The first package byte is the element count. */
            if (aml[package_offset] < 2U) {
                continue;
            }
            ++package_offset;
            if (!acpi_aml_integer(aml + package_offset,
                                  package_end - package_offset,
                                  &integer_length, &first)) {
                continue;
            }
            package_offset += integer_length;
            if (!acpi_aml_integer(aml + package_offset,
                                  package_end - package_offset,
                                  &integer_length, &second)) {
                continue;
            }
        }
        if (first > 7U || second > 7U) {
            continue;
        }
        *sleep_type_a = (uint16_t)(first << 10);
        *sleep_type_b = (uint16_t)(second << 10);
        return 1;
    }
    return 0;
}

static uint16_t acpi_gas_io_port(const uint8_t *fadt, uint32_t offset)
{
    uint64_t address;
    if (!fadt) {
        return 0;
    }
    /* Generic Address Structure: address space 1 is system I/O. */
    if (fadt[offset] != 1 || fadt[offset + 1U] == 0) {
        return 0;
    }
    address = acpi_read64(fadt, offset + 4U);
    return address <= 0xffffU ? (uint16_t)address : 0;
}

void power_init(const struct boot_info *boot)
{
    const struct acpi_rsdp *rsdp = 0;
    const struct acpi_sdt_header *fadt;
    const struct acpi_sdt_header *dsdt;
    const uint8_t *fadt_bytes;
    uint64_t dsdt_address;
    for (uint32_t i = 0; i < sizeof(acpi_power); ++i) {
        ((uint8_t *)&acpi_power)[i] = 0;
    }
    if (boot && boot->rsdp_addr <= 0xffffffffULL) {
        rsdp = (const struct acpi_rsdp *)(uintptr_t)boot->rsdp_addr;
        if (!acpi_rsdp_valid(rsdp)) {
            rsdp = 0;
        }
    }
    if (!rsdp && boot) {
        rsdp = acpi_rsdp_from_efi(boot->efi_system_table);
    }
    if (!rsdp) {
        rsdp = acpi_scan_rsdp();
    }
    if (!rsdp) {
        console_printf("[ntclks] ACPI RSDP unavailable\n");
        return;
    }
    fadt = acpi_find_table(rsdp, "FACP");
    if (!fadt || fadt->length < 90U) {
        console_printf("[ntclks] ACPI FADT unavailable\n");
        return;
    }
    fadt_bytes = (const uint8_t *)fadt;
    acpi_power.smi_command = acpi_read32(fadt_bytes, 48);
    acpi_power.acpi_enable = fadt_bytes[52];
    acpi_power.pm1_control_length = fadt_bytes[89];
    {
        uint32_t pm1a = acpi_read32(fadt_bytes, 64);
        uint32_t pm1b = acpi_read32(fadt_bytes, 68);
        acpi_power.pm1a_control = pm1a <= 0xffffU ? (uint16_t)pm1a : 0;
        acpi_power.pm1b_control = pm1b <= 0xffffU ? (uint16_t)pm1b : 0;
    }
    if (fadt->length >= 196U) {
        uint16_t x_pm1a = acpi_gas_io_port(fadt_bytes, 172);
        uint16_t x_pm1b = acpi_gas_io_port(fadt_bytes, 184);
        if (x_pm1a) {
            acpi_power.pm1a_control = x_pm1a;
        }
        if (x_pm1b) {
            acpi_power.pm1b_control = x_pm1b;
        }
    }
    dsdt_address = acpi_read32(fadt_bytes, 40);
    if (fadt->length >= 148U) {
        uint64_t extended_dsdt = acpi_read64(fadt_bytes, 140);
        if (extended_dsdt) {
            dsdt_address = extended_dsdt;
        }
    }
    if (!dsdt_address || dsdt_address > 0xffffffffULL) {
        console_printf("[ntclks] ACPI DSDT unavailable\n");
        return;
    }
    dsdt = (const struct acpi_sdt_header *)(uintptr_t)dsdt_address;
    if (!acpi_bytes_equal(dsdt->signature, "DSDT", 4) ||
        dsdt->length < sizeof(struct acpi_sdt_header) ||
        !acpi_checksum_valid((const uint8_t *)dsdt, dsdt->length) ||
        !acpi_find_sleep_types(dsdt, &acpi_power.sleep_type_a,
                               &acpi_power.sleep_type_b)) {
        console_printf("[ntclks] ACPI _S5_ sleep object unavailable\n");
        return;
    }
    if (!acpi_power.pm1a_control || acpi_power.pm1_control_length < 2U) {
        console_printf("[ntclks] ACPI PM1 control block unavailable\n");
        return;
    }
    acpi_power.available = 1;
    console_printf("[ntclks] ACPI S5 shutdown ready pm1a=0x%x pm1b=0x%x\n",
                   acpi_power.pm1a_control, acpi_power.pm1b_control);
}

static int acpi_enable_power_management(void)
{
    if (!acpi_power.available) {
        return 0;
    }
    if (!(x86_64_inw(acpi_power.pm1a_control) & 1U) &&
        acpi_power.smi_command <= 0xffffU && acpi_power.acpi_enable) {
        x86_64_outb(acpi_power.acpi_enable,
                    (uint16_t)acpi_power.smi_command);
        for (uint32_t i = 0; i < 1000000U; ++i) {
            if (x86_64_inw(acpi_power.pm1a_control) & 1U) {
                break;
            }
            __asm__ volatile("pause");
        }
    }
    return (x86_64_inw(acpi_power.pm1a_control) & 1U) != 0;
}

void power_reboot(void)
{
    console_printf("[ntclks] reboot requested\n");
    for (;;) {
        while (x86_64_inb(0x64) & 0x02) {
        }
        x86_64_outb(0xfe, 0x64);
        io_delay();
    }
}

void power_shutdown(void)
{
    console_printf("[ntclks] shutdown requested\n");
    if (acpi_enable_power_management()) {
        uint16_t sleep_enable = 1U << 13;
        uint16_t pm1a = x86_64_inw(acpi_power.pm1a_control);
        pm1a = (uint16_t)((pm1a & ~(uint16_t)(7U << 10)) |
                          acpi_power.sleep_type_a | sleep_enable);
        x86_64_outw(pm1a, acpi_power.pm1a_control);
        if (acpi_power.pm1b_control) {
            uint16_t pm1b = x86_64_inw(acpi_power.pm1b_control);
            pm1b = (uint16_t)((pm1b & ~(uint16_t)(7U << 10)) |
                              acpi_power.sleep_type_b | sleep_enable);
            x86_64_outw(pm1b, acpi_power.pm1b_control);
        }
        io_delay();
    }
    /* QEMU, Bochs and VirtualBox expose these legacy power ports. */
    x86_64_outw(0x2000, 0x604);
    io_delay();
    x86_64_outw(0x3400, 0xb004);
    io_delay();
    x86_64_outw(0x2000, 0x4004);
    io_delay();
    x86_64_outl(0x2000, 0x604);
    x86_64_halt();
}
