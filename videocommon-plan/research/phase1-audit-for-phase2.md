# Phase 1 Audit for Phase 2 Compatibility

**Auditor**: vc-debug agent
**Date**: 2026-03-01
**Branch**: videocommon-voodoo
**Scope**: Verify Phase 1 code is ready for Phase 2 (Basic Rendering) without structural rework.

**Research inputs**:
- `videocommon-plan/research/phase2-implementation.md`
- `videocommon-plan/research/phase2-shader-design.md`

---

## 1. Ring Command Compatibility

### 1.1 Command Types -- PASS

All required Phase 2 command types are defined in `vc_internal.h` lines 59-69:

```c
VC_CMD_TRIANGLE      = 0   // Phase 2: triangle with vertices + push constants
VC_CMD_SWAP          = 1   // Phase 2: swap/present signal
VC_CMD_TEXTURE_UPLOAD = 2  // Phase 4
VC_CMD_TEXTURE_BIND  = 3   // Phase 4
VC_CMD_STATE_UPDATE  = 4   // Phase 5
VC_CMD_CLEAR         = 5   // Phase 5
VC_CMD_LFB_WRITE     = 6   // Phase 7
VC_CMD_SHUTDOWN      = 7   // Phase 1 (existing)
VC_CMD_WRAPAROUND    = 8   // Phase 1 (existing, ring internal)
```

Phase 2 needs VC_CMD_TRIANGLE and VC_CMD_SWAP. Both are defined.

### 1.2 Ring Header Size Field -- PASS

Header defined at `vc_internal.h` lines 75-82:

```c
typedef struct vc_ring_cmd_header_t {
    uint16_t type;
    uint16_t size;
    uint32_t reserved;
} vc_ring_cmd_header_t;
```

Phase 2 triangle command = 288 bytes (8 header + 64 push constants + 216 vertices).
uint16_t max = 65535. 288 fits with massive headroom. **PASS**.

### 1.3 Ring Push API for 288-byte Commands -- PASS

`vc_ring_push()` at `vc_thread.c` line 297 accepts `uint16_t total_size`.

Alignment: `vc_ring_align(288) = 288` (already 16-byte aligned).
Space reservation: `needed = max(288 + 8, 288 + 16) = 304` bytes.
Write position advance: `(wp + 288) & VC_RING_MASK` -- correct.
Payload pointer: `(void *)(hdr + 1)` = 280 bytes of payload (64 push + 216 verts). **Correct**.

The ring holds `8 MB / 288 = ~29,127` triangle commands. At 10K triangles/frame at 60 fps,
this is ~2.9 frames of headroom. Adequate for Phase 2.

### 1.4 Wraparound Sentinel Size Truncation -- WARN (latent)

`vc_thread.c` line 322:
```c
wrap->size = (uint16_t) (VC_RING_SIZE - wp);
```

`VC_RING_SIZE` = 8,388,608. This value cannot fit in uint16_t. The cast silently
truncates, writing garbage into the size field.

**Impact**: Currently NONE. The consumer's wraparound handler (`vc_thread.c` line 396-399)
jumps to position 0 without reading the size field. The `continue` bypasses the size-based
advance at lines 409-413.

**Risk**: If anyone adds code that reads the sentinel's size field (e.g., for logging or
debugging), it will produce wrong values.

**Recommendation**: Either remove the size assignment entirely (since it is never read), or
change the sentinel's size field to uint32_t. Low priority -- no functional impact.

### 1.5 Consumer Size-Zero Infinite Loop Guard -- WARN (theoretical)

`vc_thread.c` lines 410-411:
```c
uint16_t aligned_size = vc_ring_align(hdr->size);
uint32_t new_rp = (rp + aligned_size) & VC_RING_MASK;
```

If `hdr->size == 0` (malformed command), `aligned_size = 0`, `new_rp = rp`, and the
consumer spins forever on the same command. No producer currently generates size-0 commands.

**Recommendation**: Add a defensive check: `if (aligned_size == 0) { /* skip min-align or
break */ }`. Low priority -- theoretical only.

---

## 2. vc_ctx_t Extensibility

