#include "browser.h"

uint32_t button_y(void)
{
    if (browser_embedded) {
        return 3U;
    }
    return BROWSER_MENU_H + 3U;
}

uint32_t address_y(void)
{
    return BROWSER_MENU_H + BROWSER_TOOLBAR_H + 5U;
}

uint32_t address_w(void)
{
    uint32_t x = 74U;
    uint32_t go_w = BROWSER_GO_W;
    if (view_w <= x + go_w + 20U) {
        return 120U;
    }
    return view_w - x - go_w - 20U;
}

uint32_t go_x(void)
{
    return 74U + address_w() + 8U;
}

uint32_t toolbar_forward_x(void)
{
    return BROWSER_BACK_X + BROWSER_BACK_W + BROWSER_NAV_GAP;
}

uint32_t toolbar_refresh_x(void)
{
    return toolbar_forward_x() + BROWSER_FORWARD_W + BROWSER_NAV_GAP;
}

uint32_t toolbar_home_x(void)
{
    return toolbar_refresh_x() + BROWSER_REFRESH_W + BROWSER_NAV_GAP;
}

uint32_t toolbar_stop_x(void)
{
    return toolbar_home_x() + BROWSER_HOME_W + BROWSER_NAV_GAP;
}

uint32_t toolbar_title_x(void)
{
    return toolbar_stop_x() + BROWSER_STOP_W + 12U;
}

uint32_t menu_row_y(uint32_t row)
{
    return BROWSER_MENU_H + 8U + row * BROWSER_MENU_ROW_STEP;
}

int hit_rect_i(int32_t px, int32_t py, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h)
{
    return px >= (int32_t)x && py >= (int32_t)y &&
           px < (int32_t)(x + w) && py < (int32_t)(y + h);
}

void draw_toolbar_button(uint32_t x, uint32_t w, const char *label,
                                uint32_t disabled)
{
    leonos_ui_toolbar_button(&ui, x, button_y(), w, label,
                             disabled ? LEONOS_UI_TOOLBAR_BUTTON_DISABLED : 0);
}

void draw_line_run(uint32_t x, uint32_t y, const char *text,
                          uint32_t len, uint32_t fg, uint32_t bg,
                          uint8_t underline, uint8_t bold,
                          uint8_t italic, uint8_t code,
                          uint32_t cell_w, uint32_t cell_h,
                          uint32_t cell_count)
{
    char tmp[BROWSER_LINE_CHARS];
    uint32_t copy_len = len;
    uint32_t clip_px;
    if (!cell_w) {
        cell_w = LEONOS_FONT_W;
    }
    if (!cell_h) {
        cell_h = LEONOS_FONT_H;
    }
    if (!cell_count) {
        cell_count = copy_len;
    }
    if (copy_len >= sizeof(tmp)) {
        copy_len = sizeof(tmp) - 1U;
    }
    for (uint32_t i = 0; i < copy_len; ++i) {
        tmp[i] = text[i];
    }
    tmp[copy_len] = 0;
    clip_px = cell_count * cell_w;
    {
        uint32_t ttf_px = leonos_ui_text_width(tmp);
        uint32_t scaled = (ttf_px * cell_w + LEONOS_FONT_W / 2U) / LEONOS_FONT_W;
        if (scaled > clip_px) {
            clip_px = scaled;
        }
    }
    if (code && copy_len) {
        if (bg == LEONOS_UI_WHITE) {
            bg = BROWSER_CODE_BG;
        }
        leonos_ui_rect(&ui, x > 1U ? x - 1U : x, y > 1U ? y - 1U : y,
                       clip_px + 2U, cell_h + 2U,
                       bg);
    }
    leonos_ui_text_resized_clipped(&ui, x, y, clip_px,
                                   tmp, fg, bg, cell_w, cell_h);
    if (bold) {
        leonos_ui_text_resized_clipped(&ui, x + 1U, y, clip_px,
                                       tmp, fg, bg, cell_w, cell_h);
    }
    if (italic) {
        for (uint32_t n = 0; n < cell_count; ++n) {
            uint32_t sx = x + n * cell_w + 1U;
            leonos_ui_rect(&ui, sx, y + cell_h - 3U, 3U, 1U, fg);
        }
    }
    if (underline && copy_len) {
        leonos_ui_rect(&ui, x, y + cell_h - 1U,
                       clip_px, 1U, fg);
    }
}

static void draw_line_run_scrolled(const struct browser_line *line,
                                   uint32_t start, uint32_t end,
                                   int32_t x, uint32_t y,
                                   uint32_t fg, uint32_t bg,
                                   uint8_t underline, uint8_t bold,
                                   uint8_t italic, uint8_t code,
                                   uint32_t cell_w, uint32_t cell_h,
                                   uint32_t run_cells)
{
    uint32_t clip_x = text_x();
    uint32_t clip_w = document_text_w();
    int32_t clip_right = (int32_t)(clip_x + clip_w);
    int32_t draw_x = x;
    uint32_t clipped_start = start;
    uint32_t cells = run_cells;
    if (!line || start >= end || !cell_w || !cells) {
        return;
    }
    if (draw_x >= clip_right ||
        draw_x + (int32_t)(run_cells * cell_w) <= (int32_t)clip_x) {
        return;
    }
    if (draw_x < (int32_t)clip_x) {
        uint32_t hidden = (uint32_t)((int32_t)clip_x - draw_x);
        uint32_t skip_cells = (hidden + cell_w - 1U) / cell_w;
        uint32_t start_cells = browser_line_cells_between(line, 0, start);
        uint32_t skipped_cells;
        clipped_start = browser_line_byte_at_cell(line, start_cells + skip_cells);
        if (clipped_start >= end) {
            return;
        }
        skipped_cells = browser_line_cells_between(line, start, clipped_start);
        if (skipped_cells >= cells) {
            return;
        }
        draw_x += (int32_t)(skipped_cells * cell_w);
        cells -= skipped_cells;
    }
    if (draw_x >= clip_right) {
        return;
    }
    if (draw_x + (int32_t)(cells * cell_w) > clip_right) {
        uint32_t fit = (uint32_t)(clip_right - draw_x) / cell_w;
        if (fit < cells) {
            cells = fit;
        }
    }
    if (!cells) {
        return;
    }
    draw_line_run((uint32_t)draw_x, y, line->text + clipped_start,
                  end - clipped_start, fg, bg, underline, bold, italic,
                  code, cell_w, cell_h, cells);
}

