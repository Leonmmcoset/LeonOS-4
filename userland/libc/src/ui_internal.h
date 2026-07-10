#ifndef LEONOS_UI_INTERNAL_H
#define LEONOS_UI_INTERNAL_H

#include <stdint.h>
#include <leonos/gui.h>
#include <leonos/psf_font.h>
#include <leonos/text.h>
#include <leonos/ui.h>

#define UI_LAYOUT_GLYPH_MAX 512U

extern uint8_t ui_shift_down;

uint32_t ui_strlen(const char *text);
int ui_is_shift_key(uint8_t keycode);
uint32_t ui_decode_utf8(const char *text, uint32_t len,
                        uint32_t off, uint32_t *byte_len);
uint32_t ui_cell_width(uint32_t cp);
int ui_layout_utf8(const char *text, uint32_t byte_len,
                   struct leonos_text_glyph *glyphs, uint32_t capacity,
                   struct leonos_text_layout *out);
uint32_t ui_next_codepoint_offset(const char *text, uint32_t len, uint32_t pos);
uint32_t ui_prev_codepoint_offset(const char *text, uint32_t pos);
uint32_t ui_text_cells_between(const char *text, uint32_t start, uint32_t end);
uint32_t ui_byte_offset_for_cell(const char *text, uint32_t len,
                                 uint32_t start, uint32_t target_cell);
void ui_char(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
             char ch, uint32_t fg, uint32_t bg, int transparent);
void ui_codepoint(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                  uint32_t codepoint, uint32_t cell_width,
                  uint32_t fg, uint32_t bg, int transparent);

void ui_window_button_draw(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                           char label, uint32_t flags);

int ui_theme_is_metro(void);

#endif
