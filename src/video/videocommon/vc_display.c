/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VideoCommon display subsystem -- swapchain management,
 *          post-process blit pipeline, present.
 *
 *          All swapchain and presentation Vulkan objects are owned
 *          exclusively by the GPU thread.  The Qt/GUI thread provides
 *          a VkSurfaceKHR via atomic, and the GPU thread creates the
 *          swapchain from it.
 *
 *          Post-process blit: fullscreen triangle (no VBO) that samples
 *          the offscreen color attachment and outputs to a swapchain
 *          image.  Nearest-neighbor filter for pixel-perfect scaling.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
/* Platform headers for sched_yield / SwitchToThread -- must come before
   86Box headers to avoid the plat.h fallthrough macro conflict. */
#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#else
#    include <sched.h>
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "vc_internal.h"

#define HAVE_STDARG_H
#include <86box/86box.h>

#include "vc_display.h"
#include "vc_gpu_state.h"
#include "vc_thread.h"

/* Embedded SPIR-V for the post-process shaders. */
#include "postprocess_vert.h"
#include "postprocess_frag.h"

/* C-callable VMA wrappers (implemented in vc_vma_impl.cpp). */
extern VkResult vc_vma_create_image(void *allocator,
                                    const VkImageCreateInfo *image_ci,
                                    VkImage *out_image, void **out_alloc);
extern void     vc_vma_destroy_image(void *allocator, VkImage image,
                                     void *alloc);
extern VkResult vc_vma_create_buffer(void *allocator,
                                     const VkBufferCreateInfo *buffer_ci,
                                     int mapped, VkBuffer *out_buffer,
                                     void **out_alloc, void **out_mapped);
extern void     vc_vma_destroy_buffer(void *allocator, VkBuffer buffer,
                                      void *alloc);

/* -------------------------------------------------------------------------- */
/*  Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static VkShaderModule
vc_create_shader_module(VkDevice device, const uint32_t *code, size_t size)
{
    VkShaderModuleCreateInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = size;
    ci.pCode    = code;

    VkShaderModule module = VK_NULL_HANDLE;
    VkResult       result = vkCreateShaderModule(device, &ci, NULL, &module);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: display shader module creation failed (%d)\n", result);
        return VK_NULL_HANDLE;
    }
    return module;
}

/* -------------------------------------------------------------------------- */
/*  Display state init                                                         */
/* -------------------------------------------------------------------------- */

void
vc_display_state_init(vc_display_t *disp)
{
    memset(disp, 0, sizeof(vc_display_t));
    atomic_store_explicit(&disp->surface_pending, (uintptr_t) VK_NULL_HANDLE,
                          memory_order_relaxed);
    atomic_store_explicit(&disp->resize_requested, 0, memory_order_relaxed);
    atomic_store_explicit(&disp->teardown_requested, 0, memory_order_relaxed);
    atomic_store_explicit(&disp->teardown_complete, 0, memory_order_relaxed);

    /* VGA passthrough blit state. */
    atomic_store_explicit(&disp->vga_frame_ready, 0, memory_order_relaxed);
    atomic_store_explicit(&disp->vga_buf_idx, 0, memory_order_relaxed);
    atomic_store_explicit(&disp->vga_blit_x, 0, memory_order_relaxed);
    atomic_store_explicit(&disp->vga_blit_y, 0, memory_order_relaxed);
    atomic_store_explicit(&disp->vga_blit_w, 0, memory_order_relaxed);
    atomic_store_explicit(&disp->vga_blit_h, 0, memory_order_relaxed);
    atomic_store_explicit(&disp->vga_buf_ptrs[0], 0, memory_order_relaxed);
    atomic_store_explicit(&disp->vga_buf_ptrs[1], 0, memory_order_relaxed);
}

/* -------------------------------------------------------------------------- */
/*  Surface format selection                                                   */
/* -------------------------------------------------------------------------- */

static VkSurfaceFormatKHR
vc_select_surface_format(vc_ctx_t *ctx, VkSurfaceKHR surface)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, surface,
                                         &count, NULL);

    VkSurfaceFormatKHR *formats = (VkSurfaceFormatKHR *)
        malloc(count * sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, surface,
                                         &count, formats);

    /* Prefer B8G8R8A8_UNORM + SRGB_NONLINEAR (no gamma correction). */
    VkSurfaceFormatKHR chosen = formats[0];
    for (uint32_t i = 0; i < count; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = formats[i];
            break;
        }
    }

    free(formats);
    return chosen;
}

/* -------------------------------------------------------------------------- */
/*  Post-process render pass creation                                          */
/* -------------------------------------------------------------------------- */

static int
vc_pp_render_pass_create(vc_ctx_t *ctx, vc_display_t *disp)
{
    VkAttachmentDescription attachment;
    memset(&attachment, 0, sizeof(attachment));
    attachment.format         = disp->format;
    attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_ref;
    memset(&color_ref, 0, sizeof(color_ref));
    color_ref.attachment = 0;
    color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass;
    memset(&subpass, 0, sizeof(subpass));
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &color_ref;

    VkSubpassDependency dependency;
    memset(&dependency, 0, sizeof(dependency));
    dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp_ci;
    memset(&rp_ci, 0, sizeof(rp_ci));
    rp_ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_ci.attachmentCount = 1;
    rp_ci.pAttachments    = &attachment;
    rp_ci.subpassCount    = 1;
    rp_ci.pSubpasses      = &subpass;
    rp_ci.dependencyCount = 1;
    rp_ci.pDependencies   = &dependency;

    VkResult result = vkCreateRenderPass(ctx->device, &rp_ci, NULL,
                                          &disp->pp_render_pass);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: post-process render pass creation failed (%d)\n",
               result);
        return -1;
    }

    return 0;
}

/* -------------------------------------------------------------------------- */
/*  Sampler + descriptor layout + pool + sets                                  */
/* -------------------------------------------------------------------------- */

