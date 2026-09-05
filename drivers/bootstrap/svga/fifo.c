#include "device.h"

static uint32_t read_word(const void *data, uint32_t offset)
{
    const uint8_t *p = (const uint8_t *)data + offset;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int ring_state(uint32_t *next, uint32_t *stop)
{
    if (!svga.fifo_ready || !svga.fifo || svga.min < 16 ||
        svga.fifo_bytes <= svga.min + 4) return SVGA_ENODEV;
    *next = svga.fifo[SVGA_FIFO_NEXT_CMD];
    *stop = svga.fifo[SVGA_FIFO_STOP];
    if (svga.fifo[SVGA_FIFO_MIN] != svga.min ||
        svga.fifo[SVGA_FIFO_MAX] != svga.fifo_bytes ||
        ((*next | *stop) & 3) || *next < svga.min || *stop < svga.min ||
        *next >= svga.fifo_bytes || *stop >= svga.fifo_bytes) return SVGA_EIO;
    return 0;
}

static void put_word(uint32_t *next, uint32_t word, bool reserve)
{
    svga.fifo[*next / 4] = word;
    *next += 4;
    if (*next == svga.fifo_bytes) *next = svga.min;
    if (!reserve) {
        /* Legacy hosts require a NEXT_CMD publication for each DWORD. */
        svga_barrier();
        svga.fifo[SVGA_FIFO_NEXT_CMD] = *next;
    }
}

static void fifo_wake_locked(void)
{
    svga_barrier();
    svga.ops.write(SVGA_REG_SYNC, 1);
}

void svga_fifo_defer_notify_locked(void)
{
    svga.fifo_notify_deferred = true;
}

void svga_fifo_flush_notify_locked(void)
{
    svga.fifo_notify_deferred = false;
    fifo_wake_locked();
}

int svga_fifo_packet_locked(uint32_t id, bool three_d,
                            const struct svga_span *spans, uint32_t count)
{
    uint32_t bytes = three_d ? 8 : 4, next = 0, stop = 0;
    if (count && !spans) return SVGA_EINVAL;
    for (uint32_t i = 0; i < count; ++i) {
        if ((spans[i].bytes & 3) || (spans[i].bytes && !spans[i].data) ||
            spans[i].bytes > UINT32_MAX - bytes) return SVGA_EINVAL;
        bytes += spans[i].bytes;
    }
    int ret = ring_state(&next, &stop);
    if (ret) return ret;
    if (bytes >= svga.fifo_bytes - svga.min) return SVGA_EINVAL;
    for (uint32_t poll = 0;; ++poll) {
        ret = ring_state(&next, &stop);
        if (ret) return ret;
        uint32_t free_bytes = next >= stop ? svga.fifo_bytes - next + stop - svga.min : stop - next;
        if (bytes < free_bytes) break;
        if (poll == SVGA_POLL_LIMIT) return SVGA_ETIMEDOUT;
        if (!poll) fifo_wake_locked();
        (void)svga.ops.read(SVGA_REG_BUSY);
        svga_relax();
    }
    bool reserve = (svga.fifo_caps & SVGA_FIFO_CAP_RESERVE) != 0;
    if (reserve) { svga.fifo[SVGA_FIFO_RESERVED] = bytes; svga_barrier(); }
    put_word(&next, id, reserve);
    if (three_d) put_word(&next, bytes - 8, reserve);
    for (uint32_t i = 0; i < count; ++i)
        for (uint32_t j = 0; j < spans[i].bytes; j += 4)
            put_word(&next, read_word(spans[i].data, j), reserve);
    svga_barrier();
    svga.fifo[SVGA_FIFO_NEXT_CMD] = next;
    svga_barrier();
    if (reserve) svga.fifo[SVGA_FIFO_RESERVED] = 0;
    if (!svga.fifo_notify_deferred) fifo_wake_locked();
    return 0;
}

int svga_command_locked(uint32_t id, const void *data, uint32_t bytes)
{
    struct svga_span span = {data, bytes};
    return svga_fifo_packet_locked(id, true, &span, 1);
}

int svga_drain_locked(void)
{
    uint32_t next, stop;
    int ret = ring_state(&next, &stop);
    if (ret) return ret;
    fifo_wake_locked();
    for (uint32_t i = 0; i < SVGA_POLL_LIMIT; ++i) {
        uint32_t busy = svga.ops.read(SVGA_REG_BUSY);
        ret = ring_state(&next, &stop);
        if (ret) return ret;
        if (!busy && next == stop) { svga_barrier(); return 0; }
        svga_relax();
    }
    return SVGA_ETIMEDOUT;
}

int svga_fence_locked(svga_handle *out)
{
    if (!out || !(svga.fifo_caps & SVGA_FIFO_CAP_FENCE)) return SVGA_ENOTSUP;
    uint32_t seq = svga.next_fence;
    if (!seq) seq = 1;
    if ((uint32_t)(seq - svga.fifo[SVGA_FIFO_FENCE]) >= 0x80000000u) {
        int ret = svga_drain_locked();
        if (ret) return ret;
    }
    struct svga_span span = {&seq, 4};
    int ret = svga_fifo_packet_locked(SVGA_CMD_FENCE, false, &span, 1);
    if (ret) return ret;
    svga.issued_fence = seq;
    svga.next_fence = seq + 1;
    *out = ((uint64_t)svga.generation << 32) | seq;
    return 0;
}

static void wait_delay_locked(uint32_t poll)
{
    /* Polling FIFO_FENCE is plain MMIO; pace the loop without the
     * synchronous FIFO processing caused by repeated SVGA_REG_BUSY reads. */
    uint32_t pauses = 16u + (poll >> 3);
    if (pauses > 1024u) pauses = 1024u;
    for (uint32_t i = 0; i < pauses; ++i) svga_relax();
}

int svga_wait_locked(svga_handle fence)
{
    uint32_t seq = (uint32_t)fence;
    if (!seq || (fence >> 32) != svga.generation ||
        (int32_t)(svga.issued_fence - seq) < 0) return SVGA_EINVAL;
    if (!(svga.fifo_caps & SVGA_FIFO_CAP_FENCE)) return SVGA_ENOTSUP;
    fifo_wake_locked();
    /* Fast path: Fence completion is published through plain FIFO MMIO.
     * Poll it without SVGA_REG_BUSY reads, which synchronously process the
     * FIFO in the guest vCPU and are billed as guest CPU time. */
    for (uint32_t i = 0; i < SVGA_POLL_LIMIT; ++i) {
        if ((int32_t)(svga.fifo[SVGA_FIFO_FENCE] - seq) >= 0) {
            svga_barrier(); return 0;
        }
        wait_delay_locked(i);
    }
    /* Slow-start fallback: preserve the old bounded synchronous loop for
     * hosts that only make progress while SVGA_REG_BUSY is being read. */
    for (uint32_t i = 0;; ++i) {
        if ((int32_t)(svga.fifo[SVGA_FIFO_FENCE] - seq) >= 0) {
            svga_barrier(); return 0;
        }
        if (i == SVGA_POLL_LIMIT) return SVGA_ETIMEDOUT;
        (void)svga.ops.read(SVGA_REG_BUSY);
        svga_relax();
    }
}

int svga_sync_locked(void)
{
    svga_handle fence;
    svga.fifo_notify_deferred = false;
    if (!(svga.fifo_caps & SVGA_FIFO_CAP_FENCE)) return svga_drain_locked();
    int ret = svga_fence_locked(&fence);
    return ret ? ret : svga_wait_locked(fence);
}

int svga_fence_insert(svga_handle *out)
{
    uint64_t flags = svga_lock();
    int ret = svga_fence_locked(out);
    svga_unlock(flags);
    return ret;
}

int svga_fence_wait(svga_handle fence)
{
    uint64_t flags = svga_lock();
    int ret = svga_wait_locked(fence);
    svga_unlock(flags);
    return ret;
}
