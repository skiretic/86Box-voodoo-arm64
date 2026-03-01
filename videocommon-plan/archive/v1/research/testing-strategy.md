# VideoCommon Testing Strategy

Comprehensive testing and validation plan for the Vulkan-accelerated Voodoo
rendering path. 86Box has no automated test framework -- all testing is manual.
VideoCommon introduces a Vulkan rendering path alongside the existing software
renderer, and our primary correctness tool is running both simultaneously and
comparing output pixel-by-pixel.

Research date: 2026-02-26

---

## Table of Contents

1. [Dual-Path Validation](#1-dual-path-validation)
2. [Vulkan Validation Layers](#2-vulkan-validation-layers)
3. [Test VM Matrix](#3-test-vm-matrix)
4. [Phase-Gated Testing](#4-phase-gated-testing)
5. [Performance Benchmarking](#5-performance-benchmarking)
6. [Regression Testing Workflow](#6-regression-testing-workflow)
7. [Screenshot Comparison Tool](#7-screenshot-comparison-tool)
8. [Edge Cases](#8-edge-cases)
9. [CI Considerations](#9-ci-considerations)

---

## 1. Dual-Path Validation

### Concept

The software renderer (`voodoo_half_triangle()` in `vid_voodoo_render.c`) is
the ground-truth reference. Every triangle the Vulkan path renders must produce
output that matches the software path to within acceptable precision bounds.

The dual-path approach leverages the existing JIT verify pattern already proven
in the Voodoo ARM64 JIT work: two independent implementations receive the same
input and their outputs are compared.

### Implementation Architecture

```
voodoo_queue_triangle()
    |
    +---> [SW path] params_buffer -> 4 render threads -> fb_mem[] (software framebuffer)
    |
    +---> [VK path] vc_voodoo_submit_triangle() -> SPSC ring -> render thread -> VkImage
                                                                                    |
                                                                    readback to shadow_fb[]
                                                                                    |
                                                            compare fb_mem[] vs shadow_fb[]
```

Both paths receive identical `voodoo_params_t` data. The software path writes
to `fb_mem[]` (the existing VRAM array). The Vulkan path renders to a VkImage
and reads back to a shadow buffer. Comparison happens after each swap.

### Where to Hook

**Triangle submission**: In `voodoo_queue_triangle()` (vid_voodoo_render.c),
both paths are dispatched simultaneously. The software path continues to run
its 4 render threads. The Vulkan path enqueues the triangle into the SPSC
command ring.

**Comparison point**: After each Voodoo swap buffer command, both paths have a
complete front buffer. The comparison runs at this point.

```c
/* In vid_voodoo_vk.c or a new vid_voodoo_verify.c */

void
vc_verify_frame(voodoo_t *voodoo)
{
    /* Read back the Vulkan front buffer to CPU memory */
    uint16_t *vk_fb = vc_readback_front(voodoo->vc_ctx);

    /* The software front buffer is in fb_mem at the front buffer offset */
    uint16_t *sw_fb = (uint16_t *)&voodoo->fb_mem[voodoo->front_offset];

    int width  = voodoo->h_disp;
    int height = voodoo->v_disp;
    int stride = voodoo->row_width;  /* pixels per row */

    vc_verify_stats_t stats = { 0 };

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint16_t sw_pixel = sw_fb[y * stride + x];
            uint16_t vk_pixel = vk_fb[y * width + x];

            if (sw_pixel != vk_pixel) {
                /* Decompose RGB565 and compute per-channel error */
                int sw_r = (sw_pixel >> 11) & 0x1F;
                int sw_g = (sw_pixel >> 5)  & 0x3F;
                int sw_b = (sw_pixel)       & 0x1F;

                int vk_r = (vk_pixel >> 11) & 0x1F;
                int vk_g = (vk_pixel >> 5)  & 0x3F;
                int vk_b = (vk_pixel)       & 0x1F;

                int err_r = abs(sw_r - vk_r);
                int err_g = abs(sw_g - vk_g);
                int err_b = abs(sw_b - vk_b);
                int max_err = (err_r > err_g) ? err_r : err_g;
                if (err_b > max_err) max_err = err_b;

                stats.divergent_pixels++;
                stats.total_channel_error += err_r + err_g + err_b;
                if (max_err > stats.max_channel_error)
                    stats.max_channel_error = max_err;
                if (max_err > 1)
                    stats.pixels_above_1lsb++;
            }
        }
    }

    stats.total_pixels = width * height;
    vc_verify_log_stats(voodoo, &stats);

    if (stats.pixels_above_1lsb > 0)
        vc_verify_dump_frame(voodoo, sw_fb, vk_fb, width, height);
}
```

### Comparison Metrics

| Metric | Definition | Acceptable Threshold |
|--------|-----------|---------------------|
| **Divergent pixel count** | Number of pixels where SW != VK | Report all; alarm if >5% of frame |
| **Max channel error** | Largest per-channel difference (in 5/6-bit space) | <=1 LSB per channel |
| **Mean channel error** | Average error across all divergent pixels | <0.5 LSB |
| **Pixels above 1 LSB** | Count of pixels with any channel error >1 | **Must be zero** for correctness |
| **Error distribution** | Histogram of error magnitudes | Should be concentrated at 0 and 1 |

### Acceptable Divergence: 1 LSB

The Vulkan path uses float arithmetic throughout. The software path uses
fixed-point (12.12 for colors, 48.16 for texture coordinates). The expected
divergence is at most 1 LSB per channel due to:

1. **Gradient reconstruction**: Reconstructing per-vertex values from gradients
   introduces ~1/4096 error per color channel in 12.12 format. After 565
   quantization, this is at most 1 LSB in the 5/6-bit output.

2. **Float rounding**: GPU float arithmetic rounds differently than CPU
   integer shifts. `int(color * 255.0) >> 3` (for 5-bit) vs the software
   path's `(startR + dRdX * dx) >> 12 >> 3`. Rounding modes differ.

3. **Texture filtering**: Bilinear interpolation on the GPU uses hardware
   fixed-point (typically 8-bit fraction). The software path uses its own
   8-bit fraction interpolation. Results should match but may differ by 1 LSB.

**Anything above 1 LSB indicates a real bug** -- wrong blend mode, wrong
texture coordinate, missing fog, incorrect combine math, etc.

### Comparison Format

The Voodoo renders to a 16-bit RGB565 framebuffer. Both paths produce 565
pixels. The Vulkan path renders internally at RGBA8 and quantizes to 565
during readback (matching what the Voodoo RAMDAC would do). Comparison is
done in the 565 domain to match what the user would see on screen.

**Note on dither**: When dithering is disabled in the Vulkan path (the
default), 1 LSB differences from the software path's dithered output are
expected and acceptable. For pixel-perfect comparison, either disable
dithering in the software path or enable "Accurate" dither tier in Vulkan.

### Output

**Per-frame log line** (printed via `pclog_ex()` when verify mode is active):

```
[VC_VERIFY] Frame 1234: 640x480, divergent=847/307200 (0.28%), max_err=1, mean_err=0.31, above_1lsb=0 -- OK
[VC_VERIFY] Frame 1235: 640x480, divergent=2134/307200 (0.69%), max_err=3, mean_err=1.12, above_1lsb=156 -- FAIL
```

**Screenshot dump** (on FAIL or on hotkey):
- `verify_NNNN_sw.raw` -- software framebuffer (raw RGB565)
- `verify_NNNN_vk.raw` -- Vulkan framebuffer (raw RGB565)
- `verify_NNNN_diff.raw` -- difference image (amplified, see Section 7)

### Activation

Dual-path verify mode is controlled by:

1. **Build-time**: `cmake -D VC_VERIFY=ON` (adds the verify code path)
2. **Runtime**: `device_config_t` checkbox "Verify rendering (slow)" on
   the Voodoo device. Only active when `VIDEOCOMMON=ON` is also set.
3. **Performance cost**: Running both renderers doubles the CPU rendering
   work and adds a readback + comparison per frame. Expect ~2x CPU usage.
   This is a development/debugging tool, not for end users.

### Threading Considerations

Both paths must complete before comparison can happen. The software path uses
4 render threads that signal completion via `render_voodoo_busy[]`. The Vulkan
path signals completion when the swap command finishes (fence signaled).

The verify comparison must wait for both:
```c
/* Wait for SW render threads to finish current frame */
for (int t = 0; t < voodoo->render_threads; t++)
    while (RENDER_VOODOO_BUSY(voodoo, t))
        thread_wait_event(voodoo->wake_render_thread[t], -1);

/* Wait for VK render to finish (already waited as part of swap) */
/* vc_sync() was called by the swap handler */

/* Now both buffers are stable -- compare */
vc_verify_frame(voodoo);
```

---

## 2. Vulkan Validation Layers

### Purpose

`VK_LAYER_KHRONOS_validation` is the single most important debugging tool for
Vulkan development. It catches:

- Missing or incorrect image layout transitions
- Pipeline barrier violations (read-after-write without barrier)
- Descriptor set binding errors (wrong type, stale descriptor)
- Push constant range violations
- Invalid usage flags on buffers/images
- Render pass compatibility errors
- Command buffer recording errors (e.g., recording outside render pass)
- Thread safety violations (recording from multiple threads without sync)

### How to Enable

**Method 1: Code (preferred for 86Box)**

```c
/* In vc_core.c, during VkInstance creation */
const char *validation_layers[] = {
    "VK_LAYER_KHRONOS_validation"
};

VkInstanceCreateInfo create_info = {
    .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .enabledLayerCount       = vc_enable_validation ? 1 : 0,
    .ppEnabledLayerNames     = validation_layers,
    .enabledExtensionCount   = ext_count,
    .ppEnabledExtensionNames = extensions,
};

/* Also enable debug utils extension for messenger callback */
/* VK_EXT_debug_utils must be in the extension list */
```

**Method 2: Environment variable (no code changes)**

```bash
# Enable validation for any Vulkan application
export VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation

# On macOS with MoltenVK, also set:
export MVK_CONFIG_LOG_LEVEL=2
```

**Method 3: Vulkan Configurator (vkconfig)**

The Vulkan SDK includes `vkconfig`, a GUI tool to enable/disable layers and
configure their behavior. Useful for one-off debugging sessions.

### Debug Messenger Callback

Route validation messages through 86Box's logging system:

```c
static VKAPI_ATTR VkBool32 VKAPI_CALL
vc_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                  VkDebugUtilsMessageTypeFlagsEXT type,
                  const VkDebugUtilsMessengerCallbackDataEXT *data,
                  void *user_data)
{
    const char *prefix;
    switch (severity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            prefix = "ERROR";
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            prefix = "WARNING";
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            prefix = "INFO";
            break;
        default:
            prefix = "VERBOSE";
            break;
    }

    pclog("[VC_VK %s] %s\n", prefix, data->pMessage);

    /* Return VK_FALSE to not abort the call that triggered the message */
    return VK_FALSE;
}

/* Register the callback after VkInstance creation */
VkDebugUtilsMessengerCreateInfoEXT messenger_info = {
    .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
    .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
                     | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
    .messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                     | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                     | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
    .pfnUserCallback = vc_debug_callback,
};
vkCreateDebugUtilsMessengerEXT(instance, &messenger_info, NULL, &debug_messenger);
```

### What to Watch For

**Critical errors (must fix immediately):**

| Error Pattern | Meaning | Likely Cause |
|--------------|---------|--------------|
| `VUID-vkCmdDraw-None-02697` | Image layout mismatch | Missing layout transition before draw |
| `VUID-VkImageMemoryBarrier-oldLayout` | Wrong oldLayout in barrier | Tracking error for current image layout |
| `VUID-vkCmdCopyImageToBuffer-srcImageLayout` | Copy from wrong layout | Missing TRANSFER_SRC transition before readback |
| `VUID-vkCmdPushConstants-offset` | Push constant out of range | Push constant struct size exceeds declared range |
| `VUID-vkCmdBindDescriptorSets-pDescriptorSets` | Invalid descriptor | Descriptor pool exhausted or stale write |
| `UNASSIGNED-CoreValidation-DrawState-InvalidImageLayout` | Image not in expected layout | Forgot to transition after render pass end |

**Performance warnings (fix when convenient):**

| Warning Pattern | Meaning | Action |
|----------------|---------|--------|
| `UNASSIGNED-BestPractices-*` | Non-optimal usage | Review but may be intentional |
| `UNASSIGNED-CoreValidation-Shader-*` | Shader issue | Check SPIR-V output |
| `vkQueueSubmit with fence not in unsignaled state` | Double-signal | Missing `vkResetFences()` |

### Performance Impact

Validation layers add approximately 10-30% overhead, depending on workload
complexity. For VideoCommon's relatively simple draw call pattern (50-200
draws per frame with small vertex counts), the overhead is on the lower end.

**Recommendation**: Always enable validation layers during development. Disable
only for release builds or performance benchmarking.

### Best Practices Validation

In addition to the standard validation layer, enable the best practices
sub-layer for performance and correctness hints:

```c
VkValidationFeaturesEXT validation_features = {
    .sType                         = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
    .enabledValidationFeatureCount = 1,
    .pEnabledValidationFeatures    = (VkValidationFeatureEnableEXT[]){
        VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
    },
};
/* Chain into VkInstanceCreateInfo.pNext */
```

### GPU-Assisted Validation

For hard-to-diagnose issues (especially descriptor indexing bugs), enable
GPU-assisted validation. This instruments shaders to detect out-of-bounds
descriptor access at the cost of significant performance overhead:

```c
VkValidationFeatureEnableEXT enables[] = {
    VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
    VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
};
```

**Only enable this when debugging specific descriptor issues.** The overhead
is 50-100% or more.

---

## 3. Test VM Matrix

### Test Games and What They Exercise

Each game targets specific Voodoo pipeline features. Games are ordered by
feature complexity, roughly mapping to implementation phases.

#### Tier 1: Basic Rendering (Phases 2-3)

| Game | Card | Key Features Exercised | Why This Game |
|------|------|----------------------|---------------|
| **GLQuake** | V1 | Textured triangles, depth test, single TMU, bilinear filter, basic alpha test (punch-through) | The canonical Voodoo test. Simple rendering, easy to verify visually. Known-good reference screenshots exist online. |
| **Tomb Raider** (1996) | V1 | Complex texturing, alpha test, chroma key, Gouraud shading, affine + perspective textures | Exercises chroma key (sprite cutouts) and mixed perspective modes. Visually distinctive -- errors are obvious. |

#### Tier 2: Combine + Alpha (Phases 4-6)

| Game | Card | Key Features Exercised | Why This Game |
|------|------|----------------------|---------------|
| **Quake 2** | V2 | Multi-texture (TMU0 + TMU1), lightmaps via multiply combine, fog, alpha blend (particles/water) | First real multi-texture test. Lightmap rendering is the most common TMU1 use case. |
| **Half-Life** | V2 | Fog (distance-based), alpha blend (water, glass), stipple (some effects), multi-texture | Heavy fog user. Tests the fog table lookup and fog color blending. |
| **Unreal** (1998) | V2 | Dual TMU, complex alpha blending (additive particles, subtractive shadows), detail textures | Exercises unusual blend factor combinations. Detail texture feature tests TMU1 detail combine mode. |
| **Turok: Dinosaur Hunter** | V1 | Heavy fog (constant + table), W-buffer depth, alpha test (foliage) | W-buffer test. Most games use Z-buffer; Turok is one of the few that uses W-buffer mode, which requires `gl_FragDepth` writes and custom depth mapping in the shader. |

#### Tier 3: Advanced Features (Phases 7-8)

| Game | Card | Key Features Exercised | Why This Game |
|------|------|----------------------|---------------|
| **Duke Nukem 3D** (Voodoo) | V1 | LFB reads (mirrors, water reflections), LFB writes (2D HUD overlay) | The canonical LFB test. Mirrors require reading back the framebuffer, compositing in software, and writing modified pixels back. Exercises the entire readback pipeline. |
| **Need for Speed III** | V2 | LFB reads (rear-view mirror), depth buffer reads (collision), alpha blend (glass) | Another LFB-heavy game. The rear-view mirror reads back a region, renders a scaled version. |
| **Star Wars: Rogue Squadron** | V2 | Stipple alpha, complex fog, depth buffer tricks | Uses stipple patterns for pseudo-transparency. |
| **MechWarrior 2** (3Dfx) | V1 | Basic rendering but with specific color combine modes (cockpit HUD overlay, targeting reticle) | Tests unusual `fbzColorPath` combine settings that most games do not use. |

#### Tier 4: Stress Tests (Phase 11)

| Game/Demo | Card | Key Features Exercised | Why This Demo |
|-----------|------|----------------------|---------------|
| **3DMark99 MAX** | V2/V3 | All features: multi-texture, fog, alpha, LFB, complex blending, high triangle count | Comprehensive benchmark that exercises every rendering feature. If this works, most things work. |
| **Final Reality** | V2 | Multi-texture, fogging, alpha, complex scenes, benchmark repeatability | Older benchmark, good for Voodoo 2 feature coverage. Deterministic run = repeatable comparison. |
| **Voodoo 1 demo disk** (3Dfx) | V1 | Official 3Dfx demos targeting V1 features | Designed to showcase every V1 feature. Good for basic coverage. |
| **Glide test programs** | V1/V2 | SDK test apps (if available) | Most direct feature tests. |

#### Tier 5: Banshee/V3 (Phase 10)

| Game | Card | Key Features Exercised | Why This Game |
|------|------|----------------------|---------------|
| **Quake III Arena** | V3 | Multi-texture, 32-bit rendering (Banshee/V3 mode), heavy alpha, fog | Tests 32-bit framebuffer mode available on Banshee/V3. |
| **Unreal Tournament** | V3 | All features at high triangle counts, 2D blitter (menus), VGA passthrough | Full Banshee/V3 feature test including 2D acceleration. |

### Per-Game Test Protocol

For each game in the matrix, the test protocol is:

1. **Boot VM** with the Voodoo card type matching the test row
2. **Load game** to a known state (main menu, specific level, or demo playback)
3. **Enable verify mode** (if not already enabled via config)
4. **Run for 60 seconds** of gameplay or demo playback
5. **Record**: frame count, divergent pixel stats, max error, any validation layer errors
6. **Screenshot**: capture at least one representative frame for visual comparison
7. **Log**: save verify log and any validation layer output

### Required Test VMs

| VM Name | OS | Card | Purpose |
|---------|----|------|---------|
| `voodoo1-win98` | Windows 98 SE | Voodoo 1 | V1 games (GLQuake, Tomb Raider, Turok, Duke3D, MechWarrior 2) |
| `voodoo2-win98` | Windows 98 SE | Voodoo 2 | V2 games (Quake 2, Half-Life, Unreal, NFS3, 3DMark99) |
| `banshee-win98` | Windows 98 SE | Banshee | Banshee-specific: 2D + 3D, VGA passthrough |
| `voodoo3-win98` | Windows 98 SE | Voodoo 3 | V3 games (Q3A, UT), 32-bit mode |

### ROM and Driver Requirements

- Voodoo 1: 3Dfx reference drivers (latest for Win98)
- Voodoo 2: 3Dfx reference drivers or MesaFX (for Glide 2.x games)
- Banshee: 3Dfx reference drivers
- Voodoo 3: 3Dfx reference drivers
- DirectX: DirectX 7.0a or later (for Direct3D games)
- Glide: Glide 2.x and 3.x runtime (installed by driver)

---

## 4. Phase-Gated Testing

Each implementation phase has specific test requirements. A phase is not
considered complete until all its test criteria are met.

### Phase 1: VideoCommon Core Infrastructure

**What is built**: Vulkan instance/device, render pass, FBO, uber-shader
compilation, SPSC ring, render thread lifecycle.

**Test criteria**:
- [ ] VkInstance creates successfully with validation layers enabled
- [ ] VkDevice selects correct physical device with required features
- [ ] Render pass and framebuffer creation succeed (RGBA8 color + D16 depth)
- [ ] SPIR-V uber-shader compiles without validation errors
- [ ] SPSC ring: produce/consume 10000 messages without corruption
- [ ] Render thread starts, processes commands, and shuts down cleanly
- [ ] Standalone test: draw a flat-color triangle to FBO, readback, verify pixel values match expected
- [ ] Validation layers report zero errors through entire test

**How to test**: Standalone test program or unit function called at startup
(before any game is loaded). Draws a known triangle with known colors and
reads back specific pixel coordinates to verify.

```c
/* Example standalone test */
void vc_self_test(vc_context_t *ctx) {
    /* Draw a red triangle covering the top-left quadrant */
    /* Expected: pixel (10, 10) = red, pixel (300, 300) = clear color */
    /* Readback and verify */
}
```

### Phase 2: Voodoo Triangle Path (Flat-Shaded)

**What is built**: `voodoo_queue_triangle()` to Vulkan path, vertex
reconstruction from gradients, depth test, scissor.

**Test criteria**:
- [ ] Boot `voodoo1-win98` VM with Vulkan path active
- [ ] Voodoo initializes without validation errors
- [ ] Flat-shaded geometry appears on screen with correct positions
- [ ] Depth testing works (correct occlusion ordering)
- [ ] Scissor clipping works (no rendering outside clip rect)
- [ ] Enable dual-path verify: max channel error <= 1 LSB on flat triangles
- [ ] No Vulkan validation errors during operation

**Games to test**: None yet -- use Voodoo test pattern (solid triangles from
Glide test apps) or the Windows desktop if available.

### Phase 3: Texture Support

**What is built**: TMU0 texturing, all 15 texture formats, point/bilinear
filtering, clamp/wrap/mirror, perspective correction.

**Test criteria**:
- [ ] Textured triangles render with correct texture mapping
- [ ] Perspective correction works (no affine distortion on large quads)
- [ ] All texture wrap modes work (clamp, wrap, mirror)
- [ ] Bilinear filtering matches software output to within 1 LSB
- [ ] NCC-format textures decode correctly
- [ ] Texture cache invalidation works (changing textures in VRAM updates display)
- [ ] Dual-path verify on GLQuake: all pixels within 1 LSB
- [ ] Dual-path verify on Tomb Raider: all pixels within 1 LSB
- [ ] Validation layers report zero errors

**Games to test**: GLQuake, Tomb Raider

### Phase 4: Color/Alpha Combine + Chroma Key

**What is built**: Full `fbzColorPath` decode -- color select, alpha select,
color combine (sub/mul/add), alpha combine, chroma key.

**Test criteria**:
- [ ] Color combine modes all produce correct results vs software path
- [ ] Alpha combine modes match
- [ ] Chroma key correctly discards matching pixels
- [ ] `cc_localselect_override` (iterated vs texture alpha select) works
- [ ] Dual-path verify on Quake 2 (lightmaps need multiply combine): <= 1 LSB
- [ ] Validation layers report zero errors

**Games to test**: GLQuake (re-test), Quake 2 (multi-pass lightmaps)

### Phase 5: TMU1 + Multi-Texture

**What is built**: Dual TMU support, TMU0/TMU1 combine, detail texture, LOD
blend, trilinear.

**Test criteria**:
- [ ] Second TMU texture fetches work
- [ ] TMU1-to-TMU0 combine produces correct output
- [ ] Detail texture mode works (bias, scale, max applied correctly)
- [ ] Trilinear filtering between LOD levels
- [ ] Dual-path verify on Quake 2 (single-pass multi-texture): <= 1 LSB
- [ ] Dual-path verify on Unreal (dual TMU, detail textures): <= 1 LSB
- [ ] Validation layers report zero errors

**Games to test**: Quake 2, Unreal, Half-Life

### Phase 6: Fog, Alpha Test, Alpha Blend

**What is built**: Fog table lookup, 3 fog modes, alpha test in shader,
alpha blend via `VkPipeline` blend state.

**Test criteria**:
- [ ] Fog table lookup produces correct fog factor
- [ ] All 3 fog modes (table, multiply, add) work
- [ ] Alpha test: all 8 compare functions work (NEVER, LESS, EQUAL, LEQUAL, GREATER, NOTEQUAL, GEQUAL, ALWAYS)
- [ ] Alpha blend: standard blend factors map correctly to VkBlendFactor
- [ ] Pipeline cache: blend state changes create new pipelines on first use, cache on reuse
- [ ] `AFUNC_ACOLORBEFOREFOG` (dest_afunc=0xF): copy-on-blend path works
- [ ] Dual-path verify on Half-Life (fog-heavy): <= 1 LSB
- [ ] Dual-path verify on Turok (W-buffer + fog): <= 1 LSB
- [ ] Validation layers report zero errors

**Games to test**: Half-Life, Turok, Unreal (re-test)

### Phase 7: Dither, Stipple, Remaining Features

**What is built**: Three-tier dither, stipple pattern test, fastfill.

**Test criteria**:
- [ ] Dither "Off" tier: RGBA8 output without dithering (default)
- [ ] Dither "Post-frame" tier: Bayer 4x4 + 565 quantization applied correctly
- [ ] Dither "Accurate" tier: per-triangle copy-on-blend dithering
- [ ] Stipple: stipple mask rejects correct pixels
- [ ] Fastfill: clear command clears color and depth correctly
- [ ] Dual-path verify with "Accurate" dither: <= 1 LSB (pixel-perfect with SW)
- [ ] Validation layers report zero errors

**Games to test**: Star Wars: Rogue Squadron (stipple), re-test all previous

### Phase 8: LFB Read/Write

**What is built**: LFB read via `vkCmdCopyImageToBuffer`, LFB write via
staging buffer upload or point primitive, dirty region tracking.

**Test criteria**:
- [ ] LFB read returns correct pixel values matching rendered content
- [ ] LFB write correctly modifies framebuffer pixels
- [ ] Pipeline writes (through uber-shader) work for LFB pixel submission
- [ ] Dirty region tracking: only modified tiles are re-read
- [ ] Async readback (double-buffered staging): correct data with 1-frame latency
- [ ] Sync readback: correct data, immediate
- [ ] Duke Nukem 3D mirrors render correctly (LFB read-modify-write cycle)
- [ ] Dual-path verify on Duke3D: <= 1 LSB
- [ ] Validation layers report zero errors

**Games to test**: Duke Nukem 3D, Need for Speed III

### Phase 9: Qt VCRenderer (Zero-Copy Display)

**Test criteria**:
- [ ] VCRenderer creates VkSurfaceKHR from Qt window
- [ ] Swapchain creation and presentation work
- [ ] Front buffer blits to swapchain without readback
- [ ] Window resize handled correctly (swapchain recreation)
- [ ] Display is visually identical to readback path
- [ ] No tearing or sync artifacts

**Games to test**: Any -- display path is game-independent

### Phase 10: Banshee/V3 2D + VGA Passthrough

**Test criteria**:
- [ ] VGA text mode displays correctly when 3D is inactive
- [ ] VGA passthrough transitions smoothly to/from 3D mode
- [ ] 2D blitter operations render correctly
- [ ] Windows desktop on Banshee/V3 displays correctly
- [ ] 3D games on Banshee/V3 work (re-test Tier 2-4 games)
- [ ] Validation layers report zero errors

**Games to test**: Quake III Arena, Unreal Tournament, Windows desktop

### Phase 11: Full Validation

**Test criteria**:
- [ ] All Tier 1-5 games pass dual-path verify with <= 1 LSB
- [ ] All Voodoo variants (V1, V2, Banshee, V3) boot and render
- [ ] 3DMark99 completes full benchmark run without errors
- [ ] Performance benchmarks completed (see Section 5)
- [ ] Zero Vulkan validation errors across all test games
- [ ] LFB-heavy games work correctly
- [ ] SLI compositing works (V2 dual-card mode)

---

## 5. Performance Benchmarking

### Metrics

| Metric | Definition | How Measured |
|--------|-----------|-------------|
| **Frame time** | Time from swap to swap | `clock_gettime()` around swap handler |
| **CPU usage** | Total CPU time across all threads | `/proc/self/stat` or `mach_thread_basic_info` |
| **SW thread idle time** | Time software render threads spend idle | Should be ~100% when Vulkan path is active alone |
| **GPU utilization** | Percentage of GPU time spent rendering | Platform tools (Metal GPU Profiler on macOS) |
| **Readback latency** | Time for a single LFB readback operation | Instrumented `vc_readback_region()` |
| **Pipeline cache hits** | Ratio of cached vs newly created pipelines | Counter in `vc_get_pipeline()` |
| **Batch count** | Draw calls per frame | Counter in render thread |
| **Triangle count** | Triangles submitted per frame | Counter in SPSC producer |

### Benchmark Protocol

1. Configure VM with test game at a specific, repeatable scene:
   - GLQuake: `timedemo demo1` (standard Quake benchmark)
   - 3DMark99: built-in benchmark mode
   - Quake 2: `timedemo demo1`
   - For other games: find a reproducible demo or scripted sequence

2. Run each benchmark twice:
   - **Software-only**: Vulkan path disabled, normal software rendering
   - **Vulkan-only**: Vulkan path active, software render threads idle

3. For each run, record:
   - Total frames rendered
   - Total wall-clock time
   - Average frame time and 99th percentile frame time
   - CPU usage (all cores combined)
   - Any hitches (frames taking >2x average)

4. Duration: 60 seconds minimum per run, or the full timedemo if available.

### Expected Results

| Metric | Software Path | Vulkan Path | Expected Improvement |
|--------|-------------|-------------|---------------------|
| CPU usage (render threads) | High (4 threads fully loaded) | Near zero (work on GPU) | 4 CPU threads freed |
| Frame time | Limited by CPU speed | Limited by GPU + CPU overhead | Comparable or better |
| Readback latency | N/A (direct VRAM access) | 0.5-2ms per readback | Acceptable |
| Frame time variance | Low (deterministic CPU work) | Low (simple GPU workload) | Similar |

The primary benefit is not raw frame rate (Voodoo games are not typically
GPU-bound on modern hardware) but CPU thread utilization. Freeing 4 render
threads means more CPU headroom for the emulated CPU, FIFO processing, and
other emulated hardware.

### Benchmark Recording Format

```
=== VideoCommon Performance Benchmark ===
Date: 2026-XX-XX
Platform: macOS 15.x, Apple M2, MoltenVK 1.4
Game: GLQuake timedemo demo1
Resolution: 640x480
Voodoo: V1

--- Software Path ---
Frames: 2169
Time: 60.00s
Avg frame time: 27.66ms (36.2 fps)
99th %ile frame time: 31.2ms
CPU usage: 287% (4 render threads)
Peak memory: 142 MB

--- Vulkan Path ---
Frames: 2169
Time: 60.00s
Avg frame time: 18.42ms (54.3 fps)
99th %ile frame time: 22.1ms
CPU usage: 78% (render threads idle)
Peak memory: 168 MB (+26 MB for Vulkan resources)
Batches/frame: 142 avg
Pipelines cached: 3
Readbacks: 0

--- Delta ---
CPU savings: 209% (4 threads freed)
Frame time: -33.4% (faster)
Memory: +18.3%
```

---

## 6. Regression Testing Workflow

### Principle

Every new phase must not break any previous phase. After completing Phase N,
re-run all test games from Phases 1 through N-1.

### Regression Test Matrix

After each phase completion, execute this checklist:

```
Phase Completed: [N]
Date: YYYY-MM-DD
Tester: [name]

P1 Core Infrastructure:
  [ ] Standalone triangle test passes
  [ ] Validation layers clean

P2 Flat-Shaded Triangles:
  [ ] VM boots with Vulkan path
  [ ] Flat geometry renders correctly

P3 Textures:
  [ ] GLQuake: verify <= 1 LSB
  [ ] Tomb Raider: verify <= 1 LSB

P4 Color/Alpha Combine:
  [ ] Quake 2 (multi-pass lightmaps): verify <= 1 LSB

P5 TMU1 Multi-Texture:
  [ ] Quake 2 (single-pass): verify <= 1 LSB
  [ ] Unreal: verify <= 1 LSB

P6 Fog/Alpha:
  [ ] Half-Life: verify <= 1 LSB
  [ ] Turok: verify <= 1 LSB

P7 Dither/Stipple:
  [ ] Accurate dither matches SW: verify <= 1 LSB

P8 LFB:
  [ ] Duke3D mirrors: verify <= 1 LSB

P9 Display:
  [ ] Visual parity with readback path

P10 Banshee/V3:
  [ ] Windows desktop renders
  [ ] Q3A/UT work on V3

Notes:
[Free-form notes on any issues found]
```

### When Regressions Are Found

1. **Identify**: Which phase's test case failed? What is the error?
2. **Bisect**: Was the regression introduced in the current phase or earlier?
   (Use git bisect if needed.)
3. **Fix**: Prioritize fixing regressions over new feature work.
4. **Verify**: Re-run the full regression matrix after the fix.

### Regression Tracking

Keep a running log in `videocommon-plan/validation/regression-log.md`:

```markdown
## Regression Log

### 2026-XX-XX -- Phase 5 Complete
- Re-tested P3 (GLQuake): PASS
- Re-tested P3 (Tomb Raider): PASS
- Re-tested P4 (Quake 2 multi-pass): PASS
- P5 (Quake 2 single-pass): PASS
- P5 (Unreal): PASS -- 0.14% divergent, all <= 1 LSB
```

---

## 7. Screenshot Comparison Tool

### Purpose

A simple offline utility to diff two framebuffer dumps and produce a visual
difference image plus error statistics. Useful for post-mortem analysis of
verify failures and for visual documentation.

### Input Format

Two raw framebuffer files, each containing `width * height` pixels in
RGB565 format (little-endian uint16), with width and height specified on the
command line.

The verify mode (Section 1) dumps these files automatically on failure.
They can also be captured manually via a hotkey.

### Implementation

```c
/* vc_screenshot_diff.c -- standalone command-line tool */
/* Build: cc -o vc_screenshot_diff vc_screenshot_diff.c -lm */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

typedef struct {
    int divergent_pixels;
    int total_pixels;
    int max_channel_error;
    double mean_channel_error;
    int pixels_above_1lsb;
    int histogram[32];  /* error magnitude distribution, 0-31 */
} diff_stats_t;

static void
rgb565_to_rgb8(uint16_t pixel, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = ((pixel >> 11) & 0x1F) << 3;  /* 5-bit -> 8-bit */
    *g = ((pixel >> 5)  & 0x3F) << 2;  /* 6-bit -> 8-bit */
    *b = ((pixel)       & 0x1F) << 3;  /* 5-bit -> 8-bit */
}

static void
compute_diff(const uint16_t *sw_fb, const uint16_t *vk_fb,
             uint8_t *diff_rgb8, int width, int height,
             diff_stats_t *stats)
{
    memset(stats, 0, sizeof(*stats));
    stats->total_pixels = width * height;
    long total_err = 0;

    for (int i = 0; i < width * height; i++) {
        uint16_t sw = sw_fb[i];
        uint16_t vk = vk_fb[i];

        if (sw == vk) {
            /* Matching pixel -- dark gray in diff image */
            diff_rgb8[i * 3 + 0] = 32;
            diff_rgb8[i * 3 + 1] = 32;
            diff_rgb8[i * 3 + 2] = 32;
            continue;
        }

        int sw_r = (sw >> 11) & 0x1F;
        int sw_g = (sw >> 5)  & 0x3F;
        int sw_b = (sw)       & 0x1F;

        int vk_r = (vk >> 11) & 0x1F;
        int vk_g = (vk >> 5)  & 0x3F;
        int vk_b = (vk)       & 0x1F;

        int err_r = abs(sw_r - vk_r);
        int err_g = abs(sw_g - vk_g);
        int err_b = abs(sw_b - vk_b);

        int max_err = err_r;
        if (err_g > max_err) max_err = err_g;
        if (err_b > max_err) max_err = err_b;

        stats->divergent_pixels++;
        total_err += err_r + err_g + err_b;
        if (max_err > stats->max_channel_error)
            stats->max_channel_error = max_err;
        if (max_err > 1)
            stats->pixels_above_1lsb++;
        if (max_err < 32)
            stats->histogram[max_err]++;

        /* Amplify error for visibility in diff image */
        /* 1 LSB = yellow, >1 LSB = red, scaling by 8x */
        uint8_t amp = (max_err > 1) ? 255 : 128;
        diff_rgb8[i * 3 + 0] = amp;                         /* R: always lit */
        diff_rgb8[i * 3 + 1] = (max_err <= 1) ? amp : 0;   /* G: yellow for 1LSB */
        diff_rgb8[i * 3 + 2] = 0;                           /* B: off */
    }

    if (stats->divergent_pixels > 0)
        stats->mean_channel_error = (double)total_err
            / (stats->divergent_pixels * 3.0);
}
```

### Output

1. **Diff image**: Raw RGB8 file showing:
   - Dark gray: matching pixels
   - Yellow: 1 LSB divergence (acceptable)
   - Red: >1 LSB divergence (bug)

2. **Statistics printed to stdout**:
```
Comparison: verify_0042_sw.raw vs verify_0042_vk.raw (640x480)
Total pixels:     307200
Divergent pixels: 847 (0.28%)
Max channel error: 1 LSB
Mean channel error: 0.31 LSB
Pixels above 1 LSB: 0
Result: PASS

Error histogram:
  0 LSB: 306353 (99.72%)
  1 LSB: 847 (0.28%)
```

### Integration with Emulator

The comparison tool can be invoked in two ways:

**Automatic**: When verify mode is active and a frame fails (any pixel >1 LSB),
the dumps and diff are written automatically to a configurable output directory.

**Manual hotkey**: A keyboard shortcut (e.g., F12) captures both framebuffers
at the current frame and writes them to disk. Useful for spot-checking specific
scenes without running in full verify mode.

```c
/* Hotkey handler in vid_voodoo_vk.c */
void
vc_capture_screenshot(voodoo_t *voodoo)
{
    static int capture_index = 0;
    char path[256];

    /* Capture SW framebuffer */
    snprintf(path, sizeof(path), "capture_%04d_sw.raw", capture_index);
    vc_write_raw_fb(path, voodoo->fb_mem + voodoo->front_offset,
                    voodoo->h_disp, voodoo->v_disp, voodoo->row_width);

    /* Capture VK framebuffer */
    snprintf(path, sizeof(path), "capture_%04d_vk.raw", capture_index);
    uint16_t *vk_fb = vc_readback_front(voodoo->vc_ctx);
    vc_write_raw_fb(path, vk_fb,
                    voodoo->h_disp, voodoo->v_disp, voodoo->h_disp);

    pclog("[VC] Captured frame %d\n", capture_index);
    capture_index++;
}
```

### Viewing Raw Files

Raw RGB565 files can be viewed with ImageMagick:

```bash
# View a raw RGB565 framebuffer dump
convert -size 640x480 -depth 16 rgb565:verify_0042_sw.raw verify_sw.png

# View the diff image (RGB8)
convert -size 640x480 -depth 8 rgb:verify_0042_diff.raw verify_diff.png
```

Or with `ffmpeg`:

```bash
ffplay -f rawvideo -pixel_format rgb565le -video_size 640x480 verify_0042_sw.raw
```

---

## 8. Edge Cases

### 8.1 Pipeline State Leakage Between Games

**Problem**: Vulkan pipeline state (blend factors, depth function) cached from
one game session may not be correctly reset when loading a different game.

**Risk**: A game that assumes a clean Voodoo init (all registers at defaults)
may inherit stale pipeline state from a previous game if the emulator is
restarted without a full Voodoo re-initialization.

**Test**: Boot Game A, play for a bit, then reboot the VM (soft reset) and
boot Game B. Verify Game B renders identically to a cold-start scenario.

**Mitigation**: On Voodoo init (`voodoo_init()`), fully reset all VideoCommon
state: destroy all cached pipelines, reset push constants to defaults, clear
texture cache, reset render pass state.

### 8.2 Texture Cache Corruption Across Mode Switches

**Problem**: The Voodoo texture cache keys on VRAM base address + tLOD bits.
If a game changes texture VRAM layout (e.g., switching from 256x256 to
128x128 textures at the same base address), stale cache entries may be served.

**Risk**: Garbled textures, wrong LOD levels, visual corruption.

**Test**: Use a game that dynamically loads textures (Quake 2 level transitions,
Unreal level streaming). Verify textures are correct after each level load.

**Mitigation**: The Vulkan backend must:
1. Invalidate texture cache entries when `texBaseAddr` changes
2. Track NCC table writes and invalidate affected textures
3. Invalidate all textures on Voodoo soft reset

### 8.3 LFB Read/Write During Active Rendering

**Problem**: The guest CPU may read or write the framebuffer while the Vulkan
path is mid-frame (triangles are in flight). This requires synchronization.

**Risk**: Reading stale data (before current frame's triangles are rendered),
or writing data that gets overwritten by in-flight draws.

**Test**: Duke Nukem 3D (mirrors), Need for Speed III (rear-view mirror).
These games interleave triangle submission with LFB read-modify-write.

**Mitigation**: `vc_sync()` must be called before any LFB read. This flushes
all pending batches, submits the command buffer, and waits for the GPU fence.
LFB writes can be batched and submitted as part of the next draw batch.

### 8.4 VGA Passthrough Mode Transitions

**Problem**: Banshee and Voodoo 3 support VGA passthrough -- when 3D is
inactive, the card acts as a standard VGA adapter. Transitioning between
VGA mode and 3D mode requires switching the display source.

**Risk**: Display corruption during transition, stale VGA content appearing
in 3D framebuffer, or 3D content appearing during VGA mode.

**Test**: Boot Windows 98 on Banshee, enter a 3D game, exit back to desktop,
re-enter the game. Verify each transition is clean.

**Mitigation**:
1. On 3D activate: begin rendering to Vulkan framebuffer, switch display
   source from VGA scanout to Vulkan front buffer
2. On 3D deactivate: stop Vulkan rendering, switch display source back to
   VGA scanout
3. Clear the Vulkan framebuffer on 3D activate to prevent stale content

### 8.5 SLI Compositing (Voodoo 2 Dual-Card)

**Problem**: SLI mode uses two Voodoo 2 cards, each rendering alternate
scanlines. The master card composites both cards' output for display.

**Risk**: Scanline interleaving artifacts, wrong card receiving wrong
triangles, sync between two VideoCommon contexts.

**Test**: Configure a Voodoo 2 SLI setup, run 3DMark99 or a Glide 3.x game.
Verify no scanline artifacts and correct compositing.

**Mitigation**: Each Voodoo 2 card gets its own `vc_context_t` with its own
VkDevice (or shared device with separate command buffers). Scanout composites
odd lines from card 0's front buffer and even lines from card 1's front buffer.

### 8.6 FIFO Stalls and Sync Deadlocks

**Problem**: The SPSC ring between the FIFO thread and the Vulkan render
thread can cause deadlocks if:
- The ring is full and the producer blocks, but the consumer is waiting for
  a sync that requires the producer to make progress
- The render thread blocks on a fence that requires a queue submission that
  has not happened yet

**Risk**: Emulator hangs.

**Test**: Games with heavy triangle submission (3DMark99) combined with
frequent LFB reads (rare but possible).

**Mitigation**:
1. SPSC ring must be large enough (4096 entries) to absorb burst submissions
2. Producer never blocks on sync -- it enqueues a sync command and waits for
   the consumer to signal completion via a separate mechanism (event/condvar)
3. Consumer processes all commands up to and including the sync command before
   signaling
4. Fence waits in the consumer must have timeouts to detect and report hangs

### 8.7 Render Pass Load/Store Ops

**Problem**: Incorrect render pass load or store operations can cause:
- `LOAD_OP_CLEAR` when we need `LOAD_OP_LOAD` (erases previous content)
- `STORE_OP_DONT_CARE` when we need `STORE_OP_STORE` (discards rendered content)

**Risk**: Frame appears as solid color (all content cleared) or missing
content from previous draws in the same frame.

**Test**: Any game with multiple draw calls per frame (all games). The first
draw in a frame uses `LOAD_OP_CLEAR` (if fastfill) or `LOAD_OP_LOAD` (if
incremental). Subsequent draws within the same render pass instance are
not affected (they do not re-execute the load op).

**Mitigation**: Track whether a clear (fastfill) command was issued for
the current frame. If yes, use `LOAD_OP_CLEAR` for the first render pass
begin. Otherwise, use `LOAD_OP_LOAD`. Always use `STORE_OP_STORE`.

### 8.8 Depth Buffer Format and Precision

**Problem**: The Voodoo uses 16-bit Z with configurable depth function.
Some games use Z-buffer mode (linear Z), others use W-buffer mode (1/Z or
logarithmic depth).

**Risk**: Z-fighting artifacts different from software path, or inverted
depth test results.

**Test**: Turok (W-buffer), GLQuake (Z-buffer). Verify depth ordering matches
software path in both modes.

**Mitigation**:
1. Z-buffer mode: write Z directly to `VK_FORMAT_D16_UNORM` depth attachment
   via rasterizer (no `gl_FragDepth` write needed if Voodoo Z maps to [0,1])
2. W-buffer mode: write custom depth to `gl_FragDepth` in the fragment shader,
   replicating the Voodoo's W-to-depth mapping
3. Depth function mapping: Voodoo depth functions map 1:1 to VkCompareOp values

### 8.9 Coordinate System Differences

**Problem**: Vulkan uses upper-left origin with Y-down in clip space and
framebuffer coordinates. The Voodoo also uses upper-left origin with Y-down.
However, the Voodoo's `y_origin` register can flip the Y axis.

**Risk**: Upside-down rendering, off-by-one scanline errors.

**Test**: All games. Verify vertical orientation matches software path.

**Mitigation**: Account for `y_origin` in vertex shader NDC conversion.
When `y_origin` flips, adjust the Y coordinate accordingly.

### 8.10 Precision of Gradient Reconstruction

**Problem**: Vertex colors and texture coordinates are reconstructed from
gradients using:
```
V_B = startV + dVdX * (xB - xA) + dVdY * (yB - yA)
```
This is done in floating-point on the CPU side. The gradients are in
fixed-point (12.12 for colors, 48.16 for texture coords). The reconstruction
introduces rounding error.

**Risk**: Accumulated error across large triangles could exceed 1 LSB in
extreme cases (e.g., a triangle spanning the entire screen with rapid color
gradient).

**Test**: Synthetic test: submit a full-screen triangle with a steep color
gradient. Compare reconstructed vertex colors against what the software path
would produce. Verify error is bounded.

**Mitigation**: Perform reconstruction in double precision if single
precision proves insufficient. Monitor the dual-path verify stats for
patterns of increasing error with triangle size.

---

## 9. CI Considerations

86Box has no CI infrastructure for automated testing. However, documenting
how automated testing could work ensures the testing strategy scales beyond
manual verification.

### Headless Vulkan Rendering

Two software Vulkan implementations enable headless (no GPU) rendering:

**SwiftShader** (Google):
- Software Vulkan 1.1 implementation
- Used by Chrome for WebGPU fallback
- Available on all platforms (Windows, Linux, macOS)
- Slow but bit-exact across runs
- License: Apache 2.0

**lavapipe** (Mesa):
- Software Vulkan 1.3 implementation (part of Mesa)
- Available on Linux
- Uses LLVM for shader JIT
- Faster than SwiftShader, still much slower than real GPU

**Usage**: Set `VK_ICD_FILENAMES` to point to the software ICD JSON:

```bash
export VK_ICD_FILENAMES=/path/to/swiftshader/vk_swiftshader_icd.json
./86Box -P /path/to/test/vm --vmpath /path/to/test/vm
```

VideoCommon would use SwiftShader or lavapipe as its Vulkan device, rendering
everything in software on the CPU. This is slow but requires no GPU.

### Deterministic Rendering

For golden-master screenshot comparison to work in CI, rendering must be
deterministic: same input produces identical output across runs.

**Requirements for determinism**:
1. **Fixed random seeds**: If any random state exists (it should not in our
   pipeline), seed it deterministically
2. **Consistent float rounding**: SwiftShader guarantees IEEE 754 compliance.
   Real GPUs may have slight rounding differences (especially for transcendental
   functions), but our shader uses only basic arithmetic (add, mul, clamp)
3. **No race conditions**: The SPSC ring and render thread must produce
   identical output regardless of scheduling. Since we use a single render
   thread with deterministic command ordering, this holds.
4. **Same Vulkan driver**: CI must pin the software Vulkan version to avoid
   cross-version differences

### Golden-Master Comparison Pipeline

```
                    +------------------+
                    | Test ROM image   |
                    | (game + savegame)|
                    +--------+---------+
                             |
                    +--------v---------+
                    | 86Box headless   |
                    | (SwiftShader VK) |
                    | Run for N frames |
                    +--------+---------+
                             |
                    +--------v---------+
                    | Capture frame at |
                    | specific points  |
                    | (F=100, 200, 300)|
                    +--------+---------+
                             |
              +--------------+--------------+
              |                             |
    +---------v----------+       +----------v---------+
    | Golden master PNGs |       | Current run PNGs   |
    | (committed to repo |       | (from this CI run) |
    | or artifact store) |       +----------+---------+
    +---------+----------+                  |
              |                             |
              +----------+------------------+
                         |
                +--------v---------+
                | Pixel-diff tool  |
                | (Section 7)      |
                +--------+---------+
                         |
                +--------v---------+
                | PASS/FAIL + diff |
                | images as        |
                | CI artifacts     |
                +------------------+
```

### Scripted Test Execution

To make tests repeatable, 86Box would need a minimal automation interface:

1. **Auto-start VM**: `--vmpath` already exists
2. **Auto-capture at frame N**: New command-line flag or config file option
   `vc_capture_frames=100,200,300` that triggers screenshot capture at
   specific frame numbers
3. **Auto-exit after N frames**: New flag `--max-frames 500` to exit after
   rendering 500 frames
4. **Headless mode**: No window, no UI. Already partially supported via
   SDL headless mode.

**Minimal additions needed**:
- `--max-frames N` flag in main loop
- `--vc-capture-at N[,N...]` flag in verify mode
- `--headless` flag to suppress window creation

These are small additions (~50 lines each) that would enable basic CI without
a full test framework.

### CI Workflow Example (GitHub Actions)

```yaml
name: VideoCommon Rendering Tests
on: [push, pull_request]
jobs:
  render-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: |
          sudo apt-get install -y libvulkan-dev mesa-vulkan-drivers
          # Install SwiftShader for headless Vulkan
          wget -q https://example.com/swiftshader-vk.tar.gz
          tar xzf swiftshader-vk.tar.gz
      - name: Build 86Box
        run: |
          cmake -S . -B build -D VIDEOCOMMON=ON -D VC_VERIFY=ON -D QT=OFF
          cmake --build build
      - name: Run GLQuake test
        run: |
          export VK_ICD_FILENAMES=$PWD/swiftshader/vk_swiftshader_icd.json
          ./build/86Box \
            --vmpath test/vms/voodoo1-glquake \
            --headless \
            --max-frames 500 \
            --vc-capture-at 100,200,300,400,500
      - name: Compare screenshots
        run: |
          for f in capture_*.raw; do
            golden="test/golden/${f}"
            ./build/vc_screenshot_diff "$golden" "$f" 640 480
          done
      - name: Upload diff images
        uses: actions/upload-artifact@v4
        if: failure()
        with:
          name: render-diffs
          path: "*.diff.png"
```

### Practical CI Constraints

1. **VM images are large**: A Windows 98 test VM with games installed is
   several GB. These cannot be committed to the repo. Options:
   - Store in a separate artifact repository (S3, GCS)
   - Use a minimal DOS-based test that exercises Glide directly
   - Create a synthetic test that submits raw Voodoo register writes
     (no guest OS needed)

2. **SwiftShader is slow**: Rendering 500 frames at 640x480 through
   SwiftShader may take minutes. Acceptable for CI but not for interactive
   development.

3. **Glide driver in VM**: The test VM needs 3Dfx drivers installed, which
   requires a Windows guest. A synthetic test bypassing the guest OS would
   be more CI-friendly.

4. **Golden master maintenance**: When the shader changes (intentionally),
   golden masters must be updated. This creates a maintenance burden. Using
   the dual-path verify (SW vs VK comparison within the same run) is more
   robust because the software path is always the reference -- no external
   golden masters needed.

### Recommendation for CI

**Short-term (during development)**: No CI. Use manual dual-path verify
(Section 1) on each developer's machine. This catches all correctness bugs
without any CI infrastructure.

**Medium-term (after Phase 11)**: Add a synthetic register-write test that
submits known Voodoo commands to the VideoCommon API (bypassing guest OS
entirely). This can run in CI with SwiftShader and compare against precomputed
expected pixel values.

**Long-term (if VideoCommon is adopted upstream)**: Full CI with VM images
stored in artifact storage, golden-master comparison, and automated regression
testing. This is a significant infrastructure investment and should only be
pursued if VideoCommon becomes a supported feature in upstream 86Box.

---

## Summary of Key Recommendations

1. **Dual-path verify is the primary correctness tool.** Run both SW and VK
   paths on every triangle, compare every frame. Threshold: <=1 LSB.

2. **Vulkan validation layers are mandatory during development.** Zero
   validation errors is a hard requirement for each phase.

3. **Test games are grouped by feature complexity** and mapped to phases.
   Each phase has specific games that must pass before proceeding.

4. **Regression test after every phase.** All previous phase games must still
   pass. Use the regression checklist.

5. **Performance benchmarking** focuses on CPU thread savings, not raw FPS.
   The expected win is freeing 4 render threads.

6. **The screenshot diff tool** is simple, standalone, and can be used offline.
   Integrate it into the emulator via hotkey for convenience.

7. **Edge cases** (LFB during rendering, VGA passthrough, SLI, depth mode)
   require specific test scenarios and are documented for targeted testing.

8. **CI is aspirational but not required initially.** The dual-path verify
   approach works without any CI infrastructure and catches bugs at
   development time.
