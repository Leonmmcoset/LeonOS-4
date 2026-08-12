/*
 * LeonOS x86_64 PCI access: enumerates and configures PCI devices.
 * Provides low-level configuration-space reads and writes for drivers.
 */
#include <ntclks/pci.h>

#include "port.h"

#define PCI_CONFIG_ADDR 0xcf8u
#define PCI_CONFIG_DATA 0xcfcu

static uint32_t pci_make_addr(uint8_t bus, uint8_t slot, uint8_t function,
                              uint8_t offset)
{
    return 0x80000000u |
           ((uint32_t)bus << 16) |
           ((uint32_t)slot << 11) |
           ((uint32_t)function << 8) |
           ((uint32_t)offset & 0xfcu);
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function,
                           uint8_t offset)
{
    x86_64_outl(pci_make_addr(bus, slot, function, offset), PCI_CONFIG_ADDR);
    return x86_64_inl(PCI_CONFIG_DATA);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function,
                        uint8_t offset, uint32_t value)
{
    x86_64_outl(pci_make_addr(bus, slot, function, offset), PCI_CONFIG_ADDR);
    x86_64_outl(value, PCI_CONFIG_DATA);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t function,
                           uint8_t offset)
{
    uint32_t value = pci_config_read32(bus, slot, function, offset);
    return (uint16_t)(value >> ((offset & 2u) * 8u));
}

void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t function,
                        uint8_t offset, uint16_t value)
{
    uint8_t aligned = offset & 0xfcu;
    uint32_t shift = (uint32_t)(offset & 2u) * 8u;
    uint32_t current = pci_config_read32(bus, slot, function, aligned);
    current &= ~(0xffffu << shift);
    current |= (uint32_t)value << shift;
    pci_config_write32(bus, slot, function, aligned, current);
}

int pci_read_device(uint8_t bus, uint8_t slot, uint8_t function,
                    struct pci_device *out)
{
    uint32_t id = pci_config_read32(bus, slot, function, 0x00);
    uint32_t class_reg;
    if ((id & 0xffffu) == 0xffffu) {
        return -1;
    }
    if (!out) {
        return 0;
    }
    class_reg = pci_config_read32(bus, slot, function, 0x08);
    *out = (struct pci_device){
        .bus = bus,
        .slot = slot,
        .function = function,
        .vendor_id = (uint16_t)(id & 0xffffu),
        .device_id = (uint16_t)(id >> 16),
        .class_code = (uint8_t)(class_reg >> 24),
        .subclass = (uint8_t)(class_reg >> 16),
        .prog_if = (uint8_t)(class_reg >> 8),
        .revision = (uint8_t)class_reg,
    };
    return 0;
}

int pci_find_device(uint16_t vendor_id, uint16_t device_id,
                    struct pci_device *out)
{
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t function = 0; function < 8; ++function) {
                struct pci_device dev;
                if (pci_read_device((uint8_t)bus, slot, function, &dev) < 0) {
                    if (function == 0) {
                        break;
                    }
                    continue;
                }
                if (dev.vendor_id == vendor_id && dev.device_id == device_id) {
                    if (out) {
                        *out = dev;
                    }
                    return 0;
                }
            }
        }
    }
    return -1;
}
