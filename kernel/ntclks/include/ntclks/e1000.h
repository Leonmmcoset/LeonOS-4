/*
 * LeonOS e1000 driver interface: declares Intel e1000 network operations.
 * Connects the PCI-backed NIC driver to the kernel networking layer.
 */
#ifndef NTCLKS_E1000_H
#define NTCLKS_E1000_H

#include <ntclks/types.h>

struct e1000_info {
    uint32_t present;
    uint32_t active;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t reserved;
    uint8_t mac[6];
};

/**
 * @brief Probe and initialize the Intel e1000 NIC, if one is present.
 */
void e1000_init(void);
/**
 * @brief Return non-zero when the e1000 driver is present and usable.
 */
int e1000_is_ready(void);
const uint8_t *e1000_mac(void);
/**
 * @brief Transmit len bytes of frame over the NIC; returns 0, or -ENODEV when absent.
 */
int e1000_send(const void *frame, uint32_t len);
/**
 * @brief Receive one packet into frame (up to capacity bytes), writing its size to out_len.
 */
int e1000_poll(void *frame, uint32_t capacity, uint32_t *out_len);
/**
 * @brief Fill info with the NIC's presence, PCI location, and MAC address.
 */
void e1000_get_info(struct e1000_info *info);

#endif
