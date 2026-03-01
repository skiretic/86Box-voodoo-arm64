# Voodoo Register-to-Shader Uniform Mapping

Complete mapping from Voodoo hardware register bits to shader uniforms and
pipeline state. Derived by tracing every register bit tested in the
software rasterizer (`vid_voodoo_render.c:voodoo_half_triangle()`) and the
rendering macros (`vid_voodoo_render.h`).

Source files referenced:
- `src/include/86box/vid_voodoo_regs.h` — register bit definitions and macros
- `src/include/86box/vid_voodoo_common.h` — `voodoo_params_t` structure
- `src/video/vid_voodoo_render.c` — software pixel pipeline
- `src/include/86box/vid_voodoo_render.h` — DEPTH_TEST, APPLY_FOG, ALPHA_TEST, ALPHA_BLEND macros

---

## 1. fbzMode (per-triangle state)

| Bit(s) | Macro/Name | What it controls | Vulkan mapping | Uniform / Pipeline state | Notes |
|--------|-----------|------------------|----------------|--------------------------|-------|
| 0 | (clipping enable) | Enable clip rect test | **Dynamic state**: `vkCmdSetScissor()` | Pipeline state | |
| 1 | `FBZ_CHROMAKEY` | Enable chroma key test | Shader uniform | `u_chroma_key_enable` (bool packed into int) | Shader does `discard` on match |
| 2 | `FBZ_STIPPLE` | Enable stipple test | Shader uniform | `u_stipple_enable` (bool packed into int) | |
| 3 | `FBZ_W_BUFFER` | Use W-buffer instead of Z-buffer for depth source | Shader uniform | `u_w_buffer` (bool packed into int) | Affects depth value written to depth buffer |
| 4 | `FBZ_DEPTH_ENABLE` | Enable depth test | **Dynamic state** (VK_EXT_extended_dynamic_state): `vkCmdSetDepthTestEnable()` | Pipeline state | |
| 7:5 | `depth_op` | Depth comparison function (NEVER..ALWAYS) | **Dynamic state** (VK_EXT_extended_dynamic_state): `vkCmdSetDepthCompareOp()` | Pipeline state | Voodoo values 0-7 map to `VkCompareOp` 0-7: NEVER=0, LESS=1, EQUAL=2, LESS_OR_EQUAL=3, GREATER=4, NOT_EQUAL=5, GREATER_OR_EQUAL=6, ALWAYS=7 |
| 8 | `FBZ_DITHER` | Enable dithering | Shader uniform | `u_dither_enable` (bool packed into int) | 4x4 or 2x2 Bayer matrix in shader |
| 9 | `FBZ_RGB_WMASK` | Enable color write mask | **Baked into VkPipeline**: `VkPipelineColorBlendAttachmentState.colorWriteMask` | Pipeline state | |
| 10 | `FBZ_DEPTH_WMASK` | Enable depth write mask | **Dynamic state** (VK_EXT_extended_dynamic_state): `vkCmdSetDepthWriteEnable()` | Pipeline state | Combined with bit 4 and bit 18 to determine what aux_mem stores |
| 11 | `FBZ_DITHER_2x2` | Use 2x2 dither instead of 4x4 | Shader uniform | `u_dither_2x2` (bool packed into int) | |
| 12 | `FBZ_STIPPLE_PATT` | Stipple mode: 0=rotating, 1=pattern (Y/X indexed) | Shader uniform | `u_stipple_mode` (bool packed into int) | |
| 13 | `FBZ_ALPHA_MASK` | Enable alpha mask test (low bit of aother) | Shader uniform | `u_alpha_mask_enable` (bool packed into int) | `discard` if !(aother & 1) |
| 15:14 | `FBZ_DRAW_MASK` | Which buffer to draw to (front/back) | Not a uniform | N/A | Selects VkFramebuffer / VkImage target, handled at command level |
| 16 | `FBZ_DEPTH_BIAS` | Enable depth bias (add zaColor to depth) | Shader uniform | `u_depth_bias_enable` (bool packed into int) | Voodoo bias is additive 16-bit, not slope-based like `vkCmdSetDepthBias()` — must implement in shader |
| 17 | (Y origin) | Invert Y origin (bottom-up vs top-down) | Not a uniform | N/A | Handled in vertex coordinate transform on CPU side |
| 18 | `FBZ_ALPHA_ENABLE` | Enable alpha buffer (aux stores alpha instead of depth) | Shader uniform / Fixed-function | `u_alpha_buffer_enable` (bool) | Changes aux buffer semantics; affects what depth_wmask writes |
| 19 | `FBZ_DITHER_SUB` | Enable dither subtraction on destination reads | Shader uniform | `u_dithersub` (bool packed into int) | Applied before alpha blend |
| 20 | `FBZ_DEPTH_SOURCE` | Depth source: 0=iterated Z, 1=zaColor constant | Shader uniform | `u_depth_source_za` (bool packed into int) | |
| 26 | `FBZ_PARAM_ADJUST` | Sub-pixel correction for parameter adjustment | Not a uniform | N/A | Only affects triangle setup (vertex reconstruction), not per-pixel |