static int
vc_pp_descriptors_create(vc_ctx_t *ctx, vc_display_t *disp)
{
    VkResult result;

    /* Sampler: nearest-neighbor, clamp-to-edge. */
    VkSamplerCreateInfo sampler_ci;
    memset(&sampler_ci, 0, sizeof(sampler_ci));
    sampler_ci.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_ci.magFilter     = VK_FILTER_NEAREST;
    sampler_ci.minFilter     = VK_FILTER_NEAREST;
    sampler_ci.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    result = vkCreateSampler(ctx->device, &sampler_ci, NULL, &disp->pp_sampler);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: post-process sampler creation failed (%d)\n", result);
        return -1;
    }

    /* Descriptor set layout: 1 combined image sampler at set=0, binding=0. */
    VkDescriptorSetLayoutBinding binding;
    memset(&binding, 0, sizeof(binding));
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_ci;
    memset(&layout_ci, 0, sizeof(layout_ci));
    layout_ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_ci.bindingCount = 1;
    layout_ci.pBindings    = &binding;

    result = vkCreateDescriptorSetLayout(ctx->device, &layout_ci, NULL,
                                          &disp->pp_desc_layout);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: post-process descriptor set layout failed (%d)\n",
               result);
        return -1;
    }

    /* Descriptor pool: 3 sets (2 offscreen FB + 1 VGA blit). */
    VkDescriptorPoolSize pool_size;
    memset(&pool_size, 0, sizeof(pool_size));
    pool_size.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 3;

    VkDescriptorPoolCreateInfo pool_ci;
    memset(&pool_ci, 0, sizeof(pool_ci));
    pool_ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_ci.maxSets       = 3;
    pool_ci.poolSizeCount = 1;
    pool_ci.pPoolSizes    = &pool_size;

    result = vkCreateDescriptorPool(ctx->device, &pool_ci, NULL,
                                     &disp->pp_desc_pool);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: post-process descriptor pool failed (%d)\n", result);
        return -1;
    }

    /* Allocate 3 descriptor sets (2 offscreen FB + 1 VGA blit). */
    VkDescriptorSetLayout layouts[3] = {
        disp->pp_desc_layout, disp->pp_desc_layout, disp->pp_desc_layout
    };
    VkDescriptorSet all_sets[3];

    VkDescriptorSetAllocateInfo alloc_ci;
    memset(&alloc_ci, 0, sizeof(alloc_ci));
    alloc_ci.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_ci.descriptorPool     = disp->pp_desc_pool;
    alloc_ci.descriptorSetCount = 3;
    alloc_ci.pSetLayouts        = layouts;

    result = vkAllocateDescriptorSets(ctx->device, &alloc_ci, all_sets);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: post-process descriptor set allocation failed (%d)\n",
               result);
        return -1;
    }

    disp->pp_desc_sets[0] = all_sets[0];
    disp->pp_desc_sets[1] = all_sets[1];
    disp->vga_desc_set    = all_sets[2];

    return 0;
}

/* -------------------------------------------------------------------------- */
/*  Post-process pipeline                                                      */
/* -------------------------------------------------------------------------- */

static int
vc_pp_pipeline_create(vc_ctx_t *ctx, vc_display_t *disp)
{
    VkResult result;

    /* Shader modules. */
    disp->pp_vert_shader = vc_create_shader_module(ctx->device,
                                                    postprocess_vert_spv,
                                                    postprocess_vert_spv_size);
    if (disp->pp_vert_shader == VK_NULL_HANDLE)
        return -1;

    disp->pp_frag_shader = vc_create_shader_module(ctx->device,
                                                    postprocess_frag_spv,
                                                    postprocess_frag_spv_size);
    if (disp->pp_frag_shader == VK_NULL_HANDLE)
        return -1;

    /* Pipeline layout: one descriptor set (the offscreen image). */
    VkPipelineLayoutCreateInfo layout_ci;
    memset(&layout_ci, 0, sizeof(layout_ci));
    layout_ci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_ci.setLayoutCount = 1;
    layout_ci.pSetLayouts    = &disp->pp_desc_layout;

    result = vkCreatePipelineLayout(ctx->device, &layout_ci, NULL,
                                     &disp->pp_pipeline_layout);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: post-process pipeline layout failed (%d)\n", result);
        return -1;
    }

    /* Shader stages. */
    VkPipelineShaderStageCreateInfo stages[2];
    memset(stages, 0, sizeof(stages));
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = disp->pp_vert_shader;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = disp->pp_frag_shader;
    stages[1].pName  = "main";

    /* Vertex input: empty (fullscreen triangle uses gl_VertexIndex). */
    VkPipelineVertexInputStateCreateInfo vertex_input;
    memset(&vertex_input, 0, sizeof(vertex_input));
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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

    /* Depth / stencil (disabled). */
    VkPipelineDepthStencilStateCreateInfo depth_stencil;
    memset(&depth_stencil, 0, sizeof(depth_stencil));
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    /* Color blend (disabled, opaque write). */
    VkPipelineColorBlendAttachmentState blend_att;
    memset(&blend_att, 0, sizeof(blend_att));
    blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                             | VK_COLOR_COMPONENT_G_BIT
                             | VK_COLOR_COMPONENT_B_BIT
                             | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend;
    memset(&color_blend, 0, sizeof(color_blend));
    color_blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments    = &blend_att;

    /* Dynamic state (viewport + scissor). */
    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamic_state;
    memset(&dynamic_state, 0, sizeof(dynamic_state));
    dynamic_state.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
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
    pipeline_ci.layout              = disp->pp_pipeline_layout;
    pipeline_ci.renderPass          = disp->pp_render_pass;
    pipeline_ci.subpass             = 0;
    pipeline_ci.basePipelineHandle  = VK_NULL_HANDLE;
    pipeline_ci.basePipelineIndex   = -1;

    result = vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1,
                                        &pipeline_ci, NULL,
                                        &disp->pp_pipeline);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: post-process pipeline creation failed (%d)\n",
               result);
        return -1;
    }

    VC_LOG("VideoCommon: post-process pipeline created\n");
    return 0;
}

/* -------------------------------------------------------------------------- */
/*  Per-frame semaphores                                                       */
/* -------------------------------------------------------------------------- */

static int
vc_display_create_semaphores(vc_ctx_t *ctx, vc_display_t *disp)
{
    VkSemaphoreCreateInfo sem_ci;
    memset(&sem_ci, 0, sizeof(sem_ci));
    sem_ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (uint32_t i = 0; i < VC_NUM_FRAMES; i++) {
        VkResult r1 = vkCreateSemaphore(ctx->device, &sem_ci, NULL,
                                         &disp->image_available_sem[i]);
        VkResult r2 = vkCreateSemaphore(ctx->device, &sem_ci, NULL,
                                         &disp->render_finished_sem[i]);
        if (r1 != VK_SUCCESS || r2 != VK_SUCCESS) {
            VC_LOG("VideoCommon: display semaphore creation failed\n");
            return -1;
        }
    }
    return 0;
}

