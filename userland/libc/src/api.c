#include <leonos/admin.h>
#include <leonos/api.h>
#include <leonos/auth.h>
#include <leonos/fs.h>
#include <leonos/ini.h>
#include <leonos/launch.h>
#include <leonos/startup.h>
#include <leonos/syscall.h>
#include <leonos/tar.h>
#include <string.h>

#define API_TEMP_PREFIX "0:/tmp/api_install_"
#define API_INI_PATH "install.ini"
#define API_PACKAGE_FORMAT "leonos-api"
#define API_PACKAGE_VERSION "1"
#define API_PROGRAM_DIR "0:/programs"
#define API_PROGRAM_ROOT "0:/programs/"
#define API_SYSTEM_DIR "0:/system"
#define API_SYSTEM_ROOT "0:/system/"

static uint32_t api_temp_sequence;

static int api_text_eq(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    return strcmp(a, b) == 0;
}

static int api_text_starts_with(const char *text, const char *prefix)
{
    uint32_t i = 0;
    if (!text || !prefix) {
        return 0;
    }
    while (prefix[i]) {
        if (text[i] != prefix[i]) {
            return 0;
        }
        ++i;
    }
    return 1;
}

static int api_text_ends_with(const char *text, const char *suffix)
{
    uint32_t text_len;
    uint32_t suffix_len;
    if (!text || !suffix) {
        return 0;
    }
    text_len = (uint32_t)strlen(text);
    suffix_len = (uint32_t)strlen(suffix);
    if (suffix_len > text_len) {
        return 0;
    }
    return strcmp(text + text_len - suffix_len, suffix) == 0;
}

static void api_append_char(char *dst, uint32_t *pos, uint32_t capacity, char ch)
{
    if (!dst || !pos || *pos + 1U >= capacity) {
        return;
    }
    dst[*pos] = ch;
    ++(*pos);
    dst[*pos] = 0;
}

static void api_append_text(char *dst, uint32_t *pos, uint32_t capacity,
                            const char *text)
{
    uint32_t i = 0;
    while (text && text[i]) {
        api_append_char(dst, pos, capacity, text[i]);
        ++i;
    }
}

