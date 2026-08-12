/*
 * LeonOS panic wrapper: provides the short fatal-error API.
 * Converts a panic message into the full kernel bugcheck path.
 */
#include <ntclks/panic.h>

__attribute__((noreturn)) void panic(const char *message)
{
    bugcheck_panic(message);
}
