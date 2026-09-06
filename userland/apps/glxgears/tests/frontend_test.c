/* The real application entry point runs against deterministic GUI/GPU stubs. */
#include <assert.h>
#include "../main.c"

enum test_mode { HARDWARE, UNAVAILABLE, CREATE_FAILURE, RENDER_FAILURE,
                 RESIZE, RESIZE_FAILURE, POLYGON_MODE, PRESENT_FAILURE, DIAGNOSTIC_UNAVAILABLE };
static enum test_mode mode;
static unsigned creates, destroys, renders, gpu_presents, sw_presents, sw_draws, polls;
static unsigned diagnostic_queries;

int leonos_gui_connect(void) { return 0; }
unsigned long leonos_uptime_ms(void) { return 1000 + renders * 16; }
int sleep_ms(unsigned long ms) { (void)ms; return 0; }
leonos_pgl_context *leonos_pgl_create(int width, int height, const char *title)
{
    const char *setting = getenv("GLXGEARS_TEST_MODE");
    mode = setting ? (enum test_mode)atoi(setting) : HARDWARE;
    assert(width == WIDTH && height == HEIGHT && title);
    return (leonos_pgl_context *)(uintptr_t)1;
}
void leonos_pgl_make_current(leonos_pgl_context *ctx) { assert(ctx); }
int leonos_pgl_window_id(const leonos_pgl_context *ctx) { assert(ctx); return 1; }
int leonos_pgl_present(leonos_pgl_context *ctx)
{
    assert(ctx);
    ++sw_presents;
    return 0;
}
void leonos_pgl_destroy(leonos_pgl_context *ctx)
{
    assert(ctx && !gear1 && !gear2 && !gear3);
    unsigned render_failed = mode == RENDER_FAILURE || mode == DIAGNOSTIC_UNAVAILABLE;
    assert(diagnostic_queries == render_failed);
    if (mode == UNAVAILABLE || mode == CREATE_FAILURE || render_failed) {
        assert(sw_presents == 1 && sw_draws == 280 && gpu_presents == 0);
        assert(renders == render_failed);
        assert(destroys == render_failed);
    } else if (mode == RESIZE_FAILURE) {
        assert(sw_presents == 1 && sw_draws == 280 && gpu_presents == 1);
        assert(renders == 1 && creates == 2 && destroys == 1);
    } else {
        unsigned frames = mode == RESIZE || mode == POLYGON_MODE ? 2 : 1;
        assert(sw_presents == 0 && sw_draws == 0);
        assert(gpu_presents == frames && renders == frames);
        assert(creates == (mode == RESIZE ? 2U : 1U) && destroys == creates);
    }
    printf("glxgears frontend scenario %d: PASS\n", mode);
}
int leonos_gui_present_window(uint32_t id, uint32_t width, uint32_t height,
                              uint32_t stride, const uint32_t *pixels)
{
    assert(id == 1 && pixels && stride == width);
    assert(width == ((mode == RESIZE && renders == 2) ? 320U : WIDTH));
    assert(height == ((mode == RESIZE && renders == 2) ? 240U : HEIGHT));
    ++gpu_presents;
    return mode == PRESENT_FAILURE ? -5 : 1;
}
int leonos_gui_poll_app_event(struct leonos_gui_app_event *event)
{
    ++polls;
    event->window_id = 1;
    if (polls == 1)
        return 0;
    if ((mode == RESIZE || mode == RESIZE_FAILURE || mode == POLYGON_MODE) && polls < 4) {
        if (polls == 3)
            return 0;
        event->type = mode == POLYGON_MODE ? LEONOS_GUI_APP_EVENT_KEY_DOWN : LEONOS_GUI_APP_EVENT_RESIZE;
        event->keycode = GLXGEARS_KEY_P;
        event->pressed = 1;
        event->width = 320;
        event->height = 240;
    } else {
        event->type = LEONOS_GUI_APP_EVENT_CLOSE;
    }
    return 1;
}
int leonos_pgl_process_event(leonos_pgl_context *ctx, const struct leonos_gui_app_event *event)
{
    assert(ctx);
    if (event->type == LEONOS_GUI_APP_EVENT_CLOSE)
        return LEONOS_PGL_EVENT_CLOSE;
    if (event->type == LEONOS_GUI_APP_EVENT_RESIZE)
        return LEONOS_PGL_EVENT_RESIZED;
    return LEONOS_PGL_EVENT_NONE;
}
int gpu_sdk_info(gpu_sdk_info_t *info)
{
    info->flags = mode == UNAVAILABLE ? 0 : GPU_SDK_AVAILABLE;
    return 0;
}
int gpu_sdk_diagnostics(gpu_sdk_diagnostics_t *diagnostic)
{
    assert((mode == RENDER_FAILURE || mode == DIAGNOSTIC_UNAVAILABLE) && renders == 1);
    assert(!destroys && diagnostic->size == sizeof(*diagnostic) && diagnostic->version == 1);
    ++diagnostic_queries;
    if (mode == DIAGNOSTIC_UNAVAILABLE) return -25;
    *diagnostic = (gpu_sdk_diagnostics_t){.size = sizeof(*diagnostic), .version = 1,
        .status = -110, .stage = GPU_SDK_ERROR_FENCE, .handle = 1,
        .generation = 2, .fifo_min = 1164, .fifo_max = 65536,
        .fifo_next = 2048, .fifo_stop = 1800, .fifo_fence = 15, .issued_fence = 16,
        .fifo_busy = 1, .submitted_frames = 1};
    return 0;
}
int gpu_sdk_create(gpu_sdk_context_t *context)
{
    ++creates;
    if (mode == CREATE_FAILURE || (mode == RESIZE_FAILURE && creates == 2))
        return -12;
    context->handle = creates;
    return 0;
}
int gpu_sdk_destroy(uint64_t handle) { assert(handle); ++destroys; return 0; }
int gpu_sdk_render(const gpu_sdk_frame_t *frame)
{
    ++renders;
    assert(frame->fill_mode == ((mode == POLYGON_MODE && renders == 2) ?
                               GPU_SDK_FILL_POINT : GPU_SDK_FILL_SOLID));
    return mode == RENDER_FAILURE || mode == DIAGNOSTIC_UNAVAILABLE ? -110 : 0;
}

