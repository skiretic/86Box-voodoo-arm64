/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VideoCommon texture module -- Vulkan texture management,
 *          staging upload, sampler cache, descriptor set allocation.
 *
 *          Phase 4: TMU0 texture upload and sampling.
 *
 *          Textures are decoded to RGBA8 on the CPU (by the existing
 *          voodoo_use_texture() path), then uploaded as VkImage objects
 *          via a staging buffer.  The GPU thread owns all Vulkan texture
 *          resources.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vc_internal.h"

#define HAVE_STDARG_H
#include <86box/86box.h>

#include "vc_texture.h"
#include "vc_gpu_state.h"
#include "vc_pipeline.h"
#include "vc_batch.h"

/* C-callable VMA wrappers (implemented in vc_vma_impl.cpp). */
extern VkResult vc_vma_create_buffer(void *allocator,
                                     const VkBufferCreateInfo *buffer_ci,
                                     int mapped, VkBuffer *out_buffer,
                                     void **out_alloc, void **out_mapped);
extern void     vc_vma_destroy_buffer(void *allocator, VkBuffer buffer,
                                      void *alloc);
extern VkResult vc_vma_create_image(void *allocator,
                                    const VkImageCreateInfo *image_ci,
                                    VkImage *out_image, void **out_alloc);
extern void     vc_vma_destroy_image(void *allocator, VkImage image,
                                     void *alloc);

/* -------------------------------------------------------------------------- */
/*  Internal: create a VkImage + VkImageView for a texture slot                */
/* -------------------------------------------------------------------------- */