static void browser_rect_clip(uint32_t clip_x, uint32_t clip_y,
                              uint32_t clip_w, uint32_t clip_h,
                              int32_t x, int32_t y, uint32_t w,
                              uint32_t h, uint32_t color)
{
    int32_t left = x;
    int32_t top = y;
    int32_t right = x + (int32_t)w;
    int32_t bottom = y + (int32_t)h;
    int32_t clip_right = (int32_t)(clip_x + clip_w);
    int32_t clip_bottom = (int32_t)(clip_y + clip_h);
    if (left < (int32_t)clip_x) {
        left = (int32_t)clip_x;
    }
    if (top < (int32_t)clip_y) {
        top = (int32_t)clip_y;
    }
    if (right > clip_right) {
        right = clip_right;
    }
    if (bottom > clip_bottom) {
        bottom = clip_bottom;
    }
    if (right <= left || bottom <= top) {
        return;
    }
    leonos_ui_rect(&ui, (uint32_t)left, (uint32_t)top,
                   (uint32_t)(right - left), (uint32_t)(bottom - top),
                   color);
}

uint8_t line_is_heading(uint8_t kind)
{
    return kind == BROWSER_LINE_HEADING1 ||
           kind == BROWSER_LINE_HEADING2 ||
           kind == BROWSER_LINE_HEADING3;
}

uint32_t browser_line_cell_w(uint8_t kind)
{
    if (kind == BROWSER_LINE_HEADING1) {
        return 16U;
    }
    if (kind == BROWSER_LINE_HEADING2) {
        return 12U;
    }
    if (kind == BROWSER_LINE_HEADING3) {
        return 10U;
    }
    return LEONOS_FONT_W;
}

uint32_t browser_line_cell_h(uint8_t kind)
{
    if (kind == BROWSER_LINE_HEADING1) {
        return 32U;
    }
    if (kind == BROWSER_LINE_HEADING2) {
        return 24U;
    }
    if (kind == BROWSER_LINE_HEADING3) {
        return 20U;
    }
    return LEONOS_FONT_H;
}

uint32_t browser_line_height(uint8_t kind)
{
    uint32_t cell_h = browser_line_cell_h(kind);
    if (kind == BROWSER_LINE_HEADING1) {
        return cell_h + 8U;
    }
    if (kind == BROWSER_LINE_HEADING2) {
        return cell_h + 6U;
    }
    if (kind == BROWSER_LINE_HEADING3) {
        return cell_h + 4U;
    }
    return cell_h + 2U;
}

uint32_t browser_line_render_height(const struct browser_line *line)
{
    uint32_t base = browser_line_height(line ? line->kind : BROWSER_LINE_NORMAL);
    if (browser_form_line_has_control(line) &&
        base < BROWSER_FORM_WIDGET_H + 2U) {
        return BROWSER_FORM_WIDGET_H + 2U;
    }
    return base;
}

uint32_t browser_line_cells_between(const struct browser_line *line,
                                    uint32_t start, uint32_t end)
{
    uint32_t cells = 0;
    if (!line || start >= line->len) {
        return 0;
    }
    if (end > line->len) {
        end = line->len;
    }
    for (uint32_t i = start; i < end; ++i) {
        cells += line->cell_width[i];
    }
    return cells;
}

uint32_t browser_line_next_byte(const struct browser_line *line,
                                uint32_t pos)
{
    if (!line || pos >= line->len) {
        return line ? line->len : 0;
    }
    ++pos;
    while (pos < line->len && line->cell_width[pos] == 0) {
        ++pos;
    }
    return pos;
}

uint32_t browser_line_byte_at_cell(const struct browser_line *line,
                                   uint32_t cell)
{
    uint32_t cells = 0;
    if (!line) {
        return 0;
    }
    for (uint32_t i = 0; i < line->len; i = browser_line_next_byte(line, i)) {
        uint32_t cw = line->cell_width[i] ? line->cell_width[i] : 1U;
        if (cell < cells + cw) {
            return i;
        }
        cells += cw;
    }
    return line->len;
}

uint32_t document_text_w(void)
{
    return page_w() > BROWSER_SCROLL_W + 24U
               ? page_w() - BROWSER_SCROLL_W - 24U
               : 80U;
}

uint32_t document_view_h(void)
{
    return page_h() > 16U ? page_h() - 16U : page_h();
}

static uint32_t table_line_cell_count(const struct browser_line *line)
{
    uint32_t pipes = 0;
    if (!line || line->kind != BROWSER_LINE_TABLE) {
        return 0;
    }
    for (uint32_t i = 0; i < line->len; i = browser_line_next_byte(line, i)) {
        if (line->text[i] == '|') {
            ++pipes;
        }
    }
    return pipes > 1U ? pipes - 1U : 0;
}

