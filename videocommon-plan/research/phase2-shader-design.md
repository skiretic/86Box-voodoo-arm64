# Phase 2 Shader Design Research

**Date**: 2026-03-01
**Phase**: 2 (Basic Rendering -- flat-shaded triangles, no textures, no blending, no depth test)
**Author**: vc-arch agent

---

## Table of Contents

1. [v1 Research Review](#1-v1-research-review)
2. [v2 Design Spec Review](#2-v2-design-spec-review)
3. [Software Rasterizer Pixel Pipeline Flow](#3-software-rasterizer-pixel-pipeline-flow)
4. [Emulator Uber-Shader Patterns](#4-emulator-uber-shader-patterns)
5. [GLSL/Vulkan/MoltenVK Compatibility Notes](#5-glslvulkanmoltenvk-compatibility-notes)
6. [Phase 2 Shader Recommendations](#6-phase-2-shader-recommendations)
7. [Gaps and Watch Items](#7-gaps-and-watch-items)

---

## 1. v1 Research Review

### 1.1 Uniform Mapping (`archive/v1/research/uniform-mapping.md`)

**Summary**: Comprehensive mapping of ALL Voodoo registers to shader uniforms. Still
fully valid for v2. Key findings that carry forward:

- **Raw register pass-through**: Six registers (fbzMode, fbzColorPath, alphaMode,
  fogMode, textureMode[0], textureMode[1]) should be passed as raw uint32 and decoded
  with bitwise ops in the shader. This reduces uniform count from ~40 to 14.
- **fbzMode bit layout**: Well-documented (clip=0, chromakey=1, stipple=2, w-buffer=3,
  depth-enable=4, depth-op=7:5, dither=8, RGB-wmask=9, depth-wmask=10, dither-2x2=11,
  stipple-mode=12, alpha-mask=13, draw-buffer=15:14, depth-bias=16, Y-origin=17,
  alpha-enable=18, dithersub=19, depth-source=20).
- **fbzColorPath bit layout**: rgb_sel=1:0, a_sel=3:2, cc_localselect=4,
  cca_localselect=6:5, cc_localselect_override=7, cc_zero_other=8, cc_sub_clocal=9,
  cc_mselect=12:10, cc_reverse_blend=13, cc_add=15:14, cc_invert_output=16,
  cca_* mirror structure at bits 17-25, texture_enabled=27.
- **Alpha blend factor mapping**: Voodoo AFUNC 0-7 map directly to VkBlendFactor.
  AFUNC_ACOLORBEFOREFOG (dest_afunc=0xF) requires shader-based blending.
- **Fog table**: 64x1 VkImage (VK_FORMAT_R8G8_UNORM) as sampler2D (sampler1D not
  available in Vulkan SPIR-V).
- **Batch break analysis**: All 6 raw registers + texture bindings + clip rect form
  the batch state. GLQuake typically achieves 50-200+ triangle batches.

**Status**: All findings valid for v2. No changes needed.

### 1.2 Push Constant Layout (`archive/v1/research/push-constant-layout.md`)

**Summary**: 64-byte push constant block with 16 fields (14 uint + 2 float). This is
the authoritative layout. Key findings:

```
Offset  Size  Type     Field
------  ----  ------   -----
 0      4     uint32   fbzMode
 4      4     uint32   fbzColorPath
 8      4     uint32   alphaMode
12      4     uint32   fogMode
16      4     uint32   textureMode0
20      4     uint32   textureMode1
24      4     uint32   color0          (0xAARRGGBB)
28      4     uint32   color1          (0xAARRGGBB)
32      4     uint32   chromaKey       (0x00RRGGBB)
36      4     uint32   fogColor        (0x00RRGGBB)
40      4     uint32   zaColor         (lo16=depth, hi16=bias)
44      4     uint32   stipple
48      4     uint32   detail0         (packed scale/bias/max)
52      4     uint32   detail1         (packed scale/bias/max)
56      4     float    fb_width
60      4     float    fb_height
------
Total: 64 bytes (50% of 128-byte Vulkan minimum)
```

- Colors packed as uint32 ARGB (NOT vec4) -- saves 40+ bytes.
- All std430 aligned (4-byte scalar types only, no padding holes).
- Single VkPushConstantRange with VERTEX_BIT | FRAGMENT_BIT.
- Helper functions: `unpackColor(uint c)`, `unpackRGB(uint c)`.
- Batch state comparison via 64-byte memcmp.
- 64-byte headroom for future fields (LOD bias, SLI, etc.).

**Status**: All findings valid for v2. This IS the definitive layout. Note that
`videocommon-plan/push-constant-layout.md` does NOT exist outside the archive --
the authoritative layout lives only in the v1 archive. v2 DESIGN.md section 7.5
references it but doesn't duplicate it.

### 1.3 Perspective Correction (`archive/v1/research/perspective-correction.md`)

**Summary**: Deep analysis of how to achieve Voodoo's perspective-correct texturing
using OpenGL/Vulkan hardware interpolation. Key findings:

- **The technique**: `gl_Position = vec4(ndc * W, z * W, W)` where `W = 1.0 / oow`
- After hardware perspective divide, NDC position is correct.
- Hardware's perspective formula reproduces Voodoo's S/W, T/W, 1/W pipeline.
- **Colors**: MUST use `noperspective` (Voodoo Gouraud is always affine/linear).
- **Depth**: MUST use `noperspective` (Voodoo Z is linearly interpolated).
- **Texture coords**: Use default `smooth` (perspective-correct, driven by W).
- **Affine mode**: When textureMode bit 0 = 0, pass `oow = 0` so `W = 1.0`,
  making smooth varyings degenerate to linear.
- **Separate TMU W**: Rare. Use TMU0 W for gl_Position.w; if TMU1 has different W,
  pass as noperspective and divide in fragment shader.
- **PCSX2** uses identical technique for PS2 GS STQ coords.
- **DOSBox** uses legacy glTexCoord4f(s*z, t*z, 0, 1/z) with texture2DProj.

**Status**: All findings valid for v2 (same math in Vulkan as OpenGL). The only
adaptation needed: Vulkan NDC Y is top-down (same as Voodoo), no Y flip needed.
OpenGL needed Y flip; Vulkan does NOT.

**CRITICAL for Phase 2**: Even though Phase 2 has no textures, the vertex shader
must still set `gl_Position.w` correctly using the `oow` attribute. This ensures
that when textures are added in Phase 4, the interpolation is already correct.
For Phase 2, `noperspective` colors will interpolate identically regardless of
W value, so the W setup is harmless but forward-compatible.

---

## 2. v2 Design Spec Review

### 2.1 DESIGN.md Section 7.5 -- Graphics Pipeline (Uber-Shader)

Key specifications from the authoritative design doc:

- One SPIR-V vertex shader + one SPIR-V fragment shader, compiled offline from GLSL.
- **Vertex shader**: Pass-through position, color, texture coords, fog. No transform
  (Voodoo operates in screen space with pre-divided coordinates).
- **Fragment shader**: Full Voodoo pixel pipeline as uber-shader.
- **Push constants (64 bytes)**: Encode per-triangle register state.
- **Dynamic state**: Viewport, scissor (always), depth test/write/compare (via EDS),
  blend (via EDS3, or baked pipeline on MoltenVK).

### 2.2 DESIGN.md Section 7.8 -- Vertex Format

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

**Note**: This is 72 bytes per vertex (with pad), or could be 48 bytes if TMU1 is
unused. The pad[3] brings total to 72 which is 16-byte aligned (good for GPU cache).

### 2.3 PHASES.md Section Phase 2

Phase 2 specifies:
- Vertex input: position (float4), color (float4)
- No blending, no depth test
- Push constants: fbzMode, fbzColorPath (just enough for color selection)
- Load SPIR-V from embedded data

**Tension**: PHASES.md says "just enough push constants" but DESIGN.md says "full
64-byte layout." The recommendation (see Section 6) is to declare the FULL push
constant block and FULL vertex format from day one, so they never change. Phase 2
simply ignores most fields (sets them to zero or defaults).

---

## 3. Software Rasterizer Pixel Pipeline Flow

Extracted from `src/video/vid_voodoo_render.c` function `voodoo_half_triangle()`,
lines 672-1530. The per-pixel pipeline executes in this order:

```
1. STIPPLE TEST           (fbzMode bit 2, bit 12)
   - Rotating or pattern-based pixel discard

2. DEPTH COMPUTATION      (fbzMode bits 3, 16, 20)
   - Source: iterated Z, W-depth (log table), or zaColor constant
   - Optional depth bias (add zaColor)

3. DEPTH TEST             (fbzMode bits 4, 7:5)
   - Compare against aux buffer (8 compare functions)
   - Skip pixel on fail

4. DESTINATION READ       (always, for blend)
   - Read RGB565 from framebuffer, unpack to 8-bit
   - Read alpha from aux buffer (if alpha_enable)

5. TEXTURE FETCH          (fbzColorPath bit 27)
   - TMU1 fetch + TMU1 combine (if dual TMU active)
   - TMU0 fetch + TMU0 combine
   - Perspective divide (textureMode bit 0)
   - LOD computation, bilinear filtering

6. COLOR LOCAL SELECT     (fbzColorPath bits 4, 7)
   - clocal = iterated RGB or color0
   - Override by texture alpha bit 7 (if bit 7 set)

7. COLOR OTHER SELECT     (fbzColorPath bits 1:0)
   - cother = iterated RGB, texture color, color1, or LFB read

8. CHROMA KEY TEST        (fbzMode bit 1)
   - Compare cother against chromaKey, discard on match

9. ALPHA LOCAL SELECT     (fbzColorPath bits 6:5)
   - alocal = iterated A, color0.a, or iterated Z

10. ALPHA OTHER SELECT    (fbzColorPath bits 3:2)
    - aother = iterated A, texture A, or color1.a

11. ALPHA MASK TEST       (fbzMode bit 13)
    - Discard if !(aother & 1)

12. COLOR COMBINE         (fbzColorPath bits 8-16)
    - src = zero_other ? 0 : cother
    - if sub_clocal: src -= clocal
    - msel = factor_select(mselect) [zero/clocal/aother/alocal/tex_a/tex_rgb]
    - if !reverse_blend: msel = ~msel
    - src = (src * (msel+1)) >> 8
    - add: src += clocal or alocal
    - clamp [0,255]
    - if invert_output: src ^= 0xFF

13. ALPHA COMBINE         (fbzColorPath bits 17-25)
    - Same structure as color combine but scalar

14. SAVE COLOR-BEFORE-FOG (for ACOLORBEFOREFOG blend)
    - colbfog_r/g/b = src_r/g/b

15. FOG APPLICATION       (fogMode bits 0-5)
    - fog source: table lookup (w_depth), iterated alpha, Z, or W
    - fog math: constant add, lerp with fogColor, or multiply
    - Applied to src_r/g/b

16. ALPHA TEST            (alphaMode bits 0-3, 31:24)
    - Compare src_a against a_ref (8 compare functions)
    - Discard on fail

17. DITHER SUBTRACTION    (fbzMode bit 19)
    - Undo dither on destination pixels before blend

18. ALPHA BLEND           (alphaMode bits 4-15)
    - src and dest blend factors (16 Voodoo AFUNC values)
    - Standard: dst * dst_factor + src * src_factor

19. OUTPUT DITHER          (fbzMode bits 8, 11)
    - 4x4 or 2x2 Bayer, quantize to RGB565

20. FRAMEBUFFER WRITE     (fbzMode bit 9)
    - Write RGB565 to framebuffer (if RGB_WMASK set)

21. AUX BUFFER WRITE      (fbzMode bits 10, 18)
    - Write depth to aux buffer (if depth_enable + depth_wmask)
    - Or write alpha to aux buffer (if alpha_enable + depth_wmask)
```

**Phase 2 simplification**: Steps 1-4 are skipped (no depth test), step 5 is
skipped (no texture), steps 6-7 select iterated color, steps 8-11 are skipped
(no chroma key, no alpha mask), step 12 produces iterated color (with the
default color combine: zero_other=0, sub_clocal=0, mselect=0/zero, reverse=0,
add=none, invert=0 means src = cother * (0+1) >> 8 = 0 -- WAIT. That would
be zero. See analysis below), steps 13-21 are all skipped.

**CRITICAL: Default color combine behavior**

The "simplest" fbzColorPath is NOT just "pass through iterated color." The
color combine always runs. With default settings (all bits 8-16 = 0):
- cc_zero_other = 0, so src = cother
- cc_sub_clocal = 0, so no subtraction
- cc_mselect = 0 (CC_MSELECT_ZERO), so msel = 0
- cc_reverse_blend = 0, so msel = ~0 = 0xFF, then msel++ = 0x100... no wait.

Let me re-read the code more carefully:

```c
if (!cc_reverse_blend) {
    msel_r ^= 0xff;  // If reverse=0, invert factor
}
msel_r++;  // Add 1

// So with mselect=ZERO (msel=0) and reverse=0:
// msel = 0 ^ 0xFF = 0xFF, then 0xFF + 1 = 0x100
// src = (src * 0x100) >> 8 = src  (identity!)
```

So the default combine (mselect=ZERO, reverse=0) is actually an identity:
`src = (cother * 256) >> 8 = cother`. The "zero" factor with "reverse" off
gives factor=256/256=1.0. This is correct -- the default color combine is
pass-through.

For Phase 2, if we set fbzColorPath = 0 (all bits zero):
- rgb_sel = 0 = iterated RGB -> cother = iterated color
- cc_zero_other = 0 -> src = cother
- cc_sub_clocal = 0 -> no sub
- cc_mselect = 0 = ZERO, cc_reverse_blend = 0 -> factor = 1.0
- cc_add = 0 -> no add
- cc_invert_output = 0 -> no invert
- Result: src = iterated color (pass-through)

Same logic for alpha combine (cca_* bits 17-25 all zero -> pass-through).

**So the Phase 2 fragment shader can literally just output `v_color`.** The full
color combine logic can be stubbed out, and it will produce the same result as
the SW rasterizer with fbzColorPath = 0.

---

## 4. Emulator Uber-Shader Patterns

### 4.1 Dolphin (GameCube/Wii)

**Source**: [Dolphin Ubershaders Blog Post](https://dolphin-emu.org/blog/2017/07/30/ubershaders/),
[Dolphin Uber-Shader GLSL Gist](https://gist.github.com/phire/25181a9bfd957ac68ea8c74afdd9e9e1)

**Architecture**: Hybrid approach -- uber-shaders handle ALL TEV configs immediately,
while specialized shaders compile asynchronously in the background. Once ready, the
specialized shader replaces the uber-shader.

**Shader structure**:
- **UBO-based state**: Two uniform buffers (PSBlock for fragment, VSBlock for vertex)
  carry ALL GPU register state per draw call.
- **Loop over TEV stages**: `for(uint stage = 0u; stage <= num_stages; stage++)` where
  `num_stages` comes from the UBO. Each iteration is a full TEV color+alpha combine.
- **Switch-case branching**: State bits extracted from UBO, used in switch statements.
  The compiler unrolls the loop and optimizes dead branches.
- **Integer arithmetic**: Colors stored as integers (0-255 range), not floats. All
  combine math is integer `(a * b) >> 8`. This matches Voodoo hardware exactly.
- **Early-Z**: `layout(early_fragment_tests) in` declared when safe (no alpha test,
  no late depth write).

**Key performance insight**: "GPUs shouldn't really be able to run these at playable
speeds, but they do." Modern GPUs handle uniform-driven branching well because all
fragments in a warp/wavefront take the same branch (state is per-draw, not per-pixel).

**Relevance to Voodoo**: The Voodoo pipeline is MUCH simpler than GameCube TEV
(fixed 1-2 TMU stages vs dynamic 16-stage TEV). If Dolphin can run a 16-stage
uber-shader at playable speeds, our single-stage Voodoo uber-shader will be trivial.

### 4.2 DuckStation (PlayStation 1)

**Source**: [gpu_hw_shadergen.cpp](https://github.com/stenzek/duckstation/blob/master/src/core/gpu_hw_shadergen.cpp)

**Architecture**: Compile-time shader variants via preprocessor macros, NOT runtime
uber-shader branching. Different shader permutations for:
- TEXTURED vs untextured
- TRANSPARENCY vs opaque (with 4 transparency modes: 0-3)
- SHADER_BLENDING vs hardware blend
- Various texture filtering modes

**Shader structure**:
- **Preprocessor-driven**: `#if TRANSPARENCY_MODE == 0`, `#ifdef TEXTURED`, etc.
  Dead code eliminated at compile time.
- **nointerpolation**: Used for discrete values like UV limits and texture page data
  (equivalent to GLSL `flat`).
- **Uniform buffer**: Contains texture window, alpha factors, interlace field.
- **Integer arithmetic**: VRAM is 16-bit, shader works in integer color space.

**Why not an uber-shader?** The PS1 GPU has relatively few state combinations compared
to GameCube TEV. DuckStation generates ~20-50 shader permutations, which is manageable.
The PS1 also has no programmable combine stages -- just a fixed texture+color path.

**Relevance to Voodoo**: DuckStation's approach would work for Voodoo too (the Voodoo
has similarly few configurations), but we chose the uber-shader approach per DESIGN.md
to avoid async shader compilation complexity. The Voodoo uber-shader cost is negligible.

### 4.3 Key Takeaways for VideoCommon

1. **Uniform-based branching is fine**: Dolphin proves that per-draw branching in
   the fragment shader has negligible cost when all fragments take the same path.
   Voodoo state is per-triangle (per-batch), so all fragments in a draw call see
   the same register values.

2. **Integer arithmetic matches hardware**: Both Dolphin and the Voodoo SW rasterizer
   use integer color math (0-255 range, `(a * b) >> 8`). However, GLSL float math
   with `floor()` and `/ 255.0` produces equivalent results and is more natural for
   GPU shaders. The SW rasterizer's integer math is an artifact of CPU optimization,
   not a precision requirement.

3. **Switch-case over dynamic indexing**: Dolphin learned that `switch(index) { case 0: ...; case 1: ...; }` is faster than array indexing (`table[index]`) in shaders.
   Use switch for cc_mselect, cca_mselect, tc_mselect, etc.

4. **Full layout from day one**: Both Dolphin and DuckStation define their UBO/push
   constant layouts once and never change them. Fields unused by the current rendering
   mode are simply ignored. Phase 2 should do the same.

---

## 5. GLSL/Vulkan/MoltenVK Compatibility Notes

### 5.1 Push Constants in GLSL for Vulkan

**Standard**: `layout(push_constant, std430) uniform PushConstants { ... } pc;`

Key rules (from [Vulkan Push Constants Guide](https://docs.vulkan.org/guide/latest/push_constants.html)):
- Push constants use std430 layout by default
- Guaranteed minimum `maxPushConstantsSize`: 128 bytes
- All scalar types (uint, float) are 4-byte aligned with 4-byte size
- No padding holes when using only scalar types (our case)
- Single `VkPushConstantRange` covering both vertex and fragment stages is correct
- Pipeline layout must match between `vkCmdPushConstants` and `vkCmdBindPipeline`

### 5.2 Interpolation Qualifiers

**GLSL declarations**:
```glsl
noperspective out vec4 v_color;     // Linear screen-space interpolation
              out vec3 v_texcoord0; // Default 'smooth' = perspective-correct
flat          out uint v_flags;     // No interpolation (constant across triangle)
```

**Vulkan/SPIR-V mapping** (from [SPIR-V Mappings](https://docs.vulkan.org/glsl/latest/chapters/spirvmappings.html)):
- `noperspective` -> SPIR-V `NoPerspective` decoration
- `smooth` (default) -> no decoration (perspective-correct is default in SPIR-V)
- `flat` -> SPIR-V `Flat` decoration

### 5.3 MoltenVK / Metal Compatibility

**Metal equivalent of `noperspective`**: Metal Shading Language provides
`[[center_no_perspective]]` as the sampling attribute for linear interpolation.
SPIRV-Cross correctly translates the SPIR-V `NoPerspective` decoration to this
Metal attribute. Confirmed working on all Apple Silicon (M1, M2, M3 families).

**No known issues** with:
- Push constants (translated to Metal argument buffers)
- `noperspective` interpolation
- std430 layout
- Integer bitfield extraction in fragment shader

**Known limitation**: Metal does NOT support `sampler1D`. The fog table must be
a 64x1 `sampler2D` (already specified in v1 research). Not relevant for Phase 2.

### 5.4 Vulkan NDC Coordinate System

**Vulkan** (differs from OpenGL):
- NDC X: [-1, +1], left to right
- NDC Y: [-1, +1], **top to bottom** (OpenGL is bottom to top)
- NDC Z: [0, 1] (OpenGL is [-1, 1])
- Depth: 0.0 = near, 1.0 = far

**Voodoo** coordinate system:
- Screen X: left to right (pixels)
- Screen Y: **top to bottom** (same as Vulkan!)
- Depth Z: 0 = near (highest priority), 0xFFFF = far

**Implication**: No Y flip needed in the vertex shader. The v1 perspective
correction research (written for OpenGL) included `ndc.y = -ndc.y`. For Vulkan,
this flip is NOT needed. The NDC conversion is simply:
```glsl
float ndc_x = (2.0 * in_position.x / pc.fb_width) - 1.0;
float ndc_y = (2.0 * in_position.y / pc.fb_height) - 1.0;
```

And depth maps from Voodoo [0, 0xFFFF] to Vulkan [0, 1]:
```glsl
gl_Position.z = in_depth;  // Already normalized to [0, 1] by CPU extraction
```

### 5.5 Early Fragment Tests

When `gl_FragDepth` is NOT written by the fragment shader, Vulkan implementations
can perform early-Z rejection (before the fragment shader runs). This is a significant
performance win for depth-heavy scenes.

When the uber-shader writes `gl_FragDepth` (needed for W-buffer mode, depth-source-
zaColor, or depth-bias), early-Z is disabled. We should use
`layout(early_fragment_tests) in;` ONLY when we can guarantee no gl_FragDepth write.

**Phase 2**: No depth test, no depth write. We CAN use early fragment tests, but
there's no benefit since depth is disabled. Simply omit the declaration for now.

---

## 6. Phase 2 Shader Recommendations

### 6.1 Design Principle

Declare the FULL interface (push constants, vertex inputs, varyings) from day one.
Stub out pipeline stages that are not yet implemented. This means:
- Push constant block is the full 64-byte struct (all 16 fields)
- Vertex inputs match the full `vc_vertex_t` layout (position, color, texcoord, fog)
- Fragment inputs include all varyings (color, texcoord0, texcoord1, depth, fog)
- Fragment shader body outputs `v_color` and ignores everything else

This way, the vertex buffer format and push constant layout NEVER change between
phases. Only the fragment shader body grows as features are added.

### 6.2 Vertex Shader

```glsl
#version 450

/* ---- Push Constants (64 bytes, full layout) ---- */
layout(push_constant, std430) uniform PushConstants {
    uint  fbzMode;           /* offset  0 */
    uint  fbzColorPath;      /* offset  4 */
    uint  alphaMode;         /* offset  8 */
    uint  fogMode;           /* offset 12 */
    uint  textureMode0;      /* offset 16 */
    uint  textureMode1;      /* offset 20 */
    uint  color0;            /* offset 24 */
    uint  color1;            /* offset 28 */
    uint  chromaKey;         /* offset 32 */
    uint  fogColor;          /* offset 36 */
    uint  zaColor;           /* offset 40 */
    uint  stipple;           /* offset 44 */
    uint  detail0;           /* offset 48 */
    uint  detail1;           /* offset 52 */
    float fb_width;          /* offset 56 */
    float fb_height;         /* offset 60 */
} pc;

/* ---- Vertex Inputs (matches vc_vertex_t) ---- */
layout(location = 0) in vec2  in_position;   /* screen-space X, Y (pixels)   */
layout(location = 1) in float in_depth;      /* Z depth (normalized [0,1])   */
layout(location = 2) in float in_oow;        /* 1/W for perspective          */
layout(location = 3) in vec4  in_color;      /* RGBA (normalized [0,1])      */
layout(location = 4) in vec3  in_texcoord0;  /* TMU0: S/W, T/W, 1/W         */
layout(location = 5) in vec3  in_texcoord1;  /* TMU1: S/W, T/W, 1/W         */
layout(location = 6) in float in_fog;        /* fog coordinate               */

/* ---- Outputs to Fragment Shader ---- */
layout(location = 0) noperspective out vec4  v_color;      /* iterated RGBA  */
layout(location = 1)               out vec3  v_texcoord0;  /* smooth (persp) */
layout(location = 2)               out vec3  v_texcoord1;  /* smooth (persp) */
layout(location = 3) noperspective out float v_depth;      /* Voodoo Z       */
layout(location = 4) noperspective out float v_fog;        /* fog coord      */

void main() {
    /* W from 1/W. When oow=0 (affine mode), W=1.0 -> smooth = linear. */
    float W = (in_oow > 0.0) ? (1.0 / in_oow) : 1.0;

    /* Screen-space to Vulkan NDC.
     * Vulkan: X [-1,+1] left-to-right, Y [-1,+1] top-to-bottom, Z [0,1].
     * Voodoo: X,Y in pixels, Y=0 at top. Same Y sense as Vulkan. */
    float ndc_x = (2.0 * in_position.x / pc.fb_width)  - 1.0;
    float ndc_y = (2.0 * in_position.y / pc.fb_height) - 1.0;

    /* Encode W for perspective-correct varying interpolation.
     * gl_Position.w = W causes GPU to apply: f_interp = sum(b_i * f_i / W_i) / sum(b_i / W_i)
     * which exactly reproduces Voodoo's S/W, T/W, 1/W pipeline. */
    gl_Position = vec4(ndc_x * W, ndc_y * W, in_depth * W, W);

    /* Colors: noperspective (Voodoo Gouraud is always affine). */
    v_color = in_color;

    /* Texture coords: smooth (perspective-corrected by W). */
    v_texcoord0 = in_texcoord0;
    v_texcoord1 = in_texcoord1;

    /* Depth: noperspective (Voodoo Z is linearly interpolated). */
    v_depth = in_depth;

    /* Fog coordinate: noperspective. */
    v_fog = in_fog;
}
```

**Notes on vertex attribute mapping to vc_vertex_t**:

```
vc_vertex_t field    ->  Vertex attribute
x, y                 ->  location 0 (vec2)
z                    ->  location 1 (float)
w (= 1/W = oow)     ->  location 2 (float)
r, g, b, a           ->  location 3 (vec4)
s0, t0, w0           ->  location 4 (vec3)
s1, t1, w1           ->  location 5 (vec3)
fog                  ->  location 6 (float)
pad[3]               ->  (not consumed by shader)
```

The `VkVertexInputBindingDescription` stride = `sizeof(vc_vertex_t)` = 72 bytes.
The `VkVertexInputAttributeDescription` offsets match the struct field offsets:
- location 0: offset 0, format VK_FORMAT_R32G32_SFLOAT
- location 1: offset 8, format VK_FORMAT_R32_SFLOAT
- location 2: offset 12, format VK_FORMAT_R32_SFLOAT
- location 3: offset 16, format VK_FORMAT_R32G32B32A32_SFLOAT
- location 4: offset 32, format VK_FORMAT_R32G32B32_SFLOAT
- location 5: offset 44, format VK_FORMAT_R32G32B32_SFLOAT
- location 6: offset 56, format VK_FORMAT_R32_SFLOAT

### 6.3 Fragment Shader

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

/* ---- Descriptor Set 0: Texture Samplers (declared but not used in Phase 2) ---- */
layout(set = 0, binding = 0) uniform sampler2D tex0;       /* TMU0 (RGBA8) */
layout(set = 0, binding = 1) uniform sampler2D tex1;       /* TMU1 (RGBA8) */
layout(set = 0, binding = 2) uniform sampler2D fog_table;  /* 64x1 RG8     */

/* ---- Inputs from Vertex Shader ---- */
layout(location = 0) noperspective in vec4  v_color;
layout(location = 1)               in vec3  v_texcoord0;
layout(location = 2)               in vec3  v_texcoord1;
layout(location = 3) noperspective in float v_depth;
layout(location = 4) noperspective in float v_fog;

/* ---- Output ---- */
layout(location = 0) out vec4 fragColor;

/* ---- Helper: unpack 0xAARRGGBB -> vec4(R,G,B,A) normalized ---- */
vec4 unpackColor(uint c) {
    return vec4(
        float((c >> 16) & 0xFFu) / 255.0,
        float((c >>  8) & 0xFFu) / 255.0,
        float( c        & 0xFFu) / 255.0,
        float((c >> 24) & 0xFFu) / 255.0
    );
}

/* ---- Helper: unpack 0x00RRGGBB -> vec3(R,G,B) normalized ---- */
vec3 unpackRGB(uint c) {
    return vec3(
        float((c >> 16) & 0xFFu) / 255.0,
        float((c >>  8) & 0xFFu) / 255.0,
        float( c        & 0xFFu) / 255.0
    );
}

void main() {
    /* ============================================================
     * Phase 2: Flat-shaded color output only.
     *
     * The full uber-shader pipeline will be built up incrementally:
     *   Phase 2: iterated color only (this)
     *   Phase 4: + texture fetch, texture combine
     *   Phase 5: + color combine, alpha combine, alpha test, chroma key
     *   Phase 6: + fog, dither, stipple, depth source/bias
     *   Phase 7: + LFB access, ACOLORBEFOREFOG shader blend
     *
     * The push constant block and sampler bindings are declared in full
     * so that the pipeline layout never changes between phases.
     * ============================================================ */

    /* --- STAGE 1: Stipple test (Phase 6) --- */
    /* TODO: implement stipple discard */

    /* --- STAGE 2-3: Depth (handled by Vulkan fixed-function, Phase 5) --- */

    /* --- STAGE 4: Destination read (not needed without shader-blend) --- */

    /* --- STAGE 5: Texture fetch (Phase 4) --- */
    /* TODO: TMU0/TMU1 fetch and combine */

    /* --- STAGE 6-7: Color/alpha local/other select --- */
    /* Phase 2: rgb_sel=0 (iterated), so cother = v_color */
    vec4 src = v_color;

    /* --- STAGE 8: Chroma key test (Phase 5) --- */
    /* TODO: discard on chroma match */

    /* --- STAGE 9-11: Alpha local/other select, alpha mask (Phase 5) --- */

    /* --- STAGE 12-13: Color/alpha combine (Phase 5) --- */
    /* With default fbzColorPath (all zero), combine is pass-through.
     * src = cother * ((0 ^ 0xFF) + 1) >> 8 = cother * 256 >> 8 = cother */

    /* --- STAGE 14: Save color-before-fog (Phase 7) --- */

    /* --- STAGE 15: Fog (Phase 6) --- */
    /* TODO: fog table lookup, fog math */

    /* --- STAGE 16: Alpha test (Phase 5) --- */
    /* TODO: discard on alpha fail */

    /* --- STAGE 17-18: Dither subtraction + alpha blend (Phase 5/6) --- */
    /* Alpha blend is Vulkan fixed-function for most cases */

    /* --- STAGE 19: Output dither (Phase 6) --- */
    /* TODO: Bayer dither quantization */

    /* --- Output --- */
    fragColor = src;
}
```

### 6.4 What to Bind for Sampler Descriptors in Phase 2

Phase 2 declares `sampler2D tex0/tex1/fog_table` in the shader but does not sample
them. The Vulkan spec says:

> Descriptors that are statically used by a pipeline must be bound before the draw/
> dispatch command is recorded.

Since Phase 2 never samples `tex0`/`tex1`/`fog_table`, they are NOT statically used
(the compiler should optimize them out). However, some implementations (validation
layers, older drivers) may still require them to be bound.

**Recommendation**: Create a 1x1 dummy VkImage (R8G8B8A8_UNORM, black pixel) at
init time and bind it to all three sampler bindings. This costs nothing and avoids
validation warnings. DuckStation and Dolphin both use this pattern.

**Alternative**: Do NOT declare the samplers in the Phase 2 shader at all. Only add
them in Phase 4 when textures are implemented. This avoids the dummy texture but
means the pipeline layout changes in Phase 4 (descriptor set layout changes).

**Recommendation**: Declare samplers in the shader and bind dummies. The pipeline
layout stays stable. This matches the "full layout from day one" principle.

### 6.5 Vertex Extraction (CPU Side)

For Phase 2, the CPU-side vertex extraction from `voodoo_params_t` is:

```c
/* Extract three vertices from voodoo_params_t gradients.
 *
 * Vertex A is the reference point. B and C are reconstructed from gradients.
 * See intercept-point.md for the gradient reconstruction math.
 *
 * For Phase 2, only position (x, y) and color (r, g, b, a) are needed.
 * Texture coords, fog, and oow are set to zero (unused by shader).
 */
static void
vc_extract_vertices(const voodoo_params_t *params,
                    const voodoo_t *voodoo,
                    vc_vertex_t out[3])
{
    float fb_w = (float)voodoo->h_disp;
    float fb_h = (float)voodoo->v_disp;

    /* Vertex positions (12.4 fixed-point -> float pixels) */
    float ax = (float)params->vertexAx / 16.0f;
    float ay = (float)params->vertexAy / 16.0f;
    float bx = (float)params->vertexBx / 16.0f;
    float by = (float)params->vertexBy / 16.0f;
    float cx = (float)params->vertexCx / 16.0f;
    float cy = (float)params->vertexCy / 16.0f;

    /* Colors at vertex A (12.12 fixed-point -> [0,1] float) */
    float r_a = (float)(int32_t)params->startR / (4096.0f * 255.0f);
    float g_a = (float)(int32_t)params->startG / (4096.0f * 255.0f);
    float b_a = (float)(int32_t)params->startB / (4096.0f * 255.0f);
    float a_a = (float)(int32_t)params->startA / (4096.0f * 255.0f);

    /* Color gradients (12.12 per pixel) */
    float drdx = (float)(int32_t)params->dRdX / (4096.0f * 255.0f);
    float drdy = (float)(int32_t)params->dRdY / (4096.0f * 255.0f);
    float dgdx = (float)(int32_t)params->dGdX / (4096.0f * 255.0f);
    float dgdy = (float)(int32_t)params->dGdY / (4096.0f * 255.0f);
    float dbdx = (float)(int32_t)params->dBdX / (4096.0f * 255.0f);
    float dbdy = (float)(int32_t)params->dBdY / (4096.0f * 255.0f);
    float dadx = (float)(int32_t)params->dAdX / (4096.0f * 255.0f);
    float dady = (float)(int32_t)params->dAdY / (4096.0f * 255.0f);

    /* Reconstruct vertex B and C colors from gradients:
     * V_B = V_A + dVdX * (Bx - Ax) + dVdY * (By - Ay) */
    float dx_b = bx - ax, dy_b = by - ay;
    float dx_c = cx - ax, dy_c = cy - ay;

    /* Vertex A */
    out[0].x = ax;  out[0].y = ay;  out[0].z = 0.0f;  out[0].w = 0.0f;
    out[0].r = r_a; out[0].g = g_a; out[0].b = b_a; out[0].a = a_a;
    memset(&out[0].s0, 0, 4 * sizeof(float));  /* s0,t0,w0,s1,t1,w1,fog = 0 */
    memset(&out[0].pad, 0, sizeof(out[0].pad));

    /* Vertex B */
    out[1].x = bx;  out[1].y = by;  out[1].z = 0.0f;  out[1].w = 0.0f;
    out[1].r = r_a + drdx * dx_b + drdy * dy_b;
    out[1].g = g_a + dgdx * dx_b + dgdy * dy_b;
    out[1].b = b_a + dbdx * dx_b + dbdy * dy_b;
    out[1].a = a_a + dadx * dx_b + dady * dy_b;
    memset(&out[1].s0, 0, 4 * sizeof(float));
    memset(&out[1].pad, 0, sizeof(out[1].pad));

    /* Vertex C */
    out[2].x = cx;  out[2].y = cy;  out[2].z = 0.0f;  out[2].w = 0.0f;
    out[2].r = r_a + drdx * dx_c + drdy * dy_c;
    out[2].g = g_a + dgdx * dx_c + dgdy * dy_c;
    out[2].b = b_a + dbdx * dx_c + dbdy * dy_c;
    out[2].a = a_a + dadx * dx_c + dady * dy_c;
    memset(&out[2].s0, 0, 4 * sizeof(float));
    memset(&out[2].pad, 0, sizeof(out[2].pad));

    /* Clamp colors to [0, 1] */
    for (int i = 0; i < 3; i++) {
        out[i].r = fmaxf(0.0f, fminf(1.0f, out[i].r));
        out[i].g = fmaxf(0.0f, fminf(1.0f, out[i].g));
        out[i].b = fmaxf(0.0f, fminf(1.0f, out[i].b));
        out[i].a = fmaxf(0.0f, fminf(1.0f, out[i].a));
    }
}
```

### 6.6 Push Constant Upload (CPU Side)

For Phase 2, the push constant block should be filled with the actual register values
even though the shader ignores most of them. This ensures correct batch break detection
from day one.

```c
static void
vc_fill_push_constants(vc_push_constants_t *pc,
                       const voodoo_params_t *params,
                       float fb_w, float fb_h)
{
    /* Raw registers (pass-through) */
    pc->fbzMode      = params->fbzMode;
    pc->fbzColorPath = params->fbzColorPath;
    pc->alphaMode    = params->alphaMode;
    pc->fogMode      = params->fogMode;
    pc->textureMode0 = params->textureMode[0];
    pc->textureMode1 = params->textureMode[1];

    /* Packed color values */
    pc->color0    = params->color0;
    pc->color1    = params->color1;
    pc->chromaKey = params->chromaKey;

    /* fogColor: rgbvoodoo_t {b, g, r, pad} -> 0x00RRGGBB */
    pc->fogColor = ((uint32_t)params->fogColor.r << 16)
                 | ((uint32_t)params->fogColor.g <<  8)
                 | ((uint32_t)params->fogColor.b);

    pc->zaColor = params->zaColor;
    pc->stipple = params->stipple;

    /* Detail params (zero in Phase 2, no textures) */
    pc->detail0 = 0;
    pc->detail1 = 0;

    /* Framebuffer dimensions */
    pc->fb_width  = fb_w;
    pc->fb_height = fb_h;
}
```

### 6.7 Pipeline Configuration for Phase 2

```
Blend:          DISABLED (blendEnable = VK_FALSE)
Depth test:     DISABLED (depthTestEnable = VK_FALSE)
Depth write:    DISABLED (depthWriteEnable = VK_FALSE)
Color write:    R | G | B | A
Cull mode:      NONE (Voodoo draws all triangles, no backface culling)
Front face:     COUNTER_CLOCKWISE (arbitrary, cull is none)
Polygon mode:   FILL
Topology:       TRIANGLE_LIST
Scissor:        Dynamic (full framebuffer for Phase 2)
Viewport:       Dynamic (full framebuffer)
```

### 6.8 SPIR-V Compilation

Compile GLSL to SPIR-V at build time using `glslc`:

```bash
glslc -fshader-stage=vertex -o voodoo_uber_vert.spv shaders/voodoo_uber.vert
glslc -fshader-stage=fragment -o voodoo_uber_frag.spv shaders/voodoo_uber.frag
```

Embed as C arrays using `xxd -i`:

```bash
xxd -i voodoo_uber_vert.spv > voodoo_uber_vert_spv.h
xxd -i voodoo_uber_frag.spv > voodoo_uber_frag_spv.h
```

Or use CMake `add_custom_command` to automate this at build time. The SPIR-V
bytecode is typically 1-4 KB per shader.

---

## 7. Gaps and Watch Items

### 7.1 Vertex Format Field Order vs Attribute Locations

The `vc_vertex_t` struct in DESIGN.md puts fields in this order:
`x, y, z, w, r, g, b, a, s0, t0, w0, s1, t1, w1, fog, pad[3]`

This maps to vertex attribute offsets:
- position (x,y) at offset 0 -- but z and w are at offset 8 and 12
- color (r,g,b,a) at offset 16
- texcoord0 (s0,t0,w0) at offset 32
- texcoord1 (s1,t1,w1) at offset 44
- fog at offset 56

The shader declares `in_position` as `vec2` (x,y only) and `in_depth` as a
separate `float` (z) and `in_oow` as another separate `float` (w). This is valid
because Vulkan vertex input attributes can overlap struct fields at arbitrary offsets
with arbitrary formats.

**Watch item**: Ensure the `VkVertexInputAttributeDescription` array exactly matches
these offsets. A mismatch will cause garbled vertex data.

### 7.2 Color Normalization

The SW rasterizer uses integer colors 0-255 with `CLAMP` macro (clamp to [0,255]).
The gradients (`startR`, `dRdX`, etc.) are 12.12 fixed-point where the integer part
is the 8-bit color value shifted left by 12.

**CPU extraction**: `float r = (float)(int32_t)params->startR / (4096.0f * 255.0f)`
gives a [0,1] normalized value suitable for the shader's `vec4 v_color`.

**Watch item**: The cast to `(int32_t)` is important because `startR` is `uint32_t`
but represents a signed 12.12 value. Without the signed cast, negative gradients
(colors decreasing across the triangle) will produce wrong results.

### 7.3 Triangle Winding Order

The Voodoo draws triangles with vertices A, B, C in the order specified by the game.
There is no backface culling in the Voodoo hardware (the game handles it). However,
Vulkan pipelines have a cull mode and front face setting.

**Requirement**: Set `VK_CULL_MODE_NONE` in the pipeline. If any triangles appear to
be missing, this is the first thing to check.

Also: The Voodoo triangle setup code determines left/right edges based on the cross
product of edge vectors. The GPU thread receives vertices in ABC order and draws them
as-is. Vulkan will rasterize them correctly with CULL_MODE_NONE regardless of winding.

### 7.4 Subpixel Precision

The Voodoo uses 12.4 fixed-point for vertex positions (4 fractional bits = 1/16
pixel precision). Our float conversion `ax = (float)params->vertexAx / 16.0f`
preserves this precision exactly (IEEE 754 float has 23-bit mantissa, more than
enough for 12.4 range).

However, the Voodoo's triangle setup has specific rounding and snapping behavior
(e.g., it snaps to half-pixel boundaries for even/odd lines in SLI mode). We should
verify that the GPU's rasterization of our float vertices produces pixels in the
same positions as the SW rasterizer's fixed-point iteration.

**Watch item**: For Phase 2, minor subpixel differences are acceptable. Exact match
will be verified in Phase 8 (pixel comparison testing).

### 7.5 Depth Format

DESIGN.md specifies `VK_FORMAT_D32_SFLOAT` for the offscreen depth image. The Voodoo
uses 16-bit integer depth (0-65535). When we implement depth testing (Phase 5), we
will need to convert: `gl_FragDepth = float(z_16bit) / 65535.0`.

Using D32_SFLOAT (instead of D16_UNORM) gives us more precision for the depth
comparison, which helps avoid Z-fighting artifacts that could occur if the Voodoo's
16-bit Z values map to the same D16 values but with different ordering due to
rounding.

**Phase 2 impact**: None -- depth is disabled.

### 7.6 Dual-Source Blend Output

The v1 research on dual-source blending (see `MEMORY.md`) established that
ACOLORBEFOREFOG requires `layout(location = 0, index = 1)` for the SRC1 output.
The shader MUST always write SRC1 when dual-source is used, even when the blend
state doesn't reference it.

**Phase 2 impact**: None -- no blending. But when the second output is added in
Phase 7, it must be present in ALL pipeline variants, not just the ACOLORBEFOREFOG
one. The shader should always write `fragColor1 = vec4(0)` as a default.

### 7.7 Descriptor Set for Phase 2 Without Textures

If the Phase 2 shader declares `sampler2D tex0/tex1/fog_table` but never samples
them, SPIR-V optimization will likely strip the descriptor references. However,
the `VkDescriptorSetLayout` must still match the pipeline layout.

**Options**:
1. Bind 1x1 dummy textures to all three bindings (safe, recommended).
2. Use a separate pipeline layout for Phase 2 with no descriptors, and change it
   in Phase 4 (simpler now, but breaks the "never change layout" principle).
3. Leave samplers in the shader but rely on SPIR-V dead-code elimination to make
   the descriptors unnecessary.

**Recommendation**: Option 1 (dummy textures). Create once at init, never change.

### 7.8 Push Constant Stage Flags Mismatch

The Vulkan spec requires that the push constant range's `stageFlags` in the pipeline
layout must match the stages that access the push constants. If the vertex shader
declares the push constant block, it must be included in `stageFlags`. If only the
fragment shader accesses push constants, only `VK_SHADER_STAGE_FRAGMENT_BIT` is needed.

Our vertex shader reads `fb_width` and `fb_height` from push constants, so both
`VERTEX_BIT` and `FRAGMENT_BIT` must be set. This is already specified in the v1
push constant layout doc and is correct.

### 7.9 glslc Version Requirements

The shaders use `#version 450` which targets Vulkan 1.0+ / SPIR-V 1.0. This is
compatible with all our targets (MoltenVK, Mesa V3DV, NVIDIA, AMD, Intel).

**Do NOT use** `#version 460` as it targets SPIR-V 1.3 which may not be supported
by all drivers. `#version 450` with `GL_KHR_vulkan_glsl` (automatically enabled by
glslc) gives us everything we need.

---

## Summary of Recommendations

| Item | Recommendation |
|------|---------------|
| Push constant block | Full 64-byte layout from day one |
| Vertex format | Full vc_vertex_t (72 bytes) from day one |
| Vertex shader | Full input/output declarations, NDC conversion with W encoding |
| Fragment shader | Full declarations, body outputs v_color only |
| Sampler descriptors | Declare in shader, bind 1x1 dummies |
| Pipeline state | No blend, no depth, cull=none, fill, triangle list |
| Color space | Float [0,1] in shader, extracted from 12.12 fixed-point |
| Y coordinate | No flip needed (Vulkan Y matches Voodoo Y) |
| SPIR-V target | `#version 450`, compile with glslc |
| Interpolation | `noperspective` for color, depth, fog; `smooth` for texcoords |

---

## Sources

### v1 Research (86Box repo)
- `videocommon-plan/archive/v1/research/uniform-mapping.md` -- register-to-uniform mapping
- `videocommon-plan/archive/v1/research/push-constant-layout.md` -- 64-byte push constant layout
- `videocommon-plan/archive/v1/research/perspective-correction.md` -- perspective math

### v2 Design (86Box repo)
- `videocommon-plan/DESIGN.md` -- sections 7.5, 7.8
- `videocommon-plan/PHASES.md` -- Phase 2 task list

### 86Box Source (software rasterizer reference)
- `src/video/vid_voodoo_render.c` -- `voodoo_half_triangle()`, per-pixel pipeline
- `src/include/86box/vid_voodoo_render.h` -- DEPTH_TEST, APPLY_FOG, ALPHA_TEST, ALPHA_BLEND macros
- `src/include/86box/vid_voodoo_common.h` -- voodoo_params_t, vert_t, voodoo_state_t

### Emulator References
- [Dolphin Ubershaders Blog](https://dolphin-emu.org/blog/2017/07/30/ubershaders/)
- [Dolphin Uber-Shader GLSL Gist](https://gist.github.com/phire/25181a9bfd957ac68ea8c74afdd9e9e1)
- [DuckStation gpu_hw_shadergen.cpp](https://github.com/stenzek/duckstation/blob/master/src/core/gpu_hw_shadergen.cpp)

### Vulkan Specifications
- [Push Constants Guide](https://docs.vulkan.org/guide/latest/push_constants.html)
- [Shader Memory Layout](https://docs.vulkan.org/guide/latest/shader_memory_layout.html)
- [SPIR-V Mappings](https://docs.vulkan.org/glsl/latest/chapters/spirvmappings.html)
- [GL_KHR_vulkan_glsl Extension](https://github.com/KhronosGroup/GLSL/blob/main/extensions/khr/GL_KHR_vulkan_glsl.txt)
