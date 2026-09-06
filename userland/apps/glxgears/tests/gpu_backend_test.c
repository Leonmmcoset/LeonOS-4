#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <leonos/pgl.h>
#include <leonos/gpu_sdk.h>

#define main portablegl_upstream_main
#include "gears-upstream.c"
#undef main
#include "../gpu_backend.h"

static unsigned create_calls, destroy_calls, render_calls;
static int info_result, create_result, render_result;
static uint32_t gpu_flags = GPU_SDK_AVAILABLE;

int gpu_sdk_info(gpu_sdk_info_t *info)
{
    assert(info->size == sizeof(*info));
    assert(info->version == GPU_SDK_ABI_VERSION);
    info->flags = gpu_flags;
    return info_result;
}

int gpu_sdk_create(gpu_sdk_context_t *context)
{
    assert(context->size == sizeof(*context));
    assert(context->version == GPU_SDK_ABI_VERSION);
    assert(context->vertex_capacity == 2400);
    ++create_calls;
    context->handle = create_result ? 0 : create_calls;
    return create_result;
}

int gpu_sdk_destroy(uint64_t handle)
{
    assert(handle);
    ++destroy_calls;
    return 0;
}

int gpu_sdk_render(const gpu_sdk_frame_t *frame)
{
    assert(frame->size == sizeof(*frame));
    assert(frame->version == GPU_SDK_ABI_VERSION);
    assert(frame->vertex_count == 2400);
    assert(frame->draw_count == 3);
    assert(frame->pixel_capacity == 320 * 240);
    assert(frame->vertices && frame->draws && frame->pixels && frame->handle);
    assert(frame->fill_mode == (uint32_t)polygon_mode + 1);
    ++render_calls;
    return render_result;
}

void glGenBuffers(GLsizei count, GLuint *buffers)
{
    while (count-- > 0)
        *buffers++ = 1;
}
void glBindBuffer(GLenum target, GLuint buffer) { (void)target; (void)buffer; }
void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
    (void)target; (void)size; (void)data; (void)usage;
}

static void close_enough(float actual, float expected)
{
    assert(fabsf(actual - expected) < 0.0001f);
}

static void check_mesh_and_shader(struct gears_gpu_backend *gpu)
{
    struct gear *gears[] = {gear1, gear2, gear3};
    const float offsets[3][2] = {{-3.0f, -2.0f}, {3.1f, -2.0f}, {-3.1f, 4.2f}};
    const float colors[3][3] = {{0.8f, 0.1f, 0.0f}, {0.0f, 0.8f, 0.2f}, {0.2f, 0.2f, 1.0f}};
    const float rotations[] = {angle, -2 * angle - 9, -2 * angle - 25};
    float view[16];
    unsigned cursor = 0;
    identity(view);
    translate(view, 0, 0, -20);
    rotate(view, 2 * M_PI * view_rot[0] / 360, 1, 0, 0);
    rotate(view, 2 * M_PI * view_rot[1] / 360, 0, 1, 0);
    rotate(view, 2 * M_PI * view_rot[2] / 360, 0, 0, 1);

    for (unsigned gear_index = 0; gear_index < 3; ++gear_index) {
        struct gear *gear = gears[gear_index];
        My_Uniforms reference;
        memcpy(reference.normal_mat, view, sizeof(view));
        translate(reference.normal_mat, offsets[gear_index][0], offsets[gear_index][1], 0);
        rotate(reference.normal_mat, 2 * M_PI * rotations[gear_index] / 360, 0, 0, 1);
        memcpy(reference.mvp_mat, ProjectionMatrix, sizeof(reference.mvp_mat));
        multiply(reference.mvp_mat, reference.normal_mat);
        invert(reference.normal_mat);
        transpose(reference.normal_mat);
        memcpy(&reference.material_color, colors[gear_index], sizeof(reference.material_color));
        assert(gpu->draws[gear_index].first == cursor);
        for (int strip_index = 0; strip_index < gear->nstrips; ++strip_index) {
            struct vertex_strip *strip = &gear->strips[strip_index];
            for (int triangle = 0; triangle < strip->count - 2; ++triangle) {
                int indices[] = {triangle + (triangle & 1), triangle + !(triangle & 1), triangle + 2};
                const gpu_sdk_vertex_t *a = &gpu->vertices[cursor];
                const gpu_sdk_vertex_t *b = a + 1;
                const gpu_sdk_vertex_t *c = a + 2;
                vec3 ab = {b->x - a->x, b->y - a->y, b->z - a->z};
                vec3 ac = {c->x - a->x, c->y - a->y, c->z - a->z};
                vec3 face_normal = cross_v3s(ab, ac);
                const float *first_source = gpu->sources[cursor];
                vec3 source_normal = {first_source[3], first_source[4], first_source[5]};
                assert(dot_v3s(face_normal, source_normal) > 0);
                for (unsigned corner = 0; corner < 3; ++corner, ++cursor) {
                    const float *source = gear->vertices[strip->first + indices[corner]];
                    const gpu_sdk_vertex_t *vertex = &gpu->vertices[cursor];
                    vec4 attrs[] = {{source[0], source[1], source[2], 1}, {source[3], source[4], source[5], 1}};
                    Shader_Builtins builtins = {0};
                    float rgb[3];
                    vertex_shader(rgb, attrs, &builtins, &reference);
                    close_enough(vertex->x, source[0]);
                    close_enough(vertex->y, source[1]);
                    close_enough(vertex->z, source[2]);
                    vec4 clip = mult_m4_v4(gpu->draws[gear_index].transform, attrs[0]);
                    close_enough(clip.x, builtins.gl_Position.x);
                    close_enough(clip.y, builtins.gl_Position.y);
                    close_enough(clip.z, (builtins.gl_Position.z + builtins.gl_Position.w) * 0.5f);
                    close_enough(clip.w, builtins.gl_Position.w);
                    assert((vertex->color >> 24) == 255);
                    for (unsigned channel = 0; channel < 3; ++channel) {
                        float packed = (float)((vertex->color >> (16 - channel * 8)) & 255) / 255;
                        assert(fabsf(packed - rgb[channel]) <= 0.5f / 255 + 0.00001f);
                    }
                }
            }
        }
        assert(gpu->draws[gear_index].count == cursor - gpu->draws[gear_index].first);
    }
    assert(cursor == 2400);
}

