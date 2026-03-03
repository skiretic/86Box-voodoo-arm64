/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Voodoo Vulkan bridge -- converts voodoo_params_t into
 *          VideoCommon ring commands (vertices + push constants).
 *
 *          This file runs on the FIFO thread (the Voodoo command
 *          processor) and pushes VC_CMD_TRIANGLE / VC_CMD_SWAP into
 *          the SPSC ring for the GPU thread to consume.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <math.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_voodoo_common.h>
#include <86box/vid_voodoo_regs.h>
#include <86box/vid_voodoo_texture.h>

/*
 * VideoCommon internal headers.  This file is compiled by src/video/
 * CMakeLists.txt, which adds the videocommon include path when
 * USE_VIDEOCOMMON is defined.
 */
#include "videocommon/vc_internal.h"
#include "videocommon/vc_thread.h"
#include "videocommon/vc_pipeline.h" /* vc_vertex_t, vc_push_constants_t */
#include "videocommon/vc_batch.h"    /* VC_CMD_TRIANGLE_SIZE */
#include "videocommon/vc_texture.h"  /* vc_tex_upload_payload_t, etc. */

/* -------------------------------------------------------------------------- */
/*  Logging                                                                    */
/* -------------------------------------------------------------------------- */

#ifdef ENABLE_VOODOO_VK_LOG
int voodoo_vk_do_log = ENABLE_VOODOO_VK_LOG;