static void
vc_display_destroy_semaphores(vc_ctx_t *ctx, vc_display_t *disp)
{
    for (uint32_t i = 0; i < VC_NUM_FRAMES; i++) {
        if (disp->image_available_sem[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(ctx->device, disp->image_available_sem[i], NULL);
            disp->image_available_sem[i] = VK_NULL_HANDLE;
        }
        if (disp->render_finished_sem[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(ctx->device, disp->render_finished_sem[i], NULL);
            disp->render_finished_sem[i] = VK_NULL_HANDLE;
        }
    }
}

/* -------------------------------------------------------------------------- */
/*  Swapchain image views                                                      */
/* -------------------------------------------------------------------------- */

static void
vc_display_destroy_image_views(vc_ctx_t *ctx, vc_display_t *disp)
{
    for (uint32_t i = 0; i < disp->image_count; i++) {
        if (disp->image_views[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(ctx->device, disp->image_views[i], NULL);
            disp->image_views[i] = VK_NULL_HANDLE;
        }
    }
}

static int
vc_display_create_image_views(vc_ctx_t *ctx, vc_display_t *disp)
{
    for (uint32_t i = 0; i < disp->image_count; i++) {
        VkImageViewCreateInfo view_ci;
        memset(&view_ci, 0, sizeof(view_ci));
        view_ci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_ci.image                           = disp->images[i];
        view_ci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        view_ci.format                          = disp->format;
        view_ci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        view_ci.subresourceRange.levelCount     = 1;
        view_ci.subresourceRange.layerCount     = 1;

        VkResult result = vkCreateImageView(ctx->device, &view_ci, NULL,
                                             &disp->image_views[i]);
        if (result != VK_SUCCESS) {
            VC_LOG("VideoCommon: swapchain image view %u failed (%d)\n",
                   i, result);
            return -1;
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/*  Post-process framebuffers (one per swapchain image)                        */
/* -------------------------------------------------------------------------- */

static void
vc_display_destroy_pp_framebuffers(vc_ctx_t *ctx, vc_display_t *disp)
{
    for (uint32_t i = 0; i < disp->image_count; i++) {
        if (disp->pp_framebuffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(ctx->device, disp->pp_framebuffers[i], NULL);
            disp->pp_framebuffers[i] = VK_NULL_HANDLE;
        }
    }
}

static int
vc_display_create_pp_framebuffers(vc_ctx_t *ctx, vc_display_t *disp)
{
    for (uint32_t i = 0; i < disp->image_count; i++) {
        VkFramebufferCreateInfo fb_ci;
        memset(&fb_ci, 0, sizeof(fb_ci));
        fb_ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_ci.renderPass      = disp->pp_render_pass;
        fb_ci.attachmentCount = 1;
        fb_ci.pAttachments    = &disp->image_views[i];
        fb_ci.width           = disp->extent.width;
        fb_ci.height          = disp->extent.height;
        fb_ci.layers          = 1;

        VkResult result = vkCreateFramebuffer(ctx->device, &fb_ci, NULL,
                                               &disp->pp_framebuffers[i]);
        if (result != VK_SUCCESS) {
            VC_LOG("VideoCommon: post-process framebuffer %u failed (%d)\n",
                   i, result);
            return -1;
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/*  Swapchain creation / recreation / destruction                              */
/* -------------------------------------------------------------------------- */

static int
vc_swapchain_create(vc_ctx_t *ctx, vc_display_t *disp)
{
    VkResult result;

    /* Verify that our graphics queue supports present. */
    VkBool32 present_supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(ctx->physical_device,
                                          ctx->queue_family,
                                          disp->surface,
                                          &present_supported);
    if (!present_supported) {
        VC_LOG("VideoCommon: queue family %u does not support present\n",
               ctx->queue_family);
        return -1;
    }

    /* Query surface capabilities. */
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->physical_device,
                                               disp->surface, &caps);

    /* Image count: prefer 3 (triple buffer). */
    uint32_t image_count = 3;
    if (caps.minImageCount > image_count)
        image_count = caps.minImageCount;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount)
        image_count = caps.maxImageCount;
    if (image_count > VC_MAX_SWAPCHAIN_IMAGES)
        image_count = VC_MAX_SWAPCHAIN_IMAGES;

    /* Format. */
    VkSurfaceFormatKHR format = vc_select_surface_format(ctx, disp->surface);
    disp->format = format.format;

    /* Extent. */
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width  = atomic_load_explicit(&disp->resize_width,
                                              memory_order_relaxed);
        extent.height = atomic_load_explicit(&disp->resize_height,
                                              memory_order_relaxed);
        if (extent.width == 0)
            extent.width = 640;
        if (extent.height == 0)
            extent.height = 480;
    }
    /* Clamp to valid range. */
    if (extent.width < caps.minImageExtent.width)
        extent.width = caps.minImageExtent.width;
    if (extent.height < caps.minImageExtent.height)
        extent.height = caps.minImageExtent.height;
    if (extent.width > caps.maxImageExtent.width)
        extent.width = caps.maxImageExtent.width;
    if (extent.height > caps.maxImageExtent.height)
        extent.height = caps.maxImageExtent.height;
    disp->extent = extent;

    /* Create swapchain. */
    VkSwapchainCreateInfoKHR ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = disp->surface;
    ci.minImageCount    = image_count;
    ci.imageFormat      = format.format;
    ci.imageColorSpace  = format.colorSpace;
    ci.imageExtent      = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform     = caps.currentTransform;
    ci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped          = VK_TRUE;
    ci.oldSwapchain     = disp->swapchain; /* VK_NULL_HANDLE on first create. */

    result = vkCreateSwapchainKHR(ctx->device, &ci, NULL, &disp->swapchain);

    /* Destroy old swapchain if we had one. */
    if (ci.oldSwapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(ctx->device, ci.oldSwapchain, NULL);
    }

    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: vkCreateSwapchainKHR failed (%d)\n", result);
        disp->swapchain = VK_NULL_HANDLE;
        return -1;
    }

    /* Retrieve swapchain images. */
    vkGetSwapchainImagesKHR(ctx->device, disp->swapchain,
                             &disp->image_count, NULL);
    if (disp->image_count > VC_MAX_SWAPCHAIN_IMAGES) {
        VC_LOG("VideoCommon: swapchain image count %u exceeds max %d\n",
               disp->image_count, VC_MAX_SWAPCHAIN_IMAGES);
        disp->image_count = VC_MAX_SWAPCHAIN_IMAGES;
    }
    vkGetSwapchainImagesKHR(ctx->device, disp->swapchain,
                             &disp->image_count, disp->images);

    /* Create image views. */
    if (vc_display_create_image_views(ctx, disp) != 0)
        return -1;

    /* Create post-process framebuffers. */
    if (vc_display_create_pp_framebuffers(ctx, disp) != 0)
        return -1;

    VC_LOG("VideoCommon: swapchain created (%ux%u, %u images, format %d)\n",
           extent.width, extent.height, disp->image_count, disp->format);
    return 0;
}

static void
vc_swapchain_destroy(vc_ctx_t *ctx, vc_display_t *disp)
{
    vkDeviceWaitIdle(ctx->device);

    vc_display_destroy_pp_framebuffers(ctx, disp);
    vc_display_destroy_image_views(ctx, disp);

    if (disp->swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(ctx->device, disp->swapchain, NULL);
        disp->swapchain = VK_NULL_HANDLE;
    }

    disp->image_count = 0;
}

/* -------------------------------------------------------------------------- */
/*  Full display create / destroy                                              */
/* -------------------------------------------------------------------------- */

int
vc_display_create(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{
    vc_display_t *disp = &gpu_st->disp;

    /* Create the swapchain first (sets disp->format needed for render pass). */
    if (vc_swapchain_create(ctx, disp) != 0)
        return -1;

    /* Post-process render pass (uses swapchain format). */
    if (vc_pp_render_pass_create(ctx, disp) != 0) {
        vc_swapchain_destroy(ctx, disp);
        return -1;
    }

    /* Descriptors (sampler, layout, pool, sets). */
    if (vc_pp_descriptors_create(ctx, disp) != 0) {
        vc_swapchain_destroy(ctx, disp);
        return -1;
    }

    /* Pipeline. */
    if (vc_pp_pipeline_create(ctx, disp) != 0) {
        vc_swapchain_destroy(ctx, disp);
        return -1;
    }

    /* Semaphores. */
    if (vc_display_create_semaphores(ctx, disp) != 0) {
        vc_swapchain_destroy(ctx, disp);
        return -1;
    }

    /* Update descriptor sets to point to the offscreen images. */
    vc_display_update_descriptors(ctx, gpu_st);

    /* Recreate post-process framebuffers (swapchain may have changed). */
    vc_display_destroy_pp_framebuffers(ctx, disp);
    if (vc_display_create_pp_framebuffers(ctx, disp) != 0) {
        vc_swapchain_destroy(ctx, disp);
        return -1;
    }

    /* Signal that the VK display is active. */
    if (disp->display_active_ptr)
        *disp->display_active_ptr = 1;

    VC_LOG("VideoCommon: display fully initialised\n");
    return 0;
}

void
vc_display_destroy(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{
    vc_display_t *disp = &gpu_st->disp;

    /* Clear display active flag. */
    if (disp->display_active_ptr)
        *disp->display_active_ptr = 0;

    vkDeviceWaitIdle(ctx->device);

    vc_display_destroy_semaphores(ctx, disp);

    if (disp->pp_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(ctx->device, disp->pp_pipeline, NULL);
        disp->pp_pipeline = VK_NULL_HANDLE;
    }

    if (disp->pp_pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(ctx->device, disp->pp_pipeline_layout, NULL);
        disp->pp_pipeline_layout = VK_NULL_HANDLE;
    }

    vc_display_destroy_vga_resources(ctx, gpu_st);

    if (disp->pp_desc_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(ctx->device, disp->pp_desc_pool, NULL);
        disp->pp_desc_pool = VK_NULL_HANDLE;
        disp->pp_desc_sets[0] = VK_NULL_HANDLE;
        disp->pp_desc_sets[1] = VK_NULL_HANDLE;
        disp->vga_desc_set    = VK_NULL_HANDLE;
    }

    if (disp->pp_desc_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(ctx->device, disp->pp_desc_layout, NULL);
        disp->pp_desc_layout = VK_NULL_HANDLE;
    }

    if (disp->pp_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(ctx->device, disp->pp_sampler, NULL);
        disp->pp_sampler = VK_NULL_HANDLE;
    }

    if (disp->pp_render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(ctx->device, disp->pp_render_pass, NULL);
        disp->pp_render_pass = VK_NULL_HANDLE;
    }

    if (disp->pp_vert_shader != VK_NULL_HANDLE) {
        vkDestroyShaderModule(ctx->device, disp->pp_vert_shader, NULL);
        disp->pp_vert_shader = VK_NULL_HANDLE;
    }

    if (disp->pp_frag_shader != VK_NULL_HANDLE) {
        vkDestroyShaderModule(ctx->device, disp->pp_frag_shader, NULL);
        disp->pp_frag_shader = VK_NULL_HANDLE;
    }

    vc_swapchain_destroy(ctx, disp);
}

/* -------------------------------------------------------------------------- */
/*  Swapchain recreation                                                       */
/* -------------------------------------------------------------------------- */

int
vc_display_recreate_swapchain(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{
    vc_display_t *disp = &gpu_st->disp;

    vkDeviceWaitIdle(ctx->device);

    vc_display_destroy_pp_framebuffers(ctx, disp);
    vc_display_destroy_image_views(ctx, disp);
    vc_display_destroy_semaphores(ctx, disp);

    /* vc_swapchain_create uses oldSwapchain for recreation. */
    if (vc_swapchain_create(ctx, disp) != 0) {
        VC_LOG("VideoCommon: swapchain recreation failed\n");
        return -1;
    }

    if (vc_display_create_semaphores(ctx, disp) != 0)
        return -1;

    vc_display_update_descriptors(ctx, gpu_st);

    VC_LOG("VideoCommon: swapchain recreated (%ux%u)\n",
           disp->extent.width, disp->extent.height);
    return 0;
}

/* -------------------------------------------------------------------------- */
/*  Descriptor set update                                                      */
/* -------------------------------------------------------------------------- */

void
vc_display_update_descriptors(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{
    vc_display_t *disp = &gpu_st->disp;

    if (disp->pp_desc_sets[0] == VK_NULL_HANDLE)
        return;

    for (int i = 0; i < 2; i++) {
        if (gpu_st->rp.fb[i].color_view == VK_NULL_HANDLE)
            continue;

        VkDescriptorImageInfo image_info;
        memset(&image_info, 0, sizeof(image_info));
        image_info.sampler     = disp->pp_sampler;
        image_info.imageView   = gpu_st->rp.fb[i].color_view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write;
        memset(&write, 0, sizeof(write));
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = disp->pp_desc_sets[i];
        write.dstBinding      = 0;
        write.descriptorCount = 1;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo      = &image_info;

        vkUpdateDescriptorSets(ctx->device, 1, &write, 0, NULL);
    }
}

/* -------------------------------------------------------------------------- */
/*  GPU thread display tick                                                    */
/* -------------------------------------------------------------------------- */

void
vc_display_tick(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{
    vc_display_t *disp = &gpu_st->disp;

    /* Check for teardown request. */
    if (atomic_load_explicit(&disp->teardown_requested, memory_order_acquire)) {
        VC_LOG("VideoCommon: display teardown requested\n");
        vc_display_destroy(ctx, gpu_st);
        atomic_store_explicit(&disp->teardown_complete, 1, memory_order_release);
        return;
    }

    /* Check for new surface. */
    uintptr_t new_surface_raw = atomic_exchange_explicit(
        &disp->surface_pending, (uintptr_t) VK_NULL_HANDLE,
        memory_order_acquire);
    VkSurfaceKHR new_surface = (VkSurfaceKHR) new_surface_raw;

    if (new_surface != VK_NULL_HANDLE) {
        VC_LOG("VideoCommon: new surface received, creating display\n");

        /* Destroy old display if any. */
        if (disp->swapchain != VK_NULL_HANDLE)
            vc_display_destroy(ctx, gpu_st);

        disp->surface = new_surface;
        if (vc_display_create(ctx, gpu_st) != 0) {
            VC_LOG("VideoCommon: display creation failed\n");
            disp->surface = VK_NULL_HANDLE;
        }
    }

    /* Check for resize. */
    if (atomic_exchange_explicit(&disp->resize_requested, 0,
                                  memory_order_acquire)) {
        if (disp->swapchain != VK_NULL_HANDLE) {
            vc_display_recreate_swapchain(ctx, gpu_st);
        }
    }

    /* Check for VGA passthrough frames.
       Only present VGA frames when no 3D render pass is active and
       the swapchain is ready. */
    if (!gpu_st->render_pass_active &&
        disp->swapchain != VK_NULL_HANDLE &&
        atomic_load_explicit(&disp->vga_frame_ready, memory_order_relaxed)) {
        vc_display_present_vga(ctx, gpu_st);
    }
}

/* -------------------------------------------------------------------------- */
/*  Post-process blit + present                                                */
/* -------------------------------------------------------------------------- */

int
vc_display_present(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st,
                   VkCommandBuffer cmd_buf, uint32_t frame_index)
{
    vc_display_t *disp = &gpu_st->disp;

    if (disp->swapchain == VK_NULL_HANDLE)
        return -1;

    /* Acquire swapchain image. */
    uint32_t image_idx;
    VkResult acq = vkAcquireNextImageKHR(
        ctx->device, disp->swapchain, UINT64_MAX,
        disp->image_available_sem[frame_index],
        VK_NULL_HANDLE, &image_idx);

    if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
        return 1; /* Caller should recreate swapchain. */
    }
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
        VC_LOG("VideoCommon: vkAcquireNextImageKHR failed (%d)\n", acq);
        return -1;
    }

    /* Barrier: offscreen color -> SHADER_READ_ONLY_OPTIMAL. */
    int back_idx = gpu_st->rp.back_index;
    VkImageMemoryBarrier barrier;
    memset(&barrier, 0, sizeof(barrier));
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = gpu_st->rp.fb[back_idx].color_image;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;

    vkCmdPipelineBarrier(cmd_buf,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);

    /* Begin post-process render pass. */
    VkClearValue clear_value;
    memset(&clear_value, 0, sizeof(clear_value));

    VkRenderPassBeginInfo rp_begin;
    memset(&rp_begin, 0, sizeof(rp_begin));
    rp_begin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass        = disp->pp_render_pass;
    rp_begin.framebuffer       = disp->pp_framebuffers[image_idx];
    rp_begin.renderArea.extent = disp->extent;
    rp_begin.clearValueCount   = 1;
    rp_begin.pClearValues      = &clear_value;

    vkCmdBeginRenderPass(cmd_buf, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    /* Set viewport and scissor to swapchain extent. */
    VkViewport viewport;
    memset(&viewport, 0, sizeof(viewport));
    viewport.width    = (float) disp->extent.width;
    viewport.height   = (float) disp->extent.height;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd_buf, 0, 1, &viewport);

    VkRect2D scissor;
    memset(&scissor, 0, sizeof(scissor));
    scissor.extent = disp->extent;
    vkCmdSetScissor(cmd_buf, 0, 1, &scissor);

    /* Bind post-process pipeline and descriptor set. */
    vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      disp->pp_pipeline);
    vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            disp->pp_pipeline_layout, 0, 1,
                            &disp->pp_desc_sets[back_idx], 0, NULL);

    /* Draw fullscreen triangle (3 vertices, no vertex buffer). */
    vkCmdDraw(cmd_buf, 3, 1, 0, 0);

    /* End post-process render pass (transitions to PRESENT_SRC). */
    vkCmdEndRenderPass(cmd_buf);

    /* Barrier: offscreen color -> COLOR_ATTACHMENT_OPTIMAL (restore). */
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    vkCmdPipelineBarrier(cmd_buf,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);

    /* End command buffer. */
    vkEndCommandBuffer(cmd_buf);

    /* Submit with semaphore sync. */
    VkPipelineStageFlags wait_stage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit;
    memset(&submit, 0, sizeof(submit));
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &disp->image_available_sem[frame_index];
    submit.pWaitDstStageMask    = &wait_stage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &cmd_buf;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &disp->render_finished_sem[frame_index];

    vc_frame_t *f = &gpu_st->frame[frame_index];
    VkResult sub_result = vkQueueSubmit(ctx->queue, 1, &submit, f->fence);
    if (sub_result != VK_SUCCESS) {
        VC_LOG("VideoCommon: display vkQueueSubmit failed (%d)\n", sub_result);
        return -1;
    }
    f->submitted = 1;

    /* Present. */
    VkPresentInfoKHR present;
    memset(&present, 0, sizeof(present));
    present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = &disp->render_finished_sem[frame_index];
    present.swapchainCount     = 1;
    present.pSwapchains        = &disp->swapchain;
    present.pImageIndices      = &image_idx;

    VkResult pres = vkQueuePresentKHR(ctx->queue, &present);
    if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) {
        return 1; /* Caller should recreate swapchain. */
    }
    if (pres != VK_SUCCESS) {
        VC_LOG("VideoCommon: vkQueuePresentKHR failed (%d)\n", pres);
        return -1;
    }

    return 0;
}

/* -------------------------------------------------------------------------- */
/*  VGA passthrough blit resources                                             */
/* -------------------------------------------------------------------------- */

/* VGA blit staging buffer size: 2048*2048 pixels * 4 bytes (BGRA8). */
#define VC_VGA_STAGING_SIZE (2048 * 2048 * 4)

int
vc_display_create_vga_resources(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{
    vc_display_t *disp = &gpu_st->disp;
    VkResult      result;

    if (disp->vga_resources_created)
        return 0;

    /* Create a VGA blit VkImage (B8G8R8A8 to match Qt QImage::Format_RGB32).
       Start at 2048x2048 (max resolution the blit chain supports). */
    disp->vga_tex_width  = 2048;
    disp->vga_tex_height = 2048;

    VkImageCreateInfo image_ci;
    memset(&image_ci, 0, sizeof(image_ci));
    image_ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_ci.imageType     = VK_IMAGE_TYPE_2D;
    image_ci.format        = VK_FORMAT_B8G8R8A8_UNORM;
    image_ci.extent.width  = disp->vga_tex_width;
    image_ci.extent.height = disp->vga_tex_height;
    image_ci.extent.depth  = 1;
    image_ci.mipLevels     = 1;
    image_ci.arrayLayers   = 1;
    image_ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    image_ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    image_ci.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT
                           | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    result = vc_vma_create_image(ctx->allocator, &image_ci,
                                 &disp->vga_image, &disp->vga_image_alloc);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: VGA blit image creation failed (%d)\n", result);
        return -1;
    }

    /* Image view. */
    VkImageViewCreateInfo view_ci;
    memset(&view_ci, 0, sizeof(view_ci));
    view_ci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_ci.image                           = disp->vga_image;
    view_ci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format                          = VK_FORMAT_B8G8R8A8_UNORM;
    view_ci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view_ci.subresourceRange.levelCount     = 1;
    view_ci.subresourceRange.layerCount     = 1;

    result = vkCreateImageView(ctx->device, &view_ci, NULL,
                                &disp->vga_image_view);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: VGA blit image view creation failed (%d)\n", result);
        return -1;
    }

    /* Staging buffer (host-visible, persistently mapped). */
    VkBufferCreateInfo buf_ci;
    memset(&buf_ci, 0, sizeof(buf_ci));
    buf_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_ci.size  = VC_VGA_STAGING_SIZE;
    buf_ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    result = vc_vma_create_buffer(ctx->allocator, &buf_ci, 1,
                                  &disp->vga_staging_buf,
                                  &disp->vga_staging_alloc,
                                  &disp->vga_staging_mapped);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: VGA blit staging buffer creation failed (%d)\n", result);
        return -1;
    }

    /* Command pool and buffer for VGA blit uploads. */
    VkCommandPoolCreateInfo pool_ci;
    memset(&pool_ci, 0, sizeof(pool_ci));
    pool_ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_ci.queueFamilyIndex = ctx->queue_family;

    result = vkCreateCommandPool(ctx->device, &pool_ci, NULL,
                                  &disp->vga_cmd_pool);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: VGA blit command pool creation failed (%d)\n", result);
        return -1;
    }

    VkCommandBufferAllocateInfo alloc_ci;
    memset(&alloc_ci, 0, sizeof(alloc_ci));
    alloc_ci.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_ci.commandPool        = disp->vga_cmd_pool;
    alloc_ci.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_ci.commandBufferCount = 1;

    result = vkAllocateCommandBuffers(ctx->device, &alloc_ci,
                                       &disp->vga_cmd_buf);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: VGA blit command buffer alloc failed (%d)\n", result);
        return -1;
    }

    /* Update the VGA descriptor set. */
    if (disp->vga_desc_set != VK_NULL_HANDLE && disp->pp_sampler != VK_NULL_HANDLE) {
        VkDescriptorImageInfo img_info;
        memset(&img_info, 0, sizeof(img_info));
        img_info.sampler     = disp->pp_sampler;
        img_info.imageView   = disp->vga_image_view;
        img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write;
        memset(&write, 0, sizeof(write));
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = disp->vga_desc_set;
        write.dstBinding      = 0;
        write.descriptorCount = 1;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo      = &img_info;

        vkUpdateDescriptorSets(ctx->device, 1, &write, 0, NULL);
    }

    disp->vga_resources_created = 1;
    VC_LOG("VideoCommon: VGA blit resources created (%ux%u)\n",
           disp->vga_tex_width, disp->vga_tex_height);
    return 0;
}

void
vc_display_destroy_vga_resources(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{
    vc_display_t *disp = &gpu_st->disp;

    if (!disp->vga_resources_created)
        return;

    if (disp->vga_cmd_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(ctx->device, disp->vga_cmd_pool, NULL);
        disp->vga_cmd_pool = VK_NULL_HANDLE;
        disp->vga_cmd_buf  = VK_NULL_HANDLE;
    }

    if (disp->vga_staging_buf != VK_NULL_HANDLE) {
        vc_vma_destroy_buffer(ctx->allocator, disp->vga_staging_buf,
                              disp->vga_staging_alloc);
        disp->vga_staging_buf    = VK_NULL_HANDLE;
        disp->vga_staging_alloc  = NULL;
        disp->vga_staging_mapped = NULL;
    }

    if (disp->vga_image_view != VK_NULL_HANDLE) {
        vkDestroyImageView(ctx->device, disp->vga_image_view, NULL);
        disp->vga_image_view = VK_NULL_HANDLE;
    }

    if (disp->vga_image != VK_NULL_HANDLE) {
        vc_vma_destroy_image(ctx->allocator, disp->vga_image,
                             disp->vga_image_alloc);
        disp->vga_image       = VK_NULL_HANDLE;
        disp->vga_image_alloc = NULL;
    }

    /* vga_desc_set is freed with the pool -- just NULL the pointer. */
    disp->vga_desc_set          = VK_NULL_HANDLE;
    disp->vga_resources_created = 0;
}

/* -------------------------------------------------------------------------- */
/*  VGA passthrough present                                                    */
/* -------------------------------------------------------------------------- */

/* Helper: recreate the VGA VkImage + view + descriptor when
   the blit dimensions change.  Called from vc_display_present_vga(). */
static int
vc_vga_ensure_image_size(vc_ctx_t *ctx, vc_display_t *disp,
                          uint32_t w, uint32_t h)
{
    if (disp->vga_tex_width == w && disp->vga_tex_height == h
        && disp->vga_image != VK_NULL_HANDLE)
        return 0;

    /* Need to resize -- destroy old image + view. */
    vkDeviceWaitIdle(ctx->device);

    if (disp->vga_image_view != VK_NULL_HANDLE) {
        vkDestroyImageView(ctx->device, disp->vga_image_view, NULL);
        disp->vga_image_view = VK_NULL_HANDLE;
    }
    if (disp->vga_image != VK_NULL_HANDLE) {
        vc_vma_destroy_image(ctx->allocator, disp->vga_image,
                             disp->vga_image_alloc);
        disp->vga_image       = VK_NULL_HANDLE;
        disp->vga_image_alloc = NULL;
    }

    disp->vga_tex_width  = w;
    disp->vga_tex_height = h;

    VkImageCreateInfo image_ci;
    memset(&image_ci, 0, sizeof(image_ci));
    image_ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_ci.imageType     = VK_IMAGE_TYPE_2D;
    image_ci.format        = VK_FORMAT_B8G8R8A8_UNORM;
    image_ci.extent.width  = w;
    image_ci.extent.height = h;
    image_ci.extent.depth  = 1;
    image_ci.mipLevels     = 1;
    image_ci.arrayLayers   = 1;
    image_ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    image_ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    image_ci.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT
                           | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult result = vc_vma_create_image(ctx->allocator, &image_ci,
                                          &disp->vga_image,
                                          &disp->vga_image_alloc);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: VGA image resize to %ux%u failed (%d)\n",
               w, h, result);
        return -1;
    }

    VkImageViewCreateInfo view_ci;
    memset(&view_ci, 0, sizeof(view_ci));
    view_ci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_ci.image                           = disp->vga_image;
    view_ci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format                          = VK_FORMAT_B8G8R8A8_UNORM;
    view_ci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view_ci.subresourceRange.levelCount     = 1;
    view_ci.subresourceRange.layerCount     = 1;

    result = vkCreateImageView(ctx->device, &view_ci, NULL,
                                &disp->vga_image_view);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: VGA image view resize failed (%d)\n", result);
        return -1;
    }

    /* Update the VGA descriptor set. */
    if (disp->vga_desc_set != VK_NULL_HANDLE &&
        disp->pp_sampler != VK_NULL_HANDLE) {
        VkDescriptorImageInfo img_info;
        memset(&img_info, 0, sizeof(img_info));
        img_info.sampler     = disp->pp_sampler;
        img_info.imageView   = disp->vga_image_view;
        img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write;
        memset(&write, 0, sizeof(write));
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = disp->vga_desc_set;
        write.dstBinding      = 0;
        write.descriptorCount = 1;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo      = &img_info;

        vkUpdateDescriptorSets(ctx->device, 1, &write, 0, NULL);
    }

    VC_LOG("VideoCommon: VGA image resized to %ux%u\n", w, h);
    return 0;
}

