#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/png.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>
#include <png.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PAINT_W 1000U
#define PAINT_H 700U
#define PAINT_MIN_W 560U
#define PAINT_MIN_H 400U
#define PAINT_MAX_W LEONOS_GUI_MAX_WINDOW_WIDTH
#define PAINT_MAX_H LEONOS_GUI_MAX_WINDOW_HEIGHT
#define PAINT_MAX_PIXELS (1024U * 1024U)
#define PAINT_PATH_CAP LEONOS_FS_PATH_LEN
#define TOOLBAR_H 44U
#define STATUS_H 26U
#define CANVAS_MARGIN 10U
#define T(en, zh) leonos_i18n((en), (zh))

enum paint_tool {
    TOOL_PENCIL = 0,
    TOOL_BRUSH,
    TOOL_ERASER,
};

static uint32_t *screen_pixels;
static uint32_t screen_stride;
static uint32_t screen_height;
static uint32_t *canvas;
static uint32_t canvas_w;
static uint32_t canvas_h;
static uint32_t view_w = PAINT_W;
static uint32_t view_h = PAINT_H;
static uint32_t color = 0x00000000U;
static uint32_t brush_size = 4U;
static uint32_t tool = TOOL_BRUSH;
static uint8_t drawing;
static uint8_t ctrl_down;
static uint8_t dirty;
static char current_path[PAINT_PATH_CAP];
static char status_text[160];

static int ensure_screen_buffer(void)
{
    uint64_t pixel_count = (uint64_t)view_w * view_h;
    uint32_t *next;
    if (!view_w || !view_h || pixel_count > (uint64_t)PAINT_MAX_W * PAINT_MAX_H) {
        return -1;
    }
    if (screen_pixels && screen_stride == view_w && screen_height == view_h) {
        return 0;
    }
    next = (uint32_t *)malloc((size_t)pixel_count * sizeof(uint32_t));
    if (!next) {
        return -1;
    }
    free(screen_pixels);
    screen_pixels = next;
    screen_stride = view_w;
    screen_height = view_h;
    return 0;
}

static uint32_t text_len(const char *s)
{
    uint32_t n = 0;
    while (s && s[n]) {
        ++n;
    }
    return n;
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

static int text_eq_ci(const char *a, const char *b)
{
    uint32_t i = 0;
    while (a && b && a[i] && b[i]) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        ++i;
    }
    return (!a || !a[i]) && (!b || !b[i]);
}

static int ends_ci(const char *path, const char *suffix)
{
    uint32_t path_len = text_len(path);
    uint32_t suffix_len = text_len(suffix);
    return suffix_len <= path_len &&
           text_eq_ci(path + path_len - suffix_len, suffix);
}

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

static void write_le16(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void set_status(const char *text)
{
    copy_text(status_text, sizeof(status_text), text);
}

static void free_canvas(void)
{
    free(canvas);
    canvas = 0;
    canvas_w = 0;
    canvas_h = 0;
}

static int new_canvas(uint32_t width, uint32_t height)
{
    uint64_t pixels = (uint64_t)width * height;
    uint32_t *next;
    if (!width || !height || pixels > PAINT_MAX_PIXELS) {
        return -1;
    }
    next = (uint32_t *)malloc((size_t)pixels * sizeof(uint32_t));
    if (!next) {
        return -1;
    }
    for (uint64_t i = 0; i < pixels; ++i) {
        next[i] = 0x00ffffffU;
    }
    free_canvas();
    canvas = next;
    canvas_w = width;
    canvas_h = height;
    dirty = 0;
    current_path[0] = 0;
    return 0;
}

static int read_file(const char *path, uint8_t **out, uint32_t *out_len)
{
    struct leonos_stat st;
    uint8_t *data;
    uint32_t offset = 0;
    int fd;
    if (!path || !out || !out_len || stat(path, &st) < 0 ||
        st.type != LEONOS_FS_TYPE_FILE || st.size == 0 ||
        st.size > LEONOS_PNG_MAX_FILE_BYTES) {
        return -1;
    }
    data = (uint8_t *)malloc((size_t)st.size);
    if (!data) return -1;
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) { free(data); return fd; }
    while (offset < (uint32_t)st.size) {
        long got = read(fd, data + offset, (uint32_t)st.size - offset);
        if (got <= 0) { close(fd); free(data); return -1; }
        offset += (uint32_t)got;
    }
    close(fd);
    *out = data;
    *out_len = offset;
    return 0;
}

