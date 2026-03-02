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
 *          The GPU thread is the sole owner of all display Vulkan
 *          objects.  The Qt GUI thread creates a VkSurfaceKHR and
 *          passes it atomically; the GPU thread creates and manages
 *          the swapchain from that surface.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#ifndef VIDEOCOMMON_DISPLAY_H
#define VIDEOCOMMON_DISPLAY_H

#include "vc_internal.h"

/* Forward declarations. */
typedef struct vc_gpu_state_t vc_gpu_state_t;

/* Maximum number of swapchain images we handle. */
#define VC_MAX_SWAPCHAIN_IMAGES 8
#define VC_VGA_TIMEOUT_FRAMES   60  /* ~1 second at 60Hz: re-enable VGA passthrough */

/* -------------------------------------------------------------------------- */
/*  Display state                                                              */
/* -------------------------------------------------------------------------- */

typedef struct vc_display_t {
    /* --- Atomic communication (GUI thread <-> GPU thread) --- */
    _Atomic(uintptr_t) surface_pending;    /* GUI -> GPU: new VkSurfaceKHR */
    _Atomic(uint32_t)  resize_width;       /* GUI -> GPU: new width */
    _Atomic(uint32_t)  resize_height;      /* GUI -> GPU: new height */
    _Atomic(int)       resize_requested;   /* GUI -> GPU: flag */
    _Atomic(int)       teardown_requested; /* GUI -> GPU: flag */
    _Atomic(int)       teardown_complete;  /* GPU -> GUI: flag */

    /* --- GPU-thread-only state (NOT atomic) --- */

    /* Surface and swapchain. */
    VkSurfaceKHR   surface;
    VkSwapchainKHR swapchain;
    VkImage        images[VC_MAX_SWAPCHAIN_IMAGES];
    VkImageView    image_views[VC_MAX_SWAPCHAIN_IMAGES];
    uint32_t       image_count;
    VkFormat       format;
    VkExtent2D     extent;

    /* Post-process pipeline. */
    VkRenderPass          pp_render_pass;
    VkPipeline            pp_pipeline;
    VkPipelineLayout      pp_pipeline_layout;
    VkDescriptorSetLayout pp_desc_layout;
    VkDescriptorPool      pp_desc_pool;
    VkDescriptorSet       pp_desc_sets[2]; /* One per offscreen FB (front/back). */
    VkSampler             pp_sampler;
    VkFramebuffer         pp_framebuffers[VC_MAX_SWAPCHAIN_IMAGES];

    /* Post-process shader modules. */
    VkShaderModule pp_vert_shader;
    VkShaderModule pp_frag_shader;

    /* Per-frame sync primitives. */
    VkSemaphore image_available_sem[VC_NUM_FRAMES];
    VkSemaphore render_finished_sem[VC_NUM_FRAMES];

    /* Pointer to voodoo_t::vc_display_active for the GPU thread to set.
       Copied from ctx->display_active_ptr during vc_gpu_thread_init(). */
    int *display_active_ptr;

    /* Timeout counter: incremented by vc_display_tick() each VGA frame
       while Voodoo display is active, reset to 0 by vc_gpu_handle_swap()
       on every real present.  When this exceeds VC_VGA_TIMEOUT_FRAMES,
       VGA passthrough is re-enabled (handles Glide app exit when no more
       swap commands arrive). */
    _Atomic(int) vga_frames_since_present;

    /* --- VGA passthrough blit state --- */

    /* Atomic communication (Qt main thread -> GPU thread). */
    _Atomic(int) vga_frame_ready; /* 1 = new VGA frame data available */
    _Atomic(int) vga_buf_idx;     /* which image buffer to read from */
    _Atomic(int) vga_blit_x;
    _Atomic(int) vga_blit_y;
    _Atomic(int) vga_blit_w;
    _Atomic(int) vga_blit_h;

    /* Pointer to the image buffer data (set once during init, stable).
       Two pointers for double-buffered image data (same as SoftwareRenderer).
       These point to the raw pixel data (BGRA8, 2048*2048*4 bytes each). */
    _Atomic(uintptr_t) vga_buf_ptrs[2];

    /* GPU-thread-only VGA resources. */
    VkImage         vga_image;
    VkImageView     vga_image_view;
    void           *vga_image_alloc; /* VmaAllocation */
    VkBuffer        vga_staging_buf;
    void           *vga_staging_alloc; /* VmaAllocation */
    void           *vga_staging_mapped;
    VkDescriptorSet vga_desc_set;
    uint32_t        vga_tex_width;
    uint32_t        vga_tex_height;
    int             vga_resources_created;
    VkCommandPool   vga_cmd_pool;
    VkCommandBuffer vga_cmd_buf[VC_NUM_FRAMES];
} vc_display_t;

/* -------------------------------------------------------------------------- */
/*  Public API -- called from Qt/GUI thread                                    */
/* -------------------------------------------------------------------------- */

/* Set a new VkSurfaceKHR for the GPU thread to pick up.
   Called from the GUI thread after platform surface creation. */
void vc_display_set_surface(vc_ctx_t *ctx, VkSurfaceKHR surface);

/* Signal the GPU thread that the window has been resized.
   width/height are in physical pixels (HiDPI-adjusted). */
void vc_display_signal_resize(vc_ctx_t *ctx, uint32_t width, uint32_t height);

/* Request teardown of swapchain resources.  Called from GUI thread. */
void vc_display_request_teardown(vc_ctx_t *ctx);

/* Wait for the GPU thread to complete teardown.  Blocks. */
void vc_display_wait_teardown(vc_ctx_t *ctx);

/* -------------------------------------------------------------------------- */
/*  GPU-thread-only API                                                        */
/* -------------------------------------------------------------------------- */

/* Initialise the display state struct (zero everything). */
void vc_display_state_init(vc_display_t *disp);

/* Called each GPU thread iteration to check for surface/resize/teardown. */
void vc_display_tick(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st);

/* Create swapchain and all post-process resources.
   Returns 0 on success, -1 on failure. */
int vc_display_create(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st);

/* Destroy swapchain and all post-process resources. */
void vc_display_destroy(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st);

/* Recreate swapchain (e.g., after resize or OUT_OF_DATE). */
int vc_display_recreate_swapchain(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st);

/* Post-process blit and present.  Called from vc_gpu_handle_swap()
   after the offscreen render pass has ended.
   Returns 0 on success, 1 if swapchain needs recreation, -1 on error. */
int vc_display_present(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st,
                       VkCommandBuffer cmd_buf, uint32_t frame_index);

/* Update descriptor sets to point to the current offscreen FB.
   Called after offscreen framebuffer (re)creation. */
void vc_display_update_descriptors(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st);

/* VGA passthrough blit: check for pending VGA frame and present it.
   Called from the GPU thread main loop when no 3D render pass is active.
   Returns 0 on success, -1 on error/no-op. */
int vc_display_present_vga(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st);

/* Create VGA blit Vulkan resources (VkImage, staging buffer, descriptor set).
   Called from the GPU thread after the display is created. */
int vc_display_create_vga_resources(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st);

/* Destroy VGA blit Vulkan resources. */
void vc_display_destroy_vga_resources(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st);

/* -------------------------------------------------------------------------- */
/*  macOS surface helper (vc_macos_surface.m)                                  */
/* -------------------------------------------------------------------------- */

#ifdef __APPLE__
/* Returns a CAMetalLayer* (as void*) from an NSView*.
   Creates the layer if one does not already exist. */
void *vc_get_metal_layer(void *ns_view_ptr);
#endif

#endif /* VIDEOCOMMON_DISPLAY_H */
