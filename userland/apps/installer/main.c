#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/system.h>
#include <leonos/ui.h>

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
#define KEY_UP 72U
#define KEY_DOWN 80U
#define COPY_BUF_SIZE (256U * 1024U)

enum installer_page {
    PAGE_WELCOME = 0,
    PAGE_DISK,
    PAGE_CONFIRM,
    PAGE_PROGRESS,
    PAGE_FINISH,
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

static uint32_t pixels[INSTALLER_MAX_W * INSTALLER_MAX_H];
static uint32_t surface_w = INSTALLER_INITIAL_W;
static uint32_t surface_h = INSTALLER_INITIAL_H;
static uint8_t page = PAGE_WELCOME;
static struct leonos_install_disk disks[LEONOS_INSTALL_MAX_DISKS];
static uint32_t disk_count;
static int32_t selected_disk = -1;
static char confirm_text[16];
static struct leonos_ui_edit_state confirm_edit;
static char status_text[128] = "Ready";
static char detail_text[128] = "";
static uint32_t progress_value;
static uint32_t copy_total;
static uint32_t copy_done;
static uint64_t copy_total_bytes;
static uint64_t copy_done_bytes;
static uint8_t install_success;
static uint8_t install_running;
static uint8_t dirty = 1;
static uint8_t copy_buf[COPY_BUF_SIZE];

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

static void set_status(const char *status, const char *detail)
{
    copy_text(status_text, sizeof(status_text), status);
    copy_text(detail_text, sizeof(detail_text), detail);
}

static void set_error_status(const char *prefix, int ret)
{
    uint32_t pos = 0;
    detail_text[0] = 0;
    append_text(detail_text, &pos, sizeof(detail_text), prefix);
    append_text(detail_text, &pos, sizeof(detail_text), " ret=");
    append_i32(detail_text, &pos, sizeof(detail_text), ret);
    copy_text(status_text, sizeof(status_text), "Installation failed");
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
    append_text(buf, &pos, cap, "  AHCI port ");
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
    return text_eq(confirm_text, "INSTALL");
}

static void refresh_disks(void)
{
    uint32_t count = 0;
    int ret = leonos_install_list_disks(disks, LEONOS_INSTALL_MAX_DISKS, &count);
    if (ret < 0) {
        disk_count = 0;
        selected_disk = -1;
        set_error_status("Could not list SATA disks", ret);
        dirty = 1;
        return;
    }
    disk_count = count;
    if (disk_count == 0) {
        selected_disk = -1;
        set_status("No SATA/AHCI disks were found", "Attach a SATA disk and click Refresh.");
    } else {
        if (selected_disk < 0 || (uint32_t)selected_disk >= disk_count) {
            selected_disk = 0;
        }
        set_status("Select the target SATA/AHCI disk", "The selected disk will be erased.");
    }
    dirty = 1;
}

static void draw_sidebar(struct leonos_ui_surface *ui)
{
    static const char *steps[] = {
        "Welcome",
        "Disk",
        "Confirm",
        "Install",
        "Finish",
    };
    struct installer_layout l = get_layout();
    if (!l.sidebar_w) {
        return;
    }
    leonos_ui_rect(ui, 0, 0, l.sidebar_w, surface_h, LEONOS_UI_ACTIVE_TITLE);
    leonos_ui_text(ui, 18, 24, "LeonOS 4", LEONOS_UI_WHITE, LEONOS_UI_ACTIVE_TITLE);
    leonos_ui_text(ui, 18, 48, "Setup", LEONOS_UI_WHITE, LEONOS_UI_ACTIVE_TITLE);
    for (uint32_t i = 0; i < 5; ++i) {
        uint32_t y = 104 + i * 34;
        uint32_t fg = i == page ? LEONOS_UI_BLACK : LEONOS_UI_WHITE;
        uint32_t bg = i == page ? LEONOS_UI_LIGHT : LEONOS_UI_ACTIVE_TITLE;
        if (i == page) {
            leonos_ui_rect(ui, 12, y - 6, l.sidebar_w > 34 ? l.sidebar_w - 34 : l.sidebar_w, 24, bg);
        }
        leonos_ui_text(ui, 20, y, steps[i], fg, bg);
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
        return "Install";
    }
    if (page == PAGE_FINISH && install_success) {
        return "Restart";
    }
    if (page == PAGE_FINISH) {
        return "Close";
    }
    return "Next";
}

static void draw_footer(struct leonos_ui_surface *ui)
{
    struct installer_layout l = get_layout();
    uint32_t back_disabled = page == PAGE_WELCOME || page == PAGE_PROGRESS ||
                             (page == PAGE_FINISH && install_success);
    uint32_t cancel_disabled = page == PAGE_PROGRESS ||
                               (page == PAGE_FINISH && install_success);
    leonos_ui_rect(ui, l.sidebar_w, l.footer_y, surface_w > l.sidebar_w ? surface_w - l.sidebar_w : surface_w, 1, LEONOS_UI_DARK);
    leonos_ui_rect(ui, l.sidebar_w, l.footer_y + 1, surface_w > l.sidebar_w ? surface_w - l.sidebar_w : surface_w, surface_h > l.footer_y + 1 ? surface_h - l.footer_y - 1 : 0, LEONOS_UI_GRAY);
    leonos_ui_button(ui, l.back_x, l.button_y, BUTTON_W, BUTTON_H, "Back",
                     back_disabled ? LEONOS_UI_BUTTON_DISABLED : 0);
    leonos_ui_button(ui, l.next_x, l.button_y, BUTTON_W, BUTTON_H, primary_label(),
                     primary_disabled() ? LEONOS_UI_BUTTON_DISABLED : 0);
    leonos_ui_button(ui, l.cancel_x, l.button_y, BUTTON_W, BUTTON_H, "Cancel",
                     cancel_disabled ? LEONOS_UI_BUTTON_DISABLED : 0);
}

static void draw_welcome(struct leonos_ui_surface *ui)
{
    struct installer_layout l = get_layout();
    draw_title(ui, "Install LeonOS 4", "This wizard will install LeonOS onto a SATA disk.");
    leonos_ui_text(ui, l.content_x, l.content_y + 84, "The installer will copy the full normal system payload", LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, l.content_x, l.content_y + 108, "from the installation media to the selected disk.", LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, l.content_x, l.content_y + 164, "Only SATA/AHCI target disks are supported by this build.", LEONOS_UI_DARK, LEONOS_UI_WHITE);
}

static void draw_disk_page(struct leonos_ui_surface *ui)
{
    struct installer_layout l = get_layout();
    char line[128];
    draw_title(ui, "Select Installation Disk", "Choose the SATA/AHCI disk that will receive LeonOS.");
    leonos_ui_button(ui, l.disk_refresh_x, l.disk_refresh_y, 92, BUTTON_H, "Refresh", 0);
    leonos_ui_list_header(ui, l.content_x, l.disk_header_y, l.table_w, "Available SATA/AHCI disks");
    leonos_ui_inset(ui, l.content_x, l.disk_list_y, l.table_w, l.disk_list_h, LEONOS_UI_WHITE);
    if (disk_count == 0) {
        leonos_ui_text(ui, l.content_x + 12, l.disk_list_y + 20, "No SATA/AHCI disks were found.", LEONOS_UI_DARK, LEONOS_UI_WHITE);
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

static void draw_confirm_page(struct leonos_ui_surface *ui)
{
    struct installer_layout l = get_layout();
    char line[128];
    draw_title(ui, "Confirm Installation", "This operation is destructive.");
    if (selected_disk >= 0 && (uint32_t)selected_disk < disk_count) {
        format_disk_line(line, sizeof(line), &disks[selected_disk]);
        leonos_ui_text(ui, l.content_x, l.content_y + 78, "Target:", LEONOS_UI_BLACK, LEONOS_UI_WHITE);
        leonos_ui_text_clipped(ui, l.content_x + 70, l.content_y + 78,
                               l.content_w > 70 ? l.content_w - 70 : l.content_w,
                               line, LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    }
    leonos_ui_text_clipped(ui, l.content_x, l.content_y + 130, l.content_w,
                           "The selected disk will be erased and formatted as one FAT32 ESP.",
                           LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, l.content_x, l.content_y + 174, "Type INSTALL to enable the Install button.", LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_edit_state_draw(ui, l.content_x, l.confirm_edit_y, 220, &confirm_edit, 0);
}

static void draw_progress_page(struct leonos_ui_surface *ui)
{
    struct installer_layout l = get_layout();
    draw_title(ui, "Installing LeonOS 4", "Do not turn off this machine.");
    leonos_ui_text(ui, l.content_x, l.content_y + 94, status_text, LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_progress(ui, l.content_x, l.content_y + 130, l.content_w, 24, progress_value, 100);
    leonos_ui_text_clipped(ui, l.content_x, l.content_y + 174, l.content_w, detail_text, LEONOS_UI_DARK, LEONOS_UI_WHITE);
}

static void draw_finish_page(struct leonos_ui_surface *ui)
{
    struct installer_layout l = get_layout();
    if (install_success) {
        draw_title(ui, "Installation Complete", "LeonOS was installed to the selected disk.");
        leonos_ui_text(ui, l.content_x, l.content_y + 96, "Remove the installation media, then restart.", LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    } else {
        draw_title(ui, "Installation Failed", "No writes will continue after this error.");
        leonos_ui_text(ui, l.content_x, l.content_y + 96, status_text, LEONOS_UI_BLACK, LEONOS_UI_WHITE);
        leonos_ui_text_clipped(ui, l.content_x, l.content_y + 130, l.content_w, detail_text, LEONOS_UI_DARK, LEONOS_UI_WHITE);
    }
}

static void draw_installer(struct leonos_ui_surface *ui)
{
    leonos_ui_rect(ui, 0, 0, surface_w, surface_h, LEONOS_UI_WHITE);
    draw_sidebar(ui);
    switch (page) {
    case PAGE_WELCOME:
        draw_welcome(ui);
        break;
    case PAGE_DISK:
        draw_disk_page(ui);
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
                  "Copying system files", detail);
}

static int count_files_recursive(const char *src, uint32_t *out_count)
{
    struct leonos_dir_entry entries[LEONOS_FS_MAX_ENTRIES];
    uint32_t count = 0;
    int ret = leonos_list_dir(src, entries, LEONOS_FS_MAX_ENTRIES, &count);
    if (ret < 0) {
        return ret;
    }
    for (uint32_t i = 0; i < count; ++i) {
        char child[LEONOS_FS_PATH_LEN];
        if (name_is_dot(entries[i].name)) {
            continue;
        }
        if (path_join(child, sizeof(child), src, entries[i].name) < 0) {
            set_status("Installation failed", "Payload path is too long");
            return -1;
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
                return ret;
            }
        }
    }
    return 0;
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
    printf("[installer.elf] copying %s -> %s\n", src, dst);
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
    struct leonos_dir_entry entries[LEONOS_FS_MAX_ENTRIES];
    uint32_t count = 0;
    int ret = leonos_list_dir(src, entries, LEONOS_FS_MAX_ENTRIES, &count);
    if (ret < 0) {
        return ret;
    }
    for (uint32_t i = 0; i < count; ++i) {
        char src_child[LEONOS_FS_PATH_LEN];
        char dst_child[LEONOS_FS_PATH_LEN];
        if (name_is_dot(entries[i].name)) {
            continue;
        }
        if (path_join(src_child, sizeof(src_child), src, entries[i].name) < 0 ||
            path_join(dst_child, sizeof(dst_child), dst, entries[i].name) < 0) {
            set_status("Installation failed", "Copy path is too long");
            return -1;
        }
        if (entries[i].type == LEONOS_FS_TYPE_DIR) {
            ret = mkdir(dst_child, 0);
            if (ret < 0) {
                printf("[installer.elf] mkdir %s ret=%d\n", dst_child, ret);
                return ret;
            }
            ret = copy_dir_recursive(src_child, dst_child, window_id, ui);
            if (ret < 0) {
                return ret;
            }
        } else if (entries[i].type == LEONOS_FS_TYPE_FILE) {
            ret = copy_file_path(src_child, dst_child, window_id, ui);
            if (ret < 0) {
                printf("[installer.elf] copy %s -> %s ret=%d\n", src_child, dst_child, ret);
                return ret;
            }
            ++copy_done;
            show_copy_progress(window_id, ui, dst_child);
        }
    }
    return 0;
}

static int copy_payload_child(const char *name, int window_id,
                              struct leonos_ui_surface *ui)
{
    char src[LEONOS_FS_PATH_LEN];
    char dst[LEONOS_FS_PATH_LEN];
    if (path_join(src, sizeof(src), "0:/install/esp", name) < 0 ||
        path_join(dst, sizeof(dst), "1:/", name) < 0) {
        set_status("Installation failed", "Payload path is too long");
        return -1;
    }
    int ret = mkdir(dst, 0);
    if (ret < 0 && ret != -17) {
        printf("[installer.elf] mkdir %s ret=%d\n", dst, ret);
        return ret;
    }
    return copy_dir_recursive(src, dst, window_id, ui);
}

static int copy_payload_ordered(int window_id, struct leonos_ui_surface *ui)
{
    static const char *const early_dirs[] = {
        "etc",
        "system",
        "userland",
        "boot",
    };
    for (uint32_t i = 0; i < sizeof(early_dirs) / sizeof(early_dirs[0]); ++i) {
        int ret = copy_payload_child(early_dirs[i], window_id, ui);
        if (ret < 0) {
            return ret;
        }
    }
    return copy_payload_child("efi", window_id, ui);
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
        printf("[installer.elf] installation completed successfully\n");
        install_success = 1;
        progress_value = 100;
        set_status("Installation completed successfully", "Press Restart to boot from the installed disk.");
    }
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
    ret = leonos_install_format_esp(disk_id);
    if (ret < 0) {
        finish_install(window_id, ui, ret, "Format failed");
        return;
    }

    show_progress(window_id, ui, 22, "Mounting target ESP", "Mount point: 1:/");
    ret = leonos_install_mount_target(disk_id);
    if (ret < 0) {
        finish_install(window_id, ui, ret, "Mount failed");
        return;
    }

    show_progress(window_id, ui, 30, "Scanning installation payload", "Source: 0:/install/esp");
    ret = count_files_recursive("0:/install/esp", &copy_total);
    if (ret < 0) {
        finish_install(window_id, ui, ret, "Payload scan failed");
        return;
    }

    show_progress(window_id, ui, 35, "Copying system files", "Destination: 1:/");
    ret = copy_payload_ordered(window_id, ui);
    if (ret < 0) {
        finish_install(window_id, ui, ret, "Copy failed");
        return;
    }

    show_progress(window_id, ui, 100, "Installation completed successfully", "Target disk is ready.");
    finish_install(window_id, ui, 0, "");
}

static void go_back(void)
{
    if (page == PAGE_DISK) {
        page = PAGE_WELCOME;
    } else if (page == PAGE_CONFIRM) {
        page = PAGE_DISK;
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
    if (page == PAGE_WELCOME) {
        page = PAGE_DISK;
        refresh_disks();
        dirty = 1;
        return 0;
    }
    if (page == PAGE_DISK) {
        page = PAGE_CONFIRM;
        reset_confirm();
        dirty = 1;
        return 0;
    }
    if (page == PAGE_CONFIRM) {
        perform_install(window_id, ui);
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
            set_status("Select the target SATA/AHCI disk", "The selected disk will be erased.");
            dirty = 1;
        }
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
    if (!install_running &&
        hit_rect_i(event->x, event->y, (int32_t)l.back_x, (int32_t)l.button_y, BUTTON_W, BUTTON_H)) {
        if (!(page == PAGE_WELCOME || page == PAGE_PROGRESS ||
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
    int window_id;

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
    refresh_disks();
    page = PAGE_WELCOME;
    present_installer(window_id, &ui);

    for (;;) {
        event.window_id = (uint32_t)window_id;
        while (leonos_gui_poll_app_event(&event) > 0) {
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
