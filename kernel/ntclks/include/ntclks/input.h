/*
 * LeonOS input queue interface: declares normalized input event operations.
 * Used by interrupt handlers, drivers, and the input manager.
 */
#ifndef NTCLKS_INPUT_H
#define NTCLKS_INPUT_H

#include <ntclks/types.h>

#define INPUT_EVENT_MOUSE 1
#define INPUT_EVENT_KEYBOARD 2
#define INPUT_EVENT_MOUSE_WHEEL 3

struct input_event {
    uint32_t type;
    int32_t x;
    int32_t y;
    int32_t dx;
    int32_t dy;
    uint8_t buttons;
    uint8_t keycode;
    uint8_t pressed;
};

/**
 * @brief Coordinates the input init operation.
 */
void input_init(void);
/**
 * @brief Coordinates the input push mouse operation.
 * @param x Input or output value used by this operation.
 * @param y Input or output value used by this operation.
 * @param dx Input or output value used by this operation.
 * @param dy Input or output value used by this operation.
 * @param buttons Input or output value used by this operation.
 */
void input_push_mouse(int32_t x, int32_t y, int32_t dx, int32_t dy, uint8_t buttons);
/**
 * @brief Coordinates the input push mouse wheel operation.
 * @param x Input or output value used by this operation.
 * @param y Input or output value used by this operation.
 * @param wheel Input or output value used by this operation.
 * @param buttons Input or output value used by this operation.
 */
void input_push_mouse_wheel(int32_t x, int32_t y, int32_t wheel, uint8_t buttons);
/**
 * @brief Coordinates the input push key operation.
 * @param keycode Input or output value used by this operation.
 * @param pressed Input or output value used by this operation.
 */
void input_push_key(uint8_t keycode, uint8_t pressed);
/**
 * @brief Coordinates the input pop operation.
 * @param event Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int input_pop(struct input_event *event);

#endif
