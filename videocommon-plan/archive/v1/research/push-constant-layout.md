# Voodoo Push Constant Layout Specification

Complete mapping from the 17 GL uniforms (see `uniform-mapping.md`) to a Vulkan
push constant block and descriptor set layout. Designed for the Voodoo uber-shader
architecture described in `DESIGN.md`.

Source documents:
- `uniform-mapping.md` -- 17 GL uniforms, register bit mapping, batch break analysis
- `vulkan-architecture.md` -- Vulkan 1.2 push constant block (Section 1), pipeline strategy (Section 2)
- `DESIGN.md` -- uber-shader architecture, phase plan
- `src/include/86box/vid_voodoo_common.h` -- `voodoo_params_t` struct definition
- `src/include/86box/vid_voodoo_regs.h` -- register bit macros, mselect enums
- `src/video/vid_voodoo_render.c` -- software rasterizer (reference implementation)

---

## 1. Push Constant Struct Definition (64 Bytes)

The Vulkan specification guarantees a minimum `maxPushConstantsSize` of 128 bytes.
Our layout targets 64 bytes -- a single L1 cache line on all target GPUs (Apple M1+,
NVIDIA, AMD, Intel, Broadcom V3D). This leaves 64 bytes of headroom for future
additions without exceeding the minimum guarantee.

### C-Side Struct

```c
/*
 * Push constant block for the Voodoo uber-shader.
 * 64 bytes total -- fits in a single GPU cache line.
 *
 * All fields are uint32_t or float (4 bytes each, naturally aligned).
 * No padding holes under std430 rules.
 *
 * Updated per-batch via vkCmdPushConstants().
 */
typedef struct vc_push_constants {
    /* --- Raw Voodoo registers (decoded with bitwise ops in shader) --- */
    uint32_t fbzMode;           /* offset  0: depth, stipple, dither, masks        */
    uint32_t fbzColorPath;      /* offset  4: color/alpha combine, texture enable  */
    uint32_t alphaMode;         /* offset  8: alpha test, alpha blend factors       */
    uint32_t fogMode;           /* offset 12: fog enable, fog source, fog math      */
    uint32_t textureMode0;      /* offset 16: TMU0 combine, perspective, trilinear */
    uint32_t textureMode1;      /* offset 20: TMU1 combine (same layout as TMU0)   */

    /* --- Packed color values (ARGB8888 as raw uint32) --- */
    uint32_t color0;            /* offset 24: combine local color (0xAARRGGBB)     */
    uint32_t color1;            /* offset 28: combine other color (0xAARRGGBB)     */
    uint32_t chromaKey;         /* offset 32: chroma key match value (0x00RRGGBB)  */
    uint32_t fogColor;          /* offset 36: fog blend color (0x00RRGGBB)         */
    uint32_t zaColor;           /* offset 40: constant depth (lo16) + bias (hi16)  */
    uint32_t stipple;           /* offset 44: 32-bit stipple pattern               */

    /* --- Detail/LOD parameters (packed per-TMU) --- */
    uint32_t detail0;           /* offset 48: TMU0 packed: scale[31:28] bias[27:20] max[19:12] */
    uint32_t detail1;           /* offset 52: TMU1 packed: same layout             */

    /* --- Framebuffer dimensions for NDC conversion (vertex shader) --- */
    float    fb_width;          /* offset 56: framebuffer width in pixels           */
    float    fb_height;         /* offset 60: framebuffer height in pixels          */
} vc_push_constants_t;

_Static_assert(sizeof(vc_push_constants_t) == 64,
               "Push constant block must be exactly 64 bytes");
```

### Detail Parameter Packing

Each `detail0`/`detail1` field packs three per-TMU values into a single uint32:

```c
/*
 * Pack detail texture parameters into a single uint32.
 *
 * Layout:
 *   bits 31:28  --  detail_scale ( 4 bits, unsigned, range 0..7)
 *   bits 27:20  --  detail_bias  ( 8 bits, signed, range -128..127)
 *   bits 19:12  --  detail_max   ( 8 bits, unsigned, range 0..255)
 *   bits 11:0   --  reserved / zero
 *
 * In the software rasterizer, detail_bias and detail_max are used as:
 *   factor = (detail_bias - lod) << detail_scale;
 *   if (factor > detail_max) factor = detail_max;
 *
 * The LOD value is computed per-pixel from the W depth. The bias/max/scale
 * are per-triangle constants from voodoo_params_t.
 */
static inline uint32_t
vc_pack_detail(int detail_bias, int detail_max, int detail_scale)
{
    return ((uint32_t)(detail_scale & 0xF)  << 28)
         | ((uint32_t)(detail_bias  & 0xFF) << 20)
         | ((uint32_t)(detail_max   & 0xFF) << 12);
}
```