### 2.1 Struct Layout -- PASS

`vc_ctx_t` at `vc_internal.h` lines 113-131:

```c
typedef struct vc_ctx_t {
    VkInstance       instance;
    VkPhysicalDevice physical_device;
    VkDevice         device;
    VkQueue          queue;
    uint32_t         queue_family;
    void            *allocator;   // VmaAllocator
    vc_ring_t        ring;
    void            *gpu_thread;
    _Atomic(int)     running;
    vc_caps_t        caps;
    char             device_name[256];
    uint32_t         api_version;
} vc_ctx_t;
```

Phase 2 needs to add: VkRenderPass (x2), VkFramebuffer, VkImage + VkImageView (color + depth),
VkPipeline, VkPipelineLayout, VkPipelineCache, VkShaderModule (x2), VkCommandPool,
VkCommandBuffer, VkFence, and frame resource arrays.

**Extensibility**: The struct is a plain C struct allocated via `malloc`. Adding fields
requires only appending new members at the end. No packing pragmas, no bitfields, no
inheritance. The struct is allocated once in `vc_init()` and freed in `vc_destroy()`.
Adding ~20 new VkHandle fields is trivial.

**Concern**: The struct mixes producer-side state (ring, running) with consumer-side Vulkan
handles. For cache line efficiency, Phase 2 could group GPU-thread-only fields together.
Not a correctness issue.

**Verdict**: PASS. No structural changes needed.

### 2.2 VMA Allocator Accessibility -- PASS

VMA is stored as `void *allocator` at line 120. Phase 2 code in new files (e.g.,
`vc_render_pass.c`, `vc_pipeline.c`) can access it through the `vc_ctx_t` pointer.
The VMA header is only needed in the file that calls VMA functions -- the opaque `void *`
pattern avoids forcing C++ on all files.

For Phase 2, VMA calls (vmaCreateImage, vmaCreateBuffer) should go in `vc_vma_impl.cpp`
as new `extern "C"` wrappers, or in a new C++ file. **No structural issue**.

---

## 3. GPU Thread Loop Structure

### 3.1 Command Dispatch -- PASS

`vc_thread.c` lines 390-406:

```c
switch (hdr->type) {
    case VC_CMD_SHUTDOWN: ...
    case VC_CMD_WRAPAROUND: ...
    default:
        VC_LOG("... skipping cmd %d ...\n", hdr->type, hdr->size);
        break;
}
```

Phase 2 adds `case VC_CMD_TRIANGLE:` and `case VC_CMD_SWAP:` to this switch. The `default`
case currently logs-and-skips unknown commands, which is forward-compatible. Adding cases
requires no restructuring.

### 3.2 Read Position Advance -- PASS

`vc_thread.c` lines 409-413:

```c
if (hdr->type != VC_CMD_WRAPAROUND) {
    uint16_t aligned_size = vc_ring_align(hdr->size);
    uint32_t new_rp = (rp + aligned_size) & VC_RING_MASK;
    atomic_store_explicit(&ring->read_pos, new_rp, memory_order_release);
}
```

The consumer advances `read_pos` by the aligned command size after processing. This works
for any command size (288 bytes for triangle, smaller for swap/shutdown). **Correct**.

### 3.3 Ring Drain Behavior -- PASS

The loop at lines 377-414 checks `rp == wp` at the top. If `rp != wp`, it processes one
command and loops. Multiple commands are drained without re-sleeping. Only when the ring is
empty does it call `vc_ring_sleep()`. This is the correct pattern for Phase 2's burst
workload (many triangles followed by a swap).

### 3.4 Payload Access Pattern -- PASS

For `VC_CMD_TRIANGLE`, the GPU thread will read the payload at `(hdr + 1)`. Since the
producer writes payload data before publishing `write_pos` (via `memory_order_release`),
and the consumer reads `write_pos` with `memory_order_acquire`, the payload is guaranteed
visible. ARM64 memory model is correctly handled by these barriers. **Verified**.

---

## 4. Vulkan Init Completeness

### 4.1 Instance, Device, Queue, VMA -- PASS