int
vc_display_present_vga(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{
    vc_display_t *disp = &gpu_st->disp;

    /* Check if a VGA frame is ready. */
    if (!atomic_exchange_explicit(&disp->vga_frame_ready, 0,
                                   memory_order_acquire))
        return -1;

    if (disp->swapchain == VK_NULL_HANDLE)
        return -1;

    /* Read the blit parameters. */
    int buf_idx = atomic_load_explicit(&disp->vga_buf_idx, memory_order_relaxed);
    int bx      = atomic_load_explicit(&disp->vga_blit_x, memory_order_relaxed);
    int by      = atomic_load_explicit(&disp->vga_blit_y, memory_order_relaxed);
    int bw      = atomic_load_explicit(&disp->vga_blit_w, memory_order_relaxed);
    int bh      = atomic_load_explicit(&disp->vga_blit_h, memory_order_relaxed);

    if (bw <= 0 || bh <= 0 || bw > 2048 || bh > 2048)
        return -1;

    /* Get the source buffer pointer. */
    uintptr_t buf_ptr = atomic_load_explicit(&disp->vga_buf_ptrs[buf_idx],
                                              memory_order_acquire);
    if (buf_ptr == 0)
        return -1;

    /* Create VGA resources if not yet done. */
    if (!disp->vga_resources_created) {
        if (vc_display_create_vga_resources(ctx, gpu_st) != 0)
            return -1;
    }

    /* Resize VGA VkImage to match the blit dimensions if needed.
       This ensures the fullscreen triangle shader's UV (0,1) range
       maps correctly across the valid pixel data. */
    if (vc_vga_ensure_image_size(ctx, disp, (uint32_t) bw, (uint32_t) bh) != 0)
        return -1;

    /* Copy the blit region from the image buffer to the staging buffer.
       The source is Format_RGB32 (BGRA8), row pitch = 2048 * 4.
       We pack into the staging buffer with tight row pitch (bw * 4). */
    const uint8_t *src = (const uint8_t *) buf_ptr;
    uint8_t       *dst = (uint8_t *) disp->vga_staging_mapped;
    int src_pitch = 2048 * 4;
    int dst_pitch = bw * 4;

    for (int y = by; y < by + bh; y++) {
        memcpy(dst + (y - by) * dst_pitch,
               src + y * src_pitch + bx * 4,
               (size_t) dst_pitch);
    }

    /* Use the per-frame resources for VGA blit. */
    uint32_t   frame_index = gpu_st->frame_index;
    vc_frame_t *f = &gpu_st->frame[frame_index];

    /* Wait for this frame's fence if needed. */
    if (f->submitted) {
        vkWaitForFences(ctx->device, 1, &f->fence, VK_TRUE, UINT64_MAX);
        vkResetFences(ctx->device, 1, &f->fence);
        f->submitted = 0;
    }

    VkCommandBuffer cmd_buf = disp->vga_cmd_buf;
    vkResetCommandBuffer(cmd_buf, 0);

    VkCommandBufferBeginInfo begin_ci;
    memset(&begin_ci, 0, sizeof(begin_ci));
    begin_ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_ci.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd_buf, &begin_ci);

    /* Barrier: VGA image UNDEFINED -> TRANSFER_DST. */
    VkImageMemoryBarrier barrier;
    memset(&barrier, 0, sizeof(barrier));
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask       = 0;
    barrier.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = disp->vga_image;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.layerCount     = 1;

    vkCmdPipelineBarrier(cmd_buf,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);

    /* Copy staging buffer -> VGA image at (0,0).
       The staging buffer contains the blit region packed tightly. */
    VkBufferImageCopy copy_region;
    memset(&copy_region, 0, sizeof(copy_region));
    copy_region.bufferOffset                    = 0;
    copy_region.bufferRowLength                 = (uint32_t) bw;
    copy_region.bufferImageHeight               = (uint32_t) bh;
    copy_region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.layerCount     = 1;
    copy_region.imageOffset.x                   = 0;
    copy_region.imageOffset.y                   = 0;
    copy_region.imageExtent.width               = (uint32_t) bw;
    copy_region.imageExtent.height              = (uint32_t) bh;
    copy_region.imageExtent.depth               = 1;

    vkCmdCopyBufferToImage(cmd_buf, disp->vga_staging_buf, disp->vga_image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            1, &copy_region);

    /* Barrier: VGA image TRANSFER_DST -> SHADER_READ_ONLY. */
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    vkCmdPipelineBarrier(cmd_buf,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);

    /* Acquire swapchain image. */
    uint32_t image_idx;
    VkResult acq = vkAcquireNextImageKHR(
        ctx->device, disp->swapchain, UINT64_MAX,
        disp->image_available_sem[frame_index],
        VK_NULL_HANDLE, &image_idx);

    if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
        vkEndCommandBuffer(cmd_buf);
        vc_display_recreate_swapchain(ctx, gpu_st);
        return 0;
    }
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
        vkEndCommandBuffer(cmd_buf);
        VC_LOG("VideoCommon: VGA blit vkAcquireNextImageKHR failed (%d)\n", acq);
        return -1;
    }

    /* Begin post-process render pass (same pipeline, VGA descriptor set). */
    VkClearValue clear_value;
    memset(&clear_value, 0, sizeof(clear_value));

    VkRenderPassBeginInfo rp_begin;
    memset(&rp_begin, 0, sizeof(rp_begin));
    rp_begin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass        = disp->pp_render_pass;
    rp_begin.framebuffer       = disp->pp_framebuffers[image_idx];
    rp_begin.renderArea.extent = disp->extent;
    rp_begin.clearValueCount   = 1;
    rp_begin.pClearValues      = &clear_value;

    vkCmdBeginRenderPass(cmd_buf, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    /* Viewport + scissor. */
    VkViewport viewport;
    memset(&viewport, 0, sizeof(viewport));
    viewport.width    = (float) disp->extent.width;
    viewport.height   = (float) disp->extent.height;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd_buf, 0, 1, &viewport);

    VkRect2D scissor;
    memset(&scissor, 0, sizeof(scissor));
    scissor.extent = disp->extent;
    vkCmdSetScissor(cmd_buf, 0, 1, &scissor);

    /* Bind pipeline and VGA descriptor set. */
    vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      disp->pp_pipeline);
    vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            disp->pp_pipeline_layout, 0, 1,
                            &disp->vga_desc_set, 0, NULL);

    /* Draw fullscreen triangle. */
    vkCmdDraw(cmd_buf, 3, 1, 0, 0);

    /* End render pass. */
    vkCmdEndRenderPass(cmd_buf);

    /* End command buffer. */
    vkEndCommandBuffer(cmd_buf);

    /* Submit with semaphore sync. */
    VkPipelineStageFlags wait_stage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit;
    memset(&submit, 0, sizeof(submit));
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &disp->image_available_sem[frame_index];
    submit.pWaitDstStageMask    = &wait_stage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &cmd_buf;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &disp->render_finished_sem[frame_index];

    VkResult sub_result = vkQueueSubmit(ctx->queue, 1, &submit, f->fence);
    if (sub_result != VK_SUCCESS) {
        VC_LOG("VideoCommon: VGA blit vkQueueSubmit failed (%d)\n", sub_result);
        return -1;
    }
    f->submitted = 1;

    /* Present. */
    VkPresentInfoKHR present;
    memset(&present, 0, sizeof(present));
    present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = &disp->render_finished_sem[frame_index];
    present.swapchainCount     = 1;
    present.pSwapchains        = &disp->swapchain;
    present.pImageIndices      = &image_idx;

    VkResult pres = vkQueuePresentKHR(ctx->queue, &present);
    if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) {
        vc_display_recreate_swapchain(ctx, gpu_st);
    } else if (pres != VK_SUCCESS) {
        VC_LOG("VideoCommon: VGA blit vkQueuePresentKHR failed (%d)\n", pres);
    }

    /* Advance frame index. */
    gpu_st->frame_index = (gpu_st->frame_index + 1) % VC_NUM_FRAMES;

    return 0;
}

