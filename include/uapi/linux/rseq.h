#ifndef LEONOS_UAPI_LINUX_RSEQ_H
#define LEONOS_UAPI_LINUX_RSEQ_H

#include <stdint.h>

#define RSEQ_CPU_ID_UNINITIALIZED        (-1)
#define RSEQ_CPU_ID_REGISTRATION_FAILED  (-2)
#define RSEQ_FLAG_UNREGISTER             (1U << 0)

struct rseq_cs {
    uint32_t version;
    uint32_t flags;
    uint64_t start_ip;
    uint64_t post_commit_offset;
    uint64_t abort_ip;
} __attribute__((aligned(32)));

struct rseq {
    uint32_t cpu_id_start;
    uint32_t cpu_id;
    uint64_t rseq_cs;
    uint32_t flags;
    uint32_t node_id;
    uint32_t mm_cid;
    char end[];
} __attribute__((aligned(32)));

#define RSEQ_SIZE 32U
#define RSEQ_SIG 0x53053053U

#endif
