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
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#ifndef VIDEOCOMMON_TEXTURE_H
#define VIDEOCOMMON_TEXTURE_H

#include "vc_internal.h"

/* Forward declaration -- full definition in vc_gpu_state.h. */
typedef struct vc_gpu_state_t vc_gpu_state_t;

/* -------------------------------------------------------------------------- */
/*  Constants                                                                  */
/* -------------------------------------------------------------------------- */

/* Maximum texture slots per TMU.  Matches TEX_CACHE_MAX in vid_voodoo_common.h. */
#define VC_TEX_SLOTS_PER_TMU 64

/* Maximum number of TMUs. */
#define VC_TEX_MAX_TMU 2

/* Maximum texture dimension. */
#define VC_TEX_MAX_DIM 256

/* Staging buffer size: enough for one 256x256 RGBA8 texture. */
#define VC_TEX_STAGING_SIZE (VC_TEX_MAX_DIM * VC_TEX_MAX_DIM * 4)

/* Maximum descriptor sets per frame (generous limit). */
#define VC_TEX_MAX_DESC_SETS 256

/* -------------------------------------------------------------------------- */
/*  Texture slot                                                               */
/* -------------------------------------------------------------------------- */

typedef struct vc_tex_slot_t {
    VkImage         image;
    VkImageView     view;
    void           *alloc;       /* VmaAllocation (opaque) */
    VkDescriptorSet desc_set;    /* Per-slot descriptor set (pre-allocated). */
    VkSampler       bound_sampler; /* Sampler written into desc_set. */
    uint32_t        width;
    uint32_t        height;
    int             valid;       /* 1 = has been uploaded */
    uint32_t        identity;    /* Quick hash for change detection (GPU side). */
} vc_tex_slot_t;

/* -------------------------------------------------------------------------- */
/*  Sampler cache                                                              */
/* -------------------------------------------------------------------------- */

/* We cache a small number of VkSampler objects keyed on filter + wrap mode. */
#define VC_SAMPLER_CACHE_MAX 16

typedef struct vc_sampler_entry_t {
    uint32_t  key;           /* Packed filter + wrap bits. */
    VkSampler sampler;
} vc_sampler_entry_t;

/* -------------------------------------------------------------------------- */
/*  Texture state (GPU-thread-only)                                            */
/* -------------------------------------------------------------------------- */

typedef struct vc_texture_state_t {
    /* Per-TMU texture slots. */
    vc_tex_slot_t slots[VC_TEX_MAX_TMU][VC_TEX_SLOTS_PER_TMU];

    /* Staging buffer for texture uploads. */
    VkBuffer staging_buf;
    void    *staging_alloc;   /* VmaAllocation */
    void    *staging_mapped;  /* Persistently mapped pointer. */

    /* Dedicated upload resources -- fence-based sync instead of vkQueueWaitIdle. */
    VkCommandPool upload_cmd_pool;
    VkCommandBuffer upload_cmd_buf;
    VkFence upload_fence;
    int     upload_pending;   /* 1 = upload submitted, fence not yet waited on */

    /* Sampler cache. */
    vc_sampler_entry_t sampler_cache[VC_SAMPLER_CACHE_MAX];
    uint32_t           sampler_count;

    /* Descriptor set layout (binding 0 = TMU0 combined image sampler). */
    VkDescriptorSetLayout desc_layout;

    /* Per-frame descriptor pool. */
    VkDescriptorPool desc_pool;

    /* Currently bound descriptor set (for the current batch). */
    VkDescriptorSet current_desc_set;

    /* Currently bound texture slot indices per TMU. */
    int bound_slot[VC_TEX_MAX_TMU]; /* -1 = none */

    /* Currently bound sampler per TMU. */
    VkSampler bound_sampler[VC_TEX_MAX_TMU];

    /* Dummy 1x1 white texture for untextured draws. */
    VkImage     dummy_image;
    VkImageView dummy_view;
    void       *dummy_alloc;  /* VmaAllocation */
    VkSampler   dummy_sampler;
    VkDescriptorSet dummy_desc_set;
} vc_texture_state_t;

/* -------------------------------------------------------------------------- */
/*  Ring command payloads for texture operations                                */
/* -------------------------------------------------------------------------- */

/*
 * VC_CMD_TEXTURE_UPLOAD payload:
 *   [uint32_t tmu]
 *   [uint32_t slot]
 *   [uint32_t width]
 *   [uint32_t height]
 *   [uint32_t identity]   -- quick hash for change detection
 *   [uint8_t* data_ptr]   -- pointer to malloc'd RGBA8 data (GPU thread frees)
 */
typedef struct vc_tex_upload_payload_t {
    uint32_t tmu;
    uint32_t slot;
    uint32_t width;
    uint32_t height;
    uint32_t identity;
    uint32_t pad;
    uint64_t data_ptr; /* Cast to uint8_t* on GPU thread. */
} vc_tex_upload_payload_t;

_Static_assert(sizeof(vc_tex_upload_payload_t) == 32,
               "vc_tex_upload_payload_t must be 32 bytes");

/*
 * VC_CMD_TEXTURE_BIND payload:
 *   [uint32_t tmu]
 *   [uint32_t slot]
 *   [uint32_t sampler_key] -- packed filter/wrap bits
 */
typedef struct vc_tex_bind_payload_t {
    uint32_t tmu;
    uint32_t slot;
    uint32_t sampler_key;
    uint32_t pad;
} vc_tex_bind_payload_t;

_Static_assert(sizeof(vc_tex_bind_payload_t) == 16,
               "vc_tex_bind_payload_t must be 16 bytes");

/* -------------------------------------------------------------------------- */
/*  Public API (GPU thread only)                                               */
/* -------------------------------------------------------------------------- */

/* Create texture subsystem resources (staging buffer, descriptor layout,
   descriptor pool, dummy texture).  Returns 0 on success, -1 on failure. */
int vc_texture_create(vc_ctx_t *ctx, vc_texture_state_t *tex);

/* Destroy all texture resources. */
void vc_texture_destroy(vc_ctx_t *ctx, vc_texture_state_t *tex);

/* Handle a VC_CMD_TEXTURE_UPLOAD command.  Uploads RGBA8 data to a VkImage.
   Takes ownership of the data pointer and frees it after upload. */
void vc_texture_handle_upload(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st,
                              const vc_tex_upload_payload_t *payload);

/* Handle a VC_CMD_TEXTURE_BIND command.  Allocates a descriptor set
   with the bound texture and sets it as current. */
void vc_texture_handle_bind(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st,
                            const vc_tex_bind_payload_t *payload);

/* Bind the current descriptor set into a command buffer.
   Call this before drawing.  Returns 0 if a set was bound, -1 if none. */
int vc_texture_bind_current(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st,
                            VkCommandBuffer cmd_buf);

/* Reset per-frame descriptor pool (call at frame start). */
void vc_texture_reset_frame(vc_ctx_t *ctx, vc_texture_state_t *tex);

/* Build a sampler key from Voodoo textureMode register bits. */
uint32_t vc_texture_sampler_key(uint32_t textureMode);

/* Get or create a VkSampler for a given sampler key. */
VkSampler vc_texture_get_sampler(vc_ctx_t *ctx, vc_texture_state_t *tex,
                                 uint32_t sampler_key);

#endif /* VIDEOCOMMON_TEXTURE_H */