static int
vc_tex_create_slot_image(vc_ctx_t *ctx, vc_tex_slot_t *slot,
                         uint32_t width, uint32_t height)
{
    /* If an existing image has wrong dimensions, destroy it. */
    if (slot->image != VK_NULL_HANDLE
        && (slot->width != width || slot->height != height))
    {
        if (slot->view != VK_NULL_HANDLE) {
            vkDestroyImageView(ctx->device, slot->view, NULL);
            slot->view = VK_NULL_HANDLE;
        }
        vc_vma_destroy_image(ctx->allocator, slot->image, slot->alloc);
        slot->image = VK_NULL_HANDLE;
        slot->alloc = NULL;
        slot->valid = 0;
    }

    /* Create image if needed. */
    if (slot->image == VK_NULL_HANDLE) {
        VkImageCreateInfo img_ci;
        memset(&img_ci, 0, sizeof(img_ci));
        img_ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img_ci.imageType     = VK_IMAGE_TYPE_2D;
        img_ci.format        = VK_FORMAT_R8G8B8A8_UNORM;
        img_ci.extent.width  = width;
        img_ci.extent.height = height;
        img_ci.extent.depth  = 1;
        img_ci.mipLevels     = 1;
        img_ci.arrayLayers   = 1;
        img_ci.samples       = VK_SAMPLE_COUNT_1_BIT;
        img_ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        img_ci.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT
                             | VK_IMAGE_USAGE_SAMPLED_BIT;
        img_ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        img_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkResult result = vc_vma_create_image(ctx->allocator, &img_ci,
                                              &slot->image, &slot->alloc);
        if (result != VK_SUCCESS) {
            VC_LOG("VideoCommon: tex slot image creation failed (%d)\n", result);
            return -1;
        }

        slot->width  = width;
        slot->height = height;
    }

    /* Create image view if needed. */
    if (slot->view == VK_NULL_HANDLE) {
        VkImageViewCreateInfo view_ci;
        memset(&view_ci, 0, sizeof(view_ci));
        view_ci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_ci.image    = slot->image;
        view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_ci.format   = VK_FORMAT_R8G8B8A8_UNORM;
        view_ci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        view_ci.subresourceRange.baseMipLevel   = 0;
        view_ci.subresourceRange.levelCount     = 1;
        view_ci.subresourceRange.baseArrayLayer = 0;
        view_ci.subresourceRange.layerCount     = 1;

        VkResult result = vkCreateImageView(ctx->device, &view_ci, NULL,
                                            &slot->view);
        if (result != VK_SUCCESS) {
            VC_LOG("VideoCommon: tex slot image view creation failed (%d)\n",
                   result);
            return -1;
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------- */
/*  Internal: transition image layout using a one-shot command buffer           */
/* -------------------------------------------------------------------------- */

static void
vc_tex_transition_layout(VkCommandBuffer cmd_buf, VkImage image,
                         VkImageLayout old_layout, VkImageLayout new_layout)
{
    VkImageMemoryBarrier barrier;
    memset(&barrier, 0, sizeof(barrier));
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = old_layout;
    barrier.newLayout           = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;

    VkPipelineStageFlags src_stage;
    VkPipelineStageFlags dst_stage;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED
        && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
               && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(cmd_buf, src_stage, dst_stage, 0,
                         0, NULL, 0, NULL, 1, &barrier);
}

/* -------------------------------------------------------------------------- */
/*  Internal: create dummy 1x1 white texture                                   */
/* -------------------------------------------------------------------------- */

static int
vc_tex_create_dummy(vc_ctx_t *ctx, vc_texture_state_t *tex)
{
    /* Create 1x1 image. */
    VkImageCreateInfo img_ci;
    memset(&img_ci, 0, sizeof(img_ci));
    img_ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_ci.imageType     = VK_IMAGE_TYPE_2D;
    img_ci.format        = VK_FORMAT_R8G8B8A8_UNORM;
    img_ci.extent.width  = 1;
    img_ci.extent.height = 1;
    img_ci.extent.depth  = 1;
    img_ci.mipLevels     = 1;
    img_ci.arrayLayers   = 1;
    img_ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    img_ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    img_ci.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT
                         | VK_IMAGE_USAGE_SAMPLED_BIT;
    img_ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    img_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult result = vc_vma_create_image(ctx->allocator, &img_ci,
                                          &tex->dummy_image, &tex->dummy_alloc);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: dummy texture creation failed (%d)\n", result);
        return -1;
    }

    /* Image view. */
    VkImageViewCreateInfo view_ci;
    memset(&view_ci, 0, sizeof(view_ci));
    view_ci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_ci.image    = tex->dummy_image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format   = VK_FORMAT_R8G8B8A8_UNORM;
    view_ci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view_ci.subresourceRange.baseMipLevel   = 0;
    view_ci.subresourceRange.levelCount     = 1;
    view_ci.subresourceRange.baseArrayLayer = 0;
    view_ci.subresourceRange.layerCount     = 1;

    result = vkCreateImageView(ctx->device, &view_ci, NULL, &tex->dummy_view);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: dummy texture view creation failed (%d)\n", result);
        return -1;
    }

    /* Nearest sampler. */
    VkSamplerCreateInfo sampler_ci;
    memset(&sampler_ci, 0, sizeof(sampler_ci));
    sampler_ci.sType     = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_ci.magFilter = VK_FILTER_NEAREST;
    sampler_ci.minFilter = VK_FILTER_NEAREST;
    sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    result = vkCreateSampler(ctx->device, &sampler_ci, NULL, &tex->dummy_sampler);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: dummy sampler creation failed (%d)\n", result);
        return -1;
    }

    /* Upload 1x1 white pixel via staging buffer. */
    uint32_t white = 0xFFFFFFFF;
    memcpy(tex->staging_mapped, &white, 4);

    /* One-shot command buffer for the upload. */
    VkCommandPoolCreateInfo pool_ci;
    memset(&pool_ci, 0, sizeof(pool_ci));
    pool_ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_ci.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_ci.queueFamilyIndex = ctx->queue_family;

    VkCommandPool tmp_pool;
    result = vkCreateCommandPool(ctx->device, &pool_ci, NULL, &tmp_pool);
    if (result != VK_SUCCESS)
        return -1;

    VkCommandBufferAllocateInfo alloc_ci;
    memset(&alloc_ci, 0, sizeof(alloc_ci));
    alloc_ci.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_ci.commandPool        = tmp_pool;
    alloc_ci.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_ci.commandBufferCount = 1;

    VkCommandBuffer tmp_cmd;
    result = vkAllocateCommandBuffers(ctx->device, &alloc_ci, &tmp_cmd);
    if (result != VK_SUCCESS) {
        vkDestroyCommandPool(ctx->device, tmp_pool, NULL);
        return -1;
    }

    VkCommandBufferBeginInfo begin_ci;
    memset(&begin_ci, 0, sizeof(begin_ci));
    begin_ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_ci.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(tmp_cmd, &begin_ci);

    vc_tex_transition_layout(tmp_cmd, tex->dummy_image,
                             VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region;
    memset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount     = 1;
    region.imageExtent.width               = 1;
    region.imageExtent.height              = 1;
    region.imageExtent.depth               = 1;

    vkCmdCopyBufferToImage(tmp_cmd, tex->staging_buf, tex->dummy_image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    vc_tex_transition_layout(tmp_cmd, tex->dummy_image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkEndCommandBuffer(tmp_cmd);

    VkSubmitInfo submit;
    memset(&submit, 0, sizeof(submit));
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &tmp_cmd;

    vkQueueSubmit(ctx->queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->queue);

    vkDestroyCommandPool(ctx->device, tmp_pool, NULL);

    VC_LOG("VideoCommon: dummy 1x1 white texture created\n");
    return 0;
}

/* -------------------------------------------------------------------------- */
/*  Descriptor set layout and pool                                             */
/* -------------------------------------------------------------------------- */

static int
vc_tex_create_descriptors(vc_ctx_t *ctx, vc_texture_state_t *tex)
{
    /* Descriptor set layout:
       binding 0 = TMU0 combined image sampler
       binding 1 = TMU1 combined image sampler
       binding 2 = fog table (64x1 R8G8_UNORM combined image sampler) */
    VkDescriptorSetLayoutBinding bindings[3];
    memset(bindings, 0, sizeof(bindings));

    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[2].binding         = 2;
    bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_ci;
    memset(&layout_ci, 0, sizeof(layout_ci));
    layout_ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_ci.bindingCount = 3;
    layout_ci.pBindings    = bindings;

    VkResult result = vkCreateDescriptorSetLayout(ctx->device, &layout_ci, NULL,
                                                  &tex->desc_layout);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: tex descriptor set layout creation failed (%d)\n",
               result);
        return -1;
    }

    /* Descriptor pool: 3 samplers per set (TMU0 + TMU1 + fog table). */
    VkDescriptorPoolSize pool_size;
    pool_size.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = VC_TEX_MAX_DESC_SETS * 3;

    VkDescriptorPoolCreateInfo pool_ci;
    memset(&pool_ci, 0, sizeof(pool_ci));
    pool_ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_ci.maxSets       = VC_TEX_MAX_DESC_SETS;
    pool_ci.poolSizeCount = 1;
    pool_ci.pPoolSizes    = &pool_size;

    result = vkCreateDescriptorPool(ctx->device, &pool_ci, NULL,
                                    &tex->desc_pool);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: tex descriptor pool creation failed (%d)\n",
               result);
        return -1;
    }

    return 0;
}

/* -------------------------------------------------------------------------- */
/*  Public: create texture subsystem                                           */
/* -------------------------------------------------------------------------- */