### fbzMode summary
- **Vulkan pipeline / dynamic state (6 bits)**: clip enable (0), depth enable (4), depth func (7:5), RGB wmask (9), depth wmask (10), draw buffer (15:14)
- **Shader uniforms (10 bits)**: chroma key enable (1), stipple enable (2), W-buffer (3), dither enable (8), dither 2x2 (11), stipple mode (12), alpha mask (13), depth bias enable (16), alpha enable (18), dithersub (19), depth source (20)
- **Not needed in shader (3 bits)**: Y origin (17), param adjust (26), draw buffer (15:14 — command-level)

---

## 2. fbzColorPath (per-triangle state)

| Bit(s) | Macro/Name | What it controls | Type | Proposed uniform name | Notes |
|--------|-----------|------------------|------|----------------------|-------|
| 1:0 | `_rgb_sel` | Color "other" source: 0=iterated RGB, 1=texture, 2=color1, 3=LFB | int | `u_cc_rgb_sel` | |
| 3:2 | `a_sel` | Alpha "other" source: 0=iterated A, 1=texture, 2=color1, 3=LFB | int | `u_cc_a_sel` | |
| 4 | `cc_localselect` | Color local source: 0=iterated RGB, 1=color0 | int | `u_cc_localselect` | Overridden if cc_localselect_override=1 |
| 6:5 | `cca_localselect` | Alpha local source: 0=iterated A, 1=color0.a, 2=iterated Z | int | `u_cca_localselect` | |
| 7 | `cc_localselect_override` | Override cc_localselect with texture alpha bit 7 | int | `u_cc_localselect_override` | |
| 8 | `cc_zero_other` | Zero the "other" color (before combine) | int | `u_cc_zero_other` | |
| 9 | `cc_sub_clocal` | Subtract clocal from src | int | `u_cc_sub_clocal` | |
| 12:10 | `cc_mselect` | Color blend factor select (0=zero, 1=clocal, 2=aother, 3=alocal, 4=tex_a, 5=tex_rgb) | int | `u_cc_mselect` | |
| 13 | `cc_reverse_blend` | Reverse blend factor (1-factor if clear) | int | `u_cc_reverse_blend` | |
| 15:14 | `cc_add` | Color add: 0=none, 1=clocal, 2=alocal | int | `u_cc_add` | |
| 16 | `cc_invert_output` | XOR output with 0xFF | int | `u_cc_invert_output` | |
| 17 | `cca_zero_other` | Zero alpha "other" | int | `u_cca_zero_other` | |
| 18 | `cca_sub_clocal` | Subtract alpha local | int | `u_cca_sub_clocal` | |
| 21:19 | `cca_mselect` | Alpha blend factor select (0=zero, 1=alocal, 2=aother, 3=alocal, 4=tex_a) | int | `u_cca_mselect` | |
| 22 | `cca_reverse_blend` | Reverse alpha blend | int | `u_cca_reverse_blend` | |
| 24:23 | `cca_add` | Alpha add (any nonzero adds alocal) | int | `u_cca_add` | |
| 25 | `cca_invert_output` | Invert alpha output | int | `u_cca_invert_output` | |
| 27 | `FBZCP_TEXTURE_ENABLED` | Enable texture sampling | int | `u_texture_enabled` | Gates all TMU work |

### Packing opportunity — fbzColorPath

Most of these are small fields. We can pack the entire fbzColorPath register into
**a single `uint` uniform** and extract bits in the shader with bitwise ops. This
reduces uniform count from 17 to 1, at the cost of a few ALU ops in the shader
(negligible compared to texture sampling).

**Proposed: `u_fbzColorPath` (uint) — pass raw register, decode in shader.**

---

## 3. alphaMode (per-triangle state)

| Bit(s) | Macro/Name | What it controls | Vulkan mapping | Proposed uniform | Notes |
|--------|-----------|------------------|----------------|-----------------|-------|
| 0 | (alpha test enable) | Enable alpha test | Shader | Part of `u_alphaMode` | `discard` in shader on fail |
| 3:1 | `alpha_func` | Alpha test function (NEVER..ALWAYS) | Shader | `u_alpha_func` or packed | Same enum as depth func |
| 4 | (alpha blend enable) | Enable alpha blending | **Baked into VkPipeline**: `VkPipelineColorBlendAttachmentState.blendEnable` + shader for exotic modes | Mixed | See discussion below |
| 11:8 | `src_afunc` | Source blend factor (16 values) | **Baked into VkPipeline** (mostly) | `VkBlendFactor` src factor | Maps to `VkBlendFactor` for values 0-7, except AFUNC_ASATURATE=0xF and AFUNC_A_COLOR/AOM_COLOR need shader |
| 15:12 | `dest_afunc` | Dest blend factor (16 values) | **Baked into VkPipeline** (mostly) | `VkBlendFactor` dst factor | Except AFUNC_ACOLORBEFOREFOG=0xF needs shader (uses pre-fog color) |
| 19:16 | `src_aafunc` | Source alpha-for-alpha-channel blend | Shader | `u_src_aafunc` | Only used when alpha_enable is on; simple (only 4=ONE tested) |
| 23:20 | `dest_aafunc` | Dest alpha-for-alpha-channel blend | Shader | `u_dest_aafunc` | Same — only 4=ONE tested in SW renderer |
| 31:24 | `a_ref` | Alpha test reference value | Shader | `u_alpha_ref` (float) | 8-bit reference, pass as float /255.0 or as int |

