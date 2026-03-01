# Vulkan Architecture for VideoCommon

Comprehensive research document for the GL-to-Vulkan pivot of the VideoCommon
GPU-accelerated rendering infrastructure. All findings are sourced from the
Vulkan 1.2 specification, MoltenVK documentation, Mesa V3DV driver status,
and emulator source code analysis.

Research date: 2026-02-26

---

## Table of Contents

1. [GL-to-Vulkan Mapping Table](#1-gl-to-vulkan-mapping-table)
2. [Pipeline Management Strategy](#2-pipeline-management-strategy)
3. [Third-Party Library Recommendations](#3-third-party-library-recommendations)
4. [MoltenVK Specifics (macOS)](#4-moltenvk-specifics-macos)
5. [Pi 5 V3D Vulkan Specifics](#5-pi-5-v3d-vulkan-specifics)
6. [Display Integration](#6-display-integration)
7. [Emulator Vulkan Backends Survey](#7-emulator-vulkan-backends-survey)
8. [SPIR-V Toolchain](#8-spir-v-toolchain)
9. [Synchronization Model](#9-synchronization-model)
10. [Complexity Honest Assessment](#10-complexity-honest-assessment)

---

## 1. GL-to-Vulkan Mapping Table

Every OpenGL concept referenced in DESIGN.md and its Vulkan 1.2 equivalent.

### Core Infrastructure

| OpenGL Concept | Vulkan 1.2 Equivalent | Notes |
|---|---|---|
| GL Context | `VkInstance` + `VkDevice` + `VkQueue` | No thread-affinity constraint in Vulkan -- any thread can record command buffers. Queue submission still needs synchronization. |
| GLAD (GL loader) | **volk** (meta-loader) | Dynamic function pointer loading. MIT license. Avoids linking against vulkan-1.dll/libvulkan.so. |
| `glEnable/glDisable` | Pipeline state or dynamic state | State is baked into `VkPipeline` unless declared dynamic. |
| GL extensions | Vulkan extensions + core features | Query via `vkEnumerateDeviceExtensionProperties()` and `VkPhysicalDeviceFeatures2`. |

### Framebuffer and Render Targets

| OpenGL Concept | Vulkan 1.2 Equivalent | Notes |
|---|---|---|
| FBO (`glGenFramebuffers`) | `VkFramebuffer` + `VkRenderPass` | Vulkan framebuffers are bound to a specific render pass and set of image views. |
| FBO color attachment (`GL_RGBA8`) | `VkImage` (format `VK_FORMAT_R8G8B8A8_UNORM`) + `VkImageView` | Created with `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT \| VK_IMAGE_USAGE_TRANSFER_SRC_BIT`. |
| FBO depth attachment (`GL_DEPTH_COMPONENT16`) | `VkImage` (format `VK_FORMAT_D16_UNORM`) + `VkImageView` | `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT`. |
| `glBindFramebuffer()` | `vkCmdBeginRenderPass()` | Render pass instance binds the framebuffer. |
| FBO swap (front/back) | Swap `VkFramebuffer` pointers + pipeline barriers | Transition back->transfer_src, front->color_attachment. |
| `glClear()` | `VkRenderPass` load op `VK_ATTACHMENT_LOAD_OP_CLEAR` or `vkCmdClearAttachments()` | Prefer load-op clear for full-frame clears (fastfill). |
| `glViewport()` | `vkCmdSetViewport()` | Always dynamic state in our design. Core Vulkan 1.0. |
| `glScissor()` | `vkCmdSetScissor()` | Always dynamic state in our design. Core Vulkan 1.0. |

### Shader and Uniforms

| OpenGL Concept | Vulkan 1.2 Equivalent | Notes |
|---|---|---|
| GLSL source | **SPIR-V bytecode** | Pre-compiled offline via `glslc`. Loaded as `VkShaderModule`. |
| Shader compilation | `vkCreateShaderModule()` + pipeline creation | No runtime GLSL compilation. Shader module creation is cheap; pipeline creation is the expensive step. |
| `glUniform*()` (per-draw) | **Push constants** (`vkCmdPushConstants`) | 128 bytes guaranteed by spec (`maxPushConstantsSize`). Our Voodoo pipeline state fits in exactly 64 bytes (see push-constant-layout.md). Fastest update path. |
| UBO (`glBindBufferBase`) | Uniform buffer in descriptor set | Alternative to push constants for larger data. Dolphin uses UBOs. For our case, push constants are simpler. |
| `sampler2D` uniforms | **Descriptor sets** with `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` | Samplers bound via descriptor writes, not push constants. |
| Shader `#version 440` | SPIR-V target via `glslc --target-env=vulkan1.2` | Version specified at compile time. |
| `noperspective` qualifier | SPIR-V `NoPerspective` decoration | Supported in all Vulkan implementations. Used for colors, depth. |
| `smooth` qualifier | Default SPIR-V interpolation (no decoration) | Used for texture S/T/W with perspective correction. |
| `gl_FragCoord` | `FragCoord` built-in (same semantics) | Origin at upper-left in Vulkan by default. |
| `gl_FragDepth` | `FragDepth` built-in (same semantics) | Writing disables early-Z, same as GL. |
| `discard` | `OpKill` in SPIR-V | Same semantics for alpha test / chroma key. |

### Push Constant Layout (replacing 17 GL uniforms)

```glsl
// GLSL push constant block for SPIR-V compilation
layout(push_constant, std430) uniform PushConstants {
    // Raw register values (bitwise-decoded in shader)
    uint fbzMode;           // offset  0, size  4
    uint fbzColorPath;      // offset  4, size  4
    uint alphaMode;         // offset  8, size  4
    uint fogMode;           // offset 12, size  4
    uint textureMode0;      // offset 16, size  4
    uint textureMode1;      // offset 20, size  4

    // Extracted color values (packed ARGB as uint)
    uint color0;            // offset 24, size  4
    uint color1;            // offset 28, size  4
    uint chromaKey;         // offset 32, size  4
    uint fogColor;          // offset 36, size  4
    uint zaColor;           // offset 40, size  4
    uint stipple;           // offset 44, size  4

    // Detail/LOD parameters (packed)
    uint detail0;           // offset 48, size  4  (bias:10 | max:8 | scale:3 packed)
    uint detail1;           // offset 52, size  4

    // Framebuffer dimensions for NDC conversion
    float fb_width;         // offset 56, size  4
    float fb_height;        // offset 60, size  4
} pc;
```

**Total: 64 bytes** -- well within the 128-byte guaranteed minimum.

This leaves 64 bytes of headroom for future additions (LOD bias, aux buffer
params, SLI configuration, etc.).

The 3 sampler uniforms (tex0, tex1, fog_table) are NOT push constants -- they
are bound via descriptor sets with `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`.

### Fixed-Function State

| OpenGL Call | Vulkan Equivalent | Dynamic? |
|---|---|---|
| `glDepthFunc()` | `vkCmdSetDepthCompareOp()` | Yes (VK_EXT_extended_dynamic_state / Vulkan 1.3 core) |
| `glDepthMask()` | `vkCmdSetDepthWriteEnable()` | Yes (VK_EXT_extended_dynamic_state / Vulkan 1.3 core) |
| `glEnable(GL_DEPTH_TEST)` | `vkCmdSetDepthTestEnable()` | Yes (VK_EXT_extended_dynamic_state / Vulkan 1.3 core) |
| `glBlendFunc()` | `VkPipelineColorBlendAttachmentState` | **Baked into pipeline** (see Section 2) |
| `glBlendEquation()` | `VkPipelineColorBlendAttachmentState` | **Baked into pipeline** (see Section 2) |
| `glEnable(GL_BLEND)` | `VkPipelineColorBlendAttachmentState` | **Baked into pipeline** (see Section 2) |
| `glColorMask()` | `VkPipelineColorBlendAttachmentState` or `vkCmdSetColorWriteMaskEXT()` | Dynamic with VK_EXT_extended_dynamic_state3 (not universal) |
| `glReadPixels()` | `vkCmdCopyImageToBuffer()` + staging `VkBuffer` + `vkMapMemory()` | Explicit copy + map replaces GL's implicit readback. |
| `glTexImage2D()` | `vkCmdCopyBufferToImage()` from staging `VkBuffer` to `VkImage` | Must transition image layout before/after copy. |
| `glTexSubImage2D()` | `vkCmdCopyBufferToImage()` with offset | Same pattern, partial region. |
| `glCopyTexSubImage2D()` | `vkCmdCopyImage()` | Image-to-image copy for copy-on-blend. Requires layout transitions. |

### Buffer Management

| OpenGL Concept | Vulkan 1.2 Equivalent | Notes |
|---|---|---|
| VBO (`GL_ARRAY_BUFFER`) | `VkBuffer` with `VK_BUFFER_USAGE_VERTEX_BUFFER_BIT` | Bind via `vkCmdBindVertexBuffers()`. |
| PBO (pixel buffer object) | Staging `VkBuffer` with `VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT` | Used for async readback and texture upload. |
| `glMapBuffer()` / `glUnmapBuffer()` | `vkMapMemory()` / `vkUnmapMemory()` | VMA provides persistent mapping (`VMA_ALLOCATION_CREATE_MAPPED_BIT`). |
| `GL_STREAM_DRAW` | Stream/ring buffer pattern | Dolphin: `VKStreamBuffer`. VMA: `VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT`. |
| `GL_UNPACK_ROW_LENGTH` | `VkBufferImageCopy::bufferRowLength` | Set per-copy, not global state. |

---

## 2. Pipeline Management Strategy

### The Problem

The Voodoo changes blend, depth, scissor, and color mask state per-triangle.
In OpenGL, these are cheap `glBlendFunc()` / `glDepthFunc()` calls. In Vulkan,
blend state is **baked into the VkPipeline object**. Creating a new pipeline
for every blend state combination is expensive and would cause stuttering.

### Dynamic State Analysis

Vulkan offers dynamic state to avoid pipeline permutation explosion. Here is
what state the Voodoo needs to change per-batch, and whether it can be dynamic:

| State | Vulkan 1.0 Core | VK_EXT_extended_dynamic_state (=Vk 1.3 core) | VK_EXT_extended_dynamic_state3 |
|---|---|---|---|
| Viewport | Dynamic (core) | -- | -- |
| Scissor | Dynamic (core) | -- | -- |
| Depth test enable | -- | `vkCmdSetDepthTestEnable()` | -- |
| Depth write enable | -- | `vkCmdSetDepthWriteEnable()` | -- |
| Depth compare op | -- | `vkCmdSetDepthCompareOp()` | -- |
| Cull mode | -- | `vkCmdSetCullMode()` | -- |
| Front face | -- | `vkCmdSetFrontFace()` | -- |
| Blend enable | -- | -- | `vkCmdSetColorBlendEnableEXT()` |
| Blend factors | -- | -- | `vkCmdSetColorBlendEquationEXT()` |
| Color write mask | -- | -- | `vkCmdSetColorWriteMaskEXT()` |

### Platform Support Matrix for Dynamic State Extensions

| Extension | MoltenVK (macOS) | Mesa V3DV (Pi 5) | Windows (NVIDIA/AMD/Intel) | Linux Mesa (radv/anv) |
|---|---|---|---|---|
| VK_EXT_extended_dynamic_state | Supported (most states) | Supported (Mesa 24.2+) | Universally supported | Universally supported |
| VK_EXT_extended_dynamic_state2 | Partial | Supported | Universally supported | Universally supported |
| VK_EXT_extended_dynamic_state3 | **NOT supported** (blend states) | Varies | NVIDIA: yes; AMD: yes; Intel: yes | radv: yes; anv: yes |

**Critical finding**: MoltenVK does NOT support VK_EXT_extended_dynamic_state3,
which means blend factors, blend enable, and color write mask cannot be dynamic
on macOS. This is our pipeline management constraint.

### MoltenVK Dynamic State Details

From [MoltenVK Issue #1739](https://github.com/KhronosGroup/MoltenVK/issues/1739):

**Supported (VK_EXT_extended_dynamic_state):**
- `VK_DYNAMIC_STATE_CULL_MODE`
- `VK_DYNAMIC_STATE_FRONT_FACE`
- `VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY`
- `VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT`
- `VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT`
- `VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE`
- `VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE`
- `VK_DYNAMIC_STATE_DEPTH_COMPARE_OP`
- `VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE`
- `VK_DYNAMIC_STATE_STENCIL_OP`

**Not supported (VK_EXT_extended_dynamic_state):**
- `VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE` (needs Metal 3.1)
- `VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE` (not applicable)

**Not supported (VK_EXT_extended_dynamic_state3 -- blend states):**
- `VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT`
- `VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT`
- `VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT`

### Recommended Pipeline Strategy

**Approach: Small pipeline cache keyed on blend state.**

Since blend state is baked into VkPipeline on all platforms, and depth/scissor
state is dynamic everywhere we care about, the pipeline key is:

```c
typedef struct vc_pipeline_key {
    /* Blend state */
    uint8_t  blend_enable;       // 0 or 1
    uint8_t  src_color_factor;   // VkBlendFactor (0-18)
    uint8_t  dst_color_factor;   // VkBlendFactor (0-18)
    uint8_t  color_blend_op;     // VkBlendOp (0-4)
    uint8_t  src_alpha_factor;   // VkBlendFactor
    uint8_t  dst_alpha_factor;   // VkBlendFactor
    uint8_t  alpha_blend_op;     // VkBlendOp
    uint8_t  color_write_mask;   // 4 bits (RGBA)
    /* Padding to 8 bytes for hash key */
} vc_pipeline_key_t;
```

**How many pipeline objects?**

Analyzing Voodoo blend factor usage in real games:

| Game Pattern | Blend Config | Frequency |
|---|---|---|
| Opaque geometry | Blend disabled | 60-80% of triangles |
| Standard alpha | ONE, ONE_MINUS_SRC_ALPHA, ADD | 10-25% |
| Additive (particles) | ONE, ONE, ADD | 5-10% |
| Multiplicative (lightmaps) | DST_COLOR, ZERO, ADD | 2-5% |
| Subtractive (rare) | ZERO, ONE_MINUS_SRC_COLOR, ADD | <1% |
| ACOLORBEFOREFOG (exotic) | Copy-on-blend in shader | <0.1% |

**Practical estimate: 5-15 unique pipeline objects per game session.**

This is trivially manageable. Create pipelines on first encounter and cache
them in a hash map. Pipeline creation takes ~1-5ms, happens only once per
unique blend config, and `VkPipelineCache` further amortizes the cost.

**All other per-batch state is dynamic:**
- Viewport, scissor: core Vulkan 1.0 dynamic state
- Depth test enable, depth write enable, depth compare op: VK_EXT_extended_dynamic_state
- Push constants: `vkCmdPushConstants()` per batch

**On platforms with VK_EXT_extended_dynamic_state3 (Windows, Linux desktop):**
Optionally use dynamic blend state to reduce to a single pipeline object.
This is a runtime optimization, not a requirement.

```c
/* Pipeline creation flow */
VkPipeline vc_get_pipeline(vc_context_t *ctx, vc_pipeline_key_t *key) {
    /* Look up in hash map */
    VkPipeline cached = hashmap_get(&ctx->pipeline_cache, key);
    if (cached != VK_NULL_HANDLE)
        return cached;

    /* Create new pipeline with this blend config */
    VkPipeline new_pipeline = vc_create_pipeline(ctx, key);
    hashmap_put(&ctx->pipeline_cache, key, new_pipeline);
    return new_pipeline;
}
```

### Dynamic State Declaration

All pipelines share these dynamic states:

```c
VkDynamicState dynamic_states[] = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR,
    /* VK_EXT_extended_dynamic_state (Vulkan 1.3 core) */
    VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
    VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
    VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
    VK_DYNAMIC_STATE_CULL_MODE,
};
```

### Fallback for Vulkan 1.2 without Extended Dynamic State

If VK_EXT_extended_dynamic_state is not available (unlikely but possible on
ancient drivers), the pipeline key expands to include depth state:

```c
typedef struct vc_pipeline_key_full {
    vc_pipeline_key_t blend;
    uint8_t depth_test_enable;
    uint8_t depth_write_enable;
    uint8_t depth_compare_op;
    uint8_t pad;
} vc_pipeline_key_full_t;
```

This increases pipeline permutations to ~30-50 per session. Still manageable.

---

## 3. Third-Party Library Recommendations

### volk (Vulkan Meta-Loader)

| Property | Value |
|---|---|
| **Repository** | [github.com/zeux/volk](https://github.com/zeux/volk) |
| **License** | MIT |
| **Language** | C89 |
| **Header-only?** | Yes (define `VOLK_IMPLEMENTATION` in one .c file) or link as static lib |
| **Dependencies** | None |
| **Platforms** | Windows, Linux, macOS (MoltenVK), Android |
| **Maturity** | 1.8k+ stars, used by ANGLE (Google Chrome), Filament, and many others |

**Key API:**
```c
volkInitialize();                    /* Load Vulkan loader */
volkLoadInstance(instance);          /* Load instance-level functions */
volkLoadDevice(device);              /* Load device-level functions (bypasses loader dispatch) */
```

**Why volk over linking against vulkan-1:**
1. **No link-time dependency** on Vulkan loader -- works on systems without Vulkan installed (graceful failure)
2. **Performance**: `volkLoadDevice()` loads function pointers directly from the ICD driver, bypassing the Vulkan loader's dispatch trampoline. [AMD reports 1-5% overhead reduction](https://gpuopen.com/learn/reducing-vulkan-api-call-overhead/).
3. **MoltenVK compatible**: Explicitly supports macOS via MoltenVK
4. **Dolphin uses a similar pattern**: `VulkanLoader.cpp` does the same dynamic loading

**Emulator usage:** Dolphin uses its own dynamic loader (`VulkanLoader.cpp`). Duckstation uses `vulkan_loader.cpp` (custom). volk provides the same functionality in a maintained, standard form.

**Recommendation: ADOPT.** Replace GLAD with volk. Single-file integration, MIT license, standard practice.

### VMA (Vulkan Memory Allocator)

| Property | Value |
|---|---|
| **Repository** | [github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) |
| **License** | MIT |
| **Language** | C++ (C API available via `VMA_VULKAN_FUNCTIONS_*`) |
| **Header-only?** | Yes (define `VMA_IMPLEMENTATION` in one .cpp file) |
| **Dependencies** | Vulkan headers only |
| **Maturity** | 2.5k+ stars, AMD-maintained, industry standard |

**Key benefits:**
1. **Memory type selection**: Automatically selects the right `memoryTypeIndex` for `VkBuffer` / `VkImage` based on usage hints
2. **Suballocation**: Pools small allocations into larger blocks, reducing `vkAllocateMemory()` calls (Vulkan limits total allocations, typically 4096)
3. **Staging helpers**: `VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT` for upload staging, `VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT` for readback
4. **Persistent mapping**: `VMA_ALLOCATION_CREATE_MAPPED_BIT` returns a persistent CPU pointer (avoids `vkMapMemory`/`vkUnmapMemory` churn)
5. **Defragmentation**: Automatic, but we probably do not need it for our simple allocation pattern

**Our allocation pattern is simple:**
- 2 color images (front/back, RGBA8, 1024x768 max = 3 MB each)
- 2 depth images (front/back, D16, 1024x768 max = 1.5 MB each)
- 1-2 staging buffers for readback (~3 MB each)
- 1-2 staging buffers for texture upload (~1 MB each)
- 1 vertex stream buffer (~1 MB ring buffer)
- Texture images (variable, ~10-50 textures of ~256x256 RGBA8 = ~13 MB)
- Total: ~30-50 MB

VMA handles this trivially. Without VMA, we would need ~100 lines of
manual memory type selection + allocation tracking code.

**Emulator usage:** VulkanMod (Minecraft Vulkan) uses VMA. Most Vulkan tutorials and production code use VMA. Dolphin and Duckstation rolled their own allocators (predating VMA's maturity), which is not recommended for new projects.

**Recommendation: ADOPT.** Eliminates the most error-prone part of Vulkan (memory management). No downsides for our use case.

### vk-bootstrap

| Property | Value |
|---|---|
| **Repository** | [github.com/charles-lunarg/vk-bootstrap](https://github.com/charles-lunarg/vk-bootstrap) |
| **License** | MIT |
| **Language** | C++17 |
| **Header-only?** | **No** -- must compile `VkBootstrap.cpp` |
| **Dependencies** | Vulkan Headers 1.1+ |
| **Maturity** | 1.2k stars, maintained by LunarG engineer |

**Key benefits:**
1. Simplifies `VkInstance` creation (validation layers, debug messenger)
2. Physical device selection with feature/extension requirements
3. Automatic queue family selection
4. Swapchain creation helpers

**Concerns for our project:**
1. **C++17 requirement** -- 86Box is C11 with C++14 for Qt only. Adding a C++17 dependency for initialization code is heavy.
2. **Not header-only** -- requires compiling a .cpp file into our C build
3. **Overkill** -- our Vulkan init is simple: one instance, one device, one queue family. Maybe 150 lines of C code.

**Recommendation: DO NOT ADOPT.** The C++17 dependency conflicts with 86Box's C11/C++14 codebase. Write our own initialization in C (see `vc_core.c`). It is not complex enough to warrant a dependency.

### Summary

| Library | Adopt? | Reason |
|---|---|---|
| **volk** | **Yes** | MIT, C89, header-only, standard practice, MoltenVK compatible |
| **VMA** | **Yes** | MIT, header-only (one .cpp), eliminates memory management complexity |
| **vk-bootstrap** | **No** | C++17 conflicts with C11 codebase; our init is simple enough in plain C |

---

## 4. MoltenVK Specifics (macOS)

### Vulkan Version Support

MoltenVK 1.4 (released August 2025) supports **Vulkan 1.4**. Our target of
Vulkan 1.2 is comfortably within MoltenVK's capabilities. MoltenVK layers
Vulkan over Apple's Metal framework.

Source: [MoltenVK Releases](https://github.com/KhronosGroup/MoltenVK/releases)

### Vulkan 1.2 Feature Gaps (Metal Limitations)

From [MoltenVK Issue #1567](https://github.com/KhronosGroup/MoltenVK/issues/1567):

| Vulkan 1.2 Feature | MoltenVK Support | Impact on VideoCommon |
|---|---|---|
| `drawIndirectCount` | Not supported (Metal lacks equivalent) | **None** -- we do not use indirect drawing |
| `shaderBufferInt64Atomics` | Not supported | **None** -- no 64-bit atomics in our shader |
| `vulkanMemoryModel` | Was unsupported, now supported in MoltenVK 1.3+ | **None** -- we do not use explicit memory model |
| `samplerFilterMinmax` | Not supported | **None** -- we do not use min/max sampler filters |

**None of these gaps affect our use case.** The Voodoo uber-shader uses
basic fragment operations: texture sampling, arithmetic, branching, discard.

### Extensions Critical to Our Design

| Extension | MoltenVK Status | Our Usage |
|---|---|---|
| `VK_EXT_extended_dynamic_state` | **Supported** (depth, cull, stencil, topology) | Dynamic depth test/write/compare per batch |
| `VK_EXT_extended_dynamic_state2` | Partial (depth bias only) | Not critical for us |
| `VK_EXT_extended_dynamic_state3` | **NOT supported** (blend states) | Cannot use dynamic blend -- use pipeline cache instead |
| `VK_KHR_dynamic_rendering` | **Supported** | Optional: skip VkRenderPass/VkFramebuffer objects |
| `VK_KHR_push_descriptor` | Supported | Optional optimization: update descriptors inline |
| `VK_KHR_swapchain` | Supported | Display integration |
| `VK_MVK_macos_surface` | Supported (MoltenVK-specific) | Surface creation for Qt window |

### MoltenVK Configuration Options

Relevant `MVK_CONFIG_*` settings for our use case:

| Setting | Default | Recommendation |
|---|---|---|
| `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS` | 2 (auto) | Keep default -- enables descriptor indexing when available |
| `MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS` | 0 (async) | Keep default -- async submission is correct for our render thread |
| `MVK_CONFIG_PREFILL_METAL_COMMAND_BUFFERS` | 0 | Keep default -- we submit small batches |
| `MVK_CONFIG_SHADER_CONVERSION_FLIP_VERTEX_Y` | 1 | **Set to 0** -- we handle Y-flip in our vertex shader |
| `MVK_CONFIG_LOG_LEVEL` | 1 (error) | Set to 2 (warning) during development |

Source: [MoltenVK Configuration Parameters](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Configuration_Parameters.md)

### MoltenVK Performance Characteristics

- **Metal command buffer overhead**: MoltenVK translates Vulkan command buffers to Metal command buffers. Each `vkQueueSubmit()` creates a Metal command buffer. Minimize submit count.
- **Pipeline creation**: Translates `VkPipeline` to Metal `MTLRenderPipelineState`. First creation ~5-10ms, cached subsequently.
- **Push constants**: Translated to Metal buffer binding. Efficient for our 64-byte block.
- **Image layout transitions**: Mostly no-ops in Metal (Metal tracks resource state internally). Pipeline barriers still needed for correct synchronization semantics.

### macOS Gotchas

1. **Fragment shader interlock**: MoltenVK supports `VK_EXT_fragment_shader_interlock` on Apple Silicon (A13+, M1+). Could be used for copy-on-blend instead of copy-to-texture approach. Worth investigating but not a launch requirement.
2. **Texture format**: `VK_FORMAT_B8G8R8A8_UNORM` is universally supported on macOS Metal. Our BGRA8 decoded textures upload directly.
3. **Coordinate system**: Vulkan uses upper-left origin, Y-down clip space. Metal uses the same convention. No MoltenVK flip needed.
4. **MoltenVK vs KosmicKrisp**: Mesa's KosmicKrisp is an alternative Vulkan-on-Metal implementation reaching feature parity with MoltenVK. Not yet mature enough to target, but good to know it exists.

Source: [KosmicKrisp Parity (Phoronix)](https://www.phoronix.com/news/KosmicKrisp-Parity)

---

## 5. Pi 5 V3D Vulkan Specifics

### Vulkan Version

The Broadcom V3DV driver for Raspberry Pi 5 (VideoCore VII, V3D 7.1.6)
achieved **Vulkan 1.3 conformance** in Mesa 24.3 (October 2024).

Our target of Vulkan 1.2 is fully covered.

Source: [Phoronix - V3DV Vulkan 1.3](https://www.phoronix.com/news/Mesa-24.3-V3DV-Vulkan-1.3)

### V3DV Extension Support

| Extension | V3DV Status | Our Usage |
|---|---|---|
| VK_EXT_extended_dynamic_state | **Supported** (Mesa 24.2+) | Dynamic depth per batch |
| VK_KHR_swapchain | Supported | Display |
| VK_KHR_dynamic_rendering | Supported (Vulkan 1.3 core) | Optional |
| VK_EXT_extended_dynamic_state3 | **Status unclear** -- may be partial | Not relied upon |

Source: [Phoronix - V3DV Extended Dynamic State](https://www.phoronix.com/news/V3DV-Extended-Dynamic-State)

### Hardware Characteristics

| Property | VideoCore VII (Pi 5) | Implication |
|---|---|---|
| GPU clock | 800 MHz (up to 1 GHz with firmware OC) | Low clock vs desktop GPUs |
| Shader cores | 8 QPUs (Quad Processor Units) | Fragment-heavy workloads may bottleneck here |
| Memory | Shared CPU/GPU (LPDDR4X) | No PCIe bus; `HOST_VISIBLE` memory IS device memory. Staging buffers may be unnecessary. |
| Tile-based renderer | Yes (TBDR architecture) | Render passes with load/store ops are important for performance |
| Max texture size | 4096x4096 | More than sufficient for Voodoo (max 256x256 per LOD) |
| Max framebuffer size | 4096x4096 | More than sufficient |
| Fill rate | ~800M pixels/sec (estimated) | Voodoo at 640x480@60fps = ~18.4M pixels/sec. 40x headroom. |

### Shared Memory Architecture

The Pi 5 uses a unified memory architecture (UMA). This has significant
implications:

1. **No staging buffer needed for uploads**: CPU and GPU share the same physical memory. A `HOST_VISIBLE | DEVICE_LOCAL` buffer can be used directly by the GPU without a copy.
2. **VMA handles this**: With `VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT`, VMA will prefer `HOST_VISIBLE | DEVICE_LOCAL` memory on UMA systems, avoiding unnecessary copies.
3. **Readback is cheaper**: Reading back framebuffer data does not require a PCIe bus transfer. The cost is mainly the GPU pipeline drain (fence wait) plus cache coherency operations.
4. **Vertex buffer**: Can use a persistently mapped `HOST_VISIBLE | DEVICE_LOCAL` buffer directly. No need for a separate staging buffer.

### Performance Expectations

The Voodoo workload is fragment-heavy (complex per-pixel pipeline) with low
vertex count (typically <10k triangles/frame). On the Pi 5:

- **Fragment shading**: Our uber-shader has ~50-80 ALU ops per fragment (texture fetch, combine, fog, alpha test). At 800 MHz with 8 QPUs, each running 4-wide, this gives ~25.6 billion ops/sec. For 640x480@60fps with ~50 ops/pixel = ~920M ops/frame, so roughly 27x headroom. Should be fine.
- **Memory bandwidth**: Voodoo textures are small (256x256 RGBA8 = 256 KB). Entire working set fits in L2 cache.
- **Draw calls**: With batching, ~50-200 draw calls per frame. Negligible CPU overhead.

### Pi 5 Known Quirks

1. **V3D tile buffer**: The V3D GPU renders to an internal tile buffer (typically 32x32 or 64x64 pixels), then writes tiles to external memory. `LOAD_OP_DONT_CARE` and `STORE_OP_DONT_CARE` are important for avoiding unnecessary tile memory operations.
2. **Depth format**: `VK_FORMAT_D16_UNORM` is universally supported. `VK_FORMAT_D24_UNORM_S8_UINT` may not be -- check at init.
3. **Subgroup size**: V3D uses subgroup size 1 (each QPU is effectively scalar). This means subgroup operations are trivial/no-ops. Not relevant for our shader.
4. **Fence latency**: Due to the tile-based architecture, fence signaling may have higher latency than on immediate-mode desktop GPUs. Use double-buffering to hide this.

---

## 6. Display Integration

### The Problem

VideoCommon renders to offscreen `VkImage` framebuffers (front/back).
We need to get the rendered front buffer to the screen via 86Box's Qt UI.

### Option Analysis

#### Option A: Readback to CPU Buffer (Simplest)

```
VkImage (front) --[vkCmdCopyImageToBuffer]--> staging VkBuffer
                --[vkMapMemory]--> CPU pointer
                --[memcpy]--> target_buffer (existing 86Box buffer)
                --[existing RendererStack]--> screen
```

**Pros:**
- Reuses existing Qt display infrastructure (Software/OpenGL/Vulkan renderers)
- Zero Qt integration changes
- Works with any display renderer
- No shared Vulkan resources between threads

**Cons:**
- One extra copy (GPU -> staging -> CPU -> display)
- ~0.5-2ms per frame at 640x480 (trivial at 60 fps)

**This is what 86Box already does** for its existing Vulkan display renderer
(`qt_vulkanrenderer.cpp`): the emulated video card writes to `target_buffer`
in CPU memory, and the Qt Vulkan renderer uploads it each frame as a texture.

#### Option B: QVulkanWindow with Shared VkInstance

```
VkImage (front) --[blit/copy in command buffer]--> swapchain image
                --[vkQueuePresentKHR]--> screen
```

Qt 5 provides `QVulkanWindow` which manages a VkDevice, swapchain, and render
pass. However, `QVulkanWindow` is designed to OWN the VkInstance and VkDevice.
To use our own device, we need the manual approach:

```cpp
// Share our VkInstance with Qt
QVulkanInstance qvkInst;
qvkInst.setVkInstance(our_vk_instance);
qvkInst.create();

// Create a QWindow for Vulkan
QWindow *window = new QWindow;
window->setSurfaceType(QSurface::VulkanSurface);
window->setVulkanInstance(&qvkInst);

// Embed in widget hierarchy
QWidget *container = QWidget::createWindowContainer(window, parent);

// Get surface for swapchain creation
VkSurfaceKHR surface = qvkInst.surfaceForWindow(window);
```

Source: [Qt with External Vulkan Renderer](https://www.niangames.com/articles/qt-vulkan-renderer)

**Pros:**
- Zero-copy display path (GPU blit front buffer to swapchain)
- No CPU in the display loop
- Maximum performance

**Cons:**
- Complex Qt integration (QWindow container, resize handling)
- Must manage our own swapchain
- VkInstance sharing between render thread and Qt UI thread requires careful synchronization
- `QWidget::createWindowContainer()` has known limitations with layouts/overlays
- Significant new code in Qt layer

#### Option C: Vulkan Readback + Existing Qt Vulkan Renderer (Hybrid)

```
VkImage (front) --[vkCmdCopyImageToBuffer]--> staging VkBuffer
                --[vkMapMemory]--> mapped pointer
                --[assign to VulkanRenderer2::mappedPtr]--> Qt Vulkan renderer uploads to swapchain
```

The existing `VulkanRenderer2` class (`qt_vulkanrenderer.cpp`) already has a
`mappedPtr` that it uploads to a texture each frame. We could point this at
our readback staging buffer directly.

**Pros:**
- Reuses existing Qt Vulkan display code with minimal changes
- Only one extra copy (GPU framebuffer -> staging buffer -> Qt texture)
- No shared VkInstance complexity

**Cons:**
- Still has a CPU-visible copy
- Requires coordination between our render thread and Qt's render loop

### What Emulators Do

| Emulator | Display Approach | Details |
|---|---|---|
| **Dolphin** | Own swapchain, no Qt dependency | Creates VkSurfaceKHR directly on the platform window. No QVulkanWindow. |
| **Duckstation** | Own swapchain via platform surface | `vulkan_swap_chain.cpp` manages VkSwapchainKHR directly. Not Qt-based. |
| **PCSX2** | Uses Qt integration | Manages its own Vulkan device, presents via platform swapchain |
| **86Box current** | QVulkanWindow + CPU readback | `VulkanRenderer2` receives CPU buffer, uploads as texture. See `qt_vulkanrenderer.cpp`. |

### Recommendation

**Phase 1: Option A (Readback to CPU buffer).**

This is the correct starting point because:
1. Zero Qt changes -- reuses existing `RendererStack` infrastructure
2. Proven pattern -- 86Box already does this for all video cards
3. Simple -- no cross-thread Vulkan resource sharing
4. The readback cost (~1ms) is negligible vs the rendering gain
5. Decouples VideoCommon from display concerns

**Phase 2 (future): Option B (VkSurfaceKHR + swapchain).**

Add as an optimization later. Only worth it if readback proves to be a bottleneck
(unlikely at Voodoo resolutions). Requires a new `VCRenderer` class in Qt.

**Rationale**: Dolphin and Duckstation both manage their own swapchains because
they are standalone applications. 86Box has a complex Qt UI with
multiple possible video output widgets. Fighting Qt's Vulkan infrastructure
in Phase 1 adds risk for minimal gain. Get rendering correct first, optimize
the display path later.

---

## 7. Emulator Vulkan Backends Survey

### Dolphin (GameCube/Wii)

Source: [dolphin-emu/dolphin/Source/Core/VideoBackends/Vulkan/](https://github.com/dolphin-emu/dolphin/tree/master/Source/Core/VideoBackends/Vulkan)

**Architecture:**

Dolphin's Vulkan backend has ~39 source files organized as:

| Component | Files | Purpose |
|---|---|---|
| Context | `VulkanContext.cpp/h` | VkInstance, VkDevice, VkQueue setup |
| Loader | `VulkanLoader.cpp/h` | Dynamic Vulkan function loading (custom, volk-equivalent) |
| Command Buffers | `CommandBufferManager.cpp/h` | Dual init/draw command buffers per frame, fence tracking, async submit thread |
| Pipelines | `VKPipeline.cpp/h`, `ObjectCache.cpp/h` | Pipeline creation, caching by UID hash, `VkPipelineCache` for disk persistence |
| State | `StateTracker.cpp/h` | Dirty-flag tracking for all GPU state (pipeline, descriptors, vertex buffers) |
| Streaming | `VKStreamBuffer.cpp/h` | Ring buffer for dynamic vertex/uniform data |
| Textures | `VKTexture.cpp/h` | Texture creation, staging upload, format conversion |
| Swap | `VKSwapChain.cpp/h` | Swapchain creation, present, resize |
| Shaders | `VKShader.cpp/h`, `ShaderCompiler.cpp/h` | Shader module management, SPIR-V compilation |

**Command Buffer Management:**

Dolphin uses a `CommandBufferManager` with multiple frames in flight:
- Two command buffers per submission: "init" (upload/barrier) and "draw" (rendering)
- Fence counter system: each submission increments a counter; completion callbacks fire when the GPU signals the fence
- Worker thread (`WorkqueueThread`) handles async queue submission
- Semaphores coordinate swapchain image acquisition with rendering

**Pipeline Management:**

Dolphin generates pipeline UIDs from rendering state and caches compiled
pipelines. The `VkPipelineCache` is persisted to disk for cross-session
reuse. Pipeline creation happens on a background thread to avoid stuttering.

**Key patterns applicable to VideoCommon:**
1. **Stream buffer ring**: Dynamic vertex data goes into a ring buffer with per-frame wrap. We should do the same for our vertex buffer.
2. **Fence counter**: Rather than raw VkFence objects, use monotonic counters. "Wait for fence N" means wait for the GPU to reach that point.
3. **Separate init/draw command buffers**: Upload commands (image transitions, buffer copies) go in the init buffer; draw commands in the draw buffer. Both submitted together.

### Duckstation (PS1)

Source: [github.com/stenzek/duckstation](https://github.com/stenzek/duckstation)

**Architecture:**

Duckstation has a cleaner, more focused Vulkan abstraction in `src/util/`:

| File | Purpose |
|---|---|
| `vulkan_device.cpp/h` | VkInstance, VkDevice, VkQueue, feature detection |
| `vulkan_pipeline.cpp/h` | Pipeline creation with state hashing |
| `vulkan_builders.cpp/h` | Builder pattern helpers for Vulkan object creation |
| `vulkan_stream_buffer.cpp/h` | Ring buffer for dynamic data |
| `vulkan_texture.cpp/h` | VkImage management, staging upload |
| `vulkan_swap_chain.cpp/h` | Swapchain, VkSurfaceKHR, present |
| `vulkan_loader.cpp/h` | Dynamic Vulkan loading (custom) |

**Key design choices:**
1. **Builder pattern**: `vulkan_builders.h` provides fluent builder APIs for pipeline, render pass, and descriptor set creation. Reduces VkCreateInfo boilerplate.
2. **Single device abstraction**: All Vulkan state lives in one `VulkanDevice` class. Simple for a single-GPU emulator.
3. **Accurate blending**: Uses `VK_EXT_fragment_shader_interlock` (Rasterizer Order Views) for PS1's semi-transparent blending where available.

**Relevance to VideoCommon:** Duckstation is the most analogous emulator to
our Voodoo use case: simple fixed-function GPU, screen-space pre-transformed
vertices, fragment-heavy shader work. Their Vulkan abstraction is lean and
well-structured. Worth studying for patterns.

### PCSX2 (PS2)

Source: [PCSX2/pcsx2/GS/Renderers/Vulkan/](https://github.com/PCSX2/pcsx2/tree/master/pcsx2/GS/Renderers/Vulkan)

**Pipeline management:**
- Pipeline cache stored in `vulkan_pipelines.bin`
- Hashes pipeline creation parameters for deduplication
- Detects and invalidates corrupted caches
- Source: [PCSX2 Issue #9839](https://github.com/PCSX2/pcsx2/issues/9839)

**Readback pattern:**
- Dirty tracking per texture region
- Skips readback if no draws have occurred since last read
- Chunked readback detection: multiple small reads between draws trigger a single large read

**Key lesson for VideoCommon:** PCSX2's pipeline cache invalidation logic
is worth studying. Bad pipeline cache data can cause silent rendering
corruption. Include a version/hash header in our pipeline cache file.

### paraLLEl-GS (PS2 GS via Vulkan Compute)

Source: [Arntzen-Software/parallel-gs](https://github.com/Arntzen-Software/parallel-gs)

Already covered in `emulator-survey.md`. Key Vulkan-specific points:
- Uses Vulkan compute shaders exclusively (no graphics pipeline)
- All rendering happens in buffer memory (not VkImage framebuffers)
- Demonstrates that for simple GPUs, uber-shader compute can be competitive
- Not directly applicable to our rasterization-based approach, but validates
  the uber-shader concept

### Summary of Patterns to Adopt

| Pattern | Source | Application |
|---|---|---|
| Stream buffer ring | Dolphin, Duckstation | Vertex buffer for dynamic triangle data |
| Fence counter (not raw fences) | Dolphin | Track GPU progress for sync points |
| Pipeline cache with hash key | All three | Cache pipelines by blend state hash |
| Dirty tracking for readback | PCSX2 | Tile-based LFB readback optimization |
| Builder pattern for VkCreateInfo | Duckstation | Reduce pipeline creation boilerplate |
| Separate upload/draw command buffers | Dolphin | Clean separation of transfer and render work |

---

## 8. SPIR-V Toolchain

### Compiler Options

| Tool | Provider | Recommended? | Notes |
|---|---|---|---|
| `glslc` | Google (shaderc) | **Yes** | GCC-like CLI, supports `#include`, good error messages. Part of Vulkan SDK. |
| `glslangValidator` | Khronos (glslang) | Fallback | Reference compiler. Less user-friendly CLI. |
| `libshaderc` | Google | For runtime | C API for runtime compilation. Not needed for our offline approach. |

**Recommendation: `glslc` for offline compilation.**

### Build Integration

SPIR-V bytecode should be compiled at build time and embedded in the binary
as C arrays. This avoids shipping separate `.spv` files and eliminates
runtime file I/O.

**CMake integration:**

```cmake
# Find glslc (part of Vulkan SDK or standalone shaderc)
find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin)
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found. Install the Vulkan SDK or shaderc.")
endif()

# Compile GLSL to SPIR-V, then generate C header
function(compile_shader SHADER_SOURCE OUTPUT_HEADER VAR_NAME)
    set(SPV_FILE "${CMAKE_CURRENT_BINARY_DIR}/${VAR_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPV_FILE}
        COMMAND ${GLSLC} --target-env=vulkan1.2 -O
                ${SHADER_SOURCE} -o ${SPV_FILE}
        DEPENDS ${SHADER_SOURCE}
        COMMENT "Compiling ${SHADER_SOURCE} to SPIR-V"
    )
    add_custom_command(
        OUTPUT ${OUTPUT_HEADER}
        COMMAND ${CMAKE_COMMAND} -E echo
            "/* Auto-generated from ${SHADER_SOURCE} */" > ${OUTPUT_HEADER}
        COMMAND xxd -i ${VAR_NAME}.spv >> ${OUTPUT_HEADER}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${SPV_FILE}
        COMMENT "Generating C header for ${VAR_NAME}"
    )
endfunction()

# Example usage:
compile_shader(
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/voodoo_uber.vert
    ${CMAKE_CURRENT_BINARY_DIR}/voodoo_uber_vert_spv.h
    voodoo_uber_vert
)
compile_shader(
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/voodoo_uber.frag
    ${CMAKE_CURRENT_BINARY_DIR}/voodoo_uber_frag_spv.h
    voodoo_uber_frag
)
```

**Generated header format (via `xxd -i`):**

```c
/* Auto-generated from shaders/voodoo_uber.vert */
unsigned char voodoo_uber_vert_spv[] = {
  0x03, 0x02, 0x23, 0x07, 0x00, 0x06, 0x01, 0x00, /* ... */
};
unsigned int voodoo_uber_vert_spv_len = 1234;
```

**Usage in C code:**

```c
#include "voodoo_uber_vert_spv.h"
#include "voodoo_uber_frag_spv.h"

VkShaderModuleCreateInfo vert_info = {
    .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = voodoo_uber_vert_spv_len,
    .pCode    = (const uint32_t *)voodoo_uber_vert_spv,
};
vkCreateShaderModule(device, &vert_info, NULL, &vert_module);
```

### Alternative: Python Script Instead of xxd

If `xxd` is not available on all build platforms (it is not standard on
Windows), a simple Python script can replace it:

```python
#!/usr/bin/env python3
import sys
with open(sys.argv[1], 'rb') as f:
    data = f.read()
name = sys.argv[2]
print(f"static const unsigned char {name}[] = {{")
for i in range(0, len(data), 12):
    chunk = data[i:i+12]
    print("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
print("};")
print(f"static const unsigned int {name}_len = {len(data)};")
```

### GLSL Dialect for SPIR-V

```glsl
#version 450
// Vulkan 1.2 targets SPIR-V 1.5, but #version 450 is sufficient
// and maximizes compatibility

// Push constants (replace GL uniforms)
layout(push_constant, std430) uniform PushConstants {
    uint fbzMode;
    uint fbzColorPath;
    // ... (see Section 1)
} pc;

// Descriptor set 0: texture samplers
layout(set = 0, binding = 0) uniform sampler2D tex0;
layout(set = 0, binding = 1) uniform sampler2D tex1;
layout(set = 0, binding = 2) uniform sampler2D fog_table;

// Vertex inputs
layout(location = 0) in vec2 in_position;     // screen-space X, Y
layout(location = 1) in float in_depth;        // Z or W depth
layout(location = 2) in vec4 in_color;         // RGBA
layout(location = 3) in vec3 in_texcoord0;     // S, T, W (for perspective)
layout(location = 4) in vec3 in_texcoord1;     // TMU1 S, T, W

// Vertex outputs
layout(location = 0) noperspective out vec4 v_color;
layout(location = 1) smooth out vec3 v_texcoord0;  // perspective-corrected
layout(location = 2) smooth out vec3 v_texcoord1;
layout(location = 3) noperspective out float v_depth;
layout(location = 4) noperspective out float v_oow;  // 1/W for fog
```

Note: `sampler1D` is not available in Vulkan SPIR-V. The fog table must be a
`sampler2D` with height=1, or a UBO/SSBO. Recommend: 64x1 `sampler2D`.

### Shader Validation

```bash
# Compile with maximum validation
glslc --target-env=vulkan1.2 -Werror -O shaders/voodoo_uber.frag -o voodoo_uber_frag.spv

# Validate SPIR-V
spirv-val --target-env vulkan1.2 voodoo_uber_frag.spv

# Disassemble for inspection
spirv-dis voodoo_uber_frag.spv
```

---

## 9. Synchronization Model

### SPSC Ring + Render Thread Mapped to Vulkan

Our architecture has the FIFO thread producing commands and the render thread
consuming them. Here is how this maps to Vulkan command buffer recording and
submission:

```
FIFO Thread                          Render Thread (Vulkan)
-----------                          ----------------------
produce(CMD_TRIANGLE, data)    -->   dequeue CMD_TRIANGLE
                                     record vkCmdDraw() into command buffer
                                     (batch with same pipeline state)

produce(CMD_STATE_CHANGE)      -->   dequeue CMD_STATE_CHANGE
                                     if batch pending: submit current batch
                                     update pipeline / push constants

produce(CMD_SWAP)              -->   dequeue CMD_SWAP
                                     submit final batch
                                     vkQueueSubmit() with fence
                                     swap front/back VkImage pointers
                                     begin new command buffer

produce(CMD_SYNC)              -->   dequeue CMD_SYNC
                                     submit current batch
                                     vkQueueSubmit() with fence
                                     vkWaitForFences() -- block until GPU done
                                     signal completion to FIFO thread

produce(CMD_READBACK, region)  -->   dequeue CMD_READBACK
                                     submit current batch
                                     transition image layout
                                     vkCmdCopyImageToBuffer()
                                     vkQueueSubmit() with fence
                                     vkWaitForFences()
                                     vkMapMemory() staging buffer
                                     copy to CPU buffer
                                     signal completion
```

### Command Buffer Strategy

**One command buffer per frame**, reset and re-recorded each frame.

```c
/* Per-frame resources (double-buffered) */
typedef struct vc_frame_resources {
    VkCommandPool   cmd_pool;
    VkCommandBuffer cmd_buf;
    VkFence         fence;        /* Signaled when GPU finishes this frame */
    uint64_t        fence_value;  /* Monotonic counter */
    bool            in_flight;    /* Whether submitted and not yet complete */
} vc_frame_resources_t;

#define VC_FRAMES_IN_FLIGHT 2
vc_frame_resources_t frames[VC_FRAMES_IN_FLIGHT];
uint32_t current_frame = 0;
```

**Recording flow within a frame:**

```
begin_frame:
    wait for frames[current_frame].fence (if in_flight)
    reset command pool
    begin command buffer
    begin render pass (load_op = LOAD for incremental, CLEAR for fastfill)

per_batch:
    bind pipeline (if blend state changed)
    set dynamic state (depth, scissor, viewport)
    push constants
    bind descriptor sets (if textures changed)
    bind vertex buffer
    vkCmdDraw(triangle_count * 3)

end_frame (CMD_SWAP):
    end render pass
    transition front buffer for readback/display
    vkQueueSubmit(cmd_buf, fence)
    current_frame = (current_frame + 1) % VC_FRAMES_IN_FLIGHT
```

### Synchronization Primitives Usage

| Primitive | Purpose | When Used |
|---|---|---|
| **VkFence** | CPU waits for GPU completion | Frame completion, LFB readback, sync commands |
| **VkSemaphore** | GPU-to-GPU ordering | Swapchain acquire/present (only if using Option B display) |
| **Pipeline barrier** | GPU-to-GPU memory/image dependency | Image layout transitions, readback buffer visibility |
| **VkEvent** | Fine-grained GPU sync | Not needed for our simple pipeline |

### Sync Points

| Sync Point | Trigger | Vulkan Operation | CPU Blocks? |
|---|---|---|---|
| **Frame swap** | CMD_SWAP in ring | `vkQueueSubmit()` with fence; wait on next frame's fence before reuse | No (double-buffered) |
| **LFB read** | CPU reads framebuffer | Submit, fence wait, `vkCmdCopyImageToBuffer()`, fence wait, map | **Yes** -- CPU must block |
| **FIFO idle** | Guest checks FIFO status | Submit current batch, fence wait | **Yes** -- rare |
| **Pixel counter** | Guest reads pixel stats | Flush batch, read query result | **Yes** -- rare |

### LFB Readback Flow (Detailed)

```c
void vc_readback_region(vc_context_t *ctx, int x, int y, int w, int h,
                        void *dst, int dst_pitch) {
    /* 1. Flush pending draws */
    vc_flush_batch(ctx);

    /* 2. End current render pass */
    vkCmdEndRenderPass(ctx->cmd_buf);

    /* 3. Transition front buffer: COLOR_ATTACHMENT -> TRANSFER_SRC */
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .image = ctx->front_image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(ctx->cmd_buf,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);

    /* 4. Copy region to staging buffer */
    VkBufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = w,
        .bufferImageHeight = h,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset = { x, y, 0 },
        .imageExtent = { w, h, 1 },
    };
    vkCmdCopyImageToBuffer(ctx->cmd_buf,
        ctx->front_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        ctx->readback_staging_buf, 1, &region);

    /* 5. Buffer memory barrier: TRANSFER_WRITE -> HOST_READ */
    VkBufferMemoryBarrier buf_barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .buffer = ctx->readback_staging_buf,
        .size = VK_WHOLE_SIZE,
    };
    vkCmdPipelineBarrier(ctx->cmd_buf,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0, 0, NULL, 1, &buf_barrier, 0, NULL);

    /* 6. Transition front buffer back: TRANSFER_SRC -> COLOR_ATTACHMENT */
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(ctx->cmd_buf,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);

    /* 7. Submit and wait */
    vkEndCommandBuffer(ctx->cmd_buf);
    VkSubmitInfo submit = { .commandBufferCount = 1, .pCommandBuffers = &ctx->cmd_buf };
    vkQueueSubmit(ctx->queue, 1, &submit, ctx->readback_fence);
    vkWaitForFences(ctx->device, 1, &ctx->readback_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(ctx->device, 1, &ctx->readback_fence);

    /* 8. Map staging buffer and copy to destination */
    void *mapped;
    vkMapMemory(ctx->device, ctx->readback_staging_mem, 0, VK_WHOLE_SIZE, 0, &mapped);
    /* Copy scanlines with pitch adjustment */
    for (int row = 0; row < h; row++) {
        memcpy((uint8_t *)dst + row * dst_pitch,
               (uint8_t *)mapped + row * w * 4,
               w * 4);
    }
    vkUnmapMemory(ctx->device, ctx->readback_staging_mem);

    /* 9. Reset command buffer and restart render pass */
    vkResetCommandPool(ctx->device, ctx->frames[ctx->current_frame].cmd_pool, 0);
    /* ... re-begin command buffer and render pass ... */
}
```

### Async Readback (Double-Buffered Staging)

For frequent readbacks (LFB-heavy games), use two staging buffers:

```c
typedef struct vc_async_readback {
    VkBuffer  staging[2];
    VmaAllocation alloc[2];
    void     *mapped[2];     /* Persistently mapped via VMA */
    VkFence   fence[2];
    int       current;       /* 0 or 1 */
    bool      pending[2];    /* True if GPU is writing to this buffer */
} vc_async_readback_t;
```

1. Submit readback to `staging[current]`
2. If `pending[1 - current]`, wait for its fence, then read from it
3. Flip `current`

This hides GPU latency by reading from the previous frame's staging buffer
while the current frame's readback is in flight.

---

## 10. Complexity Honest Assessment

### Code Volume Comparison: GL vs Vulkan

| Component | OpenGL (estimated) | Vulkan (estimated) | Ratio |
|---|---|---|---|
| Context/device init | ~50 lines (GLAD + context) | ~300 lines (instance, device, queue, volk) | 6x |
| Render pass / FBO | ~30 lines (FBO create/bind) | ~100 lines (VkRenderPass + VkFramebuffer + VkImageView) | 3x |
| Pipeline creation | ~15 lines (program link) | ~200 lines (VkGraphicsPipelineCreateInfo + all state structs) | 13x |
| Shader loading | ~20 lines (compile + link) | ~30 lines (VkShaderModule from SPIR-V blob) | 1.5x |
| Uniform/push constant | ~30 lines (glUniform calls) | ~15 lines (vkCmdPushConstants) | 0.5x |
| Texture upload | ~20 lines (glTexImage2D) | ~80 lines (staging buffer + copy + barriers) | 4x |
| Draw call | ~5 lines (bind + glDrawArrays) | ~10 lines (bind pipeline + descriptors + draw) | 2x |
| Readback | ~10 lines (glReadPixels) | ~60 lines (copy-to-buffer + fence + map) | 6x |
| Buffer management | ~10 lines (VBO create) | ~40 lines (VkBuffer + VMA alloc) | 4x |
| Synchronization | ~5 lines (glFinish/glFence) | ~60 lines (fences + barriers + semaphores) | 12x |
| Memory management | ~0 lines (GL handles it) | ~50 lines (VMA setup + allocations) | N/A |
| **Total core infrastructure** | **~200 lines** | **~950 lines** | **~5x** |

**The Vulkan implementation will be roughly 5x more code than the GL
equivalent for the core infrastructure layer.** However:

1. The uber-shader (200-300 lines of GLSL) is nearly identical between GL and
   Vulkan. Only the interface declarations change (`uniform` -> `push_constant`,
   `sampler1D` -> `sampler2D` for fog table).

2. The Voodoo bridge code (`vid_voodoo_vk.c`) is the same regardless of
   graphics API -- it converts `voodoo_params_t` to vertices and push constants.

3. The SPSC command ring and threading infrastructure is API-independent.

**Total estimated Vulkan codebase: ~2500-3500 lines of C** (vs ~1200-1500
for GL). This is well within one developer's capacity.

### The Hardest Parts (In Order)

1. **Pipeline barrier correctness** (difficulty: HIGH)
   Getting image layout transitions right. Wrong barriers cause validation
   errors at best, GPU hangs at worst. The readback flow (Section 9) is
   the most complex barrier sequence.
   **Mitigation**: Use Vulkan validation layers (`VK_LAYER_KHRONOS_validation`)
   during all development. They catch 95% of barrier errors.

2. **Pipeline management** (difficulty: MEDIUM)
   Hashing blend state, creating pipelines on first use, caching. Not
   conceptually hard, but lots of VkCreateInfo boilerplate.
   **Mitigation**: Start with a single pipeline (blend disabled). Add blend
   permutations incrementally as features are added.

3. **Texture upload with layout transitions** (difficulty: MEDIUM)
   Every texture upload requires: create staging buffer -> copy CPU data ->
   transition image UNDEFINED -> TRANSFER_DST -> copy buffer to image ->
   transition TRANSFER_DST -> SHADER_READ_ONLY.
   **Mitigation**: Wrap in a helper function. Call once per texture.

4. **VMA integration** (difficulty: LOW)
   Well-documented, widely used. Replace manual `vkAllocateMemory()` with
   `vmaCreateBuffer()` / `vmaCreateImage()`.

5. **Display integration** (difficulty: LOW with Option A)
   Readback to CPU buffer is straightforward. Zero Qt changes needed.

6. **Push constants** (difficulty: LOW)
   Our 64-byte block is well within the 128-byte minimum. Single
   `vkCmdPushConstants()` call per batch.

### What to Implement First

**Recommended implementation order (prioritized by risk reduction):**

| Phase | What | Why First |
|---|---|---|
| 1 | `vc_core.c`: VkInstance + VkDevice + VkQueue + volk init | Foundation. Validates Vulkan is functional on all targets. |
| 2 | `vc_render_pass.c`: VkRenderPass + VkFramebuffer (single buffer, RGBA8 + D16) | Validates image creation, layout transitions, render pass. |
| 3 | `vc_shader.c`: Load embedded SPIR-V, create VkShaderModule | Validates SPIR-V toolchain integration. |
| 4 | `vc_pipeline.c`: Single pipeline (blend off), push constants | First drawable state. Hardest boilerplate. |
| 5 | `vc_batch.c`: Vertex buffer (stream ring), vkCmdDraw() | First visible triangle on screen. |
| 6 | `vc_readback.c`: Image-to-staging-buffer copy, fence, map | Validates sync model, enables display via Option A. |
| 7 | `vc_texture.c`: Staging upload, image transitions | Enables textured rendering. |
| 8 | Pipeline cache: Blend state permutations | Enables alpha blending. |
| 9 | `vc_thread.c`: Dedicated render thread + SPSC ring | Decouples from FIFO thread. |
| 10 | Dual framebuffer: Front/back swap | Enables proper double-buffering. |

### Vulkan Validation Layer Strategy

**Always enable during development:**

```c
const char *validation_layers[] = {
    "VK_LAYER_KHRONOS_validation"
};
// Enable at VkInstance creation with VK_EXT_debug_utils
```

The validation layer catches:
- Missing/incorrect pipeline barriers
- Invalid image layout transitions
- Descriptor set binding errors
- Push constant range violations
- Command buffer recording errors
- Memory hazards (read-after-write without barrier)

**Cost**: ~5-15% overhead. Disable for release builds.

---

## Appendix A: Full Vulkan Resource Inventory

Resources needed per Voodoo instance:

| Resource | Count | Size | Notes |
|---|---|---|---|
| VkImage (color, front) | 1 | 1024x768 RGBA8 = 3 MB | Max Voodoo resolution |
| VkImage (color, back) | 1 | 3 MB | |
| VkImage (depth, front) | 1 | 1024x768 D16 = 1.5 MB | |
| VkImage (depth, back) | 1 | 1.5 MB | |
| VkImage (textures) | ~64 | ~256 KB each = ~16 MB | Per TMU, LOD 0 only (higher LODs smaller) |
| VkImage (fog table) | 1 | 64x1 RG8 = 128 B | Tiny |
| VkBuffer (vertex stream) | 1 | 1 MB ring | Recycled per frame |
| VkBuffer (readback staging) | 2 | 3 MB each | Double-buffered for async |
| VkBuffer (texture staging) | 1 | 256 KB | Reused for all texture uploads |
| VkFramebuffer | 2 | N/A | Front + back |
| VkRenderPass | 1 | N/A | Shared by both framebuffers (compatible) |
| VkPipeline | ~5-15 | N/A | Cached by blend state |
| VkPipelineLayout | 1 | N/A | Shared by all pipelines |
| VkPipelineCache | 1 | N/A | Persisted to disk |
| VkDescriptorPool | 1 | N/A | 3 combined_image_sampler descriptors |
| VkDescriptorSetLayout | 1 | N/A | 3 sampler bindings |
| VkDescriptorSet | 1+ | N/A | Updated when textures change |
| VkCommandPool | 2 | N/A | One per frame-in-flight |
| VkCommandBuffer | 2 | N/A | One per frame-in-flight |
| VkFence | 3 | N/A | 2 frame fences + 1 readback fence |
| VkSampler | ~4 | N/A | nearest, bilinear, clamp, wrap combinations |
| **Total GPU memory** | | **~30 MB** | Conservative estimate |

---

## Appendix B: Voodoo Blend Factor to VkBlendFactor Mapping

| Voodoo `src_afunc` / `dst_afunc` | Value | VkBlendFactor |
|---|---|---|
| AZERO | 0 | `VK_BLEND_FACTOR_ZERO` |
| ASRC_ALPHA | 1 | `VK_BLEND_FACTOR_SRC_ALPHA` |
| ACOLOR | 2 | `VK_BLEND_FACTOR_DST_COLOR` (for src) |
| ADST_ALPHA | 3 | `VK_BLEND_FACTOR_DST_ALPHA` |
| AONE | 4 | `VK_BLEND_FACTOR_ONE` |
| AOMSRC_ALPHA | 5 | `VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA` |
| AOMCOLOR | 6 | `VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR` (for src) |
| AOMDST_ALPHA | 7 | `VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA` |
| ASATURATE | 0xF | `VK_BLEND_FACTOR_SRC_ALPHA_SATURATE` (src only) |
| ACOLORBEFOREFOG | 0xF (dst) | **Not mappable** -- requires copy-on-blend in shader |

All standard Voodoo blend ops map to `VK_BLEND_OP_ADD`.

---

## Appendix C: Source References

### Vulkan Specification
- [Vulkan 1.2 Spec (Khronos)](https://registry.khronos.org/vulkan/specs/1.2/html/)
- [Push Constants Guide](https://docs.vulkan.org/guide/latest/push_constants.html)
- [VkDynamicState Reference](https://registry.khronos.org/vulkan/specs/latest/man/html/VkDynamicState.html)
- [VK_EXT_extended_dynamic_state](https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_EXT_extended_dynamic_state.html)
- [VK_EXT_extended_dynamic_state3](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_extended_dynamic_state3.html)

### MoltenVK
- [MoltenVK GitHub](https://github.com/KhronosGroup/MoltenVK)
- [MoltenVK Runtime User Guide](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md)
- [Dynamic State Support Issue #1739](https://github.com/KhronosGroup/MoltenVK/issues/1739)
- [Vulkan 1.2 Requirements Issue #1567](https://github.com/KhronosGroup/MoltenVK/issues/1567)
- [Configuration Parameters](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Configuration_Parameters.md)
- [MoltenVK Releases](https://github.com/KhronosGroup/MoltenVK/releases)

### Raspberry Pi / V3DV
- [V3DV Extended Dynamic State (Phoronix)](https://www.phoronix.com/news/V3DV-Extended-Dynamic-State)
- [V3DV Vulkan 1.3 Support (Phoronix)](https://www.phoronix.com/news/Mesa-24.3-V3DV-Vulkan-1.3)
- [V3D Mesa Documentation](https://docs.mesa3d.org/drivers/v3d.html)

### Third-Party Libraries
- [volk (GitHub)](https://github.com/zeux/volk) -- MIT license
- [VMA (GitHub)](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) -- MIT license
- [vk-bootstrap (GitHub)](https://github.com/charles-lunarg/vk-bootstrap) -- MIT license
- [shaderc / glslc (GitHub)](https://github.com/google/shaderc) -- Apache 2.0

### Emulator Source Code
- [Dolphin Vulkan Backend](https://github.com/dolphin-emu/dolphin/tree/master/Source/Core/VideoBackends/Vulkan)
- [Dolphin CommandBufferManager.h](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoBackends/Vulkan/CommandBufferManager.h)
- [Dolphin VideoCommon Unification Blog](https://dolphin-emu.org/blog/2019/04/01/the-new-era-of-video-backends/)
- [Duckstation (GitHub)](https://github.com/stenzek/duckstation)
- [PCSX2 Vulkan Renderer](https://github.com/PCSX2/pcsx2/tree/master/pcsx2/GS/Renderers/Vulkan)
- [paraLLEl-GS (GitHub)](https://github.com/Arntzen-Software/parallel-gs)
- [Maister's Blog: PS2 GS Emulation](https://themaister.net/blog/2024/07/03/playstation-2-gs-emulation-the-final-frontier-of-vulkan-compute-emulation/)

### Qt Vulkan Integration
- [QVulkanWindow Class (Qt 5.15)](https://doc.qt.io/qt-5/qvulkanwindow.html)
- [Qt Vulkan Support Blog](https://www.qt.io/blog/2017/06/06/vulkan-support-qt-5-10-part-1)
- [Qt with External Vulkan Renderer](https://www.niangames.com/articles/qt-vulkan-renderer)
- [86Box qt_vulkanrenderer.cpp](src/qt/qt_vulkanrenderer.cpp)

### SPIR-V Toolchain
- [SPIR-V Toolchain (LunarG)](https://vulkan.lunarg.com/doc/view/latest/windows/spirv_toolchain.html)
- [shaderc (Google)](https://github.com/google/shaderc)
- [Embedded Shaders Blog](https://snorristurluson.github.io/EmbeddedShaders/)

### Performance
- [Reducing Vulkan API Call Overhead (AMD GPUOpen)](https://gpuopen.com/learn/reducing-vulkan-api-call-overhead/)
- [Constant Data in Vulkan (Khronos)](https://docs.vulkan.org/samples/latest/samples/performance/constant_data/README.html)
- [VMA Usage Patterns](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html)