int
vc_texture_create(vc_ctx_t *ctx, vc_texture_state_t *tex)
{
    memset(tex, 0, sizeof(vc_texture_state_t));
    tex->bound_slot[0] = -1;
    tex->bound_slot[1] = -1;

    /* Staging buffer for texture uploads. */
    VkBufferCreateInfo buf_ci;
    memset(&buf_ci, 0, sizeof(buf_ci));
    buf_ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_ci.size        = VC_TEX_STAGING_SIZE;
    buf_ci.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vc_vma_create_buffer(
        ctx->allocator, &buf_ci, 1 /* mapped */,
        &tex->staging_buf, &tex->staging_alloc, &tex->staging_mapped);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: tex staging buffer creation failed (%d)\n", result);
        return -1;
    }

    /* Dedicated command pool + fence for texture uploads.
     * Using a fence instead of vkQueueWaitIdle allows the GPU to continue
     * rendering while the staging buffer is in use.  We only wait on the
     * fence at the START of the next upload (to reclaim the staging buffer). */
    {
        VkCommandPoolCreateInfo pool_ci;
        memset(&pool_ci, 0, sizeof(pool_ci));
        pool_ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_ci.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
                                 | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_ci.queueFamilyIndex = ctx->queue_family;

        result = vkCreateCommandPool(ctx->device, &pool_ci, NULL,
                                     &tex->upload_cmd_pool);
        if (result != VK_SUCCESS) {
            VC_LOG("VideoCommon: tex upload cmd pool failed (%d)\n", result);
            return -1;
        }

        VkCommandBufferAllocateInfo alloc_ci;
        memset(&alloc_ci, 0, sizeof(alloc_ci));
        alloc_ci.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_ci.commandPool        = tex->upload_cmd_pool;
        alloc_ci.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_ci.commandBufferCount = 1;

        result = vkAllocateCommandBuffers(ctx->device, &alloc_ci,
                                          &tex->upload_cmd_buf);
        if (result != VK_SUCCESS) {
            VC_LOG("VideoCommon: tex upload cmd buf alloc failed (%d)\n", result);
            return -1;
        }

        VkFenceCreateInfo fence_ci;
        memset(&fence_ci, 0, sizeof(fence_ci));
        fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        /* Not signaled initially -- upload_pending starts at 0. */

        result = vkCreateFence(ctx->device, &fence_ci, NULL,
                               &tex->upload_fence);
        if (result != VK_SUCCESS) {
            VC_LOG("VideoCommon: tex upload fence failed (%d)\n", result);
            return -1;
        }

        tex->upload_pending = 0;
    }

    /* Descriptor layout and pool. */
    if (vc_tex_create_descriptors(ctx, tex) != 0)
        return -1;

    /* Dummy texture. */
    if (vc_tex_create_dummy(ctx, tex) != 0)
        return -1;

    /* Pre-allocate persistent descriptor sets: 1 dummy + 128 per-slot. */
    {
        const uint32_t total_sets = 1 + VC_TEX_MAX_TMU * VC_TEX_SLOTS_PER_TMU;
        VkDescriptorSetLayout layouts[1 + VC_TEX_MAX_TMU * VC_TEX_SLOTS_PER_TMU];
        VkDescriptorSet       sets[1 + VC_TEX_MAX_TMU * VC_TEX_SLOTS_PER_TMU];

        for (uint32_t i = 0; i < total_sets; i++)
            layouts[i] = tex->desc_layout;

        VkDescriptorSetAllocateInfo set_ai;
        memset(&set_ai, 0, sizeof(set_ai));
        set_ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        set_ai.descriptorPool     = tex->desc_pool;
        set_ai.descriptorSetCount = total_sets;
        set_ai.pSetLayouts        = layouts;

        result = vkAllocateDescriptorSets(ctx->device, &set_ai, sets);
        if (result != VK_SUCCESS) {
            VC_LOG("VideoCommon: descriptor set bulk allocation failed (%d)\n",
                   result);
            return -1;
        }

        /* First set is the dummy. */
        tex->dummy_desc_set = sets[0];

        /* Remaining sets go to per-slot desc_set fields. */
        uint32_t idx = 1;
        for (int tmu = 0; tmu < VC_TEX_MAX_TMU; tmu++) {
            for (int s = 0; s < VC_TEX_SLOTS_PER_TMU; s++) {
                tex->slots[tmu][s].desc_set       = sets[idx++];
                tex->slots[tmu][s].bound_sampler   = VK_NULL_HANDLE;
            }
        }
    }

    /* Create fog table image (64x1 R8G8_UNORM).
       Initial content is all zeros (no fog).  Uploaded via VC_CMD_FOG_UPLOAD. */
    {
        VkImageCreateInfo fog_ci;
        memset(&fog_ci, 0, sizeof(fog_ci));
        fog_ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        fog_ci.imageType     = VK_IMAGE_TYPE_2D;
        fog_ci.format        = VK_FORMAT_R8G8_UNORM;
        fog_ci.extent.width  = 64;
        fog_ci.extent.height = 1;
        fog_ci.extent.depth  = 1;
        fog_ci.mipLevels     = 1;
        fog_ci.arrayLayers   = 1;
        fog_ci.samples       = VK_SAMPLE_COUNT_1_BIT;
        fog_ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        fog_ci.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT
                             | VK_IMAGE_USAGE_SAMPLED_BIT;
        fog_ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        fog_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        result = vc_vma_create_image(ctx->allocator, &fog_ci,
                                     &tex->fog_image, &tex->fog_alloc);
        if (result != VK_SUCCESS) {
            VC_LOG("VideoCommon: fog table image creation failed (%d)\n", result);
            return -1;
        }

        VkImageViewCreateInfo fog_view_ci;
        memset(&fog_view_ci, 0, sizeof(fog_view_ci));
        fog_view_ci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        fog_view_ci.image                           = tex->fog_image;
        fog_view_ci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        fog_view_ci.format                          = VK_FORMAT_R8G8_UNORM;
        fog_view_ci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        fog_view_ci.subresourceRange.levelCount     = 1;
        fog_view_ci.subresourceRange.layerCount     = 1;

        result = vkCreateImageView(ctx->device, &fog_view_ci, NULL,
                                   &tex->fog_view);
        if (result != VK_SUCCESS) {
            VC_LOG("VideoCommon: fog table image view creation failed (%d)\n", result);
            return -1;
        }

        /* Fog table sampler: nearest filtering, clamp-to-edge. */
        VkSamplerCreateInfo fog_samp_ci;
        memset(&fog_samp_ci, 0, sizeof(fog_samp_ci));
        fog_samp_ci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        fog_samp_ci.magFilter    = VK_FILTER_NEAREST;
        fog_samp_ci.minFilter    = VK_FILTER_NEAREST;
        fog_samp_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        fog_samp_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        fog_samp_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

        result = vkCreateSampler(ctx->device, &fog_samp_ci, NULL,
                                 &tex->fog_sampler);
        if (result != VK_SUCCESS) {
            VC_LOG("VideoCommon: fog sampler creation failed (%d)\n", result);
            return -1;
        }

        /* Transition fog image to SHADER_READ_ONLY (will be overwritten by upload). */
        {
            VkCommandBuffer cb = tex->upload_cmd_buf;
            vkResetCommandBuffer(cb, 0);

            VkCommandBufferBeginInfo begin_ci;
            memset(&begin_ci, 0, sizeof(begin_ci));
            begin_ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_ci.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cb, &begin_ci);

            VkImageMemoryBarrier barrier;
            memset(&barrier, 0, sizeof(barrier));
            barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image               = tex->fog_image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask       = 0;
            barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(cb,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, NULL, 0, NULL, 1, &barrier);

            vkEndCommandBuffer(cb);

            VkSubmitInfo submit;
            memset(&submit, 0, sizeof(submit));
            submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers    = &cb;
            vkQueueSubmit(ctx->queue, 1, &submit, tex->upload_fence);
            vkWaitForFences(ctx->device, 1, &tex->upload_fence, VK_TRUE, UINT64_MAX);
            vkResetFences(ctx->device, 1, &tex->upload_fence);
        }

        tex->fog_valid    = 0;
        tex->fog_checksum = 0;
    }

    /* Write dummy descriptors (binding 0 + binding 1) and fog (binding 2)
       into ALL pre-allocated descriptor sets.  This ensures TMU0, TMU1, and
       fog table are always bound regardless of which set is active. */
    {
        const uint32_t total_sets = 1 + VC_TEX_MAX_TMU * VC_TEX_SLOTS_PER_TMU;
        VkDescriptorSet all_sets[1 + VC_TEX_MAX_TMU * VC_TEX_SLOTS_PER_TMU];
        all_sets[0] = tex->dummy_desc_set;
        uint32_t idx = 1;
        for (int tmu = 0; tmu < VC_TEX_MAX_TMU; tmu++)
            for (int s = 0; s < VC_TEX_SLOTS_PER_TMU; s++)
                all_sets[idx++] = tex->slots[tmu][s].desc_set;

        VkDescriptorImageInfo dummy_info;
        memset(&dummy_info, 0, sizeof(dummy_info));
        dummy_info.sampler     = tex->dummy_sampler;
        dummy_info.imageView   = tex->dummy_view;
        dummy_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        for (uint32_t i = 0; i < total_sets; i++) {
            /* Binding 0: TMU0 dummy. */
            VkWriteDescriptorSet writes[2];
            memset(writes, 0, sizeof(writes));

            writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet          = all_sets[i];
            writes[0].dstBinding      = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].pImageInfo      = &dummy_info;

            /* Binding 1: TMU1 dummy. */
            writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet          = all_sets[i];
            writes[1].dstBinding      = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].pImageInfo      = &dummy_info;

            vkUpdateDescriptorSets(ctx->device, 2, writes, 0, NULL);

            /* Binding 2: fog table. */
            vc_texture_write_fog_descriptor(ctx, tex, all_sets[i]);
        }
    }

    tex->current_desc_set = tex->dummy_desc_set;

    VC_LOG("VideoCommon: texture subsystem created\n");
    return 0;
}

