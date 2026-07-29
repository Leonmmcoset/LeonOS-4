#include <leonos/fs.h>
#include <leonos/ini.h>
#include <leonos/syscall.h>
#include <stdint.h>
#include <string.h>

struct leonos_ini_key {
    char name[LEONOS_INI_NAME_LEN];
    char value[LEONOS_INI_VALUE_LEN];
};

struct leonos_ini_section {
    char name[LEONOS_INI_NAME_LEN];
    uint32_t key_count;
    struct leonos_ini_key keys[LEONOS_INI_MAX_KEYS_PER_SECTION];
};

struct leonos_ini_state {
    uint32_t section_count;
    struct leonos_ini_section sections[LEONOS_INI_MAX_SECTIONS];
    int loaded;
};

static struct leonos_ini_state ini_state;
static char ini_buffer[LEONOS_INI_MAX_SIZE];

static int ini_buffer_is_text(const char *buffer, long len, uint32_t strict)
{
    long i;
    if (!buffer || len < 0) {
        return 0;
    }
    for (i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char)buffer[i];
        if (ch == 0) {
            return 0;
        }
        if (strict && ch < 0x20U && ch != '\n' && ch != '\r' &&
            ch != '\t') {
            return 0;
        }
    }
    return 1;
}

static void ini_trim(char *str)
{
    uint32_t len;
    uint32_t start;
    uint32_t end;
    if (!str || !str[0]) {
        return;
    }
    len = (uint32_t)strlen(str);
    start = 0;
    while (start < len && (str[start] == ' ' || str[start] == '\t' ||
           str[start] == '\r')) {
        ++start;
    }
    end = len;
    while (end > start && (str[end - 1U] == ' ' ||
           str[end - 1U] == '\t' || str[end - 1U] == '\r')) {
        --end;
    }
    if (start > 0 || end < len) {
        uint32_t i;
        for (i = 0; start < end; ++i, ++start) {
            str[i] = str[start];
        }
        str[i] = 0;
    }
}

static int ini_is_comment_or_empty(const char *line)
{
    if (!line || !line[0]) {
        return 1;
    }
    return line[0] == ';' || line[0] == '#' || line[0] == '\n' ||
           line[0] == '\r';
}

static int ini_parse_section(const char *line, char *name, uint32_t capacity,
                             uint32_t strict)
{
    uint32_t len;
    const char *end;
    if (!line || line[0] != '[') {
        return 0;
    }
    end = strchr(line, ']');
    if (!end || end <= line + 1) {
        return 0;
    }
    if (strict && end[1] != 0) {
        return 0;
    }
    len = (uint32_t)(end - line - 1U);
    if (len >= capacity) {
        if (strict) {
            return 0;
        }
        len = capacity - 1U;
    }
    memcpy(name, line + 1, len);
    name[len] = 0;
    ini_trim(name);
    return name[0] ? 1 : 0;
}

static int ini_parse_key_value(const char *line, char *key, uint32_t key_cap,
                               char *value, uint32_t value_cap,
                               uint32_t strict)
{
    const char *eq;
    uint32_t key_len;
    uint32_t val_len;
    if (!line || !line[0]) {
        return 0;
    }
    eq = strchr(line, '=');
    if (!eq || eq == line) {
        return 0;
    }
    key_len = (uint32_t)(eq - line);
    if (key_len >= key_cap) {
        if (strict) {
            return 0;
        }
        key_len = key_cap - 1U;
    }
    memcpy(key, line, key_len);
    key[key_len] = 0;
    ini_trim(key);
    if (!key[0]) {
        return 0;
    }
    val_len = (uint32_t)strlen(eq + 1);
    if (val_len >= value_cap) {
        if (strict) {
            return 0;
        }
        val_len = value_cap - 1U;
    }
    memcpy(value, eq + 1, val_len);
    value[val_len] = 0;
    ini_trim(value);
    return 1;
}

static struct leonos_ini_section *ini_find_section(const char *name)
{
    uint32_t i;
    if (!name || !ini_state.loaded) {
        return 0;
    }
    for (i = 0; i < ini_state.section_count; ++i) {
        if (strcmp(ini_state.sections[i].name, name) == 0) {
            return &ini_state.sections[i];
        }
    }
    return 0;
}

