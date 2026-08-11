#include <leonos/gui.h>
#include <leonos/ui.h>

struct value_popup_cache {
    uint32_t valid;
    uint32_t kind;
    uint32_t requested_x;
    uint32_t requested_y;
    uint32_t requested_w;
    uint32_t w;
    uint32_t h;
    int32_t x;
    int32_t y;
};

static struct value_popup_cache value_popup;

static void value_popup_place(const struct leonos_ui_surface *surface,
                              uint32_t kind, uint32_t requested_x,
                              uint32_t requested_y, uint32_t w, uint32_t h,
                              int32_t *out_x, int32_t *out_y)
{
    int32_t x = (int32_t)requested_x;
    int32_t y = (int32_t)requested_y;
    if (surface) {
        if (w > surface->width) {
            x = 0;
        } else if ((uint32_t)x + w > surface->width) {
            x = (int32_t)(surface->width - w);
        }
        if (h > surface->height) {
            y = 0;
        } else if ((uint32_t)y + h > surface->height) {
            if (y >= (int32_t)h + 2) {
                y -= (int32_t)h + 2;
            } else {
                y = (int32_t)(surface->height - h);
            }
        }
    }
    value_popup.valid = 1;
    value_popup.kind = kind;
    value_popup.requested_x = requested_x;
    value_popup.requested_y = requested_y;
    value_popup.requested_w = w;
    value_popup.w = w;
    value_popup.h = h;
    value_popup.x = x;
    value_popup.y = y;
    if (out_x) {
        *out_x = x;
    }
    if (out_y) {
        *out_y = y;
    }
}

static void value_popup_resolve(uint32_t kind, uint32_t requested_x,
                                uint32_t requested_y, uint32_t w, uint32_t h,
                                int32_t *out_x, int32_t *out_y)
{
    int32_t x = (int32_t)requested_x;
    int32_t y = (int32_t)requested_y;
    if (value_popup.valid && value_popup.kind == kind &&
        value_popup.requested_x == requested_x &&
        value_popup.requested_y == requested_y &&
        value_popup.requested_w == w) {
        x = value_popup.x;
        y = value_popup.y;
    } else if (requested_y >= h + 2) {
        /* Fallback for callers that draw through a separate surface pass. */
        y = (int32_t)requested_y - (int32_t)h - 2;
    }
    if (out_x) {
        *out_x = x;
    }
    if (out_y) {
        *out_y = y;
    }
}

static uint8_t color_channel(uint32_t color, uint8_t channel)
{
    return (uint8_t)(color >> ((2U - (channel % 3U)) * 8U));
}

static void color_set_channel(struct leonos_ui_color_input_state *state,
                              uint8_t channel, uint8_t value)
{
    uint32_t shift = (2U - (channel % 3U)) * 8U;
    state->color = (state->color & ~(0xffU << shift)) | ((uint32_t)value << shift);
}

static void color_format(char out[8], uint32_t color)
{
    static const char hex[] = "0123456789ABCDEF";
    out[0] = '#';
    for (uint32_t index = 0; index < 6; ++index) {
        out[index + 1] = hex[(color >> ((5U - index) * 4U)) & 0x0fU];
    }
    out[7] = 0;
}

void leonos_ui_color_input_state_init(struct leonos_ui_color_input_state *state,
                                      uint32_t color)
{
    if (!state) {
        return;
    }
    state->color = color & 0x00ffffffU;
    state->open = 0;
    state->focused = 0;
    state->channel = 0;
}