### Alpha blend factor mapping: Voodoo AFUNC to VkBlendFactor

| Voodoo AFUNC | Value | VkBlendFactor | Can bake into VkPipeline? |
|-------------|-------|---------------|--------------------------|
| AZERO | 0 | `VK_BLEND_FACTOR_ZERO` | YES |
| ASRC_ALPHA | 1 | `VK_BLEND_FACTOR_SRC_ALPHA` | YES |
| A_COLOR | 2 | `VK_BLEND_FACTOR_DST_COLOR` (for src) / `VK_BLEND_FACTOR_SRC_COLOR` (for dst) | YES |
| ADST_ALPHA | 3 | `VK_BLEND_FACTOR_DST_ALPHA` | YES |
| AONE | 4 | `VK_BLEND_FACTOR_ONE` | YES |
| AOMSRC_ALPHA | 5 | `VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA` | YES |
| AOM_COLOR | 6 | `VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR` (for src) / `VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR` (for dst) | YES |
| AOMDST_ALPHA | 7 | `VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA` | YES |
| ASATURATE | 15 | `VK_BLEND_FACTOR_SRC_ALPHA_SATURATE` (src only) | YES (src only) |
| ACOLORBEFOREFOG | 15 | N/A — uses pre-fog color as factor | NO — must do in shader |

**Key finding**: AFUNC_ACOLORBEFOREFOG (dest_afunc=0xF) uses the color *before* fog
application as the blend factor. This is impossible with Vulkan pipeline-baked blending.
When `dest_afunc == 0xF`, we must do alpha blending entirely in the shader using a
"copy-on-blend" approach: read destination via an input attachment or copy image,
blend in shader, write result. This is the only exotic blend mode.

For all other blend factor combinations (values 0-7 and src ASATURATE=15), standard
`VkBlendFactor` values baked into the `VkPipeline` work perfectly.

**Proposed: `u_alphaMode` (uint) — pass raw register, decode in shader.**

Alpha test happens in the shader unconditionally (branch on bit 0). Alpha blending
uses Vulkan pipeline-baked blend state for the common case, falling back to
shader-based blending only for ACOLORBEFOREFOG.

---

## 4. fogMode (per-triangle state)

| Bit(s) | Macro/Name | What it controls | Type | Proposed uniform | Notes |
|--------|-----------|------------------|------|-----------------|-------|
| 0 | `FOG_ENABLE` | Enable fog | int | Part of `u_fogMode` | |
| 1 | `FOG_ADD` | Fog additive mode (0=use fogColor, 1=use zero as fog base) | int | Part of `u_fogMode` | |
| 2 | `FOG_MULT` | Fog multiply mode (0=lerp fog-src, 1=just multiply) | int | Part of `u_fogMode` | |
| 3 | `FOG_ALPHA` | Fog alpha source: use iterated alpha | int | Part of `u_fogMode` | |
| 4 | `FOG_Z` | Fog alpha source: use iterated Z (overrides bit 3) | int | Part of `u_fogMode` | |
| 4:3=11 | `FOG_W` | Fog alpha source: use W | int | Part of `u_fogMode` | |
| 5 | `FOG_CONSTANT` | Constant fog (just add fogColor, skip table) | int | Part of `u_fogMode` | |

Fog alpha source selection (bits 4:3):
- `00`: fog table lookup (indexed by w_depth)
- `01` (`FOG_ALPHA`): iterated alpha (ia >> 12)
- `10` (`FOG_Z`): iterated Z (z >> 20)
- `11` (`FOG_W`): W value (w >> 32)

**Proposed: `u_fogMode` (uint) — pass raw register low byte, decode in shader.**

### Additional fog uniforms

| Source | Type | Proposed uniform | Notes |
|--------|------|-----------------|-------|
| `params->fogColor` (RGB) | vec3 or ivec3 | `u_fog_color` | Passed as 0-255 integers or 0.0-1.0 floats |
| `params->fogTable[64]` (fog+dfog entries) | sampler2D (64x1) or uniform array | `u_fog_table` | 64 entries, each with 8-bit fog and 8-bit dfog. Best as a 64x1 `VK_FORMAT_R8G8_UNORM` VkImage (`sampler1D` not available in Vulkan SPIR-V). |

---

## 5. textureMode[0] — TMU0 (per-triangle state)