/* -------------------------------------------------------------------------- */
/*  Public API -- called from GUI thread                                       */
/* -------------------------------------------------------------------------- */

void
vc_display_set_surface(vc_ctx_t *ctx, VkSurfaceKHR surface)
{
    if (!ctx)
        return;

    vc_gpu_state_t *gpu_st = (vc_gpu_state_t *) ctx->render_data;
    if (!gpu_st)
        return;

    VC_LOG("VideoCommon: surface handle set by GUI thread\n");
    atomic_store_explicit(&gpu_st->disp.surface_pending,
                          (uintptr_t) surface, memory_order_release);
}

void
vc_display_signal_resize(vc_ctx_t *ctx, uint32_t width, uint32_t height)
{
    if (!ctx)
        return;

    vc_gpu_state_t *gpu_st = (vc_gpu_state_t *) ctx->render_data;
    if (!gpu_st)
        return;

    atomic_store_explicit(&gpu_st->disp.resize_width, width,
                          memory_order_relaxed);
    atomic_store_explicit(&gpu_st->disp.resize_height, height,
                          memory_order_relaxed);
    atomic_store_explicit(&gpu_st->disp.resize_requested, 1,
                          memory_order_release);
}

void
vc_display_request_teardown(vc_ctx_t *ctx)
{
    if (!ctx)
        return;

    vc_gpu_state_t *gpu_st = (vc_gpu_state_t *) ctx->render_data;
    if (!gpu_st)
        return;

    atomic_store_explicit(&gpu_st->disp.teardown_requested, 1,
                          memory_order_release);
}