static void api_append_uint(char *dst, uint32_t *pos, uint32_t capacity,
                            uint32_t value)
{
    char tmp[12];
    uint32_t n = 0;
    if (value == 0) {
        api_append_char(dst, pos, capacity, '0');
        return;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (n) {
        api_append_char(dst, pos, capacity, tmp[--n]);
    }
}

static int api_copy_trim_path(char *dst, uint32_t capacity, const char *src)
{
    uint32_t len;
    if (!dst || capacity == 0 || !src || !src[0]) {
        return 0;
    }
    len = (uint32_t)strlen(src);
    if (len >= capacity) {
        return 0;
    }
    memcpy(dst, src, len + 1U);
    while (len > 3U && dst[len - 1U] == '/') {
        dst[--len] = 0;
    }
    return dst[0] ? 1 : 0;
}

static int api_copy_field(char *dst, uint32_t capacity, const char *src)
{
    uint32_t len;
    if (!dst || capacity == 0 || !src) {
        return 0;
    }
    len = (uint32_t)strlen(src);
    if (len >= capacity) {
        return 0;
    }
    memcpy(dst, src, len + 1U);
    return 1;
}

static int api_join_path(char *out, uint32_t capacity, const char *base,
                         const char *name)
{
    uint32_t base_len;
    uint32_t name_len;
    if (!out || capacity == 0 || !base || !base[0] || !name || !name[0]) {
        return 0;
    }
    base_len = (uint32_t)strlen(base);
    name_len = (uint32_t)strlen(name);
    if (base_len + 1U + name_len >= capacity) {
        return 0;
    }
    memcpy(out, base, base_len);
    if (out[base_len - 1U] != '/') {
        out[base_len++] = '/';
    }
    if (base_len + name_len >= capacity) {
        return 0;
    }
    memcpy(out + base_len, name, name_len);
    out[base_len + name_len] = 0;
    return 1;
}

static int api_is_root_path(const char *path)
{
    return path && path[0] >= '0' && path[0] <= '9' &&
           path[1] == ':' && path[2] == '/' && path[3] == 0;
}

static int api_parent_path(const char *path, char *parent, uint32_t capacity)
{
    uint32_t len;
    uint32_t slash;
    if (!path || !path[0] || !parent || capacity == 0) {
        return 0;
    }
    len = (uint32_t)strlen(path);
    while (len > 0 && path[len - 1U] == '/') {
        --len;
    }
    if (len == 0) {
        return 0;
    }
    slash = len;
    while (slash > 0 && path[slash - 1U] != '/') {
        --slash;
    }
    if (slash == 0 || slash >= capacity) {
        return 0;
    }
    memcpy(parent, path, slash);
    parent[slash] = 0;
    return 1;
}

static int api_ensure_dir(const char *path)
{
    struct leonos_stat st;
    char clean[LEONOS_API_PATH_MAX];
    char parent[LEONOS_API_PATH_MAX];
    if (!api_copy_trim_path(clean, sizeof(clean), path)) {
        return 0;
    }
    if (api_is_root_path(clean)) {
        return 1;
    }
    if (stat(clean, &st) == 0) {
        return st.type == LEONOS_FS_TYPE_DIR ? 1 : 0;
    }
    if (api_parent_path(clean, parent, sizeof(parent)) &&
        !api_ensure_dir(parent)) {
        return 0;
    }
    if (mkdir(clean, 0) == 0) {
        return 1;
    }
    return stat(clean, &st) == 0 && st.type == LEONOS_FS_TYPE_DIR;
}

static int api_component_path_is_safe(const char *path, uint32_t start)
{
    uint32_t i = start;
    uint32_t part_start = start;
    uint32_t part_len = 0;
    if (!path || !path[start]) {
        return 0;
    }
    while (path[i]) {
        char ch = path[i];
        if (ch == ':' || ch == '\\' || (unsigned char)ch < 0x20) {
            return 0;
        }
        if (ch == '/') {
            if (part_len == 0) {
                return 0;
            }
            if ((part_len == 1U && path[part_start] == '.') ||
                (part_len == 2U && path[part_start] == '.' &&
                 path[part_start + 1U] == '.')) {
                return 0;
            }
            part_start = i + 1U;
            part_len = 0;
        } else {
            ++part_len;
        }
        ++i;
    }
    if (part_len == 0) {
        return 0;
    }
    if ((part_len == 1U && path[part_start] == '.') ||
        (part_len == 2U && path[part_start] == '.' &&
         path[part_start + 1U] == '.')) {
        return 0;
    }
    return 1;
}

static int api_path_is_clean_absolute(const char *path)
{
    return path && path[0] >= '0' && path[0] <= '9' &&
           path[1] == ':' && path[2] == '/' &&
           api_component_path_is_safe(path, 3U);
}

static int api_relative_path_is_safe(const char *path)
{
    return path && path[0] && path[0] != '/' &&
           api_component_path_is_safe(path, 0);
}

static int api_default_path_is_allowed(const char *path)
{
    return api_path_is_clean_absolute(path) &&
           api_text_starts_with(path, API_PROGRAM_ROOT);
}

static int api_install_path_requires_admin(const char *path)
{
    return api_text_eq(path, API_PROGRAM_DIR) ||
           api_text_starts_with(path, API_PROGRAM_ROOT) ||
           api_text_eq(path, API_SYSTEM_DIR) ||
           api_text_starts_with(path, API_SYSTEM_ROOT);
}

static int api_bool_value(const char *key, uint32_t *out)
{
    char val[8];
    if (!out || !leonos_ini_get("app", key, val, sizeof(val))) {
        return 0;
    }
    if (api_text_eq(val, "0")) {
        *out = 0;
        return 1;
    }
    if (api_text_eq(val, "1")) {
        *out = 1;
        return 1;
    }
    return 0;
}

static int api_bool_value_in_section(const char *section, const char *key,
                                     uint32_t *out)
{
    char val[8];
    if (!section || !key || !out || !leonos_ini_get(section, key, val, sizeof(val))) {
        return 0;
    }
    if (api_text_eq(val, "0")) {
        *out = 0;
        return 1;
    }
    if (api_text_eq(val, "1")) {
        *out = 1;
        return 1;
    }
    return 0;
}

static int api_input_method_id_is_safe(const char *id)
{
    uint32_t i = 0;
    if (!id || !id[0]) {
        return 0;
    }
    while (id[i]) {
        char ch = id[i++];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) {
            return 0;
        }
    }
    return i < LEONOS_INPUTM_ID_LEN;
}

