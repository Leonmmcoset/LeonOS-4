/*
 * LeonOS x86_64 IRQ handling: services hardware interrupt requests.
 * Routes timer, input, storage, and device interrupts to kernel subsystems.
 */
#include <ntclks/console.h>
#include <ntclks/driver_manager.h>
#include <ntclks/input.h>
#include <ntclks/sched.h>
#include <ntclks/time.h>
#include <ntclks/trap.h>
#include <ntclks/userland.h>

#include "port.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xa0
#define PIC2_DATA 0xa1
#define PIC_EOI 0x20
#define PIT_CHANNEL0 0x40
#define PIT_COMMAND 0x43
#define PS2_DATA 0x60

static uint8_t key_states[128];
static uint8_t e0_prefix;

static void io_wait(void)
{
    x86_64_outb(0, 0x80);
}

static void pic_send_eoi(uint8_t irq)
{
    if (irq >= 8) {
        x86_64_outb(PIC_EOI, PIC2_COMMAND);
    }
    x86_64_outb(PIC_EOI, PIC1_COMMAND);
}

static void pic_remap(void)
{
    x86_64_outb(0xff, PIC1_DATA);
    x86_64_outb(0xff, PIC2_DATA);

    x86_64_outb(0x11, PIC1_COMMAND);
    io_wait();
    x86_64_outb(0x11, PIC2_COMMAND);
    io_wait();
    x86_64_outb(0x20, PIC1_DATA);
    io_wait();
    x86_64_outb(0x28, PIC2_DATA);
    io_wait();
    x86_64_outb(4, PIC1_DATA);
    io_wait();
    x86_64_outb(2, PIC2_DATA);
    io_wait();
    x86_64_outb(0x01, PIC1_DATA);
    io_wait();
    x86_64_outb(0x01, PIC2_DATA);
    io_wait();

    uint8_t mask1 = (uint8_t)~((1u << 0) | (1u << 1) | (1u << 2));
    uint8_t mask2 = (uint8_t)~(1u << 4);
    x86_64_outb(mask1, PIC1_DATA);
    x86_64_outb(mask2, PIC2_DATA);
    pic_send_eoi(12);
    pic_send_eoi(1);
    pic_send_eoi(0);
}

static void pit_init_100hz(void)
{
    uint16_t divisor = (uint16_t)(1193182 / NTCLKS_TICK_HZ);
    x86_64_outb(0x36, PIT_COMMAND);
    x86_64_outb((uint8_t)(divisor & 0xff), PIT_CHANNEL0);
    x86_64_outb((uint8_t)(divisor >> 8), PIT_CHANNEL0);
}

void irq_init(void)
{
    __asm__ volatile("cli");
    pic_remap();
    pit_init_100hz();
    console_printf("[ntclks] PIC remapped, PIT=%uHz, IRQ0/1/12 enabled\n", (unsigned)NTCLKS_TICK_HZ);
}

struct task *irq_dispatch(struct trap_frame *frame)
{
    uint64_t vector = frame ? frame->vector : 0;
    bool from_user = frame && ((frame->cs & 3ULL) == 3ULL);
    if (vector == 0x20) {
        time_on_tick();
        pic_send_eoi(0);
        return from_user ? userland_schedule_from_frame(frame) : NULL;
    } else if (vector == 0x21) {
        uint8_t scancode = x86_64_inb(PS2_DATA);
        if (scancode == 0xe0) {
            e0_prefix = 1;
        } else if (scancode != 0xe1) {
            uint8_t keycode = scancode & 0x7f;
            uint8_t pressed = (scancode & 0x80) == 0;
            if (e0_prefix) {
                switch (keycode) {
                case 0x1d:
                    keycode = 116;
                    break;
                case 0x38:
                    keycode = 115;
                    break;
                case 0x5b:
                    keycode = 112;
                    break;
                case 0x5c:
                    keycode = 113;
                    break;
                case 0x5d:
                    keycode = 114;
                    break;
                default:
                    break;
                }
                e0_prefix = 0;
            }
            if (keycode < sizeof(key_states) && key_states[keycode] != pressed) {
                key_states[keycode] = pressed;
                input_push_key(keycode, pressed);
            }
        }
        pic_send_eoi(1);
    } else if (vector == 0x2c) {
        driver_manager_mouse_poll();
        pic_send_eoi(12);
    } else {
        pic_send_eoi(0);
    }
    return NULL;
}
