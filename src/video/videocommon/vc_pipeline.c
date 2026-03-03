/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VideoCommon pipeline module -- graphics pipeline creation,
 *          push constant layout, vertex input state, pipeline cache.
 *
 *          Phase 5.9: pipeline variant cache keyed on blend state.
 *          MoltenVK does NOT support EDS3 dynamic blend factors, so
 *          blend enable / blend factors / blend op are baked into
 *          VkPipeline objects.  A small linear cache (typically 5-15
 *          entries for real games) maps blend keys to pipelines.
 *          Dynamic state: viewport, scissor, depth test enable,
 *          depth write enable, depth compare op (via EDS1).
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "vc_internal.h"

#define HAVE_STDARG_H
#include <86box/86box.h>

#include "vc_pipeline.h"
#include "vc_shader.h"
#include "vc_texture.h"

/* -------------------------------------------------------------------------- */
/*  Vertex input state                                                         */
/* -------------------------------------------------------------------------- */

static void
vc_fill_vertex_input(VkVertexInputBindingDescription *binding,
                     VkVertexInputAttributeDescription attrs[VC_VERTEX_ATTRIB_COUNT])
{
    /* Single vertex buffer binding, per-vertex rate. */
    memset(binding, 0, sizeof(*binding));
    binding->binding   = 0;
    binding->stride    = sizeof(vc_vertex_t);
    binding->inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    memset(attrs, 0, VC_VERTEX_ATTRIB_COUNT * sizeof(attrs[0]));

    /* location 0: position (x, y) -- vec2 */
    attrs[0].location = 0;
    attrs[0].binding  = 0;
    attrs[0].format   = VK_FORMAT_R32G32_SFLOAT;
    attrs[0].offset   = offsetof(vc_vertex_t, x);

    /* location 1: depth (z) -- float */
    attrs[1].location = 1;
    attrs[1].binding  = 0;
    attrs[1].format   = VK_FORMAT_R32_SFLOAT;
    attrs[1].offset   = offsetof(vc_vertex_t, z);

    /* location 2: color (r, g, b, a) -- vec4
     * Note: 1/W (vc_vertex_t.w) is in the vertex buffer but not consumed
     * by the shader yet -- deferred to Phase 6 (fog). */
    attrs[2].location = 2;
    attrs[2].binding  = 0;
    attrs[2].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[2].offset   = offsetof(vc_vertex_t, r);

    /* location 3: TMU0 texcoord (s0, t0, w0) -- vec3 */
    attrs[3].location = 3;
    attrs[3].binding  = 0;
    attrs[3].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[3].offset   = offsetof(vc_vertex_t, s0);

    /* location 4: TMU1 texcoord (s1, t1, w1) -- vec3 */
    attrs[4].location = 4;
    attrs[4].binding  = 0;
    attrs[4].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[4].offset   = offsetof(vc_vertex_t, s1);
}

/* -------------------------------------------------------------------------- */
/*  Voodoo blend factor mapping                                                */
/* -------------------------------------------------------------------------- */

/*
 * Map a Voodoo AFUNC blend factor enum to VkBlendFactor.
 * `is_src` disambiguates AFUNC_A_COLOR / AFUNC_AOM_COLOR which are
 * asymmetric: src side uses DST_COLOR, dst side uses SRC_COLOR.
 *
 * Voodoo AFUNC values (from vid_voodoo_regs.h):
 *   0x0 = AZERO           -> ZERO
 *   0x1 = ASRC_ALPHA      -> SRC_ALPHA
 *   0x2 = A_COLOR         -> src: DST_COLOR / dst: SRC_COLOR
 *   0x3 = ADST_ALPHA      -> DST_ALPHA
 *   0x4 = AONE            -> ONE
 *   0x5 = AOMSRC_ALPHA    -> ONE_MINUS_SRC_ALPHA
 *   0x6 = AOM_COLOR       -> src: ONE_MINUS_DST_COLOR / dst: ONE_MINUS_SRC_COLOR
 *   0x7 = AOMDST_ALPHA    -> ONE_MINUS_DST_ALPHA
 *   0xF = ASATURATE (src) / ACOLORBEFOREFOG (dst) -> SRC_ALPHA_SATURATE / mapped below
 */
