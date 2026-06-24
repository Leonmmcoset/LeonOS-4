#ifndef LEONOS_LAUNCH_H
#define LEONOS_LAUNCH_H

#include <stdint.h>

#define LEONOS_LAUNCH_MAX_ARGS 8U

#define LEONOS_LAUNCH_ERR_EMPTY -1001
#define LEONOS_LAUNCH_ERR_TOO_MANY_ARGS -1002
#define LEONOS_LAUNCH_ERR_UNCLOSED_QUOTE -1003
#define LEONOS_LAUNCH_ERR_NOT_FOUND -1004
#define LEONOS_LAUNCH_ERR_NO_ASSOCIATION -1005

int leonos_cmdline_split(char *line, char *argv[], uint32_t max_args);
int leonos_launch_argv(char *argv[]);
int leonos_launch_command_line(char *line, char *argv[], uint32_t max_args);
const char *leonos_launch_error_text(int code);

#endif