static int api_validate_info(struct leonos_api_info *info)
{
    if (!info || !info->name[0] || !info->version[0] ||
        !info->main_exe[0] || !info->default_path[0]) {
        return 0;
    }
    if (!api_relative_path_is_safe(info->main_exe) ||
        !api_text_ends_with(info->main_exe, ".elf")) {
        return 0;
    }
    if (!api_default_path_is_allowed(info->default_path)) {
        return 0;
    }
    if (info->icon[0] && !api_relative_path_is_safe(info->icon)) {
        return 0;
    }
    if (info->input_method &&
        (!api_input_method_id_is_safe(info->input_method_id) ||
         !info->input_method_abbreviation[0] ||
         info->input_method_startup_mode > LEONOS_INPUTM_START_ON_DEMAND ||
         (info->input_method_settings[0] &&
          !api_relative_path_is_safe(info->input_method_settings)) ||
         (info->input_method_settings_app[0] &&
          (!api_relative_path_is_safe(info->input_method_settings_app) ||
           !api_text_ends_with(info->input_method_settings_app, ".elf"))))) {
        return 0;
    }
    return 1;
}

static int api_build_temp_dir(char *out, uint32_t capacity)
{
    uint32_t pos = 0;
    int pid = getpid();
    if (!out || capacity == 0) {
        return 0;
    }
    out[0] = 0;
    ++api_temp_sequence;
    if (!api_temp_sequence) {
        api_temp_sequence = 1U;
    }
    api_append_text(out, &pos, capacity, API_TEMP_PREFIX);
    api_append_uint(out, &pos, capacity, pid < 0 ? 0U : (uint32_t)pid);
    api_append_char(out, &pos, capacity, '_');
    api_append_uint(out, &pos, capacity, api_temp_sequence);
    return out[0] && pos + 1U < capacity ? 1 : 0;
}

