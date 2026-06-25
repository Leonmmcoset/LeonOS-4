#include <ntclks/console.h>
#include <ntclks/framebuffer.h>
#include <leonos/psf_font.h>

static struct framebuffer fb;

#define CURSOR_W 16
#define CURSOR_H 16

static uint32_t cursor_bg[CURSOR_W * CURSOR_H];
static uint32_t cursor_x;
static uint32_t cursor_y;
static bool cursor_visible;

static const char cursor_art[CURSOR_H][CURSOR_W + 1] = {
    "X...............",
    "XO..............",
    "XOX.............",
    "XOOX............",
    "XOOOX...........",
    "XOOOOX..........",
    "XOOOOOX.........",
    "XOOOOOOX........",
    "XOOOOOOOX.......",
    "XOOOOOOOOX......",
    "XOOOOOOOOOX.....",
    "XOOOOX..........",
    "XOOOOX..........",
    "XOOOOX..........",
    "XOOOXX..........",
    "XXXXX...........",
};

#define DESKTOP_MAX_WINDOWS 3
#define DESKTOP_TASKBAR_H 34
#define DESKTOP_TITLEBAR_H 26
#define DESKTOP_MIN_W 180
#define DESKTOP_MIN_H 96

#define DESKTOP_DRAG_NONE 0
#define DESKTOP_DRAG_MOVE 1
#define DESKTOP_DRAG_RESIZE 2

struct desktop_window {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    int32_t restore_x;
    int32_t restore_y;
    uint32_t restore_width;
    uint32_t restore_height;
    const char *title;
    uint32_t body_color;
    bool visible;
    bool minimized;
    bool maximized;
};

static struct desktop_window windows[DESKTOP_MAX_WINDOWS];
static uint8_t z_order[DESKTOP_MAX_WINDOWS];
static bool desktop_ready;
static bool start_menu_open;
static int32_t active_window;
static int32_t drag_window;
static uint8_t drag_mode;
static int32_t drag_dx;
static int32_t drag_dy;
static int32_t drag_origin_x;
static int32_t drag_origin_y;
static uint32_t drag_origin_w;
static uint32_t drag_origin_h;
static uint8_t previous_mouse_buttons;

struct efi_guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};

struct efi_table_header {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
};

struct efi_system_table {
    struct efi_table_header hdr;
    uint16_t *firmware_vendor;
    uint32_t firmware_revision;
    void *console_in_handle;
    void *con_in;
    void *console_out_handle;
    void *con_out;
    void *standard_error_handle;
    void *std_err;
    void *runtime_services;
    struct efi_boot_services *boot_services;
};

typedef uint64_t efi_status_t;
typedef void *efi_handle_t;

typedef efi_status_t (__attribute__((ms_abi)) *efi_handle_protocol_fn)(
    efi_handle_t handle,
    struct efi_guid *protocol,
    void **interface);
typedef efi_status_t (__attribute__((ms_abi)) *efi_locate_handle_buffer_fn)(
    uint64_t search_type,
    struct efi_guid *protocol,
    void *search_key,
    uint64_t *no_handles,
    efi_handle_t **buffer);

struct efi_boot_services {
    struct efi_table_header hdr;
    efi_status_t (*raise_tpl)(uint64_t tpl);
    void (*restore_tpl)(uint64_t tpl);
    char _pad1[88];
    efi_status_t (*install_protocol_interface)(void);
    efi_status_t (*reinstall_protocol_interface)(void);
    efi_status_t (*uninstall_protocol_interface)(void);
    efi_handle_protocol_fn handle_protocol;
    void *_reserved;
    efi_status_t (*register_protocol_notify)(void);
    efi_status_t (*locate_handle)(void);
    efi_status_t (*locate_device_path)(void);
    efi_status_t (*install_configuration_table)(void);
    efi_status_t (*load_image)(void);
    efi_status_t (*start_image)(void);
    efi_status_t (*exit)(void);
    efi_status_t (*unload_image)(void);
    efi_status_t (*exit_boot_services)(void);
    efi_status_t (*get_next_monotonic_count)(void);
    efi_status_t (*stall)(void);
    efi_status_t (*set_watchdog_timer)(void);
    efi_status_t (*connect_controller)(void);
    efi_status_t (*disconnect_controller)(void);
    efi_status_t (*open_protocol)(void);
    efi_status_t (*close_protocol)(void);
    efi_status_t (*open_protocol_information)(void);
    efi_status_t (*protocols_per_handle)(void);
    efi_locate_handle_buffer_fn locate_handle_buffer;
};

