#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/pty.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/system.h>
#include <leonos/ui.h>
#include "installer_sha256.h"
#include "installer_tty.h"
#include <stdlib.h>

#define INSTALLER_MAX_W 1920
#define INSTALLER_MAX_H 1080
#define INSTALLER_INITIAL_W 1280
#define INSTALLER_INITIAL_H 720
#define SIDEBAR_W 220
#define FOOTER_H 64
#define CONTENT_PAD 34
#define BUTTON_W 84
#define BUTTON_H LEONOS_UI_BUTTON_H
#define KEY_ESCAPE 1U
#define KEY_SPACE 57U
#define KEY_UP 72U
#define KEY_DOWN 80U
#define COPY_BUF_SIZE (256U * 1024U)
#define UPDATE_APP_ROW_H 24U
#define UPDATE_APP_MAX LEONOS_FS_MAX_ENTRIES
#define POLICY_SCROLLBAR_W 18U
#define POLICY_LINE_TEXT_MAX 256U
#define POLICY_MAX_LINES 192U
#define INSTALLER_CJK_FONT "/system/fonts/simsun.ttc"
#define INSTALL_ROOT_PAYLOAD "/install/root"
#define INSTALL_ESP_PAYLOAD "/install/esp"
#define INSTALL_ROOT_MOUNT "/target"
#define INSTALL_ESP_MOUNT "/target/boot"

/* The installer must be able to change language before it has a writable
 * target system.  Do not make rendering depend on persisting locale.conf on
 * the installation medium. */
#define T(en, zh) ((installer_lang == LEONOS_LANG_ZH && (zh)) ? (zh) : (en))

enum installer_page {
    PAGE_LANGUAGE = 0,
    PAGE_THANKS,
    PAGE_THEME,
    PAGE_WELCOME,
    PAGE_MODE,
    PAGE_DISK,
    PAGE_UPDATE_APPS,
    PAGE_CONFIRM,
    PAGE_PROGRESS,
    PAGE_FINISH,
    PAGE_COUNT,
};

enum installer_mode {
    INSTALL_MODE_FRESH = 0,
    INSTALL_MODE_UPDATE = 1,
};

enum installer_markdown_line_kind {
    POLICY_LINE_NORMAL = 0,
    POLICY_LINE_H1,
    POLICY_LINE_H2,
    POLICY_LINE_BULLET,
    POLICY_LINE_QUOTE,
    POLICY_LINE_RULE,
};

struct installer_layout {
    uint32_t sidebar_w;
    uint32_t footer_y;
    uint32_t content_x;
    uint32_t content_y;
    uint32_t content_w;
    uint32_t content_h;
    uint32_t table_w;
    uint32_t button_y;
    uint32_t back_x;
    uint32_t next_x;
    uint32_t cancel_x;
    uint32_t disk_refresh_x;
    uint32_t disk_refresh_y;
    uint32_t disk_header_y;
    uint32_t disk_list_y;
    uint32_t disk_list_h;
    uint32_t disk_status_y;
    uint32_t disk_detail_y;
    uint32_t confirm_edit_y;
};

struct installer_markdown_line {
    char text[POLICY_LINE_TEXT_MAX];
    uint8_t kind;
};

struct installer_text_view {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t text_x;
    uint32_t text_w;
    uint32_t checkbox_y;
};

struct update_app_entry {
    char name[LEONOS_FS_NAME_LEN];
    char src_package[LEONOS_FS_PATH_LEN];
    char dst_package[LEONOS_FS_PATH_LEN];
    char src_elf[LEONOS_FS_PATH_LEN];
    char dst_elf[LEONOS_FS_PATH_LEN];
    char src_icon[LEONOS_FS_PATH_LEN];
    char dst_icon[LEONOS_FS_PATH_LEN];
    uint8_t selected;
    uint8_t missing;
    uint8_t elf_diff;
    uint8_t icon_diff;
    uint8_t package_diff;
};

static uint32_t pixels[INSTALLER_MAX_W * INSTALLER_MAX_H];
static uint32_t surface_w = INSTALLER_INITIAL_W;
static uint32_t surface_h = INSTALLER_INITIAL_H;
static uint8_t page = PAGE_LANGUAGE;
static uint8_t install_mode = INSTALL_MODE_FRESH;
static uint8_t installer_lang = LEONOS_LANG_EN;
static uint8_t installer_theme = LEONOS_UI_THEME_METRO;
static uint8_t installer_theme_explicit;
static uint32_t acknowledgements_scroll_y;
static struct leonos_install_disk disks[LEONOS_INSTALL_MAX_DISKS];
static uint32_t disk_count;
static int32_t selected_disk = -1;
static char confirm_text[16];
static struct leonos_ui_edit_state confirm_edit;
static struct update_app_entry update_apps[UPDATE_APP_MAX];
static uint32_t update_app_count;
static struct leonos_ui_listview_state update_app_list;
static char status_text[128] = "Ready";
static char detail_text[128] = "";
static char progress_text[256] = "Ready";
static uint32_t progress_value;
static uint32_t copy_total;
static uint32_t copy_done;
static uint64_t copy_total_bytes;
static uint64_t copy_done_bytes;
static uint8_t install_success;
static uint8_t install_running;
static uint8_t dirty = 1;
static uint8_t installer_tty_mode;
static uint32_t tty_last_progress = 0xffffffffu;
static char tty_last_status[128];
static uint8_t copy_buf[COPY_BUF_SIZE];
static struct installer_markdown_line policy_lines[POLICY_MAX_LINES];
static uint32_t policy_line_count;

static const char acknowledgements_en[] =
    "# Acknowledgements\n"
    "\n"
    "LeonOS 4 gratefully acknowledges the creators, contributors, and maintainers of the public resources and open-source projects used by this release.\n"
    "\n"
    "## Runtime, Toolchain, and Applications\n"
    "- GNU GRUB 2 - GPL-3.0-or-later\n"
    "- Mbed TLS 2.28.8 - Apache License 2.0\n"
    "- Picolibc 1.8.12, including the LeonOS port - BSD\n"
    "- zlib 1.3.2, the bundled compression library - zlib License\n"
    "- libpng 1.6.58, the bundled PNG decoder library - libpng License\n"
    "- SQLite 3.46.1 - public-domain dedication and blessing\n"
    "- BusyBox 1.36.1 - GPL-2.0-only\n"
    "- ChenPi11/cmd - GPL-3.0-or-later\n"
    "- GNU less - Less License / GPL-3.0-or-later\n"
    "- Fastfetch 2.67.0 - MIT\n"
    "- sl - permissive upstream license\n"
    "- GNU nano 9.2 - GPL-3.0-or-later\n"
    "- TinyCC 0.9.28rc - LGPL-2.1-or-later\n"
    "- Lua 5.4.8 - MIT\n"
    "- Leonmmcoset/pl_editor (modified PL Editor fork) - MIT\n"
    "- file and libmagic - BSD-2-Clause\n"
    "- StardustUI - MIT\n"
    "- DoomGeneric - GPL-2.0-only\n"
    "- Freedoom - BSD-3-Clause\n"
    "- rime-pinyin-simp dictionary - Apache License 2.0\n"
    "- minimp3 - CC0-1.0\n"
    "\n"
    "## Fonts and Visual Resources\n"
    "- Noto Sans Mono - SIL Open Font License 1.1\n"
    "- Droid Sans Fallback - Apache License 2.0\n"
    "- Noto Sans CJK - SIL Open Font License 1.1\n"
    "- Microsoft fonts used by the distribution - subject to the applicable Microsoft license terms\n"
    "- NASA Image and Video Library, PIA18033 - used in accordance with NASA Media Usage Guidelines\n"
    "\n"
    "## Recorded Browser Source Dependencies\n"
    "- litehtml - BSD-3-Clause\n"
    "- Gumbo HTML Parser - Apache License 2.0\n"
    "\n"
    "These browser sources are retained for future compatibility work and are not linked into the current browser runtime.\n"
    "Complete license texts and attribution notices are preserved with the corresponding source, SDK, and/or installed program package.\n"
    "\n";

static const char acknowledgements_zh[] =
    "# 感谢\n"
    "\n"
    "LeonOS 4 诚挚感谢本发行版所使用的公共资源与开源项目的创作者、贡献者和维护者。\n"
    "\n"
    "## 运行时、开发工具链与应用程序\n"
    "- GNU GRUB 2 - GPL-3.0-or-later\n"
    "- Mbed TLS 2.28.8 - Apache License 2.0\n"
    "- Picolibc 1.8.12（含 LeonOS 移植）- BSD\n"
    "- zlib 1.3.2（内置压缩库）- zlib License\n"
    "- libpng 1.6.58（内置 PNG 解码库）- libpng License\n"
    "- SQLite 3.46.1 - 公共领域声明与许可祝福文本\n"
    "- BusyBox 1.36.1 - GPL-2.0-only\n"
    "- ChenPi11/cmd - GPL-3.0-or-later\n"
    "- GNU less - Less License / GPL-3.0-or-later\n"
    "- Fastfetch 2.67.0 - MIT\n"
    "- sl - 上游宽松许可\n"
    "- GNU nano 9.2 - GPL-3.0-or-later\n"
    "- TinyCC 0.9.28rc - LGPL-2.1-or-later\n"
    "- Lua 5.4.8 - MIT\n"
    "- Leonmmcoset/pl_editor（PL Editor 修改版）- MIT\n"
    "- file 与 libmagic - BSD-2-Clause\n"
    "- StardustUI - MIT\n"
    "- DoomGeneric - GPL-2.0-only\n"
    "- Freedoom - BSD-3-Clause\n"
    "- rime-pinyin-simp 中文词库 - Apache License 2.0\n"
    "- minimp3 - CC0-1.0\n"
    "\n"
    "## 字体与视觉资源\n"
    "- Noto Sans Mono - SIL Open Font License 1.1\n"
    "- Droid Sans Fallback - Apache License 2.0\n"
    "- Noto Sans CJK - SIL Open Font License 1.1\n"
    "- 发行版使用的 Microsoft 字体 - 受相应 Microsoft 许可条款约束\n"
    "- NASA Image and Video Library，PIA18033 - 遵循 NASA 媒体使用指南\n"
    "\n"
    "## 已记录的浏览器源码依赖\n"
    "- litehtml - BSD-3-Clause\n"
    "- Gumbo HTML Parser - Apache License 2.0\n"
    "\n"
    "这些浏览器源码为后续兼容性工作保留，当前浏览器运行时尚未链接它们。\n"
    "完整许可证文本与归属声明保留在相应源代码、开发套件和/或已安装程序包中。\n"
    "\n";

