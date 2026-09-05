#include <leonos/auth.h>
#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/http.h>
#include <leonos/text_input.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <string.h>

#define OSCHINPT_ID "oschinpt"
#define OSCHINPT_DICT_PATH "/programs/oschinpt/pinyin_simp.dict.yaml"
#define OSCHINPT_DICT_INDEX_PATH "/programs/oschinpt/oscp.idx"
#define OSCHINPT_DICT_URL "https://raw.githubusercontent.com/rime/rime-pinyin-simp/master/pinyin_simp.dict.yaml"
#define OSCHINPT_CONFIG_NAME ".inputm.conf"
#define OSCHINPT_LEARN_NAME ".oschinpt.learn"
#define OSCHINPT_COMPOSITION_CAP 48U
#define OSCHINPT_LEARN_CAP 32U
#define OSCHINPT_DICT_LINE_CAP 256U
#define OSCHINPT_DICT_READ_CHUNK 1024U
#define OSCHINPT_LOOKUP_CACHE_CAP 12U
#define OSCHINPT_CANDIDATE_POOL_CAP 32U
#define OSCHINPT_CANDIDATE_PAGE_SIZE TEXT_INPUT_MAX_CANDIDATES
#define OSCHINPT_INDEX_CODE_LEN 8U
#define OSCHINPT_INDEX_CAP 2048U
#define OSCHINPT_INDEX_MAGIC "OSCI"
#define OSCHINPT_INDEX_VERSION 1U

struct phrase {
    const char *code;
    const char *words[TEXT_INPUT_MAX_CANDIDATES];
};

struct learned_phrase {
    char code[OSCHINPT_COMPOSITION_CAP];
    char word[TEXT_INPUT_TEXT_LEN];
};

struct lookup_cache_entry {
    char code[OSCHINPT_COMPOSITION_CAP];
    char candidates[OSCHINPT_CANDIDATE_POOL_CAP][TEXT_INPUT_TEXT_LEN];
    uint32_t count;
};

struct dictionary_index_header {
    char magic[4];
    uint32_t version;
    uint32_t count;
    uint32_t dictionary_size;
};

struct dictionary_index_entry {
    char code[OSCHINPT_INDEX_CODE_LEN];
    uint32_t start;
    uint32_t end;
};

static const struct phrase common_phrases[] = {
    {"ni", {"你", "呢", "尼", "泥", "拟"}},
    {"hao", {"好", "号", "浩", "毫", "郝"}},
    {"nihao", {"你好", 0, 0, 0, 0}},
    {"zhong", {"中", "种", "重", "终", "钟"}},
    {"guo", {"国", "过", "果", "郭", "锅"}},
    {"zhongguo", {"中国", 0, 0, 0, 0}},
    {"ren", {"人", "仁", "任", "认", "忍"}},
    {"min", {"民", "敏", "明", "闽", "珉"}},
    {"renmin", {"人民", 0, 0, 0, 0}},
    {"shi", {"是", "时", "事", "市", "十"}},
    {"wo", {"我", "握", "窝", "沃", "卧"}},
    {"men", {"们", "门", "闷", "梦", "蒙"}},
    {"women", {"我们", 0, 0, 0, 0}},
    {"de", {"的", "得", "德", "地", "等"}},
    {"le", {"了", "乐", "勒", "了", "泐"}},
    {"zai", {"在", "再", "载", "灾", "仔"}},
    {"you", {"有", "又", "由", "友", "右"}},
    {"he", {"和", "合", "何", "河", "喝"}},
    {"bu", {"不", "部", "步", "布", "补"}},
    {"yi", {"一", "以", "已", "义", "意"}},
    {"ge", {"个", "各", "歌", "格", "革"}},
    {"zhe", {"这", "着", "者", "折", "哲"}},
    {"na", {"那", "拿", "哪", "纳", "娜"}},
    {"ma", {"吗", "马", "嘛", "妈", "麻"}},
    {"xing", {"行", "性", "形", "兴", "星"}},
    {"xie", {"谢", "些", "写", "鞋", "协"}},
    {"xiexie", {"谢谢", 0, 0, 0, 0}},
    {"qing", {"请", "情", "青", "清", "轻"}},
    {"wen", {"问", "文", "闻", "温", "稳"}},
    {"tian", {"天", "田", "填", "甜", "添"}},
    {"qi", {"起", "其", "气", "期", "七"}},
    {"gong", {"工", "共", "公", "功", "供"}},
    {"zuo", {"作", "做", "坐", "左", "昨"}},
    {"gongzuo", {"工作", 0, 0, 0, 0}},
    {"leonos", {"LeonOS", 0, 0, 0, 0}},
};

