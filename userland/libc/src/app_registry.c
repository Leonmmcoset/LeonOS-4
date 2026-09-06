#include <leonos/app.h>
#include <leonos/fs.h>
#include <leonos/ini.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define APP_ROOT_SYSTEM "/system/apps"
#define APP_ROOT_PROGRAMS "/programs"

static struct leonos_app_info registry[LEONOS_APP_REGISTRY_MAX];
static uint32_t registry_count;
static uint8_t registry_loaded;
static uint8_t registry_scanning;
static uint8_t registry_root_index;
static int registry_scan_fd = -1;
static int registry_scan_error;
static struct leonos_dir_entry registry_scan_entry;

static const char *const registry_roots[] = {
    APP_ROOT_SYSTEM,
    APP_ROOT_PROGRAMS,
};

static void copy_text(char *dst, uint32_t capacity, const char *src)
{
    uint32_t i = 0;
    if (!dst || capacity == 0) {
        return;
    }
    if (src) {
        while (i + 1U < capacity && src[i]) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = 0;
}

static int text_eq(const char *left, const char *right)
{
    if (!left || !right) {
        return 0;
    }
    while (*left && *right && *left == *right) {
        ++left;
        ++right;
    }
    return *left == 0 && *right == 0;
}

static int text_eq_ignore_case(const char *left, const char *right)
{
    if (!left || !right) {
        return 0;
    }
    while (*left && *right) {
        char a = *left++;
        char b = *right++;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return *left == 0 && *right == 0;
}

static uint32_t text_len(const char *text)
{
    uint32_t len = 0;
    while (text && text[len]) ++len;
    return len;
}

static int append_text(char *dst, uint32_t capacity, const char *src)
{
    uint32_t len;
    uint32_t src_len;
    if (!dst || capacity == 0) return 0;
    len = text_len(dst);
    src_len = text_len(src);
    if (len + src_len + 1U > capacity) return 0;
    if (src_len) memcpy(dst + len, src, src_len);
    dst[len + src_len] = 0;
    return 1;
}

static int join_path(char *dst, uint32_t capacity, const char *base,
                     const char *name)
{
    uint32_t base_len;
    if (!dst || capacity == 0 || !base || !name || !name[0]) return 0;
    base_len = text_len(base);
    if (base_len + 1U + text_len(name) + 1U > capacity) return 0;
    memcpy(dst, base, base_len);
    dst[base_len] = '/';
    copy_text(dst + base_len + 1U, capacity - base_len - 1U, name);
    return 1;
}

static int bool_value(const char *value, int fallback)
{
    if (!value || !value[0]) return fallback;
    if (text_eq(value, "1") || text_eq_ignore_case(value, "true") ||
        text_eq_ignore_case(value, "yes")) return 1;
    if (text_eq(value, "0") || text_eq_ignore_case(value, "false") ||
        text_eq_ignore_case(value, "no")) return 0;
    return fallback;
}

static int read_key(const char *manifest, const char *key, char *value,
                    uint32_t capacity)
{
    if (!manifest || !key || !value || capacity == 0 ||
        !leonos_ini_load_strict(manifest) ||
        !leonos_ini_get("app", key, value, capacity)) {
        if (value && capacity) value[0] = 0;
        return 0;
    }
    return value[0] != 0;
}

static int manifest_exists(const char *path)
{
    struct leonos_stat st;
    return path && stat(path, &st) == 0 && st.type == LEONOS_FS_TYPE_FILE;
}

/* Manifest paths are package metadata, not arbitrary filesystem paths.  Keep
 * relative values confined to the package directory so an installed package
 * cannot redirect the launcher or icon lookup outside its own tree. */
static int relative_path_is_safe(const char *path)
{
    if (!path || !path[0] || path[0] == '/' || path[0] == '\\' ||
        path[0] == ':') {
        return 0;
    }
    {
        const char *start = path;
        while (*start) {
            const char *end = start;
            while (*end && *end != '/') ++end;
            if (end == start) return 0;
            for (const char *ch = start; ch < end; ++ch) {
                if ((unsigned char)*ch < 0x20U || *ch == '\\' || *ch == ':') {
                    return 0;
                }
            }
            if ((end - start == 1 && start[0] == '.') ||
                (end - start == 2 && start[0] == '.' && start[1] == '.')) {
                return 0;
            }
            start = *end ? end + 1 : end;
        }
    }
    return 1;
}

static int absolute_path_is_in_package(const char *path, const char *package_dir)
{
    uint32_t dir_len;
    if (!path || !package_dir || path[0] != '/' || !package_dir[0]) return 0;
    dir_len = text_len(package_dir);
    return text_len(path) > dir_len &&
           memcmp(path, package_dir, dir_len) == 0 && path[dir_len] == '/';
}

static int app_has_id(const char *id)
{
    for (uint32_t i = 0; i < registry_count; ++i) {
        if (text_eq_ignore_case(registry[i].id, id)) return 1;
    }
    return 0;
}

static int list_contains(const char *list, const char *wanted)
{
    char item[LEONOS_APP_ID_LEN];
    uint32_t pos = 0;
    uint32_t out = 0;
    if (!list || !wanted || !wanted[0]) return 0;
    while (1) {
        char ch = list[pos++];
        if (ch == ',' || ch == 0) {
            while (out && (item[out - 1U] == ' ' || item[out - 1U] == '\t')) --out;
            item[out] = 0;
            if (text_eq_ignore_case(item, wanted)) return 1;
            out = 0;
            if (ch == 0) break;
            continue;
        }
        if (out + 1U < sizeof(item) && ch != ' ' && ch != '\t') item[out++] = ch;
    }
    return 0;
}

static int derive_icon(char *icon, uint32_t capacity, const char *exec,
                       const char *package_dir, const char *manifest_icon)
{
    if (manifest_icon && manifest_icon[0]) {
        if (manifest_icon[0] == '/') copy_text(icon, capacity, manifest_icon);
        else if (!join_path(icon, capacity, package_dir, manifest_icon)) return 0;
        return manifest_exists(icon);
    }
    copy_text(icon, capacity, exec);
    if (text_len(icon) < 4U || !text_eq_ignore_case(icon + text_len(icon) - 4U, ".elf")) {
        icon[0] = 0;
        return 0;
    }
    icon[text_len(icon) - 3U] = 'b';
    icon[text_len(icon) - 2U] = 'm';
    icon[text_len(icon) - 1U] = 'p';
    return manifest_exists(icon);
}

static int add_package(const char *root, const char *package)
{
    struct leonos_app_info info;
    struct leonos_stat st;
    char package_dir[LEONOS_APP_PATH_LEN];
    char manifest[LEONOS_APP_PATH_LEN];
    char legacy_manifest[LEONOS_APP_PATH_LEN];
    char value[LEONOS_APP_LIST_LEN];
    char fallback_exec[LEONOS_APP_PATH_LEN];
    const char *manifest_path = 0;
    uint32_t package_len;
    if (!root || !package || !package[0] || registry_count >= LEONOS_APP_REGISTRY_MAX ||
        !join_path(package_dir, sizeof(package_dir), root, package)) return 0;
    if (!join_path(manifest, sizeof(manifest), package_dir, "manifest.ini")) return 0;
    if (manifest_exists(manifest)) manifest_path = manifest;
    if (!manifest_path) {
        uint32_t pos = text_len(package_dir);
        uint32_t legacy_package_len = text_len(package);
        if (pos + 1U + legacy_package_len + 9U + 1U <= sizeof(legacy_manifest)) {
            memcpy(legacy_manifest, package_dir, pos);
            legacy_manifest[pos++] = '/';
            memcpy(legacy_manifest + pos, package, legacy_package_len);
            pos += legacy_package_len;
            memcpy(legacy_manifest + pos, ".app.ini", 9U);
            pos += 9U;
            legacy_manifest[pos] = 0;
            if (manifest_exists(legacy_manifest)) manifest_path = legacy_manifest;
        }
    }
    memset(&info, 0, sizeof(info));
    copy_text(info.id, sizeof(info.id), package);
    copy_text(info.name, sizeof(info.name), package);
    copy_text(info.version, sizeof(info.version), "system");
    copy_text(info.category, sizeof(info.category), "Applications");
    info.flags = text_eq(root, APP_ROOT_SYSTEM) ? LEONOS_APP_FLAG_SYSTEM : 0U;
    package_len = text_len(package);
    if (package_len + 5U >= sizeof(fallback_exec) ||
        !join_path(fallback_exec, sizeof(fallback_exec), package_dir, package)) return 0;
    append_text(fallback_exec, sizeof(fallback_exec), ".elf");
    copy_text(info.exec, sizeof(info.exec), fallback_exec);
    if (manifest_path) {
        if (read_key(manifest_path, "id", value, sizeof(value))) copy_text(info.id, sizeof(info.id), value);
        if (read_key(manifest_path, "name", value, sizeof(value))) copy_text(info.name, sizeof(info.name), value);
        if (read_key(manifest_path, "version", value, sizeof(value))) copy_text(info.version, sizeof(info.version), value);
        if (read_key(manifest_path, "category", value, sizeof(value))) copy_text(info.category, sizeof(info.category), value);
        if (read_key(manifest_path, "exec", value, sizeof(value))) {
            if (value[0] == '/') {
                if (absolute_path_is_in_package(value, package_dir)) {
                    copy_text(info.exec, sizeof(info.exec), value);
                }
            } else if (relative_path_is_safe(value)) {
                (void)join_path(info.exec, sizeof(info.exec), package_dir, value);
            }
        }
        if (read_key(manifest_path, "commands", value, sizeof(value))) copy_text(info.commands, sizeof(info.commands), value);
        if (read_key(manifest_path, "extensions", value, sizeof(value))) copy_text(info.extensions, sizeof(info.extensions), value);
        if (read_key(manifest_path, "entry", value, sizeof(value)) && bool_value(value, 1)) info.flags |= LEONOS_APP_FLAG_ENTRY;
        if (read_key(manifest_path, "terminal", value, sizeof(value)) && bool_value(value, 0)) info.flags |= LEONOS_APP_FLAG_TERMINAL;
        if (read_key(manifest_path, "hidden", value, sizeof(value)) && bool_value(value, 0)) info.flags |= LEONOS_APP_FLAG_HIDDEN;
        if (read_key(manifest_path, "open_with", value, sizeof(value)) && bool_value(value, 0)) info.flags |= LEONOS_APP_FLAG_OPEN_WITH;
        value[0] = 0;
        if (read_key(manifest_path, "icon", value, sizeof(value)) &&
            ((value[0] == '/' && absolute_path_is_in_package(value, package_dir)) ||
             (value[0] != '/' && relative_path_is_safe(value)))) {
            (void)derive_icon(info.icon, sizeof(info.icon), info.exec, package_dir, value);
        }
    }
    if (!info.icon[0]) derive_icon(info.icon, sizeof(info.icon), info.exec, package_dir, 0);
    if (stat(info.exec, &st) != 0 || st.type != LEONOS_FS_TYPE_FILE || app_has_id(info.id)) return 0;
    if (!info.commands[0]) copy_text(info.commands, sizeof(info.commands), info.id);
    registry[registry_count++] = info;
    return 1;
}

int leonos_app_registry_begin_refresh(void)
{
    if (registry_scan_fd >= 0) {
        close(registry_scan_fd);
        registry_scan_fd = -1;
    }
    registry_count = 0;
    registry_loaded = 0;
    registry_scanning = 1;
    registry_root_index = 0;
    registry_scan_error = 0;
    return 0;
}

int leonos_app_registry_refresh_step(uint32_t budget)
{
    if (registry_loaded) return 0;
    if (!registry_scanning) return registry_scan_error < 0 ? registry_scan_error : -1;
    if (budget == 0) budget = 1;
    while (budget) {
        int ret;
        if (registry_scan_fd < 0) {
            while (registry_root_index < sizeof(registry_roots) / sizeof(registry_roots[0])) {
                registry_scan_fd = open(registry_roots[registry_root_index], LEONOS_O_RDONLY, 0);
                if (registry_scan_fd >= 0) break;
                if (registry_scan_fd != -ENOENT) {
                    registry_scan_error = registry_scan_fd;
                    registry_scanning = 0;
                    return registry_scan_error;
                }
                ++registry_root_index;
            }
            if (registry_scan_fd < 0) {
                registry_scanning = 0;
                registry_loaded = 1;
                return 0;
            }
        }
        ret = leonos_readdir(registry_scan_fd, &registry_scan_entry);
        if (ret < 0) {
            close(registry_scan_fd);
            registry_scan_fd = -1;
            registry_scan_error = ret;
            registry_scanning = 0;
            return ret;
        }
        if (ret == 0) {
            close(registry_scan_fd);
            registry_scan_fd = -1;
            ++registry_root_index;
            continue;
        }
        if (registry_scan_entry.type == LEONOS_FS_TYPE_DIR) {
            (void)add_package(registry_roots[registry_root_index],
                              registry_scan_entry.name);
        }
        --budget;
    }
    return 1;
}

int leonos_app_registry_refresh(void)
{
    int ret;
    leonos_app_registry_begin_refresh();
    do {
        ret = leonos_app_registry_refresh_step(LEONOS_FS_MAX_ENTRIES);
    } while (ret > 0);
    return ret;
}

static int ensure_registry(void)
{
    if (registry_scanning) return 0;
    return registry_loaded ? 0 : leonos_app_registry_refresh();
}

int leonos_app_registry_is_loading(void)
{
    return registry_scanning != 0;
}

int leonos_app_registry_is_loaded(void)
{
    return registry_loaded != 0;
}

uint32_t leonos_app_registry_count(void)
{
    if (ensure_registry() < 0) return 0;
    return registry_count;
}

int leonos_app_registry_get(uint32_t index, struct leonos_app_info *info)
{
    if (!info || ensure_registry() < 0 || index >= registry_count) return -ENOENT;
    *info = registry[index];
    return 0;
}

int leonos_app_registry_find(const char *id_or_path, struct leonos_app_info *info)
{
    if (!id_or_path || !id_or_path[0] || ensure_registry() < 0) return -EINVAL;
    for (uint32_t i = 0; i < registry_count; ++i) {
        struct leonos_app_info *candidate = &registry[i];
        if (text_eq_ignore_case(candidate->id, id_or_path) ||
            text_eq(candidate->exec, id_or_path) ||
            list_contains(candidate->commands, id_or_path)) {
            if (info) *info = *candidate;
            return 0;
        }
    }
    return -ENOENT;
}

int leonos_app_registry_resolve(const char *name_or_path, char *path,
                                uint32_t capacity)
{
    struct leonos_app_info info;
    uint32_t length;
    if (!path || capacity == 0 || !name_or_path || !name_or_path[0]) return -EINVAL;
    path[0] = 0;
    if (leonos_app_registry_find(name_or_path, &info) < 0) return -ENOENT;
    length = text_len(info.exec);
    if (length + 1U > capacity) return -ENAMETOOLONG;
    copy_text(path, capacity, info.exec);
    return path[0] ? 0 : -ENOENT;
}

int leonos_app_registry_label(const char *path, char *label, uint32_t capacity)
{
    struct leonos_app_info info;
    if (!label || capacity == 0) return -EINVAL;
    label[0] = 0;
    if (leonos_app_registry_find(path, &info) < 0) return -ENOENT;
    copy_text(label, capacity, info.name);
    return 0;
}

int leonos_app_registry_icon(const char *path, char *icon, uint32_t capacity)
{
    struct leonos_app_info info;
    if (!icon || capacity == 0) return -EINVAL;
    icon[0] = 0;
    if (leonos_app_registry_find(path, &info) < 0 || !info.icon[0]) return -ENOENT;
    copy_text(icon, capacity, info.icon);
    return 0;
}

int leonos_app_registry_default_for_extension(const char *extension,
                                              char *path, uint32_t capacity)
{
    char wanted[32];
    if (!extension || !path || capacity == 0) return -EINVAL;
    path[0] = 0;
    if (ensure_registry() < 0) return -ENOENT;
    copy_text(wanted, sizeof(wanted), extension);
    if (wanted[0] != '.') {
        char prefixed[sizeof(wanted)];
        prefixed[0] = '.';
        copy_text(prefixed + 1U, sizeof(prefixed) - 1U, wanted);
        copy_text(wanted, sizeof(wanted), prefixed);
    }
    for (uint32_t i = 0; i < registry_count; ++i) {
        if ((registry[i].flags & LEONOS_APP_FLAG_OPEN_WITH) &&
            list_contains(registry[i].extensions, wanted)) {
            copy_text(path, capacity, registry[i].exec);
            return path[0] && text_len(registry[i].exec) < capacity ? 0 : -ENAMETOOLONG;
        }
    }
    return -ENOENT;
}
