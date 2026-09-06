#include <assert.h>
#include <sys/un.h>
#include <leonos/syscall.h>
#include <ntclks/input.h>
#include <ntclks/lock.h>
#include <ntclks/storage.h>

static long test_read(long number, long fd, long buffer, long length);
#define syscall3 test_read
#define main windowd_program_main
#include "../../userland/apps/windowd/main.c"
#undef main
#undef syscall3
#include "../../kernel/ntclks/input.c"

static uint64_t read_cursor;
static struct leonos_input_event delivered[32];
static unsigned delivered_count;

uint64_t time_uptime_us(void) { return 0; }
const struct framebuffer *framebuffer_get(void)
{
    static const struct framebuffer fb = {.available = 1, .width = 1280, .height = 800};
    return &fb;
}
void kernel_spin_lock_irqsave(struct kernel_spinlock *lock, uint64_t *flags)
{
    (void)lock;
    *flags = 0;
}
void kernel_spin_unlock_irqrestore(struct kernel_spinlock *lock, uint64_t flags)
{
    (void)lock;
    (void)flags;
}

static long test_read(long number, long fd, long buffer, long length)
{
    assert(number == SYS_read && fd == 10);
    return input_evdev_read(STORAGE_DEV_KIND_MOUSE, &read_cursor,
                            (void *)buffer, (uint32_t)length, 0);
}

int leonos_ipc_send(int fd, uint32_t type, const void *payload, uint32_t length)
{
    assert(fd == 11 && type == LEONOS_WIN_MSG_INPUT);
    assert(length == sizeof(delivered[0]) && delivered_count < 32);
    delivered[delivered_count++] = *(const struct leonos_input_event *)payload;
    return 0;
}

int main(void)
{
    input_init();
    read_cursor = input_evdev_cursor_now();
    policy_slot = 0;
    clients[0].fd = 11;
    display_state.fb_width = 1280;
    display_state.fb_height = 800;
    /* The driver starts at the framebuffer center, independently of windowd. */
    input_push_mouse(900, 550, 260, 150, 0);
    pump_input_device(10, LEONOS_INPUT_MOUSE);
    assert(delivered_count == 1);
    assert(delivered[0].x == 900 && delivered[0].y == 550);

    /* A button and both coordinates must be observed as one input packet. */
    input_push_mouse(1279, 799, 379, 249, 1);
    pump_input_device(10, LEONOS_INPUT_MOUSE);
    assert(delivered_count == 2);
    assert(delivered[1].x == 1279 && delivered[1].y == 799);
    assert(delivered[1].buttons == 1);
    input_push_mouse(0, 0, -1279, -799, 0);
    pump_input_device(10, LEONOS_INPUT_MOUSE);
    assert(delivered_count == 3 && delivered[2].x == 0 && delivered[2].y == 0);
    assert(delivered[2].buttons == 0);

    /* A lost old motion packet must not offset later absolute positions. */
    input_push_mouse(400, 300, 400, 300, 0);
    read_cursor = input_evdev_cursor_now();
    input_push_mouse(950, 650, 550, 350, 0);
    pump_input_device(10, LEONOS_INPUT_MOUSE);
    assert(delivered_count == 4);
    assert(delivered[3].x == 950 && delivered[3].y == 650);
    input_push_mouse_wheel(950, 650, -1, 0);
    pump_input_device(10, LEONOS_INPUT_MOUSE);
    assert(delivered_count == 5 && delivered[4].type == LEONOS_INPUT_MOUSE_WHEEL);
    assert(delivered[4].x == 950 && delivered[4].y == 650 && delivered[4].dy == -1);
    struct input_absinfo info;
    assert(input_evdev_absinfo(ABS_X, &info) == 0);
    assert(info.value == 950 && info.minimum == 0 && info.maximum == 1279);
    assert(input_evdev_absinfo(ABS_Y, &info) == 0);
    assert(info.value == 650 && info.maximum == 799);
    uint8_t bits[8];
    input_evdev_capabilities(STORAGE_DEV_KIND_MOUSE, 0, bits, sizeof(bits));
    assert(bits[0] & (1u << EV_ABS));
    input_evdev_capabilities(STORAGE_DEV_KIND_MOUSE, EV_ABS, bits, sizeof(bits));
    assert((bits[0] & 3u) == 3u);
    puts("Pointer routing passed: exact positions, packet boundaries, wheel and resynchronization");
    return 0;
}
