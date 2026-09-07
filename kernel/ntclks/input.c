/*
 * Kernel input fan-out: the desktop consumes normalized raw events while
 * /dev/input/event* exposes independent Linux evdev streams.
 */
#include <ntclks/input.h>
#include <ntclks/framebuffer.h>
#include <ntclks/lock.h>
#include <ntclks/storage.h>
#include <ntclks/time.h>
#include <linux/input.h>

#define INPUT_QUEUE_CAP 512U
#define INPUT_EVDEV_QUEUE_CAP 1024U
#define INPUT_EVDEV_DEVICES 2U
#define INPUT_EVDEV_KEY_BYTES ((KEY_CNT + 7U) / 8U)

struct input_evdev_record {
    uint64_t sequence;
    uint32_t device_kind;
    struct input_event event;
};

static struct input_raw_event queue[INPUT_QUEUE_CAP];
static struct input_evdev_record evdev_queue[INPUT_EVDEV_QUEUE_CAP];
static volatile uint32_t head;
static volatile uint32_t tail;
static uint64_t evdev_next_sequence;
static uint8_t evdev_mouse_buttons;
static int32_t evdev_mouse_x;
static int32_t evdev_mouse_y;
static uint8_t evdev_key_state[INPUT_EVDEV_KEY_BYTES];
static uint8_t evdev_present[INPUT_EVDEV_DEVICES];
static uint32_t evdev_grab_owner[INPUT_EVDEV_DEVICES];
static uint64_t evdev_grab_token[INPUT_EVDEV_DEVICES];
static uint64_t evdev_next_grab_token = 1;
static struct kernel_spinlock input_lock = KERNEL_SPINLOCK_INIT;

static uint64_t evdev_oldest_sequence(void)
{
    return evdev_next_sequence > INPUT_EVDEV_QUEUE_CAP
               ? evdev_next_sequence - INPUT_EVDEV_QUEUE_CAP
               : 1U;
}

static uint32_t evdev_device_index(uint32_t device_kind)
{
    if (device_kind == STORAGE_DEV_KIND_KEYBOARD) return 0;
    if (device_kind == STORAGE_DEV_KIND_MOUSE) return 1;
    return INPUT_EVDEV_DEVICES;
}

static void evdev_set_key(uint16_t code, int32_t value)
{
    if (code < KEY_CNT && value != 0) {
        evdev_key_state[code / 8U] |= (uint8_t)(1U << (code % 8U));
    } else if (code < KEY_CNT) {
        evdev_key_state[code / 8U] &= (uint8_t)~(1U << (code % 8U));
    }
}

static void evdev_publish(uint32_t device_kind, uint16_t type, uint16_t code,
                          int32_t value)
{
    struct input_evdev_record *record;
    uint64_t microseconds = time_uptime_us();

    record = &evdev_queue[evdev_next_sequence % INPUT_EVDEV_QUEUE_CAP];
    *record = (struct input_evdev_record){
        .sequence = evdev_next_sequence,
        .device_kind = device_kind,
        .event = {
            .time_sec = (int64_t)(microseconds / 1000000ULL),
            .time_usec = (int64_t)(microseconds % 1000000ULL),
            .type = type,
            .code = code,
            .value = value,
        },
    };
    if (type == EV_KEY) {
        evdev_set_key(code, value);
    }
    ++evdev_next_sequence;
}

static uint16_t evdev_keycode(uint8_t keycode)
{
    /* The legacy GUI uses set-1 scan codes for cursor keys and extended
     * modifiers. Convert those exceptional values to Linux input codes; the
     * ordinary PC set-1 keyboard range already matches Linux key codes. */
    switch (keycode) {
    case 71: return KEY_HOME;
    case 72: return KEY_UP;
    case 73: return KEY_PAGEUP;
    case 75: return KEY_LEFT;
    case 77: return KEY_RIGHT;
    case 79: return KEY_END;
    case 80: return KEY_DOWN;
    case 81: return KEY_PAGEDOWN;
    case 82: return KEY_INSERT;
    case 83: return KEY_DELETE;
    case 112: return KEY_LEFTMETA;
    case 113: return KEY_RIGHTMETA;
    case 114: return KEY_COMPOSE;
    case 115: return KEY_RIGHTALT;
    case 116: return KEY_RIGHTCTRL;
    default: return keycode;
    }
}

