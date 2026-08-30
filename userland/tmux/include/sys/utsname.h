#ifndef LEONOS_TMUX_SYS_UTSNAME_H
#define LEONOS_TMUX_SYS_UTSNAME_H

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

int uname(struct utsname *);

#endif