uint32_t document_content_w(void)
{
    uint32_t w = document_text_w();
    for (uint32_t i = 0; i < line_count; ++i) {
        const struct browser_line *line = &lines[i];
        uint32_t line_w = (uint32_t)line->indent * LEONOS_FONT_W;
        uint32_t cell_w = browser_line_cell_w(line->kind);
        if (line->kind == BROWSER_LINE_IMAGE) {
            line_w += 20U;
        }
        line_w += line->cells * cell_w + 16U;
        if (line->kind == BROWSER_LINE_TABLE) {
            uint32_t cells = table_line_cell_count(line);
            uint32_t table_w = line->cells * LEONOS_FONT_W + cells * 12U + 16U;
            if (table_w > line_w) {
                line_w = table_w;
            }
        }
        if (line_w > w) {
            w = line_w;
        }
    }
    return w;
}

static uint32_t table_line_next_pipe(const struct browser_line *line,
                                     uint32_t start)
{
    for (uint32_t i = start; line && i < line->len; i = browser_line_next_byte(line, i)) {
        if (line->text[i] == '|') {
            return i;
        }
    }
    return line ? line->len : 0;
}

static void table_line_trim_cell(const struct browser_line *line,
                                 uint32_t *start, uint32_t *end)
{
    while (line && *start < *end && line->text[*start] == ' ') {
        *start = browser_line_next_byte(line, *start);
    }
    while (line && *end > *start && line->text[*end - 1U] == ' ') {
        --(*end);
    }
}

static uint8_t table_line_cell_align(const struct browser_line *line,
                                     uint32_t start, uint32_t end)
{
    if (!line) {
        return BROWSER_ALIGN_LEFT;
    }
    for (uint32_t i = start; i < end; i = browser_line_next_byte(line, i)) {
        if (line->text[i] != ' ') {
            return line->cell_align[i];
        }
    }
    return BROWSER_ALIGN_LEFT;
}

static void draw_table_line_content(const struct browser_line *line,
                                    int32_t x, uint32_t y, uint32_t width)
{
    uint32_t cell_count = table_line_cell_count(line);
    uint32_t indent_px;
    int32_t content_x;
    uint32_t content_w;
    uint32_t row_w;
    uint32_t cell_h;
    uint32_t pipe;
    if (!line || cell_count == 0) {
        return;
    }
    indent_px = (uint32_t)line->indent * LEONOS_FONT_W;
    content_x = x + (int32_t)indent_px;
    content_w = width > indent_px ? width - indent_px : width;
    row_w = content_w > 6U ? content_w - 6U : content_w;
    cell_h = browser_line_cell_h(line->kind);
    pipe = table_line_next_pipe(line, 0);
    for (uint32_t cell = 0; cell < cell_count && pipe < line->len; ++cell) {
        uint32_t next_pipe = table_line_next_pipe(line, browser_line_next_byte(line, pipe));
        uint32_t content_start = browser_line_next_byte(line, pipe);
        uint32_t content_end = next_pipe;
        int32_t cell_x = content_x + (int32_t)((cell * row_w) / cell_count);
        int32_t cell_next_x = content_x + (int32_t)(((cell + 1U) * row_w) / cell_count);
        uint32_t cell_w = cell_next_x > cell_x ? (uint32_t)(cell_next_x - cell_x) : LEONOS_FONT_W;
        uint32_t inner_w = cell_w > 8U ? cell_w - 8U : cell_w;
        uint32_t text_cells;
        uint32_t text_w;
        uint32_t shift = 0;
        int32_t draw_x;
        uint8_t align;
        table_line_trim_cell(line, &content_start, &content_end);
        text_cells = browser_line_cells_between(line, content_start, content_end);
        text_w = text_cells * LEONOS_FONT_W;
        align = table_line_cell_align(line, content_start, content_end);
        if (inner_w > text_w) {
            if (align == BROWSER_ALIGN_RIGHT) {
                shift = inner_w - text_w;
            } else if (align == BROWSER_ALIGN_CENTER) {
                shift = (inner_w - text_w) / 2U;
            }
        }
        draw_x = cell_x + 4 + (int32_t)shift;
        for (uint32_t start = content_start; start < content_end;) {
            uint8_t link = line->link[start];
            uint8_t style = line->style[start];
            uint32_t run_fg = line->fg[start];
            uint32_t run_bg = line->bg[start];
            uint32_t end = browser_line_next_byte(line, start);
            uint32_t start_cells = browser_line_cells_between(line, content_start, start);
            uint32_t run_cells;
            uint32_t fg = BROWSER_TEXT_DARK;
            uint32_t bg = line->line_bg != BROWSER_COLOR_UNSET
                              ? line->line_bg
                              : BROWSER_TABLE_BG;
            uint8_t underline = 0;
            uint8_t bold = (style & BROWSER_TEXT_BOLD) != 0;
            uint8_t italic = (style & BROWSER_TEXT_ITALIC) != 0;
            uint8_t code = (style & BROWSER_TEXT_CODE) != 0;
            while (end < content_end && line->link[end] == link &&
                   line->style[end] == style &&
                   line->fg[end] == run_fg &&
                   line->bg[end] == run_bg) {
                end = browser_line_next_byte(line, end);
            }
            run_cells = browser_line_cells_between(line, start, end);
            if (run_fg != BROWSER_COLOR_UNSET) {
                fg = run_fg;
            }
            if (run_bg != BROWSER_COLOR_UNSET) {
                bg = run_bg;
            }
            if (link) {
                if (run_fg == BROWSER_COLOR_UNSET) {
                    fg = BROWSER_LINK_BLUE;
                }
                underline = 1;
                bold = (style & BROWSER_TEXT_BOLD) != 0;
            }
            if (style & BROWSER_TEXT_UNDERLINE) {
                underline = 1;
            }
            draw_line_run_scrolled(line, start, end,
                                   draw_x + (int32_t)(start_cells * LEONOS_FONT_W),
                                   y, fg, bg, underline, bold, italic, code,
                                   LEONOS_FONT_W, cell_h, run_cells);
            start = end;
        }
        pipe = next_pipe;
    }
}