struct efi_gop_mode_info {
    uint32_t version;
    uint32_t horizontal_resolution;
    uint32_t vertical_resolution;
    uint32_t pixel_format;
    uint32_t pixel_information[4];
    uint32_t pixels_per_scan_line;
};

struct efi_gop_mode {
    uint32_t max_mode;
    uint32_t mode;
    struct efi_gop_mode_info *info;
    uint64_t size_of_info;
    uint64_t framebuffer_base;
    uint64_t framebuffer_size;
};

struct efi_graphics_output_protocol {
    void *query_mode;
    void *set_mode;
    void *blt;
    struct efi_gop_mode *mode;
};

static int guid_equal(const struct efi_guid *a, const struct efi_guid *b)
{
    if (a->data1 != b->data1 || a->data2 != b->data2 || a->data3 != b->data3) {
        return 0;
    }
    for (uint32_t i = 0; i < 8; ++i) {
        if (a->data4[i] != b->data4[i]) {
            return 0;
        }
    }
    return 1;
}

static void framebuffer_init_from_gop(uint64_t system_table_addr)
{
    if (fb.available || system_table_addr == 0) {
        return;
    }

    static struct efi_guid gop_guid = {
        0x9042a9de,
        0x23dc,
        0x4a38,
        {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a},
    };

    struct efi_system_table *st = (struct efi_system_table *)(uintptr_t)system_table_addr;
    if (!st || !st->boot_services || !st->boot_services->locate_handle_buffer || !st->boot_services->handle_protocol) {
        return;
    }

    uint64_t handle_count = 0;
    efi_handle_t *handles = 0;
    efi_status_t status = st->boot_services->locate_handle_buffer(2, &gop_guid, 0, &handle_count, &handles);
    if (status || handle_count == 0 || !handles) {
        console_printf("[ntclks] GOP locate failed status=0x%llx handles=%llu\n",
                       (unsigned long long)status,
                       (unsigned long long)handle_count);
        return;
    }

    for (uint64_t i = 0; i < handle_count; ++i) {
        void *interface = 0;
        status = st->boot_services->handle_protocol(handles[i], &gop_guid, &interface);
        if (status || !interface) {
            continue;
        }
        struct efi_graphics_output_protocol *gop = (struct efi_graphics_output_protocol *)interface;
        if (!gop->mode || !gop->mode->info || gop->mode->framebuffer_base == 0) {
            continue;
        }
        fb.pixels = (uint32_t *)(uintptr_t)gop->mode->framebuffer_base;
        fb.width = gop->mode->info->horizontal_resolution;
        fb.height = gop->mode->info->vertical_resolution;
        fb.pitch = gop->mode->info->pixels_per_scan_line * 4;
        fb.bpp = 32;
        fb.available = true;
        console_printf("[ntclks] GOP framebuffer base=%p size=%llu\n",
                       (void *)(uintptr_t)gop->mode->framebuffer_base,
                       (unsigned long long)gop->mode->framebuffer_size);
        return;
    }

    console_printf("[ntclks] GOP protocol present but no usable mode\n");
    (void)guid_equal;
}


void framebuffer_init(const struct boot_info *boot)
{
    fb.pixels = (uint32_t *)(uintptr_t)boot->framebuffer_addr;
    fb.width = boot->framebuffer_width;
    fb.height = boot->framebuffer_height;
    fb.pitch = boot->framebuffer_pitch;
    fb.bpp = boot->framebuffer_bpp;
    fb.available = boot->framebuffer_addr != 0 && boot->framebuffer_bpp == 32;
    framebuffer_init_from_gop(boot->efi_system_table);

    if (fb.available) {
        console_printf("[ntclks] framebuffer %ux%u pitch=%u bpp=%u\n",
                       fb.width, fb.height, fb.pitch, fb.bpp);
    } else {
        console_printf("[ntclks] framebuffer unavailable, VGA fallback active\n");
    }
}

const struct framebuffer *framebuffer_get(void)
{
    return &fb;
}

void framebuffer_clear(uint32_t color)
{
    if (!fb.available) {
        return;
    }
    for (uint32_t y = 0; y < fb.height; ++y) {
        uint32_t *row = (uint32_t *)((uint8_t *)fb.pixels + (uint64_t)y * fb.pitch);
        for (uint32_t x = 0; x < fb.width; ++x) {
            row[x] = color;
        }
    }
}

