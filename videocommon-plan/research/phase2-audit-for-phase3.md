# Phase 2 Code Audit for Phase 3 Readiness

**Date**: 2026-03-01
**Auditor**: vc-debug agent
**Branch**: videocommon-voodoo
**Scope**: All Phase 2 files (headers, sources, shaders, CMake, integration hooks)

---

## Table of Contents

1. [Summary](#1-summary)
2. [Critical Issues](#2-critical-issues)
3. [Important Issues](#3-important-issues)
4. [Minor Issues](#4-minor-issues)
5. [Phase 3 Readiness Assessment](#5-phase-3-readiness-assessment)
6. [Files Audited](#6-files-audited)

---

## 1. Summary

Phase 2 is well-implemented. The architecture is clean, the Vulkan usage is correct,
the SPSC ring works properly, and the integration hooks into the existing Voodoo code
are placed correctly. No critical bugs that block Phase 3. Two important issues need
fixing before Phase 5 (push constant batching) and Phase 6 (fogColor byte order).
Several minor issues are noted for cleanup.

**Verdict**: Phase 3 can proceed immediately. Issues found are either latent (not
manifesting until later phases) or cosmetic.

---

## 2. Critical Issues

**None found.**

---

## 3. Important Issues

### 3.1 Per-Triangle Push Constants Not Drawn Per-Triangle (LATENT)

**Severity**: Important (Phase 5 blocker, not Phase 2/3)
**Files**: `src/video/videocommon/vc_thread.c` lines 517-549, 555-572

**Problem**: The batch accumulates triangles in the vertex buffer and sets push
constants per-triangle via `vkCmdPushConstants`, but only issues a single
`vkCmdDraw` for the entire batch (at swap time or batch-full). Since push constants
are command-buffer state, the draw call uses whatever push constants were LAST set.
All triangles in the batch share the final triangle's push constants.

**Current impact**: None in Phase 2. The fragment shader (`voodoo_uber.frag` line 102:
`vec4 src = vColor`) outputs only the interpolated vertex color. The vertex shader
uses `pc.fb_width`/`pc.fb_height` which are constant across all triangles in a frame.
Push constants are effectively unused for pixel output in Phase 2.

**Future impact**: In Phase 5+, push constants drive alpha test, blend mode, depth
function, fog mode, etc. Different triangles within a frame will have different
fbzMode/alphaMode values. Batching them into a single draw call with the last
triangle's push constants will produce incorrect rendering.

**Recommended fix (before Phase 5)**:
Option A (simplest): Issue `vkCmdDraw(3 verts)` per triangle, immediately after
`vkCmdPushConstants`. Defeats batching but is correct. Can optimize later.

Option B (efficient): Track push constant state. When push constants change between
triangles, flush the current batch with `vkCmdDraw`, reset, then start a new batch.
This batches same-state runs.

Option C (most efficient): Store per-triangle state in a storage buffer indexed by
`gl_VertexIndex / 3`. Requires descriptor set changes.

**For Phase 3**: No action needed. Phase 3 adds display output, not pipeline features.

---

### 3.2 fogColor Byte Order Mismatch (LATENT)

**Severity**: Important (Phase 6 bug when fog is implemented)
**File**: `src/video/vid_voodoo_vk.c` lines 170-172

**Problem**: fogColor is packed as `0x00BBGGRR`:
```c
pc->fogColor = (uint32_t) p->fogColor.r
             | ((uint32_t) p->fogColor.g << 8)
             | ((uint32_t) p->fogColor.b << 16);
```

But the shader's `unpackRGB()` (`voodoo_uber.frag` lines 71-77) expects `0x00RRGGBB`:
```glsl
vec3 unpackRGB(uint c) {
    return vec3(
        float((c >> 16) & 0xFFu) / 255.0,  // expects R at bits 16-23
        float((c >>  8) & 0xFFu) / 255.0,  // expects G at bits 8-15
        float( c        & 0xFFu) / 255.0   // expects B at bits 0-7
    );
}
```

With the current packing, `unpackRGB(fogColor)` returns `(B, G, R)` -- red and blue
are swapped.

**Root cause**: `rgbvoodoo_t` has fields `{b, g, r, pad}` (struct at
`src/include/86box/vid_voodoo_common.h` line 45-50). The code reads `.r` into the
low byte and `.b` into bit 16, but the shader expects R at bit 16 and B at bit 0.

**Current impact**: None -- fog is not implemented until Phase 6.

**Recommended fix**:
```c
pc->fogColor = (uint32_t) p->fogColor.b
             | ((uint32_t) p->fogColor.g << 8)
             | ((uint32_t) p->fogColor.r << 16);
```

This produces `0x00RRGGBB` matching the shader's `unpackRGB` convention.

---

## 4. Minor Issues

### 4.1 Unused CompileShader.cmake

**Severity**: Minor (dead code)
**File**: `src/video/videocommon/cmake/CompileShader.cmake`

The `compile_shader()` function is defined BOTH inline in the main `CMakeLists.txt`
(lines 39-67) and in this separate file. The separate file is never `include()`d.
Remove the dead file or refactor to use it.

### 4.2 Ring Free Space Calculation Has Redundant Slack

**Severity**: Minor (benign, wastes ~16 bytes per push)
**File**: `src/video/videocommon/vc_thread.c` lines 288-290

```c
uint32_t needed = total_size + (uint32_t) sizeof(vc_ring_cmd_header_t);
if (needed < (uint32_t) total_size + VC_RING_ALIGN)
    needed = (uint32_t) total_size + VC_RING_ALIGN;
```

This reserves an extra header's worth (8 bytes) or alignment's worth (16 bytes)
beyond the actual command size. The intent is to ensure space for a potential
wraparound sentinel. However, the wraparound sentinel is an 8-byte header at most,
and it's written BEFORE the actual command (at the old write_pos, then write_pos
resets to 0). The extra reservation is conservative but wastes ring capacity.

With 8 MB ring and 304 bytes per triangle (288 + 16 padding), this is negligible --
~27K triangles still fit. Not worth fixing.

### 4.3 No Logging for gpu_renderer Config Value

**Severity**: Minor (debugging convenience)
**File**: `src/video/vid_voodoo.c` lines 1327-1329

```c
voodoo->use_gpu_renderer = device_get_config_int("gpu_renderer");
vc_voodoo_init(voodoo);
```

No log message confirms the config value was read. When debugging startup failures,
it would be helpful to log whether gpu_renderer=1 was actually set. The
`vc_voodoo_init` function does log "GPU renderer requested", which partially covers
this, but logging the raw config value before the init attempt would help distinguish
"config not set" from "init failed".

### 4.4 ENABLE_VOODOO_VK_LOG Not Wired in CMake

**Severity**: Minor (no debug logging in bridge unless manually defined)
**File**: `src/video/vid_voodoo_vk.c` lines 53-69

The `voodoo_vk_log()` function is gated by `ENABLE_VOODOO_VK_LOG`, but this define
is not set anywhere in CMake. The `ENABLE_VIDEOCOMMON_LOG` define is set by the
videocommon CMakeLists.txt, but the voodoo target has its own logging defines. To
enable VK bridge logging, either add a CMake option or piggyback on an existing
Voodoo debug define.

### 4.5 ctx->render_data Not Atomic

**Severity**: Minor (no actual race in current code)
**File**: `src/video/videocommon/vc_thread.c` line 677

```c
ctx->render_data = gpu_st;
```

`render_data` is a plain `void*` pointer, set by the GPU thread and stored in
`vc_ctx_t`. Currently it's only read by the GPU thread itself, so there's no actual
data race. However, if Phase 3 or later needs the main thread to access GPU state
(e.g., for display info queries), this would need to be atomic or protected.

For now, not a problem.

### 4.6 Missing VK_IMAGE_USAGE_TRANSFER_SRC_BIT on Depth Image

**Severity**: Minor (Phase 7 consideration)
**File**: `src/video/videocommon/vc_render_pass.c` line 239

The depth image only has `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT`. If Phase 7
(LFB readback) needs to read back depth values, the image will need
`VK_IMAGE_USAGE_TRANSFER_SRC_BIT` added. This can be done when Phase 7 is
implemented.

---

## 5. Phase 3 Readiness Assessment

### 5.1 Color Image Usage Flags -- READY

**File**: `src/video/videocommon/vc_render_pass.c` lines 212-214

```c
color_ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
               | VK_IMAGE_USAGE_SAMPLED_BIT
               | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
```

All three flags needed for Phase 3 are present:
- `COLOR_ATTACHMENT_BIT`: render target (already used)
- `SAMPLED_BIT`: sampling in post-process blit shader (Phase 3)
- `TRANSFER_SRC_BIT`: vkCmdBlitImage if using blit-based display path (Phase 3)

### 5.2 Front Buffer Accessibility -- READY

The front buffer index is `1 - gpu_st->rp.back_index`. After `vc_render_pass_swap()`,
the just-rendered buffer becomes the front. The color image, color image view, width,
and height are all available through `gpu_st->rp.fb[front_index]`.

Phase 3's post-process shader needs:
- `VkImageView` of front color image: `gpu_st->rp.fb[front_idx].color_view`
- Sampler: needs to be created (Phase 3 work)
- Descriptor set binding: needs descriptor set layout in pipeline layout (Phase 3 work)

### 5.3 Image Layout Transition Capability -- READY

After the offscreen render pass completes, the color image is in
`COLOR_ATTACHMENT_OPTIMAL`. Phase 3 needs to transition it to
`SHADER_READ_OPTIMAL` for the post-process blit shader, then back.

This requires a pipeline barrier between the offscreen render pass end and the
post-process render pass begin. Since the GPU thread owns the command buffer, this
is straightforward: insert `vkCmdPipelineBarrier` with image layout transition
between `vkCmdEndRenderPass` (offscreen) and `vkCmdBeginRenderPass` (post-process).

### 5.4 Frame Synchronization Model -- COMPATIBLE

Triple-buffered frame resources (command pool + command buffer + fence per frame).
Phase 3 adds present semaphores and swapchain acquire/present flow, but the existing
fence-per-frame model is compatible. The per-frame fence can be used as the submit
fence, and semaphores can be added for swapchain synchronization.

### 5.5 Swapchain Integration Point -- CLEAR

Phase 3 swapchain/present code integrates into `vc_gpu_handle_swap()`:

1. After `vkCmdDraw` flushes remaining triangles
2. After `vkCmdEndRenderPass` ends the offscreen render pass
3. Transition front color image to `SHADER_READ_OPTIMAL`
4. Acquire swapchain image
5. Begin post-process render pass on swapchain image
6. Draw fullscreen triangle sampling the front color image
7. End post-process render pass
8. Submit with semaphores
9. `vkQueuePresentKHR`

This can be added to `vc_gpu_handle_swap()` or extracted into a new
`vc_display_present()` function called from `vc_gpu_handle_swap()`.

### 5.6 Surface Handle Delivery -- NEEDS IMPLEMENTATION

Phase 3 needs a way for the Qt VCRenderer to pass a `VkSurfaceKHR` handle to the
GPU thread. Per DESIGN.md section 8.1, this is done via an atomic in `vc_ctx_t`:

```c
_Atomic(VkSurfaceKHR) surface;
```

VCRenderer creates the surface, stores it atomically. GPU thread polls it at the top
of each loop iteration (or at swap time). When non-null, GPU thread creates the
swapchain.

This field needs to be added to `vc_ctx_t`. Currently not present in `vc_internal.h`.

### 5.7 Display Callback Skip -- NEEDS IMPLEMENTATION (Phase 3 task)

Per PHASES.md section "Phase 3: Display", the display callback in
`vid_voodoo_display.c` needs to skip the per-scanline pixel output and `svga_doblit`
trigger when `use_gpu_renderer` is active. The swap/retrace/swap_pending code between
these blocks must remain unchanged. This is a Phase 3 implementation task, not a
Phase 2 issue.

### 5.8 VCRenderer -- NEEDS IMPLEMENTATION (Phase 3 task)

The Qt `VCRenderer` class (`qt_vcrenderer.cpp`) needs to be created. Per DESIGN.md
and LESSONS.md, it should be ~300 lines: surface creation, atomic handoff, teardown
wait. No Vulkan drawing code.

---

## 6. Files Audited

### Headers

| File | Lines | Status |
|------|-------|--------|
| `src/video/videocommon/vc_core.h` | 25 | Clean |
| `src/include/86box/videocommon.h` | 62 | Clean |
| `src/video/videocommon/vc_internal.h` | 198 | Clean |
| `src/video/videocommon/vc_gpu_state.h` | 42 | Clean |
| `src/video/videocommon/vc_render_pass.h` | 40 | Clean |
| `src/video/videocommon/vc_batch.h` | 51 | Clean |
| `src/video/videocommon/vc_pipeline.h` | 111 | Clean |
| `src/video/videocommon/vc_shader.h` | 34 | Clean |
| `src/video/videocommon/vc_thread.h` | 59 | Clean |

### Source

| File | Lines | Status |
|------|-------|--------|
| `src/video/videocommon/vc_core.c` | 583 | Clean |
| `src/video/videocommon/vc_thread.c` | 769 | Issue 3.1 (latent) |
| `src/video/videocommon/vc_render_pass.c` | 346 | Issue 4.6 (minor) |
| `src/video/videocommon/vc_batch.c` | 105 | Clean |
| `src/video/videocommon/vc_pipeline.c` | 337 | Clean |
| `src/video/videocommon/vc_shader.c` | 106 | Clean |
| `src/video/videocommon/vc_vma_impl.cpp` | 163 | Clean |
| `src/video/vid_voodoo_vk.c` | 227 | Issue 3.2 (latent) |

### Shaders

| File | Lines | Status |
|------|-------|--------|
| `shaders/voodoo_uber.vert` | 101 | Clean |
| `shaders/voodoo_uber.frag` | 127 | Clean |

### CMake

| File | Lines | Status |
|------|-------|--------|
| `src/video/videocommon/CMakeLists.txt` | 143 | Issue 4.1 (dead cmake) |
| `src/video/CMakeLists.txt` (relevant section) | 23 | Clean |
| `cmake/SpvToHeader.cmake` | 64 | Clean |

### Integration Points

| File | Lines | Status |
|------|-------|--------|
| `src/video/vid_voodoo_render.c` (USE_VIDEOCOMMON guard) | 7 | Clean |
| `src/video/vid_voodoo_reg.c` (USE_VIDEOCOMMON guards) | 12 | Clean |
| `src/video/vid_voodoo.c` (init/close hooks) | 12 | Clean |
| `src/include/86box/vid_voodoo_common.h` (fields + externs) | 4 | Clean |

---

## Detailed Verification Notes

### Vulkan Object Lifecycle -- VERIFIED CORRECT

All Vulkan objects have balanced create/destroy pairs:

| Object | Created | Destroyed |
|--------|---------|-----------|
| VkInstance | `vc_init()` line 271 | `vc_destroy()` line 520 |
| VkDevice | `vc_init()` line 464 | `vc_destroy()` line 515 |
| VmaAllocator | `vc_init()` line 480 | `vc_destroy()` line 510 |
| VkRenderPass (x2) | `vc_render_pass_create()` | `vc_render_pass_destroy()` |
| VkImage (x4) | `vc_create_single_fb()` | `vc_destroy_single_fb()` |
| VkImageView (x4) | `vc_create_single_fb()` | `vc_destroy_single_fb()` |
| VkFramebuffer (x2) | `vc_create_single_fb()` | `vc_destroy_single_fb()` |
| VkBuffer (vertex) | `vc_batch_create()` | `vc_batch_destroy()` |
| VkShaderModule (x2) | `vc_shaders_create()` | `vc_shaders_destroy()` |
| VkPipelineLayout | `vc_pipeline_create()` | `vc_pipeline_destroy()` |
| VkPipelineCache | `vc_pipeline_create()` | `vc_pipeline_destroy()` |
| VkPipeline | `vc_pipeline_create()` | `vc_pipeline_destroy()` |
| VkCommandPool (x3) | `vc_frame_resources_create()` | `vc_frame_resources_destroy()` |
| VkFence (x3) | `vc_frame_resources_create()` | `vc_frame_resources_destroy()` |

Partial creation failure cleanup: `vc_gpu_thread_init()` (lines 624-634) destroys
all sub-modules on failure. Each sub-module's destroy function checks for
`VK_NULL_HANDLE` before calling vkDestroy*. Correct.

`vkDeviceWaitIdle` is called in both `vc_gpu_thread_cleanup()` (line 651) and
`vc_destroy()` (line 507) before any resource destruction. Correct.

### SPSC Ring Buffer -- VERIFIED CORRECT

- Single producer: only `voodoo_vk_push_triangle()` and `voodoo_vk_push_swap()` call
  `vc_ring_push()` / `vc_ring_push_and_wake()`. Both are called from the FIFO thread.
  `vc_stop_gpu_thread()` also pushes SHUTDOWN, called from the main thread, but only
  after the FIFO thread has stopped (voodoo close sequence).

- Single consumer: only `vc_gpu_thread_func()` reads from the ring.

- Memory ordering: write_pos uses `memory_order_release` on producer,
  `memory_order_acquire` on consumer. read_pos uses `memory_order_release` on consumer,
  `memory_order_acquire` on producer (in `vc_ring_free_space`). Correct for ARM64
  weak memory model.

- Wraparound: `VC_CMD_WRAPAROUND` resets read_pos to 0 with `memory_order_release`.
  Consumer `continue`s immediately after reset, skipping the normal advance. Correct.

- Free space: `VC_RING_SIZE - (wp - rp) - 1` when `wp >= rp`, `rp - wp - 1` when
  `rp > wp`. The `-1` prevents full-ring ambiguity (full vs empty). Correct.

- Wake mechanism: DuckStation-style atomic counter + semaphore. `wake()` increments
  by 2, checks if old value was negative (sleeping). `sleep()` decrements by 1,
  checks if old value was positive (work pending). ABA-safe because the counter
  tracks net wake signals. Correct.

### Push Constant Layout -- VERIFIED CORRECT

C struct `vc_push_constants_t` (`vc_pipeline.h` lines 68-85):
64 bytes, 16 fields, all uint32/float. std430 packing with no padding (all scalars).

GLSL `PushConstants` block in both shaders matches field-for-field:
- Same types (uint/float)
- Same offsets (0, 4, 8, 12, ..., 56, 60)
- Same total size (64 bytes)

`_Static_assert(sizeof(vc_push_constants_t) == 64)` confirms C-side size. Correct.

### Vertex Format -- VERIFIED CORRECT

C struct `vc_vertex_t` (`vc_pipeline.h` lines 40-49):
72 bytes per vertex, 7 active attributes + 3 floats padding.

Pipeline vertex input (`vc_pipeline.c` lines 40-92) matches:
- location 0: vec2 at offset 0 (x, y) -- `VK_FORMAT_R32G32_SFLOAT`
- location 1: float at offset 8 (z) -- `VK_FORMAT_R32_SFLOAT`
- location 2: float at offset 12 (w) -- `VK_FORMAT_R32_SFLOAT`
- location 3: vec4 at offset 16 (r,g,b,a) -- `VK_FORMAT_R32G32B32A32_SFLOAT`
- location 4: vec3 at offset 32 (s0,t0,w0) -- `VK_FORMAT_R32G32B32_SFLOAT`
- location 5: vec3 at offset 44 (s1,t1,w1) -- `VK_FORMAT_R32G32B32_SFLOAT`
- location 6: float at offset 56 (fog) -- `VK_FORMAT_R32_SFLOAT`

Stride = 72 bytes = sizeof(vc_vertex_t). Offsets verified via `offsetof()`.

Shader vertex inputs match locations and types. Correct.

`_Static_assert(sizeof(vc_vertex_t) == 72)` confirms C-side size. Correct.

### Render Pass Compatibility -- VERIFIED CORRECT

Both render passes (clear and load) are created with identical attachment descriptions
(format, samples, storeOp) -- they differ only in loadOp and initialLayout. Per
Vulkan spec section 7.2, framebuffers are compatible with any render pass that has
matching attachment count, format, and sample count. Both passes are compatible with
the same framebuffers. Correct.

### Frame Synchronization -- VERIFIED CORRECT

- Fences created with `VK_FENCE_CREATE_SIGNALED_BIT` (line 371): first-frame
  `vkWaitForFences` succeeds immediately. Correct.
- `vkWaitForFences` + `vkResetFences` before reusing command buffer (lines 416-418).
- `vkQueueSubmit` with fence (line 501), `f->submitted = 1`.
- Next use of same frame index waits on fence (line 416). Correct.
- Triple-buffered (VC_NUM_FRAMES=3): allows up to 2 frames in flight. Adequate.

### Integration Hooks -- VERIFIED CORRECT

**vid_voodoo_render.c** (line 1852-1857):
```c
#ifdef USE_VIDEOCOMMON
    if (voodoo->use_gpu_renderer) {
        voodoo_vk_push_triangle(voodoo, params);
        return;
    }
#endif
```
Clean early-return. SW render threads never see VK-mode triangles. Correct.

**vid_voodoo_reg.c** (lines 127-130, 172-175):
`voodoo_vk_push_swap(voodoo)` is called AFTER the existing swap_count/swap_pending
logic completes. This is correct per v2 design: the VK swap command is a separate
concern from the Voodoo swap lifecycle. The Voodoo display callback handles
swap_count/swap_pending independently.

Two call sites:
1. Banshee path (line 129): after leftOverlayBuf swap processing
2. Non-Banshee path (line 174): after standard swap processing

Both are correct -- they cover the two branches of SST_swapbufferCMD handling.

**vid_voodoo.c** (lines 1327-1329, 1477-1479):
`vc_voodoo_init(voodoo)` called at the end of both `voodoo_card_init()` and
`voodoo_2d3d_card_init()`. Correct -- covers both Voodoo 1/2 and Banshee/V3.

**vid_voodoo.c** (line 1637-1638):
`vc_voodoo_close(voodoo)` called in `voodoo_card_close()`. Note: this is called
BEFORE `free(voodoo)` (line 1643) and AFTER `free(voodoo->fb_mem)` etc. The VK
close path pushes SHUTDOWN and joins the GPU thread, which needs `voodoo->vc_ctx`
to be valid. Since `vc_voodoo_close` sets `voodoo->vc_ctx = NULL`, this is clean.

### Vertex Reconstruction -- VERIFIED CORRECT

`voodoo_vk_extract_vertices()` (`vid_voodoo_vk.c` lines 84-150):

- Position: 12.4 fixed-point to float via `/16.0f`. Correct.
- Color A: 12.12 fixed-point to [0,1] float via `/(4096.0f * 255.0f)`. Correct.
  This matches the SW renderer's `startR >> 12` (12-bit integer) / 255.
- Color B,C: reconstructed from A + gradient * delta. Uses float arithmetic on
  the Voodoo gradients (dRdX, dRdY, etc.). The gradients are int32_t in 12.12
  format. The reconstruction `rA + (dRdX * dx_ba + dRdY * dy_ba) / VC_COLOR_SCALE`
  is mathematically equivalent to evaluating the gradient at vertex B's position.
  This matches the Voodoo hardware model where gradients are defined relative to
  vertex A.
- Depth/W: dummy values (0.5, 1.0) for Phase 2. Phase 5 will extract real depth.
- Texture coords: zeroed. Phase 4 will extract real texture coords.

### Shader Math -- VERIFIED CORRECT (Phase 2 scope)

**Vertex shader** (`voodoo_uber.vert`):
- NDC conversion: `(2 * pos / fb_dim) - 1`. Correct for [0, fb_dim] -> [-1, +1].
- Y-down: Vulkan default matches Voodoo. No flip needed. Correct.
- gl_Position encoding: `vec4(ndc_x * W, ndc_y * W, depth * W, W)`. This is the
  standard "pre-multiplied W" encoding for perspective-correct interpolation. With
  W=1.0 (Phase 2), this simplifies to `vec4(ndc_x, ndc_y, depth, 1.0)`. Correct.
- noperspective varyings: color, depth, fog are `noperspective` (screen-space linear
  interpolation). Matches Voodoo Gouraud shading. Correct.
- smooth varyings: texture coords are `smooth` (perspective-corrected). Will be
  correct when W carries real perspective info (Phase 4+). Correct.

**Fragment shader** (`voodoo_uber.frag`):
- Phase 2: `fragColor = vColor`. Direct pass-through of interpolated color. Correct.
- unpackColor/unpackRGB helpers: `0xAARRGGBB` -> `vec4(R,G,B,A)`. Correct for
  color0/color1 which are stored as ARGB8888 in the Voodoo registers.
- Note: fogColor is packed differently (see Issue 3.2).

### VMA Implementation -- VERIFIED CORRECT

`vc_vma_impl.cpp`:
- `VMA_STATIC_VULKAN_FUNCTIONS=0`, `VMA_DYNAMIC_VULKAN_FUNCTIONS=0`: disables VMA's
  own function loading, forcing it to use the explicitly provided function pointers.
  Correct for volk usage.
- All required VMA function pointers are wired to volk's loaded symbols. Correct.
- `VMA_MEMORY_USAGE_AUTO`: VMA chooses optimal memory type. Correct.
- `VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | MAPPED_BIT`: for the
  persistently mapped vertex buffer. Correct for host-visible, coherent memory.

### CMake Build -- VERIFIED CORRECT

- Shader compilation: `glslc --target-env=vulkan1.2 -O -Werror`. Correct.
- SPIR-V embedding: `SpvToHeader.cmake` converts binary to `uint32_t[]` array with
  little-endian byte swapping. Correct for SPIR-V format.
- Generated headers in `${CMAKE_CURRENT_BINARY_DIR}/generated/`. Include path added.
- `VK_NO_PROTOTYPES` set on both videocommon and voodoo targets. Correct for volk.
- Platform surface defines set correctly per platform. Correct.

---

## Conclusion

Phase 2 is clean and well-structured. The two important issues (push constant batching
and fogColor byte order) are latent -- they will cause problems in Phase 5 and Phase 6
respectively, but do not affect Phase 2 or Phase 3. Phase 3 can proceed immediately.

The code follows the v2 design principles:
- **swap_count/swap_pending untouched** (Lessons P1, Bug 5 prevention)
- **Single producer / single consumer** (Lessons P2)
- **Ring-only communication** (Lessons P3, P4)
- **Proper OS primitives** (Lessons P5, no spin-loops)
- **Clean integration** (Lessons P7, SW path unchanged)

Phase 3 readiness is confirmed:
- Color images have SAMPLED_BIT for post-process blit
- Front buffer is accessible via `rp.fb[1 - back_index]`
- Frame sync model is compatible with adding present flow
- Integration points for swapchain code are clear
- VCRenderer surface handoff needs `_Atomic(VkSurfaceKHR)` in vc_ctx_t (Phase 3 task)