### GLSL Push Constant Block (Fragment Shader)

```glsl
#version 450

layout(push_constant, std430) uniform PushConstants {
    /* Raw Voodoo registers -- bitwise-decoded in shader */
    uint  fbzMode;           /* offset  0 */
    uint  fbzColorPath;      /* offset  4 */
    uint  alphaMode;         /* offset  8 */
    uint  fogMode;           /* offset 12 */
    uint  textureMode0;      /* offset 16 */
    uint  textureMode1;      /* offset 20 */

    /* Packed ARGB8888 color values */
    uint  color0;            /* offset 24 */
    uint  color1;            /* offset 28 */
    uint  chromaKey;         /* offset 32 */
    uint  fogColor;          /* offset 36 */
    uint  zaColor;           /* offset 40 */
    uint  stipple;           /* offset 44 */

    /* Packed detail/LOD params per TMU */
    uint  detail0;           /* offset 48 */
    uint  detail1;           /* offset 52 */

    /* Framebuffer dimensions (vertex shader uses these) */
    float fb_width;           /* offset 56 */
    float fb_height;          /* offset 60 */
} pc;
```

### GLSL Push Constant Block (Vertex Shader)

The vertex shader uses the same push constant block declaration. In practice it
only reads `fb_width` and `fb_height` for the screen-space to NDC conversion. The
Vulkan spec allows both stages to access the same push constant range -- both
see the same data, with the cost of declaring the full block in both stages being
zero (push constants are embedded in the command stream, not per-stage storage).

```glsl
#version 450

layout(push_constant, std430) uniform PushConstants {
    /* Registers -- not used in vertex shader but must match fragment layout */
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

    /* Used by vertex shader for NDC conversion */
    float fb_width;
    float fb_height;
} pc;
```

---

## 2. Push Constants vs Descriptor Sets

### Principle

- **Push constants**: Per-batch pipeline state that changes with every batch break.
  Updated inline in the command buffer via `vkCmdPushConstants()`. Zero memory
  allocation, zero descriptor update overhead, essentially free.

- **Descriptor sets**: Texture bindings (VkImageView + VkSampler). Change when the
  game switches textures. Require `vkUpdateDescriptorSets()` and
  `vkCmdBindDescriptorSets()`.

### What Goes Where

| Data | Mechanism | Rationale |
|------|-----------|-----------|
| fbzMode, fbzColorPath, alphaMode, fogMode | Push constant (uint32 x4) | Changes every batch break, tiny, no indirection |
| textureMode0, textureMode1 | Push constant (uint32 x2) | Same -- per-batch state |
| color0, color1, chromaKey, fogColor, zaColor, stipple | Push constant (uint32 x6) | Same -- per-batch constants |
| detail0, detail1 | Push constant (uint32 x2) | Per-batch, rarely nonzero but always present |
| fb_width, fb_height | Push constant (float x2) | Changes only on framebuffer resize (rare) |
| TMU0 texture | Descriptor set 0, binding 0 | VkImageView+VkSampler, changes on texture rebind |
| TMU1 texture | Descriptor set 0, binding 1 | Same |
| Fog table (64x1 texture) | Descriptor set 0, binding 2 | Rarely changes (only on fog_table register writes) |

### Descriptor Set Layout

A single descriptor set (set 0) with three combined image sampler bindings:

```c
VkDescriptorSetLayoutBinding bindings[3] = {
    {
        .binding         = 0,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
    },
    {
        .binding         = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
    },
    {
        .binding         = 2,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
    },
};

VkDescriptorSetLayoutCreateInfo layout_info = {
    .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount = 3,
    .pBindings    = bindings,
};
```

GLSL descriptor declarations:

```glsl
layout(set = 0, binding = 0) uniform sampler2D tex0;       /* TMU0 (RGBA8) */
layout(set = 0, binding = 1) uniform sampler2D tex1;       /* TMU1 (RGBA8) */
layout(set = 0, binding = 2) uniform sampler2D fog_table;   /* 64x1 RG8 */
```

Note: `sampler1D` is not available in Vulkan SPIR-V. The fog table is uploaded as
a 64x1 `VkImage` with format `VK_FORMAT_R8G8_UNORM` and sampled as `sampler2D`
with the V coordinate fixed at 0.5.

