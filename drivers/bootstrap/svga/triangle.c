#include "device.h"

/* Encode an unsigned pixel coordinate as binary32 using integer registers.
 * Round to nearest, ties to even, if the coordinate exceeds 24-bit precision.
 * The kernel's GPR-only ABI has no software floating-point runtime. */
static uint32_t vertex_float_bits(uint32_t value)
{
    if (!value) return 0;
    uint32_t exponent = 31u - (uint32_t)__builtin_clz(value);
    uint32_t significand;
    if (exponent <= 23u) {
        significand = value << (23u - exponent);
    } else {
        uint32_t shift = exponent - 23u;
        uint32_t remainder = value & ((1u << shift) - 1u);
        uint32_t halfway = 1u << (shift - 1u);
        significand = value >> shift;
        if (remainder > halfway || (remainder == halfway && (significand & 1u)))
            ++significand;
    }
    return ((exponent + 127u) << 23) + (significand - 0x800000u);
}

int svga3d_draw_triangle(svga_handle context, svga_handle target_surface,
                         svga_handle vertex_buffer, uint32_t target_width,
                         uint32_t target_height)
{
    if (!target_width || !target_height) return SVGA_EINVAL;
    uint64_t flags = svga_lock();
    int cid = svga_find_context(context), sid = svga_find_surface(target_surface),
        bid = svga_find_surface(vertex_buffer);
    int ret = (!svga_context_usable((uint32_t)cid) || !svga_surface_usable((uint32_t)sid) ||
               !svga_surface_usable((uint32_t)bid)) ? SVGA_EINVAL :
              (!svga.available ? SVGA_ENODEV : 0);
    if (!ret && (svga.contexts[cid].targets[SVGA3D_RT_COLOR0] != (uint32_t)sid ||
                 svga.surfaces[bid].desc.format != SVGA3D_BUFFER))
        ret = SVGA_EINVAL;
    if (!ret) {
        SVGA3dRenderState states[] = {
            {.state = SVGA3D_RS_ZENABLE, .uintValue = 0},
            {.state = SVGA3D_RS_ZWRITEENABLE, .uintValue = 0},
            {.state = SVGA3D_RS_LIGHTINGENABLE, .uintValue = 0},
            {.state = SVGA3D_RS_CULLMODE, .uintValue = SVGA3D_FACE_NONE},
            {.state = SVGA3D_RS_COLORWRITEENABLE, .uintValue = 0xf},
        };
        uint8_t state_bytes[sizeof(SVGA3dCmdSetRenderState) + sizeof(states)];
        SVGA3dCmdSetRenderState *state_cmd = (SVGA3dCmdSetRenderState *)state_bytes;
        state_cmd->cid = (uint32_t)cid;
        for (uint32_t i = 0; i < sizeof(states); ++i)
            state_bytes[sizeof(*state_cmd) + i] = ((const uint8_t *)states)[i];
        ret = svga_command_locked(SVGA_3D_CMD_SETRENDERSTATE, state_bytes,
                                  sizeof(state_bytes));
        if (!ret) {
            SVGA3dCmdSetViewport viewport = {(uint32_t)cid, {0, 0, target_width, target_height}};
            ret = svga_command_locked(SVGA_3D_CMD_SETVIEWPORT, &viewport, sizeof(viewport));
        }
        if (!ret) {
            SVGA3dCmdDrawPrimitives *draw;
            uint8_t draw_bytes[sizeof(SVGA3dCmdDrawPrimitives) +
                               2 * sizeof(SVGA3dVertexDecl) + sizeof(SVGA3dPrimitiveRange)];
            draw = (SVGA3dCmdDrawPrimitives *)draw_bytes;
            *draw = (SVGA3dCmdDrawPrimitives){(uint32_t)cid, 2, 1};
            SVGA3dVertexDecl *decl = (SVGA3dVertexDecl *)(draw_bytes + sizeof(*draw));
            decl[0] = (SVGA3dVertexDecl){
                {SVGA3D_DECLTYPE_FLOAT4, SVGA3D_DECLMETHOD_DEFAULT,
                 SVGA3D_DECLUSAGE_POSITIONT, 0},
                {(uint32_t)bid, 0, 20}, {0, 2}};
            decl[1] = (SVGA3dVertexDecl){
                {SVGA3D_DECLTYPE_D3DCOLOR, SVGA3D_DECLMETHOD_DEFAULT,
                 SVGA3D_DECLUSAGE_COLOR, 0},
                {(uint32_t)bid, 16, 20}, {0, 2}};
            SVGA3dPrimitiveRange *range = (SVGA3dPrimitiveRange *)
                (draw_bytes + sizeof(*draw) + 2 * sizeof(*decl));
            *range = (SVGA3dPrimitiveRange){SVGA3D_PRIMITIVE_TRIANGLELIST, 1,
                                            {SVGA3D_INVALID_ID, 0, 0}, 0, 0};
            ret = svga_command_locked(SVGA_3D_CMD_DRAW_PRIMITIVES, draw_bytes,
                                      sizeof(draw_bytes));
        }
    }
    svga_unlock(flags);
    return ret;
}

