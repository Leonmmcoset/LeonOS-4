#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define MS_COLS 9
#define MS_ROWS 9
#define MS_MINES 10
#define MS_CELLS (MS_COLS * MS_ROWS)
#define MS_TILE 28
#define MS_GAP 2
#define MS_BOARD_X 18
#define MS_BOARD_Y 76
#define MS_TOP_H 52
#define MS_W (MS_BOARD_X * 2 + MS_COLS * MS_TILE)
#define MS_H (MS_BOARD_Y + MS_ROWS * MS_TILE + 18)
#define MS_SPRITE_SIZE 20
#define MS_SPRITE_BMP_MAX_BYTES (MS_SPRITE_SIZE * MS_SPRITE_SIZE * 4U + 128U)
#define MS_MINE_SPRITE_PATH "/system/resources/minesweeper-mine.bmp"
#define MS_FLAG_SPRITE_PATH "/system/resources/minesweeper-flag.bmp"

#define CELL_MINE 0x01u
#define CELL_REVEALED 0x02u
#define CELL_FLAGGED 0x04u
#define T(en, zh) leonos_i18n((en), (zh))

static uint32_t pixels[MS_W * MS_H];
static uint8_t cells[MS_ROWS][MS_COLS];
static uint8_t adjacent[MS_ROWS][MS_COLS];
static uint8_t mines_placed;
static uint8_t game_over;
static uint8_t won;
static uint32_t revealed_count;
static uint32_t flagged_count;
static uint32_t rng_state;

struct minesweeper_sprite {
    uint32_t width;
    uint32_t height;
    uint32_t pixels[MS_SPRITE_SIZE * MS_SPRITE_SIZE];
};

static struct minesweeper_sprite mine_sprite;
static struct minesweeper_sprite flag_sprite;

static int hit_rect_i(int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rw, int32_t rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
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

static uint32_t text_len(const char *text)
{
    uint32_t n = 0;
    while (text && text[n]) {
        ++n;
    }
    return n;
}

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
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

static int load_sprite_bmp(const char *path, struct minesweeper_sprite *sprite)
{
    uint8_t bmp[MS_SPRITE_BMP_MAX_BYTES];
    struct leonos_stat st;
    uint32_t len = 0;
    uint32_t pixel_offset;
    uint32_t row_stride;
    uint32_t height;
    int32_t width;
    int32_t height_signed;
    int top_down;
    int fd;

    if (!path || !sprite || leonos_stat_legacy(path, &st) < 0 ||
        st.type != LEONOS_FS_TYPE_FILE || st.size < 54 ||
        st.size > sizeof(bmp)) {
        return 0;
    }
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return 0;
    }
    while (len < st.size) {
        long got = read(fd, bmp + len, (uint32_t)st.size - len);
        if (got <= 0) {
            close(fd);
            return 0;
        }
        len += (uint32_t)got;
    }
    close(fd);
    if (bmp[0] != 'B' || bmp[1] != 'M' || read_le32(bmp + 14) < 40 ||
        read_le16(bmp + 26) != 1 || read_le32(bmp + 30) != 0 ||
        read_le16(bmp + 28) != 32) {
        return 0;
    }
    pixel_offset = read_le32(bmp + 10);
    width = read_le32s(bmp + 18);
    height_signed = read_le32s(bmp + 22);
    if (width <= 0 || height_signed == 0 || (uint32_t)width > MS_SPRITE_SIZE) {
        return 0;
    }
    top_down = height_signed < 0;
    height = top_down ? (uint32_t)(-height_signed) : (uint32_t)height_signed;
    if (height > MS_SPRITE_SIZE || pixel_offset >= len) {
        return 0;
    }
    row_stride = (uint32_t)width * 4U;
    if (height > (len - pixel_offset) / row_stride) {
        return 0;
    }
    for (uint32_t y = 0; y < MS_SPRITE_SIZE; ++y) {
        for (uint32_t x = 0; x < MS_SPRITE_SIZE; ++x) {
            sprite->pixels[y * MS_SPRITE_SIZE + x] = 0;
        }
    }
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t src_y = top_down ? y : height - 1U - y;
        const uint8_t *row = bmp + pixel_offset + src_y * row_stride;
        for (uint32_t x = 0; x < (uint32_t)width; ++x) {
            const uint8_t *pixel = row + x * 4U;
            sprite->pixels[y * MS_SPRITE_SIZE + x] =
                ((uint32_t)pixel[3] << 24) | ((uint32_t)pixel[2] << 16) |
                ((uint32_t)pixel[1] << 8) | pixel[0];
        }
    }
    sprite->width = (uint32_t)width;
    sprite->height = height;
    return 1;
}

