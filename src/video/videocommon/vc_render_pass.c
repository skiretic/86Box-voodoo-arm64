/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VideoCommon render pass -- offscreen framebuffer management.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "vc_internal.h"

#define HAVE_STDARG_H
#include <86box/86box.h>

#include "vc_gpu_state.h"
#include "vc_render_pass.h"

/* C-callable VMA wrappers (implemented in vc_vma_impl.cpp). */
extern VkResult vc_vma_create_image(void *allocator,
                                    const VkImageCreateInfo *image_ci,
                                    VkImage *out_image, void **out_alloc);
extern void     vc_vma_destroy_image(void *allocator, VkImage image,
                                     void *alloc);

/* -------------------------------------------------------------------------- */
/*  Render pass creation                                                       */
/* -------------------------------------------------------------------------- */

static VkRenderPass
vc_create_render_pass_variant(VkDevice device, VkAttachmentLoadOp load_op,
                              VkImageLayout color_initial,
                              VkImageLayout depth_initial)
{
    VkAttachmentDescription attachments[2];
    memset(attachments, 0, sizeof(attachments));

    attachments[0].format         = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp         = load_op;
    attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout  = color_initial;
    attachments[0].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    attachments[1].format         = VK_FORMAT_D32_SFLOAT;
    attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp         = load_op;
    attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout  = depth_initial;
    attachments[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref;
    memset(&color_ref, 0, sizeof(color_ref));
    color_ref.attachment = 0;
    color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_ref;
    memset(&depth_ref, 0, sizeof(depth_ref));
    depth_ref.attachment = 1;
    depth_ref.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass;
    memset(&subpass, 0, sizeof(subpass));
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency dependency;
    memset(&dependency, 0, sizeof(dependency));
    dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                             | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                             | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                             | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp_ci;
    memset(&rp_ci, 0, sizeof(rp_ci));
    rp_ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_ci.attachmentCount = 2;
    rp_ci.pAttachments    = attachments;
    rp_ci.subpassCount    = 1;
    rp_ci.pSubpasses      = &subpass;
    rp_ci.dependencyCount = 1;
    rp_ci.pDependencies   = &dependency;

    VkRenderPass rp = VK_NULL_HANDLE;
    VkResult     result = vkCreateRenderPass(device, &rp_ci, NULL, &rp);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: vkCreateRenderPass failed (%d)\n", result);
        return VK_NULL_HANDLE;
    }

    return rp;
}

