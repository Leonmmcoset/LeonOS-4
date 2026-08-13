/*
 * LeonOS panic wrapper: provides the short fatal-error API.
 * Converts a panic message into the full kernel bugcheck path.
 */
#include <ntclks/panic.h>

/**
 * @brief Coordinates the panic operation.
 * @param message Input or output value used by this operation.
 */
__attribute__((noreturn)) void panic(const char *message)
{
    bugcheck_panic(message);
}
