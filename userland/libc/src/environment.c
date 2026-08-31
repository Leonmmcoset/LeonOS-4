#include <leonos/auth.h>
#include <leonos/environment.h>
#include <leonos/syscall.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#define LEONOS_ENV_GLOBAL_PATH "/system/config/environment.conf"
#define LEONOS_ENV_USER_SUFFIX "/.environment"

extern char **environ;

struct environment_list {
    char *items[LEONOS_ENV_MAX_ENTRIES];
    uint32_t count;
};

static uint32_t env_text_len(const char *text)
{
    uint32_t length = 0;
    while (text && text[length]) {
        ++length;
    }
    return length;
}

static void env_list_free(struct environment_list *list)
{
    if (!list) {
        return;
    }
    for (uint32_t i = 0; i < list->count; ++i) {
        free(list->items[i]);
        list->items[i] = 0;
    }
    list->count = 0;
}

static int env_name_valid(const char *name, uint32_t length)
{
    if (!name || length == 0 || length >= LEONOS_ENV_MAX_ENTRY_LEN) {
        return 0;
    }
    if (!((name[0] >= 'A' && name[0] <= 'Z') ||
          (name[0] >= 'a' && name[0] <= 'z') || name[0] == '_')) {
        return 0;
    }
    for (uint32_t i = 1; i < length; ++i) {
        char ch = name[i];
        if (!((ch >= 'A' && ch <= 'Z') ||
              (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '_')) {
            return 0;
        }
    }
    return 1;
}

static int env_item_name_length(const char *item, uint32_t *out_length)
{
    const char *equals;
    uint32_t length;
    if (!item || !out_length) {
        return 0;
    }
    equals = strchr(item, '=');
    if (!equals) {
        return 0;
    }
    length = (uint32_t)(equals - item);
    if (!env_name_valid(item, length)) {
        return 0;
    }
    *out_length = length;
    return 1;
}

static int env_item_matches(const char *item, const char *name,
                            uint32_t name_length)
{
    uint32_t item_name_length = 0;
    return env_item_name_length(item, &item_name_length) &&
           item_name_length == name_length &&
           memcmp(item, name, name_length) == 0;
}

static int env_list_set_item(struct environment_list *list, const char *item)
{
    uint32_t name_length = 0;
    uint32_t item_length;
    char *copy;
    if (!list || !item || !env_item_name_length(item, &name_length)) {
        return -1;
    }
    item_length = env_text_len(item);
    if (item_length == 0 || item_length >= LEONOS_ENV_MAX_ENTRY_LEN) {
        return -1;
    }
    copy = (char *)malloc(item_length + 1U);
    if (!copy) {
        return -12;
    }
    memcpy(copy, item, item_length + 1U);
    for (uint32_t i = 0; i < list->count; ++i) {
        if (env_item_matches(list->items[i], item, name_length)) {
            free(list->items[i]);
            list->items[i] = copy;
            return 0;
        }
    }
    if (list->count >= LEONOS_ENV_MAX_ENTRIES) {
        free(copy);
        return -7;
    }
    list->items[list->count++] = copy;
    return 0;
}

static int env_list_has(const struct environment_list *list, const char *name)
{
    uint32_t name_length;
    if (!list || !name) {
        return 0;
    }
    name_length = env_text_len(name);
    if (!env_name_valid(name, name_length)) {
        return 0;
    }
    for (uint32_t i = 0; i < list->count; ++i) {
        if (env_item_matches(list->items[i], name, name_length)) {
            return 1;
        }
    }
    return 0;
}

static int env_list_set(struct environment_list *list, const char *name,
                        const char *value)
{
    uint32_t name_length;
    uint32_t value_length;
    uint32_t total_length;
    char *item;
    int result;
    if (!list || !name || !value) {
        return -1;
    }
    name_length = env_text_len(name);
    value_length = env_text_len(value);
    if (!env_name_valid(name, name_length) ||
        strchr(value, '\n') || strchr(value, '\r') ||
        name_length + value_length + 2U > LEONOS_ENV_MAX_ENTRY_LEN) {
        return -1;
    }
    total_length = name_length + value_length + 2U;
    item = (char *)malloc(total_length);
    if (!item) {
        return -12;
    }
    memcpy(item, name, name_length);
    item[name_length] = '=';
    memcpy(item + name_length + 1U, value, value_length + 1U);
    result = env_list_set_item(list, item);
    free(item);
    return result;
}

static int env_list_unset(struct environment_list *list, const char *name)
{
    uint32_t name_length;
    if (!list || !name) {
        return -1;
    }
    name_length = env_text_len(name);
    if (!env_name_valid(name, name_length)) {
        return -1;
    }
    for (uint32_t i = 0; i < list->count; ++i) {
        if (env_item_matches(list->items[i], name, name_length)) {
            free(list->items[i]);
            for (; i + 1U < list->count; ++i) {
                list->items[i] = list->items[i + 1U];
            }
            list->items[--list->count] = 0;
            break;
        }
    }
    return 0;
}

static void env_trim_line(char *line)
{
    uint32_t start = 0;
    uint32_t end;
    uint32_t length;
    if (!line) {
        return;
    }
    length = env_text_len(line);
    while (start < length && (line[start] == ' ' || line[start] == '\t')) {
        ++start;
    }
    end = length;
    while (end > start && (line[end - 1U] == ' ' ||
           line[end - 1U] == '\t' || line[end - 1U] == '\r')) {
        --end;
    }
    if (start > 0 || end < length) {
        uint32_t out = 0;
        while (start < end) {
            line[out++] = line[start++];
        }
        line[out] = 0;
    }
}

static void env_parse_line(struct environment_list *list, char *line)
{
    if (!list || !line) {
        return;
    }
    env_trim_line(line);
    if (!line[0] || line[0] == '#') {
        return;
    }
    (void)env_list_set_item(list, line);
}

static int env_load_file(struct environment_list *list, const char *path)
{
    char *contents;
    uint32_t length = 0;
    int fd;
    long got;
    if (!list || !path) {
        return -1;
    }
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        /* Missing configuration is equivalent to an empty layer. */
        return fd == -2 ? 0 : fd;
    }
    contents = (char *)malloc(LEONOS_ENV_MAX_FILE_BYTES + 1U);
    if (!contents) {
        close(fd);
        return -12;
    }
    while (length < LEONOS_ENV_MAX_FILE_BYTES) {
        got = read(fd, contents + length,
                   LEONOS_ENV_MAX_FILE_BYTES - length);
        if (got <= 0) {
            break;
        }
        length += (uint32_t)got;
    }
    close(fd);
    contents[length] = 0;
    {
        char *line = contents;
        for (uint32_t i = 0; i <= length; ++i) {
            if (contents[i] == '\n' || contents[i] == 0) {
                contents[i] = 0;
                env_parse_line(list, line);
                line = contents + i + 1U;
            }
        }
    }
    free(contents);
    return 0;
}

