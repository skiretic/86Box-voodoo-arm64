# VideoCommon v2 -- Architecture Design Document

**Date**: 2026-03-01
**Branch**: videocommon-voodoo
**Status**: Design complete, ready for implementation
**Audience**: Coding agents (vc-lead, vc-shader, vc-plumbing, vc-debug)

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Core Architectural Insight](#2-core-architectural-insight)
3. [Threading Model](#3-threading-model)
4. [SPSC Ring Buffer](#4-spsc-ring-buffer)
5. [Swap/Sync Lifecycle](#5-swapsync-lifecycle)
6. [GPU Thread Architecture](#6-gpu-thread-architecture)
7. [Vulkan Pipeline](#7-vulkan-pipeline)
8. [Display Integration](#8-display-integration)
9. [LFB Access](#9-lfb-access)
10. [MoltenVK Considerations](#10-moltenvk-considerations)
11. [Integration Surface](#11-integration-surface)
12. [Error Handling and Fallback](#12-error-handling-and-fallback)

---

## 1. Executive Summary

VideoCommon is a GPU-accelerated rendering infrastructure for 86Box that offloads
emulated GPU pixel pipelines onto the host GPU using Vulkan 1.2. The first target is
the 3dfx Voodoo family (V1, V2, Banshee, V3).

**v1 failed** because it tried to replace the Voodoo display/swap lifecycle with a
Vulkan-native mechanism. This led to swap_count deadlocks, present channel crashes,
swapchain thrashing, and FIFO stalls. Five distinct bugs were found and fixed during
v1 development, but the root cause was architectural: the design fought the existing
Voodoo timing system instead of working with it.

**v2 succeeds by respecting the existing Voodoo code.** The VK path replaces ONLY the
software rasterizer. Everything else -- swap_count, swap_pending, retrace timing,
buffer rotation, FIFO threading, display callback -- stays exactly as-is. This maps
to the universal pattern found in Dolphin, PCSX2, and DuckStation: one GPU-owner
thread, swap as a ring command, throttle the producer not the consumer.

### What VideoCommon Does

1. Receives triangle data from the FIFO thread (instead of dispatching to SW render threads)
2. Pushes triangle data + state into a lock-free SPSC ring buffer
3. A dedicated GPU thread consumes the ring, records Vulkan command buffers, and submits
4. On swap, the GPU thread presents the rendered frame via the swapchain
5. The display callback skips scanout (GPU thread handles display) but retains ALL
   swap/retrace timing logic

### What VideoCommon Does NOT Do

- Modify swap_count, swap_pending, or retrace_count
- Replace or bypass the display callback
- Create new synchronization between FIFO thread and display callback
- Touch the CMDFIFO or FIFO ring processing
- Change SST_status register behavior

---

## 2. Core Architectural Insight

**The existing Voodoo display callback already handles swap pacing correctly.**

The swap lifecycle in the existing code is a well-designed state machine:

```
CPU thread:   swap_count++  (immediate, on guest write to 0x128)
FIFO thread:  swap_pending=1, swap_offset=... (on processing SST_swapbufferCMD)
              [double buffer: blocks in voodoo_wait_for_swap_complete()]
              [triple buffer: continues, waits if previous still pending]
Timer/CPU:    at vblank, if swap_pending && retrace_count > swap_interval:
                front_offset = swap_offset
                swap_count--
                swap_pending = 0
                wake FIFO thread
```

v1 tried to replace this with: "GPU thread decrements swap_count after present."
This broke because:
- The GPU thread had no connection to retrace timing
- swap_count was decremented at the wrong time relative to the guest's polling
- The display callback still ran but with stale state
- FIFO blocking on double-buffer was broken

v2 leaves this ENTIRE mechanism untouched. The VK path changes ONLY:
- What happens when the FIFO thread processes triangle commands (ring push instead of
  SW render thread dispatch)
- What happens when the FIFO thread processes SST_swapbufferCMD (additionally pushes
  VC_CMD_SWAP to ring, so GPU thread knows to present)
- What happens in the display callback scanout section (skips pixel-by-pixel scanout
  from fb_mem, since GPU thread handles display)

---

## 3. Threading Model

### 3.1 Thread Inventory

```
+-------------------+     +-------------------+     +-------------------+
|   CPU Thread      |     |   FIFO Thread     |     |   GPU Thread      |
|   (emulation)     |     |   (per card)      |     |   (per card)      |
|                   |     |                   |     |                   |
|  PCI register     |     |  Drain FIFO ring  |     |  Own VkDevice     |
|  writes/reads     |     |  Drain CMDFIFO    |     |  Own VkQueue      |
|  swap_count++     |     |  voodoo_reg_writel|     |  Record cmd bufs  |
|  SST_status poll  |     |  Triangle setup   |     |  vkQueueSubmit    |
|  Timer dispatch   |     |  Push to SPSC ring|     |  vkQueuePresent   |
|  (display callback|     |  swap_pending=1   |     |  Swapchain mgmt   |
|   runs here)      |     |  Block on d-buf   |     |  Texture uploads  |
+-------------------+     +-------------------+     +-------------------+
        |                         |                         ^
        |  PCI MMIO               |    SPSC Ring Buffer     |
        +-------->FIFO ring------>+========>================+
        |                         |   (lock-free, 8 MB)
        |  timer callback         |
        +--retrace timing-------->+  (wake_fifo_thread event)
```

### 3.2 Thread Roles

| Thread | Existing Role | v2 Change |
|--------|--------------|-----------|
| **CPU thread** | PCI writes, SST_status reads, timer dispatch (display callback) | NONE |
| **FIFO thread** | Drain FIFO/CMDFIFO, call voodoo_reg_writel, dispatch to render threads | Instead of dispatching to SW render threads, pushes triangle data to SPSC ring. Still handles swap_pending, buffer rotation, blocking. |
| **SW render threads (1, 2, or 4)** | Rasterize triangles from params buffer | NOT USED when VK path active. Still exist for fallback. |
| **GPU thread** (NEW) | N/A | Consumes SPSC ring, records Vulkan commands, submits, presents. Sole owner of all Vulkan objects. |

### 3.3 Data Flow

```
Guest writes triangle registers
  -> CPU thread enqueues to FIFO ring (or CMDFIFO)
    -> FIFO thread dequeues, calls voodoo_reg_writel()
      -> Triangle setup runs (unchanged)
      -> Instead of waking render thread:
         voodoo_vk_push_triangle(voodoo, &voodoo->params)
           -> Converts params to vertices + push constants
           -> Pushes VC_CMD_TRIANGLE to SPSC ring
             -> GPU thread consumes, records vkCmdDraw

Guest writes SST_swapbufferCMD
  -> CPU thread: swap_count++ (unchanged)
  -> CPU thread: enqueues to FIFO ring / writes to CMDFIFO (unchanged)
    -> FIFO thread dequeues, calls voodoo_reg_writel(SST_swapbufferCMD)
      -> Buffer rotation (unchanged)
      -> swap_pending = 1 (unchanged, for vsync path)
      -> NEW: pushes VC_CMD_SWAP to SPSC ring
      -> Double buffer: blocks in voodoo_wait_for_swap_complete() (unchanged)
        -> GPU thread processes VC_CMD_SWAP, presents frame
        -> Display callback eventually clears swap_pending (unchanged)
        -> FIFO thread unblocks (unchanged)
```

### 3.4 What the GPU Thread Does NOT Do

- Does NOT modify swap_count (display callback does this)
- Does NOT modify swap_pending (display callback does this)
- Does NOT interact with the FIFO ring or CMDFIFO
- Does NOT read SST_status or any Voodoo registers
- Does NOT communicate with the display callback
- Does NOT do frame pacing (the existing retrace system handles this)

---

## 4. SPSC Ring Buffer

### 4.1 Design

The SPSC (Single-Producer Single-Consumer) ring buffer connects the FIFO thread
(producer) to the GPU thread (consumer). It is modeled after DuckStation's 16 MB
command FIFO.

```c
#define VC_RING_SIZE       (8 * 1024 * 1024)   /* 8 MB */
#define VC_RING_MASK       (VC_RING_SIZE - 1)
#define VC_RING_ALIGN      16                    /* command alignment */

typedef struct vc_ring_t {
    uint8_t             *buffer;                 /* ring memory, VC_RING_SIZE bytes */
    _Atomic(uint32_t)    write_pos;              /* producer write position */
    _Atomic(uint32_t)    read_pos;               /* consumer read position */
    _Atomic(int32_t)     wake_counter;           /* DuckStation-style wake mechanism */
    thread_sem_t        *wake_sem;               /* semaphore for sleeping GPU thread */
} vc_ring_t;
```

### 4.2 Command Format

Variable-size commands, 16-byte aligned:

```c
typedef struct vc_ring_cmd_header_t {
    uint16_t    type;       /* vc_ring_cmd_type_e */
    uint16_t    size;       /* total command size in bytes (including header) */
    uint32_t    reserved;   /* padding to 8 bytes */
} vc_ring_cmd_header_t;
```

### 4.3 Command Types

| Command | Payload | Description |
|---------|---------|-------------|
| `VC_CMD_TRIANGLE` | 3 vertices (pos, color, tex coords) + push constant data | One triangle to rasterize |
| `VC_CMD_SWAP` | swap info (buffer index, frame number) | End of frame, trigger present |
| `VC_CMD_TEXTURE_UPLOAD` | TMU index, slot, dimensions, format, pixel data pointer | Upload texture to VkImage |
| `VC_CMD_TEXTURE_BIND` | TMU index, slot, sampler params | Bind texture for subsequent draws |
| `VC_CMD_STATE_UPDATE` | fbzMode, alphaMode, fogMode, etc. | Pipeline state that changed since last triangle |
| `VC_CMD_CLEAR` | color, depth, which buffers | Fastfill / clear operation |
| `VC_CMD_LFB_WRITE` | offset, dimensions, pixel data | LFB write from shadow buffer |
| `VC_CMD_SHUTDOWN` | (none) | Terminate GPU thread |
| `VC_CMD_WRAPAROUND` | (none) | Sentinel: read_pos wraps to 0 |

### 4.4 Push API (Producer -- FIFO Thread)

Two variants, matching DuckStation:

```c
/* Fire-and-forget (most common, for triangles) */
void vc_ring_push(vc_ring_t *ring, const void *cmd, uint32_t size);

/* Push and wake GPU thread (for swap, shutdown) */
void vc_ring_push_and_wake(vc_ring_t *ring, const void *cmd, uint32_t size);
```

Note: DuckStation has a third variant `PushCommandAndSync()` for blocking
readback. VideoCommon does NOT use this pattern because sync readback from
the CPU thread would violate the SPSC single-producer invariant (the FIFO
thread is the sole ring producer). Instead, LFB reads use a shadow buffer
maintained by the GPU thread -- see section 9.1.

### 4.5 Wake Mechanism

DuckStation-style lock-free wake using atomic counter + semaphore:

```c
void vc_ring_wake(vc_ring_t *ring) {
    /* Increment by 2; if was sleeping (negative), post semaphore */
    int32_t old = atomic_fetch_add(&ring->wake_counter, 2);
    if (old < 0)
        thread_post_sem(ring->wake_sem);
}

bool vc_ring_sleep(vc_ring_t *ring) {
    int32_t old = atomic_fetch_sub(&ring->wake_counter, 1);
    if (old > 0)
        return true;   /* more work pending, don't sleep */
    /* old == 0: transition to sleeping (-1) */
    thread_wait_sem(ring->wake_sem);
    return true;       /* woken up */
}
```

### 4.6 Backpressure (Ring Full)

When the ring is full (write_pos catches read_pos from behind), the FIFO thread
spin-waits with yield:

```c
void vc_ring_wait_for_space(vc_ring_t *ring, uint32_t needed) {
    while (vc_ring_free_space(ring) < needed) {
        vc_ring_wake(ring);         /* ensure GPU thread is awake */
        thread_yield();             /* yield CPU */
    }
}
```

With an 8 MB ring and typical triangle commands at ~128 bytes, the ring holds
~65K commands. At 60fps with ~10K triangles/frame, this is ~6 frames of headroom.
Ring-full stalls should be extremely rare in practice.

### 4.7 Wraparound Handling

When insufficient contiguous space exists at the end of the ring buffer, the
producer writes a `VC_CMD_WRAPAROUND` sentinel and wraps write_pos to 0. The
consumer, upon reading a wraparound command, resets read_pos to 0.

---

## 5. Swap/Sync Lifecycle

**This is the most critical section of the document. Read it carefully.**

### 5.1 Invariant

**VideoCommon does NOT modify swap_count, swap_pending, retrace_count, or the
display callback. These are managed by the existing Voodoo code, unchanged.**

### 5.2 Immediate Swap (val & 1 == 0)

Step-by-step flow when the guest writes SST_swapbufferCMD with bit 0 = 0:

```
1. CPU thread (voodoo_writel):
   - swap_count++ (under swap_mutex)                    [UNCHANGED]
   - cmd_written++                                      [UNCHANGED]
   - Enqueue to FIFO ring (or CMDFIFO write)            [UNCHANGED]

2. FIFO thread (voodoo_reg_writel):
   - Rotate disp_buffer/draw_buffer                     [UNCHANGED]
   - voodoo_recalc()                                    [UNCHANGED]
   - Since val & 1 == 0:
     - dirty_line = all 1s                              [UNCHANGED]
     - front_offset = params.front_offset               [UNCHANGED]
     - swap_count-- (under swap_mutex)                  [UNCHANGED]
   - NEW: push VC_CMD_SWAP to SPSC ring
   - cmd_read++                                         [UNCHANGED]

3. GPU thread (vc_cmd_swap handler):
   - Flush current batch (submit any pending draws)
   - Present frame to swapchain (vkQueuePresentKHR)
   - Advance to next frame resources (command buffer, fence)
```

For immediate swap, swap_count is incremented on the CPU thread (step 1) and
decremented on the FIFO thread (step 2) without any vsync wait between them.
The GPU thread just needs to present.

### 5.3 VSync Swap, Double Buffer (val & 1 == 1, not TRIPLE_BUFFER)

```
1. CPU thread (voodoo_writel):
   - swap_count++ (under swap_mutex)                    [UNCHANGED]
   - cmd_written++                                      [UNCHANGED]
   - Enqueue to FIFO ring (or CMDFIFO write)            [UNCHANGED]

2. FIFO thread (voodoo_reg_writel):
   - Rotate disp_buffer/draw_buffer                     [UNCHANGED]
   - voodoo_recalc()                                    [UNCHANGED]
   - Since val & 1 == 1 and not TRIPLE_BUFFER:
     - swap_interval = (val >> 1) & 0xff                [UNCHANGED]
     - swap_offset = params.front_offset                [UNCHANGED]
     - swap_pending = 1                                 [UNCHANGED]
     - NEW: push VC_CMD_SWAP to SPSC ring
     - voodoo_wait_for_swap_complete()  <-- BLOCKS      [UNCHANGED]

3. GPU thread (vc_cmd_swap handler):
   - Flush current batch
   - Present frame

4. Timer/display callback (voodoo_callback at line == v_disp):
   - retrace_count++                                    [UNCHANGED]
   - If swap_pending && retrace_count > swap_interval:  [UNCHANGED]
     - front_offset = swap_offset                       [UNCHANGED]
     - swap_count--                                     [UNCHANGED]
     - swap_pending = 0                                 [UNCHANGED]
     - wake_fifo_thread (unblocks step 2)               [UNCHANGED]
     - frame_count++                                    [UNCHANGED]

5. FIFO thread unblocks, continues processing
```

The FIFO thread blocks at step 2 until the display callback fires at vblank.
This is EXACTLY the existing double-buffer throttling. The GPU thread presents
independently -- it does not participate in the blocking.

### 5.4 VSync Swap, Triple Buffer (val & 1 == 1, TRIPLE_BUFFER)

```
1. CPU thread: swap_count++                              [UNCHANGED]

2. FIFO thread:
   - If previous swap_pending: wait for it first         [UNCHANGED]
   - swap_pending = 1                                    [UNCHANGED]
   - NEW: push VC_CMD_SWAP to SPSC ring
   - Does NOT block (triple buffer is non-blocking)      [UNCHANGED]

3. GPU thread: present frame

4. Display callback: clears swap_pending at vblank       [UNCHANGED]
```

Triple buffer allows the FIFO thread to continue immediately. Up to 2 frames
can be in flight (swap_count can reach 2 before the guest stalls). This is
the existing behavior, unchanged.

### 5.5 Emergency Swap (Unchanged)

The emergency swap in `voodoo_wait_for_swap_complete()` fires when:
- `voodoo->flush` is set (CPU called voodoo_flush), OR
- FIFO ring is full

This forces swap_pending = 0 and swap_count-- without waiting for vblank,
preventing deadlocks. VideoCommon does NOT modify this path.

### 5.6 Display Callback in VK Mode

The display callback (`voodoo_callback`) continues to fire every scanline.
In VK mode, its behavior changes ONLY in the scanout section:

| Section | SW Mode | VK Mode |
|---------|---------|---------|
| Scanline pixel output (read from fb_mem, write to monitor buffer) | Active | **SKIPPED** (GPU thread handles display) |
| Swap completion (swap_pending check, swap_count--, wake FIFO) | Active | **UNCHANGED** |
| Retrace timing (retrace_count++, v_retrace flag) | Active | **UNCHANGED** |
| svga_doblit trigger | Active | **SKIPPED** (GPU presents directly) |

The skip requires two insertion points, because the per-scanline pixel drawing
and the svga_doblit trigger are in separate conditional blocks within
`voodoo_callback()`. Both are gated by `FBIINIT0_VGA_PASS`, so the simplest
approach is to add the `use_gpu_renderer` check to both VGA_PASS blocks:

```c
/* Insertion point 1: per-scanline pixel drawing (line < v_disp) */
if ((voodoo->fbiInit0 & FBIINIT0_VGA_PASS) && !voodoo->use_gpu_renderer) {
    if (voodoo->line < voodoo->v_disp) {
        /* ... existing pixel scanout code ... */
    }
}

/* ... swap completion code (always runs, not inside VGA_PASS block) ... */

/* Insertion point 2: svga_doblit trigger (line == v_disp after increment) */
if ((voodoo->fbiInit0 & FBIINIT0_VGA_PASS) && !voodoo->use_gpu_renderer) {
    if (voodoo->line == voodoo->v_disp) {
        svga_doblit(...);
    }
}
```

Note: a single `goto skip_scanout` before the first block would NOT also skip
svga_doblit, which is in a separate block after `skip_draw:` and `line++`.

### 5.7 Why This Works

The key insight is separation of concerns:

- **Frame pacing** is handled by the existing retrace system (swap_count,
  swap_pending, retrace_count, display callback). This is guest-visible
  behavior that MUST match the hardware.

- **Rendering** is handled by the GPU thread (triangle rasterization,
  texture sampling, blending). This is an implementation detail invisible
  to the guest.

- **Display** is handled by the GPU thread (present to swapchain). This
  replaces the scanout-from-fb_mem path but does not affect timing.

v1 conflated frame pacing with rendering/display. v2 keeps them separate.

---

## 6. GPU Thread Architecture

### 6.1 Ownership

The GPU thread is the sole owner of ALL Vulkan objects:

```
Owned exclusively by GPU thread:
  VkInstance, VkDevice, VkQueue
  VkSwapchainKHR (create, present, destroy)
  VkRenderPass, VkFramebuffer (offscreen targets)
  VkPipeline, VkPipelineCache, VkPipelineLayout
  VkShaderModule (uber-shader)
  VkDescriptorPool, VkDescriptorSetLayout, VkDescriptorSet
  VkImage, VkImageView (textures, framebuffers)
  VkBuffer (vertex, staging, uniform)
  VkCommandPool, VkCommandBuffer (per-frame)
  VkFence, VkSemaphore (per-frame sync)
  VmaAllocator (memory allocator)
```

No other thread touches any Vulkan object. The FIFO thread communicates
exclusively through the SPSC ring. The GUI thread communicates exclusively
through an atomic surface handle (see section 8).

### 6.2 Frame Resources

Triple-buffered command recording (matches PCSX2/DuckStation pattern):

```c
#define VC_NUM_FRAMES   3

typedef struct vc_frame_resources_t {
    VkCommandPool       cmd_pool;
    VkCommandBuffer     cmd_buf;
    VkFence             fence;
    uint64_t            fence_value;        /* monotonic counter */
    VkSemaphore         image_available_sem; /* signaled by vkAcquireNextImageKHR */
    VkSemaphore         render_finished_sem; /* waited on by vkQueuePresentKHR */
    VkBuffer            vertex_buffer;      /* per-frame, persistently mapped */
    void               *vertex_data;        /* mapped pointer */
    uint32_t            vertex_offset;      /* current write position */
    VkDescriptorPool    desc_pool;          /* per-frame, reset at frame start */
} vc_frame_resources_t;
```

Fence counter tracking for deferred resource destruction:

```c
uint64_t submitted_fence_value;     /* last submitted */
uint64_t completed_fence_value;     /* last known completed (polled) */
```

### 6.3 Main Loop

```
gpu_thread_main(vc_ctx):
  init_vulkan()
  create_offscreen_framebuffer()
  create_pipeline()

  frame_idx = 0
  while (running):
    /* Check for new work */
    read_pos = ring->read_pos (acquire)
    write_pos = ring->write_pos (acquire)

    if (read_pos == write_pos):
      if (!vc_ring_sleep(ring)):
        continue
      read_pos = ring->read_pos (acquire)
      write_pos = ring->write_pos (acquire)

    /* Begin frame if not already recording */
    if (!recording):
      wait_for_fence(frames[frame_idx])     /* ensure previous use is done */
      reset_command_pool(frames[frame_idx])
      begin_command_buffer(frames[frame_idx])
      begin_render_pass(offscreen_fb)
      recording = true

    /* Process ring commands */
    while (read_pos != write_pos):
      cmd = ring->buffer[read_pos]
      switch (cmd.type):

        case VC_CMD_TRIANGLE:
          write_vertices(frames[frame_idx], cmd.vertices)
          bind_pipeline(cmd.state)
          push_constants(cmd.push_data)
          vkCmdDraw(3)
          break

        case VC_CMD_STATE_UPDATE:
          update_dynamic_state(cmd.state)
          break

        case VC_CMD_TEXTURE_UPLOAD:
          /* End render pass if active (layout transitions) */
          upload_texture(cmd.tmu, cmd.slot, cmd.pixels)
          /* Will re-begin render pass on next draw */
          break

        case VC_CMD_TEXTURE_BIND:
          bind_descriptor_set(cmd.tmu, cmd.slot)
          break

        case VC_CMD_SWAP:
          end_render_pass()
          submit_command_buffer()
          present_to_swapchain()
          frame_idx = (frame_idx + 1) % VC_NUM_FRAMES
          recording = false
          break

        case VC_CMD_CLEAR:
          vkCmdClearAttachments(cmd.color, cmd.depth)
          break

        case VC_CMD_LFB_WRITE:
          flush_lfb_writes(cmd.region)
          break

        case VC_CMD_SHUTDOWN:
          running = false
          break

        case VC_CMD_WRAPAROUND:
          read_pos = 0
          continue

      read_pos = (read_pos + cmd.size) & VC_RING_MASK
      ring->read_pos = read_pos (release)

  cleanup_vulkan()
```

### 6.4 Batching

Triangles are batched into a single draw call where possible. A batch break occurs when:
- Pipeline state changes (different blend mode, depth test, etc.)
- Texture binding changes
- A non-triangle command is encountered (swap, clear, texture upload)
- Vertex buffer is full

The vertex buffer is sized per-frame (e.g., 4 MB = ~28K triangles at 48 bytes/vertex
x 3 vertices). When full, the batch is flushed and the buffer wraps.

### 6.5 No Frame Pacing on GPU Thread

The GPU thread runs as fast as the ring has data. It does NOT:
- Wait for vsync (the swapchain's present mode handles this)
- Sleep for frame timing
- Throttle based on swap_count

Frame pacing is entirely handled by the existing FIFO thread blocking
(`voodoo_wait_for_swap_complete`) and the display callback's retrace system.
This matches the universal pattern: throttle the producer, not the consumer.

---

## 7. Vulkan Pipeline

### 7.1 Instance and Device

```
Vulkan 1.2 baseline (minimum for all target platforms)

Required extensions:
  VK_KHR_swapchain
  VK_KHR_dynamic_rendering OR render pass fallback

Optional extensions (used if available):
  VK_EXT_extended_dynamic_state (depth test, stencil, cull)
  VK_EXT_extended_dynamic_state2 (primitive restart, depth bias)
  VK_EXT_extended_dynamic_state3 (blend, color write mask) -- NOT on MoltenVK
  VK_KHR_push_descriptor (avoid descriptor set allocation for simple binds)
  VK_EXT_swapchain_maintenance1 (present fences for safe destroy)

Required features:
  fragmentStoresAndAtomics (for LFB write path if needed)
  independentBlend (Voodoo blends color and alpha differently)

Optional features (with runtime fallback):
  dualSrcBlend -- for ACOLORBEFOREFOG alpha blending. If not supported
    (e.g., Pi 5 stock Mesa 24.3), fall back to VK_BLEND_FACTOR_SRC_COLOR
    for the ACOLORBEFOREFOG case. This is a minor accuracy reduction
    (fog alpha vs pre-fog color alpha) but is visually acceptable for
    most games. Check VkPhysicalDeviceFeatures.dualSrcBlend at device
    creation and store the result in vc_ctx_t.has_dual_src_blend.

Physical device selection:
  Prefer discrete GPU > integrated GPU > CPU (software)
  Require Vulkan 1.2
  Require VK_KHR_swapchain
```

### 7.2 Loader

Volk (dynamic Vulkan loader, no link-time dependency on Vulkan):

```
Third-party deps (vendored in extra/):
  volk/    -- Dynamic Vulkan function loader (single header + source)
  VMA/     -- Vulkan Memory Allocator (single header)
```

volk loads vulkan-1.dll / libvulkan.so / libMoltenVK.dylib at runtime.
If Vulkan is not available, VideoCommon reports failure and the SW fallback is used.

### 7.3 Offscreen Framebuffer

Rendering targets an offscreen VkImage, NOT the swapchain directly. This decouples
render resolution from display resolution and avoids swapchain image format constraints.

```
Offscreen color image:
  Format:    VK_FORMAT_R8G8B8A8_UNORM
  Usage:     COLOR_ATTACHMENT | SAMPLED | TRANSFER_SRC
  Size:      Voodoo native resolution (typically 640x480 or 800x600)
  Tiling:    OPTIMAL
  Samples:   1

Offscreen depth image:
  Format:    VK_FORMAT_D32_SFLOAT (or D24_UNORM_S8_UINT)
  Usage:     DEPTH_STENCIL_ATTACHMENT
  Size:      Same as color
  Tiling:    OPTIMAL

Layout: depth_any (VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL
        or DEPTH_STENCIL_ATTACHMENT_OPTIMAL) for W-buffer and Z-buffer flexibility
```

### 7.4 Render Pass

Simple two-attachment render pass:

```
Attachment 0 (color):
  loadOp:  LOAD (preserve previous frame until swap clears it)
  storeOp: STORE
  initial: COLOR_ATTACHMENT_OPTIMAL
  final:   COLOR_ATTACHMENT_OPTIMAL

Attachment 1 (depth):
  loadOp:  LOAD
  storeOp: STORE
  initial: DEPTH_STENCIL_ATTACHMENT_OPTIMAL
  final:   DEPTH_STENCIL_ATTACHMENT_OPTIMAL
```

**First-frame initialization**: Newly created VkImages start in
`VK_IMAGE_LAYOUT_UNDEFINED`. Using `loadOp: LOAD` with
`initialLayout: COLOR_ATTACHMENT_OPTIMAL` on an UNDEFINED image violates
VUID-VkAttachmentDescription-format-06699 and causes undefined behavior.

The implementation tracks a `first_frame` boolean per offscreen framebuffer.
On the first frame after creation (or resize), use `loadOp: CLEAR` with
`initialLayout: UNDEFINED` for both color and depth attachments. This is
valid because CLEAR discards previous contents, so UNDEFINED layout is safe.
On subsequent frames, switch to `loadOp: LOAD` with the working layouts
(`COLOR_ATTACHMENT_OPTIMAL`, `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`).

```
First frame (first_frame == true):
  Attachment 0 (color):   loadOp: CLEAR, initialLayout: UNDEFINED
  Attachment 1 (depth):   loadOp: CLEAR, initialLayout: UNDEFINED
  Clear values: color = {0,0,0,0}, depth = 0.0 (or 1.0 per Voodoo convention)
  After this frame, first_frame = false

Subsequent frames:
  Attachment 0 (color):   loadOp: LOAD, initialLayout: COLOR_ATTACHMENT_OPTIMAL
  Attachment 1 (depth):   loadOp: LOAD, initialLayout: DEPTH_STENCIL_ATTACHMENT_OPTIMAL
```

This two-render-pass approach (CLEAR for first frame, LOAD thereafter) is
the standard pattern used by Dolphin and DuckStation. It requires two
VkRenderPass objects -- one for first frame, one for steady state -- but
both use the same pipeline and framebuffer.

### 7.5 Graphics Pipeline (Uber-Shader)

One SPIR-V vertex shader and one SPIR-V fragment shader, compiled offline from GLSL.
Pipeline state variations are minimized using dynamic state and push constants.

**Vertex shader**: Passes through position, color, texture coordinates, fog.
No transformation (Voodoo operates in screen space with pre-divided coordinates).

**Fragment shader**: Implements the full Voodoo pixel pipeline:
- Texture sampling (TMU0, TMU1, LOD, detail blend, trilinear)
- Color combine (4 modes: zero, c_local, a_other, a_local, etc.)
- Alpha combine
- Fog (table-based, vertex fog, alpha fog)
- Alpha test (8 compare functions)
- Chroma key
- Dither (4x4 and 2x2 Bayer)
- Stipple pattern

**Push constants (64 bytes)**: Encode per-triangle Voodoo register state:
- fbzMode, fogMode, alphaMode, fbzColorPath, textureMode[2]
- Fog color, chroma key, alpha reference
- See `videocommon-plan/research/push-constant-layout.md` for exact layout

**Dynamic state** (to avoid pipeline explosion):
- Viewport and scissor (always dynamic in Vulkan)
- Depth test enable, depth write enable, depth compare op (via VK_EXT_extended_dynamic_state)
- Blend enable, blend factors, blend op (via VK_EXT_extended_dynamic_state3, or baked pipeline variants on MoltenVK)

### 7.6 Pipeline Variants

Where extended_dynamic_state3 is not available (MoltenVK), blend state must be
baked into the pipeline. A pipeline cache maps blend state hashes to VkPipeline
objects. Expected unique combinations: ~20-50 for typical games.

```c
typedef struct vc_pipeline_key_t {
    uint8_t  blend_enable;
    uint8_t  src_rgb_factor;
    uint8_t  dst_rgb_factor;
    uint8_t  src_alpha_factor;
    uint8_t  dst_alpha_factor;
    uint8_t  color_write_mask;
    uint8_t  depth_format;      /* z16 vs w-buffer */
    uint8_t  dual_src_blend;    /* ACOLORBEFOREFOG */
} vc_pipeline_key_t;
```

Pipeline creation is lazy (create on first use) and cached via VkPipelineCache
for fast startup on subsequent runs.

### 7.7 Texture Management

One VkImage per texture cache slot. Voodoo has 2 TMUs x 64 cache entries = 128 slots.

```
Texture upload path:
  1. FIFO thread calls voodoo_use_texture() (unchanged)
  2. voodoo_vk_push_texture() detects texture identity change
  3. Copies decoded BGRA8 pixels (malloc'd buffer)
  4. Pushes VC_CMD_TEXTURE_UPLOAD to ring with pixel data pointer
  5. GPU thread receives, uploads to staging buffer, copies to VkImage
  6. GPU thread frees the pixel data buffer

Texture binding:
  1. FIFO thread pushes VC_CMD_TEXTURE_BIND with slot + sampler params
  2. GPU thread creates/reuses VkSampler, updates descriptor set

Descriptor management:
  Per-frame descriptor pool (reset at frame start via vkResetDescriptorPool)
  Layout: set 0 = {TMU0 sampler, TMU1 sampler, fog table sampler}
```

**Important: VK path must always increment refcount_r[0] to match** (not just on
texture change). The FIFO thread increments `refcount` unconditionally on every
`voodoo_use_texture()` call. The VK path must do the same for `refcount_r[0]` to
keep the eviction check balanced.

### 7.8 Vertex Format

```c
typedef struct vc_vertex_t {
    float x, y, z, w;          /* screen-space position (16 bytes) */
    float r, g, b, a;          /* iterated color (16 bytes) */
    float s0, t0, w0;          /* TMU0 texture coords (12 bytes) */
    float s1, t1, w1;          /* TMU1 texture coords (12 bytes) */
    float fog;                 /* fog coordinate (4 bytes) */
    float pad[3];              /* align to 16 bytes (12 bytes) */
} vc_vertex_t;                 /* total: 72 bytes, or 48 if TMU1 unused */
```

Vertex buffer: per-frame, persistently mapped, linear write (no indexing).
Three vertices per triangle, no index buffer needed (Voodoo draws individual
triangles, not strips or fans).

---

## 8. Display Integration

### 8.1 Surface Lifecycle

```
Qt GUI thread:
  VCRenderer::initialize():
    surface = createVulkanSurface()     /* platform-specific */
    vc_display_set_surface(ctx, surface)

  VCRenderer::finalize():
    vc_display_request_teardown(ctx)    /* sets atomic flag */
    wait for GPU thread to destroy resources
    destroyVulkanSurface(surface)

GPU thread:
  On vc_display_tick():
    if new surface available:
      create swapchain from surface
    if resize requested:
      recreate swapchain
    if teardown requested:
      destroy swapchain + resources
      signal teardown complete
```

### 8.2 Swapchain

```
Swapchain:
  Image count:  3 (triple buffer, avoids MoltenVK drawable exhaustion)
  Present mode: FIFO (vsync) or MAILBOX (if available and preferred)
  Format:       B8G8R8A8_SRGB (or best available)
  Color space:  SRGB_NONLINEAR
  Transform:    CURRENT_TRANSFORM
  Composite:    OPAQUE
```

### 8.3 Post-Processing Blit

The offscreen framebuffer (Voodoo native resolution) is blit to the swapchain
image (window resolution) using a fullscreen quad with a simple fragment shader:

```
Post-process pipeline:
  Vertex shader:  fullscreen triangle (no vertex buffer, SV_VertexID)
  Fragment shader: sample offscreen color image, output to swapchain
  Filter:         Nearest (pixel-perfect scaling) or Bilinear (user preference)
```

This handles resolution scaling (e.g., 640x480 Voodoo output to 1280x960 window).

### 8.4 Present Flow

Uses a single command buffer for both the offscreen render pass and the
post-process blit. This is simpler and more efficient than two separate
submits, and makes synchronization explicit via pipeline barriers rather
than relying on implicit submission ordering.

**Synchronization**: Each frame resource set contains two binary semaphores:
- `image_available_sem` -- signaled by `vkAcquireNextImageKHR`, waited by submit
- `render_finished_sem` -- signaled by submit, waited by `vkQueuePresentKHR`

```
GPU thread processes VC_CMD_SWAP:
  1. Acquire swapchain image (vkAcquireNextImageKHR -> signals image_available_sem)
  2. End offscreen render pass
  3. Pipeline barrier: offscreen image
       srcStage:  COLOR_ATTACHMENT_OUTPUT
       dstStage:  FRAGMENT_SHADER
       srcAccess: COLOR_ATTACHMENT_WRITE
       dstAccess: SHADER_READ
       oldLayout: COLOR_ATTACHMENT_OPTIMAL
       newLayout: SHADER_READ_ONLY_OPTIMAL
  4. Begin post-process render pass (swapchain image)
       The post-process render pass handles BOTH swapchain layout transitions:
       - initialLayout: UNDEFINED (discard old contents -- we write every pixel)
       - finalLayout: PRESENT_SRC_KHR (ready for presentation)
       No explicit barrier is needed for the swapchain image. The render pass
       itself transitions UNDEFINED -> attachment layout on begin and
       attachment layout -> PRESENT_SRC_KHR on end.
       The acquire semaphore wait at COLOR_ATTACHMENT_OUTPUT (step 9) ensures
       the image is available before color writes begin.
  5. Draw fullscreen triangle (samples offscreen image)
  6. End post-process render pass
       (render pass finalLayout transitions swapchain to PRESENT_SRC_KHR)
  7. Pipeline barrier: offscreen image back to COLOR_ATTACHMENT_OPTIMAL
       srcStage:  FRAGMENT_SHADER
       dstStage:  COLOR_ATTACHMENT_OUTPUT
       srcAccess: SHADER_READ
       dstAccess: COLOR_ATTACHMENT_WRITE
       oldLayout: SHADER_READ_ONLY_OPTIMAL
       newLayout: COLOR_ATTACHMENT_OPTIMAL
  8. End recording
  9. vkQueueSubmit:
       waitSemaphore:   image_available_sem
       waitDstStage:    COLOR_ATTACHMENT_OUTPUT
       signalSemaphore: render_finished_sem
       fence:           frame_fence
 10. vkQueuePresentKHR:
       waitSemaphore:   render_finished_sem
 11. Advance frame index
```

**Semaphore edge case on swapchain recreation**: If `vkAcquireNextImageKHR`
succeeds (signaling `image_available_sem`) but `vkQueuePresentKHR` fails with
`VK_ERROR_OUT_OF_DATE_KHR` and the frame's submit is skipped, the
`image_available_sem` remains signaled. A signaled binary semaphore MUST be
consumed before it can be signaled again. Handle this by either: (a) submitting
a dummy command buffer that waits on the semaphore, or (b) destroying and
recreating the semaphore during swapchain recreation. Option (b) is simpler
since swapchain recreation already destroys and recreates frame resources.

### 8.4.1 Image Layout Transitions Summary

All layout transitions in the present flow, consolidated for reference:

```
Offscreen image transitions (per frame):
  [Render pass] -> COLOR_ATTACHMENT_OPTIMAL (via render pass finalLayout)
  COLOR_ATTACHMENT_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL  (barrier, step 3)
  SHADER_READ_ONLY_OPTIMAL -> COLOR_ATTACHMENT_OPTIMAL  (barrier, step 7)

Swapchain image transitions (per frame):
  UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL  (via post-process render pass
    initialLayout: UNDEFINED, step 4)
  COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR  (via post-process render
    pass finalLayout, step 6)

Shadow buffer readback (if LFB read active, at VC_CMD_SWAP):
  COLOR_ATTACHMENT_OPTIMAL -> TRANSFER_SRC_OPTIMAL  (barrier, before copy)
  TRANSFER_SRC_OPTIMAL -> COLOR_ATTACHMENT_OPTIMAL  (barrier, after copy)
  Note: these happen BEFORE the offscreen->SHADER_READ_ONLY transition
```

The offscreen image cycles through three layouts per frame:
`COLOR_ATTACHMENT_OPTIMAL` (rendering) -> `SHADER_READ_ONLY_OPTIMAL`
(post-process sampling) -> `COLOR_ATTACHMENT_OPTIMAL` (ready for next frame).

The swapchain image cycles through:
`UNDEFINED` (after acquire) -> `COLOR_ATTACHMENT_OPTIMAL` (post-process
rendering) -> `PRESENT_SRC_KHR` (presentation).

### 8.5 Display Callback VK-Mode Skip

In `voodoo_callback()`, when `use_gpu_renderer` is set:

```
SKIP:
  - Reading pixels from fb_mem[front_offset]
  - Converting RGB565 to RGB888
  - Writing to monitor->target_buffer
  - svga_doblit() call
  - dirty_line tracking for scanout

KEEP (always runs):
  - retrace_count++ at line == v_disp
  - swap_pending / swap_count management
  - wake_fifo_thread on swap completion
  - v_retrace flag management
  - Timer re-arm
```

The display callback becomes purely a timing mechanism in VK mode, handling
swap lifecycle without doing any pixel work.

---

## 9. LFB Access

### 9.1 LFB Read (Shadow Buffer)

When the guest reads from the Voodoo LFB (linear frame buffer), it expects to
see the current contents of the front or back buffer. With GPU rendering, this
requires reading back from the Vulkan framebuffer.

**Important**: The CPU thread MUST NOT push commands to the SPSC ring. The ring
is single-producer (FIFO thread only), single-consumer (GPU thread). Having the
CPU thread push VC_CMD_READBACK would violate the SPSC invariant, breaking
lock-free correctness on ARM64 (acquire/release pairs assume single producer).

Instead, the GPU thread maintains a **double-buffered shadow buffer** (ping-pong)
that is updated automatically after each frame. The GPU writes to one buffer
while the CPU reads from the other, eliminating data races without locks.

```
Shadow buffer structure:
  shadow_buffer[0]  -- HOST_VISIBLE | HOST_COHERENT VkBuffer (1.2 MB each)
  shadow_buffer[1]  -- HOST_VISIBLE | HOST_COHERENT VkBuffer (1.2 MB each)
  shadow_write_idx  -- which buffer the GPU writes to next (0 or 1)
  shadow_read_idx   -- which buffer the CPU reads from (atomic, set by GPU)
  shadow_frame_idx  -- which per-frame fence guards the readable buffer (atomic)
  shadow_fence_value -- monotonic counter for the readable copy (atomic)

Shadow buffer update (GPU thread, at VC_CMD_SWAP):
  1. End render pass
  2. Transition offscreen image: COLOR_ATTACHMENT_OPTIMAL -> TRANSFER_SRC_OPTIMAL
  3. vkCmdCopyImageToBuffer(offscreen -> shadow_buffer[shadow_write_idx])
  4. Transition offscreen image: TRANSFER_SRC_OPTIMAL -> COLOR_ATTACHMENT_OPTIMAL
  5. Submit command buffer (fence = frames[frame_idx].fence, no stall)
  6. Publish for CPU reading:
       atomic_store(&shadow_read_idx, shadow_write_idx)
       atomic_store(&shadow_frame_idx, frame_idx)
       atomic_store(&shadow_fence_value, submitted_fence_value)
  7. Flip: shadow_write_idx = 1 - shadow_write_idx

LFB read (CPU thread, no ring interaction):
  1. Guest reads LFB address
  2. voodoo_fb_readl() detects VK mode
  3. Wait for per-frame fence (ensures last copy completed)
  4. Read directly from shadow_buffer[shadow_read_idx]
  5. Convert format (RGB565 etc.) and return to guest
```

Each shadow buffer is a `HOST_VISIBLE | HOST_COHERENT` VkBuffer, sized to hold
the full Voodoo framebuffer (e.g., 640x480 * 4 bytes = 1.2 MB). The double
buffering ensures the GPU can write the next frame's copy while the CPU reads
the previous frame's data. Reads between swaps return stale-by-one-frame data,
which is acceptable for nearly all Glide games (the LFB read typically follows
a swap).

For games that require exact same-frame readback (rare), a future enhancement
could add a FIFO-thread-routed sync readback path. But the shadow buffer
approach handles the common case without violating the SPSC invariant.

### 9.2 LFB Read Fence Synchronization

The CPU thread must wait for the shadow buffer copy to complete before reading.
This uses the **per-frame fence** (not a dedicated separate fence). The GPU
thread publishes which frame index and fence value correspond to the readable
shadow buffer. The CPU thread waits on that specific per-frame fence.

```c
/* GPU thread: after vkCmdCopyImageToBuffer in VC_CMD_SWAP handler */
/* (shadow_read_idx, shadow_frame_idx, shadow_fence_value set atomically above) */

/* CPU thread: in voodoo_fb_readl() */
uint32_t fidx = atomic_load(&ctx->shadow_frame_idx);
uint64_t needed = atomic_load(&ctx->shadow_fence_value);
if (ctx->last_waited_fence_value < needed) {
    vkWaitForFences(device, 1, &ctx->frames[fidx].fence, VK_TRUE, TIMEOUT_NS);
    ctx->last_waited_fence_value = needed;
}
uint32_t ridx = atomic_load(&ctx->shadow_read_idx);
/* Safe to read from shadow_buffer[ridx] */
```

**Why per-frame fences are safe here**: `vkWaitForFences` does NOT require
external synchronization on the fence parameter (per Vulkan spec). Only
`vkResetFences` does. The GPU thread resets a per-frame fence at the START
of each frame (in `wait_for_fence` + `vkResetFences`), which only happens
AFTER the fence is signaled. Since the CPU thread only calls `vkWaitForFences`
(never `vkResetFences`), and the GPU thread does not reset the fence until
the NEXT time that frame resource is reused (which requires the fence to
already be signaled), there is no race between the CPU wait and GPU reset.

This avoids any ring interaction from the CPU thread. The only cross-thread
communication is the atomic fence/index values, which are lock-free and safe.

### 9.3 LFB Write

Guest writes to LFB go to a shadow buffer with per-row dirty tracking.
The shadow buffer is flushed to the GPU framebuffer before:
- The next draw call (if dirty rows overlap the draw target)
- A swap command
- A readback request

```
Write path:
  1. Guest writes LFB address (CPU thread)
  2. voodoo_fb_writel() in VK mode: writes to shadow buffer, marks row dirty
  3. Before next VC_CMD_TRIANGLE or VC_CMD_SWAP:
     voodoo_vk_flush_lfb_writes() pushes VC_CMD_LFB_WRITE to ring
  4. GPU thread: copies dirty regions from staging to offscreen image
```

### 9.4 Dirty Tile Tracking

64x64 pixel tiles, bitmask tracking:

```c
#define VC_LFB_TILE_SIZE    64
#define VC_LFB_TILES_X      (1024 / VC_LFB_TILE_SIZE)   /* 16 */
#define VC_LFB_TILES_Y      (1024 / VC_LFB_TILE_SIZE)   /* 16 */

uint32_t dirty_tiles[VC_LFB_TILES_Y];   /* bitmask per row of tiles */
```

Only dirty tiles are transferred, minimizing bus traffic.

---

## 10. MoltenVK Considerations

### 10.1 Surface Creation

Create VkSurfaceKHR from CAMetalLayer (not NSView) to avoid main-thread deadlock.
Qt's QVulkanInstance provides this on macOS, or we extract the CAMetalLayer from
the NSView manually.

Reference: MoltenVK issue #234, fixed in PR #258.

### 10.2 Queue Submission

`MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS=1` (default) is correct for our model.
The GPU thread IS the right thread for synchronous processing. No GCD dispatch needed.

### 10.3 Swapchain Images

Use 3 swapchain images to avoid drawable exhaustion. MoltenVK lazily acquires
CAMetalDrawable, and having all 3 in flight can cause blocking. Triple-buffer
provides adequate overlap.

### 10.4 Extended Dynamic State

MoltenVK does NOT support `VK_EXT_extended_dynamic_state3` (blend state).
Pipeline variants with baked blend state are required on macOS.

MoltenVK DOES support `VK_EXT_extended_dynamic_state` (depth/stencil state).

### 10.5 Fog Table

MoltenVK does not support buffer textures (texelFetch from VkBuffer). Fog table
is uploaded as a 64x1 VK_FORMAT_R32_SFLOAT sampler2D instead.

### 10.6 Configuration Summary

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| `MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS` | 1 (default) | GPU thread is sole owner |
| `MVK_CONFIG_VK_SEMAPHORE_SUPPORT_STYLE` | 1 (default) | Metal events where safe |
| `MVK_CONFIG_SWAPCHAIN_MIN_MAG_FILTER_USE_NEAREST` | 1 (default) | Pixel-perfect on Retina |
| `MVK_CONFIG_USE_COMMAND_POOLING` | 1 (default) | Reduce alloc overhead |
| Swapchain images | 3 | Avoid drawable exhaustion |

---

## 11. Integration Surface

### 11.1 Existing Files Modified

The v2 design minimizes changes to existing Voodoo code. Every modification
is conditional on `voodoo->use_gpu_renderer`.

#### `src/include/86box/vid_voodoo_common.h`

Add to `voodoo_t`:
```c
void *vc_ctx;               /* VideoCommon context (opaque) */
int   use_gpu_renderer;     /* 1 = VK path, 0 = SW path */
```

#### `src/video/vid_voodoo.c`

- In `voodoo_init()`: If config says GPU renderer, call `vc_voodoo_init(voodoo)`
  to create GPU thread and Vulkan context. Set `use_gpu_renderer = 1`.
- In `voodoo_close()`: If `use_gpu_renderer`, call `vc_voodoo_close(voodoo)`
  to push VC_CMD_SHUTDOWN and join GPU thread.
- SST_swapbufferCMD handler: No change (swap_count++ is already correct).

#### `src/video/vid_voodoo_reg.c`

- In `voodoo_reg_writel()` SST_swapbufferCMD handler: After existing processing
  (buffer rotation, swap_pending, etc.), if `use_gpu_renderer`, additionally push
  VC_CMD_SWAP to the SPSC ring.

#### `src/video/vid_voodoo_render.c`

- In the triangle dispatch path: If `use_gpu_renderer`, call
  `voodoo_vk_push_triangle()` instead of dispatching to SW render threads.
  The triangle setup (parameter calculation) still runs on the FIFO thread.

#### `src/video/vid_voodoo_display.c`

- In `voodoo_callback()`: If `use_gpu_renderer`, skip the scanout section
  (pixel read from fb_mem, format conversion, monitor buffer write) and the
  svga_doblit trigger. Both VGA_PASS blocks need the `use_gpu_renderer` check
  (see section 5.6 for details). ALL swap/retrace timing code continues to
  execute unchanged.

### 11.2 New Files

#### `src/video/vid_voodoo_vk.c` (NEW)

Bridge between Voodoo emulation and VideoCommon API:
- `voodoo_vk_push_triangle()`: Extract vertices from `voodoo_params_t`, build
  push constant data from register state, push VC_CMD_TRIANGLE to ring.
- `voodoo_vk_push_texture()`: Detect texture changes, copy decoded pixels,
  push VC_CMD_TEXTURE_UPLOAD/BIND to ring.
- `voodoo_vk_push_swap()`: Push VC_CMD_SWAP to ring.
- `voodoo_vk_flush_lfb_writes()`: Push dirty LFB regions to ring.
- `vc_voodoo_init()` / `vc_voodoo_close()`: Lifecycle management.

This file includes `vid_voodoo_common.h` and `videocommon.h` but NOT any
internal VideoCommon headers (vc_core.h, etc.). It communicates exclusively
through the public VideoCommon API.

#### `src/video/videocommon/` (NEW directory)

Core infrastructure:
- `vc_core.c/h` -- Vulkan instance, device, VMA, logging
- `vc_thread.c/h` -- GPU thread, SPSC ring, frame resources
- `vc_render_pass.c/h` -- Render pass, offscreen framebuffer
- `vc_pipeline.c/h` -- Graphics pipeline, pipeline cache, dynamic state
- `vc_shader.c/h` -- SPIR-V loading, push constants
- `vc_texture.c/h` -- VkImage management, staging upload
- `vc_batch.c/h` -- Triangle batching, vertex buffer
- `vc_readback.c/h` -- LFB readback (sync, async, shadow buffer)
- `vc_display.c/h` -- Swapchain, post-process pipeline, present
- `vc_internal.h` -- Shared internal definitions

Public API:
- `src/include/86box/videocommon.h` -- C11 public header + no-op stubs

#### `src/qt/qt_vcrenderer.cpp/hpp` (NEW)

Qt integration: surface creation only. ~300 lines.

#### `shaders/` (NEW directory)

- `voodoo_uber.vert` / `voodoo_uber.frag` -- Main uber-shader
- `postprocess.vert` / `postprocess.frag` -- Post-process blit
- Pre-compiled SPIR-V checked into repo

### 11.3 CMake Integration

```cmake
option(VIDEOCOMMON "Enable GPU-accelerated rendering (requires Vulkan)" OFF)

if(VIDEOCOMMON)
    add_subdirectory(src/video/videocommon)
    target_link_libraries(86Box PRIVATE videocommon)
    target_compile_definitions(86Box PRIVATE USE_VIDEOCOMMON=1)
endif()
```

Volk and VMA are vendored in `extra/`. No `find_package(Vulkan)` needed.
SPIR-V shaders are compiled at build time via glslc (found via `find_program`).

---

## 12. Error Handling and Fallback

### 12.1 Vulkan Not Available

If volk fails to load the Vulkan loader, or no suitable physical device is found:
- `vc_voodoo_init()` returns failure
- `use_gpu_renderer` stays 0
- SW render threads are used (existing fallback)
- User sees "GPU renderer unavailable" in log

### 12.2 Runtime Vulkan Errors

If a Vulkan call fails during operation (e.g., device lost):
- GPU thread logs the error
- GPU thread pushes itself into an error state (stops processing)
- FIFO thread continues pushing to ring (ring fills up)
- Ring-full backpressure eventually stalls the FIFO thread
- Emergency swap fires, unblocking the FIFO thread
- The emulation continues with visual corruption but does not crash

A future enhancement could hot-switch back to SW rendering on device loss.

### 12.3 Validation

`VC_VALIDATE=1` environment variable enables Vulkan validation layers in any build.
The validation layer callback logs via `pclog_ex()` (VideoCommon log channel).

---

## Appendix A: Platform Targets

| Platform | Vulkan | Loader | Surface | Notes |
|----------|--------|--------|---------|-------|
| macOS | 1.2 | MoltenVK (Homebrew) | CAMetalLayer | No extended_dynamic_state3 |
| Windows | 1.3 | LunarG SDK | HWND (Win32) | Full dynamic state |
| Linux/X11 | 1.3 | Mesa | xcb/xlib | Full dynamic state |
| Linux/Wayland | 1.3 | Mesa | wl_surface | Deferred |
| Raspberry Pi 5 | 1.2 | V3DV (Mesa) | xcb | Limited features |

## Appendix B: v1 vs v2 Comparison

| Aspect | v1 | v2 |
|--------|----|----|
| swap_count management | GPU thread decremented | Display callback decrements (unchanged) |
| Present trigger | Present channel (side-band) | VC_CMD_SWAP in ring |
| Swapchain owner | Shared (GUI + GPU thread) | GPU thread exclusively |
| Display callback | Modified (VK-specific swap logic) | Unchanged (skip scanout only) |
| FIFO thread blocking | Broken (ring backpressure before swap) | Unchanged (existing mechanism) |
| Frame pacing | Custom (atomic queued_frame_count) | Existing (retrace_count + swap_interval) |
| Complexity | ~1700 lines in VCRenderer alone | ~300 lines in VCRenderer |
| Bugs found in testing | 5+ (swap stuck, present crash, drain, thrash, refcount) | N/A (design avoids root causes) |

## Appendix C: Reference Emulator Patterns Used

| Pattern | Source | Application in v2 |
|---------|--------|--------------------|
| Single GPU-owner thread | All (Dolphin, PCSX2, DuckStation) | GPU thread owns all Vulkan objects |
| Swap as ring command | PCSX2, DuckStation | VC_CMD_SWAP in SPSC ring |
| Throttle producer not consumer | All | FIFO thread blocks via existing mechanism |
| Lock-free wake counter + semaphore | DuckStation | SPSC ring wake mechanism |
| Triple-buffered frame resources | PCSX2, DuckStation | 3 command buffer sets with fence counters |
| Variable-size ring commands | DuckStation | VC_CMD with size field |
| Wraparound sentinel | DuckStation | VC_CMD_WRAPAROUND |
| Frame queue backpressure | PCSX2 (VsyncQueueSize) | Existing swap_count mechanism (already built-in!) |

## Appendix D: Key Voodoo State Used by VK Path

| Register | Bits | Used For |
|----------|------|----------|
| fbzMode | depth enable, depth func, depth write, Y origin, stipple, dither, etc. | Dynamic state + push constants |
| fbzColorPath | cc_mselect, cc_rgbselect, cc_aselect, cc_localselect, etc. | Push constants (color combine) |
| alphaMode | alpha_func, alpha_ref, src_afunc, dest_afunc, etc. | Dynamic state (alpha test) + pipeline variant (blend) |
| fogMode | fog_enable, fog_table, fog_mult, fog_add, etc. | Push constants |
| textureMode[0..1] | tc_mselect, tc_reverse, tc_add, etc. | Push constants (texture combine) |
| zaColor | depth_source, depth_bias | Push constants |
| fogColor | fog_r, fog_g, fog_b | Push constants |
| chromaKey | chroma_r, chroma_g, chroma_b | Push constants |
| clipLeftRight | clip_left, clip_right | Scissor rect |
| clipLowYHighY | clip_low, clip_high | Scissor rect |
| stipple | 32-bit pattern | Push constants |
