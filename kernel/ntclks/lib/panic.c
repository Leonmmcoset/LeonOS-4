#include <ntclks/console.h>
#include <ntclks/panic.h>

#include "../arch/x86_64/port.h"

__attribute__((noreturn)) void panic(const char *message)
{
    console_printf("\n[panic] %s\n", message);
    x86_64_halt();
}