int leonos_api_parse_info(const char *api_path, struct leonos_api_info *info)
{
    char temp_dir[LEONOS_API_PATH_MAX];
    char ini_path[LEONOS_API_PATH_MAX];
    char format[32];
    char package_version[16];
    char name_value[LEONOS_INI_VALUE_LEN];
    char version_value[LEONOS_INI_VALUE_LEN];
    char main_exe_value[LEONOS_INI_VALUE_LEN];
    char default_path_value[LEONOS_INI_VALUE_LEN];
    char icon_value[LEONOS_INI_VALUE_LEN];
    char input_type[32];
    char input_id[LEONOS_INI_VALUE_LEN];
    char input_abbreviation[LEONOS_INI_VALUE_LEN];
    char input_startup[16];
    char input_settings[LEONOS_INI_VALUE_LEN];
    char input_settings_app[LEONOS_INI_VALUE_LEN];
    int ok = 0;
    if (!api_path || !info) {
        return 0;
    }
    memset(info, 0, sizeof(*info));
    if (!api_build_temp_dir(temp_dir, sizeof(temp_dir)) ||
        !api_join_path(ini_path, sizeof(ini_path), temp_dir, API_INI_PATH) ||
        !api_ensure_dir(temp_dir)) {
        return 0;
    }
    if (!leonos_tar_extract_file(api_path, API_INI_PATH, ini_path)) {
        goto cleanup;
    }
    if (!leonos_ini_load_strict(ini_path)) {
        goto cleanup;
    }
    if (!leonos_ini_get("package", "format", format, sizeof(format)) ||
        !api_text_eq(format, API_PACKAGE_FORMAT) ||
        !leonos_ini_get("package", "version", package_version,
                        sizeof(package_version)) ||
        !api_text_eq(package_version, API_PACKAGE_VERSION)) {
        goto cleanup;
    }
    icon_value[0] = 0;
    if (!leonos_ini_get("app", "name", name_value, sizeof(name_value)) ||
        !leonos_ini_get("app", "version", version_value,
                        sizeof(version_value)) ||
        !leonos_ini_get("app", "main_exe", main_exe_value,
                        sizeof(main_exe_value)) ||
        !leonos_ini_get("app", "default_path", default_path_value,
                        sizeof(default_path_value)) ||
        !api_bool_value("requires_admin", &info->requires_admin) ||
        !api_bool_value("desktop_shortcut", &info->desktop_shortcut)) {
        goto cleanup;
    }
    (void)leonos_ini_get("app", "icon", icon_value, sizeof(icon_value));
    if (!api_copy_field(info->name, sizeof(info->name), name_value) ||
        !api_copy_field(info->version, sizeof(info->version), version_value) ||
        !api_copy_field(info->main_exe, sizeof(info->main_exe),
                        main_exe_value) ||
        !api_copy_field(info->default_path, sizeof(info->default_path),
                        default_path_value) ||
        !api_copy_field(info->icon, sizeof(info->icon), icon_value)) {
        goto cleanup;
    }
    input_type[0] = 0;
    if (leonos_ini_get("input_method", "type", input_type, sizeof(input_type))) {
        if (!api_text_eq(input_type, "input-method") ||
            !leonos_ini_get("input_method", "id", input_id, sizeof(input_id)) ||
            !leonos_ini_get("input_method", "abbreviation", input_abbreviation,
                            sizeof(input_abbreviation)) ||
            !leonos_ini_get("input_method", "startup_mode", input_startup,
                            sizeof(input_startup)) ||
            !api_bool_value_in_section("input_method", "launch_after_install",
                                       &info->input_method_launch_after_install)) {
            goto cleanup;
        }
        input_settings[0] = 0;
        input_settings_app[0] = 0;
        (void)leonos_ini_get("input_method", "settings_schema", input_settings,
                             sizeof(input_settings));
        (void)leonos_ini_get("input_method", "settings_app", input_settings_app,
                             sizeof(input_settings_app));
        info->input_method = 1;
        if (api_text_eq(input_startup, "manual")) {
            info->input_method_startup_mode = LEONOS_INPUTM_START_MANUAL;
        } else if (api_text_eq(input_startup, "login")) {
            info->input_method_startup_mode = LEONOS_INPUTM_START_LOGIN;
        } else if (api_text_eq(input_startup, "on-demand")) {
            info->input_method_startup_mode = LEONOS_INPUTM_START_ON_DEMAND;
        } else {
            goto cleanup;
        }
        if (!api_copy_field(info->input_method_id, sizeof(info->input_method_id), input_id) ||
            !api_copy_field(info->input_method_abbreviation,
                            sizeof(info->input_method_abbreviation), input_abbreviation) ||
            !api_copy_field(info->input_method_settings,
                            sizeof(info->input_method_settings), input_settings) ||
            !api_copy_field(info->input_method_settings_app,
                            sizeof(info->input_method_settings_app), input_settings_app)) {
            goto cleanup;
        }
    }
    if (!api_validate_info(info)) {
        goto cleanup;
    }
    ok = 1;

cleanup:
    unlink(ini_path);
    rmdir(temp_dir);
    if (!ok) {
        memset(info, 0, sizeof(*info));
    }
    return ok;
}