void draw_document_line_frame(const struct browser_line *line,
                              int32_t x, uint32_t y, uint32_t width)
{
    int32_t content_x;
    uint32_t content_w;
    uint32_t bg;
    uint32_t border;
    uint32_t cell_h;
    uint32_t line_h;
    uint32_t clip_x = text_x();
    uint32_t clip_y = text_y();
    uint32_t clip_w = document_text_w();
    uint32_t clip_h = document_view_h();
    uint32_t indent_px;
    if (!line) {
        return;
    }
    indent_px = (uint32_t)line->indent * LEONOS_FONT_W;
    content_x = x + (int32_t)indent_px;
    content_w = width > indent_px ? width - indent_px : width;
    cell_h = browser_line_cell_h(line->kind);
    line_h = browser_line_render_height(line);
    bg = line->line_bg != BROWSER_COLOR_UNSET ? line->line_bg : LEONOS_UI_WHITE;
    border = line->border_color != BROWSER_COLOR_UNSET
                 ? line->border_color
                 : BROWSER_TABLE_BORDER;
    if (line->kind == BROWSER_LINE_HR) {
        uint32_t hr = line->border_color != BROWSER_COLOR_UNSET
                          ? line->border_color
                          : LEONOS_UI_DARK;
        browser_rect_clip(clip_x, clip_y, clip_w, clip_h, x,
                          (int32_t)(y + line_h / 2U), width, 1U, hr);
        browser_rect_clip(clip_x, clip_y, clip_w, clip_h, x,
                          (int32_t)(y + line_h / 2U + 1U), width, 1U,
                          LEONOS_UI_LIGHT);
        return;
    }
    if (line->line_bg != BROWSER_COLOR_UNSET) {
        browser_rect_clip(clip_x, clip_y, clip_w, clip_h, content_x,
                          (int32_t)y - 1,
                          content_w > 6U ? content_w - 6U : content_w,
                          line_h > 1U ? line_h - 1U : line_h, bg);
    }
    if (line->border_color != BROWSER_COLOR_UNSET &&
        line->kind != BROWSER_LINE_TABLE &&
        line->kind != BROWSER_LINE_BLOCKQUOTE) {
        int32_t bar_x = content_x >= 5 ? content_x - 5 : content_x;
        browser_rect_clip(clip_x, clip_y, clip_w, clip_h, bar_x,
                          (int32_t)y - 1, 3U,
                          line_h > 1U ? line_h - 1U : line_h, border);
    }
    if (line->kind == BROWSER_LINE_BLOCKQUOTE) {
        int32_t bar_x = content_x >= 8 ? content_x - 8 : x;
        browser_rect_clip(clip_x, clip_y, clip_w, clip_h, bar_x,
                          (int32_t)y - 1, 3U,
                          line_h > 1U ? line_h - 1U : line_h, border);
        browser_rect_clip(clip_x, clip_y, clip_w, clip_h, bar_x + 3,
                          (int32_t)y - 1,
                          content_w > 3U ? content_w - 3U : content_w,
                          line_h > 1U ? line_h - 1U : line_h,
                          line->line_bg != BROWSER_COLOR_UNSET ? bg : BROWSER_QUOTE_BG);
        return;
    }
    if (line->kind == BROWSER_LINE_TABLE) {
        uint32_t row_w = content_w > 6U ? content_w - 6U : content_w;
        uint32_t cell_count = table_line_cell_count(line);
        browser_rect_clip(clip_x, clip_y, clip_w, clip_h, content_x,
                          (int32_t)y - 1, row_w,
                          line_h > 1U ? line_h - 1U : line_h,
                          line->line_bg != BROWSER_COLOR_UNSET ? bg : BROWSER_TABLE_BG);
        browser_rect_clip(clip_x, clip_y, clip_w, clip_h, content_x,
                          (int32_t)y - 1, row_w, 1U, border);
        browser_rect_clip(clip_x, clip_y, clip_w, clip_h, content_x,
                          (int32_t)(y + cell_h + 1U), row_w, 1U, border);
        if (cell_count == 0) {
            return;
        }
        for (uint32_t i = 0; i <= cell_count; ++i) {
            int32_t vx = content_x + (int32_t)((i * row_w) / cell_count);
            if (i == cell_count && vx > content_x) {
                --vx;
            }
            browser_rect_clip(clip_x, clip_y, clip_w, clip_h, vx,
                              (int32_t)y - 1, 1U,
                              line_h > 1U ? line_h - 1U : line_h, border);
        }
        return;
    }
    if (line->kind == BROWSER_LINE_IMAGE) {
        browser_rect_clip(clip_x, clip_y, clip_w, clip_h, content_x,
                          (int32_t)y - 1,
                          content_w > 6U ? content_w - 6U : content_w,
                          line_h,
                          line->line_bg != BROWSER_COLOR_UNSET ? bg : BROWSER_IMAGE_BG);
        browser_rect_clip(clip_x, clip_y, clip_w, clip_h, content_x + 2,
                          (int32_t)y + 1, 14U, 14U, LEONOS_UI_WHITE);
        browser_rect_clip(clip_x, clip_y, clip_w, clip_h, content_x + 2,
                          (int32_t)y + 1, 14U, 1U, border);
        browser_rect_clip(clip_x, clip_y, clip_w, clip_h, content_x + 2,
                          (int32_t)y + 14, 14U, 1U, border);
        browser_rect_clip(clip_x, clip_y, clip_w, clip_h, content_x + 2,
                          (int32_t)y + 1, 1U, 14U, border);
        browser_rect_clip(clip_x, clip_y, clip_w, clip_h, content_x + 15,
                          (int32_t)y + 1, 1U, 14U, border);
    }
}

