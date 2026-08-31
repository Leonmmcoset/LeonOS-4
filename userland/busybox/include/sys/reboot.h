#ifndef LEONOS_SYS_REBOOT_H
#define LEONOS_SYS_REBOOT_H

#define RB_HALT_SYSTEM 0xcdef0123U
#define RB_POWER_OFF   0x4321fedcU
#define RB_AUTOBOOT    0x01234567U

int reboot(unsigned int command);

#endif