static VkBlendFactor
vc_voodoo_blend_factor(uint32_t afunc, int is_src, int has_dual_src)
{
    switch (afunc) {
        case 0x0: return VK_BLEND_FACTOR_ZERO;
        case 0x1: return VK_BLEND_FACTOR_SRC_ALPHA;
        case 0x2: return is_src ? VK_BLEND_FACTOR_DST_COLOR
                                : VK_BLEND_FACTOR_SRC_COLOR;
        case 0x3: return VK_BLEND_FACTOR_DST_ALPHA;
        case 0x4: return VK_BLEND_FACTOR_ONE;
        case 0x5: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case 0x6: return is_src ? VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR
                                : VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case 0x7: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case 0xF:
            if (is_src)
                return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
            /* ACOLORBEFOREFOG (dst): use SRC1_COLOR if dual-source blend
               is available; otherwise fall back to SRC_COLOR. */
            return has_dual_src ? VK_BLEND_FACTOR_SRC1_COLOR
                                : VK_BLEND_FACTOR_SRC_COLOR;
        default:
            return VK_BLEND_FACTOR_ZERO;
    }
}

/* -------------------------------------------------------------------------- */
/*  Blend key extraction                                                       */
/* -------------------------------------------------------------------------- */

vc_blend_key_t
vc_blend_key_from_alpha_mode(uint32_t alphaMode)
{
    vc_blend_key_t key;
    memset(&key, 0, sizeof(key));
    key.blend_enable = (alphaMode >> 4) & 1;
    key.src_rgb      = (alphaMode >> 8) & 0xF;
    key.dst_rgb      = (alphaMode >> 12) & 0xF;
    /* Voodoo has NO separate alpha blend factors — RGB factors apply to both. */
    key.src_alpha    = key.src_rgb;
    key.dst_alpha    = key.dst_rgb;
    return key;
}

/* -------------------------------------------------------------------------- */
/*  Common pipeline creation helper                                            */
/* -------------------------------------------------------------------------- */

/*
 * Create a VkPipeline with the given blend attachment state.
 * Shares layout, cache, shaders, vertex input, dynamic state with `pl`.
 * Returns VK_NULL_HANDLE on failure.
 */
