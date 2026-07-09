#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>
#include <stdint.h>
#include <stdlib.h>

#define IMAGEVIEW_W 760U
#define IMAGEVIEW_H 520U
#define IMAGEVIEW_MIN_W 420U
#define IMAGEVIEW_MIN_H 300U
#define IMAGEVIEW_MAX_W 1180U
#define IMAGEVIEW_MAX_H 820U
#define IMAGEVIEW_TOOLBAR_Y 4U
#define IMAGEVIEW_TOOLBAR_H 36U
#define IMAGEVIEW_STATUS_H 28U
#define IMAGEVIEW_DETAIL_H 22U
#define IMAGEVIEW_MAX_PIXELS (1024U * 1024U)
#define IMAGEVIEW_ROWS_MAX LEONOS_FS_MAX_ENTRIES
#define T(en, zh) leonos_i18n((en), (zh))

enum zoom_mode {
    ZOOM_FIT = 0,
    ZOOM_1X = 1,
    ZOOM_2X = 2,
};

static uint32_t pixels[IMAGEVIEW_MAX_W * IMAGEVIEW_MAX_H];
static uint32_t *image_pixels;
static uint32_t image_w;
static uint32_t image_h;
static uint32_t view_w = IMAGEVIEW_W;
static uint32_t view_h = IMAGEVIEW_H;
static uint8_t zoom_mode = ZOOM_FIT;
static char current_path[LEONOS_FS_PATH_LEN];
static char current_dir[LEONOS_FS_PATH_LEN];
static char status_text[160] = "Open a BMP file from File Manager.";
static char detail_text[192] = "";
static char sibling_names[IMAGEVIEW_ROWS_MAX][LEONOS_FS_NAME_LEN];
static uint32_t sibling_count;
static uint32_t sibling_index;

static uint32_t read_le16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t read_le32s(const uint8_t *p)
{
    return (int32_t)read_le32(p);
}

static uint32_t text_len(const char *text)
{
    uint32_t n = 0;
    while (text && text[n]) {
        ++n;
    }
    return n;
}

static char ascii_tolower(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

static int text_eq_ignore_case(const char *a, const char *b)
{
    uint32_t i = 0;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i] && ascii_tolower(a[i]) == ascii_tolower(b[i])) {
        ++i;
    }
    return a[i] == 0 && b[i] == 0;
}

static int ends_with_ignore_case(const char *text, const char *suffix)
{
    uint32_t text_n = text_len(text);
    uint32_t suffix_n = text_len(suffix);
    if (!text || !suffix || suffix_n > text_n) {
        return 0;
    }
    return text_eq_ignore_case(text + text_n - suffix_n, suffix);
}

static void copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[i] && i + 1U < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void append_char(char *dst, uint32_t *pos, uint32_t cap, char ch)
{
    if (dst && pos && *pos + 1U < cap) {
        dst[*pos] = ch;
        ++(*pos);
        dst[*pos] = 0;
    }
}

static void append_text(char *dst, uint32_t *pos, uint32_t cap,
                        const char *src)
{
    while (src && *src) {
        append_char(dst, pos, cap, *src++);
    }
}

