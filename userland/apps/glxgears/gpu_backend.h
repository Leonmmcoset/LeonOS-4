#ifndef LEONOS_GLXGEARS_GPU_BACKEND_H
#define LEONOS_GLXGEARS_GPU_BACKEND_H

#include <leonos/gpu.h>

/* Included after the upstream example to share its mesh and matrix routines. */
struct gears_gpu_backend {
    uint64_t handle;
    uint32_t width, height, vertex_count;
    struct leonos_gpu_vertex *vertices;
    const GLfloat **sources;
    uint32_t *pixels;
    struct leonos_gpu_draw draws[3];
};

static void gears_gpu_release_target(struct gears_gpu_backend *gpu)
{
    if (gpu->handle)
        leonos_gpu_destroy(gpu->handle);
    gpu->handle = 0;
    free(gpu->pixels);
    gpu->pixels = NULL;
    gpu->width = gpu->height = 0;
}

static void gears_gpu_release(struct gears_gpu_backend *gpu)
{
    gears_gpu_release_target(gpu);
    free(gpu->sources);
    free(gpu->vertices);
    memset(gpu, 0, sizeof(*gpu));
}

static int gears_gpu_build_mesh(struct gears_gpu_backend *gpu)
{
    const struct gear *gears[3] = {gear1, gear2, gear3};
    uint32_t total = 0;
    uint32_t cursor = 0;

    for (unsigned i = 0; i < 3; ++i) {
        const struct gear *gear = gears[i];
        if (!gear || !gear->vertices || !gear->strips)
            return -22;
        gpu->draws[i].first = total;
        for (int s = 0; s < gear->nstrips; ++s) {
            const struct vertex_strip *strip = &gear->strips[s];
            if (strip->first < 0 || strip->count < 3 ||
                strip->first > gear->nvertices ||
                strip->count > gear->nvertices - strip->first ||
                (uint32_t)(strip->count - 2) >
                    (LEONOS_GPU_MAX_VERTICES - total) / 3)
                return -22;
            total += (uint32_t)(strip->count - 2) * 3;
        }
        gpu->draws[i].count = total - gpu->draws[i].first;
        if (!gpu->draws[i].count)
            return -22;
    }
    gpu->vertices = calloc(total, sizeof(*gpu->vertices));
    gpu->sources = calloc(total, sizeof(*gpu->sources));
    if (!gpu->vertices || !gpu->sources)
        return -12;

    for (unsigned i = 0; i < 3; ++i) {
        const struct gear *gear = gears[i];
        for (int s = 0; s < gear->nstrips; ++s) {
            const struct vertex_strip *strip = &gear->strips[s];
            for (int triangle = 0; triangle < strip->count - 2; ++triangle) {
                /* Odd strip triangles reverse their first two vertices. */
                int indices[3] = {triangle + (triangle & 1),
                                  triangle + !(triangle & 1), triangle + 2};
                for (unsigned corner = 0; corner < 3; ++corner, ++cursor) {
                    const GLfloat *source = gear->vertices[strip->first + indices[corner]];
                    gpu->sources[cursor] = source;
                    gpu->vertices[cursor].x = source[0];
                    gpu->vertices[cursor].y = source[1];
                    gpu->vertices[cursor].z = source[2];
                }
            }
        }
    }
    gpu->vertex_count = total;
    return 0;
}

static int gears_gpu_resize(struct gears_gpu_backend *gpu, int width, int height)
{
    struct leonos_gpu_context context = {0};
    int result;

    gears_gpu_release_target(gpu);
    if (width <= 0 || width > (int)LEONOS_GPU_MAX_WIDTH ||
        height <= 0 || height > (int)LEONOS_GPU_MAX_HEIGHT)
        return -22;
    gpu->pixels = calloc((size_t)width * (size_t)height, sizeof(*gpu->pixels));
    if (!gpu->pixels)
        return -12;
    context.size = sizeof(context);
    context.version = LEONOS_GPU_ABI_VERSION;
    context.width = (uint32_t)width;
    context.height = (uint32_t)height;
    context.vertex_capacity = gpu->vertex_count;
    result = leonos_gpu_create(&context);
    if (result < 0 || !context.handle) {
        gears_gpu_release_target(gpu);
        return result < 0 ? result : -5;
    }
    gpu->handle = context.handle;
    gpu->width = context.width;
    gpu->height = context.height;
    return 0;
}