void leonos_ui_color_input(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                           uint32_t w, const struct leonos_ui_color_input_state *state,
                           uint32_t flags)
{
    char value[8];
    int32_t popup_x;
    int32_t popup_y;
    uint32_t popup_w = w < 214 ? 214 : w;
    uint32_t channel_w = (popup_w - 16) / 3;
    static const char names[] = "RGB";
    if (!state || w < 36) {
        return;
    }
    color_format(value, state->color);
    leonos_ui_bevel(surface, x, y, w, LEONOS_UI_BUTTON_H, LEONOS_UI_WHITE,
                    state->focused ? LEONOS_UI_BUTTON_PRESSED : 0);
    leonos_ui_rect(surface, x + 4, y + 4, 28, LEONOS_UI_BUTTON_H - 8, state->color);
    leonos_ui_text_transparent(surface, x + 40, y + 4, value,
                               (flags & LEONOS_UI_INPUT_DISABLED) ? LEONOS_UI_DARK : LEONOS_UI_BLACK);
    leonos_ui_text_transparent(surface, x + w - 14, y + 4, state->open ? "^" : "v",
                               LEONOS_UI_DARK);
    if (!state->open || (flags & LEONOS_UI_INPUT_DISABLED)) {
        return;
    }
    value_popup_place(surface, 1, x, y + LEONOS_UI_BUTTON_H + 2,
                      popup_w, 82, &popup_x, &popup_y);
    leonos_ui_bevel(surface, (uint32_t)popup_x, (uint32_t)popup_y,
                    popup_w, 82, LEONOS_UI_GRAY, 0);
    for (uint32_t channel = 0; channel < 3; ++channel) {
        uint32_t channel_x = (uint32_t)popup_x + 6 + channel * channel_w;
        int32_t channel_value = color_channel(state->color, (uint8_t)channel);
        char label[3] = {names[channel], ':', 0};
        uint32_t preview = channel == 0 ? ((uint32_t)channel_value << 16)
                         : channel == 1 ? ((uint32_t)channel_value << 8)
                                        : (uint32_t)channel_value;
        leonos_ui_text_transparent(surface, channel_x, (uint32_t)popup_y + 8, label, LEONOS_UI_BLACK);
        leonos_ui_rect(surface, channel_x, (uint32_t)popup_y + 26,
                       channel_w > 8 ? channel_w - 8 : channel_w, 12, preview);
        leonos_ui_stepper(surface, channel_x, (uint32_t)popup_y + 46,
                          channel_w > 8 ? channel_w - 8 : channel_w,
                          LEONOS_UI_BUTTON_H, channel_value, 0, 255,
                          state->channel == channel ? LEONOS_UI_BUTTON_ACTIVE : 0);
    }
}

int leonos_ui_color_input_handle_mouse(struct leonos_ui_color_input_state *state,
                                       int32_t px, int32_t py,
                                       uint32_t x, uint32_t y, uint32_t w,
                                       uint32_t flags)
{
    int32_t popup_x;
    int32_t popup_y;
    uint32_t popup_w;
    uint32_t channel_w;
    if (!state || (flags & LEONOS_UI_INPUT_DISABLED)) {
        return 0;
    }
    if (leonos_ui_hit((uint32_t)px, (uint32_t)py, (int32_t)x, (int32_t)y,
                      w, LEONOS_UI_BUTTON_H)) {
        state->open = state->open ? 0 : 1;
        state->focused = 1;
        return 1;
    }
    if (!state->open) {
        return 0;
    }
    popup_w = w < 214 ? 214 : w;
    value_popup_resolve(1, x, y + LEONOS_UI_BUTTON_H + 2,
                        popup_w, 82, &popup_x, &popup_y);
    channel_w = (popup_w - 16) / 3;
    for (uint32_t channel = 0; channel < 3; ++channel) {
        uint32_t channel_x = (uint32_t)popup_x + 6 + channel * channel_w;
        int32_t value = color_channel(state->color, (uint8_t)channel);
        if (leonos_ui_stepper_handle_mouse(&value, 0, 255, 1, channel_x,
                                           (uint32_t)popup_y + 46,
                                           channel_w > 8 ? channel_w - 8 : channel_w,
                                           LEONOS_UI_BUTTON_H, px, py)) {
            color_set_channel(state, (uint8_t)channel, (uint8_t)value);
            state->channel = (uint8_t)channel;
            return 1;
        }
    }
    if (!leonos_ui_hit((uint32_t)px, (uint32_t)py, popup_x, popup_y,
                       popup_w, 82)) {
        state->open = 0;
        return 1;
    }
    return 0;
}

int leonos_ui_color_input_handle_key(struct leonos_ui_color_input_state *state,
                                     uint8_t keycode, uint32_t flags)
{
    if (!state || (flags & LEONOS_UI_INPUT_DISABLED)) {
        return 0;
    }
    if (keycode == LEONOS_KEY_ENTER || keycode == LEONOS_KEY_SPACE) {
        state->open = state->open ? 0 : 1;
        state->focused = 1;
        return 1;
    }
    if (state->open && keycode == LEONOS_KEY_TAB) {
        state->channel = (uint8_t)((state->channel + 1U) % 3U);
        return 1;
    }
    return 0;
}

static uint8_t date_is_leap_year(uint16_t year)
{
    return (uint8_t)((year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U);
}

static uint8_t date_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t lengths[] = {31, 28, 31, 30, 31, 30,
                                      31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 31;
    }
    return (uint8_t)(month == 2 && date_is_leap_year(year) ? 29 : lengths[month - 1]);
}