static VkPipeline
vc_create_pipeline_with_blend(vc_ctx_t *ctx, vc_pipeline_t *pl,
                              const VkPipelineColorBlendAttachmentState *blend_att)
{
    VkResult result;

    /* Shader stages. */
    VkPipelineShaderStageCreateInfo stages[2];
    memset(stages, 0, sizeof(stages));
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = pl->saved_shaders.vert;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = pl->saved_shaders.frag;
    stages[1].pName  = "main";

    /* Vertex input. */
    VkVertexInputBindingDescription   binding;
    VkVertexInputAttributeDescription attrs[VC_VERTEX_ATTRIB_COUNT];
    vc_fill_vertex_input(&binding, attrs);

    VkPipelineVertexInputStateCreateInfo vertex_input;
    memset(&vertex_input, 0, sizeof(vertex_input));
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount   = 1;
    vertex_input.pVertexBindingDescriptions      = &binding;
    vertex_input.vertexAttributeDescriptionCount = VC_VERTEX_ATTRIB_COUNT;
    vertex_input.pVertexAttributeDescriptions    = attrs;

    /* Input assembly. */
    VkPipelineInputAssemblyStateCreateInfo input_assembly;
    memset(&input_assembly, 0, sizeof(input_assembly));
    input_assembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    /* Viewport / scissor (dynamic). */
    VkPipelineViewportStateCreateInfo viewport_state;
    memset(&viewport_state, 0, sizeof(viewport_state));
    viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount  = 1;

    /* Rasterization. */
    VkPipelineRasterizationStateCreateInfo rast;
    memset(&rast, 0, sizeof(rast));
    rast.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rast.polygonMode = VK_POLYGON_MODE_FILL;
    rast.cullMode    = VK_CULL_MODE_NONE;
    rast.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rast.lineWidth   = 1.0f;

    /* Multisample (disabled). */
    VkPipelineMultisampleStateCreateInfo multisample;
    memset(&multisample, 0, sizeof(multisample));
    multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    /* Depth/stencil (defaults; EDS1 overrides per-draw). */
    VkPipelineDepthStencilStateCreateInfo depth_stencil;
    memset(&depth_stencil, 0, sizeof(depth_stencil));
    depth_stencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable  = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp   = VK_COMPARE_OP_ALWAYS;

    /* Color blend. */
    VkPipelineColorBlendStateCreateInfo color_blend;
    memset(&color_blend, 0, sizeof(color_blend));
    color_blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments    = blend_att;

    /* Dynamic state. */
    VkDynamicState dynamic_states[5];
    uint32_t       dynamic_count = 0;
    dynamic_states[dynamic_count++] = VK_DYNAMIC_STATE_VIEWPORT;
    dynamic_states[dynamic_count++] = VK_DYNAMIC_STATE_SCISSOR;
    if (ctx->caps.has_extended_dynamic_state) {
        dynamic_states[dynamic_count++] = VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE;
        dynamic_states[dynamic_count++] = VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE;
        dynamic_states[dynamic_count++] = VK_DYNAMIC_STATE_DEPTH_COMPARE_OP;
    }

    VkPipelineDynamicStateCreateInfo dynamic_state;
    memset(&dynamic_state, 0, sizeof(dynamic_state));
    dynamic_state.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = dynamic_count;
    dynamic_state.pDynamicStates    = dynamic_states;

    /* Graphics pipeline. */
    VkGraphicsPipelineCreateInfo pipeline_ci;
    memset(&pipeline_ci, 0, sizeof(pipeline_ci));
    pipeline_ci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_ci.stageCount          = 2;
    pipeline_ci.pStages             = stages;
    pipeline_ci.pVertexInputState   = &vertex_input;
    pipeline_ci.pInputAssemblyState = &input_assembly;
    pipeline_ci.pViewportState      = &viewport_state;
    pipeline_ci.pRasterizationState = &rast;
    pipeline_ci.pMultisampleState   = &multisample;
    pipeline_ci.pDepthStencilState  = &depth_stencil;
    pipeline_ci.pColorBlendState    = &color_blend;
    pipeline_ci.pDynamicState       = &dynamic_state;
    pipeline_ci.layout              = pl->layout;
    pipeline_ci.renderPass          = pl->saved_render_pass;
    pipeline_ci.subpass             = 0;
    pipeline_ci.basePipelineHandle  = VK_NULL_HANDLE;
    pipeline_ci.basePipelineIndex   = -1;

    VkPipeline pipe = VK_NULL_HANDLE;
    result = vkCreateGraphicsPipelines(ctx->device, pl->cache, 1,
                                       &pipeline_ci, NULL, &pipe);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: vkCreateGraphicsPipelines (blend variant) failed (%d)\n", result);
        return VK_NULL_HANDLE;
    }

    return pipe;
}

/* -------------------------------------------------------------------------- */
/*  Pipeline creation                                                          */
/* -------------------------------------------------------------------------- */