static char composition[OSCHINPT_COMPOSITION_CAP];
static struct learned_phrase learned[OSCHINPT_LEARN_CAP];
static uint32_t learned_count;
static uint8_t chinese_punctuation = 1;
static uint8_t full_width;
static uint8_t learning_enabled = 1;
static uint32_t composition_pid;
static uint32_t composition_window;
static struct lookup_cache_entry lookup_cache[OSCHINPT_LOOKUP_CACHE_CAP];
static uint32_t lookup_cache_next;
static struct dictionary_index_entry dictionary_index[OSCHINPT_INDEX_CAP];
static uint32_t dictionary_index_count;
static uint8_t dictionary_index_loaded;
static uint8_t dictionary_index_attempted;
static uint32_t candidate_page;

static uint32_t text_len(const char *text)
{
    uint32_t length = 0;
    while (text && text[length]) {
        ++length;
    }
    return length;
}

static int text_eq(const char *left, const char *right)
{
    uint32_t i = 0;
    if (!left || !right) {
        return 0;
    }
    while (left[i] && right[i] && left[i] == right[i]) {
        ++i;
    }
    return !left[i] && !right[i];
}

static void copy_text(char *dst, uint32_t capacity, const char *src)
{
    uint32_t i = 0;
    if (!dst || !capacity) {
        return;
    }
    while (src && src[i] && i + 1U < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int user_path(char *path, uint32_t capacity, const char *name)
{
    struct leonos_user_info user = {0};
    uint32_t home_len;
    uint32_t name_len = text_len(name);
    if (!path || !capacity || leonos_auth_current(&user) != 0 || !user.uid || !user.home[0]) {
        return 0;
    }
    home_len = text_len(user.home);
    if (home_len + 1U + name_len >= capacity) {
        return 0;
    }
    memcpy(path, user.home, home_len);
    path[home_len] = '/';
    memcpy(path + home_len + 1U, name, name_len + 1U);
    return 1;
}

static int config_enabled(const char *config, const char *key, int fallback)
{
    uint32_t key_len = text_len(key);
    uint32_t pos = 0;
    int value = fallback;
    while (config && config[pos]) {
        uint32_t start = pos;
        uint32_t end;
        uint8_t match = 1;
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
                match = 0;
                break;
            }
        }
        if (match && start + key_len + 1U < end) {
            value = config[start + key_len + 1U] == '0' ? 0 : 1;
        }
    }
    return value;
}

static void load_user_config(void)
{
    char path[LEONOS_FS_PATH_LEN];
    char config[2048];
    int fd;
    long got;
    if (!user_path(path, sizeof(path), OSCHINPT_CONFIG_NAME)) {
        return;
    }
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return;
    }
    got = read(fd, config, sizeof(config) - 1U);
    close(fd);
    if (got <= 0) {
        return;
    }
    config[got] = 0;
    chinese_punctuation = config_enabled(config, "oschinpt_punctuation", 1) ? 1 : 0;
    full_width = config_enabled(config, "oschinpt_full_width", 0) ? 1 : 0;
    learning_enabled = config_enabled(config, "oschinpt_learning", 1) ? 1 : 0;
}

static void load_learning(void)
{
    char path[LEONOS_FS_PATH_LEN];
    char data[2048];
    int fd;
    long got;
    uint32_t pos = 0;
    if (!user_path(path, sizeof(path), OSCHINPT_LEARN_NAME)) {
        return;
    }
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return;
    }
    got = read(fd, data, sizeof(data) - 1U);
    close(fd);
    if (got <= 0) {
        return;
    }
    data[got] = 0;
    while (data[pos] && learned_count < OSCHINPT_LEARN_CAP) {
        char *code = data + pos;
        char *word = 0;
        while (data[pos] && data[pos] != '\t' && data[pos] != '\n') {
            ++pos;
        }
        if (data[pos] != '\t') {
            while (data[pos] && data[pos] != '\n') {
                ++pos;
            }
            if (data[pos]) {
                ++pos;
            }
            continue;
        }
        data[pos++] = 0;
        word = data + pos;
        while (data[pos] && data[pos] != '\n' && data[pos] != '\r') {
            ++pos;
        }
        data[pos] = 0;
        while (data[pos] == '\n' || data[pos] == '\r') {
            ++pos;
        }
        if (code[0] && word[0]) {
            copy_text(learned[learned_count].code, sizeof(learned[learned_count].code), code);
            copy_text(learned[learned_count].word, sizeof(learned[learned_count].word), word);
            ++learned_count;
        }
    }
}