void framebuffer_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    if (!fb.available || x >= fb.width || y >= fb.height) {
        return;
    }
    if (x + w > fb.width) {
        w = fb.width - x;
    }
    if (y + h > fb.height) {
        h = fb.height - y;
    }
    for (uint32_t yy = y; yy < y + h; ++yy) {
        uint32_t *row = (uint32_t *)((uint8_t *)fb.pixels + (uint64_t)yy * fb.pitch);
        for (uint32_t xx = x; xx < x + w; ++xx) {
            row[xx] = color;
        }
    }
}

void framebuffer_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t stride, const uint32_t *pixels)
{
    if (!fb.available || !pixels || x >= fb.width || y >= fb.height || stride < w) {
        return;
    }
    if (x + w > fb.width) {
        w = fb.width - x;
    }
    if (y + h > fb.height) {
        h = fb.height - y;
    }
    for (uint32_t yy = 0; yy < h; ++yy) {
        uint32_t *dst = (uint32_t *)((uint8_t *)fb.pixels + (uint64_t)(y + yy) * fb.pitch);
        const uint32_t *src = pixels + (uint64_t)yy * stride;
        for (uint32_t xx = 0; xx < w; ++xx) {
            dst[x + xx] = src[xx];
        }
    }
}

static uint32_t framebuffer_get_pixel(uint32_t x, uint32_t y)
{
    if (!fb.available || x >= fb.width || y >= fb.height) {
        return 0;
    }
    uint32_t *row = (uint32_t *)((uint8_t *)fb.pixels + (uint64_t)y * fb.pitch);
    return row[x];
}

static void framebuffer_put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (!fb.available || x >= fb.width || y >= fb.height) {
        return;
    }
    uint32_t *row = (uint32_t *)((uint8_t *)fb.pixels + (uint64_t)y * fb.pitch);
    row[x] = color;
}

uint32_t framebuffer_get_pixel_public(uint32_t x, uint32_t y)
{
    return framebuffer_get_pixel(x, y);
}

void framebuffer_put_pixel_public(uint32_t x, uint32_t y, uint32_t color)
{
    framebuffer_put_pixel(x, y, color);
}

static void framebuffer_char(uint32_t x, uint32_t y, char ch, uint32_t fg, uint32_t bg)
{
    const uint8_t *glyph = leonos_psf_glyph(ch);
    for (uint32_t row = 0; row < LEONOS_FONT_H; ++row) {
        for (uint32_t col = 0; col < LEONOS_FONT_W; ++col) {
            uint32_t color = (glyph[row] & (uint8_t)(0x80u >> col)) ? fg : bg;
            framebuffer_rect(x + col, y + row, 1, 1, color);
        }
    }
}

static void framebuffer_char_transparent(uint32_t x, uint32_t y, char ch, uint32_t fg)
{
    const uint8_t *glyph = leonos_psf_glyph(ch);
    for (uint32_t row = 0; row < LEONOS_FONT_H; ++row) {
        for (uint32_t col = 0; col < LEONOS_FONT_W; ++col) {
            if (glyph[row] & (uint8_t)(0x80u >> col)) {
                framebuffer_rect(x + col, y + row, 1, 1, fg);
            }
        }
    }
}

void framebuffer_text(uint32_t x, uint32_t y, const char *text, uint32_t fg, uint32_t bg)
{
    for (uint32_t i = 0; text && text[i]; ++i) {
        framebuffer_char(x + i * LEONOS_FONT_W, y, text[i], fg, bg);
    }
}

static void framebuffer_text_transparent(uint32_t x, uint32_t y, const char *text, uint32_t fg)
{
    for (uint32_t i = 0; text && text[i]; ++i) {
        framebuffer_char_transparent(x + i * LEONOS_FONT_W, y, text[i], fg);
    }
}

static int hit_rect(uint32_t x, uint32_t y, int32_t rx, int32_t ry, uint32_t rw, uint32_t rh)
{
    return (int32_t)x >= rx && (int32_t)y >= ry &&
           (int32_t)x < rx + (int32_t)rw && (int32_t)y < ry + (int32_t)rh;
}

static uint32_t taskbar_y(void)
{
    return fb.height > DESKTOP_TASKBAR_H ? fb.height - DESKTOP_TASKBAR_H : 0;
}