`vc_core.c` creates all four:
- VkInstance (line 271) with Vulkan 1.2
- VkPhysicalDevice (lines 298-313) with graphics queue requirement
- VkDevice (line 374) with extensions
- VkQueue (line 384) for graphics
- VMA allocator (line 390)

All are stored in `vc_ctx_t` and accessible to Phase 2 code.

### 4.2 Queue Family Graphics Capability -- PASS

`vc_find_graphics_queue()` at lines 123-146 requires `VK_QUEUE_GRAPHICS_BIT`. On all target
platforms, the graphics queue also supports transfer operations implicitly. **Sufficient for
Phase 2**.

### 4.3 VMA Configuration -- PASS

`vc_vma_impl.cpp` creates VMA with:
- `vulkanApiVersion = VK_API_VERSION_1_2`
- All required volk-loaded function pointers (lines 40-63)
- Bind memory 2, memory properties 2, buffer/image memory requirements 2 (Vulkan 1.1+ KHR)

VMA can create VkImages (color, depth) and VkBuffers (vertex, staging) with
`VMA_MEMORY_USAGE_AUTO`. **Ready for Phase 2**.

### 4.4 Missing vkDeviceWaitIdle Before Destroy -- ISSUE (MODERATE)

`vc_destroy()` at `vc_core.c` lines 409-431 destroys VMA and device without calling
`vkDeviceWaitIdle()` first. In Phase 1 this is fine (no GPU work submitted). In Phase 2,
if the GPU thread has submitted commands to the queue, destroying the device without waiting
could cause validation errors or crashes.

**Fix required**: Add `vkDeviceWaitIdle(ctx->device)` as the first line of `vc_destroy()`.
Note: `vc_stop_gpu_thread()` is called before `vc_destroy()` in `vc_voodoo_close()`, and
the GPU thread should drain its work before exiting. But `vkDeviceWaitIdle` is still needed
as a safety net for in-flight GPU commands.

**Where**: `vc_core.c` line 411, before the VMA destroy.

### 4.5 EDS Feature Enablement Missing -- ISSUE (MODERATE, Phase 5)

`vc_core.c` lines 351-358 enable EDS extensions, but the corresponding feature structs
(`VkPhysicalDeviceExtendedDynamicStateFeaturesEXT` etc.) are NOT chained via `pNext` on
the `VkDeviceCreateInfo`. Without the feature struct, the extension is loaded but its
functions cannot be called.

Per Vulkan spec: "If an extension is enumerated as supported and enabled, but its features
are not requested via the pNext chain, the extension's features are NOT available."

**Phase 2 impact**: None. Phase 2 uses only viewport/scissor as dynamic state (core Vulkan,
no extension needed).

**Phase 5 impact**: CRITICAL. Dynamic depth test/write/compare requires EDS features
enabled via pNext chain. Must be fixed before Phase 5.

**Fix required (recommended now)**: Chain `VkPhysicalDeviceExtendedDynamicStateFeaturesEXT`,
`VkPhysicalDeviceExtendedDynamicState2FeaturesEXT`, and
`VkPhysicalDeviceExtendedDynamicState3FeaturesEXT` via pNext when the corresponding
extensions are available.

**Note**: When using pNext feature structs, `device_ci.pEnabledFeatures` must be NULL and
the core features must be provided via `VkPhysicalDeviceFeatures2` in the pNext chain.
This is a non-trivial refactor but well-documented.

### 4.6 depthClamp Feature Not Enabled -- WARN (Phase 5)

The Voodoo has no depth clamping, and Phase 2 disables depth test entirely. However,
future phases may need `depthClampEnable = VK_TRUE` in the rasterization state if Voodoo
Z values can exceed [0,1] before the shader writes `gl_FragDepth`. If depth clamping is
ever needed, `VkPhysicalDeviceFeatures::depthClamp` must be enabled at device creation.

**Phase 2 impact**: None.

---

## 5. Voodoo Integration Points

### 5.1 Init Wiring -- PASS

`vid_voodoo.c` calls `vc_voodoo_init()` in two init paths:
- Line 1329: `voodoo_card_init()` (Voodoo 1 / Voodoo 2)
- Line 1479: `voodoo_2d3d_card_init()` (Banshee / Voodoo 3 via `vid_voodoo_banshee.c:3495`)