static uint8_t evdev_publish_mouse_buttons(uint8_t buttons)
{
    static const uint16_t codes[] = {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE};
    uint8_t changed = (uint8_t)(buttons ^ evdev_mouse_buttons);
    uint8_t published = 0;
    for (uint32_t bit = 0; bit < sizeof(codes) / sizeof(codes[0]); ++bit) {
        if ((changed & (1U << bit)) != 0) {
            evdev_publish(STORAGE_DEV_KIND_MOUSE, EV_KEY, codes[bit],
                          (buttons & (1U << bit)) != 0 ? 1 : 0);
            published = 1;
        }
    }
    evdev_mouse_buttons = buttons;
    return published;
}

/* Caller holds input_lock. */
static void push_event(const struct input_raw_event *event)
{
    if (event && event->type == INPUT_EVENT_MOUSE && head != tail) {
        uint32_t prev = (head + INPUT_QUEUE_CAP - 1U) % INPUT_QUEUE_CAP;
        if (queue[prev].type == INPUT_EVENT_MOUSE && queue[prev].buttons == event->buttons) {
            queue[prev].x = event->x;
            queue[prev].y = event->y;
            queue[prev].dx += event->dx;
            queue[prev].dy += event->dy;
            return;
        }
    }
    {
        uint32_t next = (head + 1U) % INPUT_QUEUE_CAP;
        if (next == tail) {
            tail = (tail + 1U) % INPUT_QUEUE_CAP;
        }
        queue[head] = *event;
        head = next;
    }
}

/** @brief Reset raw and evdev input streams and their device state. */
void input_init(void)
{
    uint64_t flags;
    kernel_spin_lock_irqsave(&input_lock, &flags);
    head = 0;
    tail = 0;
    evdev_next_sequence = 1;
    evdev_mouse_buttons = 0;
    evdev_mouse_x = 0;
    evdev_mouse_y = 0;
    evdev_next_grab_token = 1;
    for (uint32_t i = 0; i < INPUT_EVDEV_DEVICES; ++i) {
        evdev_present[i] = 1;
        evdev_grab_owner[i] = 0;
        evdev_grab_token[i] = 0;
    }
    for (uint32_t i = 0; i < sizeof(evdev_key_state); ++i) {
        evdev_key_state[i] = 0;
    }
    kernel_spin_unlock_irqrestore(&input_lock, flags);
}

/**
 * @brief Publish a normalized pointer packet, preserving its absolute position.
 * Relative axes remain available for existing evdev consumers.
 */
void input_push_mouse(int32_t x, int32_t y, int32_t dx, int32_t dy, uint8_t buttons)
{
    uint64_t flags;
    struct input_raw_event event = {
        .type = INPUT_EVENT_MOUSE,
        .x = x,
        .y = y,
        .dx = dx,
        .dy = dy,
        .buttons = buttons,
    };
    kernel_spin_lock_irqsave(&input_lock, &flags);
    push_event(&event);
    if (dx != 0) {
        evdev_publish(STORAGE_DEV_KIND_MOUSE, EV_REL, REL_X, dx);
    }
    if (dy != 0) {
        evdev_publish(STORAGE_DEV_KIND_MOUSE, EV_REL, REL_Y, dy);
    }
    evdev_mouse_x = x;
    evdev_mouse_y = y;
    evdev_publish(STORAGE_DEV_KIND_MOUSE, EV_ABS, ABS_X, x);
    evdev_publish(STORAGE_DEV_KIND_MOUSE, EV_ABS, ABS_Y, y);
    (void)evdev_publish_mouse_buttons(buttons);
    evdev_publish(STORAGE_DEV_KIND_MOUSE, EV_SYN, SYN_REPORT, 0);
    kernel_spin_unlock_irqrestore(&input_lock, flags);
}