static void clamp_window(struct desktop_window *w)
{
    if (!w || !fb.available) {
        return;
    }
    if (w->width < DESKTOP_MIN_W) {
        w->width = DESKTOP_MIN_W;
    }
    if (w->height < DESKTOP_MIN_H) {
        w->height = DESKTOP_MIN_H;
    }
    if (w->width > fb.width) {
        w->width = fb.width;
    }
    uint32_t max_h = taskbar_y() > 8 ? taskbar_y() - 8 : fb.height;
    if (w->height > max_h) {
        w->height = max_h;
    }
    int32_t max_x = (int32_t)fb.width - (int32_t)w->width;
    int32_t max_y = (int32_t)taskbar_y() - (int32_t)w->height;
    if (max_x < 0) {
        max_x = 0;
    }
    if (max_y < 0) {
        max_y = 0;
    }
    if (w->x < 0) {
        w->x = 0;
    }
    if (w->y < 0) {
        w->y = 0;
    }
    if (w->x > max_x) {
        w->x = max_x;
    }
    if (w->y > max_y) {
        w->y = max_y;
    }
}

static void bring_to_front(uint8_t id)
{
    if (id >= DESKTOP_MAX_WINDOWS) {
        return;
    }
    uint8_t pos = DESKTOP_MAX_WINDOWS;
    for (uint8_t i = 0; i < DESKTOP_MAX_WINDOWS; ++i) {
        if (z_order[i] == id) {
            pos = i;
            break;
        }
    }
    if (pos == DESKTOP_MAX_WINDOWS) {
        return;
    }
    for (uint8_t i = pos; i + 1 < DESKTOP_MAX_WINDOWS; ++i) {
        z_order[i] = z_order[i + 1];
    }
    z_order[DESKTOP_MAX_WINDOWS - 1] = id;
    active_window = id;
}

static void beveled_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t fill, int pressed)
{
    const uint32_t white = 0x00ffffff;
    const uint32_t dark = 0x00808080;
    const uint32_t black = 0x00000000;
    uint32_t tl = pressed ? dark : white;
    uint32_t br = pressed ? white : black;

    framebuffer_rect(x, y, w, h, fill);
    framebuffer_rect(x, y, w, 1, tl);
    framebuffer_rect(x, y, 1, h, tl);
    framebuffer_rect(x + w - 1, y, 1, h, br);
    framebuffer_rect(x, y + h - 1, w, 1, br);
    if (w > 3 && h > 3) {
        framebuffer_rect(x + 1, y + 1, w - 2, 1, pressed ? black : 0x00dfdfdf);
        framebuffer_rect(x + 1, y + 1, 1, h - 2, pressed ? black : 0x00dfdfdf);
        framebuffer_rect(x + w - 2, y + 1, 1, h - 2, pressed ? 0x00dfdfdf : dark);
        framebuffer_rect(x + 1, y + h - 2, w - 2, 1, pressed ? 0x00dfdfdf : dark);
    }
}

static uint32_t text_fit_chars(uint32_t pixel_width)
{
    return pixel_width / LEONOS_FONT_W;
}

static void draw_window_button(uint32_t x, uint32_t y, char label, int pressed)
{
    beveled_rect(x, y, 18, 20, 0x00c0c0c0, pressed);
    char text[2] = {label, 0};
    framebuffer_text_transparent(x + 5 + (pressed ? 1 : 0), y + 2 + (pressed ? 1 : 0), text, 0x00000000);
}

