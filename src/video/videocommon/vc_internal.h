/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VideoCommon internal header -- shared defines and types for
 *          the GPU-accelerated rendering infrastructure.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#ifndef VIDEOCOMMON_INTERNAL_H
#define VIDEOCOMMON_INTERNAL_H

#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>

/* VK_NO_PROTOTYPES and platform defines are set by CMakeLists.txt.
   volk must be included before any other Vulkan headers. */
#include "volk.h"

/* VMA types are only available when vk_mem_alloc.h is included.
   For the internal header we use void* to avoid pulling in the C++ header. */

/* -------------------------------------------------------------------------- */
/*  Logging                                                                    */
/* -------------------------------------------------------------------------- */

#ifdef ENABLE_VIDEOCOMMON_LOG
extern int vc_do_log;
void       vc_log_func(const char *fmt, ...);
#    define VC_LOG(...) vc_log_func(__VA_ARGS__)
#else
#    define VC_LOG(...)
#endif

/* -------------------------------------------------------------------------- */
/*  Constants                                                                  */
/* -------------------------------------------------------------------------- */

/* SPSC ring buffer: 8 MB, power-of-two, 16-byte aligned entries. */
#define VC_RING_SIZE  (8 * 1024 * 1024)
#define VC_RING_MASK  (VC_RING_SIZE - 1)
#define VC_RING_ALIGN 16

/* Triple-buffered frame resources. */
#define VC_NUM_FRAMES 3

/* -------------------------------------------------------------------------- */
/*  Ring command types                                                         */
/* -------------------------------------------------------------------------- */

enum {
    VC_CMD_TRIANGLE      = 0,
    VC_CMD_SWAP          = 1,
    VC_CMD_TEXTURE_UPLOAD = 2,
    VC_CMD_TEXTURE_BIND  = 3,
    VC_CMD_STATE_UPDATE  = 4,
    VC_CMD_CLEAR         = 5,
    VC_CMD_LFB_WRITE     = 6,
    VC_CMD_SHUTDOWN      = 7,
    VC_CMD_WRAPAROUND    = 8,
    VC_CMD_RESIZE        = 9,
    VC_CMD_FOG_UPLOAD    = 10
};

/* -------------------------------------------------------------------------- */
/*  Ring command header                                                        */
/* -------------------------------------------------------------------------- */

typedef struct vc_ring_cmd_header_t {
    uint16_t type;     /* VC_CMD_* */
    uint16_t size;     /* Total size of this command in bytes (including header). */
    uint32_t reserved;
} vc_ring_cmd_header_t;

_Static_assert(sizeof(vc_ring_cmd_header_t) == 8,
               "vc_ring_cmd_header_t must be 8 bytes");

/* -------------------------------------------------------------------------- */
/*  VC_CMD_RESIZE payload                                                      */
/* -------------------------------------------------------------------------- */

typedef struct vc_resize_payload_t {
    uint32_t width;
    uint32_t height;
} vc_resize_payload_t;

_Static_assert(sizeof(vc_resize_payload_t) == 8,
               "vc_resize_payload_t must be 8 bytes");

/* -------------------------------------------------------------------------- */
/*  VC_CMD_CLEAR payload (fastfill)                                            */
/* -------------------------------------------------------------------------- */

typedef struct vc_clear_payload_t {
    float    color[4]; /* RGBA clear color (0.0-1.0)       */
    float    depth;    /* Depth clear value (0.0-1.0)      */
    uint32_t flags;    /* Bit 0: clear color, bit 1: clear depth */
    uint16_t x;        /* Clip rect left                   */
    uint16_t y;        /* Clip rect top (lowY)             */
    uint16_t width;    /* Clip rect width                  */
    uint16_t height;   /* Clip rect height                 */
} vc_clear_payload_t;

_Static_assert(sizeof(vc_clear_payload_t) == 32,
               "vc_clear_payload_t must be 32 bytes");

#define VC_CLEAR_COLOR (1u << 0)
#define VC_CLEAR_DEPTH (1u << 1)

/* -------------------------------------------------------------------------- */
/*  SPSC ring buffer                                                           */
/* -------------------------------------------------------------------------- */

typedef struct vc_ring_t {
    uint8_t          *buffer;
    _Atomic(uint32_t) write_pos;
    _Atomic(uint32_t) read_pos;
    _Atomic(int32_t)  wake_counter;
    void             *wake_sem;   /* Platform counting semaphore (opaque) */
    uint32_t          staged_wp;  /* Next write_pos, not yet published (producer only) */
} vc_ring_t;