void glGenBuffers(GLsizei n, GLuint *buffers) { while (n-- > 0) *buffers++ = 1; }
void glBindBuffer(GLenum target, GLuint buffer) { (void)target; (void)buffer; }
void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{ (void)target; (void)size; (void)data; (void)usage; }
void glDeleteBuffers(GLsizei n, const GLuint *buffers) { assert(n == 1 && *buffers); }
void glGenVertexArrays(GLsizei n, GLuint *arrays) { assert(n == 1); *arrays = 1; }
void glBindVertexArray(GLuint array) { assert(array); }
void glDeleteVertexArrays(GLsizei n, const GLuint *arrays) { assert(n == 1 && *arrays); }
void glEnable(GLenum cap) { (void)cap; }
void glClear(GLbitfield mask) { (void)mask; }
void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { (void)r; (void)g; (void)b; (void)a; }
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) { (void)x; (void)y; (void)width; (void)height; }
GLuint pglCreateProgram(vert_func vs, frag_func fs, GLsizei n, GLenum *interpolation, GLboolean depth)
{ (void)vs; (void)fs; (void)n; (void)interpolation; (void)depth; return 1; }
void glUseProgram(GLuint program) { assert(program); }
void pglSetUniform(void *uniform) { assert(uniform); }
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                           GLsizei stride, const void *pointer)
{ (void)index; (void)size; (void)type; (void)normalized; (void)stride; (void)pointer; }
void glEnableVertexAttribArray(GLuint index) { (void)index; }
void glDisableVertexAttribArray(GLuint index) { (void)index; }
void glDrawArrays(GLenum primitive, GLint first, GLsizei count)
{ assert(primitive == GL_TRIANGLE_STRIP && first >= 0 && count >= 3); ++sw_draws; }
void glPolygonMode(GLenum face, GLenum fill) { assert(face == GL_FRONT_AND_BACK && fill == GL_POINT); }
