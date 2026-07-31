#ifndef LEONOS_MOUSE_H
#define LEONOS_MOUSE_H

#include <stdint.h>

int leonos_mouse_hide(uint32_t window_id);
int leonos_mouse_show(uint32_t window_id);
int leonos_mouse_is_visible(void);

#endif
