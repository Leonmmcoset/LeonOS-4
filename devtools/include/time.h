#ifndef LEONOS_TIME_H
#define LEONOS_TIME_H

#include <stdint.h>

typedef int64_t time_t;

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

time_t time(time_t *seconds);
struct tm *gmtime(const time_t *seconds);
struct tm *gmtime_r(const time_t *seconds, struct tm *out);

#endif