int svga3d_triangle_test(void)
{
    struct svga_info info;
    svga_get_info(&info);
    if (!info.available) return SVGA_ENODEV;
    svga_handle context = 0, target = 0, depth = 0, vertex = 0, buffer = 0;
    uint64_t mode_flags = svga_lock();
    uint32_t width = svga.ops.read(SVGA_REG_WIDTH);
    uint32_t height = svga.ops.read(SVGA_REG_HEIGHT);
    svga_unlock(mode_flags);
    if (!width || !height) { width = 800; height = 600; }
    struct svga_surface_desc color = {SVGA3D_X8R8G8B8, SVGA3D_SURFACE_HINT_RENDERTARGET,
                                      width, height, 1, 1};
    struct svga_surface_desc z = {SVGA3D_Z_D24S8, SVGA3D_SURFACE_HINT_DEPTHSTENCIL,
                                  width, height, 1, 1};
    struct svga_surface_desc vb = {SVGA3D_BUFFER, SVGA3D_SURFACE_HINT_VERTEXBUFFER,
                                   60, 1, 1, 1};
    uint32_t left = vertex_float_bits(width / 4u);
    uint32_t right = vertex_float_bits((uint32_t)((uint64_t)width * 3u / 4u));
    uint32_t center = vertex_float_bits(width / 2u);
    uint32_t top = vertex_float_bits((uint32_t)((uint64_t)height * 17u / 100u));
    uint32_t bottom = vertex_float_bits((uint32_t)((uint64_t)height * 83u / 100u));
    /* These DWORDs carry FLOAT4 POSITIONT bits, followed by D3DCOLOR. */
    struct { uint32_t x, y, z, rhw, color; } vertices[3] = {
        {left, top, 0x3f000000u, 0x3f800000u, 0xffff0000u},
        {right, top, 0x3f000000u, 0x3f800000u, 0xff00ff00u},
        {center, bottom, 0x3f000000u, 0x3f800000u, 0xff0000ffu},
    };
    int ret = svga3d_context_create(&context);
    if (!ret) ret = svga3d_surface_create(&color, &target);
    if (!ret) ret = svga3d_surface_create(&z, &depth);
    if (!ret) ret = svga3d_surface_create(&vb, &vertex);
    if (!ret) ret = svga_gmr_create(sizeof(vertices), &buffer);
    if (!ret) ret = svga_gmr_transfer(buffer, 0, vertices, sizeof(vertices), true);
    if (!ret) {
        struct svga_dma_box box = {0, 0, 0, sizeof(vertices), 1, 1, 0, 0, 0};
        ret = svga3d_surface_dma(vertex, 0, 0, buffer, 0, sizeof(vertices),
                                 SVGA3D_WRITE_HOST_VRAM, &box, 1);
    }
    if (!ret) ret = svga3d_set_render_target(context, SVGA3D_RT_COLOR0, target);
    if (!ret) ret = svga3d_set_render_target(context, SVGA3D_RT_DEPTH, depth);
    if (!ret) ret = svga3d_clear(context, SVGA3D_CLEAR_COLOR | SVGA3D_CLEAR_DEPTH,
                                 0xff202020u, 1.0f, 0);
    if (!ret) ret = svga3d_draw_triangle(context, target, vertex, color.width, color.height);
    if (!ret) { struct svga_present_rect rect = {0, 0, color.width, color.height, 0, 0};
                ret = svga3d_present(target, &rect, 1); }
    int saved = ret;
    if (context) { int e = svga3d_context_destroy(context); if (!saved && e) saved = e; }
    if (buffer) { int e = svga_gmr_destroy(buffer); if (!saved && e) saved = e; }
    if (vertex) { int e = svga3d_surface_destroy(vertex); if (!saved && e) saved = e; }
    if (depth) { int e = svga3d_surface_destroy(depth); if (!saved && e) saved = e; }
    if (target) { int e = svga3d_surface_destroy(target); if (!saved && e) saved = e; }
    return saved;
}
