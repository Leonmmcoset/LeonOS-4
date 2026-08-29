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

/**
 * @brief Read a 32-bit config-space register at offset from the PCI function.
 */
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function,
                           uint8_t offset);
/**
 * @brief Write a 32-bit value to the PCI function's config-space register at offset.
 */
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function,
                        uint8_t offset, uint32_t value);
/**
 * @brief Read a 16-bit config-space register at offset from the PCI function.
 */
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t function,
                           uint8_t offset);
/**
 * @brief Write a 16-bit value to the PCI function's config-space register at offset.
 */
void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t function,
                        uint8_t offset, uint16_t value);
/**
 * @brief Fill out with the vendor/device/class identity at bus:slot:function; 0 if present.
 */
int pci_read_device(uint8_t bus, uint8_t slot, uint8_t function,
                    struct pci_device *out);
/**
 * @brief Scan PCI for the first device matching vendor_id/device_id; 0 if found.
 */
int pci_find_device(uint16_t vendor_id, uint16_t device_id,
                    struct pci_device *out);

#endif
