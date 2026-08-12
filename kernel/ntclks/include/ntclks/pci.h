/*
 * LeonOS PCI interface: declares architecture-neutral PCI configuration APIs.
 * Used by device discovery and PCI-backed kernel drivers.
 */
#ifndef NTCLKS_PCI_H
#define NTCLKS_PCI_H

#include <ntclks/types.h>

struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
};

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function,
                           uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function,
                        uint8_t offset, uint32_t value);
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t function,
                           uint8_t offset);
void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t function,
                        uint8_t offset, uint16_t value);
int pci_read_device(uint8_t bus, uint8_t slot, uint8_t function,
                    struct pci_device *out);
int pci_find_device(uint16_t vendor_id, uint16_t device_id,
                    struct pci_device *out);

#endif