| Bit(s) | Macro/Name | What it controls | Vulkan mapping | Notes |
|--------|-----------|------------------|----------------|-------|
| 0 | (perspective enable) | Enable perspective-correct texcoords (divide by W) | Shader | 0=affine, 1=perspective. Vulkan does perspective by default with varying/W, but Voodoo's affine mode needs shader awareness |
| 4:1 | (filter mode / format bits) | Texture filter: bit 1=minify mag, bit 2=bilinear | **VkSampler**: `VkSamplerCreateInfo.minFilter/magFilter` | `VK_FILTER_NEAREST` vs `VK_FILTER_LINEAR` |
| 5 | `TEXTUREMODE_NCC_SEL` | NCC table select (0 or 1) | Not a uniform | Handled during texture decode/upload |
| 6 | `TEXTUREMODE_TCLAMPS` | Clamp S coordinate | **VkSampler**: `VkSamplerCreateInfo.addressModeU` | `VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE` vs `VK_SAMPLER_ADDRESS_MODE_REPEAT` |
| 7 | `TEXTUREMODE_TCLAMPT` | Clamp T coordinate | **VkSampler**: `VkSamplerCreateInfo.addressModeV` | `VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE` vs `VK_SAMPLER_ADDRESS_MODE_REPEAT` |
| 11:8 | (texture format) | Texture format (RGB332, ARGB1555, RGB565, etc.) | Not a uniform | Handled at texture upload — all decoded to RGBA8 |
| 12 | `tc_zero_other` | Zero the "other" texture (TMU1 output) | Shader | Part of combine |
| 13 | `tc_sub_clocal` | Subtract local texture color | Shader | Part of combine |
| 16:14 | `tc_mselect` | Texture color blend factor (0=zero, 1=clocal, 2=aother, 3=alocal, 4=detail, 5=LOD frac) | Shader | Part of combine |
| 17 | `tc_reverse_blend` | Reverse texture blend factor | Shader | Part of combine |
| 18 | `tc_add_clocal` | Add local color after blend | Shader | Part of combine |
| 19 | `tc_add_alocal` | Add local alpha after blend | Shader | Part of combine |
| 20 | `tc_invert_output` | Invert output RGB | Shader | Part of combine |
| 21 | `tca_zero_other` | Zero "other" alpha | Shader | Part of combine |
| 22 | `tca_sub_clocal` | Subtract local alpha | Shader | Part of combine |
| 25:23 | `tca_mselect` | Alpha blend factor (same options as color) | Shader | Part of combine |
| 26 | `tca_reverse_blend` | Reverse alpha blend | Shader | Part of combine |
| 27 | `tca_add_clocal` | Add local alpha after blend | Shader | Part of combine |
| 28 | `tca_add_alocal` | Add local alpha after blend (alpha channel) | Shader | Part of combine |
| 29 | `tca_invert_output` | Invert output alpha | Shader | Part of combine |
| 30 | `TEXTUREMODE_TRILINEAR` | Trilinear blending (affects reverse_blend sense) | Shader | Part of combine |

### textureMode[0] summary
- **VkSampler state (4 bits)**: filter mode (4:1), clamp S (6), clamp T (7)
- **Shader combine bits (18 bits)**: bits 12-30 — all combine control
- **Not needed (5 bits)**: NCC select (5), format (11:8), perspective (0 — handled via vertex W)

**Proposed: `u_textureMode0` (uint) — pass raw register, decode combine bits in shader.**

The perspective enable bit (0) is interesting: when clear, the software renderer
does NOT divide S/T by W. In Vulkan, we always get perspective-correct interpolation
(unless the vertex shader output is decorated with `noperspective`).
To emulate affine texturing, we would need to either:
1. Pre-divide S/T by W on the CPU and multiply by W in the shader, or
2. Pass a flag and do `S * W_frag` in the shader.

For Phase 2, we can treat all texturing as perspective-correct (bit 0=1 is the
common case). Affine mode is rare and mostly used by non-3D applications.

---

## 6. textureMode[1] — TMU1 (per-triangle state)

Identical bit layout to textureMode[0], but for TMU1. Uses `tc_*_1` and
`tca_*_1` macros. Only relevant when dual-TMU is active (Voodoo 2 / V3).

| Source | Proposed uniform |
|--------|-----------------|
| `params->textureMode[1]` | `u_textureMode1` (uint) |

Same bit layout, same shader decode logic. Only active when `u_texture_enabled=1`
and `dual_tmus=1` and TMU0 is NOT in passthrough/local-only mode.

---

## 7. tLOD[0] and tLOD[1] (per-triangle state, via LOD computation)

