#define PGL_ABGR32
#define PGL_D24S8
/* The upstream default reserves 64 MiB for a single draw call.  Keep the
 * system ABI within the current user address-space budget while retaining
 * enough room for normal software-rendered applications. PGL_SMALL_MEM only
 * changes the scratch/attribute limits here because the explicit framebuffer
 * and depth format definitions above take precedence over its format preset. */
#define PGL_SMALL_MEM
#define PORTABLEGL_IMPLEMENTATION

#include <portablegl.h>
#include <leonos/gui.h>
#include <leonos/pgl.h>
#include <leonos/syscall.h>
#include <stdint.h>
#include <stdlib.h>

struct leonos_pgl_context {
    glContext *gl;
    pix_t *pixels;
    uint32_t *present_pixels;
    size_t present_pixel_count;
    int width;
    int height;
    uint32_t window_id;
};

static leonos_pgl_context *current_context;

static int valid_size(int width, int height)
{
    return width > 0 && width <= (int)LEONOS_GUI_MAX_WINDOW_WIDTH &&
           height > 0 && height <= (int)LEONOS_GUI_MAX_WINDOW_HEIGHT;
}

leonos_pgl_context *leonos_pgl_create(int width, int height, const char *title)
{
    leonos_pgl_context *ctx;
    int window;
    size_t pixel_count;

    if (current_context || !valid_size(width, height) || !title)
        return NULL;
    if (leonos_gui_connect() < 0)
        return NULL;
    pixel_count = (size_t)width * (size_t)height;
    if ((size_t)width != 0 && pixel_count / (size_t)width != (size_t)height)
        return NULL;
    if (pixel_count > SIZE_MAX / sizeof(uint32_t))
        return NULL;
    window = leonos_gui_create_app_window_ex(title, "PortableGL software renderer",
                                             (uint32_t)width, (uint32_t)height, 0);
    if (window <= 0)
        return NULL;
    ctx = (leonos_pgl_context *)calloc(1, sizeof(*ctx));
    if (!ctx) {
        leonos_gui_destroy_app_window((uint32_t)window);
        return NULL;
    }
    ctx->gl = (glContext *)calloc(1, sizeof(*ctx->gl));
    if (!ctx->gl) {
        free(ctx);
        leonos_gui_destroy_app_window((uint32_t)window);
        return NULL;
    }
    ctx->present_pixels = (uint32_t *)calloc(pixel_count, sizeof(*ctx->present_pixels));
    if (!ctx->present_pixels) {
        free(ctx->gl);
        free(ctx);
        leonos_gui_destroy_app_window((uint32_t)window);
        return NULL;
    }
    ctx->present_pixel_count = pixel_count;
    ctx->width = width;
    ctx->height = height;
    ctx->window_id = (uint32_t)window;
    if (!init_glContext(ctx->gl, &ctx->pixels, width, height)) {
        set_glContext(NULL);
        free(ctx->present_pixels);
        free(ctx->gl);
        free(ctx);
        leonos_gui_destroy_app_window((uint32_t)window);
        return NULL;
    }
    leonos_pgl_make_current(ctx);
    glViewport(0, 0, width, height);
    return ctx;
}

void leonos_pgl_destroy(leonos_pgl_context *ctx)
{
    if (!ctx)
        return;
    leonos_pgl_make_current(ctx);
    free_glContext(ctx->gl);
    leonos_gui_destroy_app_window(ctx->window_id);
    if (current_context == ctx)
        current_context = NULL;
    free(ctx->present_pixels);
    free(ctx->gl);
    free(ctx);
}