static void save_learning(const char *code, const char *word)
{
    char path[LEONOS_FS_PATH_LEN];
    char line[TEXT_INPUT_TEXT_LEN + OSCHINPT_COMPOSITION_CAP + 4U];
    uint32_t pos = 0;
    int fd;
    if (!learning_enabled || !code || !code[0] || !word || !word[0] ||
        !user_path(path, sizeof(path), OSCHINPT_LEARN_NAME)) {
        return;
    }
    for (uint32_t i = 0; code[i] && pos + 1U < sizeof(line); ++i) {
        line[pos++] = code[i];
    }
    if (pos + 1U < sizeof(line)) {
        line[pos++] = '\t';
    }
    for (uint32_t i = 0; word[i] && pos + 1U < sizeof(line); ++i) {
        line[pos++] = word[i];
    }
    if (pos + 1U < sizeof(line)) {
        line[pos++] = '\n';
    }
    line[pos] = 0;
    fd = open(path, LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_APPEND, 0);
    if (fd >= 0) {
        (void)write(fd, line, pos);
        close(fd);
    }
}

static void add_candidate(char candidates[OSCHINPT_CANDIDATE_POOL_CAP][TEXT_INPUT_TEXT_LEN],
                           uint32_t *count, const char *word)
{
    if (!count || !word || !word[0] || *count >= OSCHINPT_CANDIDATE_POOL_CAP) {
        return;
    }
    for (uint32_t i = 0; i < *count; ++i) {
        if (text_eq(candidates[i], word)) {
            return;
        }
    }
    copy_text(candidates[*count], TEXT_INPUT_TEXT_LEN, word);
    ++(*count);
}

static void parse_dictionary_line(const char *line, uint32_t length, const char *code,
                                  char candidates[OSCHINPT_CANDIDATE_POOL_CAP][TEXT_INPUT_TEXT_LEN],
                                  uint32_t *count)
{
    uint32_t word_end = 0;
    uint32_t code_start;
    uint32_t code_end;
    char word[TEXT_INPUT_TEXT_LEN];
    if (!line || !code || !code[0] || !count || !length || line[0] == '#') {
        return;
    }
    while (word_end < length && line[word_end] != '\t') {
        ++word_end;
    }
    if (word_end == length || word_end == 0 || word_end >= sizeof(word)) {
        return;
    }
    code_start = word_end + 1U;
    code_end = code_start;
    while (code_end < length && line[code_end] != '\t' && line[code_end] != ' ') {
        ++code_end;
    }
    if (code_end == code_start || text_len(code) != code_end - code_start) {
        return;
    }
    for (uint32_t i = 0; i < code_end - code_start; ++i) {
        if (line[code_start + i] != code[i]) {
            return;
        }
    }
    memcpy(word, line, word_end);
    word[word_end] = 0;
    add_candidate(candidates, count, word);
}

static int dictionary_line_code(const char *line, uint32_t length,
                                uint32_t *out_start, uint32_t *out_end)
{
    uint32_t word_end = 0;
    uint32_t code_start;
    uint32_t code_end;
    if (!line || !length || line[0] == '#') {
        return 0;
    }
    while (word_end < length && line[word_end] != '\t') {
        ++word_end;
    }
    if (word_end == 0 || word_end == length) {
        return 0;
    }
    code_start = word_end + 1U;
    code_end = code_start;
    while (code_end < length && line[code_end] != '\t' && line[code_end] != ' ') {
        ++code_end;
    }
    if (code_start == code_end) {
        return 0;
    }
    if (out_start) {
        *out_start = code_start;
    }
    if (out_end) {
        *out_end = code_end;
    }
    return 1;
}

static int dictionary_code_text_compare(const char *left, const char *right)
{
    uint32_t i = 0;
    while (left && right && left[i] && right[i] && left[i] == right[i]) {
        ++i;
    }
    if (!left || !right || left[i] < right[i]) {
        return -1;
    }
    if (left[i] > right[i]) {
        return 1;
    }
    return 0;
}

static int dictionary_read_exact(int fd, void *buffer, uint32_t size)
{
    uint8_t *out = (uint8_t *)buffer;
    uint32_t done = 0;
    while (done < size) {
        long got = read(fd, out + done, size - done);
        if (got <= 0) {
            return 0;
        }
        done += (uint32_t)got;
    }
    return 1;
}

static int dictionary_write_exact(int fd, const void *buffer, uint32_t size)
{
    const uint8_t *input = (const uint8_t *)buffer;
    uint32_t done = 0;
    while (done < size) {
        long wrote = write(fd, input + done, size - done);
        if (wrote <= 0) {
            return 0;
        }
        done += (uint32_t)wrote;
    }
    return 1;
}