uint32_t line_align_shift_px(const struct browser_line *line,
                                    uint32_t doc_w)
{
    uint32_t indent_px;
    uint32_t cell_w;
    uint32_t content_w;
    uint32_t text_w;
    uint32_t image_w;
    if (!line || line->align == BROWSER_ALIGN_LEFT ||
        doc_w <= (uint32_t)line->indent * LEONOS_FONT_W) {
        return 0;
    }
    indent_px = (uint32_t)line->indent * LEONOS_FONT_W;
    cell_w = browser_line_cell_w(line->kind);
    image_w = line->kind == BROWSER_LINE_IMAGE ? 20U : 0U;
    content_w = doc_w - indent_px;
    text_w = image_w + line->cells * cell_w;
    if (content_w <= text_w) {
        return 0;
    }
    if (line->align == BROWSER_ALIGN_RIGHT) {
        return content_w - text_w;
    }
    return (content_w - text_w) / 2U;
}

void draw_document_lines(void)
{
    uint32_t px = text_x();
    uint32_t py = text_y();
    uint32_t page_bottom = page_y() + page_h() - 8U;
    uint32_t doc_w = document_text_w();
    uint32_t content_w = document_content_w();
    int32_t origin_x = (int32_t)px - (int32_t)scroll_x;
    uint32_t text_bg = LEONOS_UI_WHITE;
    uint32_t y = py;
    for (uint32_t row = scroll_line; row < line_count && y < page_bottom; ++row) {
        struct browser_line *line = &lines[row];
        int32_t line_px = origin_x + (int32_t)((uint32_t)line->indent * LEONOS_FONT_W);
        uint32_t image_text_offset = line->kind == BROWSER_LINE_IMAGE ? 20U : 0U;
        uint32_t cell_w = browser_line_cell_w(line->kind);
        uint32_t cell_h = browser_line_cell_h(line->kind);
        uint32_t line_h = browser_line_render_height(line);
        uint32_t start = 0;
        if (line->len == 0 && line->kind != BROWSER_LINE_HR) {
            y += line_h;
            continue;
        }
        draw_document_line_frame(line, origin_x, y, content_w);
        if (line->kind == BROWSER_LINE_HR) {
            y += line_h;
            continue;
        }
        if (line->kind == BROWSER_LINE_TABLE && table_line_cell_count(line)) {
            draw_table_line_content(line, origin_x, y, content_w);
            y += line_h;
            continue;
        }
        line_px += (int32_t)line_align_shift_px(line, doc_w);
        while (start < line->len) {
            uint8_t link = line->link[start];
            uint8_t style = line->style[start];
            uint32_t run_fg = line->fg[start];
            uint32_t run_bg = line->bg[start];
            uint32_t end = browser_line_next_byte(line, start);
            uint32_t start_cells = browser_line_cells_between(line, 0, start);
            uint32_t run_cells;
            uint32_t form_control_index = BROWSER_MAX_FORM_CONTROLS;
            uint32_t fg = line_is_heading(line->kind) ? BROWSER_IE_NAVY : BROWSER_TEXT_DARK;
            uint32_t bg = line->line_bg != BROWSER_COLOR_UNSET ? line->line_bg : text_bg;
            uint8_t underline = 0;
            uint8_t bold = line_is_heading(line->kind) ||
                           (style & BROWSER_TEXT_BOLD);
            uint8_t italic = (style & BROWSER_TEXT_ITALIC) != 0;
            uint8_t code = (style & BROWSER_TEXT_CODE) != 0;
            while (end < line->len && line->link[end] == link &&
                   line->style[end] == style &&
                   line->fg[end] == run_fg &&
                   line->bg[end] == run_bg) {
                end = browser_line_next_byte(line, end);
            }
            if (browser_find_row == (int32_t)row && browser_find_len) {
                uint32_t match_end = browser_find_start + browser_find_len;
                if (start < browser_find_start && end > browser_find_start) {
                    end = browser_find_start;
                } else if (start < match_end && end > match_end) {
                    end = match_end;
                }
            }
            run_cells = browser_line_cells_between(line, start, end);
            if (run_fg != BROWSER_COLOR_UNSET) {
                fg = run_fg;
            }
            if (run_bg != BROWSER_COLOR_UNSET) {
                bg = run_bg;
            }
            if (browser_find_match_boundary(row, start)) {
                bg = 0x00fff3a0U;
            }
            if (link) {
                if (run_fg == BROWSER_COLOR_UNSET) {
                    fg = BROWSER_LINK_BLUE;
                }
                underline = 1;
                bold = (style & BROWSER_TEXT_BOLD) != 0;
            } else if (line->kind == BROWSER_LINE_MUTED) {
                fg = LEONOS_UI_DARK;
            } else if (line->kind == BROWSER_LINE_BLOCKQUOTE) {
                if (run_fg == BROWSER_COLOR_UNSET) {
                    fg = 0x00484848U;
                }
                if (run_bg == BROWSER_COLOR_UNSET &&
                    line->line_bg == BROWSER_COLOR_UNSET) {
                    bg = BROWSER_QUOTE_BG;
                }
            } else if (line->kind == BROWSER_LINE_TABLE) {
                if (run_bg == BROWSER_COLOR_UNSET &&
                    line->line_bg == BROWSER_COLOR_UNSET) {
                    bg = BROWSER_TABLE_BG;
                }
            } else if (line->kind == BROWSER_LINE_IMAGE) {
                if (run_fg == BROWSER_COLOR_UNSET) {
                    fg = 0x00304050U;
                }
                if (run_bg == BROWSER_COLOR_UNSET &&
                    line->line_bg == BROWSER_COLOR_UNSET) {
                    bg = BROWSER_IMAGE_BG;
                }
            }
            if (style & BROWSER_TEXT_UNDERLINE) {
                underline = 1;
            }
            if (link && (uint32_t)(link - 1U) < link_count &&
                browser_form_control_from_href(links[link - 1U].href,
                                               &form_control_index)) {
                int32_t control_x = line_px + (int32_t)(image_text_offset +
                                                        start_cells * cell_w);
                int32_t control_right = control_x + (int32_t)(run_cells * cell_w);
                uint32_t control_y = y + (line_h > BROWSER_FORM_WIDGET_H
                                              ? (line_h - BROWSER_FORM_WIDGET_H) / 2U
                                              : 0U);
                if (control_right > (int32_t)px &&
                    control_x < (int32_t)(px + doc_w)) {
                    uint32_t control_w = run_cells * cell_w;
                    if (control_x < (int32_t)px) {
                        uint32_t hidden = (uint32_t)((int32_t)px - control_x);
                        if (hidden >= control_w) {
                            start = end;
                            continue;
                        }
                        control_x = (int32_t)px;
                        control_w -= hidden;
                    }
                    if (control_x + (int32_t)control_w > (int32_t)(px + doc_w)) {
                        control_w = (uint32_t)((int32_t)(px + doc_w) - control_x);
                    }
                    browser_draw_form_control((uint32_t)control_x, control_y,
                                              control_w, form_control_index);
                }
            } else {
                draw_line_run_scrolled(line, start, end,
                                       line_px + (int32_t)(image_text_offset +
                                                          start_cells * cell_w),
                                       y, fg, bg, underline, bold, italic,
                                       code, cell_w, cell_h, run_cells);
            }
            start = end;
        }
        y += line_h;
    }
}

