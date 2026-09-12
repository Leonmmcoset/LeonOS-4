#ifndef LEONOS_UAPI_LINUX_FUTEX_H
#define LEONOS_UAPI_LINUX_FUTEX_H

#include <linux/types.h>

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_REQUEUE 3
#define FUTEX_CMP_REQUEUE 4
#define FUTEX_WAKE_OP 5
#define FUTEX_WAIT_BITSET 9
#define FUTEX_WAKE_BITSET 10
#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_CLOCK_REALTIME 256
#define FUTEX_CMD_MASK ~(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME)
#define FUTEX2_SIZE_U8 0x00
#define FUTEX2_SIZE_U16 0x01
#define FUTEX2_SIZE_U32 0x02
#define FUTEX2_SIZE_U64 0x03
#define FUTEX2_NUMA 0x04
#define FUTEX2_PRIVATE FUTEX_PRIVATE_FLAG
#define FUTEX2_SIZE_MASK 0x03
#define FUTEX_32 FUTEX2_SIZE_U32
#define FUTEX_WAITV_MAX 128
#define FUTEX_BITSET_MATCH_ANY 0xffffffffU
#define FUTEX_WAITERS 0x80000000U
#define FUTEX_OWNER_DIED 0x40000000U
#define FUTEX_TID_MASK 0x3fffffffU

struct futex_waitv {
    __u64 val;
    __u64 uaddr;
    __u32 flags;
    __u32 __reserved;
};

#define FUTEX_OP_SET 0
#define FUTEX_OP_ADD 1
#define FUTEX_OP_OR 2
#define FUTEX_OP_ANDN 3
#define FUTEX_OP_XOR 4
#define FUTEX_OP_OPARG_SHIFT 8
#define FUTEX_OP_CMP_EQ 0
#define FUTEX_OP_CMP_NE 1
#define FUTEX_OP_CMP_LT 2
#define FUTEX_OP_CMP_LE 3
#define FUTEX_OP_CMP_GT 4
#define FUTEX_OP_CMP_GE 5
#define FUTEX_OP(op, oparg, cmp, cmparg) \
    ((((unsigned)(op) & 15U) << 28) | (((unsigned)(cmp) & 15U) << 24) | \
     (((unsigned)(oparg) & 4095U) << 12) | ((unsigned)(cmparg) & 4095U))

struct linux_robust_list_head {
    unsigned long next;
    long futex_offset;
    unsigned long list_op_pending;
};

#endif
