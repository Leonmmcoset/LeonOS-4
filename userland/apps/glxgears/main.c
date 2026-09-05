/*
 * LeonOS frontend for PortableGL's upstream classic gears example, with
 * native SVGA3D drawing and a PortableGL software fallback.
 *
 * The renderer and gear mesh below come from
 * third_party/portablegl/examples/classic/gears.c.  That example has an SDL
 * frontend; LeonOS supplies the window, event and presentation layer here.
 * pgl.h is included first so the upstream implementation section is skipped
 * by portablegl.h's include guard and is provided by libportablegl instead.
 */
#include <leonos/gui.h>
#include <leonos/pgl.h>
#include <leonos/syscall.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#define main portablegl_upstream_main
#define gears_idle portablegl_upstream_gears_idle
#include "gears-upstream.c"
#undef gears_idle
#undef main

#include "gpu_backend.h"

#define GLXGEARS_KEY_P 25U

static void use_software_renderer(struct gears_gpu_backend *gpu,
                                  const char *reason, int result)
{
    if (gpu->handle) {
        struct leonos_gpu_diagnostics diagnostic = {.size = sizeof(diagnostic),
                                                    .version = LEONOS_GPU_ABI_VERSION};
        int query = leonos_gpu_diagnostics(&diagnostic);
        if (query < 0) {
            printf("glxgears: GPU_DIAGNOSTICS failed, error=%d\n", query);
        } else if (diagnostic.status && diagnostic.handle == gpu->handle) {
            static const char *const stages[] = {"none", "reap", "prepare-fence", "upload",
                "render-state", "draw", "readback-submit", "frame-fence", "copyout"};
            const char *stage = diagnostic.stage < sizeof(stages) / sizeof(stages[0]) ?
                                stages[diagnostic.stage] : "unknown";
            printf("glxgears: SVGA3D failure stage=%s(%u) status=%d generation=%u\n",
                   stage, diagnostic.stage, diagnostic.status, diagnostic.generation);
            printf("glxgears: FIFO min=%u max=%u next=%u stop=%u busy=0x%x\n",
                   diagnostic.fifo_min, diagnostic.fifo_max, diagnostic.fifo_next,
                   diagnostic.fifo_stop, diagnostic.fifo_busy);
            printf("glxgears: Fence passed=%u issued=%u submitted=%llu completed=%llu\n",
                   diagnostic.fifo_fence, diagnostic.issued_fence,
                   (unsigned long long)diagnostic.submitted_frames,
                   (unsigned long long)diagnostic.completed_frames);
        }
    }
    gears_gpu_release(gpu);
    printf("glxgears: renderer=PortableGL software (%s, error=%d)\n", reason, result);
}

static void destroy_gears(void)
{
    struct gear **gears[3] = {&gear1, &gear2, &gear3};
    for (unsigned i = 0; i < 3; ++i) {
        struct gear *gear = *gears[i];
        if (!gear)
            continue;
        glDeleteBuffers(1, &gear->vbo);
        free(gear->vertices);
        free(gear->strips);
        free(gear);
        *gears[i] = NULL;
    }
}

static void gears_idle(const struct gears_gpu_backend *gpu)
{
    static unsigned long previous_ms;
    static unsigned long report_ms;
    static unsigned long frames;
    unsigned long now = leonos_uptime_ms();
    double dt;

    if (!previous_ms)
        previous_ms = now;
    dt = (double)(now - previous_ms) / 1000.0;
    previous_ms = now;

    angle += 70.0f * (GLfloat)dt;
    if (angle > 3600.0f)
        angle -= 3600.0f;

    ++frames;
    if (!report_ms)
        report_ms = now;
    if (now - report_ms >= 5000UL) {
        double seconds = (double)(now - report_ms) / 1000.0;
        printf("%lu frames in %3.1f seconds = %6.3f FPS\n",
               frames, seconds, seconds > 0.0 ? (double)frames / seconds : 0.0);
        if (gpu->handle) {
            struct leonos_gpu_info info = {.size = sizeof(info),
                                           .version = LEONOS_GPU_ABI_VERSION};
            int result = leonos_gpu_info(&info);
            if (result < 0) {
                printf("glxgears: GPU_INFO failed, error=%d\n", result);
            } else {
                printf("glxgears: SVGA3D device totals: generation=%u "
                       "submitted=%llu completed=%llu failed=%llu triangles=%llu\n",
                       info.generation, (unsigned long long)info.submitted_frames,
                       (unsigned long long)info.completed_frames,
                       (unsigned long long)info.failed_frames,
                       (unsigned long long)info.triangles);
            }
        }
        report_ms = now;
        frames = 0;
    }
}