### Descriptor Set Update Strategy

Descriptor sets are updated only when texture bindings change:

1. **Texture rebind**: When `tex_entry[0]` or `tex_entry[1]` changes between batches,
   allocate a new descriptor set from a pool and write the new image view + sampler.
2. **Fog table update**: When `fog_table[]` contents change (rare -- only on register
   writes to the fog table), re-upload the 64x1 texture and update the descriptor.
3. **VK_KHR_push_descriptor** (optional optimization): Where supported, use
   `vkCmdPushDescriptorSetKHR()` to update descriptors inline in the command buffer,
   avoiding descriptor set allocation entirely. Supported on MoltenVK.

---

## 3. Detail Parameters: Push Constants vs UBO

### Analysis

The detail texture parameters (`detail_bias`, `detail_max`, `detail_scale`) are
per-TMU values stored in `voodoo_params_t`. They are used in the texture combine
stage when `tc_mselect` or `tca_mselect` equals `TC_MSELECT_DETAIL` (value 4).

**How common is TC_MSELECT_DETAIL?**

In practice, detail texturing is uncommon:
- GLQuake: Never used (tc_mselect is always 0, 1, or 3)
- Quake 2 (Voodoo 2): Rarely used
- Unreal: Uses detail textures on some surfaces
- Most Glide 2.x games: Never used

However, the tc_mselect field is checked on every textured pixel regardless.
The shader must decode it from textureMode bits 16:14. When the value is 4
(DETAIL), the shader needs the detail parameters.

**Decision: Include in push constants.**

Rationale:
1. Only 8 bytes total (2 packed uint32 values for TMU0 + TMU1).
2. Even at 64 bytes total, we are well within the 128-byte minimum.
3. Avoids the complexity of a separate UBO path (buffer allocation, descriptor
   binding, synchronization) for 8 bytes of data.
4. When detail texturing is not active (the majority case), the shader reads
   these values but discards them after the mselect branch. The cost of
   including unused data in push constants is zero (they are already in the
   command stream).
5. A UBO would require a `VkBuffer` allocation, a descriptor set binding,
   and a memory write per batch -- all far more expensive than 8 bytes of
   push constant data.

**If detail params were NOT in push constants**, we would save 8 bytes (going
from 64 to 56 bytes) but gain:
- A new `VkBuffer` allocation for the UBO
- A new descriptor set binding (set 1) or an additional binding in set 0
- A buffer write + flush per batch that uses detail texturing
- Conditional logic to detect when detail params are needed

This is not worth the complexity. Keep them in push constants.

---

## 4. Packing Analysis

### Byte Layout with Offsets

```
Offset  Size  Type     Field           Contents
------  ----  ------   -----           --------
 0      4     uint32   fbzMode         Raw register: depth, stipple, dither, masks
 4      4     uint32   fbzColorPath    Raw register: color/alpha combine, tex enable
 8      4     uint32   alphaMode       Raw register: alpha test, blend factors
12      4     uint32   fogMode         Raw register: fog enable/source/math
16      4     uint32   textureMode0    Raw register: TMU0 combine/perspective
20      4     uint32   textureMode1    Raw register: TMU1 combine
                                       --- 24 bytes: raw registers ---
24      4     uint32   color0          Packed 0xAARRGGBB
28      4     uint32   color1          Packed 0xAARRGGBB
32      4     uint32   chromaKey       Packed 0x00RRGGBB
36      4     uint32   fogColor        Packed 0x00RRGGBB
40      4     uint32   zaColor         lo16 = constant depth, hi16 = depth bias
44      4     uint32   stipple         32-bit stipple pattern
                                       --- 24 bytes: color/misc constants ---
48      4     uint32   detail0         TMU0: scale[31:28] bias[27:20] max[19:12]
52      4     uint32   detail1         TMU1: same layout
                                       ---  8 bytes: detail params ---
56      4     float    fb_width         Framebuffer width (pixels)
60      4     float    fb_height        Framebuffer height (pixels)
                                       ---  8 bytes: vertex shader params ---
------
Total: 64 bytes (16 uint32-sized slots)
```

### Alignment Verification (std430 Rules)

GLSL `layout(push_constant, std430)` uses std430 packing rules:

| GLSL Type | Base Alignment | Size | Our Usage |
|-----------|---------------|------|-----------|
| `uint`    | 4 bytes       | 4    | 14 fields at offsets 0-52 (all 4-byte aligned) |
| `float`   | 4 bytes       | 4    | 2 fields at offsets 56-60 (4-byte aligned) |

