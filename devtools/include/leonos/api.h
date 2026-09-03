#ifndef LEONOS_API_H
#define LEONOS_API_H

#include <stdint.h>
#include <leonos/inputm.h>

#define LEONOS_API_PATH_MAX 256U
#define LEONOS_API_NAME_LEN 64U
#define LEONOS_API_VERSION_LEN 16U

struct leonos_api_info {
    char id[64];
    char name[LEONOS_API_NAME_LEN];
    char version[LEONOS_API_VERSION_LEN];
    char category[64];
    char main_exe[LEONOS_API_PATH_MAX];
    char default_path[LEONOS_API_PATH_MAX];
    char icon[LEONOS_API_PATH_MAX];
    uint32_t requires_admin;
    uint32_t desktop_shortcut;
    uint32_t terminal;
    uint32_t hidden;
    uint32_t open_with;
    char commands[256];
    char extensions[256];
    uint32_t input_method;
    char input_method_id[LEONOS_INPUTM_ID_LEN];
    char input_method_abbreviation[LEONOS_INPUTM_ABBREV_LEN];
    uint32_t input_method_startup_mode;
    uint32_t input_method_launch_after_install;
    char input_method_settings[LEONOS_API_PATH_MAX];
    char input_method_settings_app[LEONOS_API_PATH_MAX];
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
