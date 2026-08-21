#ifndef LEONOS_NANO_CONFIG_H
#define LEONOS_NANO_CONFIG_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <stdbool.h>
#include <stdio.h>

/* Fixed configuration for the LeonOS static terminal port of GNU nano. */
#define PACKAGE "nano"
#define PACKAGE_NAME "GNU nano"
#define PACKAGE_TARNAME "nano"
#define PACKAGE_VERSION "9.2"
#define PACKAGE_STRING "GNU nano 9.2"
#define VERSION "9.2"
#define LOCALEDIR "/system/locale"
#define SYSCONFDIR "/system/config"

#define HAVE_LIMITS_H 1
#define HAVE_NCURSES_H 1
#define HAVE_TERMIOS_H 1
#define NANO_REG_EXTENDED 1

/* Keep the first port self-contained: no external tools, rc files, or
 * multibuffer UI until LeonOS grows the required process facilities. */
#define NANO_TINY 1

#endif
