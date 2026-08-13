/*
 * LeonOS kernel input queue: stores normalized keyboard and pointer events.
 * Exposes bounded producer/consumer operations to interrupt and user paths.
 */
#include <ntclks/input.h>

#define INPUT_QUEUE_CAP 512

static struct input_event queue[INPUT_QUEUE_CAP];
static volatile uint32_t head;
static volatile uint32_t tail;

/**
 * @brief Coordinates the input init operation.
 */
void input_init(void)
{
    head = 0;
    tail = 0;
}

/**
 * @brief Coordinates the push event operation.
 * @param event Input or output value used by this operation.
 */
static void push_event(const struct input_event *event)
{
    if (event && event->type == INPUT_EVENT_MOUSE && head != tail) {
        uint32_t prev = (head + INPUT_QUEUE_CAP - 1) % INPUT_QUEUE_CAP;
        if (queue[prev].type == INPUT_EVENT_MOUSE && queue[prev].buttons == event->buttons) {
            queue[prev].x = event->x;
            queue[prev].y = event->y;
            queue[prev].dx += event->dx;
            queue[prev].dy += event->dy;
            return;
        }
    }
    uint32_t next = (head + 1) % INPUT_QUEUE_CAP;
    if (next == tail) {
        tail = (tail + 1) % INPUT_QUEUE_CAP;
    }
    queue[head] = *event;
    head = next;
}

/**
 * @brief Coordinates the input push mouse operation.
 * @param x Input or output value used by this operation.
 * @param y Input or output value used by this operation.
 * @param dx Input or output value used by this operation.
 * @param dy Input or output value used by this operation.
 * @param buttons Input or output value used by this operation.
 */
void input_push_mouse(int32_t x, int32_t y, int32_t dx, int32_t dy, uint8_t buttons)
{
    struct input_event event = {
        .type = INPUT_EVENT_MOUSE,
        .x = x,
        .y = y,
        .dx = dx,
        .dy = dy,
        .buttons = buttons,
    };
    push_event(&event);
}

/**
 * @brief Coordinates the input push mouse wheel operation.
 * @param x Input or output value used by this operation.
 * @param y Input or output value used by this operation.
 * @param wheel Input or output value used by this operation.
 * @param buttons Input or output value used by this operation.
 */
void input_push_mouse_wheel(int32_t x, int32_t y, int32_t wheel, uint8_t buttons)
{
    struct input_event event = {
        .type = INPUT_EVENT_MOUSE_WHEEL,
        .x = x,
        .y = y,
        .dy = wheel,
        .buttons = buttons,
    };
    push_event(&event);
}

/**
 * @brief Coordinates the input push key operation.
 * @param keycode Input or output value used by this operation.
 * @param pressed Input or output value used by this operation.
 */
void input_push_key(uint8_t keycode, uint8_t pressed)
{
    struct input_event event = {
        .type = INPUT_EVENT_KEYBOARD,
        .keycode = keycode,
        .pressed = pressed,
    };
    push_event(&event);
}

/**
 * @brief Coordinates the input pop operation.
 * @param event Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int input_pop(struct input_event *event)
{
    if (!event || tail == head) {
        return 0;
    }
    *event = queue[tail];
    tail = (tail + 1) % INPUT_QUEUE_CAP;
    return 1;
}