static int dictionary_index_valid_code(const char *code)
{
    uint32_t i = 0;
    if (!code || !code[0]) {
        return 0;
    }
    while (code[i]) {
        if (code[i] < 'a' || code[i] > 'z' || i + 1U >= OSCHINPT_INDEX_CODE_LEN) {
            return 0;
        }
        ++i;
    }
    return 1;
}

static void dictionary_index_reset(void)
{
    dictionary_index_count = 0;
    dictionary_index_loaded = 0;
    dictionary_index_attempted = 0;
}

static int dictionary_index_load(void)
{
    struct dictionary_index_header header = {0};
    struct leonos_stat dictionary_stat = {0};
    int index_fd;
    int dictionary_fd;
    if (dictionary_index_loaded) {
        return 1;
    }
    if (dictionary_index_attempted) {
        return 0;
    }
    dictionary_index_attempted = 1;
    dictionary_index_count = 0;
    dictionary_fd = open(OSCHINPT_DICT_PATH, LEONOS_O_RDONLY, 0);
    if (dictionary_fd < 0 || leonos_fstat_legacy(dictionary_fd, &dictionary_stat) < 0) {
        if (dictionary_fd >= 0) {
            close(dictionary_fd);
        }
        return 0;
    }
    close(dictionary_fd);
    if (dictionary_stat.size > 0xffffffffULL) {
        return 0;
    }
    index_fd = open(OSCHINPT_DICT_INDEX_PATH, LEONOS_O_RDONLY, 0);
    if (index_fd < 0 || !dictionary_read_exact(index_fd, &header, sizeof(header)) ||
        header.magic[0] != 'O' || header.magic[1] != 'S' ||
        header.magic[2] != 'C' || header.magic[3] != 'I' ||
        header.version != OSCHINPT_INDEX_VERSION || !header.count ||
        header.count > OSCHINPT_INDEX_CAP ||
        header.dictionary_size != (uint32_t)dictionary_stat.size ||
        !dictionary_read_exact(index_fd, dictionary_index,
                               header.count * sizeof(dictionary_index[0]))) {
        if (index_fd >= 0) {
            close(index_fd);
        }
        return 0;
    }
    close(index_fd);
    for (uint32_t i = 0; i < header.count; ++i) {
        if (!dictionary_index_valid_code(dictionary_index[i].code) ||
            dictionary_index[i].end <= dictionary_index[i].start ||
            dictionary_index[i].end > header.dictionary_size) {
            dictionary_index_count = 0;
            return 0;
        }
        if (i) {
            int comparison = dictionary_code_text_compare(dictionary_index[i - 1U].code,
                                                           dictionary_index[i].code);
            if (comparison > 0 ||
                (comparison == 0 &&
                 dictionary_index[i - 1U].end >= dictionary_index[i].start)) {
                dictionary_index_count = 0;
                return 0;
            }
        }
    }
    dictionary_index_count = header.count;
    dictionary_index_loaded = 1;
    return 1;
}