static void append_u32(char *dst, uint32_t *pos, uint32_t cap, uint32_t value)
{
    char tmp[12];
    uint32_t n = 0;
    if (value == 0) {
        append_char(dst, pos, cap, '0');
        return;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (n) {
        append_char(dst, pos, cap, tmp[--n]);
    }
}

static const char *path_basename(const char *path)
{
    const char *base = path;
    if (!path) {
        return "";
    }
    for (uint32_t i = 0; path[i]; ++i) {
        if (path[i] == '/') {
            base = path + i + 1U;
        }
    }
    return base;
}

static void path_parent(char *dst, uint32_t cap, const char *path)
{
    uint32_t len;
    copy_text(dst, cap, path);
    len = text_len(dst);
    while (len > 3U && dst[len - 1U] != '/') {
        dst[--len] = 0;
    }
    if (len > 3U) {
        dst[len - 1U] = 0;
    } else if (len >= 3U) {
        dst[3] = 0;
    }
}

static void build_child_path(char *dst, uint32_t cap, const char *dir,
                             const char *name)
{
    uint32_t pos = 0;
    dst[0] = 0;
    append_text(dst, &pos, cap, dir);
    if (dir && dir[0] && dir[text_len(dir) - 1U] != '/') {
        append_char(dst, &pos, cap, '/');
    }
    append_text(dst, &pos, cap, name);
}

static void free_image(void)
{
    if (image_pixels) {
        free(image_pixels);
        image_pixels = 0;
    }
    image_w = 0;
    image_h = 0;
}

static int read_file_all(const char *path, uint8_t **out_data, uint32_t *out_len)
{
    struct leonos_stat st;
    uint8_t *data;
    uint32_t len = 0;
    int fd;
    if (!out_data || !out_len || stat(path, &st) < 0 ||
        st.type != LEONOS_FS_TYPE_FILE || st.size == 0 ||
        st.size > 8U * 1024U * 1024U) {
        return -1;
    }
    data = (uint8_t *)malloc((size_t)st.size);
    if (!data) {
        return -1;
    }
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        free(data);
        return fd;
    }
    while (len < (uint32_t)st.size) {
        long got = read(fd, data + len, (uint32_t)st.size - len);
        if (got < 0) {
            close(fd);
            free(data);
            return (int)got;
        }
        if (got == 0) {
            break;
        }
        len += (uint32_t)got;
    }
    close(fd);
    if (len != (uint32_t)st.size) {
        free(data);
        return -1;
    }
    *out_data = data;
    *out_len = len;
    return 0;
}

static int decode_bmp(const uint8_t *data, uint32_t len)
{
    uint32_t pixel_offset;
    uint32_t dib_size;
    int32_t width_s;
    int32_t height_s;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t compression;
    uint32_t row_stride;
    uint32_t top_down;
    uint32_t *decoded;
    if (!data || len < 54U || data[0] != 'B' || data[1] != 'M') {
        return -1;
    }
    pixel_offset = read_le32(data + 10);
    dib_size = read_le32(data + 14);
    if (dib_size < 40U || pixel_offset >= len) {
        return -1;
    }
    width_s = read_le32s(data + 18);
    height_s = read_le32s(data + 22);
    bpp = read_le16(data + 28);
    compression = read_le32(data + 30);
    if (width_s <= 0 || height_s == 0 || compression != 0U ||
        (bpp != 24U && bpp != 32U)) {
        return -1;
    }
    width = (uint32_t)width_s;
    top_down = height_s < 0;
    height = top_down ? (uint32_t)(-height_s) : (uint32_t)height_s;
    if (width == 0 || height == 0 || width > 4096U || height > 4096U ||
        width * height > IMAGEVIEW_MAX_PIXELS) {
        return -1;
    }
    row_stride = ((width * bpp + 31U) / 32U) * 4U;
    if (pixel_offset + row_stride * height > len) {
        return -1;
    }
    decoded = (uint32_t *)malloc((size_t)width * (size_t)height * sizeof(uint32_t));
    if (!decoded) {
        return -1;
    }
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t src_y = top_down ? y : height - 1U - y;
        const uint8_t *row = data + pixel_offset + src_y * row_stride;
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t *px = row + x * (bpp / 8U);
            uint32_t b = px[0];
            uint32_t g = px[1];
            uint32_t r = px[2];
            decoded[y * width + x] = (r << 16) | (g << 8) | b;
        }
    }
    free_image();
    image_pixels = decoded;
    image_w = width;
    image_h = height;
    return 0;
}

static void rebuild_siblings(void)
{
    struct leonos_dir_entry entries[LEONOS_FS_MAX_ENTRIES];
    uint32_t count = 0;
    const char *base = path_basename(current_path);
    sibling_count = 0;
    sibling_index = 0;
    path_parent(current_dir, sizeof(current_dir), current_path);
    if (leonos_list_dir(current_dir, entries, LEONOS_FS_MAX_ENTRIES, &count) < 0) {
        return;
    }
    for (uint32_t i = 0; i < count && sibling_count < IMAGEVIEW_ROWS_MAX; ++i) {
        if (entries[i].type == LEONOS_FS_TYPE_FILE &&
            ends_with_ignore_case(entries[i].name, ".bmp")) {
            copy_text(sibling_names[sibling_count],
                      sizeof(sibling_names[0]), entries[i].name);
            if (text_eq_ignore_case(entries[i].name, base)) {
                sibling_index = sibling_count;
            }
            ++sibling_count;
        }
    }
}