int
vc_pipeline_create(vc_ctx_t *ctx, vc_pipeline_t *pl,
                   const vc_shaders_t *shaders,
                   VkRenderPass render_pass,
                   VkDescriptorSetLayout desc_layout)
{
    VkResult result;

    memset(pl, 0, sizeof(vc_pipeline_t));

    /* -------------------------------------------------------------------- */
    /*  Pipeline layout (push constants + optional descriptor set)           */
    /* -------------------------------------------------------------------- */

    VkPushConstantRange push_range;
    memset(&push_range, 0, sizeof(push_range));
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT
                          | VK_SHADER_STAGE_FRAGMENT_BIT;
    push_range.offset     = 0;
    push_range.size       = sizeof(vc_push_constants_t); /* 64 bytes */

    VkPipelineLayoutCreateInfo layout_ci;
    memset(&layout_ci, 0, sizeof(layout_ci));
    layout_ci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_ci.setLayoutCount         = (desc_layout != VK_NULL_HANDLE) ? 1 : 0;
    layout_ci.pSetLayouts            = (desc_layout != VK_NULL_HANDLE) ? &desc_layout : NULL;
    layout_ci.pushConstantRangeCount = 1;
    layout_ci.pPushConstantRanges    = &push_range;

    result = vkCreatePipelineLayout(ctx->device, &layout_ci, NULL, &pl->layout);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: vkCreatePipelineLayout failed (%d)\n", result);
        return -1;
    }

    /* -------------------------------------------------------------------- */
    /*  Pipeline cache                                                       */
    /* -------------------------------------------------------------------- */

    VkPipelineCacheCreateInfo cache_ci;
    memset(&cache_ci, 0, sizeof(cache_ci));
    cache_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

    result = vkCreatePipelineCache(ctx->device, &cache_ci, NULL, &pl->cache);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: vkCreatePipelineCache failed (%d)\n", result);
        vkDestroyPipelineLayout(ctx->device, pl->layout, NULL);
        pl->layout = VK_NULL_HANDLE;
        return -1;
    }

    /* -------------------------------------------------------------------- */
    /*  Shader stages                                                        */
    /* -------------------------------------------------------------------- */

    VkPipelineShaderStageCreateInfo stages[2];
    memset(stages, 0, sizeof(stages));

    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = shaders->vert;
    stages[0].pName  = "main";

    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = shaders->frag;
    stages[1].pName  = "main";

    /* -------------------------------------------------------------------- */
    /*  Vertex input state                                                   */
    /* -------------------------------------------------------------------- */

    VkVertexInputBindingDescription   binding;
    VkVertexInputAttributeDescription attrs[VC_VERTEX_ATTRIB_COUNT];
    vc_fill_vertex_input(&binding, attrs);

    VkPipelineVertexInputStateCreateInfo vertex_input;
    memset(&vertex_input, 0, sizeof(vertex_input));
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount   = 1;
    vertex_input.pVertexBindingDescriptions      = &binding;
    vertex_input.vertexAttributeDescriptionCount = VC_VERTEX_ATTRIB_COUNT;
    vertex_input.pVertexAttributeDescriptions    = attrs;

    /* -------------------------------------------------------------------- */
    /*  Input assembly                                                       */
    /* -------------------------------------------------------------------- */

    VkPipelineInputAssemblyStateCreateInfo input_assembly;
    memset(&input_assembly, 0, sizeof(input_assembly));
    input_assembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    /* -------------------------------------------------------------------- */
    /*  Viewport / scissor (dynamic, but count must be specified)             */
    /* -------------------------------------------------------------------- */

    VkPipelineViewportStateCreateInfo viewport_state;
    memset(&viewport_state, 0, sizeof(viewport_state));
    viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount  = 1;
    /* pViewports and pScissors are NULL because they are dynamic. */

    /* -------------------------------------------------------------------- */
    /*  Rasterization                                                        */
    /* -------------------------------------------------------------------- */

    VkPipelineRasterizationStateCreateInfo rast;
    memset(&rast, 0, sizeof(rast));
    rast.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rast.depthClampEnable        = VK_FALSE;
    rast.rasterizerDiscardEnable = VK_FALSE;
    rast.polygonMode             = VK_POLYGON_MODE_FILL;
    rast.cullMode                = VK_CULL_MODE_NONE; /* Voodoo setup engine culls before we see triangles. */
    rast.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rast.depthBiasEnable         = VK_FALSE;
    rast.lineWidth               = 1.0f;

    /* -------------------------------------------------------------------- */
    /*  Multisample (disabled)                                               */
    /* -------------------------------------------------------------------- */

    VkPipelineMultisampleStateCreateInfo multisample;
    memset(&multisample, 0, sizeof(multisample));
    multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisample.sampleShadingEnable  = VK_FALSE;

    /* -------------------------------------------------------------------- */
    /*  Depth / stencil                                                      */
    /*                                                                       */
    /*  These values are defaults -- when EDS1 is available, depth test       */
    /*  enable, depth write enable, and depth compare op are set             */
    /*  dynamically per-draw call.  The baked values here serve as           */
    /*  fallback for drivers without EDS1 (unlikely but safe).               */
    /* -------------------------------------------------------------------- */

    VkPipelineDepthStencilStateCreateInfo depth_stencil;
    memset(&depth_stencil, 0, sizeof(depth_stencil));
    depth_stencil.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable       = VK_TRUE;
    depth_stencil.depthWriteEnable      = VK_TRUE;
    depth_stencil.depthCompareOp        = VK_COMPARE_OP_ALWAYS;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.stencilTestEnable     = VK_FALSE;

    /* -------------------------------------------------------------------- */
    /*  Color blend (disabled for Phase 2)                                   */
    /* -------------------------------------------------------------------- */

    VkPipelineColorBlendAttachmentState blend_att;
    memset(&blend_att, 0, sizeof(blend_att));
    blend_att.blendEnable    = VK_FALSE;
    blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                             | VK_COLOR_COMPONENT_G_BIT
                             | VK_COLOR_COMPONENT_B_BIT
                             | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend;
    memset(&color_blend, 0, sizeof(color_blend));
    color_blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.logicOpEnable   = VK_FALSE;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments    = &blend_att;

    /* -------------------------------------------------------------------- */
    /*  Dynamic state                                                        */
    /*                                                                       */
    /*  Viewport + scissor are always dynamic.  When EDS1 is available,      */
    /*  depth test/write/compare are also dynamic (set per-draw call).       */
    /* -------------------------------------------------------------------- */

    VkDynamicState dynamic_states[5];
    uint32_t       dynamic_count = 0;

    dynamic_states[dynamic_count++] = VK_DYNAMIC_STATE_VIEWPORT;
    dynamic_states[dynamic_count++] = VK_DYNAMIC_STATE_SCISSOR;

    if (ctx->caps.has_extended_dynamic_state) {
        dynamic_states[dynamic_count++] = VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE;
        dynamic_states[dynamic_count++] = VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE;
        dynamic_states[dynamic_count++] = VK_DYNAMIC_STATE_DEPTH_COMPARE_OP;
    }

    VkPipelineDynamicStateCreateInfo dynamic_state;
    memset(&dynamic_state, 0, sizeof(dynamic_state));
    dynamic_state.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = dynamic_count;
    dynamic_state.pDynamicStates    = dynamic_states;

    /* -------------------------------------------------------------------- */
    /*  Graphics pipeline                                                    */
    /* -------------------------------------------------------------------- */

    VkGraphicsPipelineCreateInfo pipeline_ci;
    memset(&pipeline_ci, 0, sizeof(pipeline_ci));
    pipeline_ci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_ci.stageCount          = 2;
    pipeline_ci.pStages             = stages;
    pipeline_ci.pVertexInputState   = &vertex_input;
    pipeline_ci.pInputAssemblyState = &input_assembly;
    pipeline_ci.pViewportState      = &viewport_state;
    pipeline_ci.pRasterizationState = &rast;
    pipeline_ci.pMultisampleState   = &multisample;
    pipeline_ci.pDepthStencilState  = &depth_stencil;
    pipeline_ci.pColorBlendState    = &color_blend;
    pipeline_ci.pDynamicState       = &dynamic_state;
    pipeline_ci.layout              = pl->layout;
    pipeline_ci.renderPass          = render_pass;
    pipeline_ci.subpass             = 0;
    pipeline_ci.basePipelineHandle  = VK_NULL_HANDLE;
    pipeline_ci.basePipelineIndex   = -1;

    result = vkCreateGraphicsPipelines(ctx->device, pl->cache, 1,
                                       &pipeline_ci, NULL, &pl->pipeline);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: vkCreateGraphicsPipelines failed (%d)\n", result);
        vkDestroyPipelineCache(ctx->device, pl->cache, NULL);
        vkDestroyPipelineLayout(ctx->device, pl->layout, NULL);
        pl->cache  = VK_NULL_HANDLE;
        pl->layout = VK_NULL_HANDLE;
        return -1;
    }

    /* Save creation params for spawning blend variants later. */
    pl->saved_render_pass = render_pass;
    pl->saved_desc_layout = desc_layout;
    pl->saved_shaders     = *shaders;
    pl->blend_pipeline_count = 0;
    pl->bound_pipeline       = VK_NULL_HANDLE;

    VC_LOG("VideoCommon: graphics pipeline created\n");
    return 0;
}