/* -------------------------------------------------------------------------- */
/*  Public: destroy texture subsystem                                          */
/* -------------------------------------------------------------------------- */

void
vc_texture_destroy(vc_ctx_t *ctx, vc_texture_state_t *tex)
{
    if (!tex)
        return;

    /* Destroy all texture slots. */
    for (int tmu = 0; tmu < VC_TEX_MAX_TMU; tmu++) {
        for (int i = 0; i < VC_TEX_SLOTS_PER_TMU; i++) {
            vc_tex_slot_t *slot = &tex->slots[tmu][i];
            if (slot->view != VK_NULL_HANDLE) {
                vkDestroyImageView(ctx->device, slot->view, NULL);
                slot->view = VK_NULL_HANDLE;
            }
            if (slot->image != VK_NULL_HANDLE) {
                vc_vma_destroy_image(ctx->allocator, slot->image, slot->alloc);
                slot->image = VK_NULL_HANDLE;
                slot->alloc = NULL;
            }
        }
    }

    /* Dummy texture. */
    if (tex->dummy_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(ctx->device, tex->dummy_sampler, NULL);
        tex->dummy_sampler = VK_NULL_HANDLE;
    }
    if (tex->dummy_view != VK_NULL_HANDLE) {
        vkDestroyImageView(ctx->device, tex->dummy_view, NULL);
        tex->dummy_view = VK_NULL_HANDLE;
    }
    if (tex->dummy_image != VK_NULL_HANDLE) {
        vc_vma_destroy_image(ctx->allocator, tex->dummy_image, tex->dummy_alloc);
        tex->dummy_image = VK_NULL_HANDLE;
        tex->dummy_alloc = NULL;
    }

    /* Fog table. */
    if (tex->fog_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(ctx->device, tex->fog_sampler, NULL);
        tex->fog_sampler = VK_NULL_HANDLE;
    }
    if (tex->fog_view != VK_NULL_HANDLE) {
        vkDestroyImageView(ctx->device, tex->fog_view, NULL);
        tex->fog_view = VK_NULL_HANDLE;
    }
    if (tex->fog_image != VK_NULL_HANDLE) {
        vc_vma_destroy_image(ctx->allocator, tex->fog_image, tex->fog_alloc);
        tex->fog_image = VK_NULL_HANDLE;
        tex->fog_alloc = NULL;
    }

    /* Sampler cache. */
    for (uint32_t i = 0; i < tex->sampler_count; i++) {
        if (tex->sampler_cache[i].sampler != VK_NULL_HANDLE) {
            vkDestroySampler(ctx->device, tex->sampler_cache[i].sampler, NULL);
            tex->sampler_cache[i].sampler = VK_NULL_HANDLE;
        }
    }
    tex->sampler_count = 0;

    /* Descriptor pool (destroys all allocated sets). */
    if (tex->desc_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(ctx->device, tex->desc_pool, NULL);
        tex->desc_pool = VK_NULL_HANDLE;
    }

    /* Descriptor layout. */
    if (tex->desc_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(ctx->device, tex->desc_layout, NULL);
        tex->desc_layout = VK_NULL_HANDLE;
    }

    /* Upload fence + command pool. */
    if (tex->upload_fence != VK_NULL_HANDLE) {
        if (tex->upload_pending)
            vkWaitForFences(ctx->device, 1, &tex->upload_fence,
                            VK_TRUE, UINT64_MAX);
        vkDestroyFence(ctx->device, tex->upload_fence, NULL);
        tex->upload_fence   = VK_NULL_HANDLE;
        tex->upload_pending = 0;
    }
    if (tex->upload_cmd_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(ctx->device, tex->upload_cmd_pool, NULL);
        tex->upload_cmd_pool = VK_NULL_HANDLE;
        tex->upload_cmd_buf  = VK_NULL_HANDLE;
    }

    /* Staging buffer. */
    if (tex->staging_buf != VK_NULL_HANDLE) {
        vc_vma_destroy_buffer(ctx->allocator, tex->staging_buf,
                              tex->staging_alloc);
        tex->staging_buf    = VK_NULL_HANDLE;
        tex->staging_alloc  = NULL;
        tex->staging_mapped = NULL;
    }
}

