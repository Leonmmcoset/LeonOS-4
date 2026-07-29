#ifndef LEONOS_INI_H
#define LEONOS_INI_H

#include <stdint.h>

#define LEONOS_INI_MAX_SIZE (64U * 1024U)
#define LEONOS_INI_MAX_SECTIONS 32U
#define LEONOS_INI_MAX_KEYS_PER_SECTION 64U
#define LEONOS_INI_NAME_LEN 64U
#define LEONOS_INI_VALUE_LEN 256U

int leonos_ini_load(const char *path);
int leonos_ini_load_strict(const char *path);
int leonos_ini_get(const char *section, const char *key,
                   char *value, uint32_t capacity);
int leonos_ini_section_count(void);
int leonos_ini_section_name(uint32_t index, char *name, uint32_t capacity);
int leonos_ini_key_count(const char *section);
int leonos_ini_key_name(const char *section, uint32_t index,
                        char *name, uint32_t capacity);

#endif
