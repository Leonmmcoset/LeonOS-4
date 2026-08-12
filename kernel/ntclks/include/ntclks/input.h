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

void input_init(void);
void input_push_mouse(int32_t x, int32_t y, int32_t dx, int32_t dy, uint8_t buttons);
void input_push_mouse_wheel(int32_t x, int32_t y, int32_t wheel, uint8_t buttons);
void input_push_key(uint8_t keycode, uint8_t pressed);
int input_pop(struct input_event *event);

#endif
