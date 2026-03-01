/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VideoCommon -- public C11 API for GPU-accelerated rendering.
 *
 *          This header is the single integration point between the core
 *          emulator and the VideoCommon Vulkan rendering infrastructure.
 *          All types are opaque; the implementation lives in
 *          src/video/videocommon/.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#ifndef VIDEOCOMMON_H
#define VIDEOCOMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef USE_VIDEOCOMMON

/* Opaque context -- actual definition is in vc_internal.h. */
typedef struct vc_ctx_t vc_ctx_t;

/* Create the Vulkan instance, select a physical device, create a logical
   device and VMA allocator.  Returns NULL on failure (SW fallback). */
vc_ctx_t *vc_init(void);

/* Destroy the context and all Vulkan resources. */
void vc_destroy(vc_ctx_t *ctx);

/* Start the dedicated GPU render thread. Returns 0 on success. */
int vc_start_gpu_thread(vc_ctx_t *ctx);

/* Push a VC_CMD_SHUTDOWN to the ring, join the GPU thread. */
void vc_stop_gpu_thread(vc_ctx_t *ctx);

/* Integration hooks for Voodoo device init/close.
   `voodoo` is a void* to voodoo_t -- avoids including voodoo headers. */
void vc_voodoo_init(void *voodoo);
void vc_voodoo_close(void *voodoo);

#else /* !USE_VIDEOCOMMON */

/* No-op stubs when VideoCommon is not compiled in. */
static inline void vc_voodoo_init(void *voodoo)  { (void) voodoo; }
static inline void vc_voodoo_close(void *voodoo) { (void) voodoo; }

#endif /* USE_VIDEOCOMMON */

#ifdef __cplusplus
}
#endif

#endif /* VIDEOCOMMON_H */