static uint32_t api_parse_u32(const char *text, uint32_t fallback)
{
    uint32_t value = 0;
    uint32_t digits = 0;
    while (text && *text >= '0' && *text <= '9') {
        uint32_t digit = (uint32_t)(*text - '0');
        if (value > (UINT32_MAX - digit) / 10U) {
            return fallback;
        }
        value = value * 10U + digit;
        ++text;
        ++digits;
    }
    return digits ? value : fallback;
}

/* Return the last exact key assignment so append-only user config remains valid. */
static int api_config_get(const char *config, const char *key,
                          char *value, uint32_t capacity)
{
    uint32_t key_len = (uint32_t)strlen(key);
    uint32_t pos = 0;
    uint8_t found = 0;
    if (!config || !key || !key_len || !value || capacity == 0) {
        return 0;
    }
    value[0] = 0;
    while (config[pos]) {
        uint32_t start = pos;
        uint32_t end;
        uint32_t out = 0;
        while (config[pos] && config[pos] != '\n' && config[pos] != '\r') {
            ++pos;
        }
        end = pos;
        while (config[pos] == '\n' || config[pos] == '\r') {
            ++pos;
        }
        if (end <= start + key_len || config[start + key_len] != '=') {
            continue;
        }
        for (uint32_t i = 0; i < key_len; ++i) {
            if (config[start + i] != key[i]) {
                goto next_line;
            }
        }
        start += key_len + 1U;
        while (start < end && out + 1U < capacity) {
            value[out++] = config[start++];
        }
        value[out] = 0;
        found = 1;
next_line:
        ;
    }
    return found;
}

static void api_inputm_provider_key(char *key, uint32_t capacity,
                                    uint32_t index, const char *field)
{
    uint32_t pos = 0;
    if (!key || capacity == 0) {
        return;
    }
    key[0] = 0;
    api_append_text(key, &pos, capacity, "provider");
    api_append_uint(key, &pos, capacity, index);
    api_append_char(key, &pos, capacity, '_');
    api_append_text(key, &pos, capacity, field);
}

static int api_append_input_method_config(const struct leonos_api_info *info,
                                          const char *exe_path)
{
    struct leonos_user_info user = {0};
    char path[LEONOS_FS_PATH_LEN];
    char config[2048];
    char line[LEONOS_FS_PATH_LEN * 3U + 256U];
    char value[LEONOS_FS_PATH_LEN];
    char settings_path[LEONOS_FS_PATH_LEN];
    char settings_app_path[LEONOS_FS_PATH_LEN];
    char install_dir[LEONOS_FS_PATH_LEN];
    uint32_t home_len;
    uint32_t count = 0;
    uint32_t index;
    uint32_t pos = 0;
    int fd;
    long got;
    if (!info || !info->input_method || !exe_path ||
        leonos_auth_current(&user) != 0 || !user.uid || !user.home[0]) {
        return 0;
    }
    home_len = (uint32_t)strlen(user.home);
    if (home_len + 14U >= sizeof(path)) {
        return 0;
    }
    memcpy(path, user.home, home_len);
    memcpy(path + home_len, "/.inputm.conf", 14U);
    config[0] = 0;
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd >= 0) {
        got = read(fd, config, sizeof(config) - 1U);
        close(fd);
        if (got > 0) {
            config[got] = 0;
        } else {
            config[0] = 0;
        }
    }
    if (api_config_get(config, "provider_count", value, sizeof(value))) {
        count = api_parse_u32(value, LEONOS_INPUTM_MAX_PROVIDERS);
    }
    line[0] = 0;
    for (uint32_t i = 0; i < count; ++i) {
        char key[48];
        api_inputm_provider_key(key, sizeof(key), i, "id");
        if (api_config_get(config, key, value, sizeof(value)) &&
            api_text_eq(value, info->input_method_id)) {
            index = i;
            goto append_provider;
        }
    }
    if (count >= LEONOS_INPUTM_MAX_PROVIDERS) {
        return 0;
    }
    index = count;
    api_append_text(line, &pos, sizeof(line), "\nprovider_count=");
    api_append_uint(line, &pos, sizeof(line), count + 1U);

