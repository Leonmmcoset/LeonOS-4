#ifndef LEONOS_SL_UNISTD_H
#define LEONOS_SL_UNISTD_H

#include_next <unistd.h>

/* sl only uses this for its 40ms frame delay. */
int usleep(unsigned int microseconds);

#endif