Both paths are guarded by `#ifdef USE_VIDEOCOMMON` and read `device_get_config_int("gpu_renderer")`.

### 5.2 Close Wiring -- PASS

`vid_voodoo.c` line 1638: `vc_voodoo_close()` in `voodoo_card_close()`.
This function is called for both Voodoo 1/2 (via `voodoo_close()`, line 1652-1653) and
Banshee/3 (via `vid_voodoo_banshee.c:3796`). Both code paths correctly tear down VC.

### 5.3 voodoo_t Fields -- PASS

`vid_voodoo_common.h` lines 764-765:

```c
void *vc_ctx;           /* vc_ctx_t* -- opaque to avoid Vulkan header deps. */
int   use_gpu_renderer; /* 1 = VK path, 0 = SW fallback. */
```

Both fields are accessible from any file that includes `vid_voodoo_common.h`. Phase 2 hooks
in `vid_voodoo_render.c` and `vid_voodoo_reg.c` can check `voodoo->use_gpu_renderer` and
access `voodoo->vc_ctx`.

### 5.4 Phase 2 Hook Point -- PASS (design confirmed)

The Phase 2 intercept point is in the triangle dispatch path. Per DESIGN.md section 11.1 and
the phase2-implementation.md research, the hook goes in `voodoo_queue_triangle()` or the
code that calls the SW render threads. The `voodoo_params_t` struct (lines 124-242) contains
all fields needed for vertex reconstruction: vertex positions, color start/gradients, and
pipeline state registers. No structural changes needed.

---

## 6. CMake Readiness

### 6.1 Adding New Source Files -- PASS

`CMakeLists.txt` uses `add_library(videocommon OBJECT ...)`. Adding new Phase 2 files
(e.g., `vc_render_pass.c`, `vc_pipeline.c`, `vc_shader.c`, `vc_batch.c`) requires only
appending to the source list. No structural changes needed.

### 6.2 Shader Compilation Infrastructure -- NOT YET (expected)

No shader compilation exists:
- No `find_program(glslc)` call
- No `compile_shader()` function
- No `shaders/` directory
- No `cmake/SpvToHeader.cmake`
- No `generated/` include path

This is expected per the Phase 2 plan. The v1 research provides a complete blueprint for the
SPIR-V compilation pipeline (`archive/v1/research/cmake-integration.md` Section 4). Phase 2
implementation will add these.

### 6.3 glslc Availability -- PASS

`glslc` found at `/usr/local/bin/glslc` on the development machine. The Phase 2
CMakeLists.txt should use `find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin /usr/bin
/usr/local/bin)`.

### 6.4 Include Paths -- PASS

Current include paths in CMakeLists.txt:
- `extra/volk` -- volk header
- `extra/VMA` -- VMA header
- `src/include` -- 86Box headers
- `${CMAKE_CURRENT_SOURCE_DIR}` -- vc_internal.h etc.

Phase 2 will need to add `${CMAKE_CURRENT_BINARY_DIR}/generated` for SPIR-V header files.
This is a one-line addition.

---

## 7. Platform Semaphore / Wake Mechanism

### 7.1 Wake Pattern for Frequent Triangle Pushes -- PASS

Phase 2 use pattern: FIFO thread pushes many VC_CMD_TRIANGLE commands (one per triangle),
then pushes VC_CMD_SWAP and wakes. The research recommends NOT waking per-triangle (too
much overhead) but only at swap or after N triangles.

The existing API supports both patterns:
- `vc_ring_push()` -- push without wake (for batch triangle pushes)
- `vc_ring_push_and_wake()` -- push + wake (for swap command)
- `vc_ring_wake()` -- standalone wake (for periodic wake during long batches)

**Backpressure wake**: `vc_ring_wait_for_space()` calls `vc_ring_wake()` when the ring is
full, ensuring the GPU thread processes commands to free space. This handles the case where
the producer fills the ring faster than the consumer drains it.