static void draw_window(uint8_t id)
{
    if (id >= DESKTOP_MAX_WINDOWS) {
        return;
    }
    struct desktop_window *w = &windows[id];
    if (!w->visible || w->minimized) {
        return;
    }

    const uint32_t gray = 0x00c0c0c0;
    const uint32_t white = 0x00ffffff;
    const uint32_t black = 0x00000000;
    const uint32_t dark = 0x00808080;
    const uint32_t active = 0x00000080;
    const uint32_t inactive = 0x00808080;
    uint32_t title = (active_window == id) ? active : inactive;

    beveled_rect((uint32_t)w->x, (uint32_t)w->y, w->width, w->height, gray, 0);
    framebuffer_rect((uint32_t)w->x + 4, (uint32_t)w->y + 4, w->width - 8, DESKTOP_TITLEBAR_H, title);

    uint32_t title_space = w->width > 76 ? w->width - 76 : 0;
    uint32_t title_chars = text_fit_chars(title_space);
    char title_buf[48];
    uint32_t pos = 0;
    while (w->title && w->title[pos] && pos + 1 < sizeof(title_buf) && pos < title_chars) {
        title_buf[pos] = w->title[pos];
        ++pos;
    }
    title_buf[pos] = 0;
    framebuffer_text((uint32_t)w->x + 10, (uint32_t)w->y + 9, title_buf, white, title);

    uint32_t bx = (uint32_t)w->x + w->width - 64;
    uint32_t by = (uint32_t)w->y + 6;
    draw_window_button(bx, by, '_', 0);
    draw_window_button(bx + 20, by, w->maximized ? 'r' : 'M', 0);
    draw_window_button(bx + 40, by, 'X', 0);

    uint32_t body_x = (uint32_t)w->x + 8;
    uint32_t body_y = (uint32_t)w->y + DESKTOP_TITLEBAR_H + 10;
    uint32_t body_w = w->width > 16 ? w->width - 16 : 0;
    uint32_t body_h = w->height > DESKTOP_TITLEBAR_H + 18 ? w->height - DESKTOP_TITLEBAR_H - 18 : 0;
    framebuffer_rect(body_x, body_y, body_w, body_h, w->body_color);
    framebuffer_rect(body_x, body_y, body_w, 1, dark);
    framebuffer_rect(body_x, body_y, 1, body_h, dark);
    framebuffer_rect(body_x + body_w - 1, body_y, 1, body_h, white);
    framebuffer_rect(body_x, body_y + body_h - 1, body_w, 1, white);

    if (id == 0) {
        framebuffer_text(body_x + 16, body_y + 18, "desktop.elf window server", black, w->body_color);
        framebuffer_text(body_x + 16, body_y + 42, "Drag title bar. Use buttons.", black, w->body_color);
        framebuffer_text(body_x + 16, body_y + 66, "Start opens closed windows.", black, w->body_color);
    } else if (id == 1) {
        framebuffer_text(body_x + 16, body_y + 18, "0:/", black, w->body_color);
        framebuffer_text(body_x + 16, body_y + 42, "boot  system  userland", black, w->body_color);
        framebuffer_text(body_x + 16, body_y + 66, "FAT32 root drive view", black, w->body_color);
    } else {
        framebuffer_text(body_x + 16, body_y + 18, "Settings", black, w->body_color);
        framebuffer_text(body_x + 16, body_y + 42, "Win98 style controls", black, w->body_color);
        framebuffer_text(body_x + 16, body_y + 66, "GUI state lives in ntclks", black, w->body_color);
    }

    framebuffer_rect((uint32_t)w->x + w->width - 13, (uint32_t)w->y + w->height - 13, 9, 1, dark);
    framebuffer_rect((uint32_t)w->x + w->width - 9, (uint32_t)w->y + w->height - 17, 1, 9, dark);
    framebuffer_rect((uint32_t)w->x + w->width - 10, (uint32_t)w->y + w->height - 10, 6, 1, black);
    framebuffer_rect((uint32_t)w->x + w->width - 6, (uint32_t)w->y + w->height - 14, 1, 6, black);
}

static void draw_taskbar_button(uint8_t id, uint32_t x, uint32_t y)
{
    if (id >= DESKTOP_MAX_WINDOWS || !windows[id].visible) {
        return;
    }
    int active = active_window == id && !windows[id].minimized;
    beveled_rect(x, y, 150, 24, 0x00c0c0c0, active);
    framebuffer_text_transparent(x + 8 + (active ? 1 : 0), y + 4 + (active ? 1 : 0), windows[id].title, 0x00000000);
}

static void draw_start_menu(void)
{
    if (!start_menu_open) {
        return;
    }
    uint32_t y = taskbar_y();
    uint32_t menu_y = y > 126 ? y - 126 : 0;
    beveled_rect(6, menu_y, 210, 126, 0x00c0c0c0, 0);
    framebuffer_rect(10, menu_y + 4, 26, 118, 0x00000080);
    framebuffer_text(44, menu_y + 16, "Desktop Server", 0x00000000, 0x00c0c0c0);
    framebuffer_text(44, menu_y + 42, "File Manager", 0x00000000, 0x00c0c0c0);
    framebuffer_text(44, menu_y + 68, "Settings", 0x00000000, 0x00c0c0c0);
    framebuffer_rect(40, menu_y + 92, 166, 1, 0x00808080);
    framebuffer_text(44, menu_y + 100, "Close Menu", 0x00000000, 0x00c0c0c0);
}