All fields are scalar (uint or float), each 4 bytes with 4-byte alignment.
There are no vectors, matrices, or arrays in the push constant block, so there
are no padding holes. Every field is naturally aligned at its offset.

**No padding, no waste. 16 fields x 4 bytes = 64 bytes exactly.**

### Color Packing: uint32 vs vec4

The GL uniform mapping (`uniform-mapping.md`) proposed `vec4` for color0/color1
and `vec3` for chromaKey/fogColor. For push constants, we pack colors as raw
uint32 (ARGB8888) and unpack in the shader:

```glsl
/* Unpack 0xAARRGGBB to vec4(R, G, B, A) normalized */
vec4 unpackColor(uint c) {
    return vec4(
        float((c >> 16) & 0xFFu) / 255.0,   /* R */
        float((c >>  8) & 0xFFu) / 255.0,   /* G */
        float((c      ) & 0xFFu) / 255.0,   /* B */
        float((c >> 24) & 0xFFu) / 255.0    /* A */
    );
}

/* Unpack 0x00RRGGBB to vec3(R, G, B) normalized */
vec3 unpackRGB(uint c) {
    return vec3(
        float((c >> 16) & 0xFFu) / 255.0,
        float((c >>  8) & 0xFFu) / 255.0,
        float((c      ) & 0xFFu) / 255.0
    );
}
```

**Why uint32 instead of vec4?**

- `vec4` color0 + `vec4` color1 + `vec3` chromaKey + `vec3` fogColor =
  16 + 16 + 12 + 12 = 56 bytes for colors alone (with std430 vec3 padding to
  16 bytes each, this becomes 16 + 16 + 16 + 16 = 64 bytes)
- `uint32` color0 + color1 + chromaKey + fogColor = 4 + 4 + 4 + 4 = 16 bytes
- Savings: 40-48 bytes -- the difference between fitting in 64 bytes or not
- Cost: 4 bitwise ops + 4 float conversions per color unpack in the shader (trivial)

The `fogColor` field is packed from `rgbvoodoo_t` (struct with r, g, b bytes)
on the C side:

```c
pc.fogColor = ((uint32_t)params->fogColor.r << 16)
            | ((uint32_t)params->fogColor.g <<  8)
            | ((uint32_t)params->fogColor.b);
```

---

## 5. VkPushConstantRange Definition

Both vertex and fragment stages access the push constant block. Vulkan allows
specifying which stage(s) can see which byte ranges.

### Option A: Single Range, Both Stages (Recommended)

```c
VkPushConstantRange push_range = {
    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    .offset     = 0,
    .size       = sizeof(vc_push_constants_t),  /* 64 bytes */
};

VkPipelineLayoutCreateInfo layout_info = {
    .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount         = 1,
    .pSetLayouts            = &descriptor_set_layout,
    .pushConstantRangeCount = 1,
    .pPushConstantRanges    = &push_range,
};
```

**Rationale**: The entire 64-byte block is updated atomically with one
`vkCmdPushConstants()` call. Splitting into per-stage ranges would require
two `vkCmdPushConstants()` calls and gains nothing -- the vertex shader
reading 64 bytes instead of 8 has no measurable cost.

### Option B: Split Ranges (NOT Recommended)

For reference only -- do NOT use this approach:

```c
/* NOT RECOMMENDED: unnecessary complexity for zero performance gain */
VkPushConstantRange ranges[2] = {
    {   /* Fragment stage: registers + colors + detail */
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset     = 0,
        .size       = 56,  /* bytes 0-55 */
    },
    {   /* Vertex stage: fb_width, fb_height */
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset     = 56,
        .size       = 8,   /* bytes 56-63 */
    },
};
```

Splitting ranges requires overlapping declarations (the fragment shader still
needs to declare the full block to access fields at offsets 0-55, and the
vertex shader must pad up to offset 56). It also requires two
`vkCmdPushConstants()` calls if the ranges are non-overlapping. There is no
benefit.

---

## 6. Performance Notes

### Push Constants vs UBO Updates

| Mechanism | CPU Cost per Batch | GPU Cost per Batch | Memory Allocation |
|-----------|-------------------|-------------------|-------------------|
| Push constants (`vkCmdPushConstants`) | ~10 ns (memcpy into cmd buffer) | Zero (data is inline in cmd stream) | None |
| UBO (`vkCmdBindDescriptorSets` + buffer write) | ~50-200 ns (write + flush + bind) | ~5-10 ns (pointer dereference to read UBO) | Requires VkBuffer + VkDeviceMemory |
| SSBO (similar to UBO) | ~50-200 ns | ~10-20 ns (no caching guarantee) | Requires VkBuffer + VkDeviceMemory |

