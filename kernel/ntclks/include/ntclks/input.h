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
 * @brief Initialize the input event queue.
 */
void input_init(void);
/**
 * @brief Enqueue a mouse move/drag: absolute position (x,y), relative delta (dx,dy), button mask.
 */
void input_push_mouse(int32_t x, int32_t y, int32_t dx, int32_t dy, uint8_t buttons);
/**
 * @brief Enqueue a mouse wheel event: position (x,y), scroll amount wheel, button mask.
 */
void input_push_mouse_wheel(int32_t x, int32_t y, int32_t wheel, uint8_t buttons);
/**
 * @brief Enqueue a keyboard event: keycode is the key, pressed is 1 for down / 0 for up.
 */
void input_push_key(uint8_t keycode, uint8_t pressed);
/**
 * @brief Dequeue the oldest event into event; returns non-zero when one was available.
 */
int input_pop(struct input_event *event);

#endif