| Bit(s) | Name | What it controls | Notes |
|--------|------|------------------|-------|
| 5:0 | LOD min | Minimum LOD (clamped) | Used in `voodoo_tmu_fetch` |
| 11:6 | LOD max | Maximum LOD (clamped) | Used in `voodoo_tmu_fetch` |
| 17:12 | LOD bias | Signed 6-bit LOD bias | Added to computed LOD |
| 18 | `LOD_ODD` | LOD odd (affects split/interleave) | Texture upload concern, not shader |
| 19 | `LOD_SPLIT` | LOD split | Texture upload concern |
| 20 | `LOD_S_IS_WIDER` | S is wider than T | Texture upload concern |
| 24 | `LOD_TMULTIBASEADDR` | Multiple base addresses per LOD | Texture upload concern |
| 28 | `LOD_TMIRROR_S` | Mirror S coordinate | **VkSampler**: `VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT` |
| 29 | `LOD_TMIRROR_T` | Mirror T coordinate | **VkSampler**: `VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT` |

LOD is computed per-triangle in `voodoo_triangle()` from the texture gradient
magnitudes (dSdX, dTdX, dSdY, dTdY). This computation happens on the CPU side
before submission. The LOD min/max/bias are then used to clamp the result.

For Vulkan: The GPU computes LOD automatically from texture coordinates. We set
`VkSamplerCreateInfo.minLod`, `VkSamplerCreateInfo.maxLod`, and
`VkSamplerCreateInfo.mipLodBias` when creating the `VkSampler`.

**Not a shader uniform** — entirely VkSampler state, configured at sampler creation.

Mirror modes: `VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT` for wrap mode when mirror bits are set.

---

## 8. Constant Color Registers (per-triangle state)

| Register | Fields used | Proposed uniform | Type | Notes |
|----------|-----------|-----------------|------|-------|
| `color0` | R (23:16), G (15:8), B (7:0), A (31:24) | `u_color0` | vec4 | Used as clocal source when cc_localselect=1, and cca_localselect=CCA_LOCALSELECT_COLOR0 |
| `color1` | R (23:16), G (15:8), B (7:0), A (31:24) | `u_color1` | vec4 | Used as cother source when rgb_sel=CC_LOCALSELECT_COLOR1 or a_sel=A_SEL_COLOR1 |
| `zaColor` | Low 16 bits: constant depth; High 16 bits: depth bias | `u_zaColor` | uint or vec2 | Depth source when FBZ_DEPTH_SOURCE set; depth bias when FBZ_DEPTH_BIAS set |
| `chromaKey_r/g/b` | Pre-extracted from chromaKey register | `u_chromaKey` | vec3 | Chroma key comparison values (0-255 or 0.0-1.0) |
| `fogColor` | R, G, B fields | `u_fog_color` | vec3 | Fog blend color |
| `stipple` | 32-bit stipple pattern | `u_stipple` | uint | Used for both rotating and patterned stipple modes |

---

## 9. Fog Table (per-triangle state, rarely changes)

The fog table has 64 entries, each containing:
- `fog` (8-bit): base fog value
- `dfog` (8-bit): delta fog value for interpolation within the entry

This is indexed by `w_depth >> 10` (6-bit index), with interpolation using
`(w_depth >> 2) & 0xFF` as the fractional part.

**Proposed**: Upload as a **64x1 VkImage** (`VK_FORMAT_R8G8_UNORM`), bound via
descriptor set as a `sampler2D`. The shader samples this image using the
w_depth-derived index. This avoids needing 64 push constant entries and allows
hardware filtering. Note: `sampler1D` is not available in Vulkan SPIR-V, so we
use a 64x1 2D image instead.

Alternative: Pack fog data into a UBO or SSBO (64 entries). This avoids an extra
descriptor binding but complicates the shader.

**Recommendation**: 64x1 `VK_FORMAT_R8G8_UNORM` VkImage. The fog table changes rarely
(only when the game writes to fogTable registers), and a 64x1 texel image is trivial.
Upload via staging buffer + `vkCmdCopyBufferToImage()`.

| Source | Vulkan resource | Name | Notes |
|--------|----------------|------|-------|
| `params->fogTable[64]` | 64x1 `VkImage` (`VK_FORMAT_R8G8_UNORM`) + `VkSampler` | `u_fog_table` / `tex_fog_table` | 64 entries of (fog, dfog), descriptor set binding 2 |

---

## 10. Texture Samplers

| TMU | Vulkan resource | Uniform name | Notes |
|-----|----------------|-------------|-------|
| TMU0 | `VkImage` (`VK_FORMAT_R8G8B8A8_UNORM`) + `VkImageView` + `VkSampler` | `u_tex0` | Descriptor set binding 0 |
| TMU1 | `VkImage` (`VK_FORMAT_R8G8B8A8_UNORM`) + `VkImageView` + `VkSampler` | `u_tex1` | Descriptor set binding 1; only when dual-TMU active |

These are `sampler2D` descriptors (combined image sampler), not push constants.
Pre-decoded RGBA8 textures uploaded via staging buffer + `vkCmdCopyBufferToImage()`.

---

## 11. Detail/LOD Fraction Parameters (per-triangle, TMU-specific)

Used in texture combine when `tc_mselect` or `tca_mselect` is DETAIL or LOD_FRAC.