**Push constants are the fastest uniform update mechanism in Vulkan.** The data
is embedded directly in the command buffer stream -- when the GPU reaches the
push constant command, the data is already in the command processor's cache.
There is no pointer dereference, no memory fetch, no descriptor lookup.

### Cost Breakdown for Our 64-Byte Block

On the CPU side, `vkCmdPushConstants()` copies 64 bytes into the command buffer.
This is a single cache-line memcpy -- approximately 10 nanoseconds on modern CPUs.

At a typical batch count of 50-200 per frame:
- **Push constants**: 50 * 10 ns = 0.5 microseconds per frame
- **UBO equivalent**: 50 * 150 ns = 7.5 microseconds per frame (buffer write +
  descriptor update + bind)

The difference is negligible at Voodoo frame rates (60 Hz), but push constants
are simpler to implement -- no buffer management, no descriptor pool, no lifetime
tracking.

### Why Not a UBO for Everything?

The GL uniform mapping document noted that the 17 uniforms could alternatively
be a UBO. For Vulkan, push constants are strictly superior for our use case:

1. **Size**: 64 bytes is well within the 128-byte minimum. UBOs are appropriate
   when data exceeds push constant limits (e.g., bone matrices for skinning).
2. **Frequency**: Updated every batch break (potentially every triangle in the
   worst case). Push constants handle this with zero overhead.
3. **Simplicity**: No VkBuffer, no VkDeviceMemory, no descriptor set updates,
   no synchronization between CPU writes and GPU reads.

### 64 Bytes vs 128 Bytes

Fitting in 64 bytes (one cache line) has a measurable advantage on tile-based
GPUs (Pi 5 V3D, mobile GPUs) where the push constant data is replicated per-tile.
Halving the push constant size halves the per-tile replication overhead. On
desktop GPUs the difference is immaterial, but we get it for free by packing
colors as uint32 instead of vec4.

---

## 7. GLSL Declarations (Complete)

### Vertex Shader (`shaders/voodoo_uber.vert`)

```glsl
#version 450

/* ---- Push Constants ---- */
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

/* ---- Vertex Inputs ---- */
layout(location = 0) in vec2  in_position;    /* screen-space X, Y (pixels)   */
layout(location = 1) in float in_depth;       /* Z depth (0.0-1.0 normalized) */
layout(location = 2) in vec4  in_color;       /* RGBA (0.0-1.0 normalized)    */
layout(location = 3) in vec3  in_texcoord0;   /* TMU0: S/W, T/W, 1/W         */
layout(location = 4) in vec3  in_texcoord1;   /* TMU1: S/W, T/W, 1/W         */
layout(location = 5) in float in_oow;         /* base 1/W for fog/depth       */

/* ---- Outputs to Fragment Stage ---- */
layout(location = 0) noperspective out vec4  v_color;
layout(location = 1)               out vec3  v_texcoord0;   /* smooth (default) */
layout(location = 2)               out vec3  v_texcoord1;   /* smooth (default) */
layout(location = 3) noperspective out float v_depth;
layout(location = 4) noperspective out float v_fogDepth;    /* w_depth for fog  */

void main() {
    /* Convert screen-space pixel coords to Vulkan NDC [-1,+1] x [-1,+1].
     * Vulkan clip space: X right, Y down, Z into screen [0,1].
     * gl_Position.w encodes perspective for correct varying interpolation. */
    float W = (in_oow > 0.0) ? (1.0 / in_oow) : 1.0;
    float ndc_x = (2.0 * in_position.x / pc.fb_width)  - 1.0;
    float ndc_y = (2.0 * in_position.y / pc.fb_height) - 1.0;
    gl_Position = vec4(ndc_x * W, ndc_y * W, in_depth * W, W);

    /* Pass interpolants.
     * Colors use noperspective (Voodoo Gouraud is always affine).
     * Texture coords use smooth (default) -- perspective-corrected by W. */
    v_color     = in_color;
    v_texcoord0 = in_texcoord0;
    v_texcoord1 = in_texcoord1;
    v_depth     = in_depth;
    v_fogDepth  = in_oow;   /* 1/W, used for fog table index in fragment */
}
```

### Fragment Shader (`shaders/voodoo_uber.frag`)