static int decode_bmp(const uint8_t *data, uint32_t len,
                      uint32_t **out, uint32_t *out_w, uint32_t *out_h)
{
    int32_t width_s;
    int32_t height_s;
    uint32_t width, height, offset, bpp, compression, stride;
    uint32_t *pixels;
    if (!data || len < 54U || data[0] != 'B' || data[1] != 'M') return -1;
    offset = read_le32(data + 10);
    if (read_le32(data + 14) < 40U || offset >= len) return -1;
    width_s = read_le32s(data + 18);
    height_s = read_le32s(data + 22);
    bpp = read_le16(data + 28);
    compression = read_le32(data + 30);
    if (width_s <= 0 || height_s == 0 || compression != 0U ||
        (bpp != 24U && bpp != 32U)) return -1;
    width = (uint32_t)width_s;
    height = height_s < 0 ? (uint32_t)(-height_s) : (uint32_t)height_s;
    stride = ((width * bpp + 31U) / 32U) * 4U;
    if (!width || !height || (uint64_t)width * height > PAINT_MAX_PIXELS ||
        (uint64_t)offset + (uint64_t)stride * height > len) return -1;
    pixels = (uint32_t *)malloc((size_t)width * height * sizeof(uint32_t));
    if (!pixels) return -1;
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t sy = height_s < 0 ? y : height - 1U - y;
        const uint8_t *row = data + offset + (uint64_t)sy * stride;
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t *px = row + x * (bpp / 8U);
            pixels[y * width + x] = ((uint32_t)px[2] << 16) |
                                     ((uint32_t)px[1] << 8) | px[0];
        }
    }
    *out = pixels;
    *out_w = width;
    *out_h = height;
    return 0;
}

static int load_image(const char *path)
{
    uint32_t *pixels = 0;
    uint32_t width = 0, height = 0;
    uint8_t *data = 0;
    uint32_t len = 0;
    int ret;
    if (ends_ci(path, ".png")) {
        ret = leonos_png_decode_file(path, &pixels, &width, &height);
    } else {
        ret = read_file(path, &data, &len);
        if (ret == 0) ret = decode_bmp(data, len, &pixels, &width, &height);
        free(data);
    }
    if (ret < 0 || !pixels || width == 0 || height == 0) {
        free(pixels);
        set_status(T("Could not open image", "无法打开图片"));
        return -1;
    }
    free_canvas();
    canvas = pixels;
    canvas_w = width;
    canvas_h = height;
    copy_text(current_path, sizeof(current_path), path);
    dirty = 0;
    set_status(T("Image opened", "图片已打开"));
    return 0;
}

static int write_all(int fd, const void *buffer, uint32_t length)
{
    const uint8_t *data = (const uint8_t *)buffer;
    while (length) {
        long wrote = write(fd, data, length);
        if (wrote <= 0) return -1;
        data += wrote;
        length -= (uint32_t)wrote;
    }
    return 0;
}