static void rebuild_detail(void)
{
    uint32_t pos = 0;
    detail_text[0] = 0;
    if (!image_pixels) {
        copy_text(detail_text, sizeof(detail_text),
                  T("No image loaded.", "未加载图片。"));
        return;
    }
    append_u32(detail_text, &pos, sizeof(detail_text), image_w);
    append_char(detail_text, &pos, sizeof(detail_text), 'x');
    append_u32(detail_text, &pos, sizeof(detail_text), image_h);
    append_text(detail_text, &pos, sizeof(detail_text), "  ");
    append_text(detail_text, &pos, sizeof(detail_text),
                zoom_mode == ZOOM_FIT ? "Fit" : (zoom_mode == ZOOM_1X ? "1x" : "2x"));
    if (sibling_count) {
        append_text(detail_text, &pos, sizeof(detail_text), "  ");
        append_u32(detail_text, &pos, sizeof(detail_text), sibling_index + 1U);
        append_char(detail_text, &pos, sizeof(detail_text), '/');
        append_u32(detail_text, &pos, sizeof(detail_text), sibling_count);
    }
}

static int load_image_path(const char *path)
{
    uint8_t *data = 0;
    uint32_t len = 0;
    int ret;
    if (!path || !path[0]) {
        return -1;
    }
    ret = read_file_all(path, &data, &len);
    if (ret < 0) {
        copy_text(status_text, sizeof(status_text),
                  T("Could not read image.", "无法读取图片。"));
        return ret;
    }
    ret = decode_bmp(data, len);
    free(data);
    if (ret < 0) {
        copy_text(status_text, sizeof(status_text),
                  T("Unsupported BMP. Use uncompressed 24/32-bit BMP.", "不支持的 BMP，请使用未压缩 24/32 位 BMP。"));
        return ret;
    }
    copy_text(current_path, sizeof(current_path), path);
    rebuild_siblings();
    rebuild_detail();
    copy_text(status_text, sizeof(status_text), T("Image loaded", "图片已打开"));
    return 0;
}

static uint32_t canvas_y(void)
{
    return IMAGEVIEW_TOOLBAR_Y + IMAGEVIEW_TOOLBAR_H + 8U;
}

static uint32_t canvas_h(void)
{
    uint32_t y = canvas_y();
    return view_h > y + IMAGEVIEW_STATUS_H + IMAGEVIEW_DETAIL_H + 12U
               ? view_h - y - IMAGEVIEW_STATUS_H - IMAGEVIEW_DETAIL_H - 12U
               : 80U;
}

static uint32_t detail_y(void)
{
    uint32_t status_y = view_h > IMAGEVIEW_STATUS_H
                            ? view_h - IMAGEVIEW_STATUS_H
                            : 0;
    uint32_t y = canvas_y() + canvas_h() + 6U;
    if (y + IMAGEVIEW_DETAIL_H > status_y) {
        y = status_y > IMAGEVIEW_DETAIL_H + 2U
                ? status_y - IMAGEVIEW_DETAIL_H - 2U
                : status_y;
    }
    return y;
}