void draw_browser_menu(void)
{
    if (browser_embedded) {
        return;
    }
    struct leonos_ui_menubar_item top_items[] = {
        {T("File", "文件"), BROWSER_MENU_FILE, BROWSER_MENU_FILE_W, 0},
        {T("Edit", "编辑"), BROWSER_MENU_EDIT, BROWSER_MENU_EDIT_W, 0},
        {T("View", "查看"), BROWSER_MENU_VIEW, BROWSER_MENU_VIEW_W, 0},
        {T("Favorites", "收藏夹"), BROWSER_MENU_FAVORITES, BROWSER_MENU_FAVORITES_W, 0},
        {T("Help", "帮助"), BROWSER_MENU_HELP, BROWSER_MENU_HELP_W, 0},
    };
    struct leonos_ui_rect r;
    if (menu_open == BROWSER_MENU_FILE) {
        struct leonos_ui_context_menu_item items[] = {
            {T("Home", "主页"), BROWSER_CMD_HOME, 0},
            {T("Refresh", "刷新"), BROWSER_CMD_REFRESH, 0},
            {T("Download Current Page", "下载当前页面"), BROWSER_CMD_DOWNLOAD, 0},
            {T("Close", "关闭"), BROWSER_CMD_CLOSE, 0},
        };
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    BROWSER_MENU_FILE, &r);
        leonos_ui_menu_popup(&ui, (uint32_t)r.x, BROWSER_MENU_H, 188U,
                             items, sizeof(items) / sizeof(items[0]), 0);
    } else if (menu_open == BROWSER_MENU_EDIT) {
        struct leonos_ui_context_menu_item items[] = {
            {T("Select Address", "选中地址"), BROWSER_CMD_SELECT_ADDRESS, 0},
            {T("Clear Address", "清空地址"), BROWSER_CMD_CLEAR_ADDRESS, 0},
            {T("Find in Page...", "在页面中查找..."), BROWSER_CMD_FIND, 0},
            {T("Find Next", "查找下一个"), BROWSER_CMD_FIND_NEXT, 0},
        };
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    BROWSER_MENU_EDIT, &r);
        leonos_ui_menu_popup(&ui, (uint32_t)r.x, BROWSER_MENU_H, 192U,
                             items, sizeof(items) / sizeof(items[0]), 0);
    } else if (menu_open == BROWSER_MENU_VIEW) {
        struct leonos_ui_context_menu_item items[] = {
            {T("Refresh", "刷新"), BROWSER_CMD_REFRESH, 0},
            {T("Top", "顶部"), BROWSER_CMD_TOP, 0},
            {T("Bottom", "底部"), BROWSER_CMD_BOTTOM, 0},
        };
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    BROWSER_MENU_VIEW, &r);
        leonos_ui_menu_popup(&ui, (uint32_t)r.x, BROWSER_MENU_H, 166U,
                             items, sizeof(items) / sizeof(items[0]), 0);
    } else if (menu_open == BROWSER_MENU_FAVORITES) {
        struct leonos_ui_context_menu_item items[BROWSER_MAX_BOOKMARKS + 4U];
        uint32_t count = 0;
        browser_bookmarks_build_menu(items, sizeof(items) / sizeof(items[0]),
                                     &count);
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    BROWSER_MENU_FAVORITES, &r);
        leonos_ui_menu_popup(&ui, (uint32_t)r.x, BROWSER_MENU_H, 204U,
                             items, count, 0);
    } else if (menu_open == BROWSER_MENU_HELP) {
        struct leonos_ui_context_menu_item items[] = {
            {T("About Browser", "关于浏览器"), BROWSER_CMD_ABOUT, 0},
        };
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    BROWSER_MENU_HELP, &r);
        leonos_ui_menu_popup(&ui, (uint32_t)r.x, BROWSER_MENU_H, 176U,
                             items, sizeof(items) / sizeof(items[0]), 0);
    }
}

