#ifndef NTCLKS_KEYBOARD_LED_H
#define NTCLKS_KEYBOARD_LED_H

/* IRQ1 consumes ACK/RESEND; the timer sends at most one byte per poll.
 * No controller waits are allowed inside either interrupt handler. */
struct keyboard_led_command {
    uint8_t phase;
    uint8_t target;
    uint8_t applied;
    uint8_t retries;
    uint64_t deadline;
};

static int keyboard_led_reply(struct keyboard_led_command *state, uint8_t byte)
{
    if (byte != 0xfa && byte != 0xfe) return 0;
    if (state->phase == 2 || state->phase == 4) {
        if (byte == 0xfa) {
            if (state->phase == 2) state->phase = 3;
            else {
                state->applied = state->target;
                state->phase = 0;
            }
            state->retries = 0;
        } else if (++state->retries <= 3) {
            --state->phase;
        } else {
            state->phase = 5;
        }
    }
    return 1;
}

static int keyboard_led_next(struct keyboard_led_command *state, uint8_t desired,
                             uint64_t now, int controller_busy, uint8_t *byte)
{
    if (state->phase == 5) {
        if (now < state->deadline) return 0;
        state->phase = 0;
    }
    if (state->phase == 2 || state->phase == 4) {
        if (now < state->deadline) return 0;
        if (++state->retries > 3) {
            state->phase = 5;
            state->deadline = now + 1000000;
            return 0;
        }
        --state->phase;
    }
    if (state->phase == 0) {
        if (state->applied == desired) return 0;
        state->target = desired;
        state->retries = 0;
        state->phase = 1;
    }
    if (controller_busy) return 0;
    *byte = state->phase == 1 ? 0xed : state->target;
    ++state->phase;
    state->deadline = now + 100000;
    return 1;
}
#endif