| Parameter | Source | Proposed uniform | Type | Notes |
|-----------|--------|-----------------|------|-------|
| `detail_bias[0]` | `params->detail_bias[0]` | Part of `u_detail_params0` | ivec3 | (bias, max, scale) for TMU0 |
| `detail_max[0]` | `params->detail_max[0]` | Part of `u_detail_params0` | ivec3 | |
| `detail_scale[0]` | `params->detail_scale[0]` | Part of `u_detail_params0` | ivec3 | |
| `detail_bias[1]` | `params->detail_bias[1]` | Part of `u_detail_params1` | ivec3 | (bias, max, scale) for TMU1 |
| `detail_max[1]` | `params->detail_max[1]` | Part of `u_detail_params1` | ivec3 | |
| `detail_scale[1]` | `params->detail_scale[1]` | Part of `u_detail_params1` | ivec3 | |

These are only needed when detail texturing or LOD blending is in use (uncommon).
Can be uploaded lazily — only when textureMode mselect bits reference detail/LOD.

---

## 12. Auxiliary Per-Instance State (set once, not per-triangle)

These are set during initialization or on mode changes, not per-triangle:

| Parameter | Source | How to handle | Notes |
|-----------|--------|---------------|-------|
| `y_origin_swap` | `voodoo->y_origin_swap` | CPU-side vertex transform | Flip Y in vertex shader or on CPU |
| `dual_tmus` | `voodoo->dual_tmus` | Could be uniform or just compile-time branch | 1 for V2/V3, 0 for V1 |
| Framebuffer dimensions | From voodoo display config | Not a uniform | Used for `vkCmdSetViewport()`, `vkCmdSetScissor()` |
| `bilinear_enabled` | `voodoo->bilinear_enabled` | VkSampler state | `VK_FILTER_LINEAR` vs `VK_FILTER_NEAREST` |
| `dithersub_enabled` | `voodoo->dithersub_enabled` | Shader uniform or compile-time | Rare feature |

---

## Complete Uniform Table (Consolidated)

### Raw Register Uniforms (pass-through, decode in shader)

| # | Uniform name | Type | Source register | Changes per... |
|---|-------------|------|-----------------|---------------|
| 1 | `u_fbzColorPath` | `uint` | `params->fbzColorPath` | Triangle |
| 2 | `u_alphaMode` | `uint` | `params->alphaMode` | Triangle |
| 3 | `u_fogMode` | `uint` | `params->fogMode` | Triangle |
| 4 | `u_textureMode0` | `uint` | `params->textureMode[0]` | Triangle |
| 5 | `u_textureMode1` | `uint` | `params->textureMode[1]` | Triangle |
| 6 | `u_fbzMode` | `uint` | `params->fbzMode` | Triangle |

### Extracted Value Uniforms

| # | Uniform name | Type | Source | Changes per... |
|---|-------------|------|--------|---------------|
| 7 | `u_color0` | `vec4` | `params->color0` (ARGB -> normalized RGBA) | Triangle |
| 8 | `u_color1` | `vec4` | `params->color1` (ARGB -> normalized RGBA) | Triangle |
| 9 | `u_chromaKey` | `vec3` | `params->chromaKey_r/g/b` (normalized) | Triangle |
| 10 | `u_fog_color` | `vec3` | `params->fogColor.r/g/b` (normalized) | Triangle |
| 11 | `u_zaColor` | `uint` | `params->zaColor` | Triangle |
| 12 | `u_stipple` | `uint` | `params->stipple` | Triangle |
| 13 | `u_detail_params0` | `ivec3` | `(detail_bias[0], detail_max[0], detail_scale[0])` | Triangle |
| 14 | `u_detail_params1` | `ivec3` | `(detail_bias[1], detail_max[1], detail_scale[1])` | Triangle |

### Texture Samplers

| # | Uniform name | Type | Binding |
|---|-------------|------|---------|
| 15 | `u_tex0` | `sampler2D` | Descriptor set binding 0 (combined image sampler) |
| 16 | `u_tex1` | `sampler2D` | Descriptor set binding 1 (combined image sampler) |
| 17 | `u_fog_table` | `sampler2D` | Descriptor set binding 2 (64x1 `VK_FORMAT_R8G8_UNORM` image) |

### Vulkan Pipeline / Dynamic State (NOT push constants, set per batch)