```glsl
#version 450

/* ---- Push Constants (same block as vertex shader) ---- */
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

/* ---- Descriptor Set 0: Texture Samplers ---- */
layout(set = 0, binding = 0) uniform sampler2D tex0;       /* TMU0 (RGBA8)     */
layout(set = 0, binding = 1) uniform sampler2D tex1;       /* TMU1 (RGBA8)     */
layout(set = 0, binding = 2) uniform sampler2D fog_table;   /* 64x1 RG8         */

/* ---- Inputs from Vertex Stage ---- */
layout(location = 0) noperspective in vec4  v_color;
layout(location = 1)               in vec3  v_texcoord0;   /* smooth (default) */
layout(location = 2)               in vec3  v_texcoord1;   /* smooth (default) */
layout(location = 3) noperspective in float v_depth;
layout(location = 4) noperspective in float v_fogDepth;

/* ---- Output ---- */
layout(location = 0) out vec4 fragColor;

/* ---- Helper: unpack ARGB8888 uint to vec4(R,G,B,A) ---- */
vec4 unpackColor(uint c) {
    return vec4(
        float((c >> 16) & 0xFFu) / 255.0,
        float((c >>  8) & 0xFFu) / 255.0,
        float((c      ) & 0xFFu) / 255.0,
        float((c >> 24) & 0xFFu) / 255.0
    );
}

vec3 unpackRGB(uint c) {
    return vec3(
        float((c >> 16) & 0xFFu) / 255.0,
        float((c >>  8) & 0xFFu) / 255.0,
        float((c      ) & 0xFFu) / 255.0
    );
}

/* ---- Helper: unpack detail params ---- */
void unpackDetail(uint packed, out int bias, out int maxVal, out int scale) {
    scale  = int((packed >> 28) & 0xFu);
    bias   = int((packed >> 20) & 0xFFu);
    if (bias >= 128) bias -= 256;           /* sign-extend 8-bit */
    maxVal = int((packed >> 12) & 0xFFu);
}

/* ---- fbzMode bit extraction macros ---- */
/* (Full pipeline stages implemented here -- only push constant
 *  access patterns shown for this specification document.) */

void main() {
    /* All pipeline state decoded from push constants via bitwise ops.
     * See uniform-mapping.md for complete register bit documentation.
     *
     * Example bit extractions:
     *   bool stipple_enable  = (pc.fbzMode & (1u << 2)) != 0u;
     *   bool w_buffer        = (pc.fbzMode & (1u << 3)) != 0u;
     *   bool dither_enable   = (pc.fbzMode & (1u << 8)) != 0u;
     *   uint depth_op        = (pc.fbzMode >> 5) & 0x7u;
     *   bool tex_enabled     = (pc.fbzColorPath & (1u << 27)) != 0u;
     *   uint alpha_func      = (pc.alphaMode >> 1) & 0x7u;
     *   uint alpha_ref       = (pc.alphaMode >> 24) & 0xFFu;
     *   bool fog_enable      = (pc.fogMode & 1u) != 0u;
     *   uint tc_mselect      = (pc.textureMode0 >> 14) & 0x7u;
     *
     * Colors unpacked on demand:
     *   vec4 c0 = unpackColor(pc.color0);
     *   vec4 c1 = unpackColor(pc.color1);
     *   vec3 ck = unpackRGB(pc.chromaKey);
     *   vec3 fc = unpackRGB(pc.fogColor);
     */

    /* ... full uber-shader pipeline stages ... */
    fragColor = vec4(1.0);  /* placeholder */
}
```

---

## 8. Push Constant Update Function

### C Function: State Extraction from voodoo_params_t