void
vc_display_wait_teardown(vc_ctx_t *ctx)
{
    if (!ctx)
        return;

    vc_gpu_state_t *gpu_st = (vc_gpu_state_t *) ctx->render_data;
    if (!gpu_st)
        return;

    /* Spin-yield until the GPU thread confirms teardown. */
    while (!atomic_load_explicit(&gpu_st->disp.teardown_complete,
                                  memory_order_acquire)) {
        /* Small sleep to avoid burning CPU. */
#if defined(_WIN32)
        SwitchToThread();
#else
        sched_yield();
#endif
    }
}

/* -------------------------------------------------------------------------- */
/*  VGA passthrough blit -- public opaque API (called from Qt VCRenderer)      */
/* -------------------------------------------------------------------------- */

void
vc_display_set_vga_bufs(void *ctx_ptr, void *buf0, void *buf1)
{
    vc_ctx_t *ctx = (vc_ctx_t *) ctx_ptr;
    if (!ctx)
        return;

    vc_gpu_state_t *gpu_st = (vc_gpu_state_t *) ctx->render_data;
    if (!gpu_st)
        return;

    atomic_store_explicit(&gpu_st->disp.vga_buf_ptrs[0],
                          (uintptr_t) buf0, memory_order_release);
    atomic_store_explicit(&gpu_st->disp.vga_buf_ptrs[1],
                          (uintptr_t) buf1, memory_order_release);
}

