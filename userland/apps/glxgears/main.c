/*
 * LeonOS frontend for PortableGL's upstream classic gears example.
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

#define GLXGEARS_KEY_P 25U

static void gears_idle(void)
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
    struct leonos_gui_app_event event = {0};
    GLuint vao;
    int width = WIDTH;
    int height = HEIGHT;
    int should_close = 0;

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
    perspective(ProjectionMatrix, 60.0f, (GLfloat)width / (GLfloat)height,
                1.0f, 1024.0f);
    glViewport(0, 0, (GLint)width, (GLint)height);

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
            }
            handle_key_event(&event);
        }
        if (should_close)
            break;

        leonos_pgl_make_current(ctx);
        gears_idle();
        gears_draw();
        if (leonos_pgl_present(ctx) < 0)
            break;
        sleep_ms(1);
    }

    leonos_pgl_destroy(ctx);
    return 0;
}