static int ini_section_exists_pending(const char *name)
{
    uint32_t i;
    if (!name) {
        return 0;
    }
    for (i = 0; i < ini_state.section_count; ++i) {
        if (strcmp(ini_state.sections[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static struct leonos_ini_key *ini_find_key(struct leonos_ini_section *section,
                                           const char *name)
{
    uint32_t i;
    if (!section || !name) {
        return 0;
    }
    for (i = 0; i < section->key_count; ++i) {
        if (strcmp(section->keys[i].name, name) == 0) {
            return &section->keys[i];
        }
    }
    return 0;
}

static int ini_load_mode(const char *path, uint32_t strict)
{
    int fd;
    long got;
    const char *p;
    const char *end;
    char line[LEONOS_INI_VALUE_LEN];
    struct leonos_ini_section *current_section = 0;
    struct leonos_stat st;
    memset(&ini_state, 0, sizeof(ini_state));
    if (!path || !path[0]) {
        return 0;
    }
    if (stat(path, &st) == 0) {
        if (st.type != LEONOS_FS_TYPE_FILE ||
            st.size >= (uint64_t)sizeof(ini_buffer)) {
            return 0;
        }
    }
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return 0;
    }
    got = read(fd, ini_buffer, sizeof(ini_buffer) - 1U);
    close(fd);
    if (got < 0) {
        return 0;
    }
    if (strict && got >= (long)sizeof(ini_buffer) - 1L) {
        return 0;
    }
    if (!ini_buffer_is_text(ini_buffer, got, strict)) {
        return 0;
    }
    ini_buffer[got] = 0;
    p = ini_buffer;
    end = ini_buffer + got;
    while (p < end) {
        const char *lf;
        uint32_t line_len;
        lf = strchr(p, '\n');
        if (!lf) {
            lf = end;
        }
        line_len = (uint32_t)(lf - p);
        if (line_len >= sizeof(line)) {
            if (strict) {
                memset(&ini_state, 0, sizeof(ini_state));
                return 0;
            }
            line_len = sizeof(line) - 1U;
        }
        memcpy(line, p, line_len);
        line[line_len] = 0;
        ini_trim(line);
        if (!ini_is_comment_or_empty(line)) {
            if (line[0] == '[') {
                current_section = 0;
                if (ini_state.section_count < LEONOS_INI_MAX_SECTIONS) {
                    char sec_name[LEONOS_INI_NAME_LEN];
                    if (ini_parse_section(line, sec_name,
                                          sizeof(sec_name), strict)) {
                        if (strict && ini_section_exists_pending(sec_name)) {
                            memset(&ini_state, 0, sizeof(ini_state));
                            return 0;
                        }
                        current_section =
                            &ini_state.sections[ini_state.section_count];
                        memcpy(current_section->name, sec_name,
                               strlen(sec_name) + 1U);
                        ++ini_state.section_count;
                    } else if (strict) {
                        memset(&ini_state, 0, sizeof(ini_state));
                        return 0;
                    }
                } else if (strict) {
                    memset(&ini_state, 0, sizeof(ini_state));
                    return 0;
                }
            } else if (current_section &&
                       current_section->key_count <
                           LEONOS_INI_MAX_KEYS_PER_SECTION) {
                struct leonos_ini_key *k =
                    &current_section->keys[current_section->key_count];
                if (ini_parse_key_value(line, k->name, sizeof(k->name),
                                        k->value, sizeof(k->value),
                                        strict)) {
                    if (strict && ini_find_key(current_section, k->name)) {
                        memset(k, 0, sizeof(*k));
                        memset(&ini_state, 0, sizeof(ini_state));
                        return 0;
                    }
                    ++current_section->key_count;
                } else if (strict) {
                    memset(&ini_state, 0, sizeof(ini_state));
                    return 0;
                }
            } else if (strict) {
                memset(&ini_state, 0, sizeof(ini_state));
                return 0;
            }
        }
        p = lf;
        if (p < end) {
            ++p;
        }
    }
    ini_state.loaded = 1;
    return 1;
}

int leonos_ini_load(const char *path)
{
    return ini_load_mode(path, 0);
}

int leonos_ini_load_strict(const char *path)
{
    return ini_load_mode(path, 1);
}

int leonos_ini_get(const char *section, const char *key,
                   char *value, uint32_t capacity)
{
    struct leonos_ini_section *sec;
    struct leonos_ini_key *k;
    uint32_t val_len;
    if (!ini_state.loaded || !section || !key || !value || capacity == 0) {
        return 0;
    }
    sec = ini_find_section(section);
    if (!sec) {
        return 0;
    }
    k = ini_find_key(sec, key);
    if (!k) {
        return 0;
    }
    val_len = (uint32_t)strlen(k->value);
    if (val_len >= capacity) {
        val_len = capacity - 1U;
    }
    memcpy(value, k->value, val_len);
    value[val_len] = 0;
    return 1;
}

int leonos_ini_section_count(void)
{
    if (!ini_state.loaded) {
        return 0;
    }
    return (int)ini_state.section_count;
}

int leonos_ini_section_name(uint32_t index, char *name, uint32_t capacity)
{
    uint32_t len;
    if (!ini_state.loaded || index >= ini_state.section_count ||
        !name || capacity == 0) {
        return 0;
    }
    len = (uint32_t)strlen(ini_state.sections[index].name);
    if (len >= capacity) {
        len = capacity - 1U;
    }
    memcpy(name, ini_state.sections[index].name, len);
    name[len] = 0;
    return 1;
}

int leonos_ini_key_count(const char *section)
{
    struct leonos_ini_section *sec;
    if (!ini_state.loaded || !section) {
        return 0;
    }
    sec = ini_find_section(section);
    if (!sec) {
        return 0;
    }
    return (int)sec->key_count;
}

int leonos_ini_key_name(const char *section, uint32_t index,
                        char *name, uint32_t capacity)
{
    struct leonos_ini_section *sec;
    uint32_t len;
    if (!ini_state.loaded || !section || !name || capacity == 0) {
        return 0;
    }
    sec = ini_find_section(section);
    if (!sec || index >= sec->key_count) {
        return 0;
    }
    len = (uint32_t)strlen(sec->keys[index].name);
    if (len >= capacity) {
        len = capacity - 1U;
    }
    memcpy(name, sec->keys[index].name, len);
    name[len] = 0;
    return 1;
}
