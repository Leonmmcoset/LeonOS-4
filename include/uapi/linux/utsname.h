#ifndef LEONOS_UAPI_LINUX_UTSNAME_H
#define LEONOS_UAPI_LINUX_UTSNAME_H

#define LEONOS_UTSNAME_LEN 65u

struct utsname {
    char sysname[LEONOS_UTSNAME_LEN];
    char nodename[LEONOS_UTSNAME_LEN];
    char release[LEONOS_UTSNAME_LEN];
    char version[LEONOS_UTSNAME_LEN];
    char machine[LEONOS_UTSNAME_LEN];
    char domainname[LEONOS_UTSNAME_LEN];
};

#endif