static uint8_t date_weekday(uint16_t year, uint8_t month, uint8_t day)
{
    static const uint8_t offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    uint32_t adjusted_year = year - (month < 3 ? 1U : 0U);
    return (uint8_t)((adjusted_year + adjusted_year / 4U - adjusted_year / 100U +
                      adjusted_year / 400U + offsets[month - 1] + day) % 7U);
}

static void date_normalize(struct leonos_ui_date_input_state *state)
{
    if (state->year == 0) {
        state->year = 2000;
    }
    if (state->month < 1) {
        state->month = 1;
    } else if (state->month > 12) {
        state->month = 12;
    }
    if (state->day < 1) {
        state->day = 1;
    } else if (state->day > date_days_in_month(state->year, state->month)) {
        state->day = date_days_in_month(state->year, state->month);
    }
}

static void date_format(char out[11], const struct leonos_ui_date_input_state *state)
{
    uint16_t year = state->year;
    out[0] = (char)('0' + (year / 1000U) % 10U);
    out[1] = (char)('0' + (year / 100U) % 10U);
    out[2] = (char)('0' + (year / 10U) % 10U);
    out[3] = (char)('0' + year % 10U);
    out[4] = '-';
    out[5] = (char)('0' + state->month / 10U);
    out[6] = (char)('0' + state->month % 10U);
    out[7] = '-';
    out[8] = (char)('0' + state->day / 10U);
    out[9] = (char)('0' + state->day % 10U);
    out[10] = 0;
}

static void date_change_month(struct leonos_ui_date_input_state *state, int direction)
{
    if (direction < 0) {
        if (state->month == 1) {
            if (state->year > 1) {
                --state->year;
            }
            state->month = 12;
        } else {
            --state->month;
        }
    } else if (state->month == 12) {
        if (state->year < 9999) {
            ++state->year;
        }
        state->month = 1;
    } else {
        ++state->month;
    }
    date_normalize(state);
}

void leonos_ui_date_input_state_init(struct leonos_ui_date_input_state *state,
                                     uint16_t year, uint8_t month, uint8_t day)
{
    if (!state) {
        return;
    }
    state->year = year;
    state->month = month;
    state->day = day;
    state->open = 0;
    state->focused = 0;
    state->part = 0;
    date_normalize(state);
}