/* -------------------------------------------------------------------------- */
/*  Public: handle texture upload                                              */
/* -------------------------------------------------------------------------- */

void
vc_texture_handle_upload(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st,
                         const vc_tex_upload_payload_t *payload)
{
    vc_texture_state_t *tex = &gpu_st->tex;
    uint32_t tmu  = payload->tmu;
    uint32_t slot = payload->slot;
    uint32_t w    = payload->width;
    uint32_t h    = payload->height;
    uint8_t *data = (uint8_t *) (uintptr_t) payload->data_ptr;

    if (tmu >= VC_TEX_MAX_TMU || slot >= VC_TEX_SLOTS_PER_TMU) {
        VC_LOG("VideoCommon: tex upload invalid tmu=%u slot=%u\n", tmu, slot);
        free(data);
        return;
    }

    if (w == 0 || h == 0 || w > VC_TEX_MAX_DIM || h > VC_TEX_MAX_DIM) {
        VC_LOG("VideoCommon: tex upload invalid size %ux%u\n", w, h);
        free(data);
        return;
    }

    vc_tex_slot_t *s = &tex->slots[tmu][slot];

    /* Create/resize image if needed. */
    if (vc_tex_create_slot_image(ctx, s, w, h) != 0) {
        free(data);
        return;
    }

    /* Wait for the PREVIOUS upload to finish before reusing the staging
     * buffer.  This is the key optimization: instead of vkQueueWaitIdle
     * (which drains the entire pipeline), we only wait on our upload
     * fence, allowing rendering to overlap with the staging buffer fill. */
    if (tex->upload_pending) {
        vkWaitForFences(ctx->device, 1, &tex->upload_fence,
                        VK_TRUE, UINT64_MAX);
        vkResetFences(ctx->device, 1, &tex->upload_fence);
        tex->upload_pending = 0;
    }

    /* Copy pixel data to staging buffer (safe now -- previous copy finished). */
    uint32_t byte_size = w * h * 4;
    memcpy(tex->staging_mapped, data, byte_size);
    free(data);

    /* If render pass is active, end the frame first so the queue is clear
       for our transfer.  The next triangle will re-start the render pass.
       We must also wait for this submission to complete, because the
       descriptor sets referenced by the submitted command buffer must not
       be updated (by a subsequent BIND command) while in flight. */
    if (gpu_st->render_pass_active) {
        vc_frame_t *f = &gpu_st->frame[gpu_st->frame_index];

        vc_batch_reset(ctx, gpu_st);

        vkCmdEndRenderPass(f->cmd_buf);
        vkEndCommandBuffer(f->cmd_buf);

        VkSubmitInfo submit;
        memset(&submit, 0, sizeof(submit));
        submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &f->cmd_buf;

        vkQueueSubmit(ctx->queue, 1, &submit, f->fence);
        vkWaitForFences(ctx->device, 1, &f->fence, VK_TRUE, UINT64_MAX);
        vkResetFences(ctx->device, 1, &f->fence);
        f->submitted               = 0;
        gpu_st->render_pass_active = 0;

        /* Advance frame index so the next begin_frame uses fresh resources. */
        gpu_st->frame_index = (gpu_st->frame_index + 1) % VC_NUM_FRAMES;
    }

    /* Record transfer commands into the persistent upload command buffer. */
    vkResetCommandBuffer(tex->upload_cmd_buf, 0);

    VkCommandBufferBeginInfo begin_ci;
    memset(&begin_ci, 0, sizeof(begin_ci));
    begin_ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_ci.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(tex->upload_cmd_buf, &begin_ci);

    vc_tex_transition_layout(tex->upload_cmd_buf, s->image,
                             s->valid ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                      : VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region;
    memset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width           = w;
    region.imageExtent.height          = h;
    region.imageExtent.depth           = 1;

    vkCmdCopyBufferToImage(tex->upload_cmd_buf, tex->staging_buf, s->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &region);

    vc_tex_transition_layout(tex->upload_cmd_buf, s->image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkEndCommandBuffer(tex->upload_cmd_buf);

    /* Submit with our upload fence -- no vkQueueWaitIdle needed. */
    VkSubmitInfo submit2;
    memset(&submit2, 0, sizeof(submit2));
    submit2.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit2.commandBufferCount = 1;
    submit2.pCommandBuffers    = &tex->upload_cmd_buf;

    VkResult submit_res = vkQueueSubmit(ctx->queue, 1, &submit2, tex->upload_fence);
    if (submit_res == VK_SUCCESS) {
        tex->upload_pending = 1;
    } else {
        fprintf(stderr, "VideoCommon: vkQueueSubmit failed for texture upload (result=%d), skipping fence wait\n", submit_res);
        vkResetFences(ctx->device, 1, &tex->upload_fence);
    }

    s->valid    = 1;
    s->identity = payload->identity;

    /* Invalidate the bound sampler so the next bind re-writes the
       descriptor with the (potentially new) image view. */
    s->bound_sampler = VK_NULL_HANDLE;

    VC_LOG("VideoCommon: tex upload tmu=%u slot=%u %ux%u\n", tmu, slot, w, h);
}

/* -------------------------------------------------------------------------- */
/*  Public: handle texture bind                                                */
/* -------------------------------------------------------------------------- */

void
vc_texture_handle_bind(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st,
                       const vc_tex_bind_payload_t *payload)
{
    vc_texture_state_t *tex = &gpu_st->tex;
    uint32_t tmu  = payload->tmu;
    uint32_t slot = payload->slot;

    if (tmu >= VC_TEX_MAX_TMU || slot >= VC_TEX_SLOTS_PER_TMU)
        return;

    vc_tex_slot_t *s = &tex->slots[tmu][slot];
    if (!s->valid || s->view == VK_NULL_HANDLE) {
        /* Texture not uploaded yet -- bind dummy. */
        VC_LOG("VideoCommon: tex bind slot not ready tmu=%u slot=%u\n",
               tmu, slot);
        if (tmu == 0)
            tex->current_desc_set = tex->dummy_desc_set;
        return;
    }

    /* Get or create sampler. */
    VkSampler sampler = vc_texture_get_sampler(ctx, tex, payload->sampler_key);
    if (sampler == VK_NULL_HANDLE) {
        if (tmu == 0)
            tex->current_desc_set = tex->dummy_desc_set;
        return;
    }

    /* Track bound state for this TMU. */
    tex->bound_slot[tmu]    = (int) slot;
    tex->bound_sampler[tmu] = sampler;

    /*
     * Descriptor set update strategy for dual-TMU:
     *
     * Each TMU slot has a pre-allocated descriptor set.  We use TMU0's
     * slot descriptor set as the "primary" set, writing:
     *   binding 0 = TMU0 texture
     *   binding 1 = TMU1 texture (if bound, else dummy)
     *   binding 2 = fog table (always pre-written)
     *
     * For single-TMU via TMU1 (backward compat): the bridge copies TMU1
     * coords into vTexCoord0 and pushes the bind as tmu=0, so the texture
     * ends up at binding 0 automatically.
     *
     * For TMU1 binds in dual-TMU mode: we write TMU1 to binding 1 of the
     * current TMU0 descriptor set (if TMU0 is currently bound).
     *
     * SAFETY NOTE: same reasoning as before -- descriptor set updates are
     * safe because the GPU thread is single-threaded and upload waits on
     * the frame fence.
     */
    if (tmu == 0) {
        /* TMU0 bind: write to binding 0 of TMU0's slot descriptor set. */
        VkDescriptorImageInfo img_info;
        memset(&img_info, 0, sizeof(img_info));
        img_info.sampler     = sampler;
        img_info.imageView   = s->view;
        img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet writes[2];
        uint32_t write_count = 0;

        memset(&writes[0], 0, sizeof(VkWriteDescriptorSet));
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = s->desc_set;
        writes[0].dstBinding      = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo      = &img_info;
        write_count = 1;

        /* Also write TMU1 to binding 1 if TMU1 is currently bound. */
        VkDescriptorImageInfo tmu1_info;
        if (tex->bound_slot[1] >= 0) {
            vc_tex_slot_t *s1 = &tex->slots[1][tex->bound_slot[1]];
            if (s1->valid && s1->view != VK_NULL_HANDLE
                && tex->bound_sampler[1] != VK_NULL_HANDLE) {
                memset(&tmu1_info, 0, sizeof(tmu1_info));
                tmu1_info.sampler     = tex->bound_sampler[1];
                tmu1_info.imageView   = s1->view;
                tmu1_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                memset(&writes[1], 0, sizeof(VkWriteDescriptorSet));
                writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[1].dstSet          = s->desc_set;
                writes[1].dstBinding      = 1;
                writes[1].descriptorCount = 1;
                writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[1].pImageInfo      = &tmu1_info;
                write_count = 2;
            }
        }

        vkUpdateDescriptorSets(ctx->device, write_count, writes, 0, NULL);
        s->bound_sampler = sampler;
        tex->current_desc_set = s->desc_set;
    } else {
        /* TMU1 bind: if TMU0 is currently bound, write TMU1 to binding 1
           of TMU0's current descriptor set (dual-TMU mode).
           If TMU0 is NOT bound, this is single-TMU-via-TMU1: write to
           binding 0 of TMU1's own descriptor set (the vertex extraction
           already copied TMU1 coords into vTexCoord0). */
        if (tex->bound_slot[0] >= 0) {
            /* Dual-TMU: write TMU1 to binding 1 of TMU0's descriptor set. */
            vc_tex_slot_t *s0 = &tex->slots[0][tex->bound_slot[0]];
            if (s0->desc_set != VK_NULL_HANDLE) {
                VkDescriptorImageInfo img_info;
                memset(&img_info, 0, sizeof(img_info));
                img_info.sampler     = sampler;
                img_info.imageView   = s->view;
                img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                VkWriteDescriptorSet write;
                memset(&write, 0, sizeof(write));
                write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet          = s0->desc_set;
                write.dstBinding      = 1;
                write.descriptorCount = 1;
                write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.pImageInfo      = &img_info;

                vkUpdateDescriptorSets(ctx->device, 1, &write, 0, NULL);
                tex->current_desc_set = s0->desc_set;
            }
        } else {
            /* Single-TMU via TMU1: write to binding 1 of TMU1's slot
               descriptor set.  The shader samples tex_tmu1 via vTexCoord1
               when only TMU1 is enabled. */
            VkDescriptorImageInfo img_info;
            memset(&img_info, 0, sizeof(img_info));
            img_info.sampler     = sampler;
            img_info.imageView   = s->view;
            img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet write;
            memset(&write, 0, sizeof(write));
            write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet          = s->desc_set;
            write.dstBinding      = 1;
            write.descriptorCount = 1;
            write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo      = &img_info;

            vkUpdateDescriptorSets(ctx->device, 1, &write, 0, NULL);
            tex->current_desc_set = s->desc_set;
        }
    }
}

/* -------------------------------------------------------------------------- */
/*  Public: bind current descriptor set                                        */
/* -------------------------------------------------------------------------- */

int
vc_texture_bind_current(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st,
                        VkCommandBuffer cmd_buf)
{
    (void) ctx;

    if (gpu_st->tex.current_desc_set == VK_NULL_HANDLE)
        return -1;

    vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            gpu_st->pipe.layout, 0, 1,
                            &gpu_st->tex.current_desc_set, 0, NULL);
    return 0;
}

/* -------------------------------------------------------------------------- */
/*  Public: reset per-frame descriptor pool                                    */
/* -------------------------------------------------------------------------- */

void
vc_texture_reset_frame(vc_ctx_t *ctx, vc_texture_state_t *tex)
{
    /* Descriptor sets are persistent per-slot -- no pool reset needed.
       Just reset current binding to dummy so the next draw either binds
       a real texture or falls back to the 1x1 white dummy.
       Also reset bound_slot so dual-TMU / single-TMU transitions between
       frames don't use stale descriptor set references. */
    (void) ctx;
    tex->current_desc_set = tex->dummy_desc_set;
    tex->bound_slot[0]    = -1;
    tex->bound_slot[1]    = -1;
    tex->bound_sampler[0] = VK_NULL_HANDLE;
    tex->bound_sampler[1] = VK_NULL_HANDLE;
}

/* -------------------------------------------------------------------------- */
/*  Public: sampler key from Voodoo textureMode                                */
/* -------------------------------------------------------------------------- */

uint32_t
vc_texture_sampler_key(uint32_t textureMode)
{
    /*
     * Voodoo textureMode bits:
     *   [1]   = minification filter (0=nearest, 1=bilinear)
     *   [2]   = magnification filter (0=nearest, 1=bilinear)
     *   [3:4] = clamp S (0=wrap, 1=clamp)
     *   [5:6] = clamp T (0=wrap, 1=clamp)
     *
     * We pack: [0]=mag_filter, [1]=min_filter, [2]=clamp_s, [3]=clamp_t
     */
    uint32_t min_filt = (textureMode >> 1) & 1;
    uint32_t mag_filt = (textureMode >> 2) & 1;
    uint32_t clamp_s  = (textureMode >> 3) & 1;
    uint32_t clamp_t  = (textureMode >> 5) & 1;

    return mag_filt | (min_filt << 1) | (clamp_s << 2) | (clamp_t << 3);
}

/* -------------------------------------------------------------------------- */
/*  Public: get or create sampler                                              */
/* -------------------------------------------------------------------------- */

VkSampler
vc_texture_get_sampler(vc_ctx_t *ctx, vc_texture_state_t *tex,
                       uint32_t sampler_key)
{
    /* Search cache. */
    for (uint32_t i = 0; i < tex->sampler_count; i++) {
        if (tex->sampler_cache[i].key == sampler_key)
            return tex->sampler_cache[i].sampler;
    }

    /* Create new sampler. */
    VkFilter mag_filter = (sampler_key & 1) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    VkFilter min_filter = (sampler_key & 2) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    VkSamplerAddressMode addr_s = (sampler_key & 4)
        ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
        : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addr_t = (sampler_key & 8)
        ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
        : VK_SAMPLER_ADDRESS_MODE_REPEAT;

    VkSamplerCreateInfo sampler_ci;
    memset(&sampler_ci, 0, sizeof(sampler_ci));
    sampler_ci.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_ci.magFilter     = mag_filter;
    sampler_ci.minFilter     = min_filter;
    sampler_ci.addressModeU  = addr_s;
    sampler_ci.addressModeV  = addr_t;
    sampler_ci.addressModeW  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_ci.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    VkSampler sampler;
    VkResult result = vkCreateSampler(ctx->device, &sampler_ci, NULL, &sampler);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: sampler creation failed (%d)\n", result);
        return VK_NULL_HANDLE;
    }

    if (tex->sampler_count >= VC_SAMPLER_CACHE_MAX) {
        /* Cache full -- evict oldest entry (index 0). */
        vkDestroySampler(ctx->device, tex->sampler_cache[0].sampler, NULL);
        memmove(&tex->sampler_cache[0], &tex->sampler_cache[1],
                (VC_SAMPLER_CACHE_MAX - 1) * sizeof(tex->sampler_cache[0]));
        tex->sampler_count = VC_SAMPLER_CACHE_MAX - 1;
    }
    tex->sampler_cache[tex->sampler_count].key     = sampler_key;
    tex->sampler_cache[tex->sampler_count].sampler = sampler;
    tex->sampler_count++;

    VC_LOG("VideoCommon: sampler created key=0x%x (mag=%s min=%s s=%s t=%s)\n",
           sampler_key,
           (sampler_key & 1) ? "linear" : "nearest",
           (sampler_key & 2) ? "linear" : "nearest",
           (sampler_key & 4) ? "clamp" : "wrap",
           (sampler_key & 8) ? "clamp" : "wrap");
    return sampler;
}