/* -------------------------------------------------------------------------- */
/*  Blend pipeline variant lookup / creation                                   */
/* -------------------------------------------------------------------------- */

VkPipeline
vc_pipeline_get_for_blend(vc_ctx_t *ctx, vc_pipeline_t *pl,
                          uint32_t alphaMode)
{
    vc_blend_key_t key = vc_blend_key_from_alpha_mode(alphaMode);

    /* No blending -> use the default pipeline (created without blend). */
    if (!key.blend_enable)
        return pl->pipeline;

    /* Search existing cache. */
    for (uint32_t i = 0; i < pl->blend_pipeline_count; i++) {
        if (memcmp(&pl->blend_pipelines[i].key, &key, sizeof(key)) == 0)
            return pl->blend_pipelines[i].pipeline;
    }

    /* Not found -- create a new variant. */
    if (pl->blend_pipeline_count >= VC_MAX_BLEND_PIPELINES) {
        VC_LOG("VideoCommon: blend pipeline cache full (%u entries), "
               "falling back to no-blend pipeline\n",
               VC_MAX_BLEND_PIPELINES);
        return pl->pipeline;
    }

    VkPipelineColorBlendAttachmentState blend_att;
    memset(&blend_att, 0, sizeof(blend_att));
    blend_att.blendEnable         = VK_TRUE;
    const int dual_src = ctx->caps.has_dual_src_blend;
    blend_att.srcColorBlendFactor = vc_voodoo_blend_factor(key.src_rgb, 1, dual_src);
    blend_att.dstColorBlendFactor = vc_voodoo_blend_factor(key.dst_rgb, 0, dual_src);
    blend_att.colorBlendOp        = VK_BLEND_OP_ADD;
    blend_att.srcAlphaBlendFactor = vc_voodoo_blend_factor(key.src_alpha, 1, dual_src);
    blend_att.dstAlphaBlendFactor = vc_voodoo_blend_factor(key.dst_alpha, 0, dual_src);
    blend_att.alphaBlendOp        = VK_BLEND_OP_ADD;
    blend_att.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT
                                  | VK_COLOR_COMPONENT_G_BIT
                                  | VK_COLOR_COMPONENT_B_BIT
                                  | VK_COLOR_COMPONENT_A_BIT;

    VkPipeline pipe = vc_create_pipeline_with_blend(ctx, pl, &blend_att);
    if (pipe == VK_NULL_HANDLE)
        return pl->pipeline; /* Fallback to no-blend. */

    /* Cache it. */
    uint32_t idx = pl->blend_pipeline_count++;
    pl->blend_pipelines[idx].key      = key;
    pl->blend_pipelines[idx].pipeline = pipe;

    VC_LOG("VideoCommon: blend pipeline variant #%u created "
           "(src_rgb=%u dst_rgb=%u src_a=%u dst_a=%u)\n",
           idx, key.src_rgb, key.dst_rgb, key.src_alpha, key.dst_alpha);

    return pipe;
}

/* -------------------------------------------------------------------------- */
/*  Cleanup                                                                    */
/* -------------------------------------------------------------------------- */

void
vc_pipeline_destroy(vc_ctx_t *ctx, vc_pipeline_t *pl)
{
    if (!pl)
        return;

    /* Destroy blend variant pipelines. */
    for (uint32_t i = 0; i < pl->blend_pipeline_count; i++) {
        if (pl->blend_pipelines[i].pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(ctx->device, pl->blend_pipelines[i].pipeline, NULL);
            pl->blend_pipelines[i].pipeline = VK_NULL_HANDLE;
        }
    }
    pl->blend_pipeline_count = 0;

    if (pl->pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(ctx->device, pl->pipeline, NULL);
        pl->pipeline = VK_NULL_HANDLE;
    }

    if (pl->cache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(ctx->device, pl->cache, NULL);
        pl->cache = VK_NULL_HANDLE;
    }

    if (pl->layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(ctx->device, pl->layout, NULL);
        pl->layout = VK_NULL_HANDLE;
    }

    pl->bound_pipeline = VK_NULL_HANDLE;
}