void
vc_display_notify_vga_frame(void *ctx_ptr, int buf_idx,
                             int x, int y, int w, int h)
{
    vc_ctx_t *ctx = (vc_ctx_t *) ctx_ptr;
    if (!ctx)
        return;

    vc_gpu_state_t *gpu_st = (vc_gpu_state_t *) ctx->render_data;
    if (!gpu_st)
        return;

    /* Store the blit rect (relaxed is fine -- vga_frame_ready has release). */
    atomic_store_explicit(&gpu_st->disp.vga_buf_idx, buf_idx,
                          memory_order_relaxed);
    atomic_store_explicit(&gpu_st->disp.vga_blit_x, x,
                          memory_order_relaxed);
    atomic_store_explicit(&gpu_st->disp.vga_blit_y, y,
                          memory_order_relaxed);
    atomic_store_explicit(&gpu_st->disp.vga_blit_w, w,
                          memory_order_relaxed);
    atomic_store_explicit(&gpu_st->disp.vga_blit_h, h,
                          memory_order_relaxed);

    /* Signal that a frame is ready (release to publish the rect). */
    atomic_store_explicit(&gpu_st->disp.vga_frame_ready, 1,
                          memory_order_release);

    /* Wake the GPU thread in case it is sleeping on the ring semaphore. */
    vc_ring_wake(&ctx->ring);
}
