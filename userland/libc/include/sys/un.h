#ifndef LEONOS_SYS_UN_H
#define LEONOS_SYS_UN_H

#include <leonos/socket.h>
#include <sys/socket.h>

struct sockaddr_un {
    sa_family_t sun_family;
    char sun_path[LEONOS_UNIX_PATH_MAX];
};

#endif
