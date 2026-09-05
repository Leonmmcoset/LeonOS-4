/* SPDX-License-Identifier: MIT
 * VMware SVGA GB wire definitions, adapted from Linux vmwgfx/device_include/
 * svga3d_cmd.h, svga3d_types.h and svga_reg.h (retrieved 2026-09-05).
 * Copyright 2012-2023 VMware, Inc., Palo Alto, CA., USA
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#ifndef LEONOS_SVGA_GB_PROTOCOL_H
#define LEONOS_SVGA_GB_PROTOCOL_H

#define SVGA_REG_SUGGESTED_GBOBJECT_MEM_SIZE_KB 51u
#define SVGA_REG_MOB_MAX_SIZE 57u
#define SVGA_3D_CMD_SET_OTABLE_BASE 1091u
#define SVGA_3D_CMD_DEFINE_GB_MOB 1093u
#define SVGA_3D_CMD_DESTROY_GB_MOB 1094u
#define SVGA_3D_CMD_DEFINE_GB_SURFACE 1097u
#define SVGA_3D_CMD_DESTROY_GB_SURFACE 1098u
#define SVGA_3D_CMD_BIND_GB_SURFACE 1099u
#define SVGA_3D_CMD_UPDATE_GB_SURFACE 1102u
#define SVGA_3D_CMD_INVALIDATE_GB_SURFACE 1106u
#define SVGA_3D_CMD_DEFINE_GB_CONTEXT 1107u
#define SVGA_3D_CMD_DESTROY_GB_CONTEXT 1108u
#define SVGA_3D_CMD_BIND_GB_CONTEXT 1109u
#define SVGA_OTABLE_MOB 0u
#define SVGA_OTABLE_SURFACE 1u
#define SVGA_OTABLE_CONTEXT 2u
#define SVGA_OTABLE_SHADER 3u
#define SVGA_OTABLE_SCREENTARGET 4u
#define SVGA_OTABLE_DX9_MAX 5u
#define SVGA3D_MOBFMT_INVALID UINT32_MAX
#define SVGA3D_MOBFMT_PT_0 0u
#define SVGA3D_MOBFMT_PT_1 1u
#define SVGA3D_MOBFMT_PT_2 2u

/* sizeof(SVGAGBContextData), verified against the upstream packed layout.
 * The host initializes this opaque state with BindGBContext.validContents=0. */
#define SVGA_GB_CONTEXT_BYTES 16384u
/* Packed OTable entry strides, not sizes of the DEFINE command payloads. */
#define SVGA_GB_MOB_ENTRY_BYTES 16u
#define SVGA_GB_SURFACE_ENTRY_BYTES 64u
#define SVGA_GB_CONTEXT_ENTRY_BYTES 8u
#define SVGA_GB_SHADER_ENTRY_BYTES 16u
#define SVGA_GB_SCREENTARGET_ENTRY_BYTES 64u

typedef struct {
    uint32_t type, baseAddress, sizeInBytes, validSizeInBytes, ptDepth;
} SVGA3dCmdSetOTableBase;
typedef struct {
    uint32_t mobid, ptDepth, base, sizeInBytes;
} SVGA3dCmdDefineGBMob;
typedef struct {
    uint32_t sid, surfaceFlags, format, numMipLevels, multisampleCount, autogenFilter;
    SVGA3dSize size;
} SVGA3dCmdDefineGBSurface;
typedef struct { uint32_t sid, mobid; } SVGA3dCmdBindGBSurface;
typedef struct { uint32_t cid, mobid, validContents; } SVGA3dCmdBindGBContext;

_Static_assert(sizeof(SVGA3dCmdSetOTableBase) == 20, "OTable wire size");
_Static_assert(sizeof(SVGA3dCmdDefineGBMob) == 16, "MOB wire size");
_Static_assert(sizeof(SVGA3dCmdDefineGBSurface) == 36, "GB surface wire size");
_Static_assert(sizeof(SVGA3dCmdBindGBSurface) == 8, "GB surface binding wire size");
_Static_assert(sizeof(SVGA3dCmdBindGBContext) == 12, "GB context binding wire size");
#endif
