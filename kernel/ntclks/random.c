#include <ntclks/random.h>

#ifndef LEONOS_RANDOM_TEST
static bool random_hardware_available(void)
{
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1), "c"(0));
    return (ecx & (1u << 30)) != 0;
}

static bool random_hardware_word(uint64_t *value)
{
    unsigned char ready;
    __asm__ volatile("rdrand %0; setc %1" : "=r"(*value), "=qm"(ready));
    return ready != 0;
}
#endif

int kernel_random_fill(void *buffer, size_t length)
{
    if (!length) return 0;
    if (!buffer) return -14;
    if (!random_hardware_available()) return -5;
    uint8_t *destination = buffer;
    size_t completed = 0;
    while (completed < length) {
        uint64_t value = 0;
        bool ready = false;
        for (unsigned attempt = 0; attempt < 10; ++attempt) {
            if (random_hardware_word(&value)) { ready = true; break; }
            __asm__ volatile("pause");
        }
        if (!ready) {
            /* Avoid leaving a partly initialized secret on an error path. */
            for (size_t i = 0; i < completed; ++i) ((volatile uint8_t *)buffer)[i] = 0;
            return -5;
        }
        for (unsigned i = 0; i < sizeof(value) && completed < length; ++i, ++completed)
            destination[completed] = (uint8_t)(value >> (i * 8));
        *(volatile uint64_t *)&value = 0;
    }
    return 0;
}
