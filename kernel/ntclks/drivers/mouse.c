#include <ntclks/console.h>
#include <ntclks/framebuffer.h>
#include <ntclks/input.h>
#include <ntclks/mouse.h>

#include "../arch/x86_64/port.h"

#define PS2_DATA 0x60
#define PS2_STATUS 0x64
#define PS2_COMMAND 0x64

static struct mouse_state state = {
    .x = 320,
    .y = 240,
    .buttons = 0,
    .present = false,
};

static uint8_t packet[3];
static uint8_t packet_index;
static uint32_t events;
static uint8_t last_status;
static uint8_t last_data;
static uint8_t last_ack;

static int wait_input_clear(void)
{
    for (int i = 0; i < 100000; ++i) {
        if ((x86_64_inb(PS2_STATUS) & 0x02) == 0) {
            return 1;
        }
    }
    return 0;
}

static int wait_output_full(void)
{
    for (int i = 0; i < 100000; ++i) {
        if (x86_64_inb(PS2_STATUS) & 0x01) {
            return 1;
        }
    }
    return 0;
}

static void ps2_write_command(uint8_t value)
{
    if (wait_input_clear()) {
        x86_64_outb(value, PS2_COMMAND);
    }
}

static void ps2_write_data(uint8_t value)
{
    if (wait_input_clear()) {
        x86_64_outb(value, PS2_DATA);
    }
}

static void mouse_write(uint8_t value)
{
    ps2_write_command(0xd4);
    ps2_write_data(value);
}

static uint8_t ps2_read_data(void)
{
    if (!wait_output_full()) {
        return 0;
    }
    last_data = x86_64_inb(PS2_DATA);
    return last_data;
}

static void ps2_flush_output(void)
{
    for (int i = 0; i < 64; ++i) {
        uint8_t status = x86_64_inb(PS2_STATUS);
        if ((status & 0x01) == 0) {
            break;
        }
        (void)x86_64_inb(PS2_DATA);
    }
}

static int mouse_write_ack(uint8_t value)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        mouse_write(value);
        uint8_t ack = ps2_read_data();
        last_ack = ack;
        if (ack == 0xfa) {
            return 1;
        }
        if (ack != 0xfe) {
            break;
        }
    }
    return 0;
}

void mouse_init(void)
{
    packet_index = 0;
    events = 0;
    last_status = 0;
    last_data = 0;
    last_ack = 0;

    ps2_flush_output();
    ps2_write_command(0xa8);
    ps2_write_command(0x20);
    uint8_t status = ps2_read_data();
    status |= 0x02;
    status &= (uint8_t)~0x20;
    ps2_write_command(0x60);
    ps2_write_data(status);
    ps2_flush_output();

    int defaults_ok = mouse_write_ack(0xf6);
    int enable_ok = mouse_write_ack(0xf4);
    state.present = defaults_ok && enable_ok;
    console_printf("[ntclks] ps/2 mouse init defaults=%d enable=%d ack=0x%x at %d,%d\n",
                   defaults_ok,
                   enable_ok,
                   last_ack,
                   state.x,
                   state.y);
}

void mouse_poll(void)
{
    for (int limit = 0; limit < 32; ++limit) {
        uint8_t status = x86_64_inb(PS2_STATUS);
        last_status = status;
        if ((status & 0x01) == 0) {
            break;
        }
        uint8_t data = x86_64_inb(PS2_DATA);
        last_data = data;
        if ((status & 0x20) == 0) {
            continue;
        }
        if (packet_index == 0 && (data & 0x08) == 0) {
            continue;
        }
        packet[packet_index++] = data;
        if (packet_index < 3) {
            continue;
        }
        packet_index = 0;

        int dx = (int8_t)packet[1];
        int dy = (int8_t)packet[2];
        state.buttons = packet[0] & 0x07;
        state.x += dx;
        state.y -= dy;
        ++events;
        const struct framebuffer *fb = framebuffer_get();
        int32_t max_x = fb->available ? (int32_t)fb->width - 16 : 1024;
        int32_t max_y = fb->available ? (int32_t)fb->height - 16 : 768;
        if (state.x < 0) {
            state.x = 0;
        }
        if (state.y < 0) {
            state.y = 0;
        }
        if (state.x > max_x) {
            state.x = max_x;
        }
        if (state.y > max_y) {
            state.y = max_y;
        }
        input_push_mouse(state.x, state.y, dx, -dy, state.buttons);
        if (events <= 8) {
            console_printf("[ntclks] ps/2 mouse packet #%u dx=%d dy=%d buttons=%u pos=%d,%d\n",
                           events,
                           dx,
                           dy,
                           state.buttons,
                           state.x,
                           state.y);
        }
    }
}

const struct mouse_state *mouse_get_state(void)
{
    return &state;
}

uint32_t mouse_event_count(void)
{
    return events;
}

uint8_t mouse_last_status(void)
{
    return last_status;
}

uint8_t mouse_last_data(void)
{
    return last_data;
}

uint8_t mouse_last_ack(void)
{
    return last_ack;
}
