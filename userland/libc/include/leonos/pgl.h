#ifndef LEONOS_PGL_H
#define LEONOS_PGL_H

/* The system library and SDK use one fixed framebuffer ABI. */
#ifndef PGL_ABGR32
#define PGL_ABGR32
#endif
#ifndef PGL_D24S8
#define PGL_D24S8
#endif
#ifndef PGL_SMALL_MEM
#define PGL_SMALL_MEM
#endif

#include <portablegl.h>
#include <leonos/gui.h>

#define LEONOS_PGL_EINVAL (-22)
#define LEONOS_PGL_ENOMEM (-12)
#define LEONOS_PGL_ENOSYS (-38)

typedef struct leonos_pgl_context leonos_pgl_context;

/* Return values from leonos_pgl_process_event. */
#define LEONOS_PGL_EVENT_NONE 0
#define LEONOS_PGL_EVENT_CLOSE 1
#define LEONOS_PGL_EVENT_RESIZED 2

leonos_pgl_context *leonos_pgl_create(
    int width, int height, const char *title);

void leonos_pgl_destroy(leonos_pgl_context *ctx);

int leonos_pgl_resize(
    leonos_pgl_context *ctx, int width, int height);

int leonos_pgl_present(leonos_pgl_context *ctx);

int leonos_pgl_window_id(
    const leonos_pgl_context *ctx);

int leonos_pgl_process_event(
    leonos_pgl_context *ctx,
    const struct leonos_gui_app_event *event);

void leonos_pgl_make_current(
    leonos_pgl_context *ctx);

glContext *leonos_pgl_native_context(
    leonos_pgl_context *ctx);

#endif