void leonos_ui_date_input(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                          uint32_t w, const struct leonos_ui_date_input_state *state,
                          uint32_t flags)
{
    char value[11];
    char day_text[3];
    int32_t popup_x;
    int32_t popup_y;
    uint32_t popup_w = w < 224 ? 224 : w;
    uint32_t cell_w = (popup_w - 12) / 7;
    uint8_t first;
    uint8_t days;
    static const char *weekdays[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    if (!state || w < 48) {
        return;
    }
    date_format(value, state);
    leonos_ui_bevel(surface, x, y, w, LEONOS_UI_BUTTON_H, LEONOS_UI_WHITE,
                    state->focused ? LEONOS_UI_BUTTON_PRESSED : 0);
    leonos_ui_text_transparent(surface, x + 8, y + 4, value,
                               (flags & LEONOS_UI_INPUT_DISABLED) ? LEONOS_UI_DARK : LEONOS_UI_BLACK);
    leonos_ui_text_transparent(surface, x + w - 16, y + 4, state->open ? "^" : "v",
                               LEONOS_UI_DARK);
    if (!state->open || (flags & LEONOS_UI_INPUT_DISABLED)) {
        return;
    }
    value_popup_place(surface, 2, x, y + LEONOS_UI_BUTTON_H + 2,
                      popup_w, 174, &popup_x, &popup_y);
    leonos_ui_bevel(surface, (uint32_t)popup_x, (uint32_t)popup_y,
                    popup_w, 174, LEONOS_UI_GRAY, 0);
    leonos_ui_button(surface, (uint32_t)popup_x + 6, (uint32_t)popup_y + 4, 22, 20, "<", 0);
    leonos_ui_button(surface, (uint32_t)popup_x + popup_w - 28,
                     (uint32_t)popup_y + 4, 22, 20, ">", 0);
    leonos_ui_text_transparent(surface, (uint32_t)popup_x + 42,
                               (uint32_t)popup_y + 6, value, LEONOS_UI_BLACK);
    for (uint32_t column = 0; column < 7; ++column) {
        leonos_ui_text_transparent(surface, (uint32_t)popup_x + 8 + column * cell_w,
                                   (uint32_t)popup_y + 32,
                                   weekdays[column], LEONOS_UI_DARK);
    }
    first = date_weekday(state->year, state->month, 1);
    days = date_days_in_month(state->year, state->month);
    for (uint8_t day = 1; day <= days; ++day) {
        uint32_t index = first + day - 1U;
        uint32_t cell_x = (uint32_t)popup_x + 6 + (index % 7U) * cell_w;
        uint32_t cell_y = (uint32_t)popup_y + 50 + (index / 7U) * 20U;
        uint32_t selected = day == state->day;
        day_text[0] = (char)('0' + day / 10U);
        day_text[1] = (char)('0' + day % 10U);
        day_text[2] = 0;
        if (selected) {
            leonos_ui_rect(surface, cell_x, cell_y, cell_w > 2 ? cell_w - 2 : cell_w,
                           18, LEONOS_UI_ACTIVE_TITLE);
        }
        leonos_ui_text_transparent(surface, cell_x + 3, cell_y + 2, day_text,
                                   selected ? LEONOS_UI_WHITE : LEONOS_UI_BLACK);
    }
}

int leonos_ui_date_input_handle_mouse(struct leonos_ui_date_input_state *state,
                                      int32_t px, int32_t py,
                                      uint32_t x, uint32_t y, uint32_t w,
                                      uint32_t flags)
{
    int32_t popup_x;
    int32_t popup_y;
    uint32_t popup_w;
    uint32_t cell_w;
    uint8_t first;
    if (!state || (flags & LEONOS_UI_INPUT_DISABLED)) {
        return 0;
    }
    if (leonos_ui_hit((uint32_t)px, (uint32_t)py, (int32_t)x, (int32_t)y,
                      w, LEONOS_UI_BUTTON_H)) {
        state->open = state->open ? 0 : 1;
        state->focused = 1;
        return 1;
    }
    if (!state->open) {
        return 0;
    }
    popup_w = w < 224 ? 224 : w;
    value_popup_resolve(2, x, y + LEONOS_UI_BUTTON_H + 2,
                        popup_w, 174, &popup_x, &popup_y);
    if ((uint32_t)py >= (uint32_t)popup_y + 4 &&
        (uint32_t)py < (uint32_t)popup_y + 24) {
        if ((uint32_t)px >= (uint32_t)popup_x + 6 &&
            (uint32_t)px < (uint32_t)popup_x + 28) {
            date_change_month(state, -1);
            return 1;
        }
        if ((uint32_t)px >= (uint32_t)popup_x + popup_w - 28 &&
            (uint32_t)px < (uint32_t)popup_x + popup_w - 6) {
            date_change_month(state, 1);
            return 1;
        }
    }
    cell_w = (popup_w - 12) / 7;
    if ((uint32_t)px >= (uint32_t)popup_x + 6 &&
        (uint32_t)px < (uint32_t)popup_x + popup_w - 6 &&
        (uint32_t)py >= (uint32_t)popup_y + 50 &&
        (uint32_t)py < (uint32_t)popup_y + 170) {
        uint32_t column = ((uint32_t)px - (uint32_t)popup_x - 6) / cell_w;
        uint32_t row = ((uint32_t)py - (uint32_t)popup_y - 50) / 20U;
        uint32_t cell = row * 7U + column;
        first = date_weekday(state->year, state->month, 1);
        if (cell >= first && cell < first + date_days_in_month(state->year, state->month)) {
            state->day = (uint8_t)(cell - first + 1U);
            state->open = 0;
            state->focused = 1;
            return 1;
        }
        return 1;
    }
    if (!leonos_ui_hit((uint32_t)px, (uint32_t)py, popup_x, popup_y,
                       popup_w, 174)) {
        state->open = 0;
        return 1;
    }
    return 0;
}

int leonos_ui_date_input_handle_key(struct leonos_ui_date_input_state *state,
                                    uint8_t keycode, uint32_t flags)
{
    if (!state || (flags & LEONOS_UI_INPUT_DISABLED)) {
        return 0;
    }
    if (keycode == LEONOS_KEY_ENTER || keycode == LEONOS_KEY_SPACE) {
        state->open = state->open ? 0 : 1;
        state->focused = 1;
        return 1;
    }
    if (keycode == LEONOS_KEY_TAB) {
        state->part = (uint8_t)((state->part + 1U) % 3U);
        state->focused = 1;
        return 1;
    }
    return 0;
}
