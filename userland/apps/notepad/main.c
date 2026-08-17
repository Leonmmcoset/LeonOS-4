#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/png.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/text.h>
#include <leonos/ui.h>

#define NOTEPAD_W 720
#define NOTEPAD_H 460
#define NOTEPAD_MIN_W 320
#define NOTEPAD_MIN_H 240
#define NOTEPAD_MAX_W LEONOS_GUI_MAX_WINDOW_WIDTH
#define NOTEPAD_MAX_H LEONOS_GUI_MAX_WINDOW_HEIGHT
#define NOTEPAD_TEXT_CAP 32768
#define NOTEPAD_ENCODED_CAP (NOTEPAD_TEXT_CAP * 2 + 4)
#define STATUS_CAP 128
#define PATH_CAP LEONOS_FS_PATH_LEN
#define VIEW_X 10
#define STATUS_H 28
#define MENU_BAR_H 28
#define VIEW_Y (MENU_BAR_H + 10)
#define MENU_ITEM_H (LEONOS_FONT_H + 8)
#define UNTITLED_NAME "Untitled"
#define NOTEPAD_WINDOW_TITLE_CAP 48
#define T(en, zh) leonos_i18n((en), (zh))

enum {
    NOTEPAD_MENU_NONE = 0,
    NOTEPAD_MENU_FILE = 1,
    NOTEPAD_MENU_EDIT = 2,
    NOTEPAD_MENU_VIEW = 3,
};

enum notepad_document_kind {
    NOTEPAD_DOCUMENT_TEXT = 0,
    NOTEPAD_DOCUMENT_PNG,
};

static uint32_t pixels[NOTEPAD_MAX_W * NOTEPAD_MAX_H];
static char file_path[PATH_CAP] = UNTITLED_NAME;
static char status_text[STATUS_CAP];
static char text_data[NOTEPAD_TEXT_CAP];
static char loaded_text[NOTEPAD_TEXT_CAP];
static char encoded_text[NOTEPAD_ENCODED_CAP];
static uint8_t truncated;
static uint8_t menu_open;
static uint8_t document_dirty;
static uint8_t document_kind;
static uint32_t document_encoding = LEONOS_TEXT_ENCODING_UTF8;
static uint32_t decode_replacements;
static uint32_t saved_hash;
static uint32_t *png_pixels;
static uint32_t png_width;
static uint32_t png_height;
static struct leonos_ui_text_area_state document;
static uint32_t view_w = NOTEPAD_W;
static uint32_t view_h = NOTEPAD_H;

static int confirm_dirty_action(const char *message);