static void draw_scaled_image(struct leonos_ui_surface *ui)
{
    uint32_t x0 = 12U;
    uint32_t y0 = canvas_y();
    uint32_t w0 = view_w > 24U ? view_w - 24U : view_w;
    uint32_t h0 = canvas_h();
    uint32_t content_x = x0 + 3U;
    uint32_t content_y = y0 + 3U;
    uint32_t content_w = w0 > 6U ? w0 - 6U : 1U;
    uint32_t content_h = h0 > 6U ? h0 - 6U : 1U;
    uint32_t clip_x1 = content_x + content_w;
    uint32_t clip_y1 = content_y + content_h;
    uint32_t draw_w;
    uint32_t draw_h;
    uint32_t scale;
    uint32_t dst_x;
    uint32_t dst_y;
    leonos_ui_inset(ui, x0, y0, w0, h0, LEONOS_UI_WHITE);
    if (!image_pixels || !image_w || !image_h) {
        leonos_ui_text_clipped(ui, x0 + 18U, y0 + 18U, w0 > 36U ? w0 - 36U : w0,
                               T("Open a .bmp file from File Manager, Run, or command line.",
                                 "请从文件资源管理器、运行或命令行打开 .bmp 文件。"),
                               LEONOS_UI_DARK, LEONOS_UI_WHITE);
        return;
    }
    if (zoom_mode == ZOOM_FIT) {
        draw_w = content_w;
        draw_h = ((uint64_t)draw_w * image_h) / image_w;
        if (draw_h > content_h) {
            draw_h = content_h;
            draw_w = ((uint64_t)draw_h * image_w) / image_h;
        }
        if (!draw_w) {
            draw_w = 1U;
        }
        if (!draw_h) {
            draw_h = 1U;
        }
    } else {
        scale = zoom_mode == ZOOM_2X ? 2U : 1U;
        draw_w = image_w * scale;
        draw_h = image_h * scale;
    }
    dst_x = content_x + (content_w > draw_w ? (content_w - draw_w) / 2U : 0U);
    dst_y = content_y + (content_h > draw_h ? (content_h - draw_h) / 2U : 0U);
    for (uint32_t y = 0; y < draw_h && dst_y + y < clip_y1; ++y) {
        uint32_t sy = (uint64_t)y * image_h / draw_h;
        if (dst_y + y < content_y) {
            continue;
        }
        for (uint32_t x = 0; x < draw_w && dst_x + x < clip_x1; ++x) {
            uint32_t sx = (uint64_t)x * image_w / draw_w;
            if (dst_x + x < content_x) {
                continue;
            }
            leonos_ui_pixel(ui, dst_x + x, dst_y + y,
                            image_pixels[sy * image_w + sx]);
        }
    }
}