static int gears_gpu_start(struct gears_gpu_backend *gpu, int width, int height)
{
    struct leonos_gpu_info info = {0};
    int result;

    info.size = sizeof(info);
    info.version = LEONOS_GPU_ABI_VERSION;
    result = leonos_gpu_info(&info);
    if (result < 0) {
        printf("glxgears: GPU_INFO failed, error=%d\n", result);
        return result;
    }
    printf("glxgears: GPU_INFO result=0 flags=0x%x generation=%u\n",
           info.flags, info.generation);
    if (!(info.flags & LEONOS_GPU_AVAILABLE)) {
        puts("glxgears: GPU_INFO succeeded, but SVGA3D is not available");
        return -19;
    }
    result = gears_gpu_build_mesh(gpu);
    if (result == 0)
        result = gears_gpu_resize(gpu, width, height);
    if (result < 0)
        gears_gpu_release(gpu);
    return result;
}

static uint32_t gears_gpu_color_channel(GLfloat value)
{
    if (value <= 0)
        return 0;
    if (value >= 1)
        return 255;
    return (uint32_t)(value * 255.0f + 0.5f);
}

static void gears_gpu_prepare_draw(struct gears_gpu_backend *gpu, unsigned index,
                                   const GLfloat view[16], GLfloat x, GLfloat y,
                                   GLfloat rotation, const GLfloat color[3])
{
    struct leonos_gpu_draw *draw = &gpu->draws[index];
    GLfloat model_view[16], normal_matrix[16];
    const vec3 light_direction = {5.0f, 5.0f, 10.0f};
    vec3 light = norm_v3(light_direction);

    memcpy(model_view, view, sizeof(model_view));
    translate(model_view, x, y, 0);
    rotate(model_view, 2 * M_PI * rotation / 360.0, 0, 0, 1);
    memcpy(draw->transform, ProjectionMatrix, sizeof(draw->transform));
    multiply(draw->transform, model_view);
    /* Keep x/y/w unchanged and map OpenGL clip z [-w,w] to D3D [0,w]. */
    for (unsigned column = 0; column < 4; ++column)
        draw->transform[column * 4 + 2] =
            0.5f * (draw->transform[column * 4 + 2] + draw->transform[column * 4 + 3]);

    memcpy(normal_matrix, model_view, sizeof(normal_matrix));
    invert(normal_matrix);
    transpose(normal_matrix);
    for (uint32_t i = draw->first; i < draw->first + draw->count; ++i) {
        const GLfloat *source = gpu->sources[i];
        vec4 normal = {source[3], source[4], source[5], 1.0f};
        vec4 eye_normal = mult_m4_v4(normal_matrix, normal);
        vec3 direction = {eye_normal.x, eye_normal.y, eye_normal.z};
        GLfloat intensity = dot_v3s(norm_v3(direction), light);
        if (intensity < 0)
            intensity = 0;
        gpu->vertices[i].color = 0xff000000U |
            (gears_gpu_color_channel(color[0] * intensity) << 16) |
            (gears_gpu_color_channel(color[1] * intensity) << 8) |
            gears_gpu_color_channel(color[2] * intensity);
    }
}

static int gears_gpu_draw(struct gears_gpu_backend *gpu)
{
    static const GLfloat red[3] = {0.8f, 0.1f, 0.0f};
    static const GLfloat green[3] = {0.0f, 0.8f, 0.2f};
    static const GLfloat blue[3] = {0.2f, 0.2f, 1.0f};
    struct leonos_gpu_frame frame = {0};
    GLfloat view[16];

    identity(view);
    translate(view, 0, 0, -20);
    rotate(view, 2 * M_PI * view_rot[0] / 360.0, 1, 0, 0);
    rotate(view, 2 * M_PI * view_rot[1] / 360.0, 0, 1, 0);
    rotate(view, 2 * M_PI * view_rot[2] / 360.0, 0, 0, 1);
    gears_gpu_prepare_draw(gpu, 0, view, -3.0f, -2.0f, angle, red);
    gears_gpu_prepare_draw(gpu, 1, view, 3.1f, -2.0f, -2 * angle - 9.0f, green);
    gears_gpu_prepare_draw(gpu, 2, view, -3.1f, 4.2f, -2 * angle - 25.0f, blue);

    frame.size = sizeof(frame);
    frame.version = LEONOS_GPU_ABI_VERSION;
    frame.handle = gpu->handle;
    frame.vertices = (uint64_t)(uintptr_t)gpu->vertices;
    frame.draws = (uint64_t)(uintptr_t)gpu->draws;
    frame.pixels = (uint64_t)(uintptr_t)gpu->pixels;
    frame.vertex_count = gpu->vertex_count;
    frame.draw_count = 3;
    frame.pixel_capacity = gpu->width * gpu->height;
    frame.fill_mode = (uint32_t)polygon_mode + LEONOS_GPU_FILL_POINT;
    return leonos_gpu_render(&frame);
}

#endif