static int load_game_assets(void)
{
    return load_sprite_bmp(MS_MINE_SPRITE_PATH, &mine_sprite) &&
           load_sprite_bmp(MS_FLAG_SPRITE_PATH, &flag_sprite);
}

static uint32_t color_for_number(uint8_t n)
{
    switch (n) {
    case 1: return 0x000000bf;
    case 2: return 0x00008000;
    case 3: return 0x00bf0000;
    case 4: return 0x00000080;
    case 5: return 0x00800000;
    case 6: return 0x00008080;
    case 7: return 0x00000000;
    default: return 0x00808080;
    }
}

static void draw_center_text(struct leonos_ui_surface *ui, uint32_t x, uint32_t y,
                             uint32_t w, uint32_t h, const char *text,
                             uint32_t fg, uint32_t bg)
{
    uint32_t tw = leonos_ui_text_width(text);
    uint32_t tx = x + (w > tw ? (w - tw) / 2 : 0);
    uint32_t ty = y + (h > LEONOS_FONT_H ? (h - LEONOS_FONT_H) / 2 : 0);
    leonos_ui_text(ui, tx, ty, text, fg, bg);
}

static uint32_t rng_next(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

static void reset_game(void)
{
    for (uint32_t y = 0; y < MS_ROWS; ++y) {
        for (uint32_t x = 0; x < MS_COLS; ++x) {
            cells[y][x] = 0;
            adjacent[y][x] = 0;
        }
    }
    mines_placed = 0;
    game_over = 0;
    won = 0;
    revealed_count = 0;
    flagged_count = 0;
    rng_state = (uint32_t)leonos_uptime_ms() ^ 0xa5c35a1du;
    if (!rng_state) {
        rng_state = 1;
    }
}

static int in_board(int x, int y)
{
    return x >= 0 && y >= 0 && x < MS_COLS && y < MS_ROWS;
}

static void add_mine_adjacency(int mine_x, int mine_y)
{
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            int x = mine_x + dx;
            int y = mine_y + dy;
            if ((dx || dy) && in_board(x, y)) {
                ++adjacent[y][x];
            }
        }
    }
}

static void place_mines(int safe_x, int safe_y)
{
    uint8_t candidates[MS_CELLS];
    uint32_t candidate_count = 0;
    for (int y = 0; y < MS_ROWS; ++y) {
        for (int x = 0; x < MS_COLS; ++x) {
            if (x != safe_x || y != safe_y) {
                candidates[candidate_count++] = (uint8_t)(y * MS_COLS + x);
            }
        }
    }
    for (uint32_t placed = 0; placed < MS_MINES; ++placed) {
        uint32_t selected = placed + rng_next() % (candidate_count - placed);
        uint8_t index = candidates[selected];
        int x = (int)(index % MS_COLS);
        int y = (int)(index / MS_COLS);
        candidates[selected] = candidates[placed];
        candidates[placed] = index;
        cells[y][x] |= CELL_MINE;
        add_mine_adjacency(x, y);
    }
    mines_placed = 1;
}

static void reveal_all_mines(void)
{
    for (uint32_t y = 0; y < MS_ROWS; ++y) {
        for (uint32_t x = 0; x < MS_COLS; ++x) {
            if (cells[y][x] & CELL_MINE) {
                cells[y][x] |= CELL_REVEALED;
            }
        }
    }
}