static void present(int window_id, struct leonos_ui_surface *ui)
{
    leonos_ui_bind(ui, pixels, view_w, view_h, IMAGEVIEW_MAX_W);
    leonos_ui_rect(ui, 0, 0, view_w, view_h, LEONOS_UI_GRAY);
    leonos_ui_toolbar(ui, 0, IMAGEVIEW_TOOLBAR_Y, view_w, IMAGEVIEW_TOOLBAR_H);
    leonos_ui_button(ui, 12, IMAGEVIEW_TOOLBAR_Y + 6U, 72,
                     LEONOS_UI_BUTTON_H, T("Previous", "上一张"),
                     sibling_count > 1U ? 0 : LEONOS_UI_BUTTON_DISABLED);
    leonos_ui_button(ui, 92, IMAGEVIEW_TOOLBAR_Y + 6U, 72,
                     LEONOS_UI_BUTTON_H, T("Next", "下一张"),
                     sibling_count > 1U ? 0 : LEONOS_UI_BUTTON_DISABLED);
    leonos_ui_button(ui, 184, IMAGEVIEW_TOOLBAR_Y + 6U, 56,
                     LEONOS_UI_BUTTON_H, "Fit",
                     zoom_mode == ZOOM_FIT ? LEONOS_UI_BUTTON_PRESSED : 0);
    leonos_ui_button(ui, 248, IMAGEVIEW_TOOLBAR_Y + 6U, 48,
                     LEONOS_UI_BUTTON_H, "1x",
                     zoom_mode == ZOOM_1X ? LEONOS_UI_BUTTON_PRESSED : 0);
    leonos_ui_button(ui, 304, IMAGEVIEW_TOOLBAR_Y + 6U, 48,
                     LEONOS_UI_BUTTON_H, "2x",
                     zoom_mode == ZOOM_2X ? LEONOS_UI_BUTTON_PRESSED : 0);
    leonos_ui_text_clipped(ui, 372, IMAGEVIEW_TOOLBAR_Y + 12U,
                           view_w > 392 ? view_w - 392 : 80,
                           current_path[0] ? current_path : T("No file", "没有文件"),
                           LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    draw_scaled_image(ui);
    leonos_ui_text_clipped(ui, 14, detail_y(),
                           view_w > 28U ? view_w - 28U : view_w,
                           detail_text, LEONOS_UI_DARK, LEONOS_UI_GRAY);
    leonos_ui_statusbar(ui, view_h - IMAGEVIEW_STATUS_H, IMAGEVIEW_STATUS_H,
                        status_text);
    leonos_gui_present_window((uint32_t)window_id, view_w, view_h,
                              IMAGEVIEW_MAX_W, pixels);
}

static int hit_rect(int32_t px, int32_t py, uint32_t x, uint32_t y,
                    uint32_t w, uint32_t h)
{
    return px >= (int32_t)x && py >= (int32_t)y &&
           px < (int32_t)(x + w) && py < (int32_t)(y + h);
}

static void load_sibling_delta(int delta)
{
    char next_path[LEONOS_FS_PATH_LEN];
    if (sibling_count <= 1U) {
        return;
    }
    if (delta < 0) {
        sibling_index = sibling_index == 0 ? sibling_count - 1U : sibling_index - 1U;
    } else {
        sibling_index = (sibling_index + 1U) % sibling_count;
    }
    build_child_path(next_path, sizeof(next_path), current_dir,
                     sibling_names[sibling_index]);
    (void)load_image_path(next_path);
}

static void handle_click(int32_t x, int32_t y)
{
    uint32_t button_y = IMAGEVIEW_TOOLBAR_Y + 6U;
    if (hit_rect(x, y, 12, button_y, 72, LEONOS_UI_BUTTON_H)) {
        load_sibling_delta(-1);
    } else if (hit_rect(x, y, 92, button_y, 72, LEONOS_UI_BUTTON_H)) {
        load_sibling_delta(1);
    } else if (hit_rect(x, y, 184, button_y, 56, LEONOS_UI_BUTTON_H)) {
        zoom_mode = ZOOM_FIT;
        rebuild_detail();
    } else if (hit_rect(x, y, 248, button_y, 48, LEONOS_UI_BUTTON_H)) {
        zoom_mode = ZOOM_1X;
        rebuild_detail();
    } else if (hit_rect(x, y, 304, button_y, 48, LEONOS_UI_BUTTON_H)) {
        zoom_mode = ZOOM_2X;
        rebuild_detail();
    }
}

int main(int argc, char **argv, char **envp)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    (void)envp;
    if (argc > 1 && argv && argv[1] && argv[1][0]) {
        (void)load_image_path(argv[1]);
    } else {
        copy_text(detail_text, sizeof(detail_text),
                  T("No image loaded.", "未加载图片。"));
    }
    window_id = leonos_gui_create_app_window_ex(T("Image Viewer", "图片查看器"),
                                                T("BMP image viewer", "BMP 图片查看器"),
                                                view_w, view_h, 0);
    if (window_id <= 0) {
        printf("[imageview.elf] create window failed=%d\n", window_id);
        free_image();
        return 1;
    }
    present(window_id, &ui);
    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                free_image();
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON &&
                (event.buttons & 1U)) {
                handle_click(event.x, event.y);
                present(window_id, &ui);
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN &&
                event.pressed && event.keycode == 1U) {
                free_image();
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_RESIZE ||
                event.type == LEONOS_GUI_APP_EVENT_FOCUS) {
                if (event.width) {
                    view_w = event.width > IMAGEVIEW_MAX_W ? IMAGEVIEW_MAX_W : event.width;
                    if (view_w < IMAGEVIEW_MIN_W) {
                        view_w = IMAGEVIEW_MIN_W;
                    }
                }
                if (event.height) {
                    view_h = event.height > IMAGEVIEW_MAX_H ? IMAGEVIEW_MAX_H : event.height;
                    if (view_h < IMAGEVIEW_MIN_H) {
                        view_h = IMAGEVIEW_MIN_H;
                    }
                }
                present(window_id, &ui);
            }
        } else {
            sleep_ms(10);
        }
    }
}