```c
#include <vulkan/vulkan.h>
#include <86box/vid_voodoo_common.h>

/*
 * Extract pipeline state from voodoo_params_t and update push constants.
 *
 * Called per batch break -- when any push constant field differs between
 * consecutive triangles. The batch detection logic compares the previous
 * vc_push_constants_t against the newly extracted one (64-byte memcmp or
 * field-wise comparison).
 *
 * @param cmd     Active command buffer (in recording state)
 * @param layout  Pipeline layout containing the push constant range
 * @param params  Voodoo triangle parameters (per-triangle state)
 * @param fb_w    Framebuffer width in pixels
 * @param fb_h    Framebuffer height in pixels
 */
void
vc_push_constants_update(VkCommandBuffer cmd,
                         VkPipelineLayout layout,
                         const voodoo_params_t *params,
                         float fb_w,
                         float fb_h)
{
    vc_push_constants_t pc;

    /* Raw register pass-through -- zero extraction cost */
    pc.fbzMode       = params->fbzMode;
    pc.fbzColorPath  = params->fbzColorPath;
    pc.alphaMode     = params->alphaMode;
    pc.fogMode       = params->fogMode;
    pc.textureMode0  = params->textureMode[0];
    pc.textureMode1  = params->textureMode[1];

    /* Color values -- pack from source format to ARGB8888 uint32.
     * color0 and color1 are already stored as uint32 ARGB in params. */
    pc.color0    = params->color0;
    pc.color1    = params->color1;
    pc.chromaKey = params->chromaKey;

    /* fogColor is stored as rgbvoodoo_t {b, g, r, pad} -- pack to uint32 */
    pc.fogColor  = ((uint32_t)params->fogColor.r << 16)
                 | ((uint32_t)params->fogColor.g <<  8)
                 | ((uint32_t)params->fogColor.b);

    pc.zaColor  = params->zaColor;
    pc.stipple  = params->stipple;

    /* Detail/LOD parameters -- pack 3 ints into 1 uint32 per TMU */
    pc.detail0 = vc_pack_detail(params->detail_bias[0],
                                params->detail_max[0],
                                params->detail_scale[0]);
    pc.detail1 = vc_pack_detail(params->detail_bias[1],
                                params->detail_max[1],
                                params->detail_scale[1]);

    /* Framebuffer dimensions for vertex shader NDC conversion */
    pc.fb_width  = fb_w;
    pc.fb_height = fb_h;

    /* Push the entire 64-byte block to both vertex and fragment stages */
    vkCmdPushConstants(cmd, layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,                          /* offset */
                       sizeof(vc_push_constants_t), /* size = 64 */
                       &pc);
}
```

### Batch Break Detection

The batch detection logic compares push constant state between consecutive
triangles. A batch break occurs when any push constant field changes, OR when
a texture binding or clip rect changes (those are not in push constants).

```c
/*
 * Compare two push constant blocks for equality.
 * Returns true if all fields match (no batch break needed).
 *
 * Using memcmp on the 64-byte block is optimal -- it compiles to a single
 * 512-bit comparison on AVX-512, or two 256-bit comparisons on AVX2, or
 * four 128-bit comparisons on NEON. All complete in 1-2 cycles.
 */
static inline bool
vc_push_constants_equal(const vc_push_constants_t *a,
                        const vc_push_constants_t *b)
{
    return memcmp(a, b, sizeof(vc_push_constants_t)) == 0;
}

/*
 * Full batch break detection includes push constants + texture bindings +
 * clip rect. This is the complete set of state that affects rendering output.
 */
typedef struct vc_batch_state {
    vc_push_constants_t pc;        /* 64 bytes: push constant block */
    int                 tex0_id;   /* texture cache entry ID for TMU0 */
    int                 tex1_id;   /* texture cache entry ID for TMU1 */
    int                 clip_l;    /* clip rect left */
    int                 clip_r;    /* clip rect right */
    int                 clip_lo;   /* clip rect low Y */
    int                 clip_hi;   /* clip rect high Y */
} vc_batch_state_t;

static inline bool
vc_batch_state_equal(const vc_batch_state_t *a, const vc_batch_state_t *b)
{
    return memcmp(a, b, sizeof(vc_batch_state_t)) == 0;
}
```

---

## 9. Register Bit Reference (Quick Lookup)

For convenience, the most frequently accessed push constant bits in the shader:

### fbzMode (offset 0)

| Bit(s) | Name | Shader Usage |
|--------|------|-------------|
| 1 | FBZ_CHROMAKEY | Chroma key enable -> `discard` |
| 2 | FBZ_STIPPLE | Stipple test enable |
| 3 | FBZ_W_BUFFER | W-buffer mode -> custom `gl_FragDepth` |
| 8 | FBZ_DITHER | Dither enable |
| 11 | FBZ_DITHER_2x2 | 2x2 dither (else 4x4) |
| 12 | FBZ_STIPPLE_PATT | Stipple mode: 0=rotating, 1=pattern |
| 13 | FBZ_ALPHA_MASK | Alpha mask test -> `discard` if !(aother & 1) |
| 16 | FBZ_DEPTH_BIAS | Depth bias enable (add zaColor) |
| 19 | FBZ_DITHER_SUB | Dither subtraction on dest reads |
| 20 | FBZ_DEPTH_SOURCE | Depth source: 0=iterated, 1=zaColor |