**Verdict**: The wake mechanism is well-suited for Phase 2. The FIFO thread should use
`vc_ring_push()` for triangles (no wake) and `vc_ring_push_and_wake()` for swap.

### 7.2 Platform Implementations -- PASS

All three platform semaphore implementations are correct:
- **macOS**: `dispatch_semaphore_t` (lines 85-109) -- battle-tested on Apple Silicon
- **Windows**: `CreateSemaphoreW` / `WaitForSingleObject` (lines 57-81)
- **POSIX**: `sem_t` heap-allocated (lines 113-144) -- works on Linux, FreeBSD, Pi 5

---

## 8. Summary

### PASS (no changes needed for Phase 2)

| Check | Status | Notes |
|-------|--------|-------|
| Ring command types (TRIANGLE, SWAP) | PASS | Defined in vc_internal.h |
| Ring header uint16 for 288 bytes | PASS | Max 65535, 288 fits easily |
| Ring push handles 288-byte commands | PASS | Alignment, space check correct |
| vc_ctx_t extensibility | PASS | Plain C struct, append fields |
| GPU thread loop extensible | PASS | Switch statement, add cases |
| GPU thread drains ring correctly | PASS | Processes all commands before sleep |
| Vulkan instance/device/queue/VMA | PASS | All created, accessible |
| Queue family graphics support | PASS | Required by device selection |
| VMA for images and buffers | PASS | Configured with volk functions |
| Voodoo init wiring (both paths) | PASS | card_init + 2d3d_card_init |
| Voodoo close wiring (both paths) | PASS | card_close called by both |
| voodoo_t vc_ctx/use_gpu_renderer | PASS | Accessible from all vid files |
| CMake add_library extensible | PASS | Append source files |
| glslc available | PASS | /usr/local/bin/glslc |
| Platform semaphore correctness | PASS | macOS/Win32/POSIX all verified |
| Wake pattern fits Phase 2 | PASS | Push without wake + batch wake |

### ISSUES (fix before or during Phase 2)

| Issue | Severity | File:Line | Description |
|-------|----------|-----------|-------------|
| No vkDeviceWaitIdle in vc_destroy | MODERATE | vc_core.c:411 | Must wait for GPU before destroying device. Phase 2 submits GPU work. |
| EDS features not enabled via pNext | MODERATE | vc_core.c:365-372 | Extensions loaded but features not requested. Blocks Phase 5 (dynamic depth). |
| Wraparound sentinel size truncation | LOW | vc_thread.c:322 | uint16_t truncates VC_RING_SIZE-wp (up to 8MB). Size field never read, but latent. |

### WARNINGS (not blocking, address opportunistically)

| Warning | File:Line | Description |
|---------|-----------|-------------|
| Consumer infinite loop on size=0 | vc_thread.c:410 | Malformed command with size=0 causes spin. Theoretical only. |
| depthClamp not enabled | vc_core.c:360-363 | May be needed in Phase 5+ for Voodoo Z edge cases. |
| Shader compilation infrastructure | CMakeLists.txt | Expected -- Phase 2 adds glslc, compile_shader(), shaders/ dir. |

### Phase 2 Implementation Readiness: GO

The Phase 1 infrastructure is well-designed and ready for Phase 2. The MODERATE issues
(vkDeviceWaitIdle and EDS feature enablement) are straightforward fixes that can be done
at the start of Phase 2 implementation. No structural rework is needed.

**Recommended order for Phase 2 implementation**:
1. Fix vkDeviceWaitIdle in vc_destroy (1 line)
2. Add SPIR-V compilation infrastructure to CMakeLists.txt
3. Create shaders/ directory with minimal vertex + fragment shaders
4. Add vc_render_pass.c (offscreen framebuffer, two render passes)
5. Add vc_pipeline.c (single pipeline, full vertex format, push constants)
6. Add vc_shader.c (SPIR-V loading from embedded headers)
7. Add vc_batch.c (vertex buffer management, draw submission)
8. Extend GPU thread loop: VC_CMD_TRIANGLE and VC_CMD_SWAP handlers
9. Add vid_voodoo_vk.c (vertex reconstruction, ring push)
10. Wire intercept point in triangle dispatch path
