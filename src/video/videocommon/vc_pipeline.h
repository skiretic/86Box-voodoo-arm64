/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VideoCommon pipeline module -- VkPipeline creation, push
 *          constant layout, vertex format definition, pipeline cache.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#ifndef VIDEOCOMMON_PIPELINE_H
#define VIDEOCOMMON_PIPELINE_H

#include "vc_internal.h"
#include "vc_shader.h"

/* -------------------------------------------------------------------------- */
/*  Vertex format                                                              */
/* -------------------------------------------------------------------------- */

/*
 * Per-vertex data for the Voodoo uber-shader.  Matches DESIGN.md 7.8.
 * 72 bytes per vertex, 216 bytes per triangle.
 *
 * Shader attribute mapping:
 *   location 0: x, y       (vec2,  offset  0)
 *   location 1: z          (float, offset  8)
 *   location 2: w (1/W)    (float, offset 12)
 *   location 3: r, g, b, a (vec4,  offset 16)
 *   location 4: s0, t0, w0 (vec3,  offset 32)
 *   location 5: s1, t1, w1 (vec3,  offset 44)
 *   location 6: fog        (float, offset 56)
 *   pad[3] at offset 60 -- not consumed by shader.
 */
typedef struct vc_vertex_t {
    float x, y;             /* screen-space position (8 bytes)      */
    float z;                /* depth, normalized [0,1] (4 bytes)    */
    float w;                /* 1/W for perspective (4 bytes)        */
    float r, g, b, a;       /* iterated color (16 bytes)            */
    float s0, t0, w0;       /* TMU0 texture coords (12 bytes)      */
    float s1, t1, w1;       /* TMU1 texture coords (12 bytes)      */
    float fog;              /* fog coordinate (4 bytes)             */
    float pad[3];           /* align to 72 bytes (12 bytes)         */
} vc_vertex_t;

_Static_assert(sizeof(vc_vertex_t) == 72,
               "vc_vertex_t must be 72 bytes");

/* Number of vertex input attributes declared in the pipeline. */
#define VC_VERTEX_ATTRIB_COUNT 7

/* -------------------------------------------------------------------------- */
/*  Push constants                                                             */
/* -------------------------------------------------------------------------- */

/*
 * 64-byte push constant block.  Raw register pass-through for 6 registers;
 * packed color values for 5 colors; stipple pattern; detail params; FB dims.
 * std430 packing -- no padding with all-scalar types.
 *
 * GLSL declaration in both vert and frag shaders must match this layout.
 */
typedef struct vc_push_constants_t {
    uint32_t fbzMode;        /* offset  0 */
    uint32_t fbzColorPath;   /* offset  4 */
    uint32_t alphaMode;      /* offset  8 */
    uint32_t fogMode;        /* offset 12 */
    uint32_t textureMode0;   /* offset 16 */
    uint32_t textureMode1;   /* offset 20 */
    uint32_t color0;         /* offset 24 -- 0xAARRGGBB */
    uint32_t color1;         /* offset 28 -- 0xAARRGGBB */
    uint32_t chromaKey;      /* offset 32 -- 0x00RRGGBB */
    uint32_t fogColor;       /* offset 36 -- 0x00RRGGBB */
    uint32_t zaColor;        /* offset 40 */
    uint32_t stipple;        /* offset 44 */
    uint32_t detail0;        /* offset 48 */
    uint32_t detail1;        /* offset 52 */
    float    fb_width;       /* offset 56 */
    float    fb_height;      /* offset 60 */
} vc_push_constants_t;

_Static_assert(sizeof(vc_push_constants_t) == 64,
               "vc_push_constants_t must be 64 bytes");

/* -------------------------------------------------------------------------- */
/*  Blend pipeline key                                                         */
/* -------------------------------------------------------------------------- */

/*
 * Pipeline cache key for blend state variants.  Voodoo games typically use
 * only 5-15 unique blend configs, so a small linear array suffices.
 * The key is derived from alphaMode register bits:
 *   bit 4:      blend enable
 *   bits 11:8:  src RGB factor
 *   bits 15:12: dst RGB factor
 *   (Voodoo has no separate alpha blend factors; RGB factors are reused.)
 */
typedef struct vc_blend_key_t {
    uint8_t blend_enable;
    uint8_t src_rgb;          /* Voodoo AFUNC enum (0-0xF) */
    uint8_t dst_rgb;          /* Voodoo AFUNC enum (0-0xF) */
    uint8_t src_alpha;        /* Same as src_rgb (Voodoo has no separate alpha factors) */
    uint8_t dst_alpha;        /* Same as dst_rgb (Voodoo has no separate alpha factors) */
    uint8_t pad[3];           /* Pad to 8 bytes for memcmp. */
} vc_blend_key_t;

_Static_assert(sizeof(vc_blend_key_t) == 8,
               "vc_blend_key_t must be 8 bytes");

/* Maximum number of cached blend pipeline variants. */
#define VC_MAX_BLEND_PIPELINES 32

typedef struct vc_blend_pipeline_entry_t {
    vc_blend_key_t key;
    VkPipeline     pipeline;
} vc_blend_pipeline_entry_t;

/* -------------------------------------------------------------------------- */
/*  Pipeline state                                                             */
/* -------------------------------------------------------------------------- */

typedef struct vc_pipeline_t {
    VkPipelineLayout  layout;
    VkPipelineCache   cache;
    VkPipeline        pipeline;   /* Default pipeline (blend disabled). */

    /* Blend pipeline variant cache (lazily populated). */
    vc_blend_pipeline_entry_t blend_pipelines[VC_MAX_BLEND_PIPELINES];
    uint32_t                  blend_pipeline_count;

    /* Saved creation parameters for spawning blend variants. */
    VkRenderPass          saved_render_pass;
    VkDescriptorSetLayout saved_desc_layout;
    vc_shaders_t          saved_shaders;

    /* Currently bound pipeline (tracked to avoid redundant binds). */
    VkPipeline            bound_pipeline;
} vc_pipeline_t;

/* Create the pipeline layout, pipeline cache, and default graphics pipeline.
   `render_pass` is the VkRenderPass this pipeline will be used with.
   `desc_layout` is the descriptor set layout for texture bindings
   (VK_NULL_HANDLE to use no descriptor sets).
   Returns 0 on success, -1 on failure. */
int  vc_pipeline_create(vc_ctx_t *ctx, vc_pipeline_t *pl,
                        const vc_shaders_t *shaders,
                        VkRenderPass render_pass,
                        VkDescriptorSetLayout desc_layout);

/* Destroy the pipeline, layout, cache, and all blend variants. */
void vc_pipeline_destroy(vc_ctx_t *ctx, vc_pipeline_t *pl);

/* Look up or create a pipeline variant for the given blend state.
   Extracts blend key from alphaMode register value.
   Returns the VkPipeline to bind, or VK_NULL_HANDLE on failure. */
VkPipeline vc_pipeline_get_for_blend(vc_ctx_t *ctx, vc_pipeline_t *pl,
                                     uint32_t alphaMode);

/* Extract a blend key from an alphaMode register value. */
vc_blend_key_t vc_blend_key_from_alpha_mode(uint32_t alphaMode);

#endif /* VIDEOCOMMON_PIPELINE_H */