int
vc_render_pass_create(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{
    gpu_st->rp.render_pass_clear = vc_create_render_pass_variant(
        ctx->device,
        VK_ATTACHMENT_LOAD_OP_CLEAR,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_UNDEFINED);
    if (gpu_st->rp.render_pass_clear == VK_NULL_HANDLE)
        return -1;

    gpu_st->rp.render_pass_load = vc_create_render_pass_variant(
        ctx->device,
        VK_ATTACHMENT_LOAD_OP_LOAD,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    if (gpu_st->rp.render_pass_load == VK_NULL_HANDLE) {
        vkDestroyRenderPass(ctx->device, gpu_st->rp.render_pass_clear, NULL);
        gpu_st->rp.render_pass_clear = VK_NULL_HANDLE;
        return -1;
    }

    gpu_st->rp.back_index = 0;
    VC_LOG("VideoCommon: render passes created (clear + load)\n");
    return 0;
}

void
vc_render_pass_destroy(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{
    if (gpu_st->rp.render_pass_load != VK_NULL_HANDLE) {
        vkDestroyRenderPass(ctx->device, gpu_st->rp.render_pass_load, NULL);
        gpu_st->rp.render_pass_load = VK_NULL_HANDLE;
    }
    if (gpu_st->rp.render_pass_clear != VK_NULL_HANDLE) {
        vkDestroyRenderPass(ctx->device, gpu_st->rp.render_pass_clear, NULL);
        gpu_st->rp.render_pass_clear = VK_NULL_HANDLE;
    }
}

/* -------------------------------------------------------------------------- */
/*  Framebuffer creation / destruction                                         */
/* -------------------------------------------------------------------------- */

static void
vc_destroy_single_fb(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st, int idx)
{
    if (gpu_st->rp.fb[idx].framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(ctx->device, gpu_st->rp.fb[idx].framebuffer, NULL);
        gpu_st->rp.fb[idx].framebuffer = VK_NULL_HANDLE;
    }
    if (gpu_st->rp.fb[idx].depth_view != VK_NULL_HANDLE) {
        vkDestroyImageView(ctx->device, gpu_st->rp.fb[idx].depth_view, NULL);
        gpu_st->rp.fb[idx].depth_view = VK_NULL_HANDLE;
    }
    if (gpu_st->rp.fb[idx].color_view != VK_NULL_HANDLE) {
        vkDestroyImageView(ctx->device, gpu_st->rp.fb[idx].color_view, NULL);
        gpu_st->rp.fb[idx].color_view = VK_NULL_HANDLE;
    }
    if (gpu_st->rp.fb[idx].depth_image != VK_NULL_HANDLE) {
        vc_vma_destroy_image(ctx->allocator, gpu_st->rp.fb[idx].depth_image,
                             gpu_st->rp.fb[idx].depth_alloc);
        gpu_st->rp.fb[idx].depth_image = VK_NULL_HANDLE;
        gpu_st->rp.fb[idx].depth_alloc = NULL;
    }
    if (gpu_st->rp.fb[idx].color_image != VK_NULL_HANDLE) {
        vc_vma_destroy_image(ctx->allocator, gpu_st->rp.fb[idx].color_image,
                             gpu_st->rp.fb[idx].color_alloc);
        gpu_st->rp.fb[idx].color_image = VK_NULL_HANDLE;
        gpu_st->rp.fb[idx].color_alloc = NULL;
    }
    gpu_st->rp.fb[idx].width  = 0;
    gpu_st->rp.fb[idx].height = 0;
}

static int
vc_create_single_fb(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st, int idx,
                    uint32_t width, uint32_t height)
{
    VkResult result;

    memset(&gpu_st->rp.fb[idx], 0, sizeof(gpu_st->rp.fb[idx]));
    gpu_st->rp.fb[idx].width       = width;
    gpu_st->rp.fb[idx].height      = height;
    gpu_st->rp.fb[idx].first_frame = 1;

    /* Color image. */
    VkImageCreateInfo color_ci;
    memset(&color_ci, 0, sizeof(color_ci));
    color_ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    color_ci.imageType     = VK_IMAGE_TYPE_2D;
    color_ci.format        = VK_FORMAT_R8G8B8A8_UNORM;
    color_ci.extent.width  = width;
    color_ci.extent.height = height;
    color_ci.extent.depth  = 1;
    color_ci.mipLevels     = 1;
    color_ci.arrayLayers   = 1;
    color_ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    color_ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    color_ci.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                           | VK_IMAGE_USAGE_SAMPLED_BIT
                           | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    color_ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    color_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    result = vc_vma_create_image(ctx->allocator, &color_ci,
                                 &gpu_st->rp.fb[idx].color_image,
                                 &gpu_st->rp.fb[idx].color_alloc);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: color image creation failed (%d)\n", result);
        return -1;
    }

    /* Depth image. */
    VkImageCreateInfo depth_ci;
    memset(&depth_ci, 0, sizeof(depth_ci));
    depth_ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depth_ci.imageType     = VK_IMAGE_TYPE_2D;
    depth_ci.format        = VK_FORMAT_D32_SFLOAT;
    depth_ci.extent.width  = width;
    depth_ci.extent.height = height;
    depth_ci.extent.depth  = 1;
    depth_ci.mipLevels     = 1;
    depth_ci.arrayLayers   = 1;
    depth_ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    depth_ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    depth_ci.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depth_ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    depth_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    result = vc_vma_create_image(ctx->allocator, &depth_ci,
                                 &gpu_st->rp.fb[idx].depth_image,
                                 &gpu_st->rp.fb[idx].depth_alloc);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: depth image creation failed (%d)\n", result);
        vc_destroy_single_fb(ctx, gpu_st, idx);
        return -1;
    }

    /* Color image view. */
    VkImageViewCreateInfo color_view_ci;
    memset(&color_view_ci, 0, sizeof(color_view_ci));
    color_view_ci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    color_view_ci.image                           = gpu_st->rp.fb[idx].color_image;
    color_view_ci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    color_view_ci.format                          = VK_FORMAT_R8G8B8A8_UNORM;
    color_view_ci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    color_view_ci.subresourceRange.levelCount     = 1;
    color_view_ci.subresourceRange.layerCount     = 1;

    result = vkCreateImageView(ctx->device, &color_view_ci, NULL,
                               &gpu_st->rp.fb[idx].color_view);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: color image view failed (%d)\n", result);
        vc_destroy_single_fb(ctx, gpu_st, idx);
        return -1;
    }

    /* Depth image view. */
    VkImageViewCreateInfo depth_view_ci;
    memset(&depth_view_ci, 0, sizeof(depth_view_ci));
    depth_view_ci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depth_view_ci.image                           = gpu_st->rp.fb[idx].depth_image;
    depth_view_ci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    depth_view_ci.format                          = VK_FORMAT_D32_SFLOAT;
    depth_view_ci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    depth_view_ci.subresourceRange.levelCount     = 1;
    depth_view_ci.subresourceRange.layerCount     = 1;

    result = vkCreateImageView(ctx->device, &depth_view_ci, NULL,
                               &gpu_st->rp.fb[idx].depth_view);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: depth image view failed (%d)\n", result);
        vc_destroy_single_fb(ctx, gpu_st, idx);
        return -1;
    }

    /* VkFramebuffer. */
    if (gpu_st->rp.render_pass_load == VK_NULL_HANDLE) {
        VC_LOG("VideoCommon: render_pass_load is NULL in create_single_fb, skipping\n");
        vc_destroy_single_fb(ctx, gpu_st, idx);
        return -1;
    }

    VkImageView fb_attachments[2] = { gpu_st->rp.fb[idx].color_view,
                                      gpu_st->rp.fb[idx].depth_view };

    VkFramebufferCreateInfo fb_ci;
    memset(&fb_ci, 0, sizeof(fb_ci));
    fb_ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_ci.renderPass      = gpu_st->rp.render_pass_load;
    fb_ci.attachmentCount = 2;
    fb_ci.pAttachments    = fb_attachments;
    fb_ci.width           = width;
    fb_ci.height          = height;
    fb_ci.layers          = 1;

    result = vkCreateFramebuffer(ctx->device, &fb_ci, NULL,
                                 &gpu_st->rp.fb[idx].framebuffer);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: VkFramebuffer creation failed (%d)\n", result);
        vc_destroy_single_fb(ctx, gpu_st, idx);
        return -1;
    }

    return 0;
}

int
vc_render_pass_create_framebuffers(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st,
                                   uint32_t width, uint32_t height)
{
    vc_render_pass_destroy_framebuffers(ctx, gpu_st);

    for (int i = 0; i < 2; i++) {
        if (vc_create_single_fb(ctx, gpu_st, i, width, height) != 0) {
            vc_render_pass_destroy_framebuffers(ctx, gpu_st);
            return -1;
        }
    }

    gpu_st->rp.back_index = 0;
    VC_LOG("VideoCommon: framebuffers created (%ux%u, double-buffered)\n",
           width, height);
    return 0;
}

void
vc_render_pass_destroy_framebuffers(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{
    for (int i = 0; i < 2; i++)
        vc_destroy_single_fb(ctx, gpu_st, i);
}

void
vc_render_pass_swap(vc_gpu_state_t *gpu_st)
{
    gpu_st->rp.back_index = 1 - gpu_st->rp.back_index;
}