/* -------------------------------------------------------------------------- */
/*  Public: write fog table descriptor (binding 2) into a descriptor set       */
/* -------------------------------------------------------------------------- */

void
vc_texture_write_fog_descriptor(vc_ctx_t *ctx, vc_texture_state_t *tex,
                                VkDescriptorSet desc_set)
{
    if (tex->fog_view == VK_NULL_HANDLE || tex->fog_sampler == VK_NULL_HANDLE)
        return;

    VkDescriptorImageInfo fog_info;
    memset(&fog_info, 0, sizeof(fog_info));
    fog_info.sampler     = tex->fog_sampler;
    fog_info.imageView   = tex->fog_view;
    fog_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write;
    memset(&write, 0, sizeof(write));
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = desc_set;
    write.dstBinding      = 2;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &fog_info;

    vkUpdateDescriptorSets(ctx->device, 1, &write, 0, NULL);
}

/* -------------------------------------------------------------------------- */
/*  Public: handle fog table upload                                            */
/* -------------------------------------------------------------------------- */

void
vc_texture_handle_fog_upload(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st,
                             const vc_fog_upload_payload_t *payload)
{
    vc_texture_state_t *tex = &gpu_st->tex;

    /* XOR checksum for deduplication (fog table rarely changes). */
    uint32_t checksum = 0;
    for (int i = 0; i < 128; i += 4) {
        uint32_t word;
        memcpy(&word, &payload->data[i], 4);
        checksum ^= word;
    }
    if (tex->fog_valid && checksum == tex->fog_checksum)
        return;  /* No change. */

    /* Wait for any pending upload to complete. */
    if (tex->upload_pending) {
        vkWaitForFences(ctx->device, 1, &tex->upload_fence,
                        VK_TRUE, UINT64_MAX);
        vkResetFences(ctx->device, 1, &tex->upload_fence);
        tex->upload_pending = 0;
    }

    /* Copy fog table data (128 bytes) into staging buffer. */
    memcpy(tex->staging_mapped, payload->data, 128);

    /* Record upload commands. */
    vkResetCommandBuffer(tex->upload_cmd_buf, 0);

    VkCommandBufferBeginInfo begin_ci;
    memset(&begin_ci, 0, sizeof(begin_ci));
    begin_ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_ci.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(tex->upload_cmd_buf, &begin_ci);

    /* Transition fog image: SHADER_READ_ONLY -> TRANSFER_DST. */
    VkImageMemoryBarrier barrier;
    memset(&barrier, 0, sizeof(barrier));
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = tex->fog_valid
                                    ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                    : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = tex->fog_image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(tex->upload_cmd_buf,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);

    /* Copy staging -> fog image. */
    VkBufferImageCopy region;
    memset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width           = 64;
    region.imageExtent.height          = 1;
    region.imageExtent.depth           = 1;

    vkCmdCopyBufferToImage(tex->upload_cmd_buf, tex->staging_buf,
                           tex->fog_image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &region);

    /* Transition fog image: TRANSFER_DST -> SHADER_READ_ONLY. */
    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(tex->upload_cmd_buf,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);

    vkEndCommandBuffer(tex->upload_cmd_buf);

    /* Submit. */
    VkSubmitInfo submit;
    memset(&submit, 0, sizeof(submit));
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &tex->upload_cmd_buf;

    VkResult res = vkQueueSubmit(ctx->queue, 1, &submit, tex->upload_fence);
    if (res == VK_SUCCESS) {
        tex->upload_pending = 1;
    } else {
        fprintf(stderr, "VideoCommon: fog upload submit failed (%d)\n", res);
        vkResetFences(ctx->device, 1, &tex->upload_fence);
    }

    tex->fog_valid    = 1;
    tex->fog_checksum = checksum;

    VC_LOG("VideoCommon: fog table uploaded (checksum=0x%08x)\n", checksum);
}
