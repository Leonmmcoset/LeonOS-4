/*
 * LeonOS kernel input queue: stores normalized keyboard and pointer events.
 * Exposes bounded producer/consumer operations to interrupt and user paths.
 */
#include <ntclks/input.h>
#include <ntclks/lock.h>

#define INPUT_QUEUE_CAP 512

static struct input_event queue[INPUT_QUEUE_CAP];
static volatile uint32_t head;
static volatile uint32_t tail;
static struct kernel_spinlock input_lock = KERNEL_SPINLOCK_INIT;

/**
 * @brief Reset the keyboard/pointer event queue to empty.
 */
void input_init(void)
{
    uint64_t flags;
    kernel_spin_lock_irqsave(&input_lock, &flags);
    head = 0;
    tail = 0;
    kernel_spin_unlock_irqrestore(&input_lock, flags);
}

/**
 * @brief Append event to the ring, coalescing consecutive mouse moves with unchanged buttons; drops the oldest event when full.
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
 * @brief Queue an absolute mouse move/drag event.
 */
void input_push_mouse(int32_t x, int32_t y, int32_t dx, int32_t dy, uint8_t buttons)
{
    uint64_t flags;
    struct input_event event = {
        .type = INPUT_EVENT_MOUSE,
        .x = x,
        .y = y,
        .dx = dx,
        .dy = dy,
        .buttons = buttons,
    };
    kernel_spin_lock_irqsave(&input_lock, &flags);
    push_event(&event);
    kernel_spin_unlock_irqrestore(&input_lock, flags);
}

/**
 * @brief Queue a mouse wheel event (delta carried in dy).
 */
void input_push_mouse_wheel(int32_t x, int32_t y, int32_t wheel, uint8_t buttons)
{
    uint64_t flags;
    struct input_event event = {
        .type = INPUT_EVENT_MOUSE_WHEEL,
        .x = x,
        .y = y,
        .dy = wheel,
        .buttons = buttons,
    };
    kernel_spin_lock_irqsave(&input_lock, &flags);
    push_event(&event);
    kernel_spin_unlock_irqrestore(&input_lock, flags);
}

/**
 * @brief Queue a keyboard press/release event.
 */
void input_push_key(uint8_t keycode, uint8_t pressed)
{
    uint64_t flags;
    struct input_event event = {
        .type = INPUT_EVENT_KEYBOARD,
        .keycode = keycode,
        .pressed = pressed,
    };
    kernel_spin_lock_irqsave(&input_lock, &flags);
    push_event(&event);
    kernel_spin_unlock_irqrestore(&input_lock, flags);
}

/**
 * @brief Remove the oldest queued event into *event; returns 1 on success, 0 when empty or null.
 */
int input_pop(struct input_event *event)
{
    uint64_t flags;
    int available = 0;
    if (!event) {
        return 0;
    }
    kernel_spin_lock_irqsave(&input_lock, &flags);
    if (tail != head) {
        *event = queue[tail];
        tail = (tail + 1) % INPUT_QUEUE_CAP;
        available = 1;
    }
    kernel_spin_unlock_irqrestore(&input_lock, flags);
    return available;
}