static void desktop_redraw(void)
{
    if (!fb.available || !desktop_ready) {
        return;
    }

    const uint32_t teal = 0x00008080;
    const uint32_t gray = 0x00c0c0c0;
    const uint32_t dark = 0x00808080;
    const uint32_t white = 0x00ffffff;
    const uint32_t black = 0x00000000;

    framebuffer_clear(teal);
    framebuffer_rect(24, 32, 48, 38, gray);
    framebuffer_rect(24, 32, 48, 2, white);
    framebuffer_rect(24, 32, 2, 38, white);
    framebuffer_rect(70, 32, 2, 38, black);
    framebuffer_rect(24, 68, 48, 2, black);
    framebuffer_text(16, 78, "0:/", white, teal);

    framebuffer_rect(24, 112, 48, 38, gray);
    framebuffer_rect(24, 112, 48, 2, white);
    framebuffer_rect(24, 112, 2, 38, white);
    framebuffer_rect(70, 112, 2, 38, black);
    framebuffer_rect(24, 148, 48, 2, black);
    framebuffer_text(8, 158, "Apps", white, teal);

    for (uint8_t i = 0; i < DESKTOP_MAX_WINDOWS; ++i) {
        draw_window(z_order[i]);
    }

    uint32_t tb_y = taskbar_y();
    framebuffer_rect(0, tb_y, fb.width, DESKTOP_TASKBAR_H, gray);
    framebuffer_rect(0, tb_y, fb.width, 2, white);
    framebuffer_rect(0, tb_y + 2, fb.width, 1, dark);
    beveled_rect(6, tb_y + 5, 86, 24, gray, start_menu_open);
    framebuffer_text_transparent(24 + (start_menu_open ? 1 : 0), tb_y + 9 + (start_menu_open ? 1 : 0), "Start", black);

    uint32_t x = 106;
    for (uint8_t i = 0; i < DESKTOP_MAX_WINDOWS; ++i) {
        draw_taskbar_button(i, x, tb_y + 5);
        x += 158;
    }

    draw_start_menu();
    cursor_visible = false;
}

void desktop_boot_paint(void)
{
    if (!fb.available) {
        return;
    }

    windows[0] = (struct desktop_window){
        .x = 120,
        .y = 84,
        .width = 420,
        .height = 220,
        .restore_x = 120,
        .restore_y = 84,
        .restore_width = 420,
        .restore_height = 220,
        .title = "Desktop Server",
        .body_color = 0x00c0c0c0,
        .visible = true,
    };
    windows[1] = (struct desktop_window){
        .x = 190,
        .y = 150,
        .width = 360,
        .height = 190,
        .restore_x = 190,
        .restore_y = 150,
        .restore_width = 360,
        .restore_height = 190,
        .title = "File Manager",
        .body_color = 0x00ffffff,
        .visible = true,
        .minimized = true,
    };
    windows[2] = (struct desktop_window){
        .x = 270,
        .y = 210,
        .width = 320,
        .height = 170,
        .restore_x = 270,
        .restore_y = 210,
        .restore_width = 320,
        .restore_height = 170,
        .title = "Settings",
        .body_color = 0x00dfdfdf,
        .visible = true,
        .minimized = true,
    };
    z_order[0] = 1;
    z_order[1] = 2;
    z_order[2] = 0;
    desktop_ready = true;
    start_menu_open = false;
    active_window = 0;
    drag_window = -1;
    drag_mode = DESKTOP_DRAG_NONE;
    previous_mouse_buttons = 0;
    desktop_redraw();
}

static int32_t hit_window(uint32_t x, uint32_t y)
{
    for (int32_t zi = DESKTOP_MAX_WINDOWS - 1; zi >= 0; --zi) {
        uint8_t id = z_order[zi];
        struct desktop_window *w = &windows[id];
        if (!w->visible || w->minimized) {
            continue;
        }
        if (hit_rect(x, y, w->x, w->y, w->width, w->height)) {
            return id;
        }
    }
    return -1;
}

static void minimize_window(uint8_t id)
{
    if (id >= DESKTOP_MAX_WINDOWS) {
        return;
    }
    windows[id].minimized = true;
    if (active_window == id) {
        active_window = -1;
        for (int32_t zi = DESKTOP_MAX_WINDOWS - 1; zi >= 0; --zi) {
            uint8_t next = z_order[zi];
            if (windows[next].visible && !windows[next].minimized) {
                active_window = next;
                break;
            }
        }
    }
}

