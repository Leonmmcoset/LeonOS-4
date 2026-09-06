#ifndef LEONOS_APP_H
#define LEONOS_APP_H

#include <stdint.h>

/* The registry is intentionally bounded: a LeonOS image currently exposes a
 * small number of application directories, while the API remains independent
 * of the build-time component list. */
#define LEONOS_APP_REGISTRY_MAX 128U
#define LEONOS_APP_ID_LEN 64U
#define LEONOS_APP_NAME_LEN 96U
#define LEONOS_APP_VERSION_LEN 32U
#define LEONOS_APP_CATEGORY_LEN 64U
#define LEONOS_APP_PATH_LEN 256U
#define LEONOS_APP_LIST_LEN 256U

#define LEONOS_APP_FLAG_ENTRY 0x00000001U
#define LEONOS_APP_FLAG_TERMINAL 0x00000002U
#define LEONOS_APP_FLAG_SYSTEM 0x00000004U
#define LEONOS_APP_FLAG_HIDDEN 0x00000008U
#define LEONOS_APP_FLAG_OPEN_WITH 0x00000010U

struct leonos_app_info {
    char id[LEONOS_APP_ID_LEN];
    char name[LEONOS_APP_NAME_LEN];
    char version[LEONOS_APP_VERSION_LEN];
    char category[LEONOS_APP_CATEGORY_LEN];
    char exec[LEONOS_APP_PATH_LEN];
    char icon[LEONOS_APP_PATH_LEN];
    char commands[LEONOS_APP_LIST_LEN];
    char extensions[LEONOS_APP_LIST_LEN];
    uint32_t flags;
};

int leonos_app_registry_refresh(void);
int leonos_app_registry_begin_refresh(void);
int leonos_app_registry_refresh_step(uint32_t budget);
int leonos_app_registry_is_loading(void);
int leonos_app_registry_is_loaded(void);
uint32_t leonos_app_registry_count(void);
int leonos_app_registry_get(uint32_t index, struct leonos_app_info *info);
int leonos_app_registry_find(const char *id_or_path,
                             struct leonos_app_info *info);
int leonos_app_registry_resolve(const char *name_or_path,
                                char *path, uint32_t capacity);
int leonos_app_registry_label(const char *path, char *label, uint32_t capacity);
int leonos_app_registry_icon(const char *path, char *icon, uint32_t capacity);
int leonos_app_registry_default_for_extension(const char *extension,
                                              char *path, uint32_t capacity);

#endif