static void reveal_cell(int x, int y)
{
    uint8_t queue[MS_CELLS];
    uint32_t head = 0;
    uint32_t tail = 0;
    if (!in_board(x, y) ||
        (cells[y][x] & (CELL_REVEALED | CELL_FLAGGED)) ||
        game_over) {
        return;
    }
    cells[y][x] |= CELL_REVEALED;
    ++revealed_count;
    if (cells[y][x] & CELL_MINE) {
        game_over = 1;
        won = 0;
        reveal_all_mines();
        return;
    }
    queue[tail++] = (uint8_t)(y * MS_COLS + x);
    while (head < tail) {
        uint8_t index = queue[head++];
        int current_x = (int)(index % MS_COLS);
        int current_y = (int)(index / MS_COLS);
        if (adjacent[current_y][current_x] != 0) {
            continue;
        }
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                int next_x = current_x + dx;
                int next_y = current_y + dy;
                if ((dx || dy) && in_board(next_x, next_y) &&
                    !(cells[next_y][next_x] &
                      (CELL_MINE | CELL_REVEALED | CELL_FLAGGED))) {
                    cells[next_y][next_x] |= CELL_REVEALED;
                    ++revealed_count;
                    queue[tail++] = (uint8_t)(next_y * MS_COLS + next_x);
                }
            }
        }
    }
    if (revealed_count >= MS_COLS * MS_ROWS - MS_MINES) {
        game_over = 1;
        won = 1;
        for (uint32_t yy = 0; yy < MS_ROWS; ++yy) {
            for (uint32_t xx = 0; xx < MS_COLS; ++xx) {
                if (cells[yy][xx] & CELL_MINE) {
                    cells[yy][xx] |= CELL_FLAGGED;
                }
            }
        }
        flagged_count = MS_MINES;
    }
}

static void toggle_flag(int x, int y)
{
    if (!in_board(x, y) || game_over || (cells[y][x] & CELL_REVEALED)) {
        return;
    }
    if (cells[y][x] & CELL_FLAGGED) {
        cells[y][x] &= (uint8_t)~CELL_FLAGGED;
        if (flagged_count) {
            --flagged_count;
        }
    } else if (flagged_count < MS_MINES) {
        cells[y][x] |= CELL_FLAGGED;
        ++flagged_count;
    }
}

static void draw_sprite(struct leonos_ui_surface *ui,
                        const struct minesweeper_sprite *sprite,
                        uint32_t x, uint32_t y)
{
    for (uint32_t yy = 0; yy < sprite->height; ++yy) {
        for (uint32_t xx = 0; xx < sprite->width; ++xx) {
            uint32_t argb = sprite->pixels[yy * MS_SPRITE_SIZE + xx];
            if (argb >> 24) {
                leonos_ui_pixel(ui, x + xx, y + yy, argb & 0x00ffffffu);
            }
        }
    }
}

static void draw_tile(struct leonos_ui_surface *ui, uint32_t gx, uint32_t gy)
{
    uint32_t x = MS_BOARD_X + gx * MS_TILE;
    uint32_t y = MS_BOARD_Y + gy * MS_TILE;
    uint8_t cell = cells[gy][gx];
    uint32_t inner = MS_TILE > MS_GAP ? MS_TILE - MS_GAP : MS_TILE;
    if (cell & CELL_REVEALED) {
        uint32_t fill = (cell & CELL_MINE) ? 0x00e8b0b0 : 0x00d8d8d8;
        leonos_ui_inset(ui, x, y, inner, inner, fill);
        if (cell & CELL_MINE) {
            draw_sprite(ui, &mine_sprite, x + (inner - mine_sprite.width) / 2U,
                        y + (inner - mine_sprite.height) / 2U);
        } else if (adjacent[gy][gx]) {
            char text[2] = {(char)('0' + adjacent[gy][gx]), 0};
            draw_center_text(ui, x, y, inner, inner, text,
                             color_for_number(adjacent[gy][gx]), fill);
        }
    } else {
        leonos_ui_bevel(ui, x, y, inner, inner, LEONOS_UI_GRAY, 0);
        if (cell & CELL_FLAGGED) {
            draw_sprite(ui, &flag_sprite, x + (inner - flag_sprite.width) / 2U,
                        y + (inner - flag_sprite.height) / 2U);
        }
    }
}