static int env_user_path(char *path, uint32_t capacity,
                         const struct leonos_user_info *user)
{
    uint32_t home_length;
    uint32_t suffix_length = (uint32_t)sizeof(LEONOS_ENV_USER_SUFFIX) - 1U;
    if (!path || capacity == 0 || !user || !user->home[0]) {
        return 0;
    }
    home_length = env_text_len(user->home);
    if (home_length + suffix_length + 1U > capacity) {
        return 0;
    }
    memcpy(path, user->home, home_length);
    memcpy(path + home_length, LEONOS_ENV_USER_SUFFIX, suffix_length + 1U);
    return 1;
}

static int env_load_layers(struct environment_list *list,
                           char *const overrides[])
{
    struct leonos_user_info user = {0};
    char user_path[LEONOS_AUTH_HOME_LEN + sizeof(LEONOS_ENV_USER_SUFFIX)];
    int result;
    if (!list) {
        return -1;
    }
    result = env_load_file(list, LEONOS_ENV_GLOBAL_PATH);
    if (result < 0) {
        return result;
    }
    if (leonos_auth_current(&user) == 0 && env_user_path(user_path,
                                                          sizeof(user_path),
                                                          &user)) {
        result = env_load_file(list, user_path);
        if (result < 0) {
            return result;
        }
        if (!env_list_has(list, "HOME") && user.home[0]) {
            (void)env_list_set(list, "HOME", user.home);
        }
        if (!env_list_has(list, "USER") && user.username[0]) {
            (void)env_list_set(list, "USER", user.username);
        }
        if (!env_list_has(list, "LOGNAME") && user.username[0]) {
            (void)env_list_set(list, "LOGNAME", user.username);
        }
    }
    if (environ) {
        for (uint32_t i = 0; environ[i]; ++i) {
            (void)env_list_set_item(list, environ[i]);
        }
    }
    if (overrides) {
        for (uint32_t i = 0; overrides[i]; ++i) {
            (void)env_list_set_item(list, overrides[i]);
        }
    }
    return 0;
}