append_provider:
    api_append_text(line, &pos, sizeof(line), "\nprovider");
    api_append_uint(line, &pos, sizeof(line), index);
    api_append_text(line, &pos, sizeof(line), "_id=");
    api_append_text(line, &pos, sizeof(line), info->input_method_id);
    api_append_text(line, &pos, sizeof(line), "\nprovider");
    api_append_uint(line, &pos, sizeof(line), index);
    api_append_text(line, &pos, sizeof(line), "_path=");
    api_append_text(line, &pos, sizeof(line), exe_path);
    if (index == count) {
        api_append_text(line, &pos, sizeof(line), "\nprovider");
        api_append_uint(line, &pos, sizeof(line), index);
        api_append_text(line, &pos, sizeof(line), "_enabled=1\nprovider");
        api_append_uint(line, &pos, sizeof(line), index);
        api_append_text(line, &pos, sizeof(line), "_startup=");
        api_append_uint(line, &pos, sizeof(line), info->input_method_startup_mode);
        api_append_text(line, &pos, sizeof(line), "\nprovider");
        api_append_uint(line, &pos, sizeof(line), index);
        api_append_text(line, &pos, sizeof(line), "_order=");
        api_append_uint(line, &pos, sizeof(line), count + 1U);
    }
    if ((info->input_method_settings[0] || info->input_method_settings_app[0]) &&
        !api_parent_path(exe_path, install_dir, sizeof(install_dir))) {
        return 0;
    }
    if (info->input_method_settings[0] &&
        !api_join_path(settings_path, sizeof(settings_path), install_dir,
                       info->input_method_settings)) {
        return 0;
    }
    if (info->input_method_settings_app[0] &&
        !api_join_path(settings_app_path, sizeof(settings_app_path), install_dir,
                       info->input_method_settings_app)) {
        return 0;
    }
    if (info->input_method_settings[0]) {
        api_append_text(line, &pos, sizeof(line), "\nprovider");
        api_append_uint(line, &pos, sizeof(line), index);
        api_append_text(line, &pos, sizeof(line), "_settings=");
        api_append_text(line, &pos, sizeof(line), settings_path);
    }
    if (info->input_method_settings_app[0]) {
        api_append_text(line, &pos, sizeof(line), "\nprovider");
        api_append_uint(line, &pos, sizeof(line), index);
        api_append_text(line, &pos, sizeof(line), "_settings_app=");
        api_append_text(line, &pos, sizeof(line), settings_app_path);
    }
    api_append_char(line, &pos, sizeof(line), '\n');
    if (pos == 0 || pos + 1U >= sizeof(line)) {
        return 0;
    }
    fd = open(path, LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_APPEND, 0);
    if (fd < 0) {
        return 0;
    }
    got = write(fd, line, pos);
    close(fd);
    return got == (long)pos;
}

static int api_install_input_method(const struct leonos_api_info *info,
                                    const char *exe_path)
{
    struct leonos_user_info user = {0};
    char *argv[2];
    if (!info || !info->input_method ||
        !api_append_input_method_config(info, exe_path)) {
        return 0;
    }
    if (leonos_auth_current(&user) == 0 && user.uid) {
        (void)leonos_inputm_notify_config(user.uid);
    }
    if (info->input_method_startup_mode == LEONOS_INPUTM_START_LOGIN) {
        struct leonos_startup_command command = {0};
        uint32_t request_id;
        if (strlen(exe_path) >= sizeof(command.path)) {
            return 0;
        }
        memcpy(command.path, exe_path, strlen(exe_path) + 1U);
        (void)leonos_startup_request(&command, &request_id);
    }
    if (!info->input_method_launch_after_install) {
        return 1;
    }
    argv[0] = (char *)exe_path;
    argv[1] = 0;
    return execve(exe_path, argv, 0) > 0;
}

