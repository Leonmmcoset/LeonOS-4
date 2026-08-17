/* Target-side runtime objects required by Picolibc and generated ELF files. */

#include <errno.h>
#include <sys/time.h>
#include <sys/times.h>

#include <leonos/system.h>

int gettimeofday(struct timeval *time_value, void *timezone)
{
    struct leonos_time_info info = {0};

    (void)timezone;
    if (!time_value || leonos_time_info(&info) != 0 || !info.valid) {
        errno = ENOSYS;
        return -1;
    }
    time_value->tv_sec = (time_t)info.unix_seconds;
    time_value->tv_usec = 0;
    return 0;
}

clock_t times(struct tms *buffer)
{
    (void)buffer;
    errno = ENOSYS;
    return (clock_t)-1;
}
