#ifndef LEONOS_MOUSE_H
#define LEONOS_MOUSE_H

#include <leonos/gui.h>

int leonos_mouse_hide(uint32_t window_id);
int leonos_mouse_show(uint32_t window_id);
int leonos_mouse_is_visible(void);
int leonos_mouse_set_position(uint32_t window_id, int32_t x, int32_t y);
int leonos_mouse_set_style(uint32_t window_id, uint32_t style);
int leonos_mouse_set_auto(uint32_t window_id);
int leonos_mouse_set_region(const struct leonos_gui_cursor_region_request *region);
int leonos_mouse_clear_regions(uint32_t window_id);
int leonos_mouse_get_state(struct leonos_mouse_state *state);
int leonos_mouse_get_position(int32_t *x, int32_t *y);

#endif
