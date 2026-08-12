/*
 * LeonOS mouse interface: declares mouse-device state and event helpers.
 * Provides normalized pointer movement, buttons, and wheel data.
 */
#ifndef NTCLKS_MOUSE_H
#define NTCLKS_MOUSE_H

#include <ntclks/types.h>

struct mouse_state {
    int32_t x;
    int32_t y;
    uint8_t buttons;
    bool present;
    bool absolute;
};

void mouse_init(void);
void mouse_poll(void);
const struct mouse_state *mouse_get_state(void);
void mouse_set_visible(bool visible);
bool mouse_is_visible(void);
uint32_t mouse_event_count(void);
uint8_t mouse_last_status(void);
uint8_t mouse_last_data(void);
uint8_t mouse_last_ack(void);

#endif