void draw_browser_devtools(void)
{
    char line[BROWSER_URL_CAP + BROWSER_TITLE_CAP + 64U];
    uint32_t height = browser_devtools_height();
    uint32_t y;
    uint32_t width;
    uint32_t source_bytes = 0;
    uint32_t pos;
    if (!height || browser_embedded) {
        return;
    }
    y = view_h > height ? view_h - height : 0;
    width = view_w > 12U ? view_w - 12U : view_w;
    while (source_bytes < BROWSER_SOURCE_CAP && page_source[source_bytes]) {
        ++source_bytes;
    }
    leonos_ui_inset(&ui, 6, y + 2U, width, height > 4U ? height - 4U : height,
                    LEONOS_UI_WHITE);
    leonos_ui_rect(&ui, 8, y + 4U, width > 4U ? width - 4U : width, 20U,
                   LEONOS_UI_ACTIVE_TITLE);
    leonos_ui_text_clipped(&ui, 16, y + 7U, width > 20U ? width - 20U : width,
                           T("Developer Tools", "开发者工具"),
                           LEONOS_UI_WHITE, LEONOS_UI_ACTIVE_TITLE);

    pos = 0;
    line[0] = 0;
    append_text(line, &pos, sizeof(line), T("Location: ", "地址: "));
    append_text(line, &pos, sizeof(line), current_location);
    leonos_ui_text_clipped(&ui, 16, y + 29U, width > 20U ? width - 20U : width,
                           line, BROWSER_TEXT_DARK, LEONOS_UI_WHITE);

    pos = 0;
    line[0] = 0;
    append_text(line, &pos, sizeof(line), T("Document: ", "文档: "));
    append_text(line, &pos, sizeof(line), page_title);
    append_text(line, &pos, sizeof(line), page_is_html ? " [HTML, " : " [text, ");
    append_u32(line, &pos, sizeof(line), source_bytes);
    append_text(line, &pos, sizeof(line), " bytes]");
    if (source_truncated) {
        append_text(line, &pos, sizeof(line), T(" source limit reached", " 源码已截断"));
    }
    leonos_ui_text_clipped(&ui, 16, y + 46U, width > 20U ? width - 20U : width,
                           line, BROWSER_TEXT_DARK, LEONOS_UI_WHITE);

    pos = 0;
    line[0] = 0;
    append_text(line, &pos, sizeof(line), T("Viewport: ", "视口: "));
    append_u32(line, &pos, sizeof(line), document_text_w());
    append_text(line, &pos, sizeof(line), " x ");
    append_u32(line, &pos, sizeof(line), document_view_h());
    append_text(line, &pos, sizeof(line), T("; content: ", "; 内容: "));
    append_u32(line, &pos, sizeof(line), document_content_w());
    append_text(line, &pos, sizeof(line), " px");
    leonos_ui_text_clipped(&ui, 16, y + 63U, width > 20U ? width - 20U : width,
                           line, BROWSER_TEXT_DARK, LEONOS_UI_WHITE);

    pos = 0;
    line[0] = 0;
    append_text(line, &pos, sizeof(line), T("Rendered: ", "渲染: "));
    append_u32(line, &pos, sizeof(line), line_count);
    append_text(line, &pos, sizeof(line), T(" lines, ", " 行, "));
    append_u32(line, &pos, sizeof(line), link_count);
    append_text(line, &pos, sizeof(line), T(" links, ", " 链接, "));
    append_u32(line, &pos, sizeof(line), browser_form_count);
    append_text(line, &pos, sizeof(line), T(" forms, ", " 表单, "));
    append_u32(line, &pos, sizeof(line), browser_form_control_count);
    append_text(line, &pos, sizeof(line), T(" controls", " 控件"));
    leonos_ui_text_clipped(&ui, 16, y + 80U, width > 20U ? width - 20U : width,
                           line, BROWSER_TEXT_DARK, LEONOS_UI_WHITE);

    pos = 0;
    line[0] = 0;
    append_text(line, &pos, sizeof(line), T("Scroll: line ", "滚动: 行 "));
    append_u32(line, &pos, sizeof(line), scroll_line);
    append_text(line, &pos, sizeof(line), " / ");
    append_u32(line, &pos, sizeof(line), line_count ? line_count - 1U : 0U);
    append_text(line, &pos, sizeof(line), ", x=");
    append_u32(line, &pos, sizeof(line), scroll_x);
    append_text(line, &pos, sizeof(line), T("; status: ", "; 状态: "));
    append_text(line, &pos, sizeof(line), status_text);
    leonos_ui_text_clipped(&ui, 16, y + 97U, width > 20U ? width - 20U : width,
                           line, BROWSER_TEXT_DARK, LEONOS_UI_WHITE);
}

