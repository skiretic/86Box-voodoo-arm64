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

/* Notify the Qt UI that VideoCommon has finished background init
   and a VCRenderer can now be created.  Implemented in qt_ui.cpp.
   Safe to call from any thread -- the actual renderer switch happens
   asynchronously on the Qt main thread. */
void vc_notify_renderer_ready(void);

/* -------------------------------------------------------------------------- */
/*  Display integration -- opaque API for Qt VCRenderer                        */
/*                                                                             */
/*  These functions use void* / uintptr_t to avoid exposing Vulkan types       */
/*  in this public header.  The actual Vulkan-typed functions are in           */
/*  vc_display.h (internal).                                                   */
/* -------------------------------------------------------------------------- */

#include <stdint.h>

/* Get the active vc_ctx_t pointer (NULL if not initialised).
   Safe to call from any thread; returns a snapshot. */
void *vc_display_get_ctx(void);

/* Get the VkInstance handle (as uintptr_t).  Returns 0 if not initialised. */
uintptr_t vc_display_get_instance(void);

/* Set a VkSurfaceKHR for the GPU thread.  `ctx` is vc_ctx_t*.
   `surface` is VkSurfaceKHR cast to uintptr_t.  Called from GUI thread. */
void vc_display_set_surface_handle(void *ctx, uintptr_t surface);

/* Signal the GPU thread to recreate swapchain.  Called from GUI thread.
   `ctx` is vc_ctx_t*. */
void vc_display_signal_resize_handle(void *ctx, uint32_t width, uint32_t height);

/* Request teardown of swapchain resources.  Called from GUI thread. */
void vc_display_request_teardown_handle(void *ctx);

/* Wait for GPU thread to complete teardown.  Blocks. */
void vc_display_wait_teardown_handle(void *ctx);

/* Create a VkSurfaceKHR from a native window handle.
   `ctx` is vc_ctx_t*.  `native_handle` is the platform window handle
   (HWND on Windows, NSView* on macOS, xcb_window_t on Linux).
   Returns VkSurfaceKHR as uintptr_t, or 0 on failure. */
uintptr_t vc_create_surface(void *ctx, uintptr_t native_handle);

/* Destroy a VkSurfaceKHR previously created by vc_create_surface().
   `surface` is VkSurfaceKHR as uintptr_t. */
void vc_destroy_surface(void *ctx, uintptr_t surface);

/* -------------------------------------------------------------------------- */
/*  VGA passthrough blit -- called from Qt VCRenderer                          */
/* -------------------------------------------------------------------------- */

/* Register VGA image buffer pointers for the GPU thread to read from.
   Called once after VCRenderer allocates its image buffers.
   `buf0` and `buf1` are pointers to the raw BGRA8 pixel data.
   Returns 0 on success, -1 if the context is not ready yet. */
int vc_display_set_vga_bufs(void *ctx, void *buf0, void *buf1);

/* Notify the GPU thread that a VGA frame is ready for presentation.
   `buf_idx` is which buffer to read (0 or 1).
   `x`, `y`, `w`, `h` are the blit rect in pixels. */
void vc_display_notify_vga_frame(void *ctx, int buf_idx,
                                  int x, int y, int w, int h);

#else /* !USE_VIDEOCOMMON */

/* No-op stubs when VideoCommon is not compiled in. */
static inline void vc_voodoo_init(void *voodoo)  { (void) voodoo; }
static inline void vc_voodoo_close(void *voodoo) { (void) voodoo; }
static inline void vc_notify_renderer_ready(void) { }
static inline void *vc_display_get_ctx(void) { return (void *) 0; }
static inline uintptr_t vc_display_get_instance(void) { return 0; }
static inline void vc_display_set_surface_handle(void *c, uintptr_t s) { (void) c; (void) s; }
static inline void vc_display_signal_resize_handle(void *c, uint32_t w, uint32_t h) { (void) c; (void) w; (void) h; }
static inline void vc_display_request_teardown_handle(void *c) { (void) c; }
static inline void vc_display_wait_teardown_handle(void *c) { (void) c; }
static inline uintptr_t vc_create_surface(void *c, uintptr_t h) { (void) c; (void) h; return 0; }
static inline void vc_destroy_surface(void *c, uintptr_t s) { (void) c; (void) s; }
static inline int vc_display_set_vga_bufs(void *c, void *b0, void *b1) { (void) c; (void) b0; (void) b1; return -1; }
static inline void vc_display_notify_vga_frame(void *c, int bi, int x, int y, int w, int h) { (void) c; (void) bi; (void) x; (void) y; (void) w; (void) h; }

#endif /* USE_VIDEOCOMMON */

#ifdef __cplusplus
}
#endif

#endif /* VIDEOCOMMON_H */
