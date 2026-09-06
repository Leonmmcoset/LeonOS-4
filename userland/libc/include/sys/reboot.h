#ifndef LEONOS_SYS_REBOOT_H
#define LEONOS_SYS_REBOOT_H

#include <linux/reboot.h>

#define RB_AUTOBOOT RB_AUTOBOOT
#define RB_HALT_SYSTEM RB_HALT_SYSTEM
#define RB_POWER_OFF RB_POWER_OFF

int reboot(int __command);

#endif