static uint32_t dictionary_index_find(const char *code)
{
    uint32_t low = 0;
    uint32_t high = dictionary_index_count;
    if (!dictionary_index_valid_code(code)) {
        return dictionary_index_count;
    }
    if (!dictionary_index_load()) {
        return dictionary_index_count;
    }
    while (low < high) {
        uint32_t middle = low + (high - low) / 2U;
        int comparison = dictionary_code_text_compare(dictionary_index[middle].code, code);
        if (comparison < 0) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    if (low < dictionary_index_count && text_eq(dictionary_index[low].code, code)) {
        return low;
    }
    return dictionary_index_count;
}

static int dictionary_index_note(const char *line, uint32_t length,
                                 uint32_t start, uint32_t end)
{
    char code[OSCHINPT_INDEX_CODE_LEN] = {0};
    uint32_t code_start;
    uint32_t code_end;
    if (!dictionary_line_code(line, length, &code_start, &code_end) ||
        code_end - code_start >= sizeof(code)) {
        return 1;
    }
    for (uint32_t i = 0; i < code_end - code_start; ++i) {
        char ch = line[code_start + i];
        if (ch < 'a' || ch > 'z') {
            return 1;
        }
        code[i] = ch;
    }
    if (dictionary_index_count && text_eq(dictionary_index[dictionary_index_count - 1U].code, code)) {
        dictionary_index[dictionary_index_count - 1U].end = end;
        return 1;
    }
    if (dictionary_index_count >= OSCHINPT_INDEX_CAP) {
        return 0;
    }
    copy_text(dictionary_index[dictionary_index_count].code,
              sizeof(dictionary_index[dictionary_index_count].code), code);
    dictionary_index[dictionary_index_count].start = start;
    dictionary_index[dictionary_index_count].end = end;
    ++dictionary_index_count;
    return 1;
}

static void dictionary_index_sort(void)
{
    for (uint32_t i = 0; i < dictionary_index_count; ++i) {
        for (uint32_t j = i + 1U; j < dictionary_index_count; ++j) {
            int comparison = dictionary_code_text_compare(dictionary_index[j].code,
                                                          dictionary_index[i].code);
            if (comparison < 0 ||
                (comparison == 0 && dictionary_index[j].start < dictionary_index[i].start)) {
                struct dictionary_index_entry temp = dictionary_index[i];
                dictionary_index[i] = dictionary_index[j];
                dictionary_index[j] = temp;
            }
        }
    }
}

static int dictionary_index_build(void)
{
    struct dictionary_index_header header = {0};
    struct leonos_stat dictionary_stat = {0};
    char buffer[OSCHINPT_DICT_READ_CHUNK];
    char line[OSCHINPT_DICT_LINE_CAP];
    uint32_t line_len = 0;
    uint32_t line_start = 0;
    uint32_t offset = 0;
    int dictionary_fd;
    int index_fd;
    long got;
    dictionary_fd = open(OSCHINPT_DICT_PATH, LEONOS_O_RDONLY, 0);
    if (dictionary_fd < 0 || leonos_fstat_legacy(dictionary_fd, &dictionary_stat) < 0 ||
        dictionary_stat.size > 0xffffffffULL) {
        if (dictionary_fd >= 0) {
            close(dictionary_fd);
        }
        return 0;
    }
    dictionary_index_reset();
    while ((got = read(dictionary_fd, buffer, sizeof(buffer))) > 0) {
        for (long i = 0; i < got; ++i, ++offset) {
            char ch = buffer[i];
            if (ch == '\n') {
                ++offset;
                if (line_len && !dictionary_index_note(line, line_len, line_start, offset)) {
                    close(dictionary_fd);
                    dictionary_index_count = 0;
                    dictionary_index_attempted = 1;
                    return 0;
                }
                line_len = 0;
                line_start = offset;
                --offset;
            } else if (ch != '\r' && line_len + 1U < sizeof(line)) {
                line[line_len++] = ch;
            }
        }
        /* Manual updates must yield while walking the full source dictionary. */
        sleep_ms(1);
    }
    close(dictionary_fd);
    if (got < 0 || !dictionary_index_count) {
        dictionary_index_count = 0;
        dictionary_index_attempted = 1;
        return 0;
    }
    dictionary_index_sort();
    header.magic[0] = 'O';
    header.magic[1] = 'S';
    header.magic[2] = 'C';
    header.magic[3] = 'I';
    header.version = OSCHINPT_INDEX_VERSION;
    header.count = dictionary_index_count;
    header.dictionary_size = (uint32_t)dictionary_stat.size;
    index_fd = open(OSCHINPT_DICT_INDEX_PATH,
                    LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    if (index_fd < 0 || !dictionary_write_exact(index_fd, &header, sizeof(header)) ||
        !dictionary_write_exact(index_fd, dictionary_index,
                                dictionary_index_count * sizeof(dictionary_index[0]))) {
        if (index_fd >= 0) {
            close(index_fd);
        }
        dictionary_index_count = 0;
        dictionary_index_attempted = 1;
        return 0;
    }
    close(index_fd);
    dictionary_index_loaded = 1;
    dictionary_index_attempted = 1;
    return 1;
}

static void dictionary_candidates(const char *code,
                                  char candidates[OSCHINPT_CANDIDATE_POOL_CAP][TEXT_INPUT_TEXT_LEN],
                                  uint32_t *count)
{
    uint32_t index_pos = dictionary_index_find(code);
    char buffer[OSCHINPT_DICT_READ_CHUNK];
    char line[OSCHINPT_DICT_LINE_CAP];
    uint32_t line_len = 0;
    uint32_t remaining;
    int fd;
    if (index_pos >= dictionary_index_count || !count) {
        return;
    }
    fd = open(OSCHINPT_DICT_PATH, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return;
    }
    while (index_pos < dictionary_index_count && *count < OSCHINPT_CANDIDATE_POOL_CAP &&
           text_eq(dictionary_index[index_pos].code, code)) {
        struct dictionary_index_entry *entry = &dictionary_index[index_pos++];
        if (lseek(fd, (long)entry->start, LEONOS_SEEK_SET) < 0) {
            break;
        }
        remaining = entry->end - entry->start;
        line_len = 0;
        while (*count < OSCHINPT_CANDIDATE_POOL_CAP && remaining) {
            uint32_t want = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
            long got = read(fd, buffer, want);
            if (got <= 0) {
                remaining = 0;
                break;
            }
            remaining -= (uint32_t)got;
            for (long i = 0; i < got; ++i) {
                char ch = buffer[i];
                if (ch == '\n' || ch == '\r') {
                    if (line_len) {
                        parse_dictionary_line(line, line_len, code, candidates, count);
                        line_len = 0;
                    }
                } else if (line_len + 1U < sizeof(line)) {
                    line[line_len++] = ch;
                }
            }
        }
    }
    close(fd);
}

static int lookup_cache_get(const char *code,
                            char candidates[OSCHINPT_CANDIDATE_POOL_CAP][TEXT_INPUT_TEXT_LEN],
                            uint32_t *out_count)
{
    if (!code || !out_count) {
        return 0;
    }
    for (uint32_t i = 0; i < OSCHINPT_LOOKUP_CACHE_CAP; ++i) {
        if (lookup_cache[i].code[0] && text_eq(lookup_cache[i].code, code)) {
            *out_count = lookup_cache[i].count;
            for (uint32_t j = 0; j < *out_count; ++j) {
                copy_text(candidates[j], TEXT_INPUT_TEXT_LEN, lookup_cache[i].candidates[j]);
            }
            return 1;
        }
    }
    return 0;
}

static void lookup_cache_put(const char *code,
                             char candidates[OSCHINPT_CANDIDATE_POOL_CAP][TEXT_INPUT_TEXT_LEN],
                             uint32_t count)
{
    struct lookup_cache_entry *entry;
    if (!code || !code[0]) {
        return;
    }
    entry = &lookup_cache[lookup_cache_next++ % OSCHINPT_LOOKUP_CACHE_CAP];
    *entry = (struct lookup_cache_entry){0};
    copy_text(entry->code, sizeof(entry->code), code);
    entry->count = count;
    for (uint32_t i = 0; i < count; ++i) {
        copy_text(entry->candidates[i], TEXT_INPUT_TEXT_LEN, candidates[i]);
    }
}

static uint32_t lookup_candidates(const char *code,
                                  char candidates[OSCHINPT_CANDIDATE_POOL_CAP][TEXT_INPUT_TEXT_LEN])
{
    uint32_t count = 0;
    if (lookup_cache_get(code, candidates, &count)) {
        return count;
    }
    for (uint32_t i = 0; i < learned_count; ++i) {
        if (text_eq(learned[i].code, code)) {
            add_candidate(candidates, &count, learned[i].word);
        }
    }
    for (uint32_t i = 0; i < sizeof(common_phrases) / sizeof(common_phrases[0]); ++i) {
        if (text_eq(common_phrases[i].code, code)) {
            for (uint32_t word = 0; word < TEXT_INPUT_MAX_CANDIDATES; ++word) {
                add_candidate(candidates, &count, common_phrases[i].words[word]);
            }
            lookup_cache_put(code, candidates, count);
            return count;
        }
    }
    dictionary_candidates(code, candidates, &count);
    lookup_cache_put(code, candidates, count);
    return count;
}

static int key_to_ascii(uint8_t keycode, char *out)
{
    static const char letters[] = "qwertyuiopasdfghjklzxcvbnm";
    static const uint8_t codes[] = {
        16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
        30, 31, 32, 33, 34, 35, 36, 37, 38,
        44, 45, 46, 47, 48, 49, 50,
    };
    if (!out) {
        return 0;
    }
    for (uint32_t i = 0; i < sizeof(codes); ++i) {
        if (keycode == codes[i]) {
            *out = letters[i];
            return 1;
        }
    }
    if (keycode >= 2 && keycode <= 11) {
        *out = keycode == 11 ? '0' : (char)('0' + keycode - 1U);
        return 1;
    }
    switch (keycode) {
    case 12: *out = '-'; return 1;
    case 13: *out = '='; return 1;
    case 39: *out = ';'; return 1;
    case 40: *out = '\''; return 1;
    case 51: *out = ','; return 1;
    case 52: *out = '.'; return 1;
    case 53: *out = '/'; return 1;
    default: return 0;
    }
}

static void respond(const text_input_key_event_t *event, uint32_t type,
                    const char *text,
                    char candidates[OSCHINPT_CANDIDATE_POOL_CAP][TEXT_INPUT_TEXT_LEN],
                    uint32_t candidate_count)
{
    text_input_result_t result = {0};
    uint32_t first = 0;
    uint32_t visible = 0;
    result.sequence = event->sequence;
    result.client_pid = event->client_pid;
    result.window_id = event->window_id;
    result.type = type;
    copy_text(result.text, sizeof(result.text), text);
    if (candidate_count) {
        uint32_t page_count = (candidate_count + OSCHINPT_CANDIDATE_PAGE_SIZE - 1U) /
                              OSCHINPT_CANDIDATE_PAGE_SIZE;
        if (candidate_page >= page_count) {
            candidate_page = page_count - 1U;
        }
        first = candidate_page * OSCHINPT_CANDIDATE_PAGE_SIZE;
        visible = candidate_count - first;
        if (visible > TEXT_INPUT_MAX_CANDIDATES) {
            visible = TEXT_INPUT_MAX_CANDIDATES;
        }
    }
    result.candidate_count = visible;
    for (uint32_t i = 0; i < visible; ++i) {
        copy_text(result.candidates[i], sizeof(result.candidates[i]), candidates[first + i]);
    }
    (void)text_input_provider_result(&result);
}

static void respond_composition(const text_input_key_event_t *event)
{
    char candidates[OSCHINPT_CANDIDATE_POOL_CAP][TEXT_INPUT_TEXT_LEN] = {{0}};
    uint32_t count = lookup_candidates(composition, candidates);
    respond(event, TEXT_INPUT_RESULT_COMPOSITION, composition, candidates, count);
}

static void clear_composition(void)
{
    composition[0] = 0;
    composition_pid = 0;
    composition_window = 0;
    candidate_page = 0;
}

static void commit_candidate(const text_input_key_event_t *event, uint32_t selected,
                              const char *suffix)
{
    char candidates[OSCHINPT_CANDIDATE_POOL_CAP][TEXT_INPUT_TEXT_LEN] = {{0}};
    char text[TEXT_INPUT_TEXT_LEN];
    uint32_t count = lookup_candidates(composition, candidates);
    uint32_t pos = 0;
    if (count && selected < count) {
        copy_text(text, sizeof(text), candidates[selected]);
    } else {
        copy_text(text, sizeof(text), composition);
    }
    save_learning(composition, text);
    pos = text_len(text);
    while (suffix && *suffix && pos + 1U < sizeof(text)) {
        text[pos++] = *suffix++;
    }
    text[pos] = 0;
    clear_composition();
    respond(event, TEXT_INPUT_RESULT_COMMIT, text, candidates, 0);
}

static const char *punctuation_for(char ch)
{
    if (!chinese_punctuation) {
        return 0;
    }
    switch (ch) {
    case ',': return "，";
    case '.': return "。";
    case ';': return "；";
    case '/': return "、";
    default: return 0;
    }
}

static uint32_t candidate_page_first(void)
{
    return candidate_page * OSCHINPT_CANDIDATE_PAGE_SIZE;
}

static void change_candidate_page(const text_input_key_event_t *event, int direction)
{
    char candidates[OSCHINPT_CANDIDATE_POOL_CAP][TEXT_INPUT_TEXT_LEN] = {{0}};
    uint32_t count = lookup_candidates(composition, candidates);
    uint32_t page_count;

    if (!count) {
        candidate_page = 0;
        respond(event, TEXT_INPUT_RESULT_COMPOSITION, composition, candidates, 0);
        return;
    }
    page_count = (count + OSCHINPT_CANDIDATE_PAGE_SIZE - 1U) /
                 OSCHINPT_CANDIDATE_PAGE_SIZE;
    if (candidate_page >= page_count) {
        candidate_page = page_count - 1U;
    }
    if (direction > 0 && candidate_page + 1U < page_count) {
        ++candidate_page;
    } else if (direction < 0 && candidate_page) {
        --candidate_page;
    }
    respond(event, TEXT_INPUT_RESULT_COMPOSITION, composition, candidates, count);
}

static int update_dictionary(void)
{
    struct leonos_http_response response = {0};
    int ret = leonos_http_download(OSCHINPT_DICT_URL, OSCHINPT_DICT_PATH,
                                   LEONOS_HTTP_DEFAULT_TIMEOUT_MS, 0, 0, &response);
    if (ret > 0 && response.http_status >= 200U && response.http_status < 300U) {
        dictionary_index_reset();
        if (!dictionary_index_build()) {
            puts("[oschinpt] dictionary update failed: could not build index");
            return 1;
        }
        puts("[oschinpt] dictionary update complete");
        return 0;
    }
    printf("[oschinpt] dictionary update failed ret=%d http=%u\n", ret, response.http_status);
    return 1;
}

static void handle_event(const text_input_key_event_t *event)
{
    char ch;
    const char *punctuation;
    if (!event->pressed) {
        respond(event, TEXT_INPUT_RESULT_PASSTHROUGH, "", 0, 0);
        return;
    }
    if (composition[0] && (composition_pid != event->client_pid ||
                           composition_window != event->window_id)) {
        clear_composition();
    }
    if (key_to_ascii(event->keycode, &ch) && ch >= 'a' && ch <= 'z') {
        uint32_t length = text_len(composition);
        if (length + 1U < sizeof(composition)) {
            composition[length] = ch;
            composition[length + 1U] = 0;
            composition_pid = event->client_pid;
            composition_window = event->window_id;
            candidate_page = 0;
        }
        respond_composition(event);
        return;
    }
    if (event->keycode == LEONOS_KEY_BACKSPACE) {
        uint32_t length = text_len(composition);
        if (length) {
            composition[length - 1U] = 0;
            candidate_page = 0;
            if (composition[0]) {
                respond_composition(event);
            } else {
                respond(event, TEXT_INPUT_RESULT_CANCEL, "", 0, 0);
            }
        } else {
            respond(event, TEXT_INPUT_RESULT_PASSTHROUGH, "", 0, 0);
        }
        return;
    }
    if (event->keycode == 1U) {
        if (composition[0]) {
            clear_composition();
            respond(event, TEXT_INPUT_RESULT_CANCEL, "", 0, 0);
        } else {
            respond(event, TEXT_INPUT_RESULT_PASSTHROUGH, "", 0, 0);
        }
        return;
    }
    if (event->keycode == LEONOS_KEY_SPACE) {
        if (composition[0]) {
            commit_candidate(event, candidate_page_first(), 0);
        } else if (full_width) {
            respond(event, TEXT_INPUT_RESULT_COMMIT, "　", 0, 0);
        } else {
            respond(event, TEXT_INPUT_RESULT_PASSTHROUGH, "", 0, 0);
        }
        return;
    }
    if (event->keycode == LEONOS_KEY_ENTER) {
        if (composition[0]) {
            commit_candidate(event, candidate_page_first(), 0);
        } else {
            respond(event, TEXT_INPUT_RESULT_PASSTHROUGH, "", 0, 0);
        }
        return;
    }
    if (event->keycode >= 2U && event->keycode <= 6U && composition[0]) {
        commit_candidate(event, candidate_page_first() + event->keycode - 2U, 0);
        return;
    }
    if (composition[0] && event->keycode == 13U) {
        /* The '=' key and its Shift-modified '+' variant share scan code 13. */
        change_candidate_page(event, 1);
        return;
    }
    if (composition[0] && event->keycode == 12U) {
        change_candidate_page(event, -1);
        return;
    }
    if (key_to_ascii(event->keycode, &ch)) {
        punctuation = punctuation_for(ch);
        if (composition[0]) {
            commit_candidate(event, candidate_page_first(), punctuation ? punctuation : "");
        } else if (punctuation) {
            respond(event, TEXT_INPUT_RESULT_COMMIT, punctuation, 0, 0);
        } else {
            respond(event, TEXT_INPUT_RESULT_PASSTHROUGH, "", 0, 0);
        }
        return;
    }
    respond(event, TEXT_INPUT_RESULT_PASSTHROUGH, "", 0, 0);
}

int main(int argc, char **argv)
{
    text_input_provider_t provider = {0};
    text_input_key_event_t event = {0};
    struct leonos_user_info user = {0};
    uint32_t config_generation = 0;
    unsigned long last_config_check = 0;
    if (argc > 1 && argv && argv[1] && text_eq(argv[1], "--update")) {
        return update_dictionary();
    }
    copy_text(provider.id, sizeof(provider.id), OSCHINPT_ID);
    copy_text(provider.name, sizeof(provider.name), "LeonOS 4 Chinese Input");
    copy_text(provider.abbreviation, sizeof(provider.abbreviation), "OSC");
    provider.startup_mode = TEXT_INPUT_START_LOGIN;
    provider.render_flags = TEXT_INPUT_RENDER_CONTROLS;
    provider.enabled = 1;
    if (text_input_register(&provider) <= 0) {
        puts("[oschinpt] provider registration failed");
        return 1;
    }
    load_user_config();
    load_learning();
    if (!dictionary_index_load()) {
        puts("[oschinpt] dictionary index unavailable; using built-in candidates only");
    }
    if (leonos_auth_current(&user) == 0 && user.uid) {
        text_input_state_t state = {0};
        if (text_input_get_state(user.uid, &state) > 0) {
            config_generation = state.config_generation;
        }
    }
    puts("[oschinpt] LeonOS 4 Chinese Input ready");
    for (;;) {
        unsigned long now = leonos_uptime_ms();
        if (user.uid && now - last_config_check >= 200UL) {
            text_input_state_t state = {0};
            last_config_check = now;
            if (text_input_get_state(user.uid, &state) > 0 &&
                state.config_generation != config_generation) {
                config_generation = state.config_generation;
                load_user_config();
            }
        }
        if (text_input_provider_next(&event) > 0) {
            handle_event(&event);
            event = (text_input_key_event_t){0};
        } else {
            sleep_ms(8);
        }
    }
}
