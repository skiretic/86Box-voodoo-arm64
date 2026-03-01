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

/*
 * VideoCommon internal headers.  This file is compiled by src/video/
 * CMakeLists.txt, which adds the videocommon include path when
 * USE_VIDEOCOMMON is defined.
 */
#include "videocommon/vc_internal.h"
#include "videocommon/vc_thread.h"
#include "videocommon/vc_pipeline.h" /* vc_vertex_t, vc_push_constants_t */
#include "videocommon/vc_batch.h"    /* VC_CMD_TRIANGLE_SIZE */

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

    /* Vertex A. */
    memset(&verts[0], 0, sizeof(vc_vertex_t));
    verts[0].x = xA;
    verts[0].y = yA;
    verts[0].z = 0.5f; /* Dummy depth for Phase 2. */
    verts[0].w = 1.0f; /* No perspective correction in Phase 2. */
    verts[0].r = rA;
    verts[0].g = gA;
    verts[0].b = bA;
    verts[0].a = aA;

    /* Vertex B. */
    memset(&verts[1], 0, sizeof(vc_vertex_t));
    verts[1].x = xB;
    verts[1].y = yB;
    verts[1].z = 0.5f;
    verts[1].w = 1.0f;
    verts[1].r = rB;
    verts[1].g = gB;
    verts[1].b = bB;
    verts[1].a = aB;

    /* Vertex C. */
    memset(&verts[2], 0, sizeof(vc_vertex_t));
    verts[2].x = xC;
    verts[2].y = yC;
    verts[2].z = 0.5f;
    verts[2].w = 1.0f;
    verts[2].r = rC;
    verts[2].g = gC;
    verts[2].b = bC;
    verts[2].a = aC;
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
/*  Public API: push triangle to ring                                          */
/* -------------------------------------------------------------------------- */

void
voodoo_vk_push_triangle(voodoo_t *voodoo, voodoo_params_t *params)
{
    vc_ctx_t *ctx = (vc_ctx_t *) voodoo->vc_ctx;
    if (!ctx)
        return;

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

    vc_ring_push_and_wake(&ctx->ring, VC_CMD_SWAP,
                          (uint16_t) sizeof(vc_ring_cmd_header_t));
}