void input_push_mouse_wheel(int32_t x, int32_t y, int32_t wheel, uint8_t buttons)
{
    uint64_t flags;
    uint8_t published = 0;
    struct input_raw_event event = {
        .type = INPUT_EVENT_MOUSE_WHEEL,
        .x = x,
        .y = y,
        .dy = wheel,
        .buttons = buttons,
    };
    kernel_spin_lock_irqsave(&input_lock, &flags);
    push_event(&event);
    if (wheel != 0) {
        evdev_publish(STORAGE_DEV_KIND_MOUSE, EV_REL, REL_WHEEL, wheel);
        published = 1;
    }
    if (evdev_publish_mouse_buttons(buttons)) {
        published = 1;
    }
    if (published) {
        evdev_publish(STORAGE_DEV_KIND_MOUSE, EV_SYN, SYN_REPORT, 0);
    }
    kernel_spin_unlock_irqrestore(&input_lock, flags);
}

void input_push_key(uint8_t keycode, uint8_t pressed)
{
    uint64_t flags;
    struct input_raw_event event = {
        .type = INPUT_EVENT_KEYBOARD,
        .keycode = keycode,
        .pressed = pressed,
    };
    kernel_spin_lock_irqsave(&input_lock, &flags);
    push_event(&event);
    evdev_publish(STORAGE_DEV_KIND_KEYBOARD, EV_KEY, evdev_keycode(keycode),
                  pressed ? 1 : 0);
    evdev_publish(STORAGE_DEV_KIND_KEYBOARD, EV_SYN, SYN_REPORT, 0);
    kernel_spin_unlock_irqrestore(&input_lock, flags);
}

int input_pop(struct input_raw_event *event)
{
    uint64_t flags;
    int available = 0;
    if (!event) {
        return 0;
    }
    kernel_spin_lock_irqsave(&input_lock, &flags);
    if (tail != head) {
        *event = queue[tail];
        tail = (tail + 1U) % INPUT_QUEUE_CAP;
        available = 1;
    }
    kernel_spin_unlock_irqrestore(&input_lock, flags);
    return available;
}

uint64_t input_evdev_cursor_now(void)
{
    uint64_t flags;
    uint64_t cursor;
    kernel_spin_lock_irqsave(&input_lock, &flags);
    cursor = evdev_next_sequence;
    kernel_spin_unlock_irqrestore(&input_lock, flags);
    return cursor;
}

int input_evdev_read(uint32_t device_kind, uint64_t *cursor,
                     void *buffer, uint32_t length, uint64_t grab_token)
{
    struct input_event *events = (struct input_event *)buffer;
    uint32_t index = evdev_device_index(device_kind);
    uint32_t capacity;
    uint32_t count = 0;
    uint64_t flags;
    if (!cursor || !buffer || length < sizeof(*events) ||
        (length % sizeof(*events)) != 0 || index >= INPUT_EVDEV_DEVICES) {
        return -22;
    }
    capacity = length / sizeof(*events);
    kernel_spin_lock_irqsave(&input_lock, &flags);
    if (!evdev_present[index]) {
        kernel_spin_unlock_irqrestore(&input_lock, flags);
        return -19; /* ENODEV */
    }
    if (*cursor == 0 || *cursor < evdev_oldest_sequence()) {
        *cursor = evdev_oldest_sequence();
    }
    while (*cursor < evdev_next_sequence && count < capacity) {
        const struct input_evdev_record *record =
            &evdev_queue[*cursor % INPUT_EVDEV_QUEUE_CAP];
        if (record->sequence != *cursor) {
            /* The producer is locked out. Rewinding on a damaged record
             * would rescan it forever with interrupts disabled. */
            ++(*cursor);
            continue;
        }
        ++(*cursor);
        if (record->device_kind != device_kind) {
            continue;
        }
        /* EVIOCGRAB is exclusive: non-owning descriptors advance over events
         * but do not observe them while another client holds the grab. */
        if (evdev_grab_token[index] == 0 ||
            (grab_token && grab_token == evdev_grab_token[index])) {
            events[count++] = record->event;
        }
    }
    kernel_spin_unlock_irqrestore(&input_lock, flags);
    return (int)(count * sizeof(*events));
}