static void handle_key_event(const struct leonos_gui_app_event *event)
{
    if (!event || event->type != LEONOS_GUI_APP_EVENT_KEY_DOWN || !event->pressed)
        return;
    switch (event->keycode) {
    case GLXGEARS_KEY_P:
        polygon_mode = (polygon_mode + 1) % 3;
        if (polygon_mode == 0)
            glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
        else if (polygon_mode == 1)
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        else
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        break;
    case LEONOS_KEY_LEFT:
        view_rot[1] += 5.0f;
        break;
    case LEONOS_KEY_RIGHT:
        view_rot[1] -= 5.0f;
        break;
    case LEONOS_KEY_UP:
        view_rot[0] += 5.0f;
        break;
    case LEONOS_KEY_DOWN:
        view_rot[0] -= 5.0f;
        break;
    default:
        break;
    }
}

int main(void)
{
    leonos_pgl_context *ctx;
    struct gears_gpu_backend gpu = {0};
    struct leonos_gui_app_event event = {0};
    GLuint vao;
    int width = WIDTH;
    int height = HEIGHT;
    int should_close = 0;
    int result;

    if (leonos_gui_connect() < 0) {
        puts("glxgears requires GUI mode");
        return 1;
    }
    ctx = leonos_pgl_create(width, height, "glxgears");
    if (!ctx) {
        puts("glxgears: unable to create GUI window");
        return 1;
    }

    leonos_pgl_make_current(ctx);
    polygon_mode = 2;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    gears_init();
    if (!gear1 || !gear2 || !gear3) {
        puts("glxgears: unable to create gear meshes");
        destroy_gears();
        leonos_pgl_destroy(ctx);
        return 1;
    }
    perspective(ProjectionMatrix, 60.0f, (GLfloat)width / (GLfloat)height,
                1.0f, 1024.0f);
    glViewport(0, 0, (GLint)width, (GLint)height);

    result = gears_gpu_start(&gpu, width, height);
    if (result < 0)
        use_software_renderer(&gpu, "SVGA3D unavailable or initialization failed", result);
    else
        puts("glxgears: renderer=VMware SVGA3D hardware");

    event.window_id = (uint32_t)leonos_pgl_window_id(ctx);
    while (!should_close) {
        while (leonos_gui_poll_app_event(&event) > 0) {
            int action = leonos_pgl_process_event(ctx, &event);
            if (action == LEONOS_PGL_EVENT_CLOSE) {
                should_close = 1;
                break;
            }
            if (action == LEONOS_PGL_EVENT_RESIZED) {
                width = (int)event.width;
                height = (int)event.height;
                if (height > 0) {
                    perspective(ProjectionMatrix, 60.0f,
                                (GLfloat)width / (GLfloat)height,
                                1.0f, 1024.0f);
                    glViewport(0, 0, (GLint)width, (GLint)height);
                }
                if (gpu.handle) {
                    result = gears_gpu_resize(&gpu, width, height);
                    if (result < 0)
                        use_software_renderer(&gpu, "SVGA3D resize failed", result);
                }
            }
            handle_key_event(&event);
        }
        if (should_close)
            break;

        leonos_pgl_make_current(ctx);
        gears_idle(&gpu);
        if (gpu.handle) {
            result = gears_gpu_draw(&gpu);
            if (result < 0)
                use_software_renderer(&gpu, "SVGA3D render failed", result);
        }
        if (gpu.handle) {
            if (leonos_gui_present_window((uint32_t)leonos_pgl_window_id(ctx),
                    gpu.width, gpu.height, gpu.width, gpu.pixels) <= 0)
                break;
        } else {
            gears_draw();
            if (leonos_pgl_present(ctx) < 0)
                break;
        }
        sleep_ms(1);
    }

    gears_gpu_release(&gpu);
    destroy_gears();
    glDeleteVertexArrays(1, &vao);
    leonos_pgl_destroy(ctx);
    return 0;
}
