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
 * @brief Coordinates the e1000 init operation.
 */
void e1000_init(void);
/**
 * @brief Coordinates the e1000 is ready operation.
 * @return Result, status, or value defined by this API.
 */
int e1000_is_ready(void);
const uint8_t *e1000_mac(void);
/**
 * @brief Coordinates the e1000 send operation.
 * @param frame Trap or syscall frame supplied by the architecture layer.
 * @param len Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
 */
int e1000_send(const void *frame, uint32_t len);
/**
 * @brief Coordinates the e1000 poll operation.
 * @param frame Trap or syscall frame supplied by the architecture layer.
 * @param capacity Capacity, in elements or bytes, of the related output buffer.
 * @param out_len Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
int e1000_poll(void *frame, uint32_t capacity, uint32_t *out_len);
/**
 * @brief Coordinates the e1000 get info operation.
 * @param info Input or output value used by this operation.
 */
void e1000_get_info(struct e1000_info *info);

#endif
