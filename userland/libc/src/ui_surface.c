#include <leonos/ui.h>
#include <leonos/mouse.h>

#include <string.h>

#include "ui_internal.h"

#define UI_SURFACE_REGISTRY_CAP 32u

struct ui_surface_registration {
    struct leonos_ui_surface *surface;
    uint32_t window_id;
    uint8_t implicit_frame_open;
    uint8_t implicit_enabled;
};

static struct ui_surface_registration ui_surface_registry[UI_SURFACE_REGISTRY_CAP];
static struct leonos_ui_surface *last_cursor_surface;

static struct ui_surface_registration *ui_surface_registration_for(
    struct leonos_ui_surface *surface, int create)
{
    struct ui_surface_registration *free_slot = NULL;
    for (uint32_t i = 0; i < UI_SURFACE_REGISTRY_CAP; ++i) {
        if (ui_surface_registry[i].surface == surface) {
            return &ui_surface_registry[i];
        }
        if (!ui_surface_registry[i].surface && !free_slot) {
            free_slot = &ui_surface_registry[i];
        }
    }
    if (create && free_slot) {
        free_slot->surface = surface;
        free_slot->window_id = 0;
        free_slot->implicit_frame_open = 0;
        free_slot->implicit_enabled = 1;
        return free_slot;
    }
    return NULL;
}

int leonos_ui_hit(uint32_t px, uint32_t py, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    return (int32_t)px >= x && (int32_t)py >= y &&
           (int32_t)px < x + (int32_t)w &&
           (int32_t)py < y + (int32_t)h;
}

void leonos_ui_bind(struct leonos_ui_surface *surface, uint32_t *pixels,
                    uint32_t width, uint32_t height, uint32_t stride)
{
    struct ui_surface_registration *registration;
    ui_theme_ensure_loaded();
    if (!surface) {
        return;
    }
    registration = ui_surface_registration_for(surface, 1);
    if (registration && registration->window_id) {
        (void)leonos_mouse_clear_regions(registration->window_id);
        registration->window_id = 0;
    }
    if (registration) {
        registration->implicit_frame_open = 0;
        registration->implicit_enabled = 1;
    }
    memset(surface, 0, sizeof(*surface));
    surface->pixels = pixels;
    surface->width = width;
    surface->height = height;
    surface->stride = stride ? stride : width;
    surface->cursor_magic = LEONOS_UI_CURSOR_MAGIC;
    if (last_cursor_surface == surface) {
        last_cursor_surface = NULL;
    }
}

void leonos_ui_set_clip(struct leonos_ui_surface *surface, int32_t x, int32_t y,
                        uint32_t width, uint32_t height)
{
    if (!surface) {
        return;
    }
    surface->clip_enabled = width && height;
    surface->clip_x = x;
    surface->clip_y = y;
    surface->clip_width = width;
    surface->clip_height = height;
}

void leonos_ui_clear_clip(struct leonos_ui_surface *surface)
{
    if (surface) {
        surface->clip_enabled = 0;
    }
}

void leonos_ui_cursor_begin(struct leonos_ui_surface *surface, uint32_t window_id)
{
    struct ui_surface_registration *registration;
    if (!surface) {
        return;
    }
    registration = ui_surface_registration_for(surface, 1);
    if (surface->cursor_tracking && surface->cursor_window_id &&
        surface->cursor_window_id != window_id) {
        (void)leonos_mouse_clear_regions(surface->cursor_window_id);
        surface->cursor_region_count = 0;
    }
    if (registration) {
        registration->window_id = window_id;
        registration->implicit_frame_open = 0;
        registration->implicit_enabled = 0;
    }
    surface->cursor_window_id = window_id;
    surface->cursor_next_region_id = 1;
    surface->cursor_tracking = window_id != 0;
    surface->cursor_implicit = 0;
    for (uint32_t i = 0; i < surface->cursor_region_count; ++i) {
        surface->cursor_regions[i].seen = 0;
    }
}

static int ui_cursor_region_equal(const struct leonos_ui_cursor_region_cache *region,
                                  int32_t x, int32_t y, uint32_t width, uint32_t height,
                                  uint32_t style, uint32_t flags)
{
    return region->x == x && region->y == y && region->width == width &&
           region->height == height && region->style == style && region->flags == flags;
}