| # | Vulkan state | Source bits | Notes |
|---|-------------|-----------|-------|
| F1 | **Dynamic**: `vkCmdSetScissor()` (enable/disable via full-framebuffer rect) | fbzMode bit 0 | Vulkan scissor is always enabled; disable by setting rect to full framebuffer |
| F2 | **Dynamic**: `vkCmdSetScissor(x, y, w, h)` | `params->clipLeft/Right/LowY/HighY` | |
| F3 | **Dynamic** (VK_EXT_extended_dynamic_state): `vkCmdSetDepthTestEnable()` | fbzMode bit 4 | |
| F4 | **Dynamic** (VK_EXT_extended_dynamic_state): `vkCmdSetDepthCompareOp()` | fbzMode bits 7:5 | Voodoo values map directly to `VkCompareOp` 0-7 |
| F5 | **Dynamic** (VK_EXT_extended_dynamic_state): `vkCmdSetDepthWriteEnable()` | fbzMode bit 10 (+ conditions on bit 4, 18) | |
| F6 | **Baked into VkPipeline**: `VkPipelineColorBlendAttachmentState.colorWriteMask` | fbzMode bit 9 | Requires pipeline variant |
| F7 | **Baked into VkPipeline**: `VkPipelineColorBlendAttachmentState.blendEnable` | alphaMode bit 4 | Requires pipeline variant |
| F8 | **Baked into VkPipeline**: `VkPipelineColorBlendAttachmentState` src/dstColorBlendFactor | alphaMode bits 11:8, 15:12 | Except ACOLORBEFOREFOG; requires pipeline variant per blend combo |
| F9 | **VkSampler**: `addressModeU` | textureMode bit 6, tLOD bit 28 | `VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE`, `_REPEAT`, or `_MIRRORED_REPEAT` |
| F10 | **VkSampler**: `addressModeV` | textureMode bit 7, tLOD bit 29 | Same |
| F11 | **VkSampler**: `minFilter` | textureMode bits 4:1, bilinear_enabled | `VK_FILTER_NEAREST` or `VK_FILTER_LINEAR` |
| F12 | **VkSampler**: `magFilter` | textureMode bits 4:1, bilinear_enabled | `VK_FILTER_NEAREST` or `VK_FILTER_LINEAR` |
| F13 | **VkSampler**: `mipLodBias` | tLOD bits 17:12 | Signed 6-bit bias |
| F14 | **VkSampler**: `minLod` | tLOD bits 5:0 | |
| F15 | **VkSampler**: `maxLod` | tLOD bits 11:6 | |

---

## Uniform Count Summary

| Category | Count |
|----------|-------|
| Raw register pass-through (uint) | 6 |
| Extracted value uniforms | 8 |
| Texture samplers (descriptor set) | 3 |
| **Total push constants + descriptors** | **17** |
| Vulkan pipeline / dynamic state settings | 15 |

**The design doc estimate of "~30 uniforms" was conservative.** By passing raw
registers as uint push constants and decoding bits in the shader, we achieve **14
non-sampler values + 3 sampler descriptors = 17 total**. The 14 scalar/vector values
fit within Vulkan's guaranteed minimum push constant size of 128 bytes (we use 64
bytes per `push-constant-layout.md`). The 3 samplers are bound via a single
descriptor set with combined image sampler bindings.

The alternative "expanded" approach (one push constant per decoded field) would require
approximately 40-45 individual values, which would exceed the 128-byte push constant
limit and force us to use a UBO instead.

**Recommendation**: Use the raw-register approach (14 push constants + 3 descriptors).
Bitfield extraction in GLSL/SPIR-V is free (1 ALU op each) and greatly simplifies
the CPU-side push constant upload — just copy 6 uint32_t registers directly via
`vkCmdPushConstants()`.

---

## Batch Break Analysis: Per-Triangle vs Per-Frame State

A "batch" is a group of triangles rendered in a single `vkCmdDraw()` call.
A batch break occurs when pipeline state changes between consecutive triangles.

### State that forces a batch break (any change = new batch)

| State | Why it breaks batching |
|-------|----------------------|
| `fbzMode` | Changes depth func, masks, stipple, etc. |
| `fbzColorPath` | Changes combine mode |
| `alphaMode` | Changes alpha test/blend |
| `fogMode` | Changes fog computation |
| `textureMode[0]` | Changes texture combine for TMU0 |
| `textureMode[1]` | Changes texture combine for TMU1 |
| `color0` | Changes combine local color |
| `color1` | Changes combine other color |
| `zaColor` | Changes depth bias/source |
| `chromaKey` | Changes chroma key value |
| `fogColor` | Changes fog blend color |
| `stipple` | Changes stipple pattern |
| `clipLeft/Right/LowY/HighY` | Changes scissor rect |
| `tex_entry[0]` | Changes which texture is bound on TMU0 |
| `tex_entry[1]` | Changes which texture is bound on TMU1 |
| `draw_offset` / buffer target | Changes which VkFramebuffer / VkImage is the render target |
| `fogTable` | Changes fog table texture (rare) |

### State that does NOT break batching (per-vertex, interpolated by GPU)

| State | Why it's per-vertex |
|-------|-------------------|
| Vertex position (Ax, Ay, Bx, By, Cx, Cy) | Naturally per-vertex |
| Iterated R, G, B, A | Per-vertex attribute, GPU interpolates |
| Iterated Z | Per-vertex attribute (or gl_FragCoord.z) |
| Iterated S, T, W (per TMU) | Per-vertex attribute, GPU interpolates |
| Iterated W (base) | Per-vertex attribute |

### Practical batching expectation