/* -------------------------------------------------------------------------- */
/*  Capability flags                                                           */
/* -------------------------------------------------------------------------- */

typedef struct vc_caps_t {
    int has_extended_dynamic_state;
    int has_extended_dynamic_state2;
    int has_extended_dynamic_state3;
    int has_push_descriptor;
    int has_dual_src_blend;
    int has_independent_blend;
} vc_caps_t;

/* -------------------------------------------------------------------------- */
/*  Per-frame GPU resources                                                    */
/* -------------------------------------------------------------------------- */

typedef struct vc_frame_t {
    VkCommandPool   cmd_pool;
    VkCommandBuffer cmd_buf;
    VkFence         fence;
    int             submitted; /* 1 if cmd_buf has been submitted and not yet waited on. */
} vc_frame_t;

/* -------------------------------------------------------------------------- */
/*  Render pass state                                                          */
/* -------------------------------------------------------------------------- */

typedef struct vc_render_pass_state_t {
    VkRenderPass render_pass_clear; /* First frame: LOAD_OP_CLEAR. */
    VkRenderPass render_pass_load;  /* Steady state: LOAD_OP_LOAD. */

    struct {
        VkImage     color_image;
        VkImageView color_view;
        void       *color_alloc; /* VmaAllocation (opaque). */

        VkImage     depth_image;
        VkImageView depth_view;
        void       *depth_alloc; /* VmaAllocation (opaque). */

        VkFramebuffer framebuffer;

        uint32_t width;
        uint32_t height;
        int      first_frame; /* 1 = use clear render pass on next use. */
    } fb[2];
    int back_index; /* 0 or 1 -- which fb is the back buffer. */
} vc_render_pass_state_t;

/* -------------------------------------------------------------------------- */
/*  Batch state (vertex buffer for triangle accumulation)                      */
/* -------------------------------------------------------------------------- */

typedef struct vc_batch_state_t {
    VkBuffer vertex_buffer;
    void    *vertex_alloc;  /* VmaAllocation (opaque). */
    void    *vertex_mapped; /* Persistently mapped pointer. */

    uint32_t triangle_count;
    uint32_t vertex_offset; /* Byte offset of next vertex write. */
} vc_batch_state_t;

/* -------------------------------------------------------------------------- */
/*  Main context                                                               */
/* -------------------------------------------------------------------------- */

/* Full definition -- only visible inside videocommon. */
typedef struct vc_ctx_t {
    /* Vulkan core handles. */
    VkInstance       instance;
    VkPhysicalDevice physical_device;
    VkDevice         device;
    VkQueue          queue;
    uint32_t         queue_family;

    void            *allocator; /* VmaAllocator (opaque, see vk_mem_alloc.h) */

    /* Thread + ring. */
    vc_ring_t        ring;
    void            *gpu_thread; /* thread_t* */
    _Atomic(int)     running;

    vc_caps_t        caps;

    /* Physical device properties (cached for logging / decisions). */
    char             device_name[256];
    uint32_t         api_version;

    /* Phase 2+ render state (GPU thread owns all of these).
       The render_data pointer holds GPU-thread-local resources
       (render pass, pipeline, batch, frame resources).  It is set by
       vc_gpu_thread_init() and freed by vc_gpu_thread_cleanup().
       This keeps vc_internal.h free of sub-module type dependencies. */
    void *render_data;

    /* Framebuffer dimensions (set during GPU thread init, read by FIFO thread
       for push constant fb_width/fb_height).  Atomic not strictly needed
       (only written once at init) but provides visibility guarantee. */
    _Atomic(uint32_t) fb_width;
    _Atomic(uint32_t) fb_height;

    /* Opaque pointer back to voodoo_t for readback hack (GPU->SW FB copy).
       Set during vc_voodoo_init, read by GPU thread. */
    void *voodoo_ptr;

    /* Pointer to voodoo_t::vc_divert_to_gpu.  Set BEFORE starting the GPU
       thread so it is visible via the thread-creation happens-before edge.
       The GPU thread sets *divert_to_gpu_ptr = 1 in vc_display_create()
       and the FIFO thread reads voodoo->vc_divert_to_gpu for routing. */
    volatile int *divert_to_gpu_ptr;

    /* Debug messenger (only created when VC_VALIDATE=1). */
    VkDebugUtilsMessengerEXT debug_messenger;
} vc_ctx_t;

#endif /* VIDEOCOMMON_INTERNAL_H */