int leonos_ui_cursor_region(struct leonos_ui_surface *surface,
                            int32_t x, int32_t y, uint32_t width, uint32_t height,
                            uint32_t style, uint32_t flags)
{
    uint32_t id;
    uint32_t index = LEONOS_UI_CURSOR_REGION_MAX;
    struct ui_surface_registration *registration;
    struct leonos_gui_cursor_region_request request;
    if (!surface || !width || !height) {
        return 0;
    }
    registration = ui_surface_registration_for(surface, 1);
    if (!surface->cursor_tracking) {
        /* Ordinary widgets implicitly form one cursor frame per present. */
        if (!registration || !registration->implicit_enabled) {
            return 0;
        }
        surface->cursor_next_region_id = 1;
        surface->cursor_tracking = 1;
        surface->cursor_implicit = 1;
        surface->cursor_window_id = 0;
        if (registration) {
            registration->implicit_frame_open = 1;
        }
        for (uint32_t i = 0; i < surface->cursor_region_count; ++i) {
            surface->cursor_regions[i].seen = 0;
        }
    }
    if (surface->cursor_next_region_id > LEONOS_UI_CURSOR_REGION_MAX) {
        return 0;
    }
    id = surface->cursor_next_region_id++;
    for (uint32_t i = 0; i < surface->cursor_region_count; ++i) {
        if (surface->cursor_regions[i].id == id) {
            index = i;
            break;
        }
    }
    if (index == LEONOS_UI_CURSOR_REGION_MAX) {
        if (surface->cursor_region_count >= LEONOS_UI_CURSOR_REGION_MAX) {
            return -1;
        }
        index = surface->cursor_region_count++;
        surface->cursor_regions[index] = (struct leonos_ui_cursor_region_cache){
            .id = id,
        };
    }
    surface->cursor_regions[index].seen = 1;
    if (ui_cursor_region_equal(&surface->cursor_regions[index], x, y, width, height,
                               style, flags)) {
        return 1;
    }
    surface->cursor_regions[index].x = x;
    surface->cursor_regions[index].y = y;
    surface->cursor_regions[index].width = width;
    surface->cursor_regions[index].height = height;
    surface->cursor_regions[index].style = style;
    surface->cursor_regions[index].flags = flags;
    surface->cursor_regions[index].synced = 0;
    request.window_id = surface->cursor_window_id;
    request.region_id = id;
    request.x = x;
    request.y = y;
    request.width = width;
    request.height = height;
    request.style = style;
    request.flags = flags;
    request.operation = LEONOS_GUI_CURSOR_REGION_SET;
    last_cursor_surface = surface;
    if (!request.window_id) {
        return 1;
    }
    return leonos_mouse_set_region(&request);
}

void leonos_ui_cursor_end(struct leonos_ui_surface *surface)
{
    struct ui_surface_registration *registration;
    if (!surface || !surface->cursor_tracking) {
        return;
    }
    registration = ui_surface_registration_for(surface, 0);
    for (uint32_t i = 0; i < surface->cursor_region_count;) {
        if (surface->cursor_regions[i].seen) {
            ++i;
            continue;
        }
        struct leonos_gui_cursor_region_request request = {
            .window_id = surface->cursor_window_id,
            .region_id = surface->cursor_regions[i].id,
            .operation = LEONOS_GUI_CURSOR_REGION_REMOVE,
        };
        (void)leonos_mouse_set_region(&request);
        surface->cursor_regions[i] = surface->cursor_regions[--surface->cursor_region_count];
    }
    surface->cursor_tracking = 0;
    surface->cursor_implicit = 0;
    if (registration) {
        registration->implicit_enabled = 1;
        registration->implicit_frame_open = 0;
    }
}

void leonos_ui_cursor_clear(struct leonos_ui_surface *surface)
{
    struct ui_surface_registration *registration;
    if (!surface) {
        return;
    }
    registration = ui_surface_registration_for(surface, 0);
    if (surface->cursor_window_id) {
        (void)leonos_mouse_clear_regions(surface->cursor_window_id);
    }
    if (registration) {
        registration->window_id = 0;
    }
    surface->cursor_region_count = 0;
    surface->cursor_next_region_id = 1;
    surface->cursor_tracking = 0;
    surface->cursor_implicit = 0;
    if (registration) {
        registration->implicit_frame_open = 0;
        registration->implicit_enabled = 1;
    }
    if (last_cursor_surface == surface) {
        last_cursor_surface = NULL;
    }
}

