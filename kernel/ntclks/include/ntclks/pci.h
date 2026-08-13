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
 * @brief Coordinates the pci config read32 operation.
 * @param bus Input or output value used by this operation.
 * @param slot Input or output value used by this operation.
 * @param function Input or output value used by this operation.
 * @param offset Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function,
                           uint8_t offset);
/**
 * @brief Coordinates the pci config write32 operation.
 * @param bus Input or output value used by this operation.
 * @param slot Input or output value used by this operation.
 * @param function Input or output value used by this operation.
 * @param offset Input or output value used by this operation.
 * @param value Input or output value used by this operation.
 */
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function,
                        uint8_t offset, uint32_t value);
/**
 * @brief Coordinates the pci config read16 operation.
 * @param bus Input or output value used by this operation.
 * @param slot Input or output value used by this operation.
 * @param function Input or output value used by this operation.
 * @param offset Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t function,
                           uint8_t offset);
/**
 * @brief Coordinates the pci config write16 operation.
 * @param bus Input or output value used by this operation.
 * @param slot Input or output value used by this operation.
 * @param function Input or output value used by this operation.
 * @param offset Input or output value used by this operation.
 * @param value Input or output value used by this operation.
 */
void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t function,
                        uint8_t offset, uint16_t value);
/**
 * @brief Coordinates the pci read device operation.
 * @param bus Input or output value used by this operation.
 * @param slot Input or output value used by this operation.
 * @param function Input or output value used by this operation.
 * @param out Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
int pci_read_device(uint8_t bus, uint8_t slot, uint8_t function,
                    struct pci_device *out);
/**
 * @brief Coordinates the pci find device operation.
 * @param vendor_id Input or output value used by this operation.
 * @param device_id Input or output value used by this operation.
 * @param out Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
int pci_find_device(uint16_t vendor_id, uint16_t device_id,
                    struct pci_device *out);

#endif
