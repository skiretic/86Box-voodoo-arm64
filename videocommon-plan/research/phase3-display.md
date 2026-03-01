# Phase 3 Research: Display -- Swapchain, Surface, Post-Process, Qt Integration

Comprehensive research report covering all Phase 3 topics for VideoCommon v2.
Cross-references DESIGN.md, LESSONS.md, Phase 2 code, Vulkan 1.2 spec,
MoltenVK documentation, and emulator reference implementations (Dolphin, DuckStation).
Phase 3 goal: make rendered frames visible in the 86Box window.

Research date: 2026-03-01
Updated: 2026-03-01 (post Glide detection bug fix)

---

## Table of Contents

1. [VkSurfaceKHR Creation from Qt5 QWidget](#1-vksurfacekhr-creation-from-qt5-qwidget)
2. [Swapchain Management](#2-swapchain-management)
3. [Post-Process Blit](#3-post-process-blit)
4. [Display Callback Integration](#4-display-callback-integration)
5. [VCRenderer Class Design](#5-vcrenderer-class-design)
6. [Cross-Platform Surface Creation](#6-cross-platform-surface-creation)
7. [Threading Considerations](#7-threading-considerations)
8. [Implementation Plan](#8-implementation-plan)
9. [Risks and Mitigations](#9-risks-and-mitigations)
10. [Appendix: Emulator Survey](#10-appendix-emulator-survey)

---

## 1. VkSurfaceKHR Creation from Qt5 QWidget

### 1.1 The Problem

VideoCommon has a VkInstance and VkDevice (created in `vc_core.c`). The existing
86Box display path blits into `monitor->target_buffer`, then the Qt RendererStack
copies that buffer into its renderer widget via `svga_doblit()` -> `blit()`.

Phase 3 needs to bypass this entire path and have the GPU thread present
directly to a window surface. This requires:
1. A VkSurfaceKHR created from the Qt widget's native window handle
2. The GPU thread to own a VkSwapchainKHR on that surface
3. The GPU thread to present via vkQueuePresentKHR

### 1.2 Qt5 Vulkan Surface Options

**Option A: QVulkanWindow (REJECTED)**

Qt5's `QVulkanWindow` (used by 86Box's existing Vulkan renderer in
`qt_vulkanwindowrenderer.cpp`) creates its OWN VkInstance and VkDevice.
This is incompatible with VideoCommon which has its own VkInstance/VkDevice
already. QVulkanWindow cannot be given an external VkDevice.

Per the Qt 5.15 documentation, `QVulkanInstance::setVkInstance()` can wrap an
external VkInstance, but QVulkanWindow still creates its own VkDevice
internally. This makes it impossible to share the VkDevice between
QVulkanWindow and our GPU thread.

**Option B: Platform-specific VkSurfaceKHR from winId() (ADOPTED)**

Create a plain QWidget, get its native window handle via `winId()`, then
create a VkSurfaceKHR using platform-specific Vulkan WSI extensions. This is
the approach used by Dolphin, DuckStation, and PCSX2 for their Vulkan backends.

The VkSurfaceKHR is created on the Qt/GUI thread, then the handle is passed
atomically to the GPU thread which creates the swapchain.

### 1.3 Current Instance Extensions (Already Enabled)

The `vc_core.c` already enables the required instance extensions:

```c
/* From vc_core.c vc_init() */
const char *inst_extensions[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,          /* All platforms */
#ifdef __APPLE__
    VK_EXT_METAL_SURFACE_EXTENSION_NAME,    /* macOS */
    "VK_KHR_portability_enumeration",
    "VK_KHR_get_physical_device_properties2",
#endif
#ifdef _WIN32
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,    /* Windows */
#endif
#if defined(__linux__) && !defined(__ANDROID__)
    VK_KHR_XCB_SURFACE_EXTENSION_NAME,      /* Linux X11 */
#endif
};
```

And `VK_KHR_SWAPCHAIN_EXTENSION_NAME` is already enabled as a device extension.

**Gap: Linux Wayland support**

The current code only enables `VK_KHR_xcb_surface` on Linux. Wayland support
requires `VK_KHR_wayland_surface`. This is a known limitation -- 86Box's Qt
build currently targets X11 (xcb) on Linux. Wayland support can be added later
by detecting the active display server at runtime.

### 1.4 Present Queue Family Validation

**CRITICAL**: The graphics queue family selected by `vc_find_graphics_queue()`
may not support presentation to a given surface. The Vulkan spec does NOT
guarantee this.

However, in practice:
- **macOS (MoltenVK)**: The graphics queue always supports presentation (Metal
  handles this transparently).
- **Windows**: NVIDIA, AMD, and Intel drivers always support presentation on
  the graphics queue family.
- **Linux (Mesa, NVIDIA proprietary)**: The graphics queue family almost always
  supports presentation. The exception would be exotic configurations.
- **Pi 5 (V3DV)**: Single queue family, supports both graphics and present.

**Requirement**: When the surface is available, the GPU thread must call
`vkGetPhysicalDeviceSurfaceSupportKHR()` to verify the graphics queue supports
presentation. If it does not, the swapchain cannot be created and display falls
back to the software path.

```c
VkBool32 present_supported = VK_FALSE;
vkGetPhysicalDeviceSurfaceSupportKHR(ctx->physical_device, ctx->queue_family,
                                      surface, &present_supported);
if (!present_supported) {
    VC_LOG("VideoCommon: queue family %u does not support present\n",
           ctx->queue_family);
    /* Fall back or try to find a present-capable queue family */
}
```

**Design decision**: We do NOT search for a separate present queue. We verify
the graphics queue supports present, and if it does not, we log an error and
fall back to SW rendering. This keeps the single-queue architecture simple and
avoids the cross-queue synchronization complexity that would come from separate
graphics and present queues.

---

## 2. Swapchain Management

### 2.1 Swapchain Creation

The swapchain is created by the GPU thread when it detects a new surface handle
has been set (via atomic). This ensures single-thread ownership -- the GPU
thread is the ONLY thread that touches VkSwapchainKHR.

```c
/* vc_display.c -- called from GPU thread */
static int
vc_swapchain_create(vc_ctx_t *ctx, vc_display_t *disp)
{
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->physical_device,
                                               disp->surface, &caps);

    /* Image count: prefer 3 (triple buffer), clamp to surface limits. */
    uint32_t image_count = 3;
    if (caps.minImageCount > image_count)
        image_count = caps.minImageCount;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount)
        image_count = caps.maxImageCount;

    /* Format selection. */
    VkSurfaceFormatKHR format = vc_select_surface_format(ctx, disp->surface);

    /* Present mode: prefer FIFO (vsync). */
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;

    /* Extent: use current surface extent. */
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        /* Surface doesn't report extent; use widget dimensions. */
        extent.width  = disp->window_width;
        extent.height = disp->window_height;
    }

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
    ci.presentMode      = mode;
    ci.clipped          = VK_TRUE;
    ci.oldSwapchain     = disp->swapchain; /* VK_NULL_HANDLE on first create */

    VkResult result = vkCreateSwapchainKHR(ctx->device, &ci, NULL,
                                            &disp->swapchain);
    /* ... retrieve images, create image views ... */
}
```

### 2.2 Format Selection

**Preferred**: `VK_FORMAT_B8G8R8A8_UNORM` with `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`.

Rationale: Our offscreen framebuffer is `R8G8B8A8_UNORM`. We do NOT want
automatic sRGB gamma correction on the swapchain -- the Voodoo pipeline does
not produce linear-space colors. Using `B8G8R8A8_UNORM` (not `_SRGB`) ensures
the post-process blit copies colors without gamma transformation. The component
swizzle (RGBA vs BGRA) is handled automatically by the blit shader which
outputs to a color attachment matching the swapchain format.

If `B8G8R8A8_UNORM` is not available, fall back to the first format in the
surface format list. Every surface must support at least one format per the
Vulkan spec.

```c
static VkSurfaceFormatKHR
vc_select_surface_format(vc_ctx_t *ctx, VkSurfaceKHR surface)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, surface,
                                          &count, NULL);

    VkSurfaceFormatKHR *formats = malloc(count * sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, surface,
                                          &count, formats);

    /* Prefer B8G8R8A8_UNORM + SRGB_NONLINEAR. */
    VkSurfaceFormatKHR chosen = formats[0]; /* fallback */
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
```

### 2.3 Present Mode

**Use `VK_PRESENT_MODE_FIFO_KHR` unconditionally.**

Rationale:
- FIFO is the ONLY present mode guaranteed by the Vulkan spec.
- FIFO provides vsync, which matches the Voodoo's retrace-based swap timing.
- MoltenVK supports FIFO reliably. `VK_PRESENT_MODE_IMMEDIATE_KHR` on
  MoltenVK sets `CAMetalLayer.displaySyncEnabled = NO`, which can degrade
  performance (per MoltenVK docs and PPSSPP issue #18084).
- The FIFO queue length matches our triple-buffer swapchain (3 images).
- The Voodoo display callback handles frame pacing via retrace timing. We do
  NOT need present-mode-based frame pacing.

### 2.4 Swapchain Image Count

**3 images (triple buffer).**

Rationale per DESIGN.md section 8.2 and 10.3:
- MoltenVK lazily acquires `CAMetalDrawable` from the `CAMetalLayer`. Having
  only 2 images risks blocking on `vkAcquireNextImageKHR` if both images are
  in use (one being presented, one being rendered to).
- 3 images provides a pipeline: one presented, one being rendered, one
  available for acquire.
- Clamped to `[minImageCount, maxImageCount]` from surface capabilities.

### 2.5 Swapchain Recreation

Swapchain must be recreated when:
1. `vkAcquireNextImageKHR` returns `VK_ERROR_OUT_OF_DATE_KHR`
2. `vkQueuePresentKHR` returns `VK_ERROR_OUT_OF_DATE_KHR`
3. `vkQueuePresentKHR` returns `VK_SUBOPTIMAL_KHR`
4. The VCRenderer signals a resize via atomic flag

**Recreation procedure** (all on GPU thread):

```
1. vkDeviceWaitIdle(device)           -- drain GPU
2. Destroy old image views
3. Call vkCreateSwapchainKHR with oldSwapchain = current swapchain
4. Destroy old swapchain (vkDestroySwapchainKHR)
5. Get new swapchain images (vkGetSwapchainImagesKHR)
6. Create new image views
7. Recreate post-process framebuffers (one per swapchain image)
```

**Fence deadlock prevention**: Per the Vulkan tutorial best practice, do NOT
reset a frame fence until AFTER confirming work will be submitted. If
`vkAcquireNextImageKHR` returns `VK_ERROR_OUT_OF_DATE_KHR`, do NOT reset the
fence -- just recreate the swapchain and try again. The fence remains signaled
from the previous frame.

**Semaphore cleanup on recreation**: When recreating the swapchain, destroy and
recreate the `image_available_sem` and `render_finished_sem` for each frame.
This handles the edge case where a semaphore was signaled by a successful
acquire but never consumed due to a failed present.

### 2.6 Swapchain Destruction and Teardown

Per Vulkan spec (VUID-vkDestroySwapchainKHR-swapchain-01282): ALL uses of
presentable images associated with the swapchain must have completed before
the swapchain is destroyed.

**Teardown sequence** (GPU thread):

```
1. vkDeviceWaitIdle(device)
2. Destroy post-process framebuffers
3. Destroy swapchain image views
4. vkDestroySwapchainKHR(device, swapchain, NULL)
5. Destroy semaphores
6. Signal teardown complete event
```

The GUI thread (VCRenderer::finalize) then destroys the VkSurfaceKHR after
receiving the teardown complete signal.

### 2.7 Deferred Swapchain Creation

The GPU thread starts (Phase 1/2) before the VCRenderer exists. The swapchain
is NOT created during GPU thread init. Instead:

```
GPU thread starts -> creates offscreen framebuffers, pipeline, etc.
  (renders to offscreen only, no present)

VCRenderer initialized -> creates VkSurfaceKHR
  -> atomic_store(&ctx->display.surface, surface)

GPU thread sees surface on next vc_display_tick()
  -> creates swapchain, post-process pipeline
  -> starts presenting on VC_CMD_SWAP
```

This means Phase 2 behavior (render to offscreen, no visible output) continues
to work even when no VCRenderer is active. Phase 3 adds visibility without
breaking Phase 2.

---

## 3. Post-Process Blit

### 3.1 Fullscreen Triangle Technique

Use a fullscreen triangle (3 vertices, no vertex buffer) to blit the offscreen
framebuffer to the swapchain image. This is the standard technique used by
Sascha Willems' Vulkan samples, DuckStation, and many other Vulkan applications.

**Vertex shader** (`shaders/postprocess.vert`):
```glsl
#version 450

layout(location = 0) out vec2 out_uv;

void main()
{
    /* Generate fullscreen triangle from vertex index.
       Vertex 0: (-1, -1), UV (0, 0)
       Vertex 1: ( 3, -1), UV (2, 0)
       Vertex 2: (-1,  3), UV (0, 2)
       The triangle covers the entire clip space [-1,1] x [-1,1].
       Clipping removes the parts outside the viewport for free. */
    out_uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(out_uv * 2.0 - 1.0, 0.0, 1.0);
}
```

**Fragment shader** (`shaders/postprocess.frag`):
```glsl
#version 450

layout(set = 0, binding = 0) uniform sampler2D offscreen_tex;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    out_color = texture(offscreen_tex, in_uv);
}
```

### 3.2 Post-Process Render Pass

A separate VkRenderPass for the post-process blit:
- Single color attachment: swapchain image format
- `loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE` (we write every pixel)
- `storeOp = VK_ATTACHMENT_STORE_OP_STORE`
- `initialLayout = VK_IMAGE_LAYOUT_UNDEFINED` (discard old contents)
- `finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` (ready for present)
- No depth attachment

This render pass handles both swapchain image layout transitions:
- `UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL` at begin (implicit)
- `COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR` at end (via finalLayout)

### 3.3 Post-Process Pipeline

A separate VkPipeline for the fullscreen triangle:
- Vertex shader: `postprocess.vert`
- Fragment shader: `postprocess.frag`
- **No vertex input state** (empty `VkPipelineVertexInputStateCreateInfo`)
- Primitive topology: `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST`
- Cull mode: `VK_CULL_MODE_NONE`
- Depth test: disabled
- Blend: disabled (opaque write)
- Dynamic state: viewport, scissor
- Descriptor set layout: 1 combined image sampler (set 0, binding 0)

### 3.4 Descriptor Set for Offscreen Image

The post-process fragment shader needs to sample the offscreen color image.
This requires:
- A `VkSampler` with `VK_FILTER_NEAREST` (pixel-perfect)
- A `VkDescriptorSetLayout` with one `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`
- A `VkDescriptorPool` and `VkDescriptorSet` (one per offscreen framebuffer,
  since we double-buffer offscreen: front and back)

**VK_KHR_push_descriptor alternative**: If the device supports
`VK_KHR_push_descriptor`, we can use `vkCmdPushDescriptorSetKHR` to avoid
pre-allocating descriptor pools and sets. This simplifies the code. However,
since the descriptor content changes only when the offscreen framebuffer
changes (rarely), pre-allocated descriptor sets are simple enough.

**Decision**: Use pre-allocated descriptor sets (2 sets, one per offscreen FB).
Update only when framebuffers are recreated (resolution change). This is
simpler than push descriptors for static resources.

### 3.5 Sampler

```c
VkSamplerCreateInfo sampler_ci;
memset(&sampler_ci, 0, sizeof(sampler_ci));
sampler_ci.sType     = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
sampler_ci.magFilter = VK_FILTER_NEAREST;
sampler_ci.minFilter = VK_FILTER_NEAREST;
sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
sampler_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
sampler_ci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
```

Nearest-neighbor filtering gives pixel-perfect scaling. Bilinear filtering
can be a future user preference option (Phase 8: Polish).

### 3.6 Image Layout Transitions

The full set of layout transitions in the present flow, consolidated:

```
Offscreen image transitions (per frame):
  COLOR_ATTACHMENT_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL  (barrier, before blit)
  SHADER_READ_ONLY_OPTIMAL -> COLOR_ATTACHMENT_OPTIMAL  (barrier, after blit)

Swapchain image transitions (per frame):
  UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL  (post-process render pass initialLayout)
  COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR  (post-process render pass finalLayout)
```

The pipeline barrier for the offscreen image:

```c
/* Before post-process blit: offscreen -> SHADER_READ_ONLY */
VkImageMemoryBarrier barrier;
memset(&barrier, 0, sizeof(barrier));
barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
barrier.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
barrier.oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
barrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
barrier.image               = offscreen_color_image;
barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

vkCmdPipelineBarrier(cmd_buf,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    0, 0, NULL, 0, NULL, 1, &barrier);

/* After post-process blit: offscreen -> COLOR_ATTACHMENT_OPTIMAL */
barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
barrier.oldLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
barrier.newLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

vkCmdPipelineBarrier(cmd_buf,
    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    0, 0, NULL, 0, NULL, 1, &barrier);
```

### 3.7 Scaling

The offscreen framebuffer (Voodoo native resolution, e.g., 640x480) may differ
from the window size (e.g., 1280x960 or arbitrary). The fullscreen triangle
with nearest-neighbor sampling handles this automatically -- the UV coordinates
span [0,1] and the sampler reads from the offscreen image at its native
resolution.

The viewport and scissor are set to the swapchain extent, and the fullscreen
triangle covers the entire swapchain image. The fragment shader samples from
the offscreen texture using the UV coordinates, producing pixel-perfect
upscaling.

**Aspect ratio correction**: For Phase 3, we stretch to fill the window. A
future enhancement can maintain aspect ratio with letterboxing by adjusting
the UV coordinates or viewport.

---

## 4. Display Callback Integration

### 4.1 The Existing Display Callback

`voodoo_callback()` in `vid_voodoo_display.c` runs on the 86Box timer thread,
once per scanline. It does three things:

1. **Scanout** (line < v_disp): reads pixels from `fb_mem[front_offset]`,
   converts RGB565 to RGB888 via CLUT, writes to `monitor->target_buffer`.
   Guarded by `fbiInit0 & FBIINIT0_VGA_PASS`.

2. **Swap lifecycle** (line == v_disp): increments `retrace_count`, checks
   `swap_pending && retrace_count > swap_interval`, decrements `swap_count`,
   clears `swap_pending`, wakes FIFO thread.

3. **Blit trigger** (line == v_disp, second VGA_PASS block): calls
   `svga_doblit()` to push the frame to the Qt display path.

### 4.2 What Phase 3 Changes

Per DESIGN.md section 8.5 and PHASES.md lines 207-217:

**SKIP in VK mode:**
- Section 1 (scanout): per-scanline pixel reads from fb_mem
- Section 3 (blit trigger): svga_doblit() call

**KEEP UNCHANGED:**
- Section 2 (swap lifecycle): retrace_count++, swap_count--, swap_pending=0,
  wake_fifo_thread. This is THE critical timing mechanism.

**IMPORTANT — Guard flag must be `vc_display_active`, NOT `use_gpu_renderer`:**

During the Glide detection bug fix (2026-03-01), we discovered that the Voodoo 2
Windows miniport driver performs a self-test during boot: it draws triangles to
the framebuffer and reads them back. If the SW scanout is skipped at boot
(because `use_gpu_renderer=1` is set from config), the framebuffer stays empty,
the driver marks the card as non-functional, and Glide says "none detected."

The fix: use a new `vc_display_active` flag (in `voodoo_t`) that is only set
when the VK display pipeline is fully connected (VCRenderer surface + swapchain
created + GPU thread presenting). The triangle diversion in
`vid_voodoo_render.c` must ALSO key off this flag (not `use_gpu_renderer`).

Boot sequence:
1. `use_gpu_renderer=1`, `vc_display_active=0` → SW triangles + SW scanout →
   driver self-test passes, Glide detects hardware
2. VCRenderer connects, swapchain created → GPU thread sets `vc_display_active=1`
3. `vc_display_active=1` → VK triangles + skip SW scanout → frames in window

**Implementation** (two insertion points):

```c
/* Insertion point 1: scanout block (line < v_disp) */
if ((fbiInit0 & FBIINIT0_VGA_PASS) && !voodoo->vc_display_active) {
    if (voodoo->line < voodoo->v_disp) {
        /* ... existing scanout code ... */
    }
}

/* (swap lifecycle code runs unconditionally between these two blocks) */

/* Insertion point 2: blit trigger block (line == v_disp) */
if ((fbiInit0 & FBIINIT0_VGA_PASS) && !voodoo->vc_display_active) {
    if (voodoo->line == voodoo->v_disp) {
        /* ... existing svga_doblit() code ... */
    }
}
```

**CRITICAL: A single `goto skip_scanout` will NOT work.** The swap lifecycle
code (section 2) sits between the two VGA_PASS blocks. A goto would skip
the swap lifecycle, which would break `swap_count` decrement and cause the
exact Bug 5 from v1 (swap_count stuck at 3). Each block must be independently
guarded.

### 4.3 Exact Code Modification

Looking at the actual `voodoo_callback()` code:

```c
/* Line 515-578: First VGA_PASS block (scanout) */
if (voodoo->fbiInit0 & FBIINIT0_VGA_PASS) {
    if (voodoo->line < voodoo->v_disp) {
        /* ... per-scanline pixel output ... */
    }
}
skip_draw:

/* Line 580-631: Swap lifecycle (line == v_disp) */
if (voodoo->line == voodoo->v_disp) {
    voodoo->retrace_count++;
    /* ... swap_pending / swap_count management ... */
    /* ... wake_fifo_thread ... */
    voodoo->v_retrace = 1;
}
voodoo->line++;

/* Line 635-657: Second VGA_PASS block (blit trigger) */
if (voodoo->fbiInit0 & FBIINIT0_VGA_PASS) {
    if (voodoo->line == voodoo->v_disp) {
        /* ... svga_doblit() ... */
    }
}
```

The change:

```c
/* First VGA_PASS block: add !use_gpu_renderer */
if ((voodoo->fbiInit0 & FBIINIT0_VGA_PASS) && !voodoo->vc_display_active) {
    if (voodoo->line < voodoo->v_disp) {
        /* ... per-scanline pixel output (unchanged) ... */
    }
}

/* Swap lifecycle: UNCHANGED -- always runs */

/* Second VGA_PASS block: add !use_gpu_renderer */
if ((voodoo->fbiInit0 & FBIINIT0_VGA_PASS) && !voodoo->vc_display_active) {
    if (voodoo->line == voodoo->v_disp) {
        /* ... svga_doblit() (unchanged) ... */
    }
}
```

### 4.4 When Does the GPU Thread Present?

The GPU thread presents when it processes `VC_CMD_SWAP` from the ring buffer.
This is pushed by the FIFO thread when it processes `SST_swapbufferCMD`.

The present flow:
1. FIFO thread processes swap -> pushes `VC_CMD_SWAP` to ring
2. FIFO thread also does `swap_pending = 1` (existing code, unchanged)
3. GPU thread processes `VC_CMD_SWAP` -> ends render pass, does post-process
   blit, calls `vkQueuePresentKHR`
4. Display callback (timer thread) independently decrements `swap_count` at
   the right retrace time (existing code, unchanged)

The GPU thread's present and the display callback's swap_count decrement are
independent and unsynchronized -- by design. The display callback handles
guest-visible timing; the GPU thread handles host-visible display. These are
separate concerns.

---

## 5. VCRenderer Class Design

### 5.1 Architecture

VCRenderer is a QWidget-derived class that:
1. Creates a VkSurfaceKHR from its native window handle
2. Passes the surface to VideoCommon via atomic
3. Handles resize events (signals the GPU thread)
4. Handles destruction (teardown handshake with GPU thread)

It does NOT:
- Create any Vulkan device or instance
- Allocate any buffers or command buffers
- Record any drawing commands
- Call vkQueueSubmit or vkQueuePresentKHR

### 5.2 Class Definition

```cpp
// qt_vcrenderer.hpp
#ifndef QT_VCRENDERER_HPP
#define QT_VCRENDERER_HPP

#include "qt_renderercommon.hpp"
#include <QWidget>
#include <atomic>

extern "C" {
struct vc_ctx_t;
}

class VCRenderer : public QWidget, public RendererCommon {
    Q_OBJECT
public:
    explicit VCRenderer(QWidget *parent);
    ~VCRenderer() override;

    void initialize();
    void finalize() override;

signals:
    void initialized();
    void errorInitializing();

protected:
    void resizeEvent(QResizeEvent *event) override;

    std::vector<std::tuple<uint8_t *, std::atomic_flag *>> getBuffers() override;
    uint32_t getBytesPerRow() override { return 0; }

private:
    VkSurfaceKHR createPlatformSurface();
    void         destroyPlatformSurface();

    VkSurfaceKHR m_surface   = VK_NULL_HANDLE;
    bool         m_finalized = false;
};

#endif // QT_VCRENDERER_HPP
```

### 5.3 Surface Creation (Platform-Specific)

```cpp
VkSurfaceKHR VCRenderer::createPlatformSurface()
{
    /* Ensure native window exists. */
    setAttribute(Qt::WA_NativeWindow, true);
    WId wid = winId();
    if (!wid)
        return VK_NULL_HANDLE;

    vc_ctx_t *ctx = /* get from global or voodoo_t */;
    VkSurfaceKHR surface = VK_NULL_HANDLE;

#if defined(Q_OS_MACOS)
    /* macOS: winId() returns NSView*. MoltenVK VK_EXT_metal_surface can
       accept either a CAMetalLayer or an NSView. When passed an NSView,
       MoltenVK extracts or creates the CAMetalLayer automatically.
       HOWEVER, per DESIGN.md 10.1 and MoltenVK issue #234, creating
       surface from NSView on a non-main thread can deadlock. Since
       createPlatformSurface() runs on the GUI (main) thread, this is safe. */
    VkMetalSurfaceCreateInfoEXT metal_ci;
    memset(&metal_ci, 0, sizeof(metal_ci));
    metal_ci.sType  = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    metal_ci.pLayer = vc_get_metal_layer((void *)wid);
    vkCreateMetalSurfaceEXT(ctx->instance, &metal_ci, NULL, &surface);

#elif defined(Q_OS_WIN)
    /* Windows: winId() returns HWND. */
    VkWin32SurfaceCreateInfoKHR win32_ci;
    memset(&win32_ci, 0, sizeof(win32_ci));
    win32_ci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    win32_ci.hwnd  = (HWND)wid;
    win32_ci.hinstance = GetModuleHandle(NULL);
    vkCreateWin32SurfaceKHR(ctx->instance, &win32_ci, NULL, &surface);

#elif defined(Q_OS_LINUX)
    /* Linux/X11: winId() returns xcb_window_t when running under xcb. */
    /* TODO: Wayland support in a future phase. */
    QWindow *window = windowHandle();
    if (!window)
        window = nativeParentWidget()->windowHandle();

    /* Get XCB connection from Qt platform native interface. */
    QPlatformNativeInterface *pni = QGuiApplication::platformNativeInterface();
    xcb_connection_t *connection = static_cast<xcb_connection_t *>(
        pni->nativeResourceForWindow("connection", window));

    VkXcbSurfaceCreateInfoKHR xcb_ci;
    memset(&xcb_ci, 0, sizeof(xcb_ci));
    xcb_ci.sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    xcb_ci.connection  = connection;
    xcb_ci.window      = (xcb_window_t)wid;
    vkCreateXcbSurfaceKHR(ctx->instance, &xcb_ci, NULL, &surface);
#endif

    return surface;
}
```

### 5.4 macOS CAMetalLayer Helper

The `vc_get_metal_layer()` function must be implemented in an Objective-C file
(`.m` or `.mm`) because it accesses Cocoa/AppKit APIs.

```objc
/* vc_macos_surface.m */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

void *vc_get_metal_layer(void *ns_view_ptr)
{
    NSView *view = (__bridge NSView *)ns_view_ptr;

    /* If the view already has a CAMetalLayer, return it. */
    if ([view.layer isKindOfClass:[CAMetalLayer class]])
        return (__bridge void *)view.layer;

    /* Otherwise, make the view layer-backed with a CAMetalLayer. */
    [view setWantsLayer:YES];
    CAMetalLayer *layer = [CAMetalLayer layer];
    [view setLayer:layer];
    layer.delegate = (id<CALayerDelegate>)view;

    return (__bridge void *)layer;
}
```

**Why extract the CAMetalLayer instead of passing NSView to MoltenVK?**

MoltenVK CAN accept an NSView via `VK_EXT_metal_surface`. However, per the
MoltenVK runtime user guide:
> "It is strongly recommended that you ensure the delegate of the CAMetalLayer
> is the NSView/UIView in which the layer is contained, to ensure correct and
> optimized Vulkan swapchain and refresh timing behavior across multiple
> display screens."

By creating the layer ourselves and setting the delegate, we ensure optimal
behavior. If Qt has already set up the layer (e.g., via `setSurfaceType(VulkanSurface)`),
we reuse it.

### 5.5 Resize Handling

```cpp
void VCRenderer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    /* Signal GPU thread to recreate swapchain.
       The GPU thread checks this flag at the top of each loop iteration
       (per DESIGN.md section 8.1). */
    vc_ctx_t *ctx = /* get from global or voodoo_t */;
    if (ctx) {
        int w = event->size().width() * devicePixelRatio();
        int h = event->size().height() * devicePixelRatio();
        vc_display_signal_resize(ctx, (uint32_t)w, (uint32_t)h);
    }
}
```

**Retina/HiDPI**: On macOS, the physical pixel size may differ from the
logical size. `devicePixelRatio()` returns 2.0 on Retina displays. The
swapchain extent should match physical pixels, not logical pixels.

### 5.6 Teardown

```cpp
void VCRenderer::finalize()
{
    if (m_finalized)
        return;
    m_finalized = true;

    vc_ctx_t *ctx = /* get from global or voodoo_t */;
    if (ctx) {
        /* Signal GPU thread to destroy swapchain resources. */
        vc_display_request_teardown(ctx);
        /* Wait for GPU thread to finish destroying swapchain. */
        vc_display_wait_teardown(ctx);
    }

    /* Now safe to destroy the surface. */
    if (m_surface != VK_NULL_HANDLE && ctx) {
        vkDestroySurfaceKHR(ctx->instance, m_surface, NULL);
        m_surface = VK_NULL_HANDLE;
    }
}
```

### 5.7 Integration into RendererStack

Add `VCRenderer` as a new renderer option. However, VCRenderer differs from
other renderers in that it does NOT participate in the `blit()` /
`blitToRenderer` path. The GPU thread handles all rendering and presentation
independently. The VCRenderer widget is just a surface container.

This means `getBuffers()` returns an empty vector and `getBytesPerRow()`
returns 0. The `RendererStack::blit()` function will not be called for VCR
because `svga_doblit()` is skipped in VK mode.

The VCRenderer option should only appear when a Voodoo card with
`vc_display_active=1` is active. This can be detected at renderer selection
time.

**Integration approach**: Rather than adding a new `Renderer::VideoCommon`
enum value, the VCRenderer can be created and managed directly by the Voodoo
VideoCommon init path. The VCRenderer widget is embedded into the existing
RendererStack layout, replacing the current renderer widget. When the Voodoo
card is closed, the VCRenderer is destroyed and the previous renderer is
restored.

**Simpler approach for Phase 3**: Skip the RendererStack integration entirely.
The VCRenderer creates a VkSurfaceKHR from the existing renderer widget's
window. The GPU thread presents directly to this surface. The existing renderer
continues to exist but its blit path is inactive (svga_doblit skipped).

This means NO changes to `qt_rendererstack.cpp` in Phase 3. The VCRenderer
creates the surface from the RendererStack's `winId()` directly. This is the
minimal-change approach.

**Revised simpler approach**: Create the VkSurfaceKHR from the RendererStack
widget itself (or its current renderer child). Pass the WId from C++ to C via
a new `vc_display_set_window_handle()` API. The C code (vc_display.c) creates
the platform-specific VkSurfaceKHR.

This keeps all Vulkan code in C and avoids mixing Vulkan calls into C++ Qt code.

---

## 6. Cross-Platform Surface Creation

### 6.1 macOS

**Extension**: `VK_EXT_metal_surface` (current, replaces deprecated `VK_MVK_macos_surface`)

**API**: `vkCreateMetalSurfaceEXT`

**Input**: `CAMetalLayer *` (extracted from NSView via `vc_get_metal_layer()`)

**Threading**: Surface creation MUST happen on the main thread (macOS AppKit
requirement). The GUI thread is the main thread in 86Box, so this is safe.
MoltenVK issue #234 documents that creating surfaces from non-main threads
can deadlock.

**Retina**: The `CAMetalLayer`'s `contentsScale` should be set to
`NSView.backingScaleFactor` for correct Retina rendering. Qt handles this
automatically when the view is layer-backed.

**Present modes supported by MoltenVK**:
- `VK_PRESENT_MODE_FIFO_KHR` -- always supported, maps to
  `CAMetalLayer.displaySyncEnabled = YES`
- `VK_PRESENT_MODE_IMMEDIATE_KHR` -- supported, maps to
  `displaySyncEnabled = NO` (can degrade performance)

### 6.2 Windows

**Extension**: `VK_KHR_win32_surface`

**API**: `vkCreateWin32SurfaceKHR`

**Input**: `HWND` (from `winId()`) and `HINSTANCE` (from `GetModuleHandle(NULL)`)

**Threading**: No restrictions. Surface can be created from any thread.

**Present modes**: All modes typically supported (FIFO, MAILBOX, IMMEDIATE,
FIFO_RELAXED). We use FIFO.

### 6.3 Linux (X11/xcb)

**Extension**: `VK_KHR_xcb_surface`

**API**: `vkCreateXcbSurfaceKHR`

**Input**: `xcb_connection_t *` (from Qt platform native interface) and
`xcb_window_t` (from `winId()`)

**Getting the xcb connection from Qt5**:
```cpp
QPlatformNativeInterface *pni = QGuiApplication::platformNativeInterface();
xcb_connection_t *connection = static_cast<xcb_connection_t *>(
    pni->nativeResourceForWindow("connection", window));
```

**Threading**: No restrictions.

### 6.4 Linux (Wayland) -- Future Phase

**Extension**: `VK_KHR_wayland_surface`

**API**: `vkCreateWaylandSurfaceKHR`

**Input**: `wl_display *` and `wl_surface *` (from Qt platform native interface)

**Not implemented in Phase 3**: The current codebase only enables
`VK_KHR_xcb_surface` on Linux. Adding Wayland requires:
1. Runtime detection of display server (X11 vs Wayland)
2. Conditional instance extension enabling
3. Platform-specific surface creation based on detected backend

This is deferred to a future phase. XWayland provides X11 compatibility for
Wayland users in the meantime.

### 6.5 Pi 5

Same as Linux (X11/xcb or Wayland depending on desktop environment).
V3DV driver supports `VK_KHR_xcb_surface` and `VK_KHR_wayland_surface`.

### 6.6 Surface Creation C API

To keep all Vulkan code in C and avoid platform-specific Vulkan calls in C++
Qt code, provide a C API that accepts a platform window handle:

```c
/* vc_display.h */

/* Set the native window handle for surface creation.
   Called from the Qt GUI thread. The GPU thread creates the VkSurfaceKHR
   and swapchain when it sees a non-zero handle.
   handle is: NSView* (macOS), HWND (Windows), xcb_window_t (Linux). */
void vc_display_set_window_handle(vc_ctx_t *ctx, uintptr_t handle);

/* Signal resize. GPU thread recreates swapchain on next tick. */
void vc_display_signal_resize(vc_ctx_t *ctx, uint32_t width, uint32_t height);

/* Request teardown. GPU thread destroys swapchain on next tick. */
void vc_display_request_teardown(vc_ctx_t *ctx);

/* Wait for GPU thread to complete teardown. */
void vc_display_wait_teardown(vc_ctx_t *ctx);
```

**Alternative**: The GUI thread creates the VkSurfaceKHR and passes it via
atomic. This is cleaner because `vkCreateXcbSurfaceKHR` etc. require
platform-specific headers that are easier to handle in C++ Qt code.

**Decision**: GUI thread creates VkSurfaceKHR, passes the handle atomically
to the GPU thread. This matches the DESIGN.md section 8.1 flow exactly.

---

## 7. Threading Considerations

### 7.1 Thread Ownership Model

| Resource | Owner | Notes |
|----------|-------|-------|
| VkSurfaceKHR | GUI thread creates, GPU thread uses, GUI thread destroys | Atomic handoff |
| VkSwapchainKHR | GPU thread only | Created/destroyed/recreated on GPU thread |
| Swapchain VkImages | GPU thread only | Owned by swapchain |
| Swapchain VkImageViews | GPU thread only | Created/destroyed with swapchain |
| Post-process pipeline | GPU thread only | Created at display init |
| Post-process render pass | GPU thread only | Created at display init |
| Descriptor sets | GPU thread only | Updated when offscreen FB changes |
| Semaphores | GPU thread only | Per-frame, created/destroyed with swapchain |
| swap_count | Display callback (timer thread) | NEVER touched by GPU thread |
| swap_pending | FIFO thread + display callback | NEVER touched by GPU thread |

### 7.2 Atomic Communication

The GUI thread and GPU thread communicate through atomics:

```c
/* In vc_display_t (part of vc_ctx_t) */
typedef struct vc_display_t {
    _Atomic(VkSurfaceKHR) surface_pending;  /* GUI -> GPU: new surface */
    _Atomic(uint32_t)     resize_width;     /* GUI -> GPU: new size */
    _Atomic(uint32_t)     resize_height;
    _Atomic(int)          resize_requested; /* GUI -> GPU: flag */
    _Atomic(int)          teardown_requested; /* GUI -> GPU: flag */
    _Atomic(int)          teardown_complete;  /* GPU -> GUI: flag */

    /* GPU-thread-only state (not atomic) */
    VkSurfaceKHR       surface;
    VkSwapchainKHR     swapchain;
    VkImage           *images;
    VkImageView       *image_views;
    uint32_t           image_count;
    VkFormat           format;
    VkExtent2D         extent;

    /* Post-process pipeline */
    VkRenderPass       pp_render_pass;
    VkPipeline         pp_pipeline;
    VkPipelineLayout   pp_pipeline_layout;
    VkDescriptorSetLayout pp_desc_layout;
    VkDescriptorPool   pp_desc_pool;
    VkDescriptorSet    pp_desc_sets[2]; /* One per offscreen FB */
    VkSampler          pp_sampler;
    VkFramebuffer     *pp_framebuffers; /* One per swapchain image */

    /* Semaphores (per-frame) */
    VkSemaphore        image_available_sem[VC_NUM_FRAMES];
    VkSemaphore        render_finished_sem[VC_NUM_FRAMES];
} vc_display_t;
```

### 7.3 GPU Thread Display Tick

Called at the top of each GPU thread loop iteration:

```c
static void
vc_display_tick(vc_ctx_t *ctx, vc_display_t *disp)
{
    /* Check for teardown request. */
    if (atomic_load_explicit(&disp->teardown_requested, memory_order_acquire)) {
        vc_swapchain_destroy(ctx, disp);
        atomic_store_explicit(&disp->teardown_complete, 1, memory_order_release);
        return;
    }

    /* Check for new surface. */
    VkSurfaceKHR new_surface = atomic_exchange_explicit(
        &disp->surface_pending, VK_NULL_HANDLE, memory_order_acquire);
    if (new_surface != VK_NULL_HANDLE) {
        disp->surface = new_surface;
        vc_swapchain_create(ctx, disp);
    }

    /* Check for resize. */
    if (atomic_exchange_explicit(&disp->resize_requested, 0,
                                  memory_order_acquire)) {
        disp->extent.width = atomic_load(&disp->resize_width);
        disp->extent.height = atomic_load(&disp->resize_height);
        vc_swapchain_recreate(ctx, disp);
    }
}
```

### 7.4 Present Flow in VC_CMD_SWAP Handler

The existing `vc_gpu_handle_swap()` is extended:

```c
static void
vc_gpu_handle_swap(vc_ctx_t *ctx, vc_gpu_state_t *gpu_st)
{
    if (!gpu_st->render_pass_active)
        return;

    vc_frame_t *f = &gpu_st->frame[gpu_st->frame_index];

    /* Flush remaining triangles. */
    if (gpu_st->batch.triangle_count > 0)
        vkCmdDraw(f->cmd_buf, gpu_st->batch.triangle_count * 3, 1, 0, 0);

    /* End offscreen render pass. */
    vkCmdEndRenderPass(f->cmd_buf);
    gpu_st->render_pass_active = 0;

    /* If swapchain is available, do post-process blit + present. */
    if (gpu_st->disp.swapchain != VK_NULL_HANDLE) {
        uint32_t image_idx;
        VkResult acq = vkAcquireNextImageKHR(ctx->device,
            gpu_st->disp.swapchain, UINT64_MAX,
            gpu_st->disp.image_available_sem[gpu_st->frame_index],
            VK_NULL_HANDLE, &image_idx);

        if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
            /* Swapchain needs recreation. Skip present this frame. */
            vc_swapchain_recreate(ctx, &gpu_st->disp);
        } else if (acq == VK_SUCCESS || acq == VK_SUBOPTIMAL_KHR) {
            /* Barrier: offscreen -> SHADER_READ_ONLY */
            vc_barrier_offscreen_to_read(f->cmd_buf, gpu_st);

            /* Begin post-process render pass on swapchain image. */
            vc_begin_postprocess_pass(f->cmd_buf, gpu_st, image_idx);

            /* Bind post-process pipeline and descriptor set. */
            vkCmdBindPipeline(f->cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              gpu_st->disp.pp_pipeline);
            vkCmdBindDescriptorSets(f->cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                gpu_st->disp.pp_pipeline_layout, 0, 1,
                &gpu_st->disp.pp_desc_sets[gpu_st->rp.back_index], 0, NULL);

            /* Set viewport and scissor to swapchain extent. */
            /* ... */

            /* Draw fullscreen triangle (3 vertices, no vertex buffer). */
            vkCmdDraw(f->cmd_buf, 3, 1, 0, 0);

            /* End post-process render pass (finalLayout -> PRESENT_SRC). */
            vkCmdEndRenderPass(f->cmd_buf);

            /* Barrier: offscreen -> COLOR_ATTACHMENT_OPTIMAL */
            vc_barrier_offscreen_to_attachment(f->cmd_buf, gpu_st);

            /* End command buffer recording. */
            vkEndCommandBuffer(f->cmd_buf);

            /* Submit with semaphore synchronization. */
            VkPipelineStageFlags wait_stage =
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo submit;
            memset(&submit, 0, sizeof(submit));
            submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.waitSemaphoreCount   = 1;
            submit.pWaitSemaphores      = &gpu_st->disp.image_available_sem[gpu_st->frame_index];
            submit.pWaitDstStageMask    = &wait_stage;
            submit.commandBufferCount   = 1;
            submit.pCommandBuffers      = &f->cmd_buf;
            submit.signalSemaphoreCount = 1;
            submit.pSignalSemaphores    = &gpu_st->disp.render_finished_sem[gpu_st->frame_index];

            vkQueueSubmit(ctx->queue, 1, &submit, f->fence);
            f->submitted = 1;

            /* Present. */
            VkPresentInfoKHR present;
            memset(&present, 0, sizeof(present));
            present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            present.waitSemaphoreCount = 1;
            present.pWaitSemaphores    = &gpu_st->disp.render_finished_sem[gpu_st->frame_index];
            present.swapchainCount     = 1;
            present.pSwapchains        = &gpu_st->disp.swapchain;
            present.pImageIndices      = &image_idx;

            VkResult pres = vkQueuePresentKHR(ctx->queue, &present);
            if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) {
                vc_swapchain_recreate(ctx, &gpu_st->disp);
            }

            goto advance;
        }
    }

    /* No swapchain (or failed acquire): submit without present. */
    vkEndCommandBuffer(f->cmd_buf);
    VkSubmitInfo submit;
    memset(&submit, 0, sizeof(submit));
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &f->cmd_buf;
    vkQueueSubmit(ctx->queue, 1, &submit, f->fence);
    f->submitted = 1;

advance:
    /* Swap front/back offscreen framebuffers. */
    vc_render_pass_swap(gpu_st);
    gpu_st->frame_index = (gpu_st->frame_index + 1) % VC_NUM_FRAMES;
    vc_batch_reset(ctx, gpu_st);
}
```

**Key insight**: When no swapchain is available, the GPU thread still renders
to the offscreen framebuffer and submits the command buffer. The only
difference is the absence of the post-process blit and present. This means
Phase 2 behavior is preserved when no display surface exists.

### 7.5 MoltenVK-Specific Threading

Per MoltenVK documentation and DESIGN.md section 10.2:

- `MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS=1` (default): correct for our model.
  The GPU thread is the sole queue owner.
- `vkQueuePresentKHR` on MoltenVK blocks the calling thread while waiting on
  semaphores (MoltenVK issue #486). Since the GPU thread is the only caller,
  this is acceptable.
- Surface creation from non-main thread can deadlock (MoltenVK issue #234).
  Our surface is created on the GUI (main) thread, so this is safe.

---

## 8. Implementation Plan

### 8.1 Files to Create

| File | Description | ~Lines |
|------|-------------|--------|
| `src/video/videocommon/vc_display.c` | Swapchain, post-process pipeline, present | ~500 |
| `src/video/videocommon/vc_display.h` | Display state struct and API declarations | ~60 |
| `src/qt/qt_vcrenderer.cpp` | VCRenderer: surface creation, resize, teardown | ~200 |
| `src/qt/qt_vcrenderer.hpp` | VCRenderer class declaration | ~40 |
| `shaders/postprocess.vert` | Fullscreen triangle vertex shader | ~12 |
| `shaders/postprocess.frag` | Sample offscreen FB, output to swapchain | ~10 |

macOS only:
| `src/video/videocommon/vc_macos_surface.m` | CAMetalLayer extraction from NSView | ~20 |

### 8.2 Files to Modify

| File | Change |
|------|--------|
| `src/include/86box/vid_voodoo_common.h` | Add `int vc_display_active` to `voodoo_t` |
| `src/video/vid_voodoo_display.c` | Add `!vc_display_active` guard to both VGA_PASS blocks |
| `src/video/vid_voodoo_render.c` | Activate VK triangle diversion (keyed on `vc_display_active`) |
| `src/video/videocommon/vc_thread.c` | Add `vc_display_tick()` call in main loop, extend swap handler |
| `src/video/videocommon/vc_gpu_state.h` | Add `vc_display_t` member |
| `src/video/videocommon/vc_internal.h` | Add display-related fields to `vc_ctx_t` |
| `src/video/videocommon/vc_core.c` | Add `vc_display_set_surface()` API |
| `src/include/86box/videocommon.h` | Add display API declarations |
| `src/video/videocommon/CMakeLists.txt` | Add new source files |
| `src/qt/CMakeLists.txt` | Add qt_vcrenderer.cpp |

### 8.3 Implementation Order

1. **Add `vc_display_active` flag** to `voodoo_t` in `vid_voodoo_common.h`
2. **Post-process shaders** (vert + frag) + SPIR-V compilation
3. **vc_display.c**: swapchain creation, post-process pipeline, present flow
4. **VCRenderer** (`qt_vcrenderer.cpp`): surface creation, atomic handoff
5. **macOS surface helper** (`vc_macos_surface.m`)
6. **GPU thread integration** (`vc_thread.c`): display_tick, swap handler,
   set `vc_display_active=1` when swapchain is created
7. **Activate triangle diversion** (`vid_voodoo_render.c`): change the
   Phase 2 TODO to `if (voodoo->vc_display_active && voodoo->vc_ctx)`
8. **Display callback skip** (`vid_voodoo_display.c`): add
   `!voodoo->vc_display_active` guard to both VGA_PASS blocks
   (steps 7+8 must activate together for correct behavior)
9. **Testing**: 3DMark99 with Voodoo 2, verify frames visible, verify swap_count

NOTE: `vc_voodoo_init()` now runs on a background thread (deferred Vulkan init,
added during the Glide detection bug fix).  The GPU thread and vc_ctx are ready
well before the VCRenderer connects, so no sequencing issues.

### 8.4 Estimated Complexity

Total new code: ~850 lines of C + ~250 lines of C++ + ~20 lines of GLSL
Total modifications: ~30 lines changed in existing files

This is significantly less than v1's display integration (~1700 lines for
VCRenderer alone) because the GPU thread owns everything and the VCRenderer
is a thin surface container.

---

## 9. Risks and Mitigations

### 9.1 Risk: Present Blocks GPU Thread (MoltenVK)

**Description**: `vkQueuePresentKHR` on MoltenVK blocks until the semaphore is
signaled. With FIFO present mode, this blocks until the next vsync.

**Impact**: GPU thread is blocked during present, cannot process ring commands.

**Mitigation**: This is acceptable for Phase 3. The Voodoo rarely renders
faster than 60fps. If blocking becomes an issue, MAILBOX present mode or
async presentation (separate present thread) can be investigated later.

### 9.2 Risk: Swapchain Recreation Stalls

**Description**: Rapid resizing can trigger many swapchain recreations.

**Impact**: `vkDeviceWaitIdle` stalls the GPU thread during recreation.

**Mitigation**: Debounce resize events. Only recreate swapchain when the
resize flag is set, and only check the flag once per frame (at VC_CMD_SWAP
time or at the top of the main loop). Rapid resize events coalesce naturally
because the atomic flag is just overwritten.

### 9.3 Risk: Display Callback Timing Affected

**Description**: Skipping svga_doblit() might affect the display callback's
interaction with the video subsystem.

**Impact**: `mon_renderedframes` counter might not be incremented correctly.

**Mitigation**: When `vc_display_active` is set, the VK mode should still
increment `mon_renderedframes` at the appropriate time. Examine
`svga_doblit()` to see what side effects it has beyond pixel copying.

### 9.4 Risk: Qt Widget Destruction Race

**Description**: If the Qt widget is destroyed (e.g., closing the window)
while the GPU thread is presenting to its surface.

**Impact**: Use-after-free crash in `vkQueuePresentKHR`.

**Mitigation**: `VCRenderer::finalize()` performs the teardown handshake:
1. Signal GPU thread to destroy swapchain
2. Wait for GPU thread to confirm destruction
3. Only then destroy the VkSurfaceKHR
4. Only then allow Qt to destroy the widget

This is the exact pattern described in DESIGN.md section 8.1.

### 9.5 Risk: Offscreen FB Descriptor Set Invalidation

**Description**: If the offscreen framebuffer is recreated (resolution change),
the descriptor sets pointing to the old color image view become invalid.

**Impact**: Validation error or crash when sampling stale descriptor.

**Mitigation**: Recreate descriptor sets whenever offscreen framebuffers are
recreated. Both operations happen on the GPU thread, so no synchronization is
needed.

---

## 10. Appendix: Emulator Survey

### 10.1 Dolphin

Dolphin creates VkSurfaceKHR from platform-specific window handles in
`SwapChain.cpp`. It uses a `WindowSystemInfo` struct that abstracts the
platform handle (HWND, X11 Display+Window, Wayland Display+Surface, or
NSView). The surface is created by the GPU thread, NOT the GUI thread.
Extensions are selected in `VulkanContext.cpp` based on `WindowSystemType`.

For macOS, Dolphin uses `VK_EXT_metal_surface` (replacing the deprecated
`VK_MVK_macos_surface`).

Source: `dolphin-emu/dolphin/Source/Core/VideoBackends/Vulkan/VulkanContext.cpp`

### 10.2 DuckStation

DuckStation's `vulkan_host_display.cpp` creates surfaces via an abstracted
`Vulkan::SwapChain::CreateVulkanSurface()` function. It uses the window handle
from its frontend (Qt or SDL) and creates platform-specific surfaces internally.
The swapchain is owned by the render/video thread.

Source: `stenzek/duckstation/src/frontend-common/vulkan_host_display.cpp`

### 10.3 PCSX2

PCSX2 creates surfaces in `GSDeviceVK.cpp` using platform-specific APIs.
Similar to Dolphin, it abstracts the window system into a struct and creates
the surface from the render thread.

### 10.4 Common Patterns

All three emulators:
1. Use platform-specific `vkCreate*SurfaceKHR` calls (NOT QVulkanWindow)
2. Have the render thread own the swapchain exclusively
3. Use FIFO present mode as default
4. Handle `VK_ERROR_OUT_OF_DATE_KHR` by recreating the swapchain
5. Use triple buffering (3 swapchain images)

Our approach matches these proven patterns exactly.

---

## References

- [Vulkan 1.2 Specification -- VkSwapchainCreateInfoKHR](https://registry.khronos.org/vulkan/specs/1.2-extensions/man/html/VkSwapchainCreateInfoKHR.html)
- [Vulkan Tutorial -- Swap Chain Recreation](https://vulkan-tutorial.com/Drawing_a_triangle/Swap_chain_recreation)
- [MoltenVK Runtime User Guide](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md)
- [Sascha Willems -- Fullscreen Quad Without Buffers](https://www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/)
- [Qt Blog -- Vulkan for Qt on macOS](https://www.qt.io/blog/2018/05/30/vulkan-for-qt-on-macos)
- [Dolphin Source -- VulkanContext.cpp](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoBackends/Vulkan/VulkanContext.cpp)
- [DuckStation Source -- vulkan_host_display.cpp](https://github.com/stenzek/duckstation)
- [VkPresentModeKHR Spec](https://registry.khronos.org/vulkan/specs/latest/man/html/VkPresentModeKHR.html)
- [vkGetPhysicalDeviceSurfaceSupportKHR Spec](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceSurfaceSupportKHR.html)
- [Qt5 QVulkanInstance Documentation](https://doc.qt.io/qt-5/qvulkaninstance.html)
- [PPSSPP MoltenVK FIFO Issue #18084](https://github.com/hrydgard/ppsspp/issues/18084)
- [MoltenVK Issue #234 -- Surface from Non-Main Thread](https://github.com/KhronosGroup/MoltenVK/issues/234)
- [MoltenVK Issue #486 -- vkQueuePresentKHR Blocking](https://github.com/KhronosGroup/MoltenVK/issues/486)
