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
 * @brief Coordinates the mouse init operation.
 */
void mouse_init(void);
/**
 * @brief Coordinates the mouse poll operation.
 */
void mouse_poll(void);
const struct mouse_state *mouse_get_state(void);
/**
 * @brief Coordinates the mouse set visible operation.
 * @param visible Input or output value used by this operation.
 */
void mouse_set_visible(bool visible);
/**
 * @brief Coordinates the mouse is visible operation.
 * @return Result, status, or value defined by this API.
 */
bool mouse_is_visible(void);
/**
 * @brief Coordinates the mouse event count operation.
 * @return Result, status, or value defined by this API.
 */
uint32_t mouse_event_count(void);
/**
 * @brief Coordinates the mouse last status operation.
 * @return Result, status, or value defined by this API.
 */
uint8_t mouse_last_status(void);
/**
 * @brief Coordinates the mouse last data operation.
 * @return Result, status, or value defined by this API.
 */
uint8_t mouse_last_data(void);
/**
 * @brief Coordinates the mouse last ack operation.
 * @return Result, status, or value defined by this API.
 */
uint8_t mouse_last_ack(void);

#endif
