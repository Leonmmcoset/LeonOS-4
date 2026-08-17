/* LeonOS file-status ABI adapter for GNU nano's POSIX-facing source. */
#include <errno.h>
#include <stdint.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <leonos/system.h>

char *realpath(const char *path, char *resolved)
{
    char cwd[4096];
    char *result = resolved;
    size_t path_len;
    size_t cwd_len;
    if (!path || !path[0] || access(path, 0) < 0) {
        return 0;
    }
    if (path[1] == ':') {
        path_len = strlen(path);
        if (!result) {
            result = (char *)malloc(path_len + 1U);
        }
        if (!result) {
            errno = ENOMEM;
            return 0;
        }
        memcpy(result, path, path_len + 1U);
        return result;
    }
    if (!getcwd(cwd, sizeof(cwd))) {
        return 0;
    }
    cwd_len = strlen(cwd);
    path_len = strlen(path);
    if (!result) {
        result = (char *)malloc(cwd_len + path_len + 2U);
    }
    if (!result) {
        errno = ENOMEM;
        return 0;
    }
    memcpy(result, cwd, cwd_len);
    if (cwd_len && result[cwd_len - 1U] != '/') {
        result[cwd_len++] = '/';
    }
    memcpy(result + cwd_len, path, path_len + 1U);
    return result;
}

int sigaction(int signal_number, const struct sigaction *action,
              struct sigaction *previous)
{
    (void)signal_number;
    (void)action;
    if (previous) {
        uint8_t *bytes = (uint8_t *)previous;
        uint32_t index;
        for (index = 0; index < sizeof(*previous); ++index) {
            bytes[index] = 0;
        }
        previous->sa_handler = SIG_DFL;
    }
    return 0;
}

int gettimeofday(struct timeval *time_value, void *timezone)
{
    struct leonos_time_info info;
    (void)timezone;
    if (!time_value) {
        errno = EINVAL;
        return -1;
    }
    if (leonos_time_info(&info) < 0) {
        return -1;
    }
    time_value->tv_sec = (time_t)info.unix_seconds;
    time_value->tv_usec = (suseconds_t)((info.uptime_ms % 1000U) * 1000U);
    return 0;
}