static const char *encoding_name(uint32_t encoding)
{
    switch (encoding) {
    case LEONOS_TEXT_ENCODING_UTF8_BOM:
        return "UTF-8 BOM";
    case LEONOS_TEXT_ENCODING_UTF16LE:
        return "UTF-16 LE";
    case LEONOS_TEXT_ENCODING_UTF16BE:
        return "UTF-16 BE";
    case LEONOS_TEXT_ENCODING_GBK:
        return "GBK";
    case LEONOS_TEXT_ENCODING_GB2312:
        return "GB2312";
    case LEONOS_TEXT_ENCODING_UTF8:
    default:
        return "UTF-8";
    }
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

static void append_char(char *buf, uint32_t *pos, uint32_t cap, char ch)
{
    if (!buf || !pos || *pos + 1 >= cap) {
        return;
    }
    buf[*pos] = ch;
    ++(*pos);
    buf[*pos] = 0;
}

static void append_text(char *buf, uint32_t *pos, uint32_t cap, const char *text)
{
    while (text && *text) {
        append_char(buf, pos, cap, *text++);
    }
}

static void append_u32(char *buf, uint32_t *pos, uint32_t cap, uint32_t value)
{
    char tmp[16];
    uint32_t n = 0;
    if (value == 0) {
        append_char(buf, pos, cap, '0');
        return;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (n) {
        append_char(buf, pos, cap, tmp[--n]);
    }
}

static int text_equals(const char *a, const char *b)
{
    uint32_t i = 0;
    for (;;) {
        char ca = a ? a[i] : 0;
        char cb = b ? b[i] : 0;
        if (ca != cb) {
            return 0;
        }
        if (ca == 0) {
            return 1;
        }
        ++i;
    }
}

static int text_ends_with_ignore_case(const char *text, const char *suffix)
{
    uint32_t text_len = 0;
    uint32_t suffix_len = 0;
    uint32_t i;
    while (text && text[text_len]) {
        ++text_len;
    }
    while (suffix && suffix[suffix_len]) {
        ++suffix_len;
    }
    if (!suffix_len || suffix_len > text_len) {
        return 0;
    }
    for (i = 0; i < suffix_len; ++i) {
        char a = text[text_len - suffix_len + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b) {
            return 0;
        }
    }
    return 1;
}

static int path_is_png(const char *path)
{
    return text_ends_with_ignore_case(path, ".png");
}

static int is_untitled_path(const char *path)
{
    return !path || !path[0] || text_equals(path, UNTITLED_NAME);
}

static const char *path_basename(const char *path)
{
    const char *base = path;
    if (is_untitled_path(path)) {
        return T("Untitled", "未命名");
    }
    for (uint32_t index = 0; path && path[index]; ++index) {
        if (path[index] == '/') {
            base = path + index + 1U;
        }
    }
    return base && base[0] ? base : T("Untitled", "未命名");
}

static int hit_rect_i(int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rw, int32_t rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static uint32_t text_view_w(void)
{
    return view_w > VIEW_X + 28 ? view_w - VIEW_X - 28 : 80;
}

static uint32_t text_view_h(void)
{
    return view_h > VIEW_Y + STATUS_H + 10 ? view_h - VIEW_Y - STATUS_H - 10 : LEONOS_FONT_H;
}

static uint32_t scrollbar_x(void)
{
    return view_w > 26 ? view_w - 26 : VIEW_X + text_view_w() + 2;
}

static uint32_t visible_rows(void)
{
    uint32_t rows = text_view_h() / LEONOS_FONT_H;
    return rows ? rows : 1;
}

static uint32_t document_hash(void)
{
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < document.length; ++i) {
        hash ^= (uint8_t)text_data[i];
        hash *= 16777619u;
    }
    hash ^= document.length;
    hash *= 16777619u;
    return hash;
}

static void rebuild_status(void)
{
    uint32_t pos = 0;
    if (document_kind == NOTEPAD_DOCUMENT_PNG) {
        status_text[0] = 0;
        append_text(status_text, &pos, sizeof(status_text), T("PNG preview ", "PNG 预览 "));
        append_u32(status_text, &pos, sizeof(status_text), png_width);
        append_char(status_text, &pos, sizeof(status_text), 'x');
        append_u32(status_text, &pos, sizeof(status_text), png_height);
        append_text(status_text, &pos, sizeof(status_text), T("  Read-only", "  只读"));
        return;
    }
    leonos_ui_text_area_state_sync(&document, text_view_w());
    status_text[0] = 0;
    append_text(status_text, &pos, sizeof(status_text), T("Lines ", "行数 "));
    append_u32(status_text, &pos, sizeof(status_text), document.line_count);
    append_text(status_text, &pos, sizeof(status_text), T("  Bytes ", "  字节 "));
    append_u32(status_text, &pos, sizeof(status_text), document.length);
    append_text(status_text, &pos, sizeof(status_text), T("  Encoding ", "  编码 "));
    append_text(status_text, &pos, sizeof(status_text), encoding_name(document_encoding));
    if (document_dirty) {
        append_text(status_text, &pos, sizeof(status_text), T("  Modified", "  已修改"));
    }
    if (truncated) {
        append_text(status_text, &pos, sizeof(status_text), T("  Truncated", "  已截断"));
    }
    if (decode_replacements) {
        append_text(status_text, &pos, sizeof(status_text), T("  Invalid bytes replaced", "  无效字节已替换"));
    }
    if (document.length == 0) {
        append_text(status_text, &pos, sizeof(status_text), T("  Empty", "  空文件"));
    }
}

static void clamp_scroll(void)
{
    uint32_t rows = visible_rows();
    leonos_ui_text_area_state_sync(&document, text_view_w());
    if (rows == 0) {
        document.scroll_line = 0;
        return;
    }
    if (document.line_count <= rows) {
        document.scroll_line = 0;
        return;
    }
    if (document.scroll_line + rows > document.line_count) {
        document.scroll_line = document.line_count - rows;
    }
}

static void set_error_status(const char *prefix, int value)
{
    uint32_t pos = 0;
    status_text[0] = 0;
    append_text(status_text, &pos, sizeof(status_text), prefix);
    if (value < 0) {
        append_char(status_text, &pos, sizeof(status_text), '-');
        value = -value;
    }
    append_u32(status_text, &pos, sizeof(status_text), (uint32_t)value);
}

static void mark_document_clean(void)
{
    saved_hash = document_hash();
    document_dirty = 0;
    rebuild_status();
}

static void refresh_document_dirty(void)
{
    document_dirty = document_hash() != saved_hash;
    rebuild_status();
}

static void clear_document_contents(void)
{
    document.length = 0;
    document.cursor = 0;
    document.scroll_line = 0;
    text_data[0] = 0;
    truncated = 0;
    leonos_ui_text_area_state_sync(&document, text_view_w());
}

static void clear_png_preview(void)
{
    leonos_png_free(png_pixels);
    png_pixels = 0;
    png_width = 0;
    png_height = 0;
}

static void begin_new_document(void)
{
    clear_png_preview();
    document_kind = NOTEPAD_DOCUMENT_TEXT;
    document.readonly = 0;
    document.focused = 1;
    copy_text(file_path, sizeof(file_path), UNTITLED_NAME);
    clear_document_contents();
    document_encoding = LEONOS_TEXT_ENCODING_UTF8;
    decode_replacements = 0;
    mark_document_clean();
}

static int load_document(const char *path)
{
    struct leonos_stat st;
    int fd;
    int ret;
    int decode_ret;
    uint32_t loaded_length = 0;
    uint32_t decoded_length = 0;
    uint32_t detected_encoding = LEONOS_TEXT_ENCODING_UTF8;
    uint32_t replacements = 0;
    uint8_t loaded_truncated = 0;
    if (!path || !path[0]) {
        begin_new_document();
        return 0;
    }
    ret = stat(path, &st);
    if (ret < 0) {
        set_error_status(T("stat failed ", "状态读取失败 "), ret);
        return ret;
    }
    if (st.type != LEONOS_FS_TYPE_FILE) {
        copy_text(status_text, sizeof(status_text), T("Selected path is not a file", "所选路径不是文件"));
        return -1;
    }
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        set_error_status(T("open failed ", "打开失败 "), fd);
        saved_hash = document_hash();
        document_dirty = 0;
        return fd;
    }
    for (;;) {
        long got;
        uint32_t free_bytes = sizeof(loaded_text) - loaded_length - 1;
        if (free_bytes == 0) {
            loaded_truncated = 1;
            break;
        }
        got = read(fd, loaded_text + loaded_length, free_bytes);
        if (got < 0) {
            close(fd);
            set_error_status(T("read failed ", "读取失败 "), (int)got);
            return (int)got;
        }
        if (got == 0) {
            break;
        }
        loaded_length += (uint32_t)got;
        loaded_text[loaded_length] = 0;
        if ((uint32_t)got == free_bytes) {
            loaded_truncated = 1;
            break;
        }
    }
    close(fd);
    clear_png_preview();
    document_kind = NOTEPAD_DOCUMENT_TEXT;
    document.readonly = 0;
    document.focused = 1;
    clear_document_contents();
    if (leonos_text_detect_encoding(loaded_text, loaded_length, &detected_encoding) < 0) {
        copy_text(status_text, sizeof(status_text), T("Unsupported text encoding", "不支持的文本编码"));
        return -1;
    }
    decode_ret = leonos_text_decode(loaded_text, loaded_length, detected_encoding,
                                    text_data, sizeof(text_data) - 1U,
                                    &decoded_length, &replacements);
    if (decode_ret < 0 && decode_ret != LEONOS_TEXT_ENCODING_NO_SPACE) {
        copy_text(status_text, sizeof(status_text), T("Could not decode text file", "无法解码文本文件"));
        return decode_ret;
    }
    text_data[decoded_length] = 0;
    document.length = decoded_length;
    document_encoding = detected_encoding;
    decode_replacements = replacements;
    truncated = loaded_truncated || decode_ret == LEONOS_TEXT_ENCODING_NO_SPACE;
    copy_text(file_path, sizeof(file_path), path);
    document.cursor = 0;
    document.scroll_line = 0;
    leonos_ui_text_area_state_sync(&document, text_view_w());
    clamp_scroll();
    mark_document_clean();
    printf("[notepad.elf] open path=%s bytes=%d encoding=%s lines=%d truncated=%d\n",
           file_path, (int)document.length, encoding_name(document_encoding),
           (int)document.line_count, (int)truncated);
    return 0;
}

static int load_png_document(const char *path)
{
    uint32_t *decoded = 0;
    uint32_t decoded_width = 0;
    uint32_t decoded_height = 0;
    int ret = leonos_png_decode_file(path, &decoded, &decoded_width, &decoded_height);
    if (ret < 0) {
        copy_text(status_text, sizeof(status_text),
                  T("Could not decode PNG (maximum 1024x1024).",
                    "无法解码 PNG（最大 1024x1024）。"));
        return ret;
    }
    clear_png_preview();
    clear_document_contents();
    png_pixels = decoded;
    png_width = decoded_width;
    png_height = decoded_height;
    document_kind = NOTEPAD_DOCUMENT_PNG;
    document.readonly = 1;
    document.focused = 0;
    copy_text(file_path, sizeof(file_path), path);
    mark_document_clean();
    printf("[notepad.elf] open png=%s size=%dx%d\n", file_path,
           (int)png_width, (int)png_height);
    return 0;
}

static int save_document_to_path(const char *path, uint32_t encoding)
{
    int fd;
    int encode_ret;
    uint32_t flags;
    uint32_t encoded_length = 0;
    uint32_t replacements = 0;
    long wrote;
    if (!path || !path[0]) {
        return 0;
    }
    if (document_kind == NOTEPAD_DOCUMENT_PNG) {
        copy_text(status_text, sizeof(status_text),
                  T("PNG preview cannot be saved as text", "PNG 预览不能保存为文本"));
        return 0;
    }
    encode_ret = leonos_text_encode(text_data, document.length, encoding,
                                    encoded_text, sizeof(encoded_text),
                                    &encoded_length, &replacements);
    if (encode_ret < 0) {
        copy_text(status_text, sizeof(status_text),
                  T("Text is too large for selected encoding", "文本过大，无法使用所选编码保存"));
        return 0;
    }
    if (replacements && !leonos_ui_show_confirm_dialog(
            T("Notepad", "记事本"),
            T("Some characters are not available in this encoding. Save them as '?'?",
              "所选编码无法表示部分字符。将其保存为 '?' 吗？"), 0)) {
        return 0;
    }
    flags = LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC;
    fd = open(path, flags, 0);
    if (fd < 0) {
        set_error_status(T("save open failed ", "保存打开失败 "), fd);
        return 0;
    }
    wrote = 0;
    if (encoded_length) {
        wrote = write(fd, encoded_text, encoded_length);
        if (wrote < 0) {
            close(fd);
            set_error_status(T("save write failed ", "保存写入失败 "), (int)wrote);
            return 0;
        }
        if ((uint32_t)wrote != encoded_length) {
            close(fd);
            copy_text(status_text, sizeof(status_text), T("save write incomplete", "保存写入不完整"));
            return 0;
        }
    }
    close(fd);
    copy_text(file_path, sizeof(file_path), path);
    document_encoding = encoding;
    decode_replacements = 0;
    mark_document_clean();
    printf("[notepad.elf] saved path=%s bytes=%d encoding=%s\n", file_path,
           (int)encoded_length, encoding_name(document_encoding));
    return 1;
}

static int save_document_as(void)
{
    char path[PATH_CAP];
    uint32_t encoding = document_encoding;
    static const struct leonos_ui_dropdown_item encoding_items[] = {
        {"UTF-8", LEONOS_TEXT_ENCODING_UTF8, 0},
        {"UTF-8 with BOM", LEONOS_TEXT_ENCODING_UTF8_BOM, 0},
        {"UTF-16 LE", LEONOS_TEXT_ENCODING_UTF16LE, 0},
        {"UTF-16 BE", LEONOS_TEXT_ENCODING_UTF16BE, 0},
        {"GBK", LEONOS_TEXT_ENCODING_GBK, 0},
        {"GB2312", LEONOS_TEXT_ENCODING_GB2312, 0},
    };
    const struct leonos_ui_file_dialog_input inputs[] = {
        {
            .type = LEONOS_UI_FILE_DIALOG_INPUT_DROPDOWN,
            .id = 1,
            .label = T("Encoding", "编码"),
            .value = &encoding,
            .items = encoding_items,
            .item_count = sizeof(encoding_items) / sizeof(encoding_items[0]),
        },
    };
    const struct leonos_ui_file_dialog_options options = {
        .inputs = inputs,
        .input_count = sizeof(inputs) / sizeof(inputs[0]),
    };
    if (document_kind == NOTEPAD_DOCUMENT_PNG) {
        copy_text(status_text, sizeof(status_text),
                  T("PNG preview cannot be saved as text", "PNG 预览不能保存为文本"));
        return 0;
    }
    if (is_untitled_path(file_path)) {
        path[0] = 0;
    } else {
        copy_text(path, sizeof(path), file_path);
    }
    if (leonos_ui_show_save_dialog_with_options(
            T("Save As", "另存为"), path, sizeof(path),
            T("Text files (*.txt)", "文本文件 (*.txt)"), ".txt", &options) <= 0) {
        return 0;
    }
    if (!path[0]) {
        return 0;
    }
    return save_document_to_path(path, encoding);
}

static int open_document_via_dialog(void)
{
    char path[PATH_CAP];
    path[0] = 0;
    if (leonos_ui_show_open_dialog(T("Open", "打开"), path, sizeof(path),
                                    T("Text and PNG (*.txt; *.png)", "文本和 PNG (*.txt; *.png)"),
                                    ".txt;.png") <= 0) {
        return 0;
    }
    if (!path[0]) {
        return 0;
    }
    if (!confirm_dirty_action(T("Save changes before opening another file?", "打开其他文件前保存更改？"))) {
        return 0;
    }
    return (path_is_png(path) ? load_png_document(path) : load_document(path)) == 0;
}

static int save_document(void)
{
    if (document_kind == NOTEPAD_DOCUMENT_PNG) {
        copy_text(status_text, sizeof(status_text),
                  T("PNG preview cannot be saved as text", "PNG 预览不能保存为文本"));
        return 0;
    }
    if (is_untitled_path(file_path)) {
        return save_document_as();
    }
    return save_document_to_path(file_path, document_encoding);
}

static int confirm_dirty_action(const char *message)
{
    if (!document_dirty) {
        return 1;
    }
    if (!leonos_ui_show_confirm_dialog(T("Notepad", "记事本"), message, 1)) {
        return 1;
    }
    return save_document() && !document_dirty;
}

static void draw_png_preview(struct leonos_ui_surface *ui, uint32_t x0,
                             uint32_t y0, uint32_t w0, uint32_t h0)
{
    uint32_t content_x = x0 + 3U;
    uint32_t content_y = y0 + 3U;
    uint32_t content_w = w0 > 6U ? w0 - 6U : 1U;
    uint32_t content_h = h0 > 6U ? h0 - 6U : 1U;
    uint32_t draw_w = content_w;
    uint32_t draw_h;
    uint32_t dst_x;
    uint32_t dst_y;

    leonos_ui_inset(ui, x0, y0, w0, h0, LEONOS_UI_WHITE);
    if (!png_pixels || !png_width || !png_height) {
        leonos_ui_text_clipped(ui, x0 + 12U, y0 + 12U,
                                w0 > 24U ? w0 - 24U : w0,
                                T("PNG preview is unavailable.", "PNG 预览不可用。"),
                                LEONOS_UI_DARK, LEONOS_UI_WHITE);
        return;
    }
    draw_h = (uint32_t)(((uint64_t)draw_w * png_height) / png_width);
    if (draw_h > content_h) {
        draw_h = content_h;
        draw_w = (uint32_t)(((uint64_t)draw_h * png_width) / png_height);
    }
    if (!draw_w) {
        draw_w = 1U;
    }
    if (!draw_h) {
        draw_h = 1U;
    }
    dst_x = content_x + (content_w > draw_w ? (content_w - draw_w) / 2U : 0U);
    dst_y = content_y + (content_h > draw_h ? (content_h - draw_h) / 2U : 0U);
    for (uint32_t y = 0; y < draw_h; ++y) {
        uint32_t source_y = (uint32_t)(((uint64_t)y * png_height) / draw_h);
        for (uint32_t x = 0; x < draw_w; ++x) {
            uint32_t source_x = (uint32_t)(((uint64_t)x * png_width) / draw_w);
            leonos_ui_pixel(ui, dst_x + x, dst_y + y,
                            png_pixels[source_y * png_width + source_x]);
        }
    }
}

static void draw_notepad(struct leonos_ui_surface *ui)
{
    uint32_t rows = visible_rows();
    uint32_t edit_w = text_view_w();
    uint32_t edit_h = text_view_h();
    uint32_t scroll_x = scrollbar_x();
    leonos_ui_text_area_state_sync(&document, edit_w);
    leonos_ui_rect(ui, 0, 0, view_w, view_h, LEONOS_UI_WHITE);
    leonos_ui_menubar(ui, 0, 0, view_w);
    leonos_ui_menubar_item(ui, 8, 0, 54, T("File", "文件"), menu_open == NOTEPAD_MENU_FILE);
    leonos_ui_menubar_item(ui, 64, 0, 54, T("Edit", "编辑"), menu_open == NOTEPAD_MENU_EDIT);
    leonos_ui_menubar_item(ui, 120, 0, 54, T("View", "查看"), menu_open == NOTEPAD_MENU_VIEW);
    if (document_kind == NOTEPAD_DOCUMENT_PNG) {
        draw_png_preview(ui, VIEW_X, VIEW_Y, edit_w + 18U, edit_h);
    } else {
        leonos_ui_text_area_state_draw(ui, VIEW_X, VIEW_Y, edit_w, edit_h, &document, 0);
        leonos_ui_vscrollbar(ui, scroll_x, VIEW_Y, 18, edit_h, document.scroll_line,
                             document.line_count > 0 ? document.line_count : 1, rows,
                             document.line_count <= rows ? LEONOS_UI_SCROLLBAR_DISABLED : 0);
    }
    leonos_ui_statusbar(ui, view_h - STATUS_H, STATUS_H, status_text);

    if (menu_open == NOTEPAD_MENU_FILE) {
        leonos_ui_menu(ui, 8, MENU_BAR_H, 154, 112);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 8, 116, T("Choose file", "选择文件"), 0);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 34, 116, T("New", "新建"), 0);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 60, 116, T("Save", "保存"), 0);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 86, 116, T("Save As", "另存为"), 0);
    } else if (menu_open == NOTEPAD_MENU_EDIT) {
        leonos_ui_menu(ui, 64, MENU_BAR_H, 154, 86);
        leonos_ui_menu_item(ui, 98, MENU_BAR_H + 8, 116, T("Clear", "清空"), 0);
        leonos_ui_menu_item(ui, 98, MENU_BAR_H + 34, 116, T("Home", "开头"), 0);
        leonos_ui_menu_item(ui, 98, MENU_BAR_H + 60, 116, T("End", "末尾"), 0);
    } else if (menu_open == NOTEPAD_MENU_VIEW) {
        leonos_ui_menu(ui, 120, MENU_BAR_H, 154, 60);
        leonos_ui_menu_item(ui, 154, MENU_BAR_H + 8, 116, T("Top", "顶部"), 0);
        leonos_ui_menu_item(ui, 154, MENU_BAR_H + 34, 116, T("About", "关于"), 0);
    }
}