int leonos_environment_build(char *const overrides[], char ***out_envp)
{
    struct environment_list list = {0};
    char **envp;
    int result;
    if (!out_envp) {
        return -1;
    }
    *out_envp = 0;
    result = env_load_layers(&list, overrides);
    if (result < 0) {
        env_list_free(&list);
        return result;
    }
    envp = (char **)calloc(list.count + 1U, sizeof(*envp));
    if (!envp) {
        env_list_free(&list);
        return -12;
    }
    for (uint32_t i = 0; i < list.count; ++i) {
        envp[i] = list.items[i];
        list.items[i] = 0;
    }
    env_list_free(&list);
    *out_envp = envp;
    return 0;
}

void leonos_environment_free(char **envp)
{
    if (!envp) {
        return;
    }
    for (uint32_t i = 0; envp[i]; ++i) {
        free(envp[i]);
    }
    free(envp);
}

static int env_write_file(const char *path, const struct environment_list *list,
                          int mode)
{
    char *contents;
    uint32_t length = 0;
    int fd;
    if (!path || !list) {
        return -1;
    }
    contents = (char *)malloc(LEONOS_ENV_MAX_FILE_BYTES);
    if (!contents) {
        return -12;
    }
    for (uint32_t i = 0; i < list->count; ++i) {
        uint32_t item_length = env_text_len(list->items[i]);
        if (length + item_length + 1U > LEONOS_ENV_MAX_FILE_BYTES) {
            free(contents);
            return -7;
        }
        memcpy(contents + length, list->items[i], item_length);
        length += item_length;
        contents[length++] = '\n';
    }
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) {
        free(contents);
        return fd;
    }
    uint32_t written = 0;
    while (written < length) {
        long result = write(fd, contents + written, length - written);
        if (result <= 0) {
            close(fd);
            free(contents);
            return result < 0 ? (int)result : -5;
        }
        written += (uint32_t)result;
    }
    close(fd);
    free(contents);
    return 0;
}

static int env_persistent_path(uint32_t scope, char *path, uint32_t capacity,
                               struct leonos_user_info *user)
{
    if (!path || !capacity || !user) {
        return -1;
    }
    *user = (struct leonos_user_info){0};
    if (leonos_auth_current(user) < 0) {
        return -13;
    }
    if (scope == LEONOS_ENV_SCOPE_GLOBAL) {
        if (user->role != LEONOS_AUTH_ROLE_ADMIN) {
            return -13;
        }
        if (env_text_len(LEONOS_ENV_GLOBAL_PATH) + 1U > capacity) {
            return -7;
        }
        memcpy(path, LEONOS_ENV_GLOBAL_PATH,
               env_text_len(LEONOS_ENV_GLOBAL_PATH) + 1U);
        return 0;
    }
    if (scope == LEONOS_ENV_SCOPE_USER &&
        env_user_path(path, capacity, user)) {
        return 0;
    }
    return -22;
}

static int env_persistent_update(uint32_t scope, const char *name,
                                 const char *value, int remove)
{
    struct environment_list list = {0};
    struct leonos_user_info user;
    char path[LEONOS_AUTH_HOME_LEN + sizeof(LEONOS_ENV_USER_SUFFIX)];
    int result;
    result = env_persistent_path(scope, path, sizeof(path), &user);
    if (result < 0) {
        return result;
    }
    result = env_load_file(&list, path);
    if (result < 0) {
        env_list_free(&list);
        return result;
    }
    result = remove ? env_list_unset(&list, name) :
                      env_list_set(&list, name, value);
    if (result == 0) {
        result = env_write_file(path, &list,
                                scope == LEONOS_ENV_SCOPE_GLOBAL ? 0644 : 0600);
    }
    env_list_free(&list);
    return result;
}

int leonos_environment_set(uint32_t scope, const char *name, const char *value)
{
    return env_persistent_update(scope, name, value, 0);
}

int leonos_environment_unset(uint32_t scope, const char *name)
{
    return env_persistent_update(scope, name, "", 1);
}