static void restore_window(uint8_t id)
{
    if (id >= DESKTOP_MAX_WINDOWS || !windows[id].visible) {
        return;
    }
    windows[id].minimized = false;
    bring_to_front(id);
    clamp_window(&windows[id]);
}

static void toggle_maximize(uint8_t id)
{
    if (id >= DESKTOP_MAX_WINDOWS) {
        return;
    }
    struct desktop_window *w = &windows[id];
    if (w->maximized) {
        w->x = w->restore_x;
        w->y = w->restore_y;
        w->width = w->restore_width;
        w->height = w->restore_height;
        w->maximized = false;
        clamp_window(w);
        return;
    }

    w->restore_x = w->x;
    w->restore_y = w->y;
    w->restore_width = w->width;
    w->restore_height = w->height;
    w->x = 0;
    w->y = 0;
    w->width = fb.width;
    w->height = taskbar_y();
    w->maximized = true;
}

static void close_window(uint8_t id)
{
    if (id >= DESKTOP_MAX_WINDOWS) {
        return;
    }
    windows[id].visible = false;
    windows[id].minimized = false;
    if (active_window == id) {
        active_window = -1;
    }
}

static void handle_start_click(uint32_t x, uint32_t y)
{
    uint32_t tb_y = taskbar_y();
    if (hit_rect(x, y, 6, (int32_t)tb_y + 5, 86, 24)) {
        start_menu_open = !start_menu_open;
        return;
    }

    if (!start_menu_open) {
        return;
    }

    uint32_t menu_y = tb_y > 126 ? tb_y - 126 : 0;
    if (!hit_rect(x, y, 6, (int32_t)menu_y, 210, 126)) {
        start_menu_open = false;
        return;
    }

    if (hit_rect(x, y, 40, (int32_t)menu_y + 8, 166, 24)) {
        windows[0].visible = true;
        restore_window(0);
    } else if (hit_rect(x, y, 40, (int32_t)menu_y + 34, 166, 24)) {
        windows[1].visible = true;
        restore_window(1);
    } else if (hit_rect(x, y, 40, (int32_t)menu_y + 60, 166, 24)) {
        windows[2].visible = true;
        restore_window(2);
    }
    start_menu_open = false;
}

static int handle_taskbar_click(uint32_t x, uint32_t y)
{
    uint32_t tb_y = taskbar_y();
    if (!hit_rect(x, y, 0, (int32_t)tb_y, fb.width, DESKTOP_TASKBAR_H)) {
        return 0;
    }

    handle_start_click(x, y);
    if (hit_rect(x, y, 6, (int32_t)tb_y + 5, 86, 24)) {
        return 1;
    }

    uint32_t bx = 106;
    for (uint8_t i = 0; i < DESKTOP_MAX_WINDOWS; ++i) {
        if (hit_rect(x, y, (int32_t)bx, (int32_t)tb_y + 5, 150, 24) && windows[i].visible) {
            if (active_window == i && !windows[i].minimized) {
                minimize_window(i);
            } else {
                restore_window(i);
            }
            start_menu_open = false;
            return 1;
        }
        bx += 158;
    }
    start_menu_open = false;
    return 1;
}

static void begin_window_drag(uint8_t id, uint32_t x, uint32_t y)
{
    struct desktop_window *w = &windows[id];
    if (w->maximized) {
        return;
    }
    drag_window = id;
    drag_mode = DESKTOP_DRAG_MOVE;
    drag_dx = (int32_t)x - w->x;
    drag_dy = (int32_t)y - w->y;
}

static void begin_window_resize(uint8_t id, uint32_t x, uint32_t y)
{
    struct desktop_window *w = &windows[id];
    if (w->maximized) {
        return;
    }
    drag_window = id;
    drag_mode = DESKTOP_DRAG_RESIZE;
    drag_origin_x = (int32_t)x;
    drag_origin_y = (int32_t)y;
    drag_origin_w = w->width;
    drag_origin_h = w->height;
}