void draw_browser(void)
{
    uint32_t p_y = page_y();
    uint32_t p_w = page_w();
    uint32_t p_h = page_h();
    uint32_t rows = visible_rows();
    uint32_t content_w = document_content_w();
    uint32_t doc_w = document_text_w();
    uint32_t has_hscroll = content_w > doc_w;
    uint32_t can_back = history_index > 0;
    uint32_t can_forward = history_index >= 0 && (uint32_t)history_index + 1U < history_count;
    char title_line[BROWSER_TITLE_CAP + 32U];
    uint32_t pos = 0;
    struct leonos_ui_menubar_item top_items[] = {
        {T("File", "文件"), BROWSER_MENU_FILE, BROWSER_MENU_FILE_W, 0},
        {T("Edit", "编辑"), BROWSER_MENU_EDIT, BROWSER_MENU_EDIT_W, 0},
        {T("View", "查看"), BROWSER_MENU_VIEW, BROWSER_MENU_VIEW_W, 0},
        {T("Favorites", "收藏夹"), BROWSER_MENU_FAVORITES, BROWSER_MENU_FAVORITES_W, 0},
        {T("Help", "帮助"), BROWSER_MENU_HELP, BROWSER_MENU_HELP_W, 0},
    };
    leonos_ui_rect(&ui, 0, 0, view_w, view_h, LEONOS_UI_GRAY);
    if (browser_embedded) {
        leonos_ui_toolbar(&ui, 0, 0, view_w, BROWSER_TOOLBAR_H);
        draw_toolbar_button(BROWSER_BACK_X, BROWSER_BACK_W, T("Back", "后退"), !can_back);
        draw_toolbar_button(toolbar_forward_x(), BROWSER_FORWARD_W, T("Forward", "前进"), !can_forward);
        draw_toolbar_button(toolbar_refresh_x(), BROWSER_REFRESH_W, T("Refresh", "刷新"), 0);
        draw_toolbar_button(toolbar_home_x(), BROWSER_HOME_W,
                            T("Setup", "返回"), 0);
        leonos_ui_text_clipped(&ui, toolbar_stop_x(), button_y() + 5U,
                               view_w > toolbar_stop_x() + 12U ? view_w - toolbar_stop_x() - 12U : 80U,
                               T("License Website", "许可证网站"),
                               BROWSER_TEXT_DARK, LEONOS_UI_GRAY);
        leonos_ui_inset(&ui, BROWSER_PAGE_X, p_y, p_w, p_h, LEONOS_UI_WHITE);
        draw_document_lines();
        leonos_ui_vscrollbar(&ui, BROWSER_PAGE_X + p_w - BROWSER_SCROLL_W - 2U,
                             p_y + 2U, BROWSER_SCROLL_W, p_h > 4U ? p_h - 4U : p_h,
                             scroll_line, line_count ? line_count : 1U, rows,
                             line_count <= rows ? LEONOS_UI_SCROLLBAR_DISABLED : 0);
        if (has_hscroll) {
            leonos_ui_hscrollbar(&ui, text_x(), text_y() + document_view_h(),
                                 doc_w, BROWSER_SCROLL_W,
                                 scroll_x, content_w, doc_w, 0);
        }
        leonos_ui_toast_draw(&ui, &browser_toast, leonos_uptime_ms());
        return;
    }
    leonos_ui_menubar_draw(&ui, 0, 0, view_w, top_items,
                           sizeof(top_items) / sizeof(top_items[0]),
                           menu_open);
    leonos_ui_toolbar(&ui, 0, BROWSER_MENU_H, view_w, BROWSER_TOOLBAR_H);
    draw_toolbar_button(BROWSER_BACK_X, BROWSER_BACK_W, T("Back", "后退"), !can_back);
    draw_toolbar_button(toolbar_forward_x(), BROWSER_FORWARD_W, T("Forward", "前进"), !can_forward);
    draw_toolbar_button(toolbar_refresh_x(), BROWSER_REFRESH_W, T("Refresh", "刷新"), 0);
    draw_toolbar_button(toolbar_home_x(), BROWSER_HOME_W, T("Home", "主页"), 0);
    draw_toolbar_button(toolbar_stop_x(), BROWSER_STOP_W, T("Stop", "停止"), 1);
    title_line[0] = 0;
    append_text(title_line, &pos, sizeof(title_line), "LeonOS Browser - ");
    append_text(title_line, &pos, sizeof(title_line), page_title);
    leonos_ui_text_clipped(&ui, toolbar_title_x(), button_y() + 5U,
                           view_w > toolbar_title_x() + 12U ? view_w - toolbar_title_x() - 12U : 80U,
                           title_line, BROWSER_TEXT_DARK, LEONOS_UI_GRAY);
    leonos_ui_rect(&ui, 0, BROWSER_MENU_H + BROWSER_TOOLBAR_H, view_w,
                   BROWSER_ADDR_H, BROWSER_IE_SKY);
    leonos_ui_text(&ui, 12, address_y() + 5U, T("Address", "地址"),
                   BROWSER_TEXT_DARK, BROWSER_IE_SKY);
    leonos_ui_edit_state_draw(&ui, 74, address_y(), address_w(), &address_edit, 0);
    leonos_ui_button(&ui, go_x(), address_y(), BROWSER_GO_W, LEONOS_UI_BUTTON_H,
                     T("Go", "转到"), 0);
    leonos_ui_inset(&ui, BROWSER_PAGE_X, p_y, p_w, p_h, LEONOS_UI_WHITE);
    draw_document_lines();
    leonos_ui_vscrollbar(&ui, BROWSER_PAGE_X + p_w - BROWSER_SCROLL_W - 2U,
                         p_y + 2U, BROWSER_SCROLL_W, p_h > 4U ? p_h - 4U : p_h,
                         scroll_line, line_count ? line_count : 1U, rows,
                         line_count <= rows ? LEONOS_UI_SCROLLBAR_DISABLED : 0);
    if (has_hscroll) {
        leonos_ui_hscrollbar(&ui, text_x(), text_y() + document_view_h(),
                             doc_w, BROWSER_SCROLL_W,
                             scroll_x, content_w, doc_w, 0);
    }
    draw_browser_menu();
    draw_browser_devtools();
    leonos_ui_toast_draw(&ui, &browser_toast, leonos_uptime_ms());
}

void present_browser(void)
{
    if (browser_embedded) {
        draw_browser();
        return;
    }
    if (window_id <= 0) {
        return;
    }
    leonos_ui_bind(&ui, pixels, view_w, view_h, BROWSER_MAX_W);
    draw_browser();
    leonos_gui_present_window((uint32_t)window_id, view_w, view_h,
                              BROWSER_MAX_W, pixels);
}