static int text_eq(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static int name_is_dot(const char *name)
{
    return text_eq(name, ".") || text_eq(name, "..");
}

static uint32_t text_len(const char *text)
{
    uint32_t len = 0;
    while (text && text[len]) {
        ++len;
    }
    return len;
}

static int text_ends_with(const char *text, const char *suffix)
{
    uint32_t text_n = text_len(text);
    uint32_t suffix_n = text_len(suffix);
    if (!suffix_n || suffix_n > text_n) {
        return 0;
    }
    return text_eq(text + text_n - suffix_n, suffix);
}

static void copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int append_char(char *buf, uint32_t *pos, uint32_t cap, char ch)
{
    if (!buf || !pos || *pos + 1 >= cap) {
        return -1;
    }
    buf[(*pos)++] = ch;
    buf[*pos] = 0;
    return 0;
}

static int append_text(char *buf, uint32_t *pos, uint32_t cap, const char *text)
{
    for (uint32_t i = 0; text && text[i]; ++i) {
        if (append_char(buf, pos, cap, text[i]) < 0) {
            return -1;
        }
    }
    return 0;
}

static int append_u64(char *buf, uint32_t *pos, uint32_t cap, uint64_t value)
{
    char tmp[24];
    uint32_t n = 0;
    if (value == 0) {
        return append_char(buf, pos, cap, '0');
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (n) {
        if (append_char(buf, pos, cap, tmp[--n]) < 0) {
            return -1;
        }
    }
    return 0;
}

static int append_i32(char *buf, uint32_t *pos, uint32_t cap, int32_t value)
{
    uint32_t mag;
    if (value < 0) {
        if (append_char(buf, pos, cap, '-') < 0) {
            return -1;
        }
        mag = (uint32_t)(-value);
    } else {
        mag = (uint32_t)value;
    }
    return append_u64(buf, pos, cap, mag);
}

static uint32_t policy_utf8_char(const char *text, uint32_t pos, uint32_t len,
                                 uint32_t *out_cells)
{
    uint8_t first;
    uint32_t bytes = 1;
    if (out_cells) {
        *out_cells = 1;
    }
    if (!text || pos >= len || !text[pos]) {
        return 0;
    }
    first = (uint8_t)text[pos];
    if (first >= 0xc2u && first < 0xe0u && pos + 1U < len &&
        (((uint8_t)text[pos + 1U] & 0xc0u) == 0x80u)) {
        bytes = 2;
    } else if (first >= 0xe0u && first < 0xf0u && pos + 2U < len &&
               (((uint8_t)text[pos + 1U] & 0xc0u) == 0x80u) &&
               (((uint8_t)text[pos + 2U] & 0xc0u) == 0x80u)) {
        bytes = 3;
    } else if (first >= 0xf0u && first < 0xf5u && pos + 3U < len &&
               (((uint8_t)text[pos + 1U] & 0xc0u) == 0x80u) &&
               (((uint8_t)text[pos + 2U] & 0xc0u) == 0x80u) &&
               (((uint8_t)text[pos + 3U] & 0xc0u) == 0x80u)) {
        bytes = 4;
    }
    if (out_cells && bytes >= 3U) {
        *out_cells = 2;
    }
    return bytes;
}

static uint32_t policy_text_cells(const char *text)
{
    uint32_t pos = 0;
    uint32_t cells = 0;
    uint32_t len = text_len(text);
    while (pos < len) {
        uint32_t char_cells = 1;
        uint32_t bytes = policy_utf8_char(text, pos, len, &char_cells);
        if (!bytes) {
            break;
        }
        cells += char_cells;
        pos += bytes;
    }
    return cells;
}

static uint32_t policy_utf8_prefix_len(const char *text, uint32_t len,
                                       uint32_t capacity)
{
    uint32_t pos = 0;
    while (pos < len && pos < capacity) {
        uint32_t bytes = policy_utf8_char(text, pos, len, 0);
        if (!bytes || bytes > capacity - pos) {
            break;
        }
        pos += bytes;
    }
    return pos;
}

static void policy_clean_inline(char *dst, uint32_t cap, const char *src)
{
    uint32_t out = 0;
    uint32_t pos = 0;
    uint32_t len = text_len(src);
    if (!dst || cap == 0) {
        return;
    }
    dst[0] = 0;
    while (pos < len) {
        uint32_t bytes = policy_utf8_char(src, pos, len, 0);
        if (!bytes) {
            break;
        }
        if (bytes == 1U && (src[pos] == '*' || src[pos] == '_' || src[pos] == '`')) {
            ++pos;
            continue;
        }
        if (out + bytes >= cap) {
            break;
        }
        for (uint32_t i = 0; i < bytes; ++i) {
            dst[out++] = src[pos + i];
        }
        dst[out] = 0;
        pos += bytes;
    }
}

static void policy_add_line(uint8_t kind, const char *text)
{
    if (policy_line_count >= POLICY_MAX_LINES) {
        return;
    }
    policy_lines[policy_line_count].kind = kind;
    copy_text(policy_lines[policy_line_count].text,
              sizeof(policy_lines[policy_line_count].text), text ? text : "");
    ++policy_line_count;
}

static int policy_line_is_rule(const char *line)
{
    uint32_t count = 0;
    for (uint32_t i = 0; line && line[i]; ++i) {
        if (line[i] == ' ' || line[i] == '\t') {
            continue;
        }
        if (line[i] != '-') {
            return 0;
        }
        ++count;
    }
    return count >= 3U;
}

static void policy_emit_wrapped(uint8_t kind, const char *prefix,
                                const char *text, uint32_t width)
{
    char line[POLICY_LINE_TEXT_MAX];
    uint32_t out = 0;
    uint32_t pos = 0;
    uint32_t max_cells = leonos_ui_text_fit_chars(width);
    uint32_t prefix_cells = policy_text_cells(prefix);
    uint32_t cells = prefix_cells;
    uint32_t source_len = text_len(text);
    if (max_cells < 16U) {
        max_cells = 16U;
    }
    line[0] = 0;
    (void)append_text(line, &out, sizeof(line), prefix);
    while (pos < source_len) {
        uint32_t char_cells = 1;
        uint32_t bytes = policy_utf8_char(text, pos, source_len, &char_cells);
        if (!bytes) {
            break;
        }
        if ((text[pos] == ' ' || text[pos] == '\t') && cells == prefix_cells) {
            pos += bytes;
            continue;
        }
        if (text[pos] != ' ' && text[pos] != '\t') {
            uint32_t word_pos = pos;
            uint32_t word_cells = 0;
            while (word_pos < source_len && text[word_pos] != ' ' && text[word_pos] != '\t') {
                uint32_t next_cells = 1;
                uint32_t next_bytes = policy_utf8_char(text, word_pos, source_len,
                                                       &next_cells);
                if (!next_bytes) {
                    break;
                }
                word_cells += next_cells;
                word_pos += next_bytes;
            }
            if (word_cells <= max_cells && cells > prefix_cells &&
                cells + word_cells > max_cells) {
                policy_add_line(kind, line);
                out = 0;
                line[0] = 0;
                if (prefix_cells) {
                    (void)append_text(line, &out, sizeof(line), "  ");
                    cells = 2;
                } else {
                    cells = 0;
                }
                continue;
            }
        }
        if (cells + char_cells > max_cells || out + bytes + 1U >= sizeof(line)) {
            policy_add_line(kind, line);
            out = 0;
            line[0] = 0;
            if (prefix_cells) {
                (void)append_text(line, &out, sizeof(line), "  ");
                cells = 2;
            } else {
                cells = 0;
            }
            if (text[pos] == ' ' || text[pos] == '\t') {
                pos += bytes;
                continue;
            }
        }
        for (uint32_t i = 0; i < bytes && out + 1U < sizeof(line); ++i) {
            line[out++] = text[pos + i];
        }
        line[out] = 0;
        cells += char_cells;
        pos += bytes;
    }
    if (out || prefix_cells) {
        policy_add_line(kind, line);
    }
}

static void markdown_reflow(const char *source, uint32_t width)
{
    uint32_t pos = 0;
    policy_line_count = 0;
    while (source[pos] && policy_line_count < POLICY_MAX_LINES) {
        char raw[512];
        char clean[sizeof(raw)];
        uint32_t line_start = pos;
        uint32_t line_end;
        uint32_t raw_len;
        char *line;
        while (source[pos] && source[pos] != '\n' && source[pos] != '\r') {
            ++pos;
        }
        line_end = pos;
        while (source[pos] == '\n' || source[pos] == '\r') {
            ++pos;
        }
        raw_len = line_end - line_start;
        raw_len = policy_utf8_prefix_len(source + line_start, raw_len,
                                         sizeof(raw) - 1U);
        for (uint32_t i = 0; i < raw_len; ++i) {
            raw[i] = source[line_start + i];
        }
        raw[raw_len] = 0;
        line = raw;
        while (*line == ' ' || *line == '\t') {
            ++line;
        }
        if (!line[0]) {
            policy_add_line(POLICY_LINE_NORMAL, "");
        } else if (line[0] == '#' && line[1] == '#' && line[2] == ' ') {
            policy_clean_inline(clean, sizeof(clean), line + 3);
            policy_emit_wrapped(POLICY_LINE_H2, "", clean, width);
        } else if (line[0] == '#' && line[1] == ' ') {
            policy_clean_inline(clean, sizeof(clean), line + 2);
            policy_emit_wrapped(POLICY_LINE_H1, "", clean, width);
        } else if (policy_line_is_rule(line)) {
            policy_add_line(POLICY_LINE_RULE, "");
        } else if (line[0] == '>' && (line[1] == ' ' || line[1] == '\t')) {
            policy_clean_inline(clean, sizeof(clean), line + 2);
            policy_emit_wrapped(POLICY_LINE_QUOTE, "", clean, width);
        } else if ((line[0] == '-' || line[0] == '*') &&
                   (line[1] == ' ' || line[1] == '\t')) {
            policy_clean_inline(clean, sizeof(clean), line + 2);
            policy_emit_wrapped(POLICY_LINE_BULLET, "- ", clean, width);
        } else {
            uint32_t ordered_end = 0;
            while (line[ordered_end] >= '0' && line[ordered_end] <= '9') {
                ++ordered_end;
            }
            if (ordered_end && line[ordered_end] == '.' && line[ordered_end + 1U] == ' ') {
                char prefix[16];
                uint32_t prefix_len = 0;
                for (uint32_t i = 0; i < ordered_end + 2U && prefix_len + 1U < sizeof(prefix); ++i) {
                    prefix[prefix_len++] = line[i];
                }
                prefix[prefix_len] = 0;
                policy_clean_inline(clean, sizeof(clean), line + ordered_end + 2U);
                policy_emit_wrapped(POLICY_LINE_BULLET, prefix, clean, width);
            } else {
                policy_clean_inline(clean, sizeof(clean), line);
                policy_emit_wrapped(POLICY_LINE_NORMAL, "", clean, width);
            }
        }
    }
}

static void acknowledgements_reflow(uint32_t width)
{
    markdown_reflow(installer_lang == LEONOS_LANG_ZH
                        ? acknowledgements_zh : acknowledgements_en,
                    width);
}

static uint32_t policy_line_height(uint8_t kind)
{
    if (kind == POLICY_LINE_H1) {
        return 24;
    }
    if (kind == POLICY_LINE_H2) {
        return 21;
    }
    if (kind == POLICY_LINE_RULE) {
        return 12;
    }
    return 18;
}

static uint32_t policy_total_height(void)
{
    uint32_t total = 0;
    for (uint32_t i = 0; i < policy_line_count; ++i) {
        total += policy_line_height(policy_lines[i].kind);
    }
    return total;
}

static void copy_replace_extension(char *dst, uint32_t cap,
                                   const char *path, const char *extension)
{
    uint32_t len;
    copy_text(dst, cap, path);
    len = text_len(dst);
    if (!extension || !text_ends_with(dst, ".elf") || len < 4) {
        return;
    }
    dst[len - 4] = 0;
    len -= 4;
    (void)append_text(dst, &len, cap, extension);
}

static void set_status(const char *status, const char *detail)
{
    copy_text(status_text, sizeof(status_text), status);
    copy_text(detail_text, sizeof(detail_text), detail);
}

static void set_progress_text(const char *status, const char *detail)
{
    uint32_t pos = 0;
    progress_text[0] = 0;
    (void)append_text(progress_text, &pos, sizeof(progress_text), status ? status : "");
    if (detail && detail[0]) {
        (void)append_text(progress_text, &pos, sizeof(progress_text), " - ");
        (void)append_text(progress_text, &pos, sizeof(progress_text), detail);
    }
}

static void set_error_status(const char *prefix, int ret)
{
    uint32_t pos = 0;
    detail_text[0] = 0;
    append_text(detail_text, &pos, sizeof(detail_text), prefix);
    append_text(detail_text, &pos, sizeof(detail_text), " ret=");
    append_i32(detail_text, &pos, sizeof(detail_text), ret);
    copy_text(status_text, sizeof(status_text), T("Installation failed", "安装失败"));
}

static int hit_rect_i(int32_t x, int32_t y, int32_t rx, int32_t ry,
                      int32_t rw, int32_t rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static void update_surface_size(uint32_t width, uint32_t height)
{
    if (width == 0 || width > INSTALLER_MAX_W) {
        width = INSTALLER_MAX_W;
    }
    if (height == 0 || height > INSTALLER_MAX_H) {
        height = INSTALLER_MAX_H;
    }
    surface_w = width;
    surface_h = height;
}

static void update_surface_size_from_framebuffer(void)
{
    struct leonos_fb_info fb;
    if (leonos_fb_info(&fb) >= 0) {
        update_surface_size(fb.width, fb.height);
    }
}

static struct installer_layout get_layout(void)
{
    struct installer_layout l;
    l.sidebar_w = surface_w > 900 ? SIDEBAR_W : 180;
    if (l.sidebar_w + CONTENT_PAD * 2 + 360 > surface_w) {
        l.sidebar_w = surface_w > 520 ? 160 : 0;
    }
    l.footer_y = surface_h > FOOTER_H ? surface_h - FOOTER_H : 0;
    l.content_x = l.sidebar_w + CONTENT_PAD;
    l.content_y = surface_h > 640 ? 64 : 44;
    l.content_w = surface_w > l.content_x + CONTENT_PAD ? surface_w - l.content_x - CONTENT_PAD : surface_w;
    if (l.content_w < 320 && surface_w > CONTENT_PAD * 2) {
        l.content_x = CONTENT_PAD;
        l.content_w = surface_w - CONTENT_PAD * 2;
    }
    l.content_h = l.footer_y > l.content_y + 20 ? l.footer_y - l.content_y - 20 : 120;
    l.table_w = l.content_w;
    if (l.table_w > 1120) {
        l.table_w = 1120;
    }
    l.button_y = l.footer_y + 20;
    if (l.button_y + BUTTON_H + 10 > surface_h) {
        l.button_y = surface_h > BUTTON_H + 12 ? surface_h - BUTTON_H - 12 : 0;
    }
    l.cancel_x = surface_w > BUTTON_W + 28 ? surface_w - BUTTON_W - 28 : 0;
    l.next_x = l.cancel_x > BUTTON_W + 10 ? l.cancel_x - BUTTON_W - 10 : 0;
    l.back_x = l.next_x > BUTTON_W + 10 ? l.next_x - BUTTON_W - 10 : 0;
    l.disk_refresh_y = l.content_y + 44;
    l.disk_refresh_x = l.content_x + l.table_w > 92 ? l.content_x + l.table_w - 92 : l.content_x;
    l.disk_header_y = l.content_y + 84;
    l.disk_list_y = l.disk_header_y + 24;
    l.disk_list_h = l.content_h > 210 ? l.content_h - 146 : 150;
    if (l.disk_list_h > 360) {
        l.disk_list_h = 360;
    }
    l.disk_status_y = l.disk_list_y + l.disk_list_h + 16;
    l.disk_detail_y = l.disk_status_y + 32;
    l.confirm_edit_y = l.content_y + 200;
    return l;
}

static struct installer_text_view get_acknowledgements_view(void)
{
    struct installer_layout l = get_layout();
    struct installer_text_view view;
    view.x = l.content_x;
    view.y = l.content_y + 56;
    view.w = l.table_w;
    view.h = l.footer_y > view.y + 12 ? l.footer_y - view.y - 12 : 64;
    view.text_x = view.x + 10;
    view.text_w = view.w > POLICY_SCROLLBAR_W + 30
                      ? view.w - POLICY_SCROLLBAR_W - 30
                      : 80;
    view.checkbox_y = 0;
    return view;
}

static int path_join(char *dst, uint32_t cap, const char *base, const char *name)
{
    uint32_t pos = 0;
    if (!dst || cap == 0 || !base || !name) {
        return -1;
    }
    dst[0] = 0;
    if (append_text(dst, &pos, cap, base) < 0) {
        return -1;
    }
    if (pos > 0 && dst[pos - 1] != '/') {
        if (append_char(dst, &pos, cap, '/') < 0) {
            return -1;
        }
    }
    return append_text(dst, &pos, cap, name);
}

static const char *mode_action_text(void)
{
    return install_mode == INSTALL_MODE_UPDATE ? T("Update", "更新") : T("Install", "安装");
}

static const char *mode_progress_title(void)
{
    return install_mode == INSTALL_MODE_UPDATE ? T("Updating LeonOS 4", "正在更新 LeonOS 4")
                                               : T("Installing LeonOS 4", "正在安装 LeonOS 4");
}

static void set_disk_select_status(void)
{
    if (install_mode == INSTALL_MODE_UPDATE) {
        set_status(T("Select the disk to update", "请选择要更新的硬盘"),
                   T("Setup will check for an existing LeonOS 4 system.", "安装程序会检测现有 LeonOS 4 系统。"));
    } else {
        set_status(T("Select the target disk", "请选择目标硬盘"),
                   T("The selected disk will be erased.", "所选硬盘将被清空。"));
    }
}

static void reset_update_app_list(void)
{
    update_app_count = 0;
    leonos_ui_listview_state_set_count(&update_app_list, 0);
    update_app_list.selected = -1;
    update_app_list.scroll = 0;
}

static void format_disk_line(char *buf, uint32_t cap,
                             const struct leonos_install_disk *disk)
{
    uint32_t pos = 0;
    uint64_t mib = 0;
    if (!disk) {
        copy_text(buf, cap, "");
        return;
    }
    if (disk->sector_size) {
        mib = (disk->sector_count * (uint64_t)disk->sector_size) / (1024ULL * 1024ULL);
    }
    buf[0] = 0;
    append_text(buf, &pos, cap, "Disk ");
    append_u64(buf, &pos, cap, disk->id);
    append_text(buf, &pos, cap, "  ");
    append_text(buf, &pos, cap, disk->name[0] ? disk->name : "Disk");
    append_text(buf, &pos, cap, " port ");
    append_u64(buf, &pos, cap, disk->port);
    append_text(buf, &pos, cap, "  ");
    if (mib >= 1024) {
        append_u64(buf, &pos, cap, mib / 1024);
        append_text(buf, &pos, cap, " GiB");
    } else {
        append_u64(buf, &pos, cap, mib);
        append_text(buf, &pos, cap, " MiB");
    }
    if (disk->flags & LEONOS_INSTALL_DISK_FLAG_BOOT_ROOT) {
        append_text(buf, &pos, cap, "  boot");
    }
    if (disk->flags & LEONOS_INSTALL_DISK_FLAG_TARGET_MOUNTED) {
        append_text(buf, &pos, cap, "  mounted");
    }
}

static void reset_confirm(void)
{
    confirm_text[0] = 0;
    leonos_ui_edit_state_init(&confirm_edit, confirm_text, sizeof(confirm_text));
    confirm_edit.focused = 1;
}

static int confirmation_ok(void)
{
    return install_mode == INSTALL_MODE_UPDATE ? text_eq(confirm_text, "UPDATE")
                                               : text_eq(confirm_text, "INSTALL");
}

static void refresh_disks(void)
{
    uint32_t count = 0;
    int ret = leonos_install_list_disks(disks, LEONOS_INSTALL_MAX_DISKS, &count);
    if (ret < 0) {
        disk_count = 0;
        selected_disk = -1;
        set_error_status("Could not list disks", ret);
        dirty = 1;
        return;
    }
    disk_count = count;
    if (disk_count == 0) {
        selected_disk = -1;
        set_status(T("No disks were found", "未找到硬盘"), T("Attach a disk and click Refresh.", "连接硬盘后点击刷新。"));
    } else {
        if (selected_disk < 0 || (uint32_t)selected_disk >= disk_count) {
            selected_disk = 0;
        }
        set_disk_select_status();
    }
    dirty = 1;
}

static void draw_sidebar(struct leonos_ui_surface *ui)
{
    struct installer_layout l = get_layout();
    if (!l.sidebar_w) {
        return;
    }
    leonos_ui_rect(ui, 0, 0, l.sidebar_w, surface_h, LEONOS_UI_ACTIVE_TITLE);
    leonos_ui_text(ui, 18, 24, "LeonOS 4", LEONOS_UI_WHITE, LEONOS_UI_ACTIVE_TITLE);
    leonos_ui_text(ui, 18, 48, T("Setup", "安装"), LEONOS_UI_WHITE, LEONOS_UI_ACTIVE_TITLE);
    for (uint32_t i = 0; i < PAGE_COUNT; ++i) {
        uint32_t y = 104 + i * 34;
        uint32_t fg = i == page ? LEONOS_UI_BLACK : LEONOS_UI_WHITE;
        uint32_t bg = i == page ? LEONOS_UI_LIGHT : LEONOS_UI_ACTIVE_TITLE;
        if (i == page) {
            leonos_ui_rect(ui, 12, y - 6, l.sidebar_w > 34 ? l.sidebar_w - 34 : l.sidebar_w, 24, bg);
        }
        const char *label = "";
        if (i == PAGE_LANGUAGE) {
            label = T("Language", "语言");
        } else if (i == PAGE_THANKS) {
            label = T("Thanks", "感谢");
        } else if (i == PAGE_THEME) {
            label = T("Style", "样式");
        } else if (i == PAGE_WELCOME) {
            label = T("Welcome", "欢迎");
        } else if (i == PAGE_MODE) {
            label = T("Mode", "模式");
        } else if (i == PAGE_DISK) {
            label = T("Disk", "硬盘");
        } else if (i == PAGE_UPDATE_APPS) {
            label = T("Apps", "程序");
        } else if (i == PAGE_CONFIRM) {
            label = T("Confirm", "确认");
        } else if (i == PAGE_PROGRESS) {
            label = mode_action_text();
        } else if (i == PAGE_FINISH) {
            label = T("Finish", "完成");
        }
        leonos_ui_text(ui, 20, y, label, fg, bg);
    }
}

static void draw_title(struct leonos_ui_surface *ui, const char *title,
                       const char *subtitle)
{
    struct installer_layout l = get_layout();
    leonos_ui_text(ui, l.content_x, l.content_y, title, LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    if (subtitle) {
        leonos_ui_text_clipped(ui, l.content_x, l.content_y + 26, l.content_w,
                               subtitle, LEONOS_UI_DARK, LEONOS_UI_WHITE);
    }
}

static uint32_t primary_disabled(void)
{
    if (install_running) {
        return 1;
    }
    if (page == PAGE_DISK) {
        return selected_disk < 0 || (uint32_t)selected_disk >= disk_count;
    }
    if (page == PAGE_CONFIRM) {
        return !confirmation_ok();
    }
    return 0;
}

static const char *primary_label(void)
{
    if (page == PAGE_CONFIRM) {
        return install_mode == INSTALL_MODE_UPDATE ? T("Update", "更新")
                                                   : T("Install", "安装");
    }
    if (page == PAGE_FINISH && install_success) {
        return T("Restart", "重启");
    }
    if (page == PAGE_FINISH) {
        return T("Close", "关闭");
    }
    return T("Next", "下一步");
}

static void draw_footer(struct leonos_ui_surface *ui)
{
    struct installer_layout l = get_layout();
    uint32_t back_disabled = page == PAGE_LANGUAGE || page == PAGE_PROGRESS ||
                             (page == PAGE_FINISH && install_success);
    uint32_t cancel_disabled = page == PAGE_PROGRESS ||
                               (page == PAGE_FINISH && install_success);
    leonos_ui_rect(ui, l.sidebar_w, l.footer_y, surface_w > l.sidebar_w ? surface_w - l.sidebar_w : surface_w, 1, LEONOS_UI_DARK);
    leonos_ui_rect(ui, l.sidebar_w, l.footer_y + 1, surface_w > l.sidebar_w ? surface_w - l.sidebar_w : surface_w, surface_h > l.footer_y + 1 ? surface_h - l.footer_y - 1 : 0, LEONOS_UI_GRAY);
    leonos_ui_button(ui, l.back_x, l.button_y, BUTTON_W, BUTTON_H, T("Back", "上一步"),
                     back_disabled ? LEONOS_UI_BUTTON_DISABLED : 0);
    leonos_ui_button(ui, l.next_x, l.button_y, BUTTON_W, BUTTON_H, primary_label(),
                     primary_disabled() ? LEONOS_UI_BUTTON_DISABLED : 0);
    leonos_ui_button(ui, l.cancel_x, l.button_y, BUTTON_W, BUTTON_H, T("Cancel", "取消"),
                     cancel_disabled ? LEONOS_UI_BUTTON_DISABLED : 0);
}

static void draw_language_page(struct leonos_ui_surface *ui)
{
    struct installer_layout l = get_layout();
    draw_title(ui, T("Select Language", "选择语言"), T("Choose the language for Setup and the installed system.", "选择安装程序和安装后系统使用的语言。"));
    leonos_ui_button(ui, l.content_x, l.content_y + 88, 140, BUTTON_H, "English",
                     installer_lang == LEONOS_LANG_EN ? LEONOS_UI_BUTTON_PRESSED : 0);
    leonos_ui_button(ui, l.content_x + 156, l.content_y + 88, 140, BUTTON_H, "中文",
                     installer_lang == LEONOS_LANG_ZH ? LEONOS_UI_BUTTON_PRESSED : 0);
    leonos_ui_text(ui, l.content_x, l.content_y + 140,
                   T("The installed system will use the same language.",
                     "安装后的操作系统将使用相同语言。"),
                   LEONOS_UI_DARK, LEONOS_UI_WHITE);
}

static void draw_acknowledgements_page(struct leonos_ui_surface *ui)
{
    struct installer_text_view view = get_acknowledgements_view();
    uint32_t total_h;
    uint32_t max_scroll;
    uint32_t offset = 0;
    draw_title(ui, T("Thank You", "感谢"),
               T("Acknowledgements for public resources and open-source projects.",
                 "公共资源与开源项目致谢。"));
    acknowledgements_reflow(view.text_w);
    total_h = policy_total_height();
    max_scroll = total_h > view.h ? total_h - view.h : 0;
    if (acknowledgements_scroll_y > max_scroll) {
        acknowledgements_scroll_y = max_scroll;
    }
    leonos_ui_scroll_view_frame(ui, view.x, view.y, view.w, view.h);
    for (uint32_t i = 0; i < policy_line_count; ++i) {
        uint32_t line_h = policy_line_height(policy_lines[i].kind);
        int32_t line_y = (int32_t)view.y + (int32_t)offset -
                         (int32_t)acknowledgements_scroll_y;
        if (line_y >= (int32_t)view.y &&
            line_y + (int32_t)line_h <= (int32_t)(view.y + view.h)) {
            if (policy_lines[i].kind == POLICY_LINE_H1) {
                leonos_ui_text_resized_clipped(ui, view.text_x, (uint32_t)line_y,
                                                view.text_w, policy_lines[i].text,
                                                LEONOS_UI_ACTIVE_TITLE, LEONOS_UI_WHITE, 9, 18);
            } else if (policy_lines[i].kind == POLICY_LINE_H2) {
                leonos_ui_text_resized_clipped(ui, view.text_x, (uint32_t)line_y,
                                                view.text_w, policy_lines[i].text,
                                                LEONOS_UI_ACTIVE_TITLE, LEONOS_UI_WHITE, 9, 17);
            } else if (policy_lines[i].kind == POLICY_LINE_RULE) {
                leonos_ui_rect(ui, view.text_x, (uint32_t)line_y + 5,
                               view.text_w, 1, LEONOS_UI_DARK);
            } else if (policy_lines[i].kind == POLICY_LINE_QUOTE) {
                leonos_ui_rect(ui, view.text_x, (uint32_t)line_y + 1, 3,
                               line_h > 2 ? line_h - 2 : line_h, LEONOS_UI_ACTIVE_TITLE);
                leonos_ui_text_clipped(ui, view.text_x + 9, (uint32_t)line_y,
                                       view.text_w > 9 ? view.text_w - 9 : view.text_w,
                                       policy_lines[i].text, LEONOS_UI_DARK, LEONOS_UI_WHITE);
            } else {
                leonos_ui_text_clipped(ui, view.text_x, (uint32_t)line_y,
                                       view.text_w, policy_lines[i].text,
                                       policy_lines[i].kind == POLICY_LINE_BULLET
                                           ? LEONOS_UI_BLACK : LEONOS_UI_DARK,
                                       LEONOS_UI_WHITE);
            }
        }
        offset += line_h;
    }
    leonos_ui_vscrollbar(ui, view.x + view.w - POLICY_SCROLLBAR_W, view.y,
                         POLICY_SCROLLBAR_W, view.h, acknowledgements_scroll_y,
                         total_h > view.h ? total_h : view.h, view.h,
                         total_h <= view.h ? LEONOS_UI_SCROLLBAR_DISABLED : 0);
}

static void draw_theme_page(struct leonos_ui_surface *ui)
{
    struct installer_layout l = get_layout();
    draw_title(ui, T("Choose UI Style", "选择界面样式"),
               T("Preview a style now and apply it to the installed system.",
                 "可立即预览样式，安装后系统也会使用此样式。"));
    leonos_ui_button(ui, l.content_x, l.content_y + 88, 140, BUTTON_H, "Metro",
                     installer_theme == LEONOS_UI_THEME_METRO
                         ? LEONOS_UI_BUTTON_PRESSED : 0);
    leonos_ui_button(ui, l.content_x + 156, l.content_y + 88, 140, BUTTON_H, "Win95",
                     installer_theme == LEONOS_UI_THEME_WIN95
                         ? LEONOS_UI_BUTTON_PRESSED : 0);
    leonos_ui_text(ui, l.content_x, l.content_y + 140,
                   installer_theme == LEONOS_UI_THEME_METRO
                       ? T("Metro uses the modern flat system appearance.",
                           "Metro 使用现代扁平化系统外观。")
                       : T("Win95 keeps the classic beveled system appearance.",
                           "Win95 保留经典立体系统外观。"),
                   LEONOS_UI_DARK, LEONOS_UI_WHITE);
}

static void draw_welcome(struct leonos_ui_surface *ui)
{
    struct installer_layout l = get_layout();
    draw_title(ui, T("LeonOS 4 Setup", "LeonOS 4 安装程序"), T("Install a new system or update an existing LeonOS 4 disk.", "全新安装系统，或更新现有 LeonOS 4 硬盘。"));
    leonos_ui_text(ui, l.content_x, l.content_y + 84, T("Setup can copy the full normal system payload", "安装程序可以复制完整的普通系统文件"), LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, l.content_x, l.content_y + 108, T("or replace the boot/system files on an existing installation.", "也可以替换现有安装中的启动和系统文件。"), LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, l.content_x, l.content_y + 164, T("SATA/AHCI and IDE/PATA target disks are supported.", "支持 SATA/AHCI 和 IDE/PATA 目标硬盘。"), LEONOS_UI_DARK, LEONOS_UI_WHITE);
}

static void draw_mode_page(struct leonos_ui_surface *ui)
{
    struct installer_layout l = get_layout();
    uint32_t card_w = l.content_w > 620 ? 280 : l.content_w;
    draw_title(ui, T("Choose Setup Mode", "选择安装模式"), T("Fresh install erases the disk. Update keeps existing users and extra programs.", "全新安装会清空硬盘；更新会保留用户和额外程序。"));
    leonos_ui_button(ui, l.content_x, l.content_y + 84, card_w, BUTTON_H,
                     T("Fresh Install", "全新安装"),
                     install_mode == INSTALL_MODE_FRESH ? LEONOS_UI_BUTTON_PRESSED : 0);
    leonos_ui_text_clipped(ui, l.content_x, l.content_y + 122, card_w,
                           T("Format the selected disk and copy a clean LeonOS 4 system.",
                             "格式化所选硬盘并复制干净的 LeonOS 4 系统。"),
                           LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_button(ui, l.content_x, l.content_y + 180, card_w, BUTTON_H,
                     T("Update Existing System", "更新现有系统"),
                     install_mode == INSTALL_MODE_UPDATE ? LEONOS_UI_BUTTON_PRESSED : 0);
    leonos_ui_text_clipped(ui, l.content_x, l.content_y + 218, l.content_w,
                           T("Replace boot, system, EFI and bundled docs. Then choose changed or missing system apps.",
                             "替换 boot、system、EFI 和内置文档，然后选择变化或缺失的系统程序。"),
                           LEONOS_UI_DARK, LEONOS_UI_WHITE);
}

static void draw_disk_page(struct leonos_ui_surface *ui)
{
    struct installer_layout l = get_layout();
    char line[128];
    draw_title(ui,
               install_mode == INSTALL_MODE_UPDATE ? T("Select Disk to Update", "选择要更新的硬盘")
                                                   : T("Select Installation Disk", "选择安装硬盘"),
               install_mode == INSTALL_MODE_UPDATE ? T("Choose the disk that already contains LeonOS 4.", "选择已经包含 LeonOS 4 的硬盘。")
                                                   : T("Choose the disk that will receive LeonOS.", "选择用于安装 LeonOS 的硬盘。"));
    leonos_ui_button(ui, l.disk_refresh_x, l.disk_refresh_y, 92, BUTTON_H, T("Refresh", "刷新"), 0);
    leonos_ui_list_header(ui, l.content_x, l.disk_header_y, l.table_w, T("Available disks", "可用硬盘"));
    leonos_ui_inset(ui, l.content_x, l.disk_list_y, l.table_w, l.disk_list_h, LEONOS_UI_WHITE);
    if (disk_count == 0) {
        leonos_ui_text(ui, l.content_x + 12, l.disk_list_y + 20, T("No disks were found.", "未找到硬盘。"), LEONOS_UI_DARK, LEONOS_UI_WHITE);
    }
    for (uint32_t i = 0; i < disk_count && i < LEONOS_INSTALL_MAX_DISKS; ++i) {
        uint32_t row_y = l.disk_list_y + 2 + i * 24;
        if (row_y + 22 > l.disk_list_y + l.disk_list_h) {
            break;
        }
        format_disk_line(line, sizeof(line), &disks[i]);
        leonos_ui_list_row(ui, l.content_x + 2, row_y,
                           l.table_w > 4 ? l.table_w - 4 : l.table_w, line,
                           selected_disk == (int32_t)i ? LEONOS_UI_MENU_SELECTED : 0);
    }
    leonos_ui_inset(ui, l.content_x, l.disk_status_y, l.table_w, 26, LEONOS_UI_LIGHT);
    leonos_ui_text_clipped(ui, l.content_x + 8, l.disk_status_y + 5,
                           l.table_w > 16 ? l.table_w - 16 : l.table_w,
                           status_text, LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
    leonos_ui_text_clipped(ui, l.content_x, l.disk_detail_y, l.table_w, detail_text, LEONOS_UI_DARK, LEONOS_UI_WHITE);
}

static void format_update_reason(char *buf, uint32_t cap,
                                 const struct update_app_entry *entry)
{
    uint32_t pos = 0;
    buf[0] = 0;
    if (!entry) {
        return;
    }
    append_text(buf, &pos, cap, entry->name);
    append_text(buf, &pos, cap, "  -  ");
    if (entry->missing) {
        append_text(buf, &pos, cap, T("missing on target", "目标中缺失"));
    } else if (entry->elf_diff && entry->icon_diff) {
        append_text(buf, &pos, cap, T("program and icon differ", "程序和图标不同"));
    } else if (entry->elf_diff) {
        append_text(buf, &pos, cap, T("program differs", "程序不同"));
    } else if (entry->icon_diff) {
        append_text(buf, &pos, cap, T("icon differs", "图标不同"));
    } else if (entry->package_diff) {
        append_text(buf, &pos, cap, T("package files differ", "程序包文件不同"));
    } else {
        append_text(buf, &pos, cap, T("will be replaced", "将被替换"));
    }
}

static void sync_update_list_layout(uint32_t list_h)
{
    uint32_t visible_rows = list_h / UPDATE_APP_ROW_H;
    if (visible_rows == 0) {
        visible_rows = 1;
    }
    update_app_list.visible_rows = visible_rows;
    update_app_list.row_height = UPDATE_APP_ROW_H;
    leonos_ui_listview_state_set_count(&update_app_list, update_app_count);
}

static void draw_update_apps_page(struct leonos_ui_surface *ui)
{
    struct installer_layout l = get_layout();
    uint32_t header_y = l.content_y + 112;
    uint32_t list_y = header_y + 24;
    uint32_t list_h = l.content_h > 220 ? l.content_h - 178 : 120;
    uint32_t list_w = l.table_w > 22 ? l.table_w - 22 : l.table_w;
    char line[160];
    sync_update_list_layout(list_h);
    draw_title(ui, T("Program and Driver Updates", "程序和驱动程序更新"),
               T("Changed or missing programs are selected; changed drivers are refreshed automatically.",
                 "变化或缺失的程序默认勾选；变化的驱动程序会自动刷新。"));
    leonos_ui_text_clipped(ui, l.content_x, l.content_y + 72, l.content_w,
                           T("boot, system/lib, kerneldebug, EFI, docs, and drivers will be refreshed. Extra target programs and drivers are kept.",
                             "boot、system/lib、kerneldebug、EFI、内置文档和驱动程序会刷新；目标中多出的程序和驱动会保留。"),
                           LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_list_header(ui, l.content_x, header_y, list_w,
                          T("Programs to replace", "要替换的程序"));
    leonos_ui_inset(ui, l.content_x, list_y, list_w, list_h, LEONOS_UI_WHITE);
    if (update_app_count == 0) {
        leonos_ui_text(ui, l.content_x + 12, list_y + 20,
                       T("No program package differences were found.", "未发现程序包差异。"),
                       LEONOS_UI_DARK, LEONOS_UI_WHITE);
    }
    for (uint32_t row = 0; row < update_app_list.visible_rows; ++row) {
        uint32_t i = update_app_list.scroll + row;
        uint32_t row_y = list_y + 2 + row * UPDATE_APP_ROW_H;
        uint32_t selected = update_app_list.selected == (int32_t)i;
        uint32_t bg = selected ? LEONOS_UI_ACTIVE_TITLE : LEONOS_UI_WHITE;
        uint32_t fg = selected ? LEONOS_UI_WHITE : LEONOS_UI_BLACK;
        if (i >= update_app_count || row_y + UPDATE_APP_ROW_H > list_y + list_h) {
            break;
        }
        leonos_ui_rect(ui, l.content_x + 2, row_y,
                       list_w > 4 ? list_w - 4 : list_w, UPDATE_APP_ROW_H, bg);
        leonos_ui_checkbox(ui, l.content_x + 8, row_y + 3, "",
                           update_apps[i].selected, 0);
        format_update_reason(line, sizeof(line), &update_apps[i]);
        leonos_ui_text_clipped(ui, l.content_x + 34, row_y + 5,
                               list_w > 42 ? list_w - 42 : list_w,
                               line, fg, bg);
    }
    leonos_ui_vscrollbar(ui, l.content_x + list_w, list_y, 18, list_h,
                         update_app_list.scroll,
                         update_app_count > update_app_list.visible_rows
                             ? update_app_count : update_app_list.visible_rows,
                         update_app_list.visible_rows,
                         update_app_count <= update_app_list.visible_rows
                             ? LEONOS_UI_SCROLLBAR_DISABLED : 0);
    leonos_ui_text_clipped(ui, l.content_x, list_y + list_h + 14, l.content_w,
                           status_text, LEONOS_UI_DARK, LEONOS_UI_WHITE);
}

static void draw_confirm_page(struct leonos_ui_surface *ui)
{
    struct installer_layout l = get_layout();
    char line[128];
    draw_title(ui,
               install_mode == INSTALL_MODE_UPDATE ? T("Confirm Update", "确认更新")
                                                   : T("Confirm Installation", "确认安装"),
               install_mode == INSTALL_MODE_UPDATE ? T("System folders and runtime files will be replaced.", "系统文件夹和运行时文件将被替换。")
                                                   : T("This operation is destructive.", "此操作会清空目标硬盘。"));
    if (selected_disk >= 0 && (uint32_t)selected_disk < disk_count) {
        format_disk_line(line, sizeof(line), &disks[selected_disk]);
        leonos_ui_text(ui, l.content_x, l.content_y + 78, T("Target:", "目标:"), LEONOS_UI_BLACK, LEONOS_UI_WHITE);
        leonos_ui_text_clipped(ui, l.content_x + 70, l.content_y + 78,
                               l.content_w > 70 ? l.content_w - 70 : l.content_w,
                               line, LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    }
    leonos_ui_text_clipped(ui, l.content_x, l.content_y + 130, l.content_w,
                           install_mode == INSTALL_MODE_UPDATE
                       ? T("The FAT32 boot partition, exFAT or ext2 system files, dynamic runtime libraries, kernel debugger, and bundled docs will be refreshed. Selected program packages will be refreshed.",
                                   "FAT32 启动分区、exFAT 或 ext2 系统文件、动态运行库、内核调试模块和内置文档会刷新；已选程序包会刷新。")
                               : T("The selected disk will be erased and formatted with a FAT32 ESP and exFAT system root.",
                                   "所选硬盘会被清空并格式化为 FAT32 ESP 和 exFAT 系统根分区。"),
                           LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, l.content_x, l.content_y + 174,
                   install_mode == INSTALL_MODE_UPDATE
                       ? T("Type UPDATE to enable the Update button.", "输入 UPDATE 以启用更新按钮。")
                       : T("Type INSTALL to enable the Install button.", "输入 INSTALL 以启用安装按钮。"),
                   LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_edit_state_draw(ui, l.content_x, l.confirm_edit_y, 220, &confirm_edit, 0);
}

static void draw_progress_page(struct leonos_ui_surface *ui)
{
    struct installer_layout l = get_layout();
    draw_title(ui, mode_progress_title(), T("Do not turn off this machine.", "请勿关闭此计算机。"));
    leonos_ui_text_clipped(ui, l.content_x, l.content_y + 94, l.content_w,
                           progress_text, LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_progress(ui, l.content_x, l.content_y + 130, l.content_w, 24, progress_value, 100);
}

static void draw_finish_page(struct leonos_ui_surface *ui)
{
    struct installer_layout l = get_layout();
    if (install_success) {
        draw_title(ui,
                   install_mode == INSTALL_MODE_UPDATE ? T("Update Complete", "更新完成")
                                                       : T("Installation Complete", "安装完成"),
                   install_mode == INSTALL_MODE_UPDATE ? T("LeonOS was updated on the selected disk.", "所选硬盘上的 LeonOS 已更新。")
                                                       : T("LeonOS was installed to the selected disk.", "LeonOS 已安装到所选硬盘。"));
        leonos_ui_text(ui, l.content_x, l.content_y + 96, T("Remove the installation media, then restart.", "移除安装介质，然后重启。"), LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    } else {
        draw_title(ui,
                   install_mode == INSTALL_MODE_UPDATE ? T("Update Failed", "更新失败")
                                                       : T("Installation Failed", "安装失败"),
                   T("No writes will continue after this error.", "出现此错误后不会继续写入。"));
        leonos_ui_text(ui, l.content_x, l.content_y + 96, status_text, LEONOS_UI_BLACK, LEONOS_UI_WHITE);
        leonos_ui_text_clipped(ui, l.content_x, l.content_y + 130, l.content_w, detail_text, LEONOS_UI_DARK, LEONOS_UI_WHITE);
    }
}

static void draw_installer(struct leonos_ui_surface *ui)
{
    leonos_ui_rect(ui, 0, 0, surface_w, surface_h, LEONOS_UI_WHITE);
    draw_sidebar(ui);
    switch (page) {
    case PAGE_LANGUAGE:
        draw_language_page(ui);
        break;
    case PAGE_THANKS:
        draw_acknowledgements_page(ui);
        break;
    case PAGE_THEME:
        draw_theme_page(ui);
        break;
    case PAGE_WELCOME:
        draw_welcome(ui);
        break;
    case PAGE_MODE:
        draw_mode_page(ui);
        break;
    case PAGE_DISK:
        draw_disk_page(ui);
        break;
    case PAGE_UPDATE_APPS:
        draw_update_apps_page(ui);
        break;
    case PAGE_CONFIRM:
        draw_confirm_page(ui);
        break;
    case PAGE_PROGRESS:
        draw_progress_page(ui);
        break;
    case PAGE_FINISH:
    default:
        draw_finish_page(ui);
        break;
    }
    draw_footer(ui);
}

static void present_installer(int window_id, struct leonos_ui_surface *ui)
{
    if (installer_tty_mode || !ui || window_id <= 0) {
        return;
    }
    draw_installer(ui);
    leonos_gui_present_window((uint32_t)window_id, surface_w, surface_h,
                              INSTALLER_MAX_W, pixels);
    dirty = 0;
}

static void show_progress(int window_id, struct leonos_ui_surface *ui,
                          uint32_t value, const char *status,
                          const char *detail)
{
    if (value > 100) {
        value = 100;
    }
    progress_value = value;
    set_status(status, detail);
    set_progress_text(status, detail);
    if (installer_tty_mode) {
        /* File copying calls this for every file. Keep the CLI readable by
         * emitting only stage changes and new percentage values. */
        if (value != tty_last_progress || !text_eq(progress_text, tty_last_status)) {
            printf("[%3u%%] %s\n", value, progress_text);
            tty_last_progress = value;
            copy_text(tty_last_status, sizeof(tty_last_status), progress_text);
        }
        return;
    }
    present_installer(window_id, ui);
}

static uint32_t copy_progress_percent(void)
{
    if (copy_total_bytes == 0) {
        return copy_total ? 35 + (copy_done * 60U) / copy_total : 35;
    }
    if (copy_done_bytes > copy_total_bytes) {
        copy_done_bytes = copy_total_bytes;
    }
    return 35 + (uint32_t)((copy_done_bytes * 60ULL) / copy_total_bytes);
}

static void show_copy_progress(int window_id, struct leonos_ui_surface *ui,
                               const char *detail)
{
    show_progress(window_id, ui, copy_progress_percent(),
                  T("Copying system files", "正在复制系统文件"), detail);
}

/* Each recursive traversal retains its parent listing while descending. Keep
 * that 8 KiB listing off the small user stack. */
static struct leonos_dir_entry *alloc_dir_entries(void)
{
    return (struct leonos_dir_entry *)malloc(sizeof(struct leonos_dir_entry) *
                                             LEONOS_FS_MAX_ENTRIES);
}

static int count_files_recursive(const char *src, uint32_t *out_count)
{
    struct leonos_dir_entry *entries = alloc_dir_entries();
    uint32_t count = 0;
    int ret;
    if (!entries) {
        return -12;
    }
    ret = leonos_list_dir(src, entries, LEONOS_FS_MAX_ENTRIES, &count);
    if (ret < 0) {
        goto out;
    }
    for (uint32_t i = 0; i < count; ++i) {
        char child[LEONOS_FS_PATH_LEN];
        if (name_is_dot(entries[i].name)) {
            continue;
        }
        if (path_join(child, sizeof(child), src, entries[i].name) < 0) {
            set_status(T("Installation failed", "安装失败"), T("Payload path is too long", "负载路径过长"));
            ret = -1;
            goto out;
        }
        if (entries[i].type == LEONOS_FS_TYPE_FILE) {
            struct leonos_stat st;
            if (stat(child, &st) == 0 && st.type == LEONOS_FS_TYPE_FILE) {
                copy_total_bytes += st.size;
            }
            ++*out_count;
            continue;
        }
        if (entries[i].type == LEONOS_FS_TYPE_DIR) {
            ret = count_files_recursive(child, out_count);
            if (ret < 0) {
                goto out;
            }
        }
    }
    ret = 0;
out:
    free(entries);
    return ret;
}

static int path_has_type(const char *path, uint32_t type)
{
    struct leonos_stat st;
    int ret = stat(path, &st);
    if (ret < 0) {
        return ret;
    }
    return st.type == type ? 0 : -20;
}

static int source_file_exists(const char *path)
{
    return path_has_type(path, LEONOS_FS_TYPE_FILE) == 0;
}

static int add_file_copy_work(const char *path)
{
    struct leonos_stat st;
    int ret = stat(path, &st);
    if (ret < 0) {
        return ret;
    }
    if (st.type != LEONOS_FS_TYPE_FILE) {
        return -20;
    }
    ++copy_total;
    copy_total_bytes += st.size;
    return 0;
}

static int remove_path_recursive(const char *path)
{
    struct leonos_stat st;
    struct leonos_dir_entry *entries;
    int ret = stat(path, &st);
    if (ret < 0) {
        return ret == -2 ? 0 : ret;
    }
    if (st.type == LEONOS_FS_TYPE_FILE) {
        return unlink(path);
    }
    if (st.type != LEONOS_FS_TYPE_DIR) {
        return -20;
    }
    entries = alloc_dir_entries();
    if (!entries) {
        return -12;
    }
    for (;;) {
        uint32_t count = 0;
        uint32_t removed = 0;
        ret = leonos_list_dir(path, entries, LEONOS_FS_MAX_ENTRIES, &count);
        if (ret < 0) {
            goto out;
        }
        if (count == 0) {
            break;
        }
        for (uint32_t i = 0; i < count; ++i) {
            char child[LEONOS_FS_PATH_LEN];
            if (name_is_dot(entries[i].name)) {
                continue;
            }
            if (path_join(child, sizeof(child), path, entries[i].name) < 0) {
                ret = -1;
                goto out;
            }
            ret = remove_path_recursive(child);
            if (ret < 0) {
                goto out;
            }
            removed = 1;
        }
        if (!removed) {
            break;
        }
    }
    ret = rmdir(path);
out:
    free(entries);
    return ret;
}

static int copy_dir_recursive(const char *src, const char *dst,
                              int window_id, struct leonos_ui_surface *ui);

static int replace_payload_dir_at(const char *source_root, const char *target_root,
                                  const char *source_name, const char *target_name,
                                  int window_id,
                                  struct leonos_ui_surface *ui)
{
    char src[LEONOS_FS_PATH_LEN];
    char dst[LEONOS_FS_PATH_LEN];
    int ret;
    if (path_join(src, sizeof(src), source_root, source_name) < 0 ||
        path_join(dst, sizeof(dst), target_root, target_name) < 0) {
        return -1;
    }
    show_copy_progress(window_id, ui, dst);
    ret = remove_path_recursive(dst);
    if (ret < 0) {
        return ret;
    }
    ret = mkdir(dst, 0);
    if (ret < 0 && ret != -17) {
        return ret;
    }
    return copy_dir_recursive(src, dst, window_id, ui);
}

static int copy_file_path(const char *src, const char *dst,
                          int window_id, struct leonos_ui_surface *ui)
{
    int in_fd = open(src, LEONOS_O_RDONLY, 0);
    int out_fd;
    long got;
    if (in_fd < 0) {
        printf("[installer.elf] open source %s ret=%d\n", src, in_fd);
        return in_fd;
    }
    out_fd = open(dst, LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    if (out_fd < 0) {
        close(in_fd);
        printf("[installer.elf] open target %s ret=%d\n", dst, out_fd);
        return out_fd;
    }
    if (!installer_tty_mode) {
        printf("[installer.elf] copying %s -> %s\n", src, dst);
    }
    show_copy_progress(window_id, ui, dst);
    while ((got = read(in_fd, copy_buf, sizeof(copy_buf))) > 0) {
        long written = 0;
        while (written < got) {
            long ret = write(out_fd, copy_buf + written, (uint32_t)(got - written));
            if (ret <= 0) {
                close(in_fd);
                close(out_fd);
                printf("[installer.elf] write target %s ret=%d\n", dst, ret < 0 ? (int)ret : -1);
                return ret < 0 ? (int)ret : -1;
            }
            written += ret;
            copy_done_bytes += (uint64_t)ret;
            show_copy_progress(window_id, ui, dst);
            sleep_ms(1);
        }
    }
    close(in_fd);
    close(out_fd);
    if (got < 0) {
        printf("[installer.elf] read source %s ret=%d\n", src, (int)got);
    }
    return got < 0 ? (int)got : 0;
}

static int copy_dir_recursive(const char *src, const char *dst,
                              int window_id, struct leonos_ui_surface *ui)
{
    struct leonos_dir_entry *entries = alloc_dir_entries();
    uint32_t count = 0;
    int ret;
    if (!entries) {
        return -12;
    }
    ret = leonos_list_dir(src, entries, LEONOS_FS_MAX_ENTRIES, &count);
    if (ret < 0) {
        goto out;
    }
    for (uint32_t i = 0; i < count; ++i) {
        char src_child[LEONOS_FS_PATH_LEN];
        char dst_child[LEONOS_FS_PATH_LEN];
        if (name_is_dot(entries[i].name)) {
            continue;
        }
        if (path_join(src_child, sizeof(src_child), src, entries[i].name) < 0 ||
            path_join(dst_child, sizeof(dst_child), dst, entries[i].name) < 0) {
            set_status(T("Installation failed", "安装失败"), T("Copy path is too long", "复制路径过长"));
            ret = -1;
            goto out;
        }
        if (entries[i].type == LEONOS_FS_TYPE_DIR) {
            ret = mkdir(dst_child, 0);
            if (ret == -17) {
                struct leonos_stat dst_st;
                ret = stat(dst_child, &dst_st);
                if (ret == 0 && dst_st.type == LEONOS_FS_TYPE_DIR) {
                    ret = 0;
                } else if (ret == 0) {
                    ret = -20;
                }
            }
            if (ret < 0) {
                printf("[installer.elf] mkdir %s ret=%d\n", dst_child, ret);
                goto out;
            }
            ret = copy_dir_recursive(src_child, dst_child, window_id, ui);
            if (ret < 0) {
                goto out;
            }
        } else if (entries[i].type == LEONOS_FS_TYPE_FILE) {
            ret = copy_file_path(src_child, dst_child, window_id, ui);
            if (ret < 0) {
                printf("[installer.elf] copy %s -> %s ret=%d\n", src_child, dst_child, ret);
                goto out;
            }
            ++copy_done;
            show_copy_progress(window_id, ui, dst_child);
        }
    }
    ret = 0;
out:
    free(entries);
    return ret;
}

static int merge_dir_recursive(const char *src, const char *dst,
                               int window_id, struct leonos_ui_surface *ui)
{
    struct leonos_dir_entry *entries = alloc_dir_entries();
    uint32_t count = 0;
    int ret;
    if (!entries) {
        return -12;
    }
    ret = mkdir(dst, 0);
    if (ret < 0 && ret != -17) {
        goto out;
    }
    ret = leonos_list_dir(src, entries, LEONOS_FS_MAX_ENTRIES, &count);
    if (ret < 0) {
        goto out;
    }
    for (uint32_t i = 0; i < count; ++i) {
        char src_child[LEONOS_FS_PATH_LEN];
        char dst_child[LEONOS_FS_PATH_LEN];
        if (name_is_dot(entries[i].name)) {
            continue;
        }
        if (path_join(src_child, sizeof(src_child), src, entries[i].name) < 0 ||
            path_join(dst_child, sizeof(dst_child), dst, entries[i].name) < 0) {
            set_status(T("Installation failed", "安装失败"), T("Copy path is too long", "复制路径过长"));
            ret = -1;
            goto out;
        }
        if (entries[i].type == LEONOS_FS_TYPE_DIR) {
            ret = merge_dir_recursive(src_child, dst_child, window_id, ui);
            if (ret < 0) {
                goto out;
            }
        } else if (entries[i].type == LEONOS_FS_TYPE_FILE) {
            ret = copy_file_path(src_child, dst_child, window_id, ui);
            if (ret < 0) {
                printf("[installer.elf] copy %s -> %s ret=%d\n", src_child, dst_child, ret);
                goto out;
            }
            ++copy_done;
            show_copy_progress(window_id, ui, dst_child);
        }
    }
    ret = 0;
out:
    free(entries);
    return ret;
}

/* Return whether any source file differs from the corresponding target file.
 * Extra files already present on the target are intentionally ignored: update
 * mode is additive for user data and only refreshes files shipped by LeonOS. */
static int package_has_changes(const char *src, const char *dst)
{
    struct leonos_dir_entry *entries = alloc_dir_entries();
    uint32_t count = 0;
    int ret;
    if (!entries) {
        return -12;
    }
    ret = leonos_list_dir(src, entries, LEONOS_FS_MAX_ENTRIES, &count);
    if (ret < 0) {
        goto out;
    }
    for (uint32_t i = 0; i < count; ++i) {
        char src_child[LEONOS_FS_PATH_LEN];
        char dst_child[LEONOS_FS_PATH_LEN];
        if (name_is_dot(entries[i].name)) {
            continue;
        }
        if (path_join(src_child, sizeof(src_child), src, entries[i].name) < 0 ||
            path_join(dst_child, sizeof(dst_child), dst, entries[i].name) < 0) {
            ret = -1;
            goto out;
        }
        if (entries[i].type == LEONOS_FS_TYPE_DIR) {
            struct leonos_stat dst_stat;
            ret = stat(dst_child, &dst_stat);
            if (ret < 0 || dst_stat.type != LEONOS_FS_TYPE_DIR) {
                ret = 1;
                goto out;
            }
            ret = package_has_changes(src_child, dst_child);
            if (ret != 0) {
                goto out;
            }
        } else if (entries[i].type == LEONOS_FS_TYPE_FILE) {
            uint8_t missing;
            uint8_t different;
            ret = installer_files_equal(src_child, dst_child, &missing, &different);
            if (ret < 0) {
                goto out;
            }
            if (missing || different) {
                ret = 1;
                goto out;
            }
        }
    }
    ret = 0;
out:
    free(entries);
    return ret;
}

static int count_changed_files_recursive(const char *src, const char *dst)
{
    struct leonos_dir_entry *entries = alloc_dir_entries();
    uint32_t count = 0;
    int ret;
    if (!entries) {
        return -12;
    }
    ret = leonos_list_dir(src, entries, LEONOS_FS_MAX_ENTRIES, &count);
    if (ret < 0) {
        goto out;
    }
    for (uint32_t i = 0; i < count; ++i) {
        char src_child[LEONOS_FS_PATH_LEN];
        char dst_child[LEONOS_FS_PATH_LEN];
        if (name_is_dot(entries[i].name)) {
            continue;
        }
        if (path_join(src_child, sizeof(src_child), src, entries[i].name) < 0 ||
            path_join(dst_child, sizeof(dst_child), dst, entries[i].name) < 0) {
            set_status(T("Installation failed", "安装失败"), T("Copy path is too long", "复制路径过长"));
            ret = -1;
            goto out;
        }
        if (entries[i].type == LEONOS_FS_TYPE_DIR) {
            ret = count_changed_files_recursive(src_child, dst_child);
        } else if (entries[i].type == LEONOS_FS_TYPE_FILE) {
            uint8_t missing;
            uint8_t different;
            ret = installer_files_equal(src_child, dst_child, &missing, &different);
            if (ret >= 0 && (missing || different)) {
                ret = add_file_copy_work(src_child);
            }
        } else {
            continue;
        }
        if (ret < 0) {
            goto out;
        }
    }
    ret = 0;
out:
    free(entries);
    return ret;
}

static int copy_changed_dir_recursive(const char *src, const char *dst,
                                      int window_id, struct leonos_ui_surface *ui)
{
    struct leonos_dir_entry *entries = alloc_dir_entries();
    uint32_t count = 0;
    int ret;
    if (!entries) {
        return -12;
    }
    ret = mkdir(dst, 0);
    if (ret < 0 && ret != -17) {
        goto out;
    }
    ret = leonos_list_dir(src, entries, LEONOS_FS_MAX_ENTRIES, &count);
    if (ret < 0) {
        goto out;
    }
    for (uint32_t i = 0; i < count; ++i) {
        char src_child[LEONOS_FS_PATH_LEN];
        char dst_child[LEONOS_FS_PATH_LEN];
        if (name_is_dot(entries[i].name)) {
            continue;
        }
        if (path_join(src_child, sizeof(src_child), src, entries[i].name) < 0 ||
            path_join(dst_child, sizeof(dst_child), dst, entries[i].name) < 0) {
            set_status(T("Installation failed", "安装失败"), T("Copy path is too long", "复制路径过长"));
            ret = -1;
            goto out;
        }
        if (entries[i].type == LEONOS_FS_TYPE_DIR) {
            struct leonos_stat dst_st;
            int dst_ret = stat(dst_child, &dst_st);
            if (dst_ret == 0 && dst_st.type != LEONOS_FS_TYPE_DIR) {
                ret = remove_path_recursive(dst_child);
                if (ret < 0) {
                    goto out;
                }
            } else if (dst_ret < 0 && dst_ret != -2) {
                ret = dst_ret;
                goto out;
            }
            ret = copy_changed_dir_recursive(src_child, dst_child, window_id, ui);
        } else if (entries[i].type == LEONOS_FS_TYPE_FILE) {
            uint8_t missing;
            uint8_t different;
            struct leonos_stat dst_st;
            int dst_ret = stat(dst_child, &dst_st);
            if (dst_ret == 0 && dst_st.type != LEONOS_FS_TYPE_FILE) {
                ret = remove_path_recursive(dst_child);
                if (ret < 0) {
                    goto out;
                }
            } else if (dst_ret < 0 && dst_ret != -2) {
                ret = dst_ret;
                goto out;
            }
            ret = installer_files_equal(src_child, dst_child, &missing, &different);
            if (ret >= 0 && (missing || different)) {
                ret = copy_file_path(src_child, dst_child, window_id, ui);
                if (ret >= 0) {
                    ++copy_done;
                    show_copy_progress(window_id, ui, dst_child);
                }
            }
        } else {
            continue;
        }
        if (ret < 0) {
            goto out;
        }
    }
    ret = 0;
out:
    free(entries);
    return ret;
}

static int copy_payload_ordered(int window_id, struct leonos_ui_surface *ui)
{
    int ret = copy_dir_recursive(INSTALL_ROOT_PAYLOAD, INSTALL_ROOT_MOUNT, window_id, ui);
    if (ret < 0) return ret;
    ret = copy_dir_recursive(INSTALL_ESP_PAYLOAD, INSTALL_ESP_MOUNT, window_id, ui);
    if (ret < 0) return ret;
    ret = mkdir("/target/system/state", 0);
    return ret < 0 && ret != -17 ? ret : 0;
}

static int replace_system_payload(int window_id, struct leonos_ui_surface *ui)
{
    static const char *const dirs[] = {
        "system/apps",
        "system/certs",
        "system/fonts",
        "system/lib",
        "system/resources",
    };
    static const char *const root_files[] = {
        "system/kerneldebug.sys",
        "system/osmlayer.manifest",
    };
    static const char *const esp_files[] = {
        "system/kernel.sys",
        "system/middlelayer.sys",
    };
    for (uint32_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
        int ret = replace_payload_dir_at(INSTALL_ROOT_PAYLOAD, INSTALL_ROOT_MOUNT,
                                         dirs[i], dirs[i], window_id, ui);
        if (ret < 0) {
            return ret;
        }
    }
    for (uint32_t i = 0; i < sizeof(root_files) / sizeof(root_files[0]); ++i) {
        char src[LEONOS_FS_PATH_LEN];
        char dst[LEONOS_FS_PATH_LEN];
        int ret;
        if (path_join(src, sizeof(src), INSTALL_ROOT_PAYLOAD, root_files[i]) < 0 ||
            path_join(dst, sizeof(dst), INSTALL_ROOT_MOUNT, root_files[i]) < 0) {
            return -1;
        }
        ret = copy_file_path(src, dst, window_id, ui);
        if (ret < 0) {
            return ret;
        }
        ++copy_done;
    }
    for (uint32_t i = 0; i < sizeof(esp_files) / sizeof(esp_files[0]); ++i) {
        char src[LEONOS_FS_PATH_LEN];
        char dst[LEONOS_FS_PATH_LEN];
        int ret;
        if (path_join(src, sizeof(src), INSTALL_ESP_PAYLOAD, esp_files[i]) < 0 ||
            path_join(dst, sizeof(dst), INSTALL_ESP_MOUNT, esp_files[i]) < 0) {
            return -1;
        }
        ret = copy_file_path(src, dst, window_id, ui);
        if (ret < 0) {
            return ret;
        }
        ++copy_done;
    }
    return 0;
}

static int check_update_target_required(void)
{
    static const char *const required_root_dirs[] = {
        "/target/docs",
        "/target/system",
        "/target/system/apps",
        "/target/system/config",
        "/target/system/state",
        "/target/programs",
        "/target/drivers",
    };
    static const char *const required_esp_dirs[] = {
        "/target/boot",
        "/target/boot/EFI",
        "/target/boot/system",
    };
    static const char *const required_root_files[] = {
        "/target/system/apps/desktop/desktop.elf",
    };
    static const char *const required_esp_files[] = {
        "/target/boot/loader.elf",
        "/target/boot/system/kernel.sys",
        "/target/boot/system/middlelayer.sys",
    };
    for (uint32_t i = 0; i < sizeof(required_root_dirs) / sizeof(required_root_dirs[0]); ++i) {
        int ret = path_has_type(required_root_dirs[i], LEONOS_FS_TYPE_DIR);
        if (ret < 0) {
            set_status(T("Existing LeonOS 4 was not detected", "未检测到现有 LeonOS 4"),
                       required_root_dirs[i]);
            return ret;
        }
    }
    for (uint32_t i = 0; i < sizeof(required_esp_dirs) / sizeof(required_esp_dirs[0]); ++i) {
        int ret = path_has_type(required_esp_dirs[i], LEONOS_FS_TYPE_DIR);
        if (ret < 0) {
            set_status(T("Existing LeonOS 4 boot partition was not detected", "未检测到现有 LeonOS 4 启动分区"),
                       required_esp_dirs[i]);
            return ret;
        }
    }
    for (uint32_t i = 0; i < sizeof(required_root_files) / sizeof(required_root_files[0]); ++i) {
        int ret = path_has_type(required_root_files[i], LEONOS_FS_TYPE_FILE);
        if (ret < 0) {
            set_status(T("Existing LeonOS 4 was not detected", "未检测到现有 LeonOS 4"),
                       required_root_files[i]);
            return ret;
        }
    }
    for (uint32_t i = 0; i < sizeof(required_esp_files) / sizeof(required_esp_files[0]); ++i) {
        int ret = path_has_type(required_esp_files[i], LEONOS_FS_TYPE_FILE);
        if (ret < 0) {
            set_status(T("Existing LeonOS 4 boot partition was not detected", "未检测到现有 LeonOS 4 启动分区"),
                       required_esp_files[i]);
            return ret;
        }
    }
    return 0;
}

static int check_update_payload_required(void)
{
    static const char *const required_dirs[] = {
        "/install/root/docs",
        "/install/root/system",
        "/install/root/drivers",
        "/install/root/system/apps",
        "/install/root/system/config",
        "/install/root/system/lib",
        "/install/root/programs",
        "/install/esp/grub",
        "/install/esp/system",
        "/install/esp/EFI",
    };
    static const char *const required_files[] = {
        "/install/root/system/kerneldebug.sys",
        "/install/root/system/lib/ld-leonos.elf",
        "/install/root/system/lib/libleonos.so.1",
    };
    for (uint32_t i = 0; i < sizeof(required_dirs) / sizeof(required_dirs[0]); ++i) {
        int ret = path_has_type(required_dirs[i], LEONOS_FS_TYPE_DIR);
        if (ret < 0) {
            set_status(T("Installation media is incomplete", "安装介质不完整"),
                       required_dirs[i]);
            return ret;
        }
    }
    for (uint32_t i = 0; i < sizeof(required_files) / sizeof(required_files[0]); ++i) {
        int ret = path_has_type(required_files[i], LEONOS_FS_TYPE_FILE);
        if (ret < 0) {
            set_status(T("Installation media is incomplete", "安装介质不完整"),
                       required_files[i]);
            return ret;
        }
    }
    return 0;
}

static int add_update_app_entry(const char *name,
                                const char *src_elf,
                                const char *dst_elf,
                                const char *src_icon,
                                const char *dst_icon,
                                uint8_t missing,
                                uint8_t elf_diff,
                                uint8_t icon_diff)
{
    struct update_app_entry *entry;
    if (update_app_count >= UPDATE_APP_MAX) {
        return -28;
    }
    entry = &update_apps[update_app_count++];
    copy_text(entry->name, sizeof(entry->name), name);
    copy_text(entry->src_elf, sizeof(entry->src_elf), src_elf);
    copy_text(entry->dst_elf, sizeof(entry->dst_elf), dst_elf);
    copy_text(entry->src_icon, sizeof(entry->src_icon), src_icon);
    copy_text(entry->dst_icon, sizeof(entry->dst_icon), dst_icon);
    entry->selected = 1;
    entry->missing = missing;
    entry->elf_diff = elf_diff;
    entry->icon_diff = icon_diff;
    return 0;
}

static int scan_update_apps(void)
{
    static const char *const source_root = INSTALL_ROOT_PAYLOAD "/programs";
    static const char *const target_root = "/target/programs";
    struct leonos_dir_entry entries[LEONOS_FS_MAX_ENTRIES];
    uint32_t count = 0;
    int ret;
    reset_update_app_list();
    ret = leonos_list_dir(source_root, entries,
                          LEONOS_FS_MAX_ENTRIES, &count);
    if (ret < 0) {
        return ret;
    }
    for (uint32_t i = 0; i < count; ++i) {
        char src_elf[LEONOS_FS_PATH_LEN];
        char dst_elf[LEONOS_FS_PATH_LEN];
        char src_icon[LEONOS_FS_PATH_LEN];
        char dst_icon[LEONOS_FS_PATH_LEN];
        char src_package[LEONOS_FS_PATH_LEN];
        char dst_package[LEONOS_FS_PATH_LEN];
        char elf_name[LEONOS_FS_PATH_LEN];
        uint32_t name_len = 0;
        uint8_t missing = 0;
        uint8_t elf_diff = 1;
        uint8_t icon_missing = 0;
        uint8_t icon_diff = 0;
        uint8_t package_diff = 0;
        if (entries[i].type != LEONOS_FS_TYPE_DIR) {
            continue;
        }
        if (path_join(src_package, sizeof(src_package), source_root, entries[i].name) < 0 ||
            path_join(dst_package, sizeof(dst_package), target_root, entries[i].name) < 0) {
            return -1;
        }
        copy_text(elf_name, sizeof(elf_name), entries[i].name);
        while (elf_name[name_len]) {
            ++name_len;
        }
        append_text(elf_name, &name_len, sizeof(elf_name), ".elf");
        if (path_join(src_elf, sizeof(src_elf), src_package, elf_name) < 0 ||
            path_join(dst_elf, sizeof(dst_elf), dst_package, elf_name) < 0 ||
            !source_file_exists(src_elf)) {
            continue;
        }
        copy_replace_extension(src_icon, sizeof(src_icon), src_elf, ".bmp");
        copy_replace_extension(dst_icon, sizeof(dst_icon), dst_elf, ".bmp");
        ret = installer_files_equal(src_elf, dst_elf, &missing, &elf_diff);
        if (ret < 0) {
            return ret;
        }
        if (source_file_exists(src_icon)) {
            ret = installer_files_equal(src_icon, dst_icon, &icon_missing, &icon_diff);
            if (ret < 0) {
                return ret;
            }
            if (icon_missing) {
                icon_diff = 1;
            }
        } else {
            src_icon[0] = 0;
            dst_icon[0] = 0;
        }
        if (missing || elf_diff || icon_diff) {
            package_diff = 1;
        } else {
            ret = package_has_changes(src_package, dst_package);
            if (ret < 0) {
                return ret;
            }
            package_diff = ret ? 1 : 0;
        }
        if (missing || elf_diff || icon_diff || package_diff) {
            ret = add_update_app_entry(entries[i].name, src_elf, dst_elf,
                                       src_icon, dst_icon, missing,
                                       elf_diff, icon_diff);
            if (ret < 0) {
                return ret;
            }
            copy_text(update_apps[update_app_count - 1].src_package,
                      sizeof(update_apps[update_app_count - 1].src_package),
                      src_package);
            copy_text(update_apps[update_app_count - 1].dst_package,
                      sizeof(update_apps[update_app_count - 1].dst_package),
                      dst_package);
            update_apps[update_app_count - 1].package_diff = package_diff;
        }
    }
    leonos_ui_listview_state_set_count(&update_app_list, update_app_count);
    update_app_list.selected = update_app_count ? 0 : -1;
    if (update_app_count) {
        char line[128];
        uint32_t pos = 0;
        line[0] = 0;
        append_u64(line, &pos, sizeof(line), update_app_count);
        append_text(line, &pos, sizeof(line),
                    T(" program package update(s) found.", " 个程序包更新。"));
        set_status(line, T("All are selected by default.", "默认全部勾选。"));
    } else {
        set_status(T("No program package differences were found.", "未发现程序包差异。"),
                   T("Core files, bundled docs, and drivers can still be updated.",
                     "仍可更新核心文件、内置文档和驱动程序。"));
    }
    return 0;
}

static int count_selected_update_work(void)
{
    int ret;
    for (uint32_t i = 0; i < update_app_count; ++i) {
        if (!update_apps[i].selected) {
            continue;
        }
        ret = count_changed_files_recursive(update_apps[i].src_package,
                                            update_apps[i].dst_package);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

static int copy_selected_update_apps(int window_id, struct leonos_ui_surface *ui)
{
    int ret;
    ret = mkdir("/target/programs", 0);
    if (ret < 0 && ret != -17) {
        return ret;
    }
    for (uint32_t i = 0; i < update_app_count; ++i) {
        if (!update_apps[i].selected) {
            continue;
        }
        char package_dir[LEONOS_FS_PATH_LEN];
        if (path_join(package_dir, sizeof(package_dir), "/target/programs", update_apps[i].name) < 0) {
            return -1;
        }
        {
            struct leonos_stat package_stat;
            ret = stat(package_dir, &package_stat);
            if (ret == 0) {
                if (package_stat.type != LEONOS_FS_TYPE_DIR) {
                    ret = remove_path_recursive(package_dir);
                    if (ret < 0) {
                        return ret;
                    }
                }
            } else if (ret != -2) {
                return ret;
            }
            ret = mkdir(package_dir, 0);
            if (ret < 0 && ret != -17) {
                return ret;
            }
        }
        ret = copy_changed_dir_recursive(update_apps[i].src_package,
                                         update_apps[i].dst_package,
                                         window_id, ui);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

static int write_target_locale(void)
{
    const char *text = installer_lang == LEONOS_LANG_ZH ? "lang=zh\n" : "lang=en\n";
    int fd = open("/target/system/config/locale.conf",
                  LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    long wrote;
    if (fd < 0) {
        return fd;
    }
    wrote = write(fd, text, strlen(text));
    close(fd);
    if (wrote < 0) {
        return (int)wrote;
    }
    return wrote == (long)strlen(text) ? 0 : -5;
}

static int display_config_line_is_theme(const char *line, uint32_t len)
{
    return len >= 6 && line[0] == 't' && line[1] == 'h' && line[2] == 'e' &&
           line[3] == 'm' && line[4] == 'e' && line[5] == '=';
}

static int write_target_theme(void)
{
    char input[384];
    char output[512];
    const char *theme = installer_theme == LEONOS_UI_THEME_WIN95 ? "win95" : "metro";
    struct leonos_stat stat_info;
    uint32_t input_len = 0;
    uint32_t output_len = 0;
    uint32_t offset = 0;
    int ret = stat("/target/system/config/display.conf", &stat_info);
    if (ret == 0) {
        int fd;
        long got;
        if (stat_info.type != LEONOS_FS_TYPE_FILE || stat_info.size >= sizeof(input)) {
            return -27;
        }
        fd = open("/target/system/config/display.conf", LEONOS_O_RDONLY, 0);
        if (fd < 0) {
            return fd;
        }
        got = read(fd, input, (uint32_t)stat_info.size);
        close(fd);
        if (got < 0) {
            return (int)got;
        }
        input_len = (uint32_t)got;
    } else if (ret != -2) {
        return ret;
    }
    while (offset < input_len) {
        uint32_t line_start = offset;
        uint32_t line_end;
        while (offset < input_len && input[offset] != '\n') {
            ++offset;
        }
        line_end = offset;
        if (line_end > line_start && input[line_end - 1] == '\r') {
            --line_end;
        }
        if (offset < input_len) {
            ++offset;
        }
        if (display_config_line_is_theme(input + line_start, line_end - line_start)) {
            continue;
        }
        for (uint32_t index = line_start; index < line_end; ++index) {
            if (append_char(output, &output_len, sizeof(output), input[index]) < 0) {
                return -27;
            }
        }
        if (append_char(output, &output_len, sizeof(output), '\n') < 0) {
            return -27;
        }
    }
    if (append_text(output, &output_len, sizeof(output), "theme=") < 0 ||
        append_text(output, &output_len, sizeof(output), theme) < 0 ||
        append_char(output, &output_len, sizeof(output), '\n') < 0) {
        return -27;
    }
    {
        int fd = open("/target/system/config/display.conf",
                      LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
        long wrote;
        if (fd < 0) {
            return fd;
        }
        wrote = write(fd, output, output_len);
        close(fd);
        if (wrote < 0) {
            return (int)wrote;
        }
        return wrote == (long)output_len ? 0 : -5;
    }
}

static int write_target_preferences(void)
{
    int ret;
    if (install_mode == INSTALL_MODE_FRESH) {
        ret = write_target_locale();
        if (ret < 0) {
            return ret;
        }
    }
    if (install_mode == INSTALL_MODE_FRESH || installer_theme_explicit) {
        return write_target_theme();
    }
    return 0;
}

static void finish_install(int window_id, struct leonos_ui_surface *ui, int ret,
                           const char *prefix)
{
    install_running = 0;
    page = PAGE_FINISH;
    if (ret < 0) {
        printf("[installer.elf] %s ret=%d\n", prefix ? prefix : "install failed", ret);
        install_success = 0;
        set_error_status(prefix, ret);
        progress_value = 0;
    } else {
        printf("[installer.elf] %s completed successfully\n",
               install_mode == INSTALL_MODE_UPDATE ? "update" : "installation");
        install_success = 1;
        progress_value = 100;
        set_status(install_mode == INSTALL_MODE_UPDATE
                       ? T("Update completed successfully", "更新成功完成")
                       : T("Installation completed successfully", "安装成功完成"),
                   T("Press Restart to boot from the installed disk.", "点击重启从已安装硬盘启动。"));
    }
    if (installer_tty_mode) {
        if (ret < 0) {
            printf("\n%s (error %d)\n", prefix ? prefix : "Install failed", ret);
        } else {
            puts("\nInstallation completed successfully.");
            puts("Remove the installation media before rebooting.");
        }
    } else {
        present_installer(window_id, ui);
    }
}

static void perform_install(int window_id, struct leonos_ui_surface *ui);
static void perform_update(int window_id, struct leonos_ui_surface *ui);
static void prepare_update_target(int window_id, struct leonos_ui_surface *ui);

static void tty_print_update_packages(void)
{
    char line[160];
    if (!update_app_count) {
        puts("No program package differences were found.");
        return;
    }
    puts("Program packages selected for update:");
    for (uint32_t i = 0; i < update_app_count; ++i) {
        format_update_reason(line, sizeof(line), &update_apps[i]);
        printf("  [%u] %s %s\n", i, update_apps[i].name, line);
    }
}

static void tty_prepare_update(void)
{
    prepare_update_target(0, 0);
}

static void tty_perform_install(void)
{
    perform_install(0, 0);
}

static void tty_perform_update(void)
{
    perform_update(0, 0);
}

static void prepare_update_target(int window_id, struct leonos_ui_surface *ui)
{
    uint32_t disk_id;
    int ret;
    if (selected_disk < 0 || (uint32_t)selected_disk >= disk_count) {
        return;
    }
    disk_id = disks[selected_disk].id;
    page = PAGE_PROGRESS;
    progress_value = 0;
    reset_update_app_list();
    show_progress(window_id, ui, 5,
                  T("Mounting target filesystems", "正在挂载目标文件系统"),
                  T("Root: /target; ESP: /target/boot", "根分区：/target；ESP：/target/boot"));
    ret = leonos_install_mount_target(disk_id);
    if (ret < 0) {
        finish_install(window_id, ui, ret, T("Mount failed", "挂载失败"));
        return;
    }
    show_progress(window_id, ui, 18,
                  T("Checking existing LeonOS 4", "正在检测现有 LeonOS 4"),
                  T("Target: /target", "目标: /target"));
    ret = check_update_payload_required();
    if (ret < 0) {
        finish_install(window_id, ui, ret, T("Payload check failed", "负载检测失败"));
        return;
    }
    ret = check_update_target_required();
    if (ret < 0) {
        finish_install(window_id, ui, ret, T("Existing system check failed", "现有系统检测失败"));
        return;
    }
    show_progress(window_id, ui, 30,
                  T("Scanning program packages", "正在扫描程序包"),
                  T("Drivers will also be synchronized during the update.", "更新时也会同步驱动程序。"));
    ret = scan_update_apps();
    if (ret < 0) {
        finish_install(window_id, ui, ret, T("Program scan failed", "程序包扫描失败"));
        return;
    }
    page = PAGE_UPDATE_APPS;
    dirty = 1;
    present_installer(window_id, ui);
}

static void perform_install(int window_id, struct leonos_ui_surface *ui)
{
    uint32_t disk_id;
    int ret;
    if (selected_disk < 0 || (uint32_t)selected_disk >= disk_count) {
        return;
    }
    disk_id = disks[selected_disk].id;
    install_running = 1;
    install_success = 0;
    page = PAGE_PROGRESS;
    copy_total = 0;
    copy_done = 0;
    copy_total_bytes = 0;
    copy_done_bytes = 0;

    show_progress(window_id, ui, 2, "Preparing target disk", "");
    ret = leonos_install_format_target(disk_id);
    if (ret < 0) {
        finish_install(window_id, ui, ret, "Format failed");
        return;
    }

    show_progress(window_id, ui, 22, "Mounting target filesystems", "Root: /target (exFAT or ext2), ESP: /target/boot (FAT32)");
    ret = leonos_install_mount_target(disk_id);
    if (ret < 0) {
        finish_install(window_id, ui, ret, "Mount failed");
        return;
    }

    show_progress(window_id, ui, 30, "Scanning installation payload", "Root and boot partitions");
    ret = count_files_recursive(INSTALL_ROOT_PAYLOAD, &copy_total);
    if (ret >= 0) {
        ret = count_files_recursive(INSTALL_ESP_PAYLOAD, &copy_total);
    }
    if (ret < 0) {
        finish_install(window_id, ui, ret, T("Payload scan failed", "扫描安装负载失败"));
        return;
    }

    show_progress(window_id, ui, 35, T("Copying system files", "正在复制系统文件"),
                  T("Root: /target; boot partition: /target/boot", "根分区：/target；启动分区：/target/boot"));
    ret = copy_payload_ordered(window_id, ui);
    if (ret < 0) {
        finish_install(window_id, ui, ret, T("Copy failed", "复制失败"));
        return;
    }

    ret = write_target_preferences();
    if (ret < 0) {
        finish_install(window_id, ui, ret, T("Could not save installed preferences", "无法保存安装后偏好设置"));
        return;
    }

    show_progress(window_id, ui, 100, T("Installation completed successfully", "安装成功完成"), T("Target disk is ready.", "目标硬盘已准备就绪。"));
    finish_install(window_id, ui, 0, "");
}

static void perform_update(int window_id, struct leonos_ui_surface *ui)
{
    static const char *const boot_dirs[] = {
        "grub",
        "EFI",
    };
    static const char *const boot_files[] = {
        "loader.elf",
    };
    static const char *const root_system_dirs[] = {
        "system/apps",
        "system/certs",
        "system/fonts",
        "system/lib",
        "system/resources",
    };
    static const char *const root_system_files[] = {
        "system/kerneldebug.sys",
        "system/osmlayer.manifest",
    };
    static const char *const boot_system_files[] = {
        "system/kernel.sys",
        "system/middlelayer.sys",
    };
    uint32_t disk_id;
    int ret;
    if (selected_disk < 0 || (uint32_t)selected_disk >= disk_count) {
        return;
    }
    disk_id = disks[selected_disk].id;
    install_running = 1;
    install_success = 0;
    page = PAGE_PROGRESS;
    copy_total = 0;
    copy_done = 0;
    copy_total_bytes = 0;
    copy_done_bytes = 0;

    show_progress(window_id, ui, 2, T("Mounting target filesystems", "正在挂载目标文件系统"),
                  T("Root: /target; ESP: /target/boot", "根分区：/target；ESP：/target/boot"));
    ret = leonos_install_mount_target(disk_id);
    if (ret < 0) {
        finish_install(window_id, ui, ret, T("Mount failed", "挂载失败"));
        return;
    }

    show_progress(window_id, ui, 10, T("Checking existing LeonOS 4", "正在检测现有 LeonOS 4"),
                  T("Target: /target", "目标: /target"));
    ret = check_update_payload_required();
    if (ret < 0) {
        finish_install(window_id, ui, ret, T("Payload check failed", "负载检测失败"));
        return;
    }
    ret = check_update_target_required();
    if (ret < 0) {
        finish_install(window_id, ui, ret, T("Existing system check failed", "现有系统检测失败"));
        return;
    }

    show_progress(window_id, ui, 18, T("Scanning update payload", "正在扫描更新负载"),
                  T("Root and boot payloads", "根分区和启动分区负载"));
    for (uint32_t i = 0; i < sizeof(boot_dirs) / sizeof(boot_dirs[0]); ++i) {
        char src[LEONOS_FS_PATH_LEN];
        if (path_join(src, sizeof(src), INSTALL_ESP_PAYLOAD, boot_dirs[i]) < 0) {
            finish_install(window_id, ui, -1, T("Payload path is too long", "负载路径过长"));
            return;
        }
        ret = count_files_recursive(src, &copy_total);
        if (ret < 0) {
            finish_install(window_id, ui, ret, T("Payload scan failed", "扫描安装负载失败"));
            return;
        }
    }
    for (uint32_t i = 0; i < sizeof(boot_files) / sizeof(boot_files[0]); ++i) {
        char src[LEONOS_FS_PATH_LEN];
        if (path_join(src, sizeof(src), INSTALL_ESP_PAYLOAD, boot_files[i]) < 0 ||
            add_file_copy_work(src) < 0) {
            finish_install(window_id, ui, -1, T("Payload scan failed", "扫描安装负载失败"));
            return;
        }
    }
    for (uint32_t i = 0; i < sizeof(root_system_dirs) / sizeof(root_system_dirs[0]); ++i) {
        char src[LEONOS_FS_PATH_LEN];
        if (path_join(src, sizeof(src), INSTALL_ROOT_PAYLOAD, root_system_dirs[i]) < 0) {
            finish_install(window_id, ui, -1, T("Payload path is too long", "负载路径过长"));
            return;
        }
        ret = count_files_recursive(src, &copy_total);
        if (ret < 0) {
            finish_install(window_id, ui, ret, T("Payload scan failed", "扫描安装负载失败"));
            return;
        }
    }
    for (uint32_t i = 0; i < sizeof(root_system_files) / sizeof(root_system_files[0]); ++i) {
        char src[LEONOS_FS_PATH_LEN];
        if (path_join(src, sizeof(src), INSTALL_ROOT_PAYLOAD, root_system_files[i]) < 0 ||
            add_file_copy_work(src) < 0) {
            finish_install(window_id, ui, -1, T("Payload scan failed", "扫描安装负载失败"));
            return;
        }
    }
    for (uint32_t i = 0; i < sizeof(boot_system_files) / sizeof(boot_system_files[0]); ++i) {
        char src[LEONOS_FS_PATH_LEN];
        if (path_join(src, sizeof(src), INSTALL_ESP_PAYLOAD, boot_system_files[i]) < 0 ||
            add_file_copy_work(src) < 0) {
            finish_install(window_id, ui, -1, T("Payload scan failed", "扫描安装负载失败"));
            return;
        }
    }
    ret = count_files_recursive(INSTALL_ROOT_PAYLOAD "/docs", &copy_total);
    if (ret < 0) {
        finish_install(window_id, ui, ret, T("Payload scan failed", "扫描安装负载失败"));
        return;
    }
    ret = count_changed_files_recursive(INSTALL_ROOT_PAYLOAD "/drivers", "/target/drivers");
    if (ret < 0) {
        finish_install(window_id, ui, ret, T("Driver scan failed", "驱动程序扫描失败"));
        return;
    }
    ret = count_selected_update_work();
    if (ret < 0) {
        finish_install(window_id, ui, ret, T("Payload scan failed", "扫描安装负载失败"));
        return;
    }

    show_progress(window_id, ui, 35, T("Replacing core system files", "正在替换核心系统文件"),
                  T("boot partition and system apps", "启动分区和系统应用"));
    for (uint32_t i = 0; i < sizeof(boot_dirs) / sizeof(boot_dirs[0]); ++i) {
        ret = replace_payload_dir_at(INSTALL_ESP_PAYLOAD, INSTALL_ESP_MOUNT,
                                     boot_dirs[i], boot_dirs[i], window_id, ui);
        if (ret < 0) {
            finish_install(window_id, ui, ret, T("Core update failed", "核心系统更新失败"));
            return;
        }
    }
    for (uint32_t i = 0; i < sizeof(boot_files) / sizeof(boot_files[0]); ++i) {
        char src[LEONOS_FS_PATH_LEN];
        char dst[LEONOS_FS_PATH_LEN];
        if (path_join(src, sizeof(src), INSTALL_ESP_PAYLOAD, boot_files[i]) < 0 ||
            path_join(dst, sizeof(dst), INSTALL_ESP_MOUNT, boot_files[i]) < 0) {
            finish_install(window_id, ui, -1, T("Payload path is too long", "负载路径过长"));
            return;
        }
        ret = copy_file_path(src, dst, window_id, ui);
        if (ret < 0) {
            finish_install(window_id, ui, ret, T("Core update failed", "核心系统更新失败"));
            return;
        }
        ++copy_done;
    }
    ret = replace_system_payload(window_id, ui);
    if (ret < 0) {
        finish_install(window_id, ui, ret, T("Core update failed", "核心系统更新失败"));
        return;
    }

    show_progress(window_id, ui, copy_progress_percent(),
                  T("Updating help documents", "正在更新帮助文档"),
                  T("Destination: /target/docs", "目标: /target/docs"));
    ret = merge_dir_recursive(INSTALL_ROOT_PAYLOAD "/docs", "/target/docs", window_id, ui);
    if (ret < 0) {
        finish_install(window_id, ui, ret, T("Help document update failed", "帮助文档更新失败"));
        return;
    }

    show_progress(window_id, ui, copy_progress_percent(),
                  T("Updating hardware drivers", "正在更新硬件驱动程序"),
                  T("Destination: /target/drivers; extra files are kept.",
                    "目标：/target/drivers；多出的文件会保留。"));
    ret = copy_changed_dir_recursive(INSTALL_ROOT_PAYLOAD "/drivers", "/target/drivers",
                                     window_id, ui);
    if (ret < 0) {
        finish_install(window_id, ui, ret, T("Driver update failed", "驱动程序更新失败"));
        return;
    }

    show_progress(window_id, ui, copy_progress_percent(),
                  T("Updating selected programs", "正在更新已选程序"),
                  T("Destination: /target/programs", "目标: /target/programs"));
    ret = copy_selected_update_apps(window_id, ui);
    if (ret < 0) {
        finish_install(window_id, ui, ret, T("Program update failed", "程序更新失败"));
        return;
    }

    show_progress(window_id, ui, 100, T("Update completed successfully", "更新成功完成"),
                  T("Target disk is ready.", "目标硬盘已准备就绪。"));
    finish_install(window_id, ui, 0, "");
}

static void go_back(void)
{
    if (page == PAGE_THANKS) {
        page = PAGE_LANGUAGE;
    } else if (page == PAGE_THEME) {
        page = PAGE_THANKS;
    } else if (page == PAGE_WELCOME) {
        page = PAGE_THEME;
    } else if (page == PAGE_MODE) {
        page = PAGE_WELCOME;
    } else if (page == PAGE_DISK) {
        page = PAGE_MODE;
    } else if (page == PAGE_UPDATE_APPS) {
        page = PAGE_DISK;
    } else if (page == PAGE_CONFIRM) {
        page = install_mode == INSTALL_MODE_UPDATE ? PAGE_UPDATE_APPS : PAGE_DISK;
    } else if (page == PAGE_FINISH && !install_success) {
        page = PAGE_DISK;
    }
    dirty = 1;
}

static int go_primary(int window_id, struct leonos_ui_surface *ui)
{
    if (primary_disabled()) {
        return 0;
    }
    if (page == PAGE_LANGUAGE) {
        page = PAGE_THANKS;
        dirty = 1;
        return 0;
    }
    if (page == PAGE_THANKS) {
        page = PAGE_THEME;
        dirty = 1;
        return 0;
    }
    if (page == PAGE_THEME) {
        page = PAGE_WELCOME;
        dirty = 1;
        return 0;
    }
    if (page == PAGE_WELCOME) {
        page = PAGE_MODE;
        dirty = 1;
        return 0;
    }
    if (page == PAGE_MODE) {
        page = PAGE_DISK;
        refresh_disks();
        dirty = 1;
        return 0;
    }
    if (page == PAGE_DISK) {
        if (install_mode == INSTALL_MODE_UPDATE) {
            prepare_update_target(window_id, ui);
            return 0;
        }
        page = PAGE_CONFIRM;
        reset_confirm();
        dirty = 1;
        return 0;
    }
    if (page == PAGE_UPDATE_APPS) {
        page = PAGE_CONFIRM;
        reset_confirm();
        dirty = 1;
        return 0;
    }
    if (page == PAGE_CONFIRM) {
        if (install_mode == INSTALL_MODE_UPDATE) {
            perform_update(window_id, ui);
        } else {
            perform_install(window_id, ui);
        }
        return 0;
    }
    if (page == PAGE_FINISH && install_success) {
        leonos_system_reboot();
        return 0;
    }
    if (page == PAGE_FINISH) {
        return 1;
    }
    return 0;
}

static void handle_disk_click(int32_t x, int32_t y)
{
    struct installer_layout l = get_layout();
    if (hit_rect_i(x, y, (int32_t)l.disk_refresh_x, (int32_t)l.disk_refresh_y, 92, BUTTON_H)) {
        refresh_disks();
        return;
    }
    if (hit_rect_i(x, y, (int32_t)l.content_x, (int32_t)l.disk_list_y,
                   (int32_t)l.table_w, (int32_t)l.disk_list_h)) {
        int32_t row = (y - (int32_t)l.disk_list_y - 2) / 24;
        if (row >= 0 && (uint32_t)row < disk_count) {
            selected_disk = row;
            set_disk_select_status();
            dirty = 1;
        }
    }
}

static void handle_mode_click(int32_t x, int32_t y)
{
    struct installer_layout l = get_layout();
    uint32_t card_w = l.content_w > 620 ? 280 : l.content_w;
    if (hit_rect_i(x, y, (int32_t)l.content_x, (int32_t)l.content_y + 84,
                   (int32_t)card_w, BUTTON_H)) {
        install_mode = INSTALL_MODE_FRESH;
        reset_update_app_list();
        dirty = 1;
    } else if (hit_rect_i(x, y, (int32_t)l.content_x, (int32_t)l.content_y + 180,
                          (int32_t)card_w, BUTTON_H)) {
        install_mode = INSTALL_MODE_UPDATE;
        reset_update_app_list();
        dirty = 1;
    }
}

static void installer_apply_language_font(void)
{
    if (installer_lang == LEONOS_LANG_ZH) {
        /* SimSun supplies the CJK glyphs used throughout the Chinese UI. */
        (void)leonos_ui_set_font_fallback_path(0);
        (void)leonos_ui_set_font_path(INSTALLER_CJK_FONT);
    } else {
        /* The language page contains the native-language "中文" selector.
         * Use the complete face here so it renders deterministically before
         * any fallback glyph cache has been warmed. */
        (void)leonos_ui_set_font_fallback_path(0);
        (void)leonos_ui_set_font_path(INSTALLER_CJK_FONT);
    }
}

static void handle_language_click(int32_t x, int32_t y)
{
    struct installer_layout l = get_layout();
    uint8_t language = installer_lang;
    if (hit_rect_i(x, y, (int32_t)l.content_x, (int32_t)l.content_y + 88, 140, BUTTON_H)) {
        language = LEONOS_LANG_EN;
    } else if (hit_rect_i(x, y, (int32_t)l.content_x + 156, (int32_t)l.content_y + 88, 140, BUTTON_H)) {
        language = LEONOS_LANG_ZH;
    }
    if (language != installer_lang) {
        installer_lang = language;
        installer_apply_language_font();
        acknowledgements_scroll_y = 0;
        dirty = 1;
    }
}

static void handle_acknowledgements_click(int32_t x, int32_t y)
{
    struct installer_text_view view = get_acknowledgements_view();
    uint32_t total_h;
    acknowledgements_reflow(view.text_w);
    total_h = policy_total_height();
    if (leonos_ui_vscrollbar_handle_mouse(&acknowledgements_scroll_y,
                                           total_h > view.h ? total_h : view.h,
                                           view.h,
                                           view.x + view.w - POLICY_SCROLLBAR_W,
                                           view.y, POLICY_SCROLLBAR_W, view.h,
                                           x, y)) {
        dirty = 1;
    }
}

static void handle_acknowledgements_wheel(int32_t delta)
{
    struct installer_text_view view = get_acknowledgements_view();
    uint32_t total_h;
    uint32_t steps = delta < 0 ? (uint32_t)(-delta) : (uint32_t)delta;
    int32_t pixels;
    if (!steps) {
        steps = 1;
    }
    acknowledgements_reflow(view.text_w);
    total_h = policy_total_height();
    pixels = delta > 0 ? (int32_t)(steps * 36U) : -(int32_t)(steps * 36U);
    if (leonos_ui_vscrollbar_handle_wheel(&acknowledgements_scroll_y,
                                          total_h > view.h ? total_h : view.h,
                                          view.h, pixels)) {
        dirty = 1;
    }
}

static void handle_theme_click(int32_t x, int32_t y)
{
    struct installer_layout l = get_layout();
    uint32_t theme = installer_theme;
    uint8_t selected = 0;
    if (hit_rect_i(x, y, (int32_t)l.content_x, (int32_t)l.content_y + 88,
                   140, BUTTON_H)) {
        theme = LEONOS_UI_THEME_METRO;
        selected = 1;
    } else if (hit_rect_i(x, y, (int32_t)l.content_x + 156,
                          (int32_t)l.content_y + 88, 140, BUTTON_H)) {
        theme = LEONOS_UI_THEME_WIN95;
        selected = 1;
    }
    if (selected) {
        installer_theme = (uint8_t)theme;
        installer_theme_explicit = 1;
        (void)leonos_ui_theme_set(theme);
        dirty = 1;
    }
}

static void handle_update_apps_click(int32_t x, int32_t y)
{
    struct installer_layout l = get_layout();
    uint32_t header_y = l.content_y + 112;
    uint32_t list_y = header_y + 24;
    uint32_t list_h = l.content_h > 220 ? l.content_h - 178 : 120;
    uint32_t list_w = l.table_w > 22 ? l.table_w - 22 : l.table_w;
    sync_update_list_layout(list_h);
    if (leonos_ui_vscrollbar_handle_mouse(&update_app_list.scroll,
                                           update_app_count > update_app_list.visible_rows
                                               ? update_app_count : update_app_list.visible_rows,
                                           update_app_list.visible_rows,
                                           l.content_x + list_w, list_y, 18, list_h, x, y)) {
        dirty = 1;
        return;
    }
    if (hit_rect_i(x, y, (int32_t)l.content_x, (int32_t)list_y,
                   (int32_t)list_w, (int32_t)list_h)) {
        int32_t row = (y - (int32_t)list_y - 2) / (int32_t)UPDATE_APP_ROW_H;
        uint32_t index;
        if (row < 0) {
            return;
        }
        index = update_app_list.scroll + (uint32_t)row;
        if (index >= update_app_count) {
            return;
        }
        update_app_list.selected = (int32_t)index;
        update_apps[index].selected = update_apps[index].selected ? 0 : 1;
        dirty = 1;
    }
}

static void handle_update_apps_wheel(int32_t delta)
{
    if (leonos_ui_listview_state_handle_wheel(&update_app_list, delta)) {
        dirty = 1;
    }
}

static int handle_mouse(int window_id, struct leonos_ui_surface *ui,
                        const struct leonos_gui_app_event *event)
{
    struct installer_layout l = get_layout();
    if (!(event->buttons & 1u)) {
        return 0;
    }
    if (page == PAGE_CONFIRM &&
        leonos_ui_edit_state_handle_mouse(&confirm_edit, event->x, event->y,
                                          l.content_x, l.confirm_edit_y, 220, event->buttons)) {
        dirty = 1;
    }
    if (page == PAGE_DISK) {
        handle_disk_click(event->x, event->y);
    }
    if (page == PAGE_MODE) {
        handle_mode_click(event->x, event->y);
    }
    if (page == PAGE_UPDATE_APPS) {
        handle_update_apps_click(event->x, event->y);
    }
    if (page == PAGE_LANGUAGE) {
        handle_language_click(event->x, event->y);
    }
    if (page == PAGE_THANKS) {
        handle_acknowledgements_click(event->x, event->y);
    }
    if (page == PAGE_THEME) {
        handle_theme_click(event->x, event->y);
    }
    if (!install_running &&
        hit_rect_i(event->x, event->y, (int32_t)l.back_x, (int32_t)l.button_y, BUTTON_W, BUTTON_H)) {
        if (!(page == PAGE_LANGUAGE || page == PAGE_PROGRESS ||
              (page == PAGE_FINISH && install_success))) {
            go_back();
        }
    } else if (!install_running &&
               hit_rect_i(event->x, event->y, (int32_t)l.next_x, (int32_t)l.button_y, BUTTON_W, BUTTON_H)) {
        if (go_primary(window_id, ui)) {
            return 1;
        }
    } else if (!install_running &&
               hit_rect_i(event->x, event->y, (int32_t)l.cancel_x, (int32_t)l.button_y, BUTTON_W, BUTTON_H)) {
        if (!(page == PAGE_FINISH && install_success)) {
            return 1;
        }
    }
    return 0;
}

static int handle_key(int window_id, struct leonos_ui_surface *ui,
                      const struct leonos_gui_app_event *event)
{
    if (event->type == LEONOS_GUI_APP_EVENT_KEY_DOWN && event->pressed) {
        if (event->keycode == KEY_ESCAPE && page != PAGE_PROGRESS) {
            return 1;
        }
        if (page == PAGE_UPDATE_APPS) {
            uint32_t activated = 0;
            if (event->keycode == KEY_SPACE) {
                if (update_app_list.selected >= 0 &&
                    (uint32_t)update_app_list.selected < update_app_count) {
                    uint32_t i = (uint32_t)update_app_list.selected;
                    update_apps[i].selected = update_apps[i].selected ? 0 : 1;
                    dirty = 1;
                }
                return 0;
            }
            if (event->keycode != LEONOS_KEY_ENTER &&
                leonos_ui_listview_state_handle_key(&update_app_list,
                                                    event->keycode, &activated)) {
                dirty = 1;
                return 0;
            }
        }
        if (page == PAGE_DISK) {
            if (event->keycode == KEY_UP && selected_disk > 0) {
                --selected_disk;
                dirty = 1;
                return 0;
            }
            if (event->keycode == KEY_DOWN &&
                selected_disk >= 0 && (uint32_t)(selected_disk + 1) < disk_count) {
                ++selected_disk;
                dirty = 1;
                return 0;
            }
        }
        if (event->keycode == LEONOS_KEY_ENTER && page != PAGE_PROGRESS) {
            return go_primary(window_id, ui);
        }
    }
    if (page == PAGE_CONFIRM &&
        (event->type == LEONOS_GUI_APP_EVENT_KEY_DOWN ||
         event->type == LEONOS_GUI_APP_EVENT_KEY_UP)) {
        if (leonos_ui_edit_state_handle_key(&confirm_edit, event->keycode,
                                            event->pressed)) {
            dirty = 1;
        }
    }
    return 0;
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    struct installer_tty_context tty_context;
    int window_id;

    if (leonos_pty_self() > 0) {
        installer_tty_mode = 1;
        installer_lang = (uint8_t)leonos_i18n_language();
        tty_context.disks = disks;
        tty_context.disk_count = &disk_count;
        tty_context.selected_disk = &selected_disk;
        tty_context.install_mode = &install_mode;
        tty_context.install_success = &install_success;
        tty_context.page = &page;
        tty_context.update_apps_page = PAGE_UPDATE_APPS;
        tty_context.refresh_disks = refresh_disks;
        tty_context.format_disk_line = format_disk_line;
        tty_context.print_update_packages = tty_print_update_packages;
        tty_context.prepare_update = tty_prepare_update;
        tty_context.perform_install = tty_perform_install;
        tty_context.perform_update = tty_perform_update;
        return installer_tty_main(&tty_context);
    }
    puts("[installer.elf] starting installer wizard");
    update_surface_size_from_framebuffer();
    window_id = leonos_gui_create_app_window_ex("LeonOS Setup", "Install LeonOS 4",
                                                surface_w, surface_h,
                                                LEONOS_GUI_WINDOW_FULLSCREEN);
    if (window_id <= 0) {
        printf("[installer.elf] create window failed=%d\n", window_id);
        return 1;
    }

    leonos_ui_bind(&ui, pixels, surface_w, surface_h, INSTALLER_MAX_W);
    installer_lang = (uint8_t)leonos_i18n_language();
    installer_theme = (uint8_t)leonos_ui_theme();
    installer_apply_language_font();
    leonos_ui_listview_state_init(&update_app_list, 1, UPDATE_APP_ROW_H);
    refresh_disks();
    page = PAGE_LANGUAGE;
    present_installer(window_id, &ui);

    for (;;) {
        event.window_id = (uint32_t)window_id;
        while (leonos_gui_wait_app_event(&event, LEONOS_GUI_IDLE_WAIT_MS) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                leonos_gui_destroy_app_window((uint32_t)window_id);
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_RESIZE) {
                update_surface_size(event.width, event.height);
                leonos_ui_bind(&ui, pixels, surface_w, surface_h, INSTALLER_MAX_W);
                dirty = 1;
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON &&
                handle_mouse(window_id, &ui, &event)) {
                leonos_gui_destroy_app_window((uint32_t)window_id);
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_WHEEL) {
                if (page == PAGE_UPDATE_APPS) {
                    handle_update_apps_wheel(event.dy);
                } else if (page == PAGE_THANKS) {
                    handle_acknowledgements_wheel(event.dy);
                }
            }
            if ((event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN ||
                 event.type == LEONOS_GUI_APP_EVENT_KEY_UP) &&
                handle_key(window_id, &ui, &event)) {
                leonos_gui_destroy_app_window((uint32_t)window_id);
                return 0;
            }
        }
        if (dirty) {
            present_installer(window_id, &ui);
        }
        sleep_ms(10);
    }
}
