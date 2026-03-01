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
    VC_CMD_WRAPAROUND    = 8
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
/*  SPSC ring buffer                                                           */
/* -------------------------------------------------------------------------- */

typedef struct vc_ring_t {
    uint8_t          *buffer;
    _Atomic(uint32_t) write_pos;
    _Atomic(uint32_t) read_pos;
    _Atomic(int32_t)  wake_counter;
    void             *wake_sem;   /* Platform counting semaphore (opaque) */
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
} vc_caps_t;

/* -------------------------------------------------------------------------- */
/*  Main context                                                               */
/* -------------------------------------------------------------------------- */

/* Forward declaration -- full definition only visible inside videocommon. */
typedef struct vc_ctx_t {
    VkInstance       instance;
    VkPhysicalDevice physical_device;
    VkDevice         device;
    VkQueue          queue;
    uint32_t         queue_family;

    void            *allocator; /* VmaAllocator (opaque, see vk_mem_alloc.h) */

    vc_ring_t        ring;
    void            *gpu_thread; /* thread_t* */
    _Atomic(int)     running;

    vc_caps_t        caps;

    /* Physical device properties (cached for logging / decisions). */
    char             device_name[256];
    uint32_t         api_version;
} vc_ctx_t;

#endif /* VIDEOCOMMON_INTERNAL_H */