static void present_notepad(uint32_t window_id, struct leonos_ui_surface *ui)
{
    char title[NOTEPAD_WINDOW_TITLE_CAP];
    uint32_t title_pos = 0;
    title[0] = 0;
    append_text(title, &title_pos, sizeof(title), T("Notepad - ", "记事本 - "));
    append_text(title, &title_pos, sizeof(title), path_basename(file_path));
    if (document_dirty) {
        append_text(title, &title_pos, sizeof(title), " *");
    }
    (void)leonos_gui_set_window_title(window_id, title);
    leonos_ui_bind(ui, pixels, view_w, view_h, NOTEPAD_MAX_W);
    draw_notepad(ui);
    leonos_gui_present_window(window_id, view_w, view_h, NOTEPAD_MAX_W, pixels);
}

static int handle_menu_click(int32_t x, int32_t y)
{
    if (y >= 0 && y < (int32_t)MENU_BAR_H) {
        if (hit_rect_i(x, y, 8, 0, 54, (int32_t)MENU_BAR_H)) {
            menu_open = menu_open == NOTEPAD_MENU_FILE ? NOTEPAD_MENU_NONE : NOTEPAD_MENU_FILE;
            return 1;
        }
        if (hit_rect_i(x, y, 64, 0, 54, (int32_t)MENU_BAR_H)) {
            menu_open = menu_open == NOTEPAD_MENU_EDIT ? NOTEPAD_MENU_NONE : NOTEPAD_MENU_EDIT;
            return 1;
        }
        if (hit_rect_i(x, y, 120, 0, 54, (int32_t)MENU_BAR_H)) {
            menu_open = menu_open == NOTEPAD_MENU_VIEW ? NOTEPAD_MENU_NONE : NOTEPAD_MENU_VIEW;
            return 1;
        }
        menu_open = NOTEPAD_MENU_NONE;
        return 1;
    }
    if (menu_open == NOTEPAD_MENU_FILE) {
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 8, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = NOTEPAD_MENU_NONE;
            if (open_document_via_dialog()) {
                rebuild_status();
            }
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 34, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = NOTEPAD_MENU_NONE;
            if (confirm_dirty_action(T("Save changes before creating a new file?", "新建文件前保存更改？"))) {
                begin_new_document();
            }
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 60, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = NOTEPAD_MENU_NONE;
            save_document();
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 86, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = NOTEPAD_MENU_NONE;
            save_document_as();
            return 1;
        }
        menu_open = NOTEPAD_MENU_NONE;
        return 1;
    }
    if (menu_open == NOTEPAD_MENU_EDIT) {
        if (hit_rect_i(x, y, 98, (int32_t)MENU_BAR_H + 8, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = NOTEPAD_MENU_NONE;
            if (confirm_dirty_action(T("Save changes before clearing this file?", "清空文件前保存更改？"))) {
                clear_document_contents();
                refresh_document_dirty();
            }
            return 1;
        }
        if (hit_rect_i(x, y, 98, (int32_t)MENU_BAR_H + 34, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = NOTEPAD_MENU_NONE;
            document.cursor = 0;
            document.scroll_line = 0;
            rebuild_status();
            return 1;
        }
        if (hit_rect_i(x, y, 98, (int32_t)MENU_BAR_H + 60, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = NOTEPAD_MENU_NONE;
            document.cursor = document.length;
            clamp_scroll();
            rebuild_status();
            return 1;
        }
        menu_open = NOTEPAD_MENU_NONE;
        return 1;
    }
    if (menu_open == NOTEPAD_MENU_VIEW) {
        if (hit_rect_i(x, y, 154, (int32_t)MENU_BAR_H + 8, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = NOTEPAD_MENU_NONE;
            document.scroll_line = 0;
            return 1;
        }
        if (hit_rect_i(x, y, 154, (int32_t)MENU_BAR_H + 34, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = NOTEPAD_MENU_NONE;
            leonos_ui_show_message_box(T("Notepad", "记事本"), T("Open files from File Manager or Run.", "从文件管理器或运行打开文件。"), T("OK", "确定"));
            return 1;
        }
        menu_open = NOTEPAD_MENU_NONE;
        return 1;
    }
    return 0;
}

int main(int argc, char **argv, char **envp)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    (void)envp;

    puts("[notepad.elf] notepad starting");
    copy_text(status_text, sizeof(status_text),
              T("Choose a text file or PNG image", "请选择文本文件或 PNG 图片"));
    window_id = leonos_gui_create_app_window_ex(T("Notepad", "记事本"), T("LeonOS text viewer", "LeonOS 文本查看器"),
                                                NOTEPAD_W, NOTEPAD_H, 0);
    if (window_id <= 0) {
        printf("[notepad.elf] create window failed=%d\n", window_id);
        return 1;
    }

    leonos_ui_bind(&ui, pixels, view_w, view_h, NOTEPAD_MAX_W);
    leonos_ui_text_area_state_init(&document, text_data, sizeof(text_data));
    document.focused = 1;
    document.readonly = 0;
    begin_new_document();
    if (argc > 1 && argv && argv[1] && argv[1][0]) {
        load_document(argv[1]);
    }
    present_notepad((uint32_t)window_id, &ui);

    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_wait_app_event(&event, LEONOS_GUI_IDLE_WAIT_MS) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                if (confirm_dirty_action(T("Save changes before closing?", "关闭前保存更改？"))) {
                    clear_png_preview();
                    return 0;
                }
                present_notepad((uint32_t)window_id, &ui);
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON) {
                if (event.buttons & 1u) {
                    if (handle_menu_click(event.x, event.y)) {
                        present_notepad((uint32_t)window_id, &ui);
                        continue;
                    }
                    menu_open = NOTEPAD_MENU_NONE;
                    if (document_kind == NOTEPAD_DOCUMENT_PNG) {
                        continue;
                    }
                    {
                        uint32_t before = document.scroll_line;
                        uint32_t edit_w = text_view_w();
                        uint32_t edit_h = text_view_h();
                        uint32_t scroll_x = scrollbar_x();
                        if (event.x >= (int32_t)scroll_x && event.y >= VIEW_Y &&
                            event.y < (int32_t)(VIEW_Y + edit_h)) {
                            leonos_ui_vscrollbar_handle_mouse(&document.scroll_line,
                                                              document.line_count, visible_rows(),
                                                              scroll_x, VIEW_Y, 18, edit_h,
                                                              event.x, event.y);
                        } else {
                            leonos_ui_text_area_state_handle_mouse(&document, event.x, event.y,
                                                                   VIEW_X, VIEW_Y, edit_w, edit_h,
                                                                   event.buttons);
                        }
                        if (before != document.scroll_line || document.focused) {
                            present_notepad((uint32_t)window_id, &ui);
                        }
                    }
                }
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_MOVE) {
                if (document_kind == NOTEPAD_DOCUMENT_PNG) {
                    continue;
                }
                if (event.buttons & 1u) {
                    uint32_t before = document.scroll_line;
                    leonos_ui_text_area_state_handle_mouse(&document, event.x, event.y,
                                                           VIEW_X, VIEW_Y, text_view_w(), text_view_h(),
                                                           event.buttons);
                    if (before != document.scroll_line || document.focused) {
                        present_notepad((uint32_t)window_id, &ui);
                    }
                } else if (document.selecting) {
                    leonos_ui_text_area_state_handle_mouse(&document, event.x, event.y,
                                                           VIEW_X, VIEW_Y, text_view_w(), text_view_h(), 0);
                }
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_WHEEL) {
                if (document_kind == NOTEPAD_DOCUMENT_PNG) {
                    continue;
                }
                if (leonos_ui_vscrollbar_handle_wheel(&document.scroll_line,
                                                      document.line_count, visible_rows(),
                                                      event.dy)) {
                    present_notepad((uint32_t)window_id, &ui);
                }
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN || event.type == LEONOS_GUI_APP_EVENT_KEY_UP) {
                if (document_kind == NOTEPAD_DOCUMENT_PNG) {
                    continue;
                }
                uint32_t before_hash = document_hash();
                menu_open = NOTEPAD_MENU_NONE;
                if (leonos_ui_text_area_state_handle_key(&document, event.keycode, event.pressed,
                                                         text_view_w(), text_view_h())) {
                    clamp_scroll();
                    if (before_hash != document_hash()) {
                        refresh_document_dirty();
                    } else {
                        rebuild_status();
                    }
                    present_notepad((uint32_t)window_id, &ui);
                }
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_RESIZE || event.type == LEONOS_GUI_APP_EVENT_FOCUS) {
                if (event.width) {
                    view_w = event.width > NOTEPAD_MAX_W ? NOTEPAD_MAX_W : event.width;
                    if (view_w < NOTEPAD_MIN_W) {
                        view_w = NOTEPAD_MIN_W;
                    }
                }
                if (event.height) {
                    view_h = event.height > NOTEPAD_MAX_H ? NOTEPAD_MAX_H : event.height;
                    if (view_h < NOTEPAD_MIN_H) {
                        view_h = NOTEPAD_MIN_H;
                    }
                }
                clamp_scroll();
                present_notepad((uint32_t)window_id, &ui);
            }
        } else {
            sleep_ms(10);
        }
    }
}