int input_evdev_available(uint32_t device_kind, uint64_t cursor,
                          uint64_t grab_token)
{
    uint32_t index = evdev_device_index(device_kind);
    uint64_t flags;
    int available = 0;
    if (index >= INPUT_EVDEV_DEVICES) {
        return 0;
    }
    kernel_spin_lock_irqsave(&input_lock, &flags);
    if (!evdev_present[index]) {
        kernel_spin_unlock_irqrestore(&input_lock, flags);
        return 0;
    }
    if (cursor == 0 || cursor < evdev_oldest_sequence()) {
        cursor = evdev_oldest_sequence();
    }
    while (cursor < evdev_next_sequence) {
        const struct input_evdev_record *record =
            &evdev_queue[cursor % INPUT_EVDEV_QUEUE_CAP];
        if (record->sequence == cursor && record->device_kind == device_kind &&
            (evdev_grab_token[index] == 0 ||
             (grab_token && grab_token == evdev_grab_token[index]))) {
            available = 1;
            break;
        }
        ++cursor;
    }
    kernel_spin_unlock_irqrestore(&input_lock, flags);
    return available;
}

int64_t input_evdev_grab(uint32_t device_kind, uint64_t current_token,
                         int enable, uint32_t pid)
{
    uint32_t index = evdev_device_index(device_kind);
    uint64_t flags;
    if (index >= INPUT_EVDEV_DEVICES || !pid) return -22;
    kernel_spin_lock_irqsave(&input_lock, &flags);
    if (!enable) {
        if (current_token && current_token == evdev_grab_token[index] &&
            evdev_grab_owner[index] == pid) {
            evdev_grab_owner[index] = 0;
            evdev_grab_token[index] = 0;
        }
        kernel_spin_unlock_irqrestore(&input_lock, flags);
        return 0;
    }
    if (evdev_grab_token[index] != 0 &&
        evdev_grab_token[index] != current_token) {
        kernel_spin_unlock_irqrestore(&input_lock, flags);
        return -16; /* EBUSY */
    }
    if (current_token && current_token == evdev_grab_token[index] &&
        evdev_grab_owner[index] == pid) {
        kernel_spin_unlock_irqrestore(&input_lock, flags);
        return (int64_t)current_token;
    }
    if (evdev_next_grab_token == 0) evdev_next_grab_token = 1;
    evdev_grab_token[index] = evdev_next_grab_token;
    evdev_grab_owner[index] = pid;
    ++evdev_next_grab_token;
    kernel_spin_unlock_irqrestore(&input_lock, flags);
    return (int64_t)evdev_grab_token[index];
}

void input_evdev_release(uint32_t device_kind, uint64_t grab_token,
                         uint32_t pid)
{
    uint32_t index = evdev_device_index(device_kind);
    uint64_t flags;
    if (index >= INPUT_EVDEV_DEVICES || !grab_token) return;
    kernel_spin_lock_irqsave(&input_lock, &flags);
    if (evdev_grab_token[index] == grab_token &&
        evdev_grab_owner[index] == pid) {
        evdev_grab_token[index] = 0;
        evdev_grab_owner[index] = 0;
    }
    kernel_spin_unlock_irqrestore(&input_lock, flags);
}

void input_evdev_key_state(void *buffer, uint32_t length)
{
    uint64_t flags;
    uint32_t copy;
    if (!buffer || !length) return;
    copy = length < sizeof(evdev_key_state) ? length : sizeof(evdev_key_state);
    kernel_spin_lock_irqsave(&input_lock, &flags);
    for (uint32_t i = 0; i < copy; ++i) {
        ((uint8_t *)buffer)[i] = evdev_key_state[i];
    }
    kernel_spin_unlock_irqrestore(&input_lock, flags);
    for (uint32_t i = copy; i < length; ++i) {
        ((uint8_t *)buffer)[i] = 0;
    }
}