void desktop_handle_mouse(uint32_t x, uint32_t y, uint8_t buttons)
{
    if (!fb.available || !desktop_ready) {
        previous_mouse_buttons = buttons;
        return;
    }

    uint8_t left = buttons & 1;
    uint8_t was_left = previous_mouse_buttons & 1;
    int changed = 0;

    if (left && drag_window >= 0 && drag_window < DESKTOP_MAX_WINDOWS) {
        struct desktop_window *w = &windows[drag_window];
        if (drag_mode == DESKTOP_DRAG_MOVE) {
            w->x = (int32_t)x - drag_dx;
            w->y = (int32_t)y - drag_dy;
            clamp_window(w);
            changed = 1;
        } else if (drag_mode == DESKTOP_DRAG_RESIZE) {
            int32_t dw = (int32_t)x - drag_origin_x;
            int32_t dh = (int32_t)y - drag_origin_y;
            int32_t new_w = (int32_t)drag_origin_w + dw;
            int32_t new_h = (int32_t)drag_origin_h + dh;
            w->width = new_w < DESKTOP_MIN_W ? DESKTOP_MIN_W : (uint32_t)new_w;
            w->height = new_h < DESKTOP_MIN_H ? DESKTOP_MIN_H : (uint32_t)new_h;
            clamp_window(w);
            changed = 1;
        }
    }

    if (!left && was_left) {
        drag_window = -1;
        drag_mode = DESKTOP_DRAG_NONE;
    }

    if (left && !was_left) {
        if (handle_taskbar_click(x, y)) {
            changed = 1;
        } else {
            int32_t id = hit_window(x, y);
            if (id >= 0) {
                struct desktop_window *w = &windows[id];
                bring_to_front((uint8_t)id);
                start_menu_open = false;
                changed = 1;

                uint32_t bx = (uint32_t)w->x + w->width - 64;
                uint32_t by = (uint32_t)w->y + 7;
                if (hit_rect(x, y, (int32_t)bx, (int32_t)by, 18, 16)) {
                    minimize_window((uint8_t)id);
                } else if (hit_rect(x, y, (int32_t)bx + 20, (int32_t)by, 18, 16)) {
                    toggle_maximize((uint8_t)id);
                } else if (hit_rect(x, y, (int32_t)bx + 40, (int32_t)by, 18, 16)) {
                    close_window((uint8_t)id);
                } else if (hit_rect(x, y, w->x + (int32_t)w->width - 18, w->y + (int32_t)w->height - 18, 18, 18)) {
                    begin_window_resize((uint8_t)id, x, y);
                } else if (hit_rect(x, y, w->x + 4, w->y + 4, w->width > 8 ? w->width - 8 : 0, DESKTOP_TITLEBAR_H)) {
                    begin_window_drag((uint8_t)id, x, y);
                }
            } else if (start_menu_open) {
                start_menu_open = false;
                changed = 1;
            }
        }
    }

    previous_mouse_buttons = buttons;
    if (changed) {
        desktop_redraw();
    }
}

void desktop_draw_mouse(uint32_t x, uint32_t y)
{
    if (!fb.available) {
        return;
    }
    if (x + CURSOR_W > fb.width) {
        x = fb.width > CURSOR_W ? fb.width - CURSOR_W : 0;
    }
    if (y + CURSOR_H > fb.height) {
        y = fb.height > CURSOR_H ? fb.height - CURSOR_H : 0;
    }
    if (cursor_visible && cursor_x == x && cursor_y == y) {
        return;
    }

    if (cursor_visible) {
        for (uint32_t yy = 0; yy < CURSOR_H; ++yy) {
            for (uint32_t xx = 0; xx < CURSOR_W; ++xx) {
                framebuffer_put_pixel(cursor_x + xx, cursor_y + yy, cursor_bg[yy * CURSOR_W + xx]);
            }
        }
    }

    for (uint32_t yy = 0; yy < CURSOR_H; ++yy) {
        for (uint32_t xx = 0; xx < CURSOR_W; ++xx) {
            cursor_bg[yy * CURSOR_W + xx] = framebuffer_get_pixel(x + xx, y + yy);
        }
    }

    for (uint32_t yy = 0; yy < CURSOR_H; ++yy) {
        for (uint32_t xx = 0; xx < CURSOR_W; ++xx) {
            char cell = cursor_art[yy][xx];
            if (cell == 'X') {
                framebuffer_put_pixel(x + xx, y + yy, 0x00000000);
            } else if (cell == 'O') {
                framebuffer_put_pixel(x + xx, y + yy, 0x00ffffff);
            }
        }
    }
    cursor_x = x;
    cursor_y = y;
    cursor_visible = true;
}
