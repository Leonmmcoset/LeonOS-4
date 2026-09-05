#ifndef TASKMGR_GPU_SAMPLE_H
#define TASKMGR_GPU_SAMPLE_H

#include <leonos/gpu.h>

struct taskmgr_gpu_sample {
    uint64_t sample_ticks, busy_ticks;
    uint32_t generation, percent;
    uint8_t available, valid, baseline_valid;
};

/* Returns nonzero when the caller must discard the old GPU history. */
static inline int taskmgr_gpu_sample_update(struct taskmgr_gpu_sample *state,
                                             const struct leonos_gpu_info *info)
{
    uint64_t elapsed, busy;
    int reset;
    state->percent = 0;
    state->valid = 0;
    if (!info || !(info->flags & LEONOS_GPU_AVAILABLE)) {
        state->available = 0;
        state->baseline_valid = 0;
        return 1;
    }
    state->available = 1;
    reset = !state->baseline_valid || state->generation != info->generation ||
            info->sample_ticks < state->sample_ticks ||
            info->busy_ticks < state->busy_ticks;
    if (!reset) {
        elapsed = info->sample_ticks - state->sample_ticks;
        busy = info->busy_ticks - state->busy_ticks;
        if (!elapsed) {
            return 0;
        }
        if (busy >= elapsed) {
            state->percent = 100;
        } else {
            /* Compare against ceil(elapsed * percent / 100) without an
             * overflowing multiplication or a 128-bit runtime division. */
            uint64_t whole = elapsed / 100U;
            uint32_t remainder = (uint32_t)(elapsed % 100U);
            for (uint32_t bit = 64U; bit; bit >>= 1U) {
                uint32_t candidate = state->percent + bit;
                if (candidate <= 100U &&
                    busy >= whole * candidate +
                                (remainder * candidate + 99U) / 100U) {
                    state->percent = candidate;
                }
            }
        }
        state->valid = 1;
    }
    state->sample_ticks = info->sample_ticks;
    state->busy_ticks = info->busy_ticks;
    state->generation = info->generation;
    state->baseline_valid = 1;
    return reset;
}

#endif
