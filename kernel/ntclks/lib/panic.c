#include <ntclks/panic.h>

__attribute__((noreturn)) void panic(const char *message)
{
    bugcheck_panic(message);
}