static void
voodoo_vk_log(const char *fmt, ...)
{
    va_list ap;

    if (voodoo_vk_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define voodoo_vk_log(fmt, ...)
#endif

/* -------------------------------------------------------------------------- */
/*  Color conversion constants                                                 */
/* -------------------------------------------------------------------------- */

/* Voodoo colors are 12.12 fixed-point (start values) with per-pixel
   gradients.  To convert to float [0..1]:
     color_float = (int32_t)start / (4096.0f * 255.0f) */
#define VC_COLOR_SCALE (4096.0f * 255.0f)

/* W (1/W) is 18.32 fixed-point (int64_t). */
#define VC_W_SCALE (4294967296.0) /* 2^32 */

/* TMU S/T/W are 18.32 fixed-point (int64_t). */
#define VC_ST_SCALE (4294967296.0) /* 2^32 */

/* Z is 20.12 fixed-point (uint32_t start, int32_t gradients).
   After >> 12 the result is a 16-bit depth value (0-65535). */
#define VC_Z_SCALE (4096.0f)         /* 2^12, to convert 20.12 -> integer */
#define VC_Z_MAX   (65535.0f)        /* Voodoo depth range */

/* -------------------------------------------------------------------------- */
/*  Per-Voodoo texture tracking state                                          */
/* -------------------------------------------------------------------------- */

/*
 * We track texture identity per-TMU to avoid redundant uploads.
 * Identity is: texBaseAddr + tLOD + palette_checksum.
 * When any of these change, we re-upload.
 */
typedef struct voodoo_vk_tex_track_t {
    uint32_t addr;
    uint32_t tLOD;
    uint32_t pal_checksum;
    int      slot;             /* tex_entry index currently bound */
    int      last_upload_slot; /* Last slot we uploaded to (for identity compare). */
    uint32_t last_identity;    /* Quick hash of last uploaded texture. */
} voodoo_vk_tex_track_t;

/* Diagnostic counter for producer-side triangle pushes. */


/* Per-device VK bridge state (texture tracking, allocated lazily). */
typedef struct voodoo_vk_state_t {
    voodoo_vk_tex_track_t tex_track[2];
    uint32_t fog_checksum;  /* XOR checksum of fogTable[64] for dedup. */
    int      fog_uploaded;  /* 1 = has been uploaded at least once. */
} voodoo_vk_state_t;

static voodoo_vk_state_t *
voodoo_vk_get_state(voodoo_t *voodoo)
{
    if (!voodoo->vc_vk_state) {
        voodoo_vk_state_t *st = (voodoo_vk_state_t *) calloc(1, sizeof(voodoo_vk_state_t));
        st->tex_track[0].addr = ~0u;
        st->tex_track[0].tLOD = ~0u;
        st->tex_track[0].pal_checksum = ~0u;
        st->tex_track[0].slot = -1;
        st->tex_track[0].last_upload_slot = -1;
        st->tex_track[1].addr = ~0u;
        st->tex_track[1].tLOD = ~0u;
        st->tex_track[1].pal_checksum = ~0u;
        st->tex_track[1].slot = -1;
        st->tex_track[1].last_upload_slot = -1;
        voodoo->vc_vk_state = st;
    }
    return (voodoo_vk_state_t *) voodoo->vc_vk_state;
}

/* -------------------------------------------------------------------------- */
/*  Depth helpers                                                              */
/* -------------------------------------------------------------------------- */

/*
 * voodoo_w_to_depth -- convert Voodoo W (int64_t, 18.32 fixed-point) to a
 * 16-bit depth value using the same log-space conversion as the SW renderer
 * (voodoo_fls).  This is the W-buffer depth encoding.
 */
static int
vk_fls(uint16_t val)
{
    int num = 0;

    if (!(val & 0xff00)) {
        num += 8;
        val <<= 8;
    }
    if (!(val & 0xf000)) {
        num += 4;
        val <<= 4;
    }
    if (!(val & 0xc000)) {
        num += 2;
        val <<= 2;
    }
    if (!(val & 0x8000)) {
        num += 1;
        val <<= 1;
    }
    return num;
}

static uint16_t
voodoo_w_to_depth(int64_t w)
{
    int w_depth;

    if (w & UINT64_C(0xffff00000000))
        w_depth = 0;
    else if (!(w & UINT64_C(0xffff0000)))
        w_depth = 0xf001;
    else {
        int exp  = vk_fls((uint16_t) ((uint32_t) w >> 16));
        int mant = (~(uint32_t) w >> (19 - exp)) & 0xfff;
        w_depth  = (exp << 12) + mant + 1;
        if (w_depth > 0xffff)
            w_depth = 0xffff;
    }
    return (uint16_t) w_depth;
}

/*
 * Compute per-vertex Vulkan depth [0,1] from Voodoo Z or W gradients.
 *
 * Z-buffer mode: depth iterates linearly (20.12 fixed-point) -> 16-bit.
 *   Per-vertex linear interpolation on GPU is exact.
 *
 * W-buffer mode: depth = log-space conversion of W.
 *   Per-vertex is an approximation (true W-buffer is per-pixel nonlinear),
 *   but much better than z=0.5 for everything.
 *
 * depth_source override: constant depth from zaColor register.
 * depth_bias: signed 16-bit offset added before normalization.
 */
static float
voodoo_z_to_float(uint32_t fbzMode, uint32_t zaColor,
                  int64_t z_or_w, int is_w_buffer)
{
    int depth16;

    if (is_w_buffer) {
        depth16 = voodoo_w_to_depth(z_or_w);
    } else {
        /* Z-buffer: 20.12 fixed -> 16-bit integer. */
        int32_t z_int = (int32_t) (z_or_w >> 12);
        depth16 = (z_int < 0) ? 0 : ((z_int > 65535) ? 65535 : z_int);
    }

    /* Depth bias: add signed 16-bit zaColor offset. */
    if (fbzMode & FBZ_DEPTH_BIAS) {
        depth16 += (int16_t) zaColor;
        if (depth16 < 0)
            depth16 = 0;
        if (depth16 > 65535)
            depth16 = 65535;
    }

    return (float) depth16 / VC_Z_MAX;
}

/* -------------------------------------------------------------------------- */
/*  Vertex reconstruction from gradients                                       */
/* -------------------------------------------------------------------------- */

static void
voodoo_vk_extract_vertices(const voodoo_params_t *p, vc_vertex_t verts[3])
{
    /* Positions: 12.4 fixed-point -> pixel float. */
    float xA = (float) p->vertexAx / 16.0f;
    float yA = (float) p->vertexAy / 16.0f;
    float xB = (float) p->vertexBx / 16.0f;
    float yB = (float) p->vertexBy / 16.0f;
    float xC = (float) p->vertexCx / 16.0f;
    float yC = (float) p->vertexCy / 16.0f;

    double dx_ba = (double) xB - (double) xA;
    double dy_ba = (double) yB - (double) yA;
    double dx_ca = (double) xC - (double) xA;
    double dy_ca = (double) yC - (double) yA;

    /* FBZ_PARAM_ADJUST: subpixel correction for vertex A start values.
       The SW renderer snaps the starting scanline to pixel centers and
       adjusts iterated parameters accordingly.  dx/dy are the subpixel
       offsets from vertex A to the nearest pixel center, in 12.4 units. */
    int32_t adj_dx = 0, adj_dy = 0;
    if (p->fbzColorPath & FBZ_PARAM_ADJUST) {
        adj_dx = 8 - (p->vertexAx & 0xf);
        if ((p->vertexAx & 0xf) > 8)
            adj_dx += 16;
        adj_dy = 8 - (p->vertexAy & 0xf);
        if ((p->vertexAy & 0xf) > 8)
            adj_dy += 16;
    }

    /* Color A (start values), with optional subpixel adjustment. */
    int32_t startR = (int32_t) p->startR;
    int32_t startG = (int32_t) p->startG;
    int32_t startB = (int32_t) p->startB;
    int32_t startA = (int32_t) p->startA;
    if (p->fbzColorPath & FBZ_PARAM_ADJUST) {
        startR += (adj_dx * p->dRdX + adj_dy * p->dRdY) >> 4;
        startG += (adj_dx * p->dGdX + adj_dy * p->dGdY) >> 4;
        startB += (adj_dx * p->dBdX + adj_dy * p->dBdY) >> 4;
        startA += (adj_dx * p->dAdX + adj_dy * p->dAdY) >> 4;
    }
    float rA = (float) startR / VC_COLOR_SCALE;
    float gA = (float) startG / VC_COLOR_SCALE;
    float bA = (float) startB / VC_COLOR_SCALE;
    float aA = (float) startA / VC_COLOR_SCALE;

    /* Color B = A + gradient * delta (double intermediates for precision). */
    float rB = rA + (float) ((double) p->dRdX * dx_ba + (double) p->dRdY * dy_ba) / VC_COLOR_SCALE;
    float gB = gA + (float) ((double) p->dGdX * dx_ba + (double) p->dGdY * dy_ba) / VC_COLOR_SCALE;
    float bB = bA + (float) ((double) p->dBdX * dx_ba + (double) p->dBdY * dy_ba) / VC_COLOR_SCALE;
    float aB = aA + (float) ((double) p->dAdX * dx_ba + (double) p->dAdY * dy_ba) / VC_COLOR_SCALE;

    /* Color C = A + gradient * delta (double intermediates for precision). */
    float rC = rA + (float) ((double) p->dRdX * dx_ca + (double) p->dRdY * dy_ca) / VC_COLOR_SCALE;
    float gC = gA + (float) ((double) p->dGdX * dx_ca + (double) p->dGdY * dy_ca) / VC_COLOR_SCALE;
    float bC = bA + (float) ((double) p->dBdX * dx_ca + (double) p->dBdY * dy_ca) / VC_COLOR_SCALE;
    float aC = aA + (float) ((double) p->dAdX * dx_ca + (double) p->dAdY * dy_ca) / VC_COLOR_SCALE;

    /* 1/W: 18.32 fixed-point (int64_t).  startW IS 1/W (not W).
       Apply subpixel adjustment in fixed-point before float conversion. */
    int64_t adj_startW = p->startW;
    if (p->fbzColorPath & FBZ_PARAM_ADJUST)
        adj_startW += ((int64_t) adj_dx * p->dWdX + (int64_t) adj_dy * p->dWdY) >> 4;
    double oowA = (double) adj_startW / VC_W_SCALE;
    double oowB = oowA + ((double) p->dWdX * dx_ba + (double) p->dWdY * dy_ba) / VC_W_SCALE;
    double oowC = oowA + ((double) p->dWdX * dx_ca + (double) p->dWdY * dy_ca) / VC_W_SCALE;

    /* TMU S/T/W: 18.32 fixed-point.
       Populate both TMU0 and TMU1 vertex fields independently so the shader
       can access both texture units for multi-texture rendering (Phase 6).
       Each TMU is populated if its textureMode enable bit is set. */
    int textured = (p->fbzColorPath & FBZCP_TEXTURE_ENABLED) ? 1 : 0;
    int tmu0_enabled = (p->textureMode[0] & 1) ? 1 : 0;
    int tmu1_enabled = (p->textureMode[1] & 1) ? 1 : 0;
    double s0A = 0.0, t0A = 0.0, w0A = 0.0;
    double s0B = 0.0, t0B = 0.0, w0B = 0.0;
    double s0C = 0.0, t0C = 0.0, w0C = 0.0;
    double s1A = 0.0, t1A = 0.0, w1A = 0.0;
    double s1B = 0.0, t1B = 0.0, w1B = 0.0;
    double s1C = 0.0, t1C = 0.0, w1C = 0.0;

    if (textured && tmu0_enabled) {
        int64_t adj_s = p->tmu[0].startS;
        int64_t adj_t = p->tmu[0].startT;
        int64_t adj_tw = p->tmu[0].startW;
        if (p->fbzColorPath & FBZ_PARAM_ADJUST) {
            adj_s += ((int64_t) adj_dx * p->tmu[0].dSdX + (int64_t) adj_dy * p->tmu[0].dSdY) >> 4;
            adj_t += ((int64_t) adj_dx * p->tmu[0].dTdX + (int64_t) adj_dy * p->tmu[0].dTdY) >> 4;
            adj_tw += ((int64_t) adj_dx * p->tmu[0].dWdX + (int64_t) adj_dy * p->tmu[0].dWdY) >> 4;
        }
        s0A = (double) adj_s / VC_ST_SCALE;
        t0A = (double) adj_t / VC_ST_SCALE;
        w0A = (double) adj_tw / VC_ST_SCALE;

        s0B = s0A + ((double) p->tmu[0].dSdX * dx_ba + (double) p->tmu[0].dSdY * dy_ba) / VC_ST_SCALE;
        t0B = t0A + ((double) p->tmu[0].dTdX * dx_ba + (double) p->tmu[0].dTdY * dy_ba) / VC_ST_SCALE;
        w0B = w0A + ((double) p->tmu[0].dWdX * dx_ba + (double) p->tmu[0].dWdY * dy_ba) / VC_ST_SCALE;

        s0C = s0A + ((double) p->tmu[0].dSdX * dx_ca + (double) p->tmu[0].dSdY * dy_ca) / VC_ST_SCALE;
        t0C = t0A + ((double) p->tmu[0].dTdX * dx_ca + (double) p->tmu[0].dTdY * dy_ca) / VC_ST_SCALE;
        w0C = w0A + ((double) p->tmu[0].dWdX * dx_ca + (double) p->tmu[0].dWdY * dy_ca) / VC_ST_SCALE;
    }

    if (textured && tmu1_enabled) {
        int64_t adj_s = p->tmu[1].startS;
        int64_t adj_t = p->tmu[1].startT;
        int64_t adj_tw = p->tmu[1].startW;
        if (p->fbzColorPath & FBZ_PARAM_ADJUST) {
            adj_s += ((int64_t) adj_dx * p->tmu[1].dSdX + (int64_t) adj_dy * p->tmu[1].dSdY) >> 4;
            adj_t += ((int64_t) adj_dx * p->tmu[1].dTdX + (int64_t) adj_dy * p->tmu[1].dTdY) >> 4;
            adj_tw += ((int64_t) adj_dx * p->tmu[1].dWdX + (int64_t) adj_dy * p->tmu[1].dWdY) >> 4;
        }
        s1A = (double) adj_s / VC_ST_SCALE;
        t1A = (double) adj_t / VC_ST_SCALE;
        w1A = (double) adj_tw / VC_ST_SCALE;

        s1B = s1A + ((double) p->tmu[1].dSdX * dx_ba + (double) p->tmu[1].dSdY * dy_ba) / VC_ST_SCALE;
        t1B = t1A + ((double) p->tmu[1].dTdX * dx_ba + (double) p->tmu[1].dTdY * dy_ba) / VC_ST_SCALE;
        w1B = w1A + ((double) p->tmu[1].dWdX * dx_ba + (double) p->tmu[1].dWdY * dy_ba) / VC_ST_SCALE;

        s1C = s1A + ((double) p->tmu[1].dSdX * dx_ca + (double) p->tmu[1].dSdY * dy_ca) / VC_ST_SCALE;
        t1C = t1A + ((double) p->tmu[1].dTdX * dx_ca + (double) p->tmu[1].dTdY * dy_ca) / VC_ST_SCALE;
        w1C = w1A + ((double) p->tmu[1].dWdX * dx_ca + (double) p->tmu[1].dWdY * dy_ca) / VC_ST_SCALE;
    }

    /* TMU1 coords stay in their own vertex fields (vTexCoord1).
       The shader samples tex_tmu1 via vTexCoord1 when only TMU1 is active,
       or when both TMUs are active (dual-TMU mode). */

    /* Depth: compute per-vertex Z from Voodoo gradients.
     *
     * Z-buffer mode: startZ (uint32_t, 20.12 fixed-point) + dZdX/dZdY (int32_t).
     * W-buffer mode: startW (int64_t, 18.32) + dWdX/dWdY (int64_t), log-space.
     * depth_source override: constant depth from zaColor register.
     */
    float zA, zB, zC;
    int is_w_buffer = (p->fbzMode & FBZ_W_BUFFER) ? 1 : 0;

    if (p->fbzMode & FBZ_DEPTH_SOURCE) {
        /* Depth source override: use zaColor as constant depth for all verts. */
        float z_const = (float) (p->zaColor & 0xffff) / VC_Z_MAX;
        zA = zB = zC = z_const;
    } else if (is_w_buffer) {
        /* W-buffer mode: compute per-vertex W, then convert via fls.
           adj_startW already has subpixel correction applied. */
        int64_t wA_fp = adj_startW;
        int64_t wB_fp = wA_fp + (int64_t) ((double) p->dWdX * dx_ba + (double) p->dWdY * dy_ba);
        int64_t wC_fp = wA_fp + (int64_t) ((double) p->dWdX * dx_ca + (double) p->dWdY * dy_ca);

        zA = voodoo_z_to_float(p->fbzMode, p->zaColor, wA_fp, 1);
        zB = voodoo_z_to_float(p->fbzMode, p->zaColor, wB_fp, 1);
        zC = voodoo_z_to_float(p->fbzMode, p->zaColor, wC_fp, 1);
    } else {
        /* Z-buffer mode: linear iteration of Z (20.12 fixed-point).
           Apply subpixel adjustment to Z start value. */
        int64_t zA_fp = (int64_t) p->startZ;
        if (p->fbzColorPath & FBZ_PARAM_ADJUST)
            zA_fp += ((int64_t) adj_dx * p->dZdX + (int64_t) adj_dy * p->dZdY) >> 4;
        int64_t zB_fp = zA_fp + (int64_t) ((double) p->dZdX * dx_ba + (double) p->dZdY * dy_ba);
        int64_t zC_fp = zA_fp + (int64_t) ((double) p->dZdX * dx_ca + (double) p->dZdY * dy_ca);

        zA = voodoo_z_to_float(p->fbzMode, p->zaColor, zA_fp, 0);
        zB = voodoo_z_to_float(p->fbzMode, p->zaColor, zB_fp, 0);
        zC = voodoo_z_to_float(p->fbzMode, p->zaColor, zC_fp, 0);
    }

    /* Vertex A. */
    memset(&verts[0], 0, sizeof(vc_vertex_t));
    verts[0].x  = xA;
    verts[0].y  = yA;
    verts[0].z  = zA;
    verts[0].w  = (float) oowA;
    verts[0].r  = rA;
    verts[0].g  = gA;
    verts[0].b  = bA;
    verts[0].a  = aA;
    verts[0].s0 = (float) s0A;
    verts[0].t0 = (float) t0A;
    verts[0].w0 = (float) w0A;
    verts[0].s1 = (float) s1A;
    verts[0].t1 = (float) t1A;
    verts[0].w1 = (float) w1A;

    /* Vertex B. */
    memset(&verts[1], 0, sizeof(vc_vertex_t));
    verts[1].x  = xB;
    verts[1].y  = yB;
    verts[1].z  = zB;
    verts[1].w  = (float) oowB;
    verts[1].r  = rB;
    verts[1].g  = gB;
    verts[1].b  = bB;
    verts[1].a  = aB;
    verts[1].s0 = (float) s0B;
    verts[1].t0 = (float) t0B;
    verts[1].w0 = (float) w0B;
    verts[1].s1 = (float) s1B;
    verts[1].t1 = (float) t1B;
    verts[1].w1 = (float) w1B;

    /* Vertex C. */
    memset(&verts[2], 0, sizeof(vc_vertex_t));
    verts[2].x  = xC;
    verts[2].y  = yC;
    verts[2].z  = zC;
    verts[2].w  = (float) oowC;
    verts[2].r  = rC;
    verts[2].g  = gC;
    verts[2].b  = bC;
    verts[2].a  = aC;
    verts[2].s0 = (float) s0C;
    verts[2].t0 = (float) t0C;
    verts[2].w0 = (float) w0C;
    verts[2].s1 = (float) s1C;
    verts[2].t1 = (float) t1C;
    verts[2].w1 = (float) w1C;
}

/* -------------------------------------------------------------------------- */
/*  Push constant extraction                                                   */
/* -------------------------------------------------------------------------- */

static void
voodoo_vk_extract_push_constants(const voodoo_params_t *p,
                                 vc_push_constants_t *pc,
                                 uint32_t fb_w, uint32_t fb_h)
{
    pc->fbzMode       = p->fbzMode;
    pc->fbzColorPath  = p->fbzColorPath;
    pc->alphaMode     = p->alphaMode;
    pc->fogMode       = p->fogMode;
    pc->textureMode0  = p->textureMode[0];
    pc->textureMode1  = p->textureMode[1];
    pc->color0        = p->color0;
    pc->color1        = p->color1;
    pc->chromaKey      = p->chromaKey;
    /* Pack fogColor as 0x00RRGGBB for the shader's unpackRGB().
       Voodoo fogColor is stored as {r, g, b} in rgbvoodoo_t. */
    pc->fogColor       = ((uint32_t) p->fogColor.r << 16)
                       | ((uint32_t) p->fogColor.g << 8)
                       | ((uint32_t) p->fogColor.b);
    pc->zaColor        = p->zaColor;
    pc->stipple        = p->stipple;
    pc->detail0        = 0;
    pc->detail1        = 0;
    pc->fb_width       = (float) fb_w;
    pc->fb_height      = (float) fb_h;
}

/* -------------------------------------------------------------------------- */
/*  Texture upload: push decoded RGBA8 to GPU thread                           */
/* -------------------------------------------------------------------------- */

static void
voodoo_vk_push_texture(voodoo_t *voodoo, voodoo_params_t *params, int tmu)
{
    vc_ctx_t *ctx = (vc_ctx_t *) voodoo->vc_ctx;
    if (!ctx)
        return;

    int tex_entry = params->tex_entry[tmu];
    if (tex_entry < 0 || tex_entry >= TEX_CACHE_MAX) {
        VC_LOG("VideoCommon: push_texture bail tex_entry=%d out of range (tmu=%d)\n", tex_entry, tmu);
        return;
    }

    texture_t *tc = &voodoo->texture_cache[tmu][tex_entry];

    /* Check if this texture is already uploaded using direct field comparison
       (more robust than the previous XOR hash which could collide). */
    voodoo_vk_state_t     *vk_st = voodoo_vk_get_state(voodoo);
    voodoo_vk_tex_track_t *trk   = &vk_st->tex_track[tmu];
    if (trk->slot == tex_entry
        && trk->addr == tc->base
        && trk->tLOD == tc->tLOD
        && trk->pal_checksum == tc->palette_checksum) {
        return; /* Already uploaded and bound. */
    }

    /* Determine base mip dimensions.  The texture cache stores decoded mips
       from lod_min through lod_max, where lod_min = the finest (largest) LOD
       level available.  Use LOD 0 dimensions (full-size texture) when the
       cache has LOD 0 data (lod_min == 0), otherwise fall back to the largest
       available mip at lod_min.  The data offset always starts at
       texture_offset[lod_min] regardless. */
    int lod_min = (tc->tLOD >> 2) & 15;
    if (lod_min > 8) lod_min = 8;

    /* Use the base mip that IS available in the cache. */
    int upload_lod = lod_min;
    uint32_t width  = params->tex_w_mask[tmu][upload_lod] + 1;
    uint32_t height = params->tex_h_mask[tmu][upload_lod] + 1;
    if (width == 0 || height == 0 || width > 256 || height > 256) {
        VC_LOG("VideoCommon: push_texture bail bad dims %ux%u tmu=%d entry=%d lod=%d\n",
               width, height, tmu, tex_entry, upload_lod);
        return;
    }

    /* Copy decoded RGBA8 data from texture cache.
       The data array layout: texture_offset[lod] gives the uint32 offset
       for each LOD level. */
    uint32_t pixel_count = width * height;
    uint32_t byte_size   = pixel_count * 4;

    uint8_t *data = (uint8_t *) malloc(byte_size);
    if (!data)
        return;

    /* Copy from the texture cache data buffer at the correct LOD offset.
       The cache data is stored as 0xAARRGGBB (makergba format).
       We need to repack to RGBA8 for VK_FORMAT_R8G8B8A8_UNORM. */
    const uint32_t *src = &tc->data[texture_offset[upload_lod]];
    uint8_t        *dst = data;
    for (uint32_t i = 0; i < pixel_count; i++) {
        uint32_t c = src[i];
        /* makergba packs as (b) | (g << 8) | (r << 16) | (a << 24)
           So: byte 0 = R (bits 16-23), byte 1 = G (bits 8-15),
               byte 2 = B (bits 0-7), byte 3 = A (bits 24-31) */
        dst[0] = (c >> 16) & 0xFF; /* R */
        dst[1] = (c >>  8) & 0xFF; /* G */
        dst[2] = (c      ) & 0xFF; /* B */
        dst[3] = (c >> 24) & 0xFF; /* A */
        dst += 4;
    }

    /* Compute identity from the cache entry fields (used by GPU-side tracking). */
    uint32_t identity = tc->base ^ tc->tLOD ^ tc->palette_checksum;

    /* Push upload command -- GPU thread takes ownership of data pointer. */
    uint16_t cmd_size = (uint16_t) (sizeof(vc_ring_cmd_header_t)
                                  + sizeof(vc_tex_upload_payload_t));
    vc_tex_upload_payload_t *up = (vc_tex_upload_payload_t *)
        vc_ring_reserve(&ctx->ring, VC_CMD_TEXTURE_UPLOAD, cmd_size);

    up->tmu      = (uint32_t) tmu;
    up->slot     = (uint32_t) tex_entry;
    up->width    = width;
    up->height   = height;
    up->identity = identity;
    up->pad      = 0;
    up->data_ptr = (uint64_t) (uintptr_t) data;

    vc_ring_commit_and_wake(&ctx->ring);

    /* Push bind command. */
    uint16_t bind_size = (uint16_t) (sizeof(vc_ring_cmd_header_t)
                                   + sizeof(vc_tex_bind_payload_t));
    vc_tex_bind_payload_t *bp = (vc_tex_bind_payload_t *)
        vc_ring_reserve(&ctx->ring, VC_CMD_TEXTURE_BIND, bind_size);

    bp->tmu         = (uint32_t) tmu;
    bp->slot        = (uint32_t) tex_entry;
    bp->sampler_key = vc_texture_sampler_key(params->textureMode[tmu]);
    bp->pad         = 0;

    vc_ring_commit_and_wake(&ctx->ring);

    /* Update tracking with actual cache fields (not the XOR hash). */
    trk->addr          = tc->base;
    trk->tLOD          = tc->tLOD;
    trk->pal_checksum  = tc->palette_checksum;
    trk->slot          = tex_entry;
    trk->last_upload_slot = tex_entry;
    trk->last_identity = identity;

    VC_LOG("VideoCommon: tex push tmu=%d slot=%d %ux%u lod_min=%d\n",
           tmu, tex_entry, width, height, lod_min);
}

/* -------------------------------------------------------------------------- */
/*  Public API: push triangle to ring                                          */
/* -------------------------------------------------------------------------- */

void
voodoo_vk_push_triangle(voodoo_t *voodoo, voodoo_params_t *params)
{
    vc_ctx_t *ctx = (vc_ctx_t *) voodoo->vc_ctx;
    if (!ctx)
        return;

    /* Upload fog table if fog is enabled and table has changed. */
    if (params->fogMode & FOG_ENABLE) {
        voodoo_vk_state_t *vk_st = voodoo_vk_get_state(voodoo);
        uint32_t checksum = 0;
        for (int i = 0; i < 64; i++)
            checksum ^= ((uint32_t) params->fogTable[i].fog << 8)
                       | (uint32_t) params->fogTable[i].dfog
                       | ((uint32_t) i << 16);
        if (!vk_st->fog_uploaded || checksum != vk_st->fog_checksum) {
            /* Push VC_CMD_FOG_UPLOAD with 128 bytes of fog table data. */
            uint16_t cmd_size = sizeof(vc_ring_cmd_header_t) + 128;
            void *payload = vc_ring_reserve(&ctx->ring, VC_CMD_FOG_UPLOAD, cmd_size);
            memcpy(payload, params->fogTable, 128);
            vc_ring_commit_and_wake(&ctx->ring);
            vk_st->fog_checksum = checksum;
            vk_st->fog_uploaded = 1;
        }
    }

    /* Handle texture if textured.
       On Voodoo 2, TMU 1 is the upstream texture unit — when only a single
       texture is active, the driver programs TMU 1 (not TMU 0).
       For dual-TMU: push both TMU0 and TMU1 textures. */
    if (params->fbzColorPath & FBZCP_TEXTURE_ENABLED) {
        int tmu0_act = (params->textureMode[0] & 1) ? 1 : 0;
        int tmu1_act = (params->textureMode[1] & 1) ? 1 : 0;

        if (tmu0_act && tmu1_act) {
            /* Dual-TMU: upload/bind TMU1 first (upstream), then TMU0.
               TMU1 bind as tmu=1 writes to binding 1.
               TMU0 bind as tmu=0 writes to binding 0 and includes TMU1
               at binding 1 in the same descriptor set. */
            voodoo_vk_push_texture(voodoo, params, 1);
            voodoo_vk_push_texture(voodoo, params, 0);
        } else if (tmu1_act) {
            /* Single-TMU via TMU1: push with tmu=1.
               The shader samples tex_tmu1 via vTexCoord1 directly. */
            voodoo_vk_push_texture(voodoo, params, 1);
        } else if (tmu0_act) {
            /* Single-TMU via TMU0. */
            voodoo_vk_push_texture(voodoo, params, 0);
        }
    }

    /* Balance refcount_r for both TMUs unconditionally, matching the
       unconditional voodoo_use_texture calls in voodoo_queue_triangle
       and the SW renderer's unconditional increments (vid_voodoo_render.c:1628-1629).
       Only incrementing the active TMU caused the inactive TMU's refcount
       to drift ahead of refcount_r, eventually spinning voodoo_use_texture's
       eviction loop forever after 64 unique textures. */
    voodoo->texture_cache[0][params->tex_entry[0]].refcount_r[0]++;
    if (voodoo->dual_tmus)
        voodoo->texture_cache[1][params->tex_entry[1]].refcount_r[0]++;

    vc_vertex_t verts[3];
    voodoo_vk_extract_vertices(params, verts);

    uint32_t fb_w = atomic_load_explicit(&ctx->fb_width, memory_order_acquire);
    uint32_t fb_h = atomic_load_explicit(&ctx->fb_height, memory_order_acquire);
    if (fb_w == 0) fb_w = 640;
    if (fb_h == 0) fb_h = 480;

    vc_push_constants_t pc;
    voodoo_vk_extract_push_constants(params, &pc, fb_w, fb_h);

    /* Extract clip rectangle from Voodoo registers. */
    vc_clip_rect_t clip;
    memset(&clip, 0, sizeof(clip));
    clip.enable = (params->fbzMode & 1) ? 1 : 0;
    clip.left   = (uint16_t) params->clipLeft;
    clip.right  = (uint16_t) params->clipRight;
    clip.low_y  = (uint16_t) params->clipLowY;
    clip.high_y = (uint16_t) params->clipHighY;

    /* Reserve space in the ring, fill payload, then commit + wake.
       This ensures write_pos is not published until payload is complete. */
    void *payload = vc_ring_reserve(&ctx->ring, VC_CMD_TRIANGLE,
                                    VC_CMD_TRIANGLE_SIZE);

    /* Layout: [push_constants] [clip_rect] [verts[3]] */
    memcpy(payload, &pc, sizeof(vc_push_constants_t));
    memcpy((uint8_t *) payload + sizeof(vc_push_constants_t),
           &clip, sizeof(vc_clip_rect_t));
    memcpy((uint8_t *) payload + sizeof(vc_push_constants_t) + sizeof(vc_clip_rect_t),
           verts, 3 * sizeof(vc_vertex_t));

    vc_ring_commit_and_wake(&ctx->ring);
}

/* -------------------------------------------------------------------------- */
/*  Public API: push swap to ring                                              */
/* -------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------- */
/*  Public API: notify resolution change                                       */
/* -------------------------------------------------------------------------- */

void
vc_voodoo_set_resolution(void *ctx_opaque, int width, int height)
{
    vc_ctx_t *ctx = (vc_ctx_t *) ctx_opaque;
    if (!ctx || width <= 0 || height <= 0)
        return;

    uint32_t w = (uint32_t) width;
    uint32_t h = (uint32_t) height;

    /* Skip if dimensions haven't changed. */
    uint32_t cur_w = atomic_load_explicit(&ctx->fb_width, memory_order_acquire);
    uint32_t cur_h = atomic_load_explicit(&ctx->fb_height, memory_order_acquire);
    if (cur_w == w && cur_h == h)
        return;

    /* Update atomic dimensions so new triangles use correct NDC transform
       immediately (before the GPU thread processes the resize). */
    atomic_store_explicit(&ctx->fb_width, w, memory_order_release);
    atomic_store_explicit(&ctx->fb_height, h, memory_order_release);

    /* Push VC_CMD_RESIZE to the GPU thread ring. */
    uint16_t cmd_size = (uint16_t) (sizeof(vc_ring_cmd_header_t)
                                   + sizeof(vc_resize_payload_t));
    vc_resize_payload_t *rp = (vc_resize_payload_t *)
        vc_ring_reserve(&ctx->ring, VC_CMD_RESIZE, cmd_size);

    rp->width  = w;
    rp->height = h;

    vc_ring_commit_and_wake(&ctx->ring);

    fprintf(stderr, "VC_DIAG: vc_voodoo_set_resolution %ux%u -> %ux%u\n",
            cur_w, cur_h, w, h);
}

/* -------------------------------------------------------------------------- */
/*  Public API: push swap to ring                                              */
/* -------------------------------------------------------------------------- */

void
voodoo_vk_push_swap(voodoo_t *voodoo)
{
    vc_ctx_t *ctx = (vc_ctx_t *) voodoo->vc_ctx;
    if (!ctx)
        return;

    vc_ring_push_and_wake(&ctx->ring, VC_CMD_SWAP,
                          (uint16_t) sizeof(vc_ring_cmd_header_t));
}
