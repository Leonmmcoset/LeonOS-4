#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../kernel/ntclks/syscall.c"
#include "../../kernel/ntclks/input.c"

static bool output_writable = true;
bool user_range_ok(uint64_t address, uint64_t length)
{ return address != 0 || length == 0; }
bool user_range_writable(uint64_t address, uint64_t length)
{ return output_writable && user_range_ok(address, length); }
uint32_t sched_current_pid(void) { return 1; }
uint64_t time_uptime_us(void) { return 0; }
const struct framebuffer *framebuffer_get(void) { return NULL; }
void kernel_spin_lock_irqsave(struct kernel_spinlock *lock, uint64_t *flags)
{ (void)lock; *flags = 0; }
void kernel_spin_unlock_irqrestore(struct kernel_spinlock *lock, uint64_t flags)
{ (void)lock; (void)flags; }

int main(void)
{
    struct task_file file = {.flags = TASK_FILE_FLAG_DEV_NODE};
    file.node.first_cluster = STORAGE_DEV_KIND_KEYBOARD;
    uint8_t buffer[16];
    input_init();
    memset(buffer, 0xaa, sizeof(buffer));
    assert(task_evdev_ioctl(&file, EVIOCGLED(16), (uintptr_t)buffer) == 8);
    for (unsigned i = 0; i < 8; ++i) assert(buffer[i] == 0);
    for (unsigned i = 8; i < sizeof(buffer); ++i) assert(buffer[i] == 0xaa);
    input_push_key(KEY_CAPSLOCK, 1);
    assert(task_evdev_ioctl(&file, EVIOCGLED(1), (uintptr_t)buffer) == 1);
    assert(buffer[0] == 1U << LED_CAPSL);
    input_push_key(KEY_CAPSLOCK, 1);
    assert(task_evdev_ioctl(&file, EVIOCGLED(1), (uintptr_t)buffer) == 1);
    assert(buffer[0] == 1U << LED_CAPSL);
    output_writable = false;
    assert(task_evdev_ioctl(&file, EVIOCGLED(1), (uintptr_t)buffer) == -LEONOS_EFAULT);
    assert(task_evdev_ioctl(&file, EVIOCGLED(0), 0) == 0);
    output_writable = true;
    assert(task_evdev_ioctl(&file, EVIOCGLED(1), 0) == -LEONOS_EFAULT);
    file.node.first_cluster = STORAGE_DEV_KIND_MOUSE;
    assert(task_evdev_ioctl(&file, EVIOCGLED(1), (uintptr_t)buffer) == 1);
    assert(buffer[0] == 0);
    puts("EVIOCGLED passed: real input state, bitmap size, untouched tail and EFAULT");
    return 0;
}
