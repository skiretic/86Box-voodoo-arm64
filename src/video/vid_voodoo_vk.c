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

/* Global tracking (could be per-voodoo if needed, but there's only ever one). */
static voodoo_vk_tex_track_t vk_tex_track[2] = {
    { .addr = ~0u, .tLOD = ~0u, .pal_checksum = ~0u, .slot = -1, .last_upload_slot = -1, .last_identity = 0 },
    { .addr = ~0u, .tLOD = ~0u, .pal_checksum = ~0u, .slot = -1, .last_upload_slot = -1, .last_identity = 0 },
};

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

    float dx_ba = xB - xA;
    float dy_ba = yB - yA;
    float dx_ca = xC - xA;
    float dy_ca = yC - yA;

    /* Color A (start values). */
    float rA = (float) (int32_t) p->startR / VC_COLOR_SCALE;
    float gA = (float) (int32_t) p->startG / VC_COLOR_SCALE;
    float bA = (float) (int32_t) p->startB / VC_COLOR_SCALE;
    float aA = (float) (int32_t) p->startA / VC_COLOR_SCALE;

    /* Color B = A + gradient * delta. */
    float rB = rA + ((float) p->dRdX * dx_ba + (float) p->dRdY * dy_ba) / VC_COLOR_SCALE;
    float gB = gA + ((float) p->dGdX * dx_ba + (float) p->dGdY * dy_ba) / VC_COLOR_SCALE;
    float bB = bA + ((float) p->dBdX * dx_ba + (float) p->dBdY * dy_ba) / VC_COLOR_SCALE;
    float aB = aA + ((float) p->dAdX * dx_ba + (float) p->dAdY * dy_ba) / VC_COLOR_SCALE;

    /* Color C = A + gradient * delta. */
    float rC = rA + ((float) p->dRdX * dx_ca + (float) p->dRdY * dy_ca) / VC_COLOR_SCALE;
    float gC = gA + ((float) p->dGdX * dx_ca + (float) p->dGdY * dy_ca) / VC_COLOR_SCALE;
    float bC = bA + ((float) p->dBdX * dx_ca + (float) p->dBdY * dy_ca) / VC_COLOR_SCALE;
    float aC = aA + ((float) p->dAdX * dx_ca + (float) p->dAdY * dy_ca) / VC_COLOR_SCALE;

    /* 1/W: 18.32 fixed-point (int64_t).  startW IS 1/W (not W). */
    double oowA = (double) p->startW / VC_W_SCALE;
    double oowB = oowA + ((double) p->dWdX * dx_ba + (double) p->dWdY * dy_ba) / VC_W_SCALE;
    double oowC = oowA + ((double) p->dWdX * dx_ca + (double) p->dWdY * dy_ca) / VC_W_SCALE;

    /* TMU0 S/T/W: 18.32 fixed-point. */
    int textured = (p->fbzColorPath & FBZCP_TEXTURE_ENABLED) ? 1 : 0;
    double s0A = 0.0, t0A = 0.0, w0A = 0.0;
    double s0B = 0.0, t0B = 0.0, w0B = 0.0;
    double s0C = 0.0, t0C = 0.0, w0C = 0.0;

    if (textured) {
        s0A = (double) p->tmu[0].startS / VC_ST_SCALE;
        t0A = (double) p->tmu[0].startT / VC_ST_SCALE;
        w0A = (double) p->tmu[0].startW / VC_ST_SCALE;

        s0B = s0A + ((double) p->tmu[0].dSdX * dx_ba + (double) p->tmu[0].dSdY * dy_ba) / VC_ST_SCALE;
        t0B = t0A + ((double) p->tmu[0].dTdX * dx_ba + (double) p->tmu[0].dTdY * dy_ba) / VC_ST_SCALE;
        w0B = w0A + ((double) p->tmu[0].dWdX * dx_ba + (double) p->tmu[0].dWdY * dy_ba) / VC_ST_SCALE;

        s0C = s0A + ((double) p->tmu[0].dSdX * dx_ca + (double) p->tmu[0].dSdY * dy_ca) / VC_ST_SCALE;
        t0C = t0A + ((double) p->tmu[0].dTdX * dx_ca + (double) p->tmu[0].dTdY * dy_ca) / VC_ST_SCALE;
        w0C = w0A + ((double) p->tmu[0].dWdX * dx_ca + (double) p->tmu[0].dWdY * dy_ca) / VC_ST_SCALE;
    }

    /* Vertex A. */
    memset(&verts[0], 0, sizeof(vc_vertex_t));
    verts[0].x  = xA;
    verts[0].y  = yA;
    verts[0].z  = 0.5f;
    verts[0].w  = (float) oowA;
    verts[0].r  = rA;
    verts[0].g  = gA;
    verts[0].b  = bA;
    verts[0].a  = aA;
    verts[0].s0 = (float) s0A;
    verts[0].t0 = (float) t0A;
    verts[0].w0 = (float) w0A;

    /* Vertex B. */
    memset(&verts[1], 0, sizeof(vc_vertex_t));
    verts[1].x  = xB;
    verts[1].y  = yB;
    verts[1].z  = 0.5f;
    verts[1].w  = (float) oowB;
    verts[1].r  = rB;
    verts[1].g  = gB;
    verts[1].b  = bB;
    verts[1].a  = aB;
    verts[1].s0 = (float) s0B;
    verts[1].t0 = (float) t0B;
    verts[1].w0 = (float) w0B;

    /* Vertex C. */
    memset(&verts[2], 0, sizeof(vc_vertex_t));
    verts[2].x  = xC;
    verts[2].y  = yC;
    verts[2].z  = 0.5f;
    verts[2].w  = (float) oowC;
    verts[2].r  = rC;
    verts[2].g  = gC;
    verts[2].b  = bC;
    verts[2].a  = aC;
    verts[2].s0 = (float) s0C;
    verts[2].t0 = (float) t0C;
    verts[2].w0 = (float) w0C;
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
    pc->fogColor       = (uint32_t) p->fogColor.r
                       | ((uint32_t) p->fogColor.g << 8)
                       | ((uint32_t) p->fogColor.b << 16);
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
    if (tex_entry < 0 || tex_entry >= TEX_CACHE_MAX)
        return;

    texture_t *tc = &voodoo->texture_cache[tmu][tex_entry];

    /* Compute identity hash. */
    uint32_t identity = tc->base ^ tc->tLOD ^ tc->palette_checksum;

    /* Check if this texture is already uploaded. */
    voodoo_vk_tex_track_t *trk = &vk_tex_track[tmu];
    if (trk->slot == tex_entry && trk->last_identity == identity)
        return; /* Already uploaded and bound. */

    /* Determine LOD 0 dimensions. */
    int lod_min = (params->tLOD[tmu] >> 2) & 15;
    if (lod_min > 8) lod_min = 8;
    uint32_t width  = params->tex_w_mask[tmu][lod_min] + 1;
    uint32_t height = params->tex_h_mask[tmu][lod_min] + 1;
    if (width == 0 || height == 0 || width > 256 || height > 256)
        return;

    /* Copy decoded RGBA8 data from texture cache.
       The data array layout: texture_offset[lod] gives the uint32 offset
       for each LOD level.  LOD 0 starts at texture_offset[lod_min]. */
    uint32_t pixel_count = width * height;
    uint32_t byte_size   = pixel_count * 4;

    uint8_t *data = (uint8_t *) malloc(byte_size);
    if (!data)
        return;

    /* Copy from the texture cache data buffer at the correct LOD offset.
       The cache data is stored as 0xAARRGGBB (makergba format).
       We need to repack to RGBA8 for VK_FORMAT_R8G8B8A8_UNORM. */
    const uint32_t *src = &tc->data[texture_offset[lod_min]];
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

    /* Push upload command -- GPU thread takes ownership of data pointer. */
    uint16_t cmd_size = (uint16_t) (sizeof(vc_ring_cmd_header_t)
                                  + sizeof(vc_tex_upload_payload_t));
    vc_tex_upload_payload_t *up = (vc_tex_upload_payload_t *)
        vc_ring_push_and_wake(&ctx->ring, VC_CMD_TEXTURE_UPLOAD, cmd_size);

    up->tmu      = (uint32_t) tmu;
    up->slot     = (uint32_t) tex_entry;
    up->width    = width;
    up->height   = height;
    up->identity = identity;
    up->pad      = 0;
    up->data_ptr = (uint64_t) (uintptr_t) data;

    /* Push bind command. */
    uint16_t bind_size = (uint16_t) (sizeof(vc_ring_cmd_header_t)
                                   + sizeof(vc_tex_bind_payload_t));
    vc_tex_bind_payload_t *bp = (vc_tex_bind_payload_t *)
        vc_ring_push_and_wake(&ctx->ring, VC_CMD_TEXTURE_BIND, bind_size);

    bp->tmu         = (uint32_t) tmu;
    bp->slot        = (uint32_t) tex_entry;
    bp->sampler_key = vc_texture_sampler_key(params->textureMode[tmu]);
    bp->pad         = 0;

    /* Update tracking. */
    trk->addr          = tc->base;
    trk->tLOD          = tc->tLOD;
    trk->pal_checksum  = tc->palette_checksum;
    trk->slot          = tex_entry;
    trk->last_upload_slot = tex_entry;
    trk->last_identity = identity;
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

    /* Handle texture if textured. */
    if (params->fbzColorPath & FBZCP_TEXTURE_ENABLED) {
        /* Ensure voodoo_use_texture() has been called (it has -- the
           render path calls it before us).  Now push to GPU thread. */
        voodoo_vk_push_texture(voodoo, params, 0);

        /* CRITICAL: increment refcount_r[0] to match refcount for eviction.
           The SW render path does this in voodoo_half_triangle; since we
           divert to VK, we must do it here. */
        voodoo->texture_cache[0][params->tex_entry[0]].refcount_r[0]++;
    }

    vc_vertex_t verts[3];
    voodoo_vk_extract_vertices(params, verts);

    uint32_t fb_w = atomic_load_explicit(&ctx->fb_width, memory_order_acquire);
    uint32_t fb_h = atomic_load_explicit(&ctx->fb_height, memory_order_acquire);
    if (fb_w == 0) fb_w = 640;
    if (fb_h == 0) fb_h = 480;

    vc_push_constants_t pc;
    voodoo_vk_extract_push_constants(params, &pc, fb_w, fb_h);

    /* Push VC_CMD_TRIANGLE to ring (no wake -- batched). */
    void *payload = vc_ring_push(&ctx->ring, VC_CMD_TRIANGLE,
                                 VC_CMD_TRIANGLE_SIZE);

    /* Layout: [push_constants] [verts[3]] */
    memcpy(payload, &pc, sizeof(vc_push_constants_t));
    memcpy((uint8_t *) payload + sizeof(vc_push_constants_t),
           verts, 3 * sizeof(vc_vertex_t));
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

    pclog("VideoCommon: swap pushed to ring\n");
    vc_ring_push_and_wake(&ctx->ring, VC_CMD_SWAP,
                          (uint16_t) sizeof(vc_ring_cmd_header_t));
}