static int api_extract_tar(const char *api_path, const char *dest_dir,
                           leonos_api_progress_fn progress, void *context)
{
    char manifest_path[LEONOS_API_PATH_MAX];
    int ok;
    if (!api_path || !dest_dir) {
        return 0;
    }
    ok = leonos_tar_extract_all_with_progress(api_path, dest_dir, progress,
                                              context);
    if (ok && api_join_path(manifest_path, sizeof(manifest_path), dest_dir,
                            API_INI_PATH)) {
        unlink(manifest_path);
    }
    return ok;
}

int leonos_api_extract_files(const char *api_path, const char *dest_dir)
{
    struct leonos_api_info info;
    char install_root[LEONOS_API_PATH_MAX];
    if (!api_path || !dest_dir || !dest_dir[0]) {
        return 0;
    }
    if (!leonos_api_parse_info(api_path, &info)) {
        return 0;
    }
    if (!api_copy_trim_path(install_root, sizeof(install_root), dest_dir) ||
        !api_path_is_clean_absolute(install_root)) {
        return 0;
    }
    if ((info.requires_admin || api_install_path_requires_admin(install_root)) &&
        !leonos_admin_elevate()) {
        return 0;
    }
    if (!api_ensure_dir(install_root)) {
        return 0;
    }
    return api_extract_tar(api_path, install_root, 0, 0);
}

int leonos_api_install_with_progress(const char *api_path, const char *dest_dir,
                                     uint32_t create_shortcut,
                                     leonos_api_progress_fn progress,
                                     void *context)
{
    struct leonos_api_info info;
    struct leonos_user_info user;
    struct leonos_stat exe_stat;
    char install_root[LEONOS_API_PATH_MAX];
    char exe_path[LEONOS_API_PATH_MAX];
    char shortcut_name[LEONOS_FS_PATH_LEN];
    if (!api_path || !dest_dir || !dest_dir[0]) {
        return 0;
    }
    if (!leonos_api_parse_info(api_path, &info)) {
        return 0;
    }
    if (!api_copy_trim_path(install_root, sizeof(install_root), dest_dir) ||
        !api_path_is_clean_absolute(install_root)) {
        return 0;
    }
    if ((info.requires_admin || api_install_path_requires_admin(install_root)) &&
        !leonos_admin_elevate()) {
        return 0;
    }
    if (!api_ensure_dir(install_root)) {
        return 0;
    }
    if (!api_extract_tar(api_path, install_root, progress, context)) {
        return 0;
    }
    if (!api_join_path(exe_path, sizeof(exe_path), install_root,
                       info.main_exe) ||
        stat(exe_path, &exe_stat) != 0 ||
        exe_stat.type != LEONOS_FS_TYPE_FILE) {
        return 0;
    }
    if (info.input_method && !api_install_input_method(&info, exe_path)) {
        return 0;
    }
    if (create_shortcut && info.main_exe[0]) {
        if (leonos_auth_current(&user) == 0 && user.uid && user.home[0]) {
            char desktop_dir[LEONOS_FS_PATH_LEN];
            uint32_t hlen = (uint32_t)strlen(user.home);
            if (hlen + 8U < sizeof(desktop_dir)) {
                memcpy(desktop_dir, user.home, hlen);
                memcpy(desktop_dir + hlen, "/desktop", 9U);
                api_ensure_dir(desktop_dir);
                if (leonos_launch_create_shortcut_in_dir(
                        desktop_dir, exe_path,
                        shortcut_name,
                        sizeof(shortcut_name)) < 0) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

int leonos_api_install(const char *api_path, const char *dest_dir,
                       uint32_t create_shortcut)
{
    return leonos_api_install_with_progress(api_path, dest_dir,
                                            create_shortcut, 0, 0);
}