int main(void)
{
    struct gears_gpu_backend gpu = {0};
    gear1 = create_gear(1, 4, 1, 20, 0.7f);
    gear2 = create_gear(0.5f, 2, 2, 10, 0.7f);
    gear3 = create_gear(1.3f, 2, 0.5f, 10, 0.7f);
    assert(gear1 && gear2 && gear3);
    perspective(ProjectionMatrix, 60, 320.0f / 240, 1, 1024);
    assert(gears_gpu_start(&gpu, 320, 240) == 0);
    for (int mode = 0; mode < 3; ++mode) {
        polygon_mode = mode;
        angle = 31.5f * mode;
        view_rot[0] += 5;
        view_rot[1] -= 15;
        assert(gears_gpu_draw(&gpu) == 0);
        check_mesh_and_shader(&gpu);
    }
    assert(render_calls == 3);
    assert(gears_gpu_resize(&gpu, 640, 480) == 0);
    assert(create_calls == 2 && destroy_calls == 1);
    create_result = -12;
    assert(gears_gpu_resize(&gpu, 320, 240) == -12);
    assert(!gpu.handle && !gpu.pixels);
    gears_gpu_release(&gpu);
    assert(!gpu.vertices && !gpu.sources && !gpu.pixels && !gpu.handle);
    assert(destroy_calls == 2);
    create_result = 0;
    assert(gears_gpu_start(&gpu, 320, 240) == 0);
    render_result = -5;
    assert(gears_gpu_draw(&gpu) == -5);
    gears_gpu_release(&gpu);
    assert(destroy_calls == 3);
    info_result = -38;
    assert(gears_gpu_start(&gpu, 320, 240) == -38);
    assert(!gpu.handle && !gpu.vertices && !gpu.pixels);
    info_result = 0;
    gpu_flags = 0;
    assert(gears_gpu_start(&gpu, 320, 240) == -19);
    assert(!gpu.handle && !gpu.vertices);
    for (unsigned i = 0; i < 3; ++i) {
        struct gear *gear = i == 0 ? gear1 : i == 1 ? gear2 : gear3;
        free(gear->vertices);
        free(gear->strips);
        free(gear);
    }
    puts("glxgears GPU mesh, shader equivalence, fill modes, and resource lifecycle: PASS");
    return 0;
}
