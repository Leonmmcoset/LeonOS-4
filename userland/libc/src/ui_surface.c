#include <leonos/ui.h>
#include <leonos/mouse.h>

#include <string.h>

#include "ui_internal.h"

#define UI_SURFACE_REGISTRY_CAP 32u

struct ui_surface_registration {
    struct leonos_ui_surface *surface;
    uint32_t window_id;
};

static struct ui_surface_registration ui_surface_registry[UI_SURFACE_REGISTRY_CAP];

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
    memset(surface, 0, sizeof(*surface));
    surface->pixels = pixels;
    surface->width = width;
    surface->height = height;
    surface->stride = stride ? stride : width;
    surface->cursor_magic = LEONOS_UI_CURSOR_MAGIC;
}

void leonos_ui_cursor_begin(struct leonos_ui_surface *surface, uint32_t window_id)
{
    struct ui_surface_registration *registration;
    if (!surface) {
        return;
    }
    registration = ui_surface_registration_for(surface, 1);
    if (surface->cursor_tracking && surface->cursor_window_id != window_id) {
        (void)leonos_mouse_clear_regions(surface->cursor_window_id);
        surface->cursor_region_count = 0;
    }
    if (registration) {
        registration->window_id = window_id;
    }
    surface->cursor_window_id = window_id;
    surface->cursor_next_region_id = 1;
    surface->cursor_tracking = window_id != 0;
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
    struct leonos_gui_cursor_region_request request;
    if (!surface || !surface->cursor_tracking || !width || !height ||
        surface->cursor_next_region_id > LEONOS_UI_CURSOR_REGION_MAX) {
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
    request.window_id = surface->cursor_window_id;
    request.region_id = id;
    request.x = x;
    request.y = y;
    request.width = width;
    request.height = height;
    request.style = style;
    request.flags = flags;
    request.operation = LEONOS_GUI_CURSOR_REGION_SET;
    return leonos_mouse_set_region(&request);
}

void leonos_ui_cursor_end(struct leonos_ui_surface *surface)
{
    if (!surface || !surface->cursor_tracking) {
        return;
    }
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
}

void leonos_ui_pixel(struct leonos_ui_surface *surface, uint32_t x, uint32_t y, uint32_t color)
{
    if (!surface || !surface->pixels || x >= surface->width || y >= surface->height) {
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
    for (uint32_t yy = y; yy < y + h; ++yy) {
        uint32_t *row = surface->pixels + (uint64_t)yy * surface->stride;
        for (uint32_t xx = x; xx < x + w; ++xx) {
            row[xx] = color;
        }
    }
}
