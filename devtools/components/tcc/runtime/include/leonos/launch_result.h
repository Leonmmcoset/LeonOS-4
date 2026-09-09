#ifndef LEONOS_LAUNCH_RESULT_H
#define LEONOS_LAUNCH_RESULT_H

/* POSIX-facing launch-result classification. The historical negative result
 * codes remain in the libc transition layer; applications use this enum and
 * leonos_launch_error_text(). Every legacy failure also sets errno to the
 * POSIX value reported by leonos_launch_errno(). */
enum launch_result_error {
    LAUNCH_RESULT_EMPTY = -1001,
    LAUNCH_RESULT_TOO_MANY_ARGS = -1002,
    LAUNCH_RESULT_UNCLOSED_QUOTE = -1003,
    LAUNCH_RESULT_NOT_FOUND = -1004,
    LAUNCH_RESULT_NO_ASSOCIATION = -1005,
    LAUNCH_RESULT_INVALID_SHORTCUT = -1006,
    LAUNCH_RESULT_SHORTCUT_LOOP = -1007,
    LAUNCH_RESULT_EXISTS = -1008,
    LAUNCH_RESULT_ALREADY_RUNNING = -1009,
};

int leonos_launch_is_error(int result);
int leonos_launch_error_kind(int result);
int leonos_launch_errno(int result);

#endif
