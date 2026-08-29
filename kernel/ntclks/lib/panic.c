/*
 * LeonOS panic wrapper: provides the short fatal-error API.
 * Converts a panic message into the full kernel bugcheck path.
 */
#include <ntclks/panic.h>

/**
 * @brief Forward the panic message into the full bugcheck path; never returns.
 */
__attribute__((noreturn)) void panic(const char *message)
{
    bugcheck_panic(message);
}
