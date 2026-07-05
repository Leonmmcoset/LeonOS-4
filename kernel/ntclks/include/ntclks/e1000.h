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

void e1000_init(void);
int e1000_is_ready(void);
const uint8_t *e1000_mac(void);
int e1000_send(const void *frame, uint32_t len);
int e1000_poll(void *frame, uint32_t capacity, uint32_t *out_len);
void e1000_get_info(struct e1000_info *info);

#endif
