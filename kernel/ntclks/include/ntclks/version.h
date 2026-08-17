/*
 * LeonOS version interface: declares exported system-version metadata.
 * Provides build identity to kernel and userland information services.
 */
#ifndef NTCLKS_VERSION_H
#define NTCLKS_VERSION_H

#include <leonos/system.h>

const struct leonos_system_info *ntclks_system_info(void);

#endif
