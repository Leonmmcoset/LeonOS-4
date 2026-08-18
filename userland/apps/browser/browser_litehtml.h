#ifndef LEONOS_BROWSER_LITEHTML_H
#define LEONOS_BROWSER_LITEHTML_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <leonos/ui.h>
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct browser_litehtml_document;
struct leonos_gui_app_event;

typedef void (*browser_litehtml_link_callback)(void *opaque, const char *url);
typedef void (*browser_litehtml_title_callback)(void *opaque, const char *title);
typedef void (*browser_litehtml_submit_callback)(void *opaque,
                                                 const char *action,
                                                 const char *method,
                                                 const char *body);
typedef int (*browser_litehtml_resource_callback)(
    void *opaque, const char *url, uint8_t **data, uint32_t *size,
    char *content_type, uint32_t content_type_cap);

struct browser_litehtml_document *browser_litehtml_create(
    const char *source, const char *base_url,
    browser_litehtml_link_callback link_callback,
    browser_litehtml_title_callback title_callback,
    browser_litehtml_submit_callback submit_callback,
    browser_litehtml_resource_callback resource_callback,
    void *opaque, uint32_t width, uint32_t height);

void browser_litehtml_destroy(struct browser_litehtml_document *document);

int browser_litehtml_resize(struct browser_litehtml_document *document,
                            uint32_t width, uint32_t height);

void browser_litehtml_draw(struct browser_litehtml_document *document,
                           struct leonos_ui_surface *surface,
                           int32_t origin_x, int32_t origin_y,
                           int32_t scroll_x, int32_t scroll_y,
                           uint32_t clip_x, uint32_t clip_y,
                           uint32_t clip_w, uint32_t clip_h);

uint32_t browser_litehtml_content_width(
    const struct browser_litehtml_document *document);
uint32_t browser_litehtml_content_height(
    const struct browser_litehtml_document *document);

int browser_litehtml_mouse_move(struct browser_litehtml_document *document,
                                int32_t x, int32_t y);
int browser_litehtml_lbutton_down(struct browser_litehtml_document *document,
                                  int32_t x, int32_t y);
int browser_litehtml_lbutton_up(struct browser_litehtml_document *document,
                                int32_t x, int32_t y);
int browser_litehtml_mouse_leave(struct browser_litehtml_document *document);

void browser_litehtml_form_clear_focus(struct browser_litehtml_document *document);
int browser_litehtml_form_input_active(
    const struct browser_litehtml_document *document);
int browser_litehtml_form_input_secure(
    const struct browser_litehtml_document *document);
int browser_litehtml_form_handle_key(struct browser_litehtml_document *document,
                                     const struct leonos_gui_app_event *event);
uint32_t browser_litehtml_form_count(
    const struct browser_litehtml_document *document);
uint32_t browser_litehtml_form_control_count(
    const struct browser_litehtml_document *document);

#ifdef __cplusplus
}
#endif

#endif