static void input_evdev_set_cap(uint8_t *bits, uint32_t capacity, uint32_t bit)
{
    if (bits && bit / 8U < capacity) {
        bits[bit / 8U] |= (uint8_t)(1U << (bit % 8U));
    }
}

/** @brief Return the event types and axes supported by the normalized stream. */
void input_evdev_capabilities(uint32_t device_kind, uint32_t event_type,
                              void *buffer, uint32_t length)
{
    uint8_t bits[INPUT_EVDEV_KEY_BYTES] = {0};
    uint64_t flags;
    uint32_t copy = length < sizeof(bits) ? length : sizeof(bits);
    if (!buffer || !length) return;
    if (event_type == 0) {
        input_evdev_set_cap(bits, sizeof(bits), EV_SYN);
        input_evdev_set_cap(bits, sizeof(bits), EV_KEY);
        if (device_kind == STORAGE_DEV_KIND_MOUSE) {
            input_evdev_set_cap(bits, sizeof(bits), EV_REL);
            input_evdev_set_cap(bits, sizeof(bits), EV_ABS);
        }
    } else if (event_type == EV_KEY) {
        if (device_kind == STORAGE_DEV_KIND_KEYBOARD) {
            for (uint32_t key = KEY_ESC; key <= KEY_COMPOSE; ++key) {
                input_evdev_set_cap(bits, sizeof(bits), key);
            }
        } else {
            input_evdev_set_cap(bits, sizeof(bits), BTN_LEFT);
            input_evdev_set_cap(bits, sizeof(bits), BTN_RIGHT);
            input_evdev_set_cap(bits, sizeof(bits), BTN_MIDDLE);
        }
    } else if (event_type == EV_REL &&
               device_kind == STORAGE_DEV_KIND_MOUSE) {
        input_evdev_set_cap(bits, sizeof(bits), REL_X);
        input_evdev_set_cap(bits, sizeof(bits), REL_Y);
        input_evdev_set_cap(bits, sizeof(bits), REL_WHEEL);
    } else if (event_type == EV_ABS && device_kind == STORAGE_DEV_KIND_MOUSE) {
        input_evdev_set_cap(bits, sizeof(bits), ABS_X);
        input_evdev_set_cap(bits, sizeof(bits), ABS_Y);
    }
    (void)flags;
    for (uint32_t i = 0; i < copy; ++i) {
        ((uint8_t *)buffer)[i] = bits[i];
    }
    for (uint32_t i = copy; i < length; ++i) {
        ((uint8_t *)buffer)[i] = 0;
    }
}

/** @brief Report a normalized pointer axis in framebuffer pixels. */
int input_evdev_absinfo(uint32_t axis, struct input_absinfo *info)
{
    const struct framebuffer *fb = framebuffer_get();
    uint64_t flags;
    uint32_t extent;
    if (!info || (axis != ABS_X && axis != ABS_Y)) return -22;
    extent = axis == ABS_X ? fb->width : fb->height;
    if (!fb->available || !extent) extent = axis == ABS_X ? 1024u : 768u;
    *info = (struct input_absinfo){.maximum = (int32_t)extent - 1};
    kernel_spin_lock_irqsave(&input_lock, &flags);
    info->value = axis == ABS_X ? evdev_mouse_x : evdev_mouse_y;
    kernel_spin_unlock_irqrestore(&input_lock, flags);
    return 0;
}

int input_evdev_present(uint32_t device_kind)
{
    uint32_t index = evdev_device_index(device_kind);
    uint64_t flags;
    int present;
    if (index >= INPUT_EVDEV_DEVICES) return 0;
    kernel_spin_lock_irqsave(&input_lock, &flags);
    present = evdev_present[index];
    kernel_spin_unlock_irqrestore(&input_lock, flags);
    return present;
}