void leonos_ui_present_for_pixels(const uint32_t *pixels, uint32_t window_id)
{
    struct leonos_ui_surface *surface = NULL;
    struct ui_surface_registration *registration = NULL;

    if (pixels) {
        for (uint32_t i = 0; i < UI_SURFACE_REGISTRY_CAP; ++i) {
            if (ui_surface_registry[i].surface &&
                ui_surface_registry[i].surface->pixels == pixels) {
                surface = ui_surface_registry[i].surface;
                registration = &ui_surface_registry[i];
                break;
            }
        }
    }
    if (!surface && last_cursor_surface) {
        registration = ui_surface_registration_for(last_cursor_surface, 0);
        if (registration && last_cursor_surface->pixels == pixels) {
            surface = last_cursor_surface;
        }
    }
    if (!surface || !registration || !registration->implicit_enabled || !window_id) {
        return;
    }

    if (!surface->cursor_tracking && surface->cursor_region_count == 0) {
        registration->implicit_frame_open = 0;
        return;
    }

    if (registration->window_id && registration->window_id != window_id) {
        (void)leonos_mouse_clear_regions(registration->window_id);
    }

    surface->cursor_window_id = window_id;
    registration->window_id = window_id;
    for (uint32_t i = 0; i < surface->cursor_region_count; ++i) {
        struct leonos_ui_cursor_region_cache *region = &surface->cursor_regions[i];
        struct leonos_gui_cursor_region_request request = {
            .window_id = window_id,
            .region_id = region->id,
            .x = region->x,
            .y = region->y,
            .width = region->width,
            .height = region->height,
            .style = region->style,
            .flags = region->flags,
            .operation = LEONOS_GUI_CURSOR_REGION_SET,
        };
        if (region->seen && !region->synced) {
            (void)leonos_mouse_set_region(&request);
            region->synced = 1;
        }
    }
    for (uint32_t i = 0; i < surface->cursor_region_count;) {
        if (surface->cursor_regions[i].seen) {
            ++i;
            continue;
        }
        struct leonos_gui_cursor_region_request request = {
            .window_id = window_id,
            .region_id = surface->cursor_regions[i].id,
            .operation = LEONOS_GUI_CURSOR_REGION_REMOVE,
        };
        (void)leonos_mouse_set_region(&request);
        surface->cursor_regions[i] = surface->cursor_regions[--surface->cursor_region_count];
    }
    for (uint32_t i = 0; i < surface->cursor_region_count; ++i) {
        surface->cursor_regions[i].seen = 0;
    }
    surface->cursor_tracking = 0;
    surface->cursor_implicit = 0;
    surface->cursor_next_region_id = 1;
    registration->implicit_frame_open = 0;
}

void leonos_ui_pixel(struct leonos_ui_surface *surface, uint32_t x, uint32_t y, uint32_t color)
{
    if (!surface || !surface->pixels || x >= surface->width || y >= surface->height) {
        return;
    }
    if (surface->clip_enabled &&
        ((int32_t)x < surface->clip_x || (int32_t)y < surface->clip_y ||
         x >= (uint32_t)(surface->clip_x + (int32_t)surface->clip_width) ||
         y >= (uint32_t)(surface->clip_y + (int32_t)surface->clip_height))) {
        return;
    }
    surface->pixels[(uint64_t)y * surface->stride + x] = color;
}

void leonos_ui_rect(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                    uint32_t w, uint32_t h, uint32_t color)
{
    if (!surface || !surface->pixels || x >= surface->width || y >= surface->height) {
        return;
    }
    if (x + w > surface->width) {
        w = surface->width - x;
    }
    if (y + h > surface->height) {
        h = surface->height - y;
    }
    if (surface->clip_enabled) {
        uint32_t clip_x = surface->clip_x > 0 ? (uint32_t)surface->clip_x : 0;
        uint32_t clip_y = surface->clip_y > 0 ? (uint32_t)surface->clip_y : 0;
        uint32_t clip_right = surface->clip_x > 0
                                  ? clip_x + surface->clip_width
                                  : surface->clip_width;
        uint32_t clip_bottom = surface->clip_y > 0
                                   ? clip_y + surface->clip_height
                                   : surface->clip_height;
        if (x < clip_x) {
            uint32_t delta = clip_x - x;
            if (delta >= w) {
                return;
            }
            x += delta;
            w -= delta;
        }
        if (y < clip_y) {
            uint32_t delta = clip_y - y;
            if (delta >= h) {
                return;
            }
            y += delta;
            h -= delta;
        }
        if (x >= clip_right || y >= clip_bottom) {
            return;
        }
        if (x + w > clip_right) {
            w = clip_right - x;
        }
        if (y + h > clip_bottom) {
            h = clip_bottom - y;
        }
    }
    for (uint32_t yy = y; yy < y + h; ++yy) {
        uint32_t *row = surface->pixels + (uint64_t)yy * surface->stride;
        for (uint32_t xx = x; xx < x + w; ++xx) {
            row[xx] = color;
        }
    }
}