static void draw_game(struct leonos_ui_surface *ui)
{
    char mines_text[32];
    const char *status = T("Ready", "准备");
    int mines_left = (int)MS_MINES - (int)flagged_count;
    leonos_ui_rect(ui, 0, 0, MS_W, MS_H, LEONOS_UI_LIGHT);
    leonos_ui_text(ui, 18, 16, T("Minesweeper", "扫雷"), LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
    leonos_ui_button(ui, MS_W - 88, 12, 70, LEONOS_UI_BUTTON_H, T("New", "新游戏"), 0);
    if (game_over) {
        status = won ? T("You won", "胜利") : T("Boom", "爆炸");
    } else if (mines_placed) {
        status = T("Playing", "游戏中");
    }
    copy_text(mines_text, sizeof(mines_text), T("Mines: ", "地雷: "));
    if (mines_left < 0) {
        uint32_t p = text_len(mines_text);
        mines_text[p] = '-';
        mines_left = -mines_left;
        mines_text[p + 1] = (char)('0' + (mines_left / 10) % 10);
        mines_text[p + 2] = (char)('0' + mines_left % 10);
        mines_text[p + 3] = 0;
    } else {
        uint32_t p = text_len(mines_text);
        mines_text[p] = (char)('0' + (mines_left / 10) % 10);
        mines_text[p + 1] = (char)('0' + mines_left % 10);
        mines_text[p + 2] = 0;
    }
    leonos_ui_text(ui, 18, 42, mines_text, LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
    leonos_ui_text(ui, 130, 42, status, LEONOS_UI_DARK, LEONOS_UI_LIGHT);
    leonos_ui_inset(ui, MS_BOARD_X - 4, MS_BOARD_Y - 4,
                    MS_COLS * MS_TILE + 6, MS_ROWS * MS_TILE + 6,
                    LEONOS_UI_GRAY);
    for (uint32_t y = 0; y < MS_ROWS; ++y) {
        for (uint32_t x = 0; x < MS_COLS; ++x) {
            draw_tile(ui, x, y);
        }
    }
}

static int board_pos(int32_t px, int32_t py, int *out_x, int *out_y)
{
    if (px < MS_BOARD_X || py < MS_BOARD_Y ||
        px >= MS_BOARD_X + MS_COLS * MS_TILE ||
        py >= MS_BOARD_Y + MS_ROWS * MS_TILE) {
        return 0;
    }
    *out_x = (px - MS_BOARD_X) / MS_TILE;
    *out_y = (py - MS_BOARD_Y) / MS_TILE;
    return in_board(*out_x, *out_y);
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    puts("[minesweeper.elf] starting");
    if (!load_game_assets()) {
        puts("[minesweeper.elf] required BMP assets unavailable");
        return 1;
    }
    window_id = leonos_gui_create_app_window_ex(T("Minesweeper", "扫雷"), T("LeonOS Minesweeper", "LeonOS 扫雷"),
                                                MS_W, MS_H, LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        printf("[minesweeper.elf] create window failed=%d\n", window_id);
        return 1;
    }
    leonos_ui_bind(&ui, pixels, MS_W, MS_H, MS_W);
    reset_game();
    draw_game(&ui);
    leonos_gui_present_window((uint32_t)window_id, MS_W, MS_H, MS_W, pixels);
    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_wait_app_event(&event, LEONOS_GUI_IDLE_WAIT_MS) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                break;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 3u)) {
                int gx = 0;
                int gy = 0;
                if ((event.buttons & 1u) &&
                    hit_rect_i(event.x, event.y, MS_W - 88, 12, 70,
                               (int32_t)LEONOS_UI_BUTTON_H)) {
                    reset_game();
                } else if (board_pos(event.x, event.y, &gx, &gy)) {
                    if (event.buttons & 2u) {
                        toggle_flag(gx, gy);
                    } else if (event.buttons & 1u) {
                        if (!mines_placed) {
                            place_mines(gx, gy);
                        }
                        reveal_cell(gx, gy);
                    }
                }
                draw_game(&ui);
                leonos_gui_present_window((uint32_t)window_id, MS_W, MS_H, MS_W, pixels);
            } else if (event.type == LEONOS_GUI_APP_EVENT_RESIZE ||
                       event.type == LEONOS_GUI_APP_EVENT_FOCUS) {
                draw_game(&ui);
                leonos_gui_present_window((uint32_t)window_id, MS_W, MS_H, MS_W, pixels);
            }
        } else {
            sleep_ms(10);
        }
    }
    leonos_gui_destroy_app_window((uint32_t)window_id);
    return 0;
}