static int save_bmp(const char *path)
{
    uint8_t header[54];
    uint32_t stride = ((canvas_w * 24U + 31U) / 32U) * 4U;
    uint32_t file_size = 54U + stride * canvas_h;
    uint8_t *row;
    int fd;
    memset(header, 0, sizeof(header));
    header[0] = 'B'; header[1] = 'M';
    write_le32(header + 2, file_size);
    write_le32(header + 10, 54U);
    write_le32(header + 14, 40U);
    write_le32(header + 18, canvas_w);
    write_le32(header + 22, canvas_h);
    write_le16(header + 26, 1U);
    write_le16(header + 28, 24U);
    write_le32(header + 34, stride * canvas_h);
    fd = open(path, LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    if (fd < 0) return fd;
    row = (uint8_t *)malloc(stride);
    if (!row || write_all(fd, header, sizeof(header)) < 0) {
        free(row); close(fd); return -1;
    }
    for (uint32_t y = 0; y < canvas_h; ++y) {
        uint32_t sy = canvas_h - 1U - y;
        memset(row, 0, stride);
        for (uint32_t x = 0; x < canvas_w; ++x) {
            uint32_t pixel = canvas[sy * canvas_w + x];
            row[x * 3U] = (uint8_t)pixel;
            row[x * 3U + 1U] = (uint8_t)(pixel >> 8);
            row[x * 3U + 2U] = (uint8_t)(pixel >> 16);
        }
        if (write_all(fd, row, stride) < 0) {
            free(row); close(fd); return -1;
        }
    }
    free(row);
    close(fd);
    return 0;
}

static int save_png(const char *path)
{
    png_image image;
    uint8_t *rgb;
    uint64_t bytes = (uint64_t)canvas_w * canvas_h * 3U;
    int ret;
    if (bytes > 0xffffffffULL) return -1;
    rgb = (uint8_t *)malloc((size_t)bytes);
    if (!rgb) return -1;
    for (uint64_t i = 0; i < (uint64_t)canvas_w * canvas_h; ++i) {
        uint32_t pixel = canvas[i];
        rgb[i * 3U] = (uint8_t)(pixel >> 16);
        rgb[i * 3U + 1U] = (uint8_t)(pixel >> 8);
        rgb[i * 3U + 2U] = (uint8_t)pixel;
    }
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    image.width = canvas_w;
    image.height = canvas_h;
    image.format = PNG_FORMAT_RGB;
    ret = png_image_write_to_file(&image, path, 0, rgb, 0, 0) ? 0 : -1;
    png_image_free(&image);
    free(rgb);
    return ret;
}

static int save_image(const char *path)
{
    int ret;
    if (!path || !path[0] || !canvas) return -1;
    ret = ends_ci(path, ".png") ? save_png(path) : save_bmp(path);
    if (ret < 0) {
        set_status(T("Could not save image", "无法保存图片"));
        return ret;
    }
    copy_text(current_path, sizeof(current_path), path);
    dirty = 0;
    set_status(T("Image saved", "图片已保存"));
    return 0;
}

static int hit(int32_t x, int32_t y, uint32_t rx, uint32_t ry,
               uint32_t w, uint32_t h)
{
    return x >= (int32_t)rx && y >= (int32_t)ry &&
           x < (int32_t)(rx + w) && y < (int32_t)(ry + h);
}

static uint32_t canvas_x(void) { return CANVAS_MARGIN; }
static uint32_t canvas_y(void) { return TOOLBAR_H; }
static uint32_t canvas_view_w(void)
{
    return view_w > CANVAS_MARGIN * 2U ? view_w - CANVAS_MARGIN * 2U : 1U;
}
static uint32_t canvas_view_h(void)
{
    return view_h > TOOLBAR_H + STATUS_H + CANVAS_MARGIN
               ? view_h - TOOLBAR_H - STATUS_H - CANVAS_MARGIN : 1U;
}

static int point_to_canvas(int32_t x, int32_t y, uint32_t *out_x, uint32_t *out_y)
{
    uint32_t vw = canvas_view_w();
    uint32_t vh = canvas_view_h();
    if (x < (int32_t)canvas_x() || y < (int32_t)canvas_y() ||
        x >= (int32_t)(canvas_x() + vw) || y >= (int32_t)(canvas_y() + vh)) return 0;
    *out_x = (uint32_t)((uint64_t)(x - (int32_t)canvas_x()) * canvas_w / vw);
    *out_y = (uint32_t)((uint64_t)(y - (int32_t)canvas_y()) * canvas_h / vh);
    if (*out_x >= canvas_w) *out_x = canvas_w - 1U;
    if (*out_y >= canvas_h) *out_y = canvas_h - 1U;
    return 1;
}

static void paint_point(uint32_t px, uint32_t py)
{
    int32_t radius = (int32_t)(brush_size / 2U);
    uint32_t paint_color = tool == TOOL_ERASER ? 0x00ffffffU : color;
    if (radius < 1) radius = 1;
    for (int32_t y = -radius; y <= radius; ++y) {
        for (int32_t x = -radius; x <= radius; ++x) {
            int32_t xx = (int32_t)px + x;
            int32_t yy = (int32_t)py + y;
            if (tool == TOOL_BRUSH && x * x + y * y > radius * radius) continue;
            if (xx >= 0 && yy >= 0 && (uint32_t)xx < canvas_w && (uint32_t)yy < canvas_h)
                canvas[(uint32_t)yy * canvas_w + (uint32_t)xx] = paint_color;
        }
    }
    dirty = 1;
}

static void paint_line(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    int32_t dx = (int32_t)x1 - (int32_t)x0;
    int32_t dy = (int32_t)y1 - (int32_t)y0;
    int32_t steps = dx < 0 ? -dx : dx;
    int32_t abs_dy = dy < 0 ? -dy : dy;
    if (abs_dy > steps) steps = abs_dy;
    if (steps == 0) { paint_point(x0, y0); return; }
    for (int32_t i = 0; i <= steps; ++i) {
        paint_point((uint32_t)((int32_t)x0 + dx * i / steps),
                    (uint32_t)((int32_t)y0 + dy * i / steps));
    }
}

static void open_dialog(void)
{
    char path[PAINT_PATH_CAP] = {0};
    if (leonos_ui_show_open_dialog(T("Open image", "打开图片"), path, sizeof(path),
                                   T("Images (*.bmp; *.dib; *.png)", "图片 (*.bmp; *.dib; *.png)"),
                                   ".bmp;.dib;.png") > 0) {
        (void)load_image(path);
    }
}

static void save_as_dialog(void)
{
    char path[PAINT_PATH_CAP];
    copy_text(path, sizeof(path), current_path[0] ? current_path : "/untitled.bmp");
    if (leonos_ui_show_save_dialog_ex(T("Save image", "保存图片"), path, sizeof(path),
                                      T("Bitmap or PNG (*.bmp; *.png)", "位图或 PNG (*.bmp; *.png)"),
                                      ".bmp;.png") > 0) {
        (void)save_image(path);
    }
}

static void save_current(void)
{
    if (current_path[0]) {
        (void)save_image(current_path);
    } else {
        save_as_dialog();
    }
}

static void new_image(void)
{
    if (dirty && !leonos_ui_show_confirm_dialog(T("Discard changes?", "放弃更改？"),
                                                 T("The current drawing has not been saved.", "当前绘画尚未保存。"), 0)) return;
    if (new_canvas(800U, 520U) < 0) {
        set_status(T("Could not create canvas", "无法创建画布"));
    } else {
        set_status(T("New canvas", "新建画布"));
    }
}

static void draw(struct leonos_ui_surface *ui, uint32_t window_id)
{
    uint32_t vw = canvas_view_w();
    uint32_t vh = canvas_view_h();
    if (ensure_screen_buffer() < 0) {
        return;
    }
    leonos_ui_bind(ui, screen_pixels, view_w, view_h, screen_stride);
    leonos_ui_rect(ui, 0, 0, view_w, view_h, LEONOS_UI_GRAY);
    leonos_ui_toolbar(ui, 0, 0, view_w, TOOLBAR_H);
    leonos_ui_button(ui, 8, 10, 52, LEONOS_UI_BUTTON_H, T("New", "新建"), 0);
    leonos_ui_button(ui, 66, 10, 58, LEONOS_UI_BUTTON_H, T("Open", "打开"), 0);
    leonos_ui_button(ui, 128, 10, 58, LEONOS_UI_BUTTON_H, T("Save", "保存"), dirty ? LEONOS_UI_BUTTON_ACTIVE : 0);
    leonos_ui_button(ui, 190, 10, 76, LEONOS_UI_BUTTON_H, T("Save as", "另存为"), 0);
    leonos_ui_button(ui, 274, 10, 58, LEONOS_UI_BUTTON_H, T("Pencil", "铅笔"), tool == TOOL_PENCIL ? LEONOS_UI_BUTTON_PRESSED : 0);
    leonos_ui_button(ui, 336, 10, 58, LEONOS_UI_BUTTON_H, T("Brush", "画笔"), tool == TOOL_BRUSH ? LEONOS_UI_BUTTON_PRESSED : 0);
    leonos_ui_button(ui, 398, 10, 58, LEONOS_UI_BUTTON_H, T("Eraser", "橡皮"), tool == TOOL_ERASER ? LEONOS_UI_BUTTON_PRESSED : 0);
    leonos_ui_text(ui, 466, 16, T("Size", "粗细"), LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    for (uint32_t i = 0; i < 3; ++i) {
        uint32_t sizes[3] = {2U, 6U, 14U};
        uint32_t x = 505U + i * 24U;
        leonos_ui_button(ui, x, 10, 20, LEONOS_UI_BUTTON_H,
                         i == 0 ? "S" : (i == 1 ? "M" : "L"),
                         brush_size == sizes[i] ? LEONOS_UI_BUTTON_PRESSED : 0);
    }
    {
        const uint32_t swatches[] = {0x00000000U, 0x00ff0000U, 0x000080ffU,
                                     0x0000aa00U, 0x00ffff00U, 0x00ffffffU};
        for (uint32_t i = 0; i < 6; ++i) {
            uint32_t x = 584U + i * 24U;
            leonos_ui_bevel(ui, x, 11, 20, 20, swatches[i], color == swatches[i] ? LEONOS_UI_BUTTON_PRESSED : 0);
        }
    }
    leonos_ui_inset(ui, canvas_x(), canvas_y(), vw, vh, LEONOS_UI_WHITE);
    if (canvas && canvas_w && canvas_h) {
        if (canvas_w == vw && canvas_h == vh) {
            for (uint32_t y = 0; y < vh; ++y) {
                memcpy(screen_pixels + (canvas_y() + y) * screen_stride + canvas_x(),
                       canvas + y * canvas_w, vw * sizeof(uint32_t));
            }
        } else {
            for (uint32_t y = 0; y < vh; ++y) {
                uint32_t sy = (uint64_t)y * canvas_h / vh;
                uint32_t *dst = screen_pixels + (canvas_y() + y) * screen_stride + canvas_x();
                for (uint32_t x = 0; x < vw; ++x) {
                    uint32_t sx = (uint64_t)x * canvas_w / vw;
                    dst[x] = canvas[sy * canvas_w + sx];
                }
            }
        }
    }
    /* The status bar stays below the canvas even when the window is resized. */
    leonos_ui_statusbar(ui, view_h - STATUS_H, STATUS_H, status_text);
    leonos_gui_present_window(window_id, view_w, view_h, screen_stride, screen_pixels);
    (void)window_id;
}

static void handle_toolbar(int32_t x, int32_t y)
{
    if (y < 10 || y >= 10 + (int32_t)LEONOS_UI_BUTTON_H) return;
    if (hit(x, y, 8, 10, 52, LEONOS_UI_BUTTON_H)) new_image();
    else if (hit(x, y, 66, 10, 58, LEONOS_UI_BUTTON_H)) open_dialog();
    else if (hit(x, y, 128, 10, 58, LEONOS_UI_BUTTON_H)) save_current();
    else if (hit(x, y, 190, 10, 76, LEONOS_UI_BUTTON_H)) save_as_dialog();
    else if (hit(x, y, 274, 10, 58, LEONOS_UI_BUTTON_H)) tool = TOOL_PENCIL;
    else if (hit(x, y, 336, 10, 58, LEONOS_UI_BUTTON_H)) tool = TOOL_BRUSH;
    else if (hit(x, y, 398, 10, 58, LEONOS_UI_BUTTON_H)) tool = TOOL_ERASER;
    else if (hit(x, y, 505, 10, 20, LEONOS_UI_BUTTON_H)) brush_size = 2U;
    else if (hit(x, y, 529, 10, 20, LEONOS_UI_BUTTON_H)) brush_size = 6U;
    else if (hit(x, y, 553, 10, 20, LEONOS_UI_BUTTON_H)) brush_size = 14U;
    else {
        const uint32_t swatches[] = {0x00000000U, 0x00ff0000U, 0x000080ffU,
                                     0x0000aa00U, 0x00ffff00U, 0x00ffffffU};
        for (uint32_t i = 0; i < 6; ++i) {
            if (hit(x, y, 584U + i * 24U, 11, 20, 20)) color = swatches[i];
        }
    }
}

int main(int argc, char **argv, char **envp)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    uint32_t last_x = 0, last_y = 0;
    int window_id;
    (void)envp;
    if (new_canvas(800U, 520U) < 0) return 1;
    set_status(T("Ready", "就绪"));
    if (argc > 1 && argv && argv[1] && argv[1][0]) (void)load_image(argv[1]);
    window_id = leonos_gui_create_app_window_ex(T("Paint", "画图"),
                                                T("LeonOS Paint", "LeonOS 画图"),
                                                view_w, view_h, 0);
    if (window_id <= 0) { free_canvas(); free(screen_pixels); return 1; }
    draw(&ui, (uint32_t)window_id);
    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_wait_app_event(&event, LEONOS_GUI_IDLE_WAIT_MS) <= 0) {
            sleep_ms(10);
            continue;
        }
        if (event.type == LEONOS_GUI_APP_EVENT_CLOSE ||
            (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN && event.pressed && event.keycode == LEONOS_KEY_ESCAPE)) {
            if (dirty) {
                int save = leonos_ui_show_confirm_dialog(
                    T("Save changes?", "保存更改？"),
                    T("Save the current drawing before closing?", "关闭前保存当前绘画？"), 1);
                if (save > 0) {
                    save_current();
                    if (dirty) {
                        draw(&ui, (uint32_t)window_id);
                        continue;
                    }
                }
            }
            free_canvas();
            free(screen_pixels);
            screen_pixels = 0;
            screen_stride = 0;
            screen_height = 0;
            return 0;
        }
        if (event.type == LEONOS_GUI_APP_EVENT_RESIZE) {
            if (event.width >= PAINT_MIN_W && event.width <= PAINT_MAX_W) view_w = event.width;
            if (event.height >= PAINT_MIN_H && event.height <= PAINT_MAX_H) view_h = event.height;
            draw(&ui, (uint32_t)window_id);
            continue;
        }
        if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN || event.type == LEONOS_GUI_APP_EVENT_KEY_UP) {
            if (event.keycode == LEONOS_KEY_LEFT_CTRL || event.keycode == LEONOS_KEY_RIGHT_CTRL) ctrl_down = event.pressed;
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN && event.pressed && ctrl_down) {
                if (event.keycode == 49U) new_image();
                else if (event.keycode == 24U) open_dialog();
                else if (event.keycode == 31U) save_current();
                else if (event.keycode == 45U) save_as_dialog();
                draw(&ui, (uint32_t)window_id);
            }
            continue;
        }
        if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON) {
            if (!(event.buttons & 1U)) { drawing = 0; continue; }
            if (event.y < (int32_t)TOOLBAR_H) {
                handle_toolbar(event.x, event.y);
            } else if (point_to_canvas(event.x, event.y, &last_x, &last_y)) {
                drawing = 1;
                paint_point(last_x, last_y);
            }
            draw(&ui, (uint32_t)window_id);
            continue;
        }
        if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_MOVE && (event.buttons & 1U) && drawing) {
            uint32_t x, y;
            if (point_to_canvas(event.x, event.y, &x, &y)) {
                paint_line(last_x, last_y, x, y);
                last_x = x; last_y = y;
                draw(&ui, (uint32_t)window_id);
            }
            continue;
        }
        if (event.type == LEONOS_GUI_APP_EVENT_FOCUS) draw(&ui, (uint32_t)window_id);
    }
}
