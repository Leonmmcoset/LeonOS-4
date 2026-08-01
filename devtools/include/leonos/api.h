#ifndef LEONOS_API_H
#define LEONOS_API_H

#include <stdint.h>

#define LEONOS_API_PATH_MAX 256U
#define LEONOS_API_NAME_LEN 64U
#define LEONOS_API_VERSION_LEN 16U

struct leonos_api_info {
    char name[LEONOS_API_NAME_LEN];
    char version[LEONOS_API_VERSION_LEN];
    char main_exe[LEONOS_API_PATH_MAX];
    char default_path[LEONOS_API_PATH_MAX];
    char icon[LEONOS_API_PATH_MAX];
    uint32_t requires_admin;
    uint32_t desktop_shortcut;
};

typedef int (*leonos_api_progress_fn)(uint32_t processed, uint32_t total,
                                      void *context);

int leonos_api_parse_info(const char *api_path, struct leonos_api_info *info);
int leonos_api_extract_files(const char *api_path, const char *dest_dir);
int leonos_api_install(const char *api_path, const char *dest_dir,
                       uint32_t create_shortcut);
int leonos_api_install_with_progress(const char *api_path,
                                     const char *dest_dir,
                                     uint32_t create_shortcut,
                                     leonos_api_progress_fn progress,
                                     void *context);

#endif
