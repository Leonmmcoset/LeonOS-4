#ifndef LEONOS_INSTALLER_SHA256_H
#define LEONOS_INSTALLER_SHA256_H

#include <stdint.h>

#define INSTALLER_SHA256_HASH_LEN 32U

int installer_files_equal(const char *source, const char *target,
                          uint8_t *out_missing, uint8_t *out_diff);

#endif
