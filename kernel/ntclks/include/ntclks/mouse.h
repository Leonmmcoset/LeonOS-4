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

/**
 * @brief Initialize the mouse device and its shared state.
 */
void mouse_init(void);
/**
 * @brief Read pending PS/2 mouse bytes and update position/buttons/wheel.
 */
void mouse_poll(void);
const struct mouse_state *mouse_get_state(void);
/**
 * @brief Show or hide the mouse cursor.
 */
void mouse_set_visible(bool visible);
/**
 * @brief Return true when the mouse cursor is visible.
 */
bool mouse_is_visible(void);
/**
 * @brief Return how many mouse events are waiting in the queue.
 */
uint32_t mouse_event_count(void);
/**
 * @brief Return the last PS/2 mouse status byte received.
 */
uint8_t mouse_last_status(void);
/**
 * @brief Return the last PS/2 mouse data byte received.
 */
uint8_t mouse_last_data(void);
/**
 * @brief Return the last PS/2 mouse acknowledge byte received.
 */
uint8_t mouse_last_ack(void);

#endif
