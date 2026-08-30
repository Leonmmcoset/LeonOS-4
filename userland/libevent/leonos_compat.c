#include <net/if.h>
#include <netdb.h>

int h_errno;

unsigned int if_nametoindex(const char *name)
{
    (void)name;
    return 0;
}

struct hostent *gethostbyname(const char *name)
{
    (void)name;
    h_errno = HOST_NOT_FOUND;
    return 0;
}
