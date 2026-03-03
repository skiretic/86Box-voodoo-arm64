# Phase 2 Implementation Research: Basic Rendering

Comprehensive research report covering all Phase 2 topics for VideoCommon v2.
Cross-references v1 archive research, DESIGN.md specifications, Vulkan 1.2 spec,
and MoltenVK behavior. Phase 2 goal: flat-shaded colored triangles rendered to an
offscreen framebuffer. No textures, no blending, no depth test.

Research date: 2026-03-01

---

## Table of Contents

1. [Offscreen Framebuffer Setup](#1-offscreen-framebuffer-setup)
2. [Graphics Pipeline for Phase 2](#2-graphics-pipeline-for-phase-2)
3. [Vertex Format and Extraction](#3-vertex-format-and-extraction)
4. [SPIR-V Shader Compilation](#4-spirv-shader-compilation)
5. [MoltenVK Considerations](#5-moltenvk-considerations)
6. [Summary of Gaps and Contradictions](#6-summary-of-gaps-and-contradictions)

---

## 1. Offscreen Framebuffer Setup

### 1.1 What the v1 Research Says

The v1 research doc `archive/v1/research/vulkan-architecture.md` Section 1
("GL-to-Vulkan Mapping Table") provides the mapping:

- FBO color attachment (GL_RGBA8) -> `VkImage` with `VK_FORMAT_R8G8B8A8_UNORM`
  and usage `COLOR_ATTACHMENT_BIT | TRANSFER_SRC_BIT`
- FBO depth attachment (GL_DEPTH_COMPONENT16) -> `VkImage` with
  `VK_FORMAT_D16_UNORM` and usage `DEPTH_STENCIL_ATTACHMENT_BIT`
- `glClear()` -> `VK_ATTACHMENT_LOAD_OP_CLEAR` or `vkCmdClearAttachments()`
- `glBindFramebuffer()` -> `vkCmdBeginRenderPass()`

**Important v1-to-v2 change**: The v1 doc specified `D16_UNORM` for depth (matching
Voodoo's native 16-bit depth buffer). The v2 DESIGN.md section 7.3 specifies
`D32_SFLOAT` instead. This is a deliberate upgrade -- `D32_SFLOAT` provides
sufficient precision for both the Voodoo's linear Z mode (16-bit) and W-buffer
mode (logarithmic depth) without quantization artifacts. The shader writes
`gl_FragDepth` to control the actual depth value, so the hardware format just
needs enough precision to represent the Voodoo's depth range.

The v1 doc also specifies `TRANSFER_SRC_BIT` on the color image -- this is
still correct in v2 for the readback path (shadow buffer copy) and the
post-process blit.

### 1.2 What DESIGN.md Section 7.3 Specifies

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
```

The `SAMPLED` usage flag on the color image is for Phase 3 (post-process blit
where the offscreen image is sampled by a fragment shader). It costs nothing to
include at creation time and avoids recreating the image later.

### 1.3 VkImage Creation Details

For Phase 2, we need two VkImages plus their VkImageViews:

**Color image** (`VK_FORMAT_R8G8B8A8_UNORM`):

```c
VkImageCreateInfo color_info = {
    .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType   = VK_IMAGE_TYPE_2D,
    .format      = VK_FORMAT_R8G8B8A8_UNORM,
    .extent      = { width, height, 1 },
    .mipLevels   = 1,
    .arrayLayers = 1,
    .samples     = VK_SAMPLE_COUNT_1_BIT,
    .tiling      = VK_IMAGE_TILING_OPTIMAL,
    .usage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                 | VK_IMAGE_USAGE_SAMPLED_BIT
                 | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
};
```

**Depth image** (`VK_FORMAT_D32_SFLOAT`):

```c
VkImageCreateInfo depth_info = {
    .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType   = VK_IMAGE_TYPE_2D,
    .format      = VK_FORMAT_D32_SFLOAT,
    .extent      = { width, height, 1 },
    .mipLevels   = 1,
    .arrayLayers = 1,
    .samples     = VK_SAMPLE_COUNT_1_BIT,
    .tiling      = VK_IMAGE_TILING_OPTIMAL,
    .usage       = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
};
```

Both images should be allocated via VMA with `VMA_MEMORY_USAGE_AUTO` (formerly
`VMA_MEMORY_USAGE_GPU_ONLY`). VMA will select device-local memory. On UMA
platforms (Pi 5), this is the only memory type anyway.

**VkImageViews** for both images:

```c
VkImageViewCreateInfo view_info = {
    .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .image      = color_image,   /* or depth_image */
    .viewType   = VK_IMAGE_VIEW_TYPE_2D,
    .format     = VK_FORMAT_R8G8B8A8_UNORM,  /* or D32_SFLOAT */
    .subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, /* or DEPTH_BIT */
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    },
};
```

### 1.4 Render Pass Configuration

DESIGN.md section 7.4 specifies a two-render-pass pattern for first-frame
initialization. This is the critical correctness requirement.

**Why two VkRenderPass objects are needed**:

A newly created VkImage starts in `VK_IMAGE_LAYOUT_UNDEFINED`. If we use
`loadOp: LOAD` with `initialLayout: COLOR_ATTACHMENT_OPTIMAL` on an image
that is actually in UNDEFINED layout, this violates VUID-VkAttachmentDescription-
initialLayout (the implementation may interpret UNDEFINED contents as garbage,
but the spec says the initial layout must match the actual layout unless
UNDEFINED is explicitly specified as the initial layout). The only safe way to
begin using a fresh image is with `initialLayout: UNDEFINED` combined with
`loadOp: CLEAR` (or DONT_CARE), which tells the driver that previous contents
are discarded.

**Render pass A (first frame / after resize)**:

```c
VkAttachmentDescription attachments[2] = {
    [0] = {  /* Color */
        .format         = VK_FORMAT_R8G8B8A8_UNORM,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    },
    [1] = {  /* Depth */
        .format         = VK_FORMAT_D32_SFLOAT,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    },
};
```

**Render pass B (steady state)**:

```c
VkAttachmentDescription attachments[2] = {
    [0] = {  /* Color */
        .format         = VK_FORMAT_R8G8B8A8_UNORM,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    },
    [1] = {  /* Depth */
        .format         = VK_FORMAT_D32_SFLOAT,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    },
};
```

**Pipeline compatibility**: Vulkan render pass compatibility (Vulkan 1.2 spec
section 7.2 "Render Pass Compatibility") compares ONLY format, sample count,
and attachment reference indices between two render passes. The `loadOp`,
`storeOp`, `initialLayout`, and `finalLayout` fields do NOT affect
compatibility. Therefore, a VkPipeline created against render pass A is also
valid for use with render pass B. We only need ONE pipeline for both render
pass variants.

**Subpass configuration** (shared by both render passes):

```c
VkAttachmentReference color_ref = {
    .attachment = 0,
    .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
};
VkAttachmentReference depth_ref = {
    .attachment = 1,
    .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
};
VkSubpassDescription subpass = {
    .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
    .colorAttachmentCount    = 1,
    .pColorAttachments       = &color_ref,
    .pDepthStencilAttachment = &depth_ref,
};
```

**Subpass dependencies**: We need no external subpass dependencies in Phase 2.
The implicit subpass dependency (VK_SUBPASS_EXTERNAL -> subpass 0) with
`srcStageMask = TOP_OF_PIPE` and `dstStageMask = COLOR_ATTACHMENT_OUTPUT |
EARLY_FRAGMENT_TESTS` is sufficient. However, for safety and clarity, an
explicit dependency is recommended:

```c
VkSubpassDependency dependency = {
    .srcSubpass    = VK_SUBPASS_EXTERNAL,
    .dstSubpass    = 0,
    .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                   | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
    .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                   | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
    .srcAccessMask = 0,
    .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                   | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
};
```

### 1.5 VkFramebuffer Creation

One VkFramebuffer that references both image views, used with both render
passes:

```c
VkImageView fb_attachments[2] = { color_view, depth_view };
VkFramebufferCreateInfo fb_info = {
    .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    .renderPass      = render_pass_b,  /* either pass works (compatible) */
    .attachmentCount = 2,
    .pAttachments    = fb_attachments,
    .width           = width,
    .height          = height,
    .layers          = 1,
};
```

### 1.6 Image Layout Transitions in Phase 2

In Phase 2 (no swapchain, no readback), the layout management is simple:

- **First frame**: render pass A transitions both images from UNDEFINED to
  their working layouts (COLOR_ATTACHMENT_OPTIMAL, DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
- **Subsequent frames**: render pass B keeps both images in their working layouts
  (initialLayout == finalLayout)
- **No additional barriers needed** -- the render pass handles all transitions

The offscreen images remain in COLOR_ATTACHMENT_OPTIMAL and
DEPTH_STENCIL_ATTACHMENT_OPTIMAL for the entire Phase 2 lifetime. Additional
transitions (to SHADER_READ_ONLY, to TRANSFER_SRC) will be added in Phase 3
when the post-process blit and readback paths are introduced.

### 1.7 Clear Values for First Frame

```c
VkClearValue clear_values[2] = {
    [0] = { .color = { .float32 = { 0.0f, 0.0f, 0.0f, 0.0f } } },
    [1] = { .depthStencil = { .depth = 0.0f, .stencil = 0 } },
};
```

DESIGN.md section 7.4 notes "depth = 0.0 (or 1.0 per Voodoo convention)." The
Voodoo's default clear depth depends on the depth function used by the game.
Most games use `LEQUAL` with max depth clear (0xFFFF in 16-bit = 1.0 normalized),
but some use `GEQUAL` with zero clear. For Phase 2 (no depth test), the clear
value does not matter. For Phase 5 (depth test), the clear value should match
the Voodoo's `zaColor` depth field when fastfill is used. For Phase 2,
initializing to 0.0f is fine.

### 1.8 Framebuffer Resize

When the Voodoo resolution changes (e.g., switching from 640x480 to 800x600),
the offscreen images must be recreated. This requires:

1. `vkDeviceWaitIdle()` (ensure all GPU work referencing the old images is done)
2. Destroy old: VkFramebuffer, VkImageView (x2), VkImage (x2, via VMA)
3. Create new: VkImage (x2), VkImageView (x2), VkFramebuffer
4. Set `first_frame = true` (so render pass A is used for the next frame)

The render passes themselves do NOT need recreation (they are independent of
image dimensions). This is a Vulkan advantage over some GL implementations.

### 1.9 Recommendation for Phase 2

- Create two VkRenderPass objects (clear variant and load variant)
- Create one VkFramebuffer referencing both image views
- Track `first_frame` boolean per framebuffer; set true on creation/resize
- Use render pass A for first frame, then switch to render pass B
- For Phase 2, `D32_SFLOAT` depth is correct per DESIGN.md, even though
  Phase 2 does not enable depth testing
- Include `SAMPLED | TRANSFER_SRC` on the color image from day one

---

## 2. Graphics Pipeline for Phase 2

### 2.1 What the v1 Research Says

The v1 research `archive/v1/research/vulkan-architecture.md` Section 2
("Pipeline Management Strategy") established:

- Blend state is baked into VkPipeline (MoltenVK lacks EDS3)
- Depth/scissor/viewport are dynamic via VK_EXT_extended_dynamic_state
- Pipeline key: blend_enable, src/dst factors, color write mask, depth format
- Expected ~5-15 pipeline objects per game session, created on first encounter

The v1 research `archive/v1/research/push-constant-layout.md` established:

- 64-byte push constant block with 16 fields (14 uint32 + 2 float)
- Single VkPushConstantRange covering both VERTEX_BIT and FRAGMENT_BIT
- Descriptor set layout with 3 combined image samplers (TMU0, TMU1, fog table)

These findings are still valid for v2. The push constant layout is identical.

### 2.2 What DESIGN.md Section 7.5 Specifies

```
Push constants (64 bytes): Encode per-triangle Voodoo register state:
- fbzMode, fogMode, alphaMode, fbzColorPath, textureMode[2]
- Fog color, chroma key, alpha reference
- See videocommon-plan/research/push-constant-layout.md for exact layout
```

DESIGN.md section 7.8 specifies the vertex format:

```c
typedef struct vc_vertex_t {
    float x, y, z, w;          /* screen-space position (16 bytes) */
    float r, g, b, a;          /* iterated color (16 bytes) */
    float s0, t0, w0;          /* TMU0 texture coords (12 bytes) */
    float s1, t1, w1;          /* TMU1 texture coords (12 bytes) */
    float fog;                 /* fog coordinate (4 bytes) */
    float pad[3];              /* align to 16 bytes (12 bytes) */
} vc_vertex_t;                 /* total: 72 bytes */
```

### 2.3 Phase 2 Minimal Pipeline

For Phase 2, the pipeline is maximally simple:

**What is enabled**:
- Vertex input (position + color)
- Rasterization (fill mode, no face culling for Phase 2)
- Color output (write to color attachment)
- Dynamic state: viewport + scissor

**What is disabled/default**:
- No blending (`blendEnable = VK_FALSE`)
- No depth test (`depthTestEnable = VK_FALSE`, `depthWriteEnable = VK_FALSE`)
- No stencil test
- No multisampling (1 sample)

**Vertex input binding** (Phase 2):

Even though the full `vc_vertex_t` is 72 bytes, for Phase 2 we can still use
the full stride but only declare the attributes we consume. However, the
DESIGN.md vertex format should be used from day one to avoid changing the
vertex buffer layout later. The vertex shader simply ignores unused attributes.

```c
VkVertexInputBindingDescription binding = {
    .binding   = 0,
    .stride    = sizeof(vc_vertex_t),  /* 72 bytes */
    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
};

/* Phase 2: only position (location 0) and color (location 1) are used.
   However, we declare ALL attributes from day one so the buffer layout
   never changes. The shader simply does not read unused locations. */
VkVertexInputAttributeDescription attributes[] = {
    /* location 0: position (x, y, z, w) */
    { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT,
      .offset = offsetof(vc_vertex_t, x) },
    /* location 1: color (r, g, b, a) */
    { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT,
      .offset = offsetof(vc_vertex_t, r) },
    /* location 2: texcoord0 (s0, t0, w0) */
    { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
      .offset = offsetof(vc_vertex_t, s0) },
    /* location 3: texcoord1 (s1, t1, w1) */
    { .location = 3, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
      .offset = offsetof(vc_vertex_t, s1) },
    /* location 4: fog */
    { .location = 4, .binding = 0, .format = VK_FORMAT_R32_SFLOAT,
      .offset = offsetof(vc_vertex_t, fog) },
};
```

**Important consideration**: Vulkan validation requires that every vertex
attribute declared in the pipeline's vertex input state must have a
corresponding input variable in the vertex shader, OR the shader must not
consume undeclared attributes. The recommended approach is to declare ALL
attributes in the pipeline vertex input state from day one, and have the
vertex shader declare all `in` variables even if the fragment shader does
not use them. The GLSL compiler will optimize away unused varyings. This
avoids changing the pipeline vertex input state across phases.

**ALTERNATIVE approach for Phase 2**: Declare only position and color in the
vertex input state, and use a reduced `sizeof_phase2_vertex` stride. This is
simpler but requires changing the pipeline when Phase 4/5 add more attributes.
The recommendation is to use the full vertex format from day one -- the wasted
bandwidth of unused attributes is negligible (24 extra bytes per vertex, ~72 KB
per frame at 1000 triangles).

### 2.4 Push Constant Range

The push constant layout from `archive/v1/research/push-constant-layout.md`
is still valid for v2. For Phase 2, the fragment shader only reads `fbzColorPath`
(to select between iterated color vs constant color) and `fb_width`/`fb_height`
(vertex shader, for NDC conversion). But the full 64-byte range should be
declared from day one:

```c
VkPushConstantRange push_range = {
    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    .offset     = 0,
    .size       = 64,  /* sizeof(vc_push_constants_t) */
};
```

### 2.5 Pipeline Layout

The pipeline layout includes the push constant range and a descriptor set
layout. Even though Phase 2 uses no textures, the descriptor set layout should
be created from day one (with an empty set bound at draw time, or using
VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT to defer).

**Recommended approach for Phase 2**: Create the descriptor set layout with
3 bindings (TMU0, TMU1, fog table) per the push-constant-layout.md spec, but
create a "dummy" 1x1 white texture and bind it to all three slots. This avoids
any conditional logic around descriptor binding.

```c
VkDescriptorSetLayoutBinding bindings[3] = {
    { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT },
    { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT },
    { .binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT },
};

VkPipelineLayoutCreateInfo layout_info = {
    .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount         = 1,
    .pSetLayouts            = &desc_set_layout,
    .pushConstantRangeCount = 1,
    .pPushConstantRanges    = &push_range,
};
```

**Alternative for Phase 2 simplicity**: Use `setLayoutCount = 0` and
`pSetLayouts = NULL` (no descriptor sets). This means the pipeline layout has
only push constants. When Phase 4 adds textures, the pipeline layout changes,
requiring all pipelines to be recreated. This is a minor one-time cost.

**Recommendation**: Use `setLayoutCount = 0` for Phase 2. It is simpler and
avoids creating dummy textures. The pipeline layout change in Phase 4 is
trivial (just add the descriptor set layout and recreate pipelines).

### 2.6 Dynamic State

Phase 2 only needs viewport and scissor as dynamic state:

```c
VkDynamicState dynamic_states[] = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR,
};
```

Phase 5 will add depth test/write/compare via VK_EXT_extended_dynamic_state
(where available). For Phase 2, depth state is fully baked into the pipeline
as disabled.

### 2.7 Remaining Pipeline State

```c
/* Input assembly: triangle list (Voodoo submits individual triangles) */
VkPipelineInputAssemblyStateCreateInfo ia = {
    .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .primitiveRestartEnable = VK_FALSE,
};

/* Rasterization */
VkPipelineRasterizationStateCreateInfo rast = {
    .depthClampEnable        = VK_FALSE,
    .rasterizerDiscardEnable = VK_FALSE,
    .polygonMode             = VK_POLYGON_MODE_FILL,
    .cullMode                = VK_CULL_MODE_NONE,  /* Phase 2: no culling */
    .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    .depthBiasEnable         = VK_FALSE,
    .lineWidth               = 1.0f,
};

/* Multisampling (disabled) */
VkPipelineMultisampleStateCreateInfo ms = {
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    .sampleShadingEnable  = VK_FALSE,
};

/* Depth/stencil (disabled for Phase 2) */
VkPipelineDepthStencilStateCreateInfo ds = {
    .depthTestEnable       = VK_FALSE,
    .depthWriteEnable      = VK_FALSE,
    .depthCompareOp        = VK_COMPARE_OP_ALWAYS,
    .depthBoundsTestEnable = VK_FALSE,
    .stencilTestEnable     = VK_FALSE,
};

/* Color blend (disabled for Phase 2) */
VkPipelineColorBlendAttachmentState blend_att = {
    .blendEnable    = VK_FALSE,
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                    | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
};
VkPipelineColorBlendStateCreateInfo blend = {
    .logicOpEnable   = VK_FALSE,
    .attachmentCount = 1,
    .pAttachments    = &blend_att,
};

/* Viewport/scissor (dynamic, but count must be specified) */
VkPipelineViewportStateCreateInfo vp = {
    .viewportCount = 1,
    .scissorCount  = 1,
};
```

### 2.8 VkPipelineCache

Create a `VkPipelineCache` at startup for faster pipeline creation on
subsequent runs. For Phase 2 with a single pipeline, this is trivial but
sets the pattern for Phase 5+ where multiple blend variants will be cached.

```c
VkPipelineCacheCreateInfo cache_info = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
    /* initialDataSize = 0, pInitialData = NULL for first run */
};
```

Optionally, serialize the cache to disk between runs. This is a Phase 8
optimization.

### 2.9 Recommendation for Phase 2

- Declare full 72-byte vc_vertex_t in vertex input from day one
- Use 64-byte push constant range (both stages)
- Pipeline layout: push constants only (no descriptor sets in Phase 2)
- Single pipeline object: no blend, no depth, cull mode none
- Dynamic state: viewport + scissor only
- Create VkPipelineCache for future use

---

## 3. Vertex Format and Extraction

### 3.1 What the v1 Research Says

The v1 doc `archive/v1/research/intercept-point.md` is the definitive
analysis of where to intercept triangles. Key findings:

**Two submission modes**:
- Mode 1 (Voodoo 1 / Glide 2.x): CPU writes gradients directly to
  `voodoo_params_t` via SST_startR/dRdX/dRdY registers, fires triangleCMD.
  NO per-vertex data in `vert_t`.
- Mode 2 (Voodoo 2+ / Glide 3.x / CMDFIFO): CPU writes per-vertex floats
  to `voodoo->verts[3]`, fires sDrawTriCMD which calls
  `voodoo_triangle_setup()` to compute gradients.

**Both converge** at `voodoo_queue_triangle()`.

**v1 recommendation** (final): Intercept at `voodoo_queue_triangle()` (single
point, Approach B). Reconstruct per-vertex data from gradients for both modes.
The precision loss from reconstruction is ~1/4096 per color channel (12.12
fixed-point), which is acceptable.

**This recommendation is still correct for v2.** DESIGN.md section 11.1
confirms: "In the triangle dispatch path: If `use_gpu_renderer`, call
`voodoo_vk_push_triangle()` instead of dispatching to SW render threads."

### 3.2 Reconstruction Math (from v1, verified)

The v1 intercept-point.md provides the corrected reconstruction formula:

```c
/* Positions: 12.4 fixed-point -> pixel float */
float xA = (float)p->vertexAx / 16.0f;
float yA = (float)p->vertexAy / 16.0f;
float xB = (float)p->vertexBx / 16.0f;
float yB = (float)p->vertexBy / 16.0f;
float xC = (float)p->vertexCx / 16.0f;
float yC = (float)p->vertexCy / 16.0f;

float dx_ba = xB - xA;
float dy_ba = yB - yA;
float dx_ca = xC - xA;
float dy_ca = yC - yA;

/* Color A = start value (12.12 -> float [0..1]) */
float rA = (float)(int32_t)p->startR / (4096.0f * 255.0f);
/* Color B = A + gradient * delta_position */
float rB = rA + ((float)p->dRdX * dx_ba + (float)p->dRdY * dy_ba)
               / (4096.0f * 255.0f);
/* Color C = A + gradient * delta_position */
float rC = rA + ((float)p->dRdX * dx_ca + (float)p->dRdY * dy_ca)
               / (4096.0f * 255.0f);
```

**Key insight from v1 analysis**: The gradients `dRdX` etc. are per-pixel
(not per-12.4 unit). The software renderer confirms this with the `>> 4`
shift when multiplying 12.4 position deltas by per-pixel gradients. Our
reconstruction uses floating-point pixel positions (divided by 16.0f), so
the multiply-add is correct without any additional shifting.

**Sign extension**: `startR` is declared as `uint32_t` but must be treated as
signed (`int32_t`) for reconstruction. The v1 doc notes this edge case correctly.

### 3.3 DESIGN.md Section 7.8 Vertex Format

```c
typedef struct vc_vertex_t {
    float x, y, z, w;      /* 16 bytes */
    float r, g, b, a;      /* 16 bytes */
    float s0, t0, w0;      /* 12 bytes */
    float s1, t1, w1;      /* 12 bytes */
    float fog;              /* 4 bytes */
    float pad[3];           /* 12 bytes */
} vc_vertex_t;              /* 72 bytes total */
```

### 3.4 Phase 2 Vertex Fields

For Phase 2 (flat-shaded colored triangles, no textures, no depth), we need:

| Field | Source | Conversion |
|-------|--------|-----------|
| `x` | `p->vertexAx` (12.4 fixed-point) | `/ 16.0f` -> pixel float |
| `y` | `p->vertexAy` (12.4 fixed-point) | `/ 16.0f` -> pixel float |
| `z` | 0.5f (dummy, no depth test in Phase 2) | Constant |
| `w` | 1.0f (no perspective correction in Phase 2) | Constant |
| `r` | Reconstructed from `startR/dRdX/dRdY` | `/ (4096.0f * 255.0f)` -> [0,1] |
| `g` | Reconstructed from `startG/dGdX/dGdY` | Same |
| `b` | Reconstructed from `startB/dBdX/dBdY` | Same |
| `a` | Reconstructed from `startA/dAdX/dAdY` | Same |
| `s0, t0, w0` | 0.0f (unused in Phase 2) | Zero |
| `s1, t1, w1` | 0.0f (unused in Phase 2) | Zero |
| `fog` | 0.0f (unused in Phase 2) | Zero |
| `pad[3]` | 0.0f | Zero |

**Vertex shader NDC conversion** (from push constants fb_width/fb_height):

```glsl
float ndc_x = (2.0 * in_position.x / pc.fb_width)  - 1.0;
float ndc_y = (2.0 * in_position.y / pc.fb_height) - 1.0;
gl_Position = vec4(ndc_x * W, ndc_y * W, in_position.z * W, W);
```

**Y axis direction**: Vulkan clip space has Y pointing downward (unlike OpenGL
where Y points up). The Voodoo's screen space also has Y=0 at the top. So
there is no Y flip needed -- the Voodoo Y coordinates map directly to Vulkan's
clip space Y direction. This is a v1-to-v2 improvement: the v1 perspective
correction research (written for OpenGL) included a Y flip
(`ndc.y = 1.0 - (a_position.y / u_viewport_size.y) * 2.0`). In Vulkan, the
correct conversion is `ndc_y = (2.0 * y / height) - 1.0` with no negation.

**Note**: The v1 perspective-correction.md used `ndc.y = -ndc.y` (GL convention).
The v2 Vulkan vertex shader does NOT negate Y. This is a critical v1-to-v2
difference.

### 3.5 The voodoo_params_t Fields Needed for Phase 2

Looking at `src/include/86box/vid_voodoo_common.h` lines 124-242:

```c
/* Positions */
int32_t vertexAx, vertexAy;   /* vertex A (Y-sorted topmost) */
int32_t vertexBx, vertexBy;   /* vertex B */
int32_t vertexCx, vertexCy;   /* vertex C */

/* Color start values (12.12 fixed-point) */
uint32_t startR, startG, startB, startA;

/* Color gradients (12.12 fixed-point, per pixel) */
int32_t dRdX, dGdX, dBdX, dAdX;
int32_t dRdY, dGdY, dBdY, dAdY;

/* Pipeline state -- needed for push constants */
uint32_t fbzMode;
uint32_t fbzColorPath;
uint32_t alphaMode;
uint32_t fogMode;
uint32_t textureMode[2];
uint32_t color0, color1;
uint32_t zaColor;
uint32_t chromaKey;
uint32_t stipple;
int detail_max[2], detail_bias[2], detail_scale[2];

/* Clip rect */
int clipLeft, clipRight, clipLowY, clipHighY;

/* Winding */
int sign;

/* Fog */
rgbvoodoo_t fogColor;
struct { uint8_t fog, dfog; } fogTable[64];
```

For Phase 2 specifically, we use: positions (6 fields), color start/gradients
(16 fields), and the pipeline state registers for push constants. The Z, W,
S, T, fog fields are zero-filled.

### 3.6 The Integration Point

Per DESIGN.md section 11.1, the intercept is in `vid_voodoo_render.c` at the
triangle dispatch. The `voodoo_vk_push_triangle()` function in
`vid_voodoo_vk.c` will:

1. Reconstruct 3 vertices from `voodoo_params_t` gradients
2. Fill a `vc_vertex_t[3]` array
3. Extract push constant state from params
4. Push `VC_CMD_TRIANGLE` to the SPSC ring (with vertex data + push constants)

The ring command payload for `VC_CMD_TRIANGLE` is:

```
vc_ring_cmd_header_t  header;       /* 8 bytes */
vc_push_constants_t   push;         /* 64 bytes */
vc_clip_rect_t        clip;         /* 12 bytes (added Phase 5.10) */
vc_vertex_t           verts[3];     /* 216 bytes (72 * 3) */
/* Total: 300 bytes, aligned to 16 -> 304 bytes */
```

At 304 bytes per triangle and 8 MB ring, the ring holds ~27,000 triangles.
At 60 fps with 10K triangles/frame, this is ~2-3 frames of headroom. Adequate.

### 3.7 Recommendation for Phase 2

- Intercept at `voodoo_queue_triangle()` per v1 recommendation (unchanged)
- Reconstruct per-vertex colors from gradients (v1 math, verified)
- Fill full 72-byte vc_vertex_t with zeros for unused fields
- No Y flip in Vulkan (unlike v1 OpenGL path)
- Push constants include all 64 bytes even though Phase 2 only reads a few

---

## 4. SPIR-V Shader Compilation

### 4.1 What the v1 Research Says

The v1 doc `archive/v1/research/cmake-integration.md` Section 4 provides a
complete SPIR-V compilation pipeline:

- `glslc --target-env=vulkan1.2 -O -Werror` to compile GLSL to SPIR-V
- CMake `add_custom_command` for the compilation step
- A `SpvToHeader.cmake` script to convert `.spv` to C header arrays
- Cross-platform (no `xxd` dependency)
- Generated files go to `${CMAKE_CURRENT_BINARY_DIR}/generated/`
- A `compile_shader()` CMake function wrapping both steps

This plan is still fully valid for v2. The v1 doc was written for Vulkan
(not OpenGL), so no updates are needed for the shader compilation toolchain.

### 4.2 What the Current CMakeLists.txt Has

The current `src/video/videocommon/CMakeLists.txt` (Phase 1) contains:

```cmake
add_library(videocommon OBJECT
    vc_core.c
    vc_thread.c
    vc_vma_impl.cpp
)
```

It does NOT have:
- `find_program(GLSLC glslc ...)` -- no shader compiler search
- `compile_shader()` function -- no shader compilation
- `shaders/` directory references -- no shader sources
- Generated header include paths -- no `generated/` directory

All of these need to be added for Phase 2.

### 4.3 Phase 2 Shader Compilation Plan

**New files to create**:

```
src/video/videocommon/
    cmake/
        CompileShader.cmake     # compile_shader() function
        SpvToHeader.cmake       # SPIR-V binary to C header conversion
    shaders/
        voodoo_uber.vert        # Vertex shader (screen-space passthrough)
        voodoo_uber.frag        # Fragment shader (iterated color only)
```

**CMakeLists.txt additions**:

```cmake
# Find glslc
find_program(GLSLC glslc
    HINTS $ENV{VULKAN_SDK}/bin /usr/bin /usr/local/bin
)
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found. Install shaderc or Vulkan SDK.")
endif()

# Include the shader compilation module
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileShader.cmake)

# Compile shaders
compile_shader(
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/voodoo_uber.vert
    ${CMAKE_CURRENT_BINARY_DIR}/generated/voodoo_uber_vert_spv.h
    voodoo_uber_vert_spv
)
compile_shader(
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/voodoo_uber.frag
    ${CMAKE_CURRENT_BINARY_DIR}/generated/voodoo_uber_frag_spv.h
    voodoo_uber_frag_spv
)

# Add generated header include path
target_include_directories(videocommon PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# Ensure shaders are compiled before videocommon
add_custom_target(videocommon_shaders DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/generated/voodoo_uber_vert_spv.h
    ${CMAKE_CURRENT_BINARY_DIR}/generated/voodoo_uber_frag_spv.h
)
add_dependencies(videocommon videocommon_shaders)
```

### 4.4 Phase 2 Shader Contents

**Vertex shader** (`voodoo_uber.vert`):

```glsl
#version 450

layout(push_constant, std430) uniform PushConstants {
    uint  fbzMode;
    uint  fbzColorPath;
    uint  alphaMode;
    uint  fogMode;
    uint  textureMode0;
    uint  textureMode1;
    uint  color0;
    uint  color1;
    uint  chromaKey;
    uint  fogColor;
    uint  zaColor;
    uint  stipple;
    uint  detail0;
    uint  detail1;
    float fb_width;
    float fb_height;
} pc;

layout(location = 0) in vec4  in_position;   /* x, y, z, w */
layout(location = 1) in vec4  in_color;      /* r, g, b, a */
layout(location = 2) in vec3  in_texcoord0;  /* s0, t0, w0 */
layout(location = 3) in vec3  in_texcoord1;  /* s1, t1, w1 */
layout(location = 4) in float in_fog;

layout(location = 0) noperspective out vec4  v_color;
layout(location = 1)               out vec3  v_texcoord0;
layout(location = 2)               out vec3  v_texcoord1;
layout(location = 3) noperspective out float v_depth;
layout(location = 4) noperspective out float v_fog;

void main() {
    float W = (in_position.w > 0.0) ? (1.0 / in_position.w) : 1.0;

    float ndc_x = (2.0 * in_position.x / pc.fb_width)  - 1.0;
    float ndc_y = (2.0 * in_position.y / pc.fb_height) - 1.0;

    gl_Position = vec4(ndc_x * W, ndc_y * W, in_position.z * W, W);

    v_color     = in_color;
    v_texcoord0 = in_texcoord0;
    v_texcoord1 = in_texcoord1;
    v_depth     = in_position.z;
    v_fog       = in_fog;
}
```

**Fragment shader** (`voodoo_uber.frag`, Phase 2 minimal):

```glsl
#version 450

layout(push_constant, std430) uniform PushConstants {
    uint  fbzMode;
    uint  fbzColorPath;
    uint  alphaMode;
    uint  fogMode;
    uint  textureMode0;
    uint  textureMode1;
    uint  color0;
    uint  color1;
    uint  chromaKey;
    uint  fogColor;
    uint  zaColor;
    uint  stipple;
    uint  detail0;
    uint  detail1;
    float fb_width;
    float fb_height;
} pc;

layout(location = 0) noperspective in vec4  v_color;
layout(location = 1)               in vec3  v_texcoord0;
layout(location = 2)               in vec3  v_texcoord1;
layout(location = 3) noperspective in float v_depth;
layout(location = 4) noperspective in float v_fog;

layout(location = 0) out vec4 fragColor;

void main() {
    /* Phase 2: just output the iterated color. */
    fragColor = clamp(v_color, 0.0, 1.0);
}
```

The `clamp` is important because reconstructed colors from Voodoo gradients
can go slightly out of [0,1] range due to fixed-point quantization. The Voodoo
hardware clamps per-pixel, so we match that behavior.

### 4.5 Shader Design Note: in_position.w as 1/W

The DESIGN.md vertex format has `w` as the 4th component of position. The v1
perspective-correction research established that this field holds `1/W` (the
Voodoo's `sW0` or `sWb`). The vertex shader computes `W = 1.0 / in_position.w`
and encodes it into `gl_Position.w` for perspective-correct texture
interpolation.

For Phase 2 (no textures, no perspective), `in_position.w = 1.0` (so `W = 1.0`,
no perspective correction). This degenerates `smooth` varyings to linear
interpolation. Colors use `noperspective` regardless, so they are always
linearly interpolated.

### 4.6 Recommendation for Phase 2

- Use the v1 CMake integration plan verbatim (CompileShader.cmake, SpvToHeader.cmake)
- Target `--target-env=vulkan1.2` for shader compilation
- Declare full push constant block and all vertex inputs from day one
- Phase 2 fragment shader: just output clamped iterated color
- Phase 2 vertex shader: full W encoding logic (but W=1.0 in Phase 2)
- No descriptor set declarations in Phase 2 shaders (add in Phase 4)

---

## 5. MoltenVK Considerations

### 5.1 VK_FORMAT_D32_SFLOAT Support

MoltenVK maps `VK_FORMAT_D32_SFLOAT` to Metal's `MTLPixelFormatDepth32Float`.
This format is supported on ALL Apple Silicon devices (M1, M2, M3, M4, etc.)
and on Intel Macs with discrete GPUs. It is a universally available depth
format on macOS.

Source: MoltenVK pixel format mapping
(`MoltenVK/GPUObjects/MVKPixelFormats.mm`) maps D32_SFLOAT directly to
`Depth32Float`. Apple developer documentation confirms
`MTLPixelFormat.depth32Float` is available on all GPU families.

**D24_UNORM_S8_UINT**: MoltenVK has a known issue where `D24_UNORM_S8_UINT`
reports as supported via format properties but then fails at image creation on
some devices (GitHub issue #888). The recommendation is to avoid D24 on
MoltenVK and use D32_SFLOAT exclusively. Our DESIGN.md already specifies
D32_SFLOAT as the primary format with D24 as a fallback -- for MoltenVK,
D32_SFLOAT is always preferred.

### 5.2 Render Pass Compatibility

MoltenVK translates VkRenderPass to Metal render pass descriptors. The
two-render-pass pattern (CLEAR vs LOAD) is fully supported. Metal render pass
descriptors have their own `loadAction` (MTLLoadActionClear vs MTLLoadActionLoad)
which MoltenVK sets based on the Vulkan loadOp. No MoltenVK-specific issues
with this pattern.

### 5.3 VK_FORMAT_R8G8B8A8_UNORM

Maps to `MTLPixelFormatRGBA8Unorm`. Universally supported. No issues.

### 5.4 Push Constants

MoltenVK implements push constants by encoding them into Metal argument
buffers. The 64-byte block is well within Metal's limits. No issues.

### 5.5 Dynamic State (Phase 2)

Phase 2 uses only `VK_DYNAMIC_STATE_VIEWPORT` and `VK_DYNAMIC_STATE_SCISSOR`.
These are core Vulkan 1.0 dynamic states, fully supported by MoltenVK. No
extension dependencies.

### 5.6 Vertex Input

MoltenVK translates vertex input bindings and attributes to Metal vertex
descriptors. The 72-byte stride with mixed float2/float3/float4 attributes
is fully supported. No alignment issues (Metal requires 4-byte alignment for
vertex attributes, which is naturally satisfied by our float-based layout).

### 5.7 VkPipelineCache

MoltenVK supports VkPipelineCache but the underlying Metal pipeline cache
(`MTLBinaryArchive` on macOS 11+) may not serialize across driver versions.
This is a minor limitation -- pipeline cache hits provide a small speedup, but
pipeline creation from scratch is fast enough for our ~1-5 pipeline variants.

### 5.8 DESIGN.md Section 10 Cross-Reference

DESIGN.md section 10 lists these MoltenVK considerations:

| Item | Relevance to Phase 2 |
|------|---------------------|
| 10.1 Surface creation (CAMetalLayer) | Not Phase 2 (no swapchain) |
| 10.2 Queue submission (synchronous) | Applies -- GPU thread submits |
| 10.3 Swapchain images (triple buffer) | Not Phase 2 (no swapchain) |
| 10.4 EDS3 not supported (blend state) | Not Phase 2 (no blending) |
| 10.5 Fog table (no buffer textures) | Not Phase 2 (no fog) |
| 10.6 Configuration defaults | Applies -- MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS=1 |

No Phase 2-specific MoltenVK blockers.

### 5.9 Recommendation for Phase 2

- Use `VK_FORMAT_D32_SFLOAT` exclusively (no D24 fallback needed)
- No MoltenVK-specific workarounds for Phase 2
- Push constants, dynamic viewport/scissor, vertex input all work as expected
- Ensure `MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS=1` (the default) for single-
  thread GPU model

---

## 6. Summary of Gaps and Contradictions

### 6.1 v1 vs v2: Depth Format

- **v1**: `D16_UNORM` (matching Voodoo's 16-bit depth)
- **v2**: `D32_SFLOAT` (higher precision for W-buffer support)
- **Resolution**: v2 is correct. D32_SFLOAT accommodates both Z-buffer and
  W-buffer modes. The shader writes gl_FragDepth to map Voodoo depth values.

### 6.2 v1 vs v2: Y Axis Direction

- **v1** (OpenGL): Y flip required (`ndc_y = 1.0 - 2.0 * y / h`)
- **v2** (Vulkan): No Y flip (`ndc_y = 2.0 * y / h - 1.0`)
- **Resolution**: Vulkan clip space Y points down (same as Voodoo screen space).
  No negation needed. The v1 perspective-correction.md OpenGL code MUST NOT be
  used verbatim.

### 6.3 v1 vs v2: Push Constant Layout

- **v1 push-constant-layout.md**: 64-byte block, 16 fields. Complete with
  byte offsets, GLSL declarations, and packing analysis.
- **v2 DESIGN.md section 7.5**: References push-constant-layout.md for layout.
- **Resolution**: The v1 push constant layout is fully compatible with v2.
  The layout was designed for Vulkan push constants (not GL uniforms), so it
  requires no changes. The `archive/v1/research/push-constant-layout.md` file
  is the authoritative specification.

### 6.4 v1 vs v2: Shader Compilation

- **v1 cmake-integration.md**: Complete CMake integration plan for glslc +
  SpvToHeader.cmake.
- **v2 current CMakeLists.txt**: Has no shader compilation.
- **Resolution**: Implement the v1 plan during Phase 2. The v1 plan was
  already written for Vulkan SPIR-V, not OpenGL GLSL.

### 6.5 v1 vs v2: Vertex Format

- **v1 intercept-point.md**: Recommended reconstructing per-vertex data from
  gradients at `voodoo_queue_triangle()`. Provided concrete C code.
- **v1 perspective-correction.md**: Recommended `gl_Position.w = W` encoding
  with `noperspective` for colors and `smooth` for texcoords.
- **v2 DESIGN.md section 7.8**: Specifies 72-byte `vc_vertex_t` with
  `x, y, z, w, r, g, b, a, s0, t0, w0, s1, t1, w1, fog, pad[3]`.
- **Resolution**: The v2 vertex format is consistent with v1 recommendations.
  The `w` field corresponds to `1/W` (the `sW0` or `sWb` from the Voodoo).
  The reconstruction math from v1 is valid. The only addition in v2 is the
  `pad[3]` for 16-byte alignment (total 72 bytes).

### 6.6 Missing: Descriptor Set Layout

- **v1**: Specifies 3-binding descriptor set (TMU0, TMU1, fog table)
- **v2 DESIGN.md**: Same (section 7.7)
- **Phase 2**: No textures. Should descriptor set layout be created?
- **Resolution**: Recommended to defer descriptor set layout to Phase 4.
  Use `setLayoutCount = 0` in Phase 2 pipeline layout. This keeps Phase 2
  minimal and avoids dummy texture creation.

### 6.7 Missing: vc_vertex_t pad[3] Purpose

The DESIGN.md says "align to 16 bytes (12 bytes)" for pad[3]. The total is
72 bytes, which is NOT a multiple of 16. With pad[3], the struct is:
- 4 floats (x,y,z,w) = 16
- 4 floats (r,g,b,a) = 16
- 3 floats (s0,t0,w0) = 12
- 3 floats (s1,t1,w1) = 12
- 1 float (fog) = 4
- 3 floats (pad) = 12
- Total = 72

72 is NOT a power of two or multiple of 16. However, 72 IS a multiple of 4
(required by Vulkan vertex attribute alignment). The pad[3] ensures the fog
field is followed by 12 bytes to fill out a 16-byte group (fog + pad = 16
bytes). This does not affect Vulkan correctness. The padding is for POTENTIAL
cache line alignment of per-vertex data, but is not a hard requirement.

**Consideration**: If we want 16-byte alignment of the struct itself (for
SIMD-friendly memcpy), the size should be 80 bytes (add 8 more bytes of
padding). However, this wastes 8 bytes per vertex. At 3 vertices per
triangle, that is 24 bytes per triangle or ~240 KB per 10K-triangle frame.
This is negligible, but the current 72-byte layout works fine.

**Recommendation**: Use the 72-byte layout as specified in DESIGN.md. The
stride in the vertex input binding is set to 72 and Vulkan handles it.

### 6.8 Missing: Winding Order / Face Culling

The v1 intercept-point.md notes: "The vertex positions in params are Y-sorted
(A is topmost), which changes the original vertex order -- this affects winding
but OpenGL handles this via `params.sign`."

For Vulkan Phase 2: `cullMode = VK_CULL_MODE_NONE`, so winding does not
matter. For Phase 5 (depth test), the Voodoo does NOT do hardware face culling
at the rasterizer level (it culls in the setup engine before
`voodoo_queue_triangle()`). So face culling should remain NONE in the Vulkan
pipeline. The `params.sign` field is used by the Voodoo for frontFace
determination in the SOFTWARE culling path, but since we intercept AFTER the
setup engine has already culled, every triangle we receive has already passed
the cull test.

**Recommendation**: `VK_CULL_MODE_NONE` permanently. No per-triangle winding
adjustment needed.

---

## References

### Vulkan Specification
- Vulkan 1.2 Spec, Section 7.2: Render Pass Compatibility
- Vulkan 1.2 Spec, Section 8.1: VkRenderPassCreateInfo
- Vulkan 1.2 Spec, Section 14.6.1: Perspective interpolation formula
- [Render passes - Vulkan Tutorial](https://vulkan-tutorial.com/Drawing_a_triangle/Graphics_pipeline_basics/Render_passes)
- [Render Pass :: Vulkan Documentation Project](https://docs.vulkan.org/spec/latest/chapters/renderpass.html)

### MoltenVK
- [MoltenVK pixel format mapping (MVKPixelFormats.mm)](https://github.com/KhronosGroup/MoltenVK/blob/main/MoltenVK/MoltenVK/GPUObjects/MVKPixelFormats.mm)
- [MoltenVK D24 issue #888](https://github.com/KhronosGroup/MoltenVK/issues/888)
- [MTLPixelFormat.depth32Float (Apple)](https://developer.apple.com/documentation/metal/mtlpixelformat/depth32float)

### 86Box Source (Phase 1 implementation)
- `src/video/videocommon/vc_core.h` -- vc_ctx_t context structure
- `src/video/videocommon/vc_internal.h` -- Ring commands, capabilities, constants
- `src/video/videocommon/vc_thread.h` -- GPU thread and ring API
- `src/video/videocommon/CMakeLists.txt` -- Current Phase 1 build

### v1 Research (archived, cross-referenced)
- `videocommon-plan/archive/v1/research/vulkan-architecture.md` -- GL-to-VK mapping
- `videocommon-plan/archive/v1/research/intercept-point.md` -- Triangle intercept analysis
- `videocommon-plan/archive/v1/research/perspective-correction.md` -- W encoding technique
- `videocommon-plan/archive/v1/research/push-constant-layout.md` -- 64-byte push constant spec
- `videocommon-plan/archive/v1/research/uniform-mapping.md` -- Register bit mapping
- `videocommon-plan/archive/v1/research/cmake-integration.md` -- SPIR-V build pipeline

### v2 Design
- `videocommon-plan/DESIGN.md` -- Sections 7.3-7.8, 10, 11
- `videocommon-plan/PHASES.md` -- Phase 2 specification