### fbzColorPath (offset 4)

| Bit(s) | Name | Shader Usage |
|--------|------|-------------|
| 1:0 | rgb_sel | Color other source (iterated/tex/color1/LFB) |
| 3:2 | a_sel | Alpha other source |
| 4 | cc_localselect | Color local: 0=iterated, 1=color0 |
| 6:5 | cca_localselect | Alpha local: 0=itA, 1=color0.a, 2=itZ |
| 7 | cc_localselect_override | Override by tex alpha bit 7 |
| 8 | cc_zero_other | Zero color other before combine |
| 9 | cc_sub_clocal | Subtract clocal |
| 12:10 | cc_mselect | Color blend factor (0-5) |
| 13 | cc_reverse_blend | Reverse blend factor |
| 15:14 | cc_add | Color add (0=none, 1=clocal, 2=alocal) |
| 16 | cc_invert_output | XOR output with 0xFF |
| 17-25 | cca_* | Alpha combine (same structure as cc_*) |
| 27 | FBZCP_TEXTURE_ENABLED | Texture sampling enable |

### alphaMode (offset 8)

| Bit(s) | Name | Shader Usage |
|--------|------|-------------|
| 0 | alpha_test_enable | Alpha test -> `discard` |
| 3:1 | alpha_func | Comparison function (NEVER..ALWAYS) |
| 31:24 | a_ref | Alpha reference value (0-255) |

Note: Bits 4-23 (blend enable, blend factors) are mapped to Vulkan
fixed-function state via the pipeline key, NOT decoded in the shader.
The shader never reads blend state from the push constant.

### fogMode (offset 12)

| Bit(s) | Name | Shader Usage |
|--------|------|-------------|
| 0 | FOG_ENABLE | Master fog enable |
| 1 | FOG_ADD | Additive fog (use zero base) |
| 2 | FOG_MULT | Multiply-only fog |
| 4:3 | FOG_SOURCE | 00=table, 01=alpha, 10=Z, 11=W |
| 5 | FOG_CONSTANT | Constant fog (skip table) |

### textureMode0/1 (offsets 16, 20)

| Bit(s) | Name | Shader Usage |
|--------|------|-------------|
| 12 | tc_zero_other | Zero TMU1 output |
| 13 | tc_sub_clocal | Subtract local texture |
| 16:14 | tc_mselect | Blend factor (0=zero..5=LOD) |
| 17 | tc_reverse_blend | Reverse factor |
| 18 | tc_add_clocal | Add local color |
| 19 | tc_add_alocal | Add local alpha |
| 20 | tc_invert_output | Invert RGB output |
| 21-29 | tca_* | Alpha combine (same structure) |
| 30 | TRILINEAR | Trilinear mode flag |

---

## 10. Future Expansion Budget

With 64 bytes used out of 128 bytes guaranteed, we have 64 bytes of headroom:

| Potential Future Field | Size | Priority |
|----------------------|------|----------|
| LOD bias per TMU (float x2) | 8 bytes | Medium -- needed if GPU LOD computation diverges from CPU |
| SLI even/odd line flag | 4 bytes | Low -- SLI is rare |
| Copy-on-blend source rect (ivec4) | 16 bytes | Medium -- needed for ACOLORBEFOREFOG exotic blend |
| Aux buffer mode flags | 4 bytes | Low -- alpha buffer mode |
| Per-frame time / animation | 4 bytes | Low -- not relevant to Voodoo |
| **Total potential** | **~36 bytes** | Fits within 64-byte headroom |

The layout is designed to be extended by appending new fields after `fb_height`.
Existing shaders that do not reference the new fields continue to work -- SPIR-V
push constant blocks are accessed by offset, not by name. A shader that declares
a 64-byte block is compatible with a pipeline layout that defines a 96-byte range
(the shader simply ignores the trailing bytes).

---

## Summary

| Property | Value |
|----------|-------|
| Total push constant size | **64 bytes** |
| Number of fields | 16 (14 uint32 + 2 float) |
| Alignment | std430, all fields 4-byte aligned, no padding |
| Cache lines | 1 (on all target GPUs) |
| Vulkan minimum budget used | 50% (64 of 128 bytes) |
| Headroom for expansion | 64 bytes |
| Update mechanism | `vkCmdPushConstants()` -- single call, both stages |
| Stage visibility | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
| Descriptor set bindings | 3 combined image samplers (set 0, bindings 0-2) |
| CPU cost per update | ~10 ns (64-byte memcpy into command buffer) |
