#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../../userland/apps/taskmgr/gpu_sample.h"

static struct leonos_gpu_info sample(uint32_t generation, uint64_t ticks,
                                      uint64_t busy)
{
    return (struct leonos_gpu_info){
        .size = sizeof(struct leonos_gpu_info),
        .version = LEONOS_GPU_ABI_VERSION,
        .flags = LEONOS_GPU_AVAILABLE | LEONOS_GPU_BUSY_ESTIMATED,
        .generation = generation,
        .sample_ticks = ticks,
        .busy_ticks = busy,
    };
}

int main(void)
{
    struct taskmgr_gpu_sample state = {0};
    struct leonos_gpu_info info = sample(1, 1000, 900);

    assert(taskmgr_gpu_sample_update(&state, &info));
    assert(state.available && !state.valid && state.percent == 0);
    info = sample(1, 1500, 1025);
    assert(!taskmgr_gpu_sample_update(&state, &info));
    assert(state.valid && state.percent == 25);

    /* Duplicate clock readings must not show or discard unmeasured work. */
    info = sample(1, 1500, 1050);
    assert(!taskmgr_gpu_sample_update(&state, &info));
    assert(!state.valid && state.percent == 0);
    info = sample(1, 2000, 1125);
    assert(!taskmgr_gpu_sample_update(&state, &info));
    assert(state.valid && state.percent == 20);

    info = sample(2, 3000, 2100);
    assert(taskmgr_gpu_sample_update(&state, &info));
    assert(!state.valid && state.percent == 0);
    info = sample(2, 3500, 2200);
    assert(!taskmgr_gpu_sample_update(&state, &info));
    assert(state.valid && state.percent == 20);
    info = sample(2, 10, 5);
    assert(taskmgr_gpu_sample_update(&state, &info));
    assert(!state.valid && state.percent == 0);
    info = sample(2, 20, 1);
    assert(taskmgr_gpu_sample_update(&state, &info));
    assert(!state.valid && state.percent == 0);
    info = sample(2, 30, 21);
    assert(!taskmgr_gpu_sample_update(&state, &info));
    assert(state.valid && state.percent == 100);

    assert(taskmgr_gpu_sample_update(&state, NULL));
    assert(!state.available && !state.valid && state.percent == 0);
    info = sample(2, 1030, 121);
    assert(taskmgr_gpu_sample_update(&state, &info));
    assert(state.available && !state.valid);
    info.flags = 0;
    assert(taskmgr_gpu_sample_update(&state, &info));
    assert(!state.available && !state.valid && state.percent == 0);

    /* Full-width counters expose overflow in a naive busy * 100 formula. */
    info = sample(3, 0, 0);
    taskmgr_gpu_sample_update(&state, &info);
    info = sample(3, UINT64_MAX, UINT64_MAX / 2);
    taskmgr_gpu_sample_update(&state, &info);
    assert(state.valid && state.percent == 49);
    info = sample(4, 0, 0);
    taskmgr_gpu_sample_update(&state, &info);
    info = sample(4, UINT64_MAX, UINT64_MAX - 1);
    taskmgr_gpu_sample_update(&state, &info);
    assert(state.valid && state.percent == 99);
    info = sample(5, 0, 0);
    taskmgr_gpu_sample_update(&state, &info);
    info = sample(5, 3, 1);
    taskmgr_gpu_sample_update(&state, &info);
    assert(state.valid && state.percent == 33);

    puts("taskmgr GPU sampling tests passed");
    return 0;
}