int leonos_pgl_resize(leonos_pgl_context *ctx, int width, int height)
{
    size_t pixel_count;
    uint32_t *present_pixels = ctx ? ctx->present_pixels : NULL;
    if (!ctx || !ctx->gl)
        return LEONOS_PGL_EINVAL;
    if (!valid_size(width, height))
        return LEONOS_PGL_EINVAL;
    pixel_count = (size_t)width * (size_t)height;
    if ((size_t)width != 0 && pixel_count / (size_t)width != (size_t)height)
        return LEONOS_PGL_EINVAL;
    if (pixel_count > SIZE_MAX / sizeof(uint32_t))
        return LEONOS_PGL_ENOMEM;
    if (pixel_count != ctx->present_pixel_count) {
        present_pixels = (uint32_t *)calloc(pixel_count, sizeof(*present_pixels));
        if (!present_pixels)
            return LEONOS_PGL_ENOMEM;
    }
    leonos_pgl_make_current(ctx);
    if (!pglResizeFramebuffer(width, height)) {
        if (present_pixels != ctx->present_pixels)
            free(present_pixels);
        return LEONOS_PGL_ENOMEM;
    }
    if (present_pixels != ctx->present_pixels)
        free(ctx->present_pixels);
    ctx->present_pixels = present_pixels;
    ctx->present_pixel_count = pixel_count;
    ctx->pixels = (pix_t *)pglGetBackBuffer();
    ctx->width = width;
    ctx->height = height;
    glViewport(0, 0, width, height);
    return 0;
}

int leonos_pgl_present(leonos_pgl_context *ctx)
{
    size_t index;
    if (!ctx || !ctx->gl || !ctx->pixels)
        return LEONOS_PGL_EINVAL;
    leonos_pgl_make_current(ctx);
    if (!ctx->present_pixels || ctx->present_pixel_count !=
            (size_t)ctx->width * (size_t)ctx->height)
        return LEONOS_PGL_ENOMEM;
    /* PortableGL's fixed ABGR32 layout is AABBGGRR in a host uint32_t,
     * while the window service consumes LeonOS's logical 00RRGGBB form. */
    for (index = 0; index < ctx->present_pixel_count; ++index) {
        uint32_t pixel = ((const uint32_t *)ctx->pixels)[index];
        ctx->present_pixels[index] = ((pixel & 0x000000ffU) << 16) |
                                     (pixel & 0x0000ff00U) |
                                     ((pixel & 0x00ff0000U) >> 16);
    }
    if (leonos_gui_present_window(ctx->window_id, (uint32_t)ctx->width,
                                  (uint32_t)ctx->height, (uint32_t)ctx->width,
                                  ctx->present_pixels) <= 0)
        return LEONOS_PGL_ENOSYS;
    return 0;
}

int leonos_pgl_window_id(const leonos_pgl_context *ctx)
{
    return ctx ? (int)ctx->window_id : LEONOS_PGL_EINVAL;
}

int leonos_pgl_process_event(leonos_pgl_context *ctx,
                             const struct leonos_gui_app_event *event)
{
    if (!ctx || !event || event->window_id != ctx->window_id)
        return LEONOS_PGL_EINVAL;
    if (event->type == LEONOS_GUI_APP_EVENT_CLOSE ||
        (event->type == LEONOS_GUI_APP_EVENT_KEY_DOWN &&
         event->keycode == LEONOS_KEY_ESCAPE))
        return LEONOS_PGL_EVENT_CLOSE;
    if (event->type == LEONOS_GUI_APP_EVENT_RESIZE) {
        if (leonos_pgl_resize(ctx, (int)event->width, (int)event->height) < 0)
            return LEONOS_PGL_EINVAL;
        return LEONOS_PGL_EVENT_RESIZED;
    }
    return LEONOS_PGL_EVENT_NONE;
}

void leonos_pgl_make_current(leonos_pgl_context *ctx)
{
    if (!ctx || !ctx->gl)
        return;
    if (current_context && current_context != ctx)
        return;
    current_context = ctx;
    set_glContext(ctx->gl);
}

glContext *leonos_pgl_native_context(leonos_pgl_context *ctx)
{
    return ctx ? ctx->gl : NULL;
}