In typical Voodoo usage:
- **GLQuake**: Long runs of same state (lightmapped passes), batch sizes 50-200+ triangles
- **Multi-pass rendering**: State changes between passes, but each pass has many triangles
- **Worst case**: UI/text rendering with per-quad state changes, batch size 1-2

A single `uint32_t` comparison of each register (6 comparisons) determines if a
batch break is needed. This is the "state signature" approach: hash or compare the
6 key registers + texture binding + clip rect.

---

## Depth Buffer Considerations

The Voodoo aux buffer serves double duty:
1. **Normal mode** (FBZ_ALPHA_ENABLE=0): aux stores 16-bit depth
2. **Alpha mode** (FBZ_ALPHA_ENABLE=1): aux stores 8-bit alpha (low byte of 16-bit word)

In Vulkan, we use `VK_FORMAT_D16_UNORM` for the depth attachment. When alpha-buffer
mode is active, we need a separate storage mechanism:
- Option A: Use a separate `VK_FORMAT_R8_UNORM` VkImage as the alpha buffer (not the depth attachment)
- Option B: Write alpha to a specific channel of the color buffer

The depth value written can be either:
- Iterated Z (clamped 16-bit from `state->z >> 12`)
- W-depth (computed from 1/W via the `voodoo_fls` log table)
- Constant depth (`zaColor & 0xFFFF`)
- Biased depth (+ `(int16_t)zaColor`)

For W-buffering: The Voodoo W-to-depth conversion uses a custom logarithmic mapping.
The shader must replicate this conversion to write the correct depth value to
`gl_FragDepth` (SPIR-V `FragDepth` built-in). This means `gl_FragDepth` must be
written explicitly in the shader when W-buffering or depth-source-zaColor is active.
Note: writing `gl_FragDepth` disables early-Z on most Vulkan implementations.

---

## Dither Implementation Notes

Dithering in the Voodoo quantizes 8-bit color channels to RGB565 using a
position-dependent threshold matrix:

- **4x4 Bayer**: indexed by `(real_y & 3, x & 3)` — 16 threshold entries
- **2x2 Bayer**: indexed by `(real_y & 1, x & 1)` — 4 threshold entries

The dither lookup tables (`dither_rb[256][4][4]` and `dither_g[256][4][4]`) map
each 8-bit input value to a 5-bit (R/B) or 6-bit (G) output based on position.

In the shader, dithering can be implemented as:
```glsl
// 4x4 Bayer dither
ivec2 dpos = ivec2(gl_FragCoord.xy) & 3;  // or & 1 for 2x2
float threshold = bayerMatrix[dpos.y][dpos.x];
color.r = floor(color.r * 31.0 + threshold) / 31.0;  // 5-bit R
color.g = floor(color.g * 63.0 + threshold) / 63.0;  // 6-bit G
color.b = floor(color.b * 31.0 + threshold) / 31.0;  // 5-bit B
```

Since the VkImage color attachment is `VK_FORMAT_R8G8B8A8_UNORM`, the dithered
values are stored at 8-bit precision but quantized to 565 levels. This matches the
Voodoo behavior where the 565 value is what gets stored to the 16-bit framebuffer.

The dither subtraction (`FBZ_DITHER_SUB`, `dithersub_enabled`) is applied to
*destination* pixels read from the framebuffer before alpha blending. This
compensates for the quantization of the dest color. This only matters for the
shader-based alpha blend path (copy-on-blend).

**No additional uniforms needed for dither** — the Bayer matrix is a compile-time
constant in the shader, and the enable/mode bits are part of `u_fbzMode`.

---

## Final Recommended Shader Interface

```glsl
// Push constants (64 bytes total — within Vulkan's 128-byte minimum guarantee)
// Raw Voodoo registers — decoded with bitwise ops in shader
layout(push_constant) uniform PushConstants {
    uint u_fbzMode;          // fbzMode register
    uint u_fbzColorPath;     // fbzColorPath register
    uint u_alphaMode;        // alphaMode register
    uint u_fogMode;          // fogMode register
    uint u_textureMode0;     // textureMode[0] register
    uint u_textureMode1;     // textureMode[1] register

    // Decoded constant values
    vec4 u_color0;           // color0 (RGBA normalized)
    vec4 u_color1;           // color1 (RGBA normalized)
    // ... remaining fields packed to fit 64 bytes
    // See push-constant-layout.md for definitive layout
} pc;

// Descriptor set 0: combined image samplers
layout(set = 0, binding = 0) uniform sampler2D u_tex0;       // TMU0 texture (VK_FORMAT_R8G8B8A8_UNORM)
layout(set = 0, binding = 1) uniform sampler2D u_tex1;       // TMU1 texture (VK_FORMAT_R8G8B8A8_UNORM)
layout(set = 0, binding = 2) uniform sampler2D u_fog_table;  // Fog table (VK_FORMAT_R8G8_UNORM, 64x1 image)
```

**Total: 14 scalar/vector push constants (64 bytes) + 3 descriptor bindings = 17 values.
Push constants uploaded via `vkCmdPushConstants()`. Samplers bound via descriptor set.**
