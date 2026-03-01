# Voodoo Texture Data Flow Analysis

**Date**: 2026-02-27
**Scope**: How the SW renderer's texture cache works, and what the Vulkan path needs to do.

---

## 1. Texture Cache Structure

### `texture_t` (each cache entry)

**File**: `src/include/86box/vid_voodoo_common.h:245-255`

```c
typedef struct texture_t {
    uint32_t   base;              // Voodoo texture base address (or -1 = invalid)
    uint32_t   tLOD;              // tLOD register snapshot (masked to 0xf00fff)
    ATOMIC_INT refcount;          // Incremented when assigned to a triangle
    ATOMIC_INT refcount_r[4];     // Per-render-thread consumed count
    int        is16;              // 1 if texture format >= 8 (16-bit source texels)
    uint32_t   palette_checksum;  // XOR of palette[0..255] for palettized formats
    uint32_t   addr_start[4];    // Address range groups for dirty tracking
    uint32_t   addr_end[4];      // (4 groups covering different LOD ranges)
    uint32_t  *data;             // DECODED PIXEL DATA -- BGRA8888 packed uint32_t
} texture_t;
```

### Cache array in `voodoo_t`

**File**: `src/include/86box/vid_voodoo_common.h:705`

```c
texture_t texture_cache[2][TEX_CACHE_MAX];   // [tmu][slot]
// TEX_CACHE_MAX = 64 (vid_voodoo_common.h:32)
```

### Data buffer allocation

**File**: `src/video/vid_voodoo.c:1221-1228` (and :1371-1379 for Banshee/V3)

Each entry's `data` is allocated as:
```c
(256*256 + 256*256 + 128*128 + 64*64 + 32*32 + 16*16 + 8*8 + 4*4 + 2*2) * 4
```
= `(65536 + 65536 + 16384 + 4096 + 1024 + 256 + 64 + 16 + 4) * 4`
= `152896 * 4` = **611,584 bytes** per cache entry.

Note: The first 256*256 block (`texture_offset[0]`) is LOD 0 (base level).
Subsequent blocks are LOD 1 through LOD 8.

### `texture_offset[]` table

**File**: `src/include/86box/vid_voodoo_texture.h:19-31`

```c
static const uint32_t texture_offset[LOD_MAX + 3] = {
    0,                                              // LOD 0: 256x256
    256*256,                                        // LOD 1: 128x128
    256*256 + 128*128,                              // LOD 2: 64x64
    256*256 + 128*128 + 64*64,                      // LOD 3: 32x32
    256*256 + 128*128 + 64*64 + 32*32,              // LOD 4: 16x16
    256*256 + 128*128 + 64*64 + 32*32 + 16*16,      // LOD 5: 8x8
    ...                                              // LOD 6-8: 4x4, 2x2, 1x1
};
```

**CRITICAL**: The `data[]` array is indexed by `texture_offset[lod]` giving the
start of each mip level. However, the dimensions of each LOD may be SMALLER
than 256x(256>>lod) due to the aspect ratio -- only the USED region is populated.

---

## 2. Pixel Data Format

### `makergba` macro

**File**: `src/video/vid_voodoo_texture.c:238`

```c
#define makergba(r, g, b, a) ((b) | ((g) << 8) | ((r) << 16) | ((a) << 24))
```

This produces: `0xAARRGGBB` = **BGRA8888** (little-endian uint32_t).

- Byte 0 (lowest): B
- Byte 1: G
- Byte 2: R
- Byte 3 (highest): A

This matches `VK_FORMAT_B8G8R8A8_UNORM` exactly, which is what
`vc_texture_upload()` already creates the VkImage with. **No format
conversion needed.**

### How `tex_read()` unpacks it

**File**: `src/video/vid_voodoo_render.c:219-249`

```c
dat = state->tex[tmu][state->lod][s + (t << tex_shift)];
state->tex_b[tmu] = dat & 0xff;
state->tex_g[tmu] = (dat >> 8) & 0xff;
state->tex_r[tmu] = (dat >> 16) & 0xff;
state->tex_a[tmu] = (dat >> 24) & 0xff;
```

Confirms BGRA8888 (B in low byte).

---

## 3. Texture Decode -- All Formats

**File**: `src/video/vid_voodoo_texture.c:239-548` (`voodoo_use_texture()`)

`voodoo_use_texture()` decodes raw Voodoo texture memory (`voodoo->tex_mem[tmu]`)
into the `data[]` buffer as BGRA8888. It handles ALL 14 Voodoo texture formats:

| ID | Name | Src BPP | Decode method |
|----|------|---------|---------------|
| 0x0 | TEX_RGB332 | 1 | rgb332[] lookup table |
| 0x1 | TEX_Y4I2Q2 | 1 | ncc_lookup[] palette (YIQ->RGB) |
| 0x2 | TEX_A8 | 1 | A=R=G=B=dat |
| 0x3 | TEX_I8 | 1 | R=G=B=dat, A=0xFF |
| 0x4 | TEX_AI8 | 1 | 4-bit intensity + 4-bit alpha |
| 0x5 | TEX_PAL8 | 1 | palette[] lookup, A=0xFF |
| 0x6 | TEX_APAL8 | 1 | palette[] w/ alpha repack |
| 0x8 | TEX_ARGB8332 | 2 | rgb332[low]+alpha=high |
| 0x9 | TEX_A8Y4I2Q2 | 2 | ncc_lookup[low]+alpha=high |
| 0xa | TEX_R5G6B5 | 2 | rgb565[] lookup |
| 0xb | TEX_ARGB1555 | 2 | argb1555[] lookup |
| 0xc | TEX_ARGB4444 | 2 | argb4444[] lookup |
| 0xd | TEX_A8I8 | 2 | intensity low + alpha high |
| 0xe | TEX_APAL88 | 2 | palette[low]+alpha=high |

Formats 0-6 are 8-bit (1 byte/texel). Formats 8-14 (0x8-0xe) are 16-bit
(2 bytes/texel). The `is16` field = `(tformat & 8)`.

---

## 4. Texture Dimensions and Mip Levels

### From `voodoo_recalc_tex12()`

**File**: `src/video/vid_voodoo_texture.c:58-143`

Base dimensions start at 256x256, then the aspect ratio (from `tLOD` bits 21-22)
shrinks one axis:

```c
int aspect = (tLOD >> 21) & 3;   // 0, 1, 2, or 3
// LOD_S_IS_WIDER (bit 20) determines which axis shrinks
if (tLOD & LOD_S_IS_WIDER)
    height >>= aspect;    // e.g. aspect=2: 256x64
else
    width >>= aspect;     // e.g. aspect=2: 64x256
```

LOD range is controlled by:
```c
lod_min = (tLOD >> 2) & 15;   // Coarsest LOD to use
lod_max = (tLOD >> 8) & 15;   // Finest LOD to use
```

Both are clamped to `MIN(val, 8)` (LOD_MAX = 8).

### Per-LOD dimensions

For each LOD level, the width and height are stored in `params->tex_w_mask[tmu][lod]`
and `params->tex_h_mask[tmu][lod]`. These are masks (width-1, height-1), so:

```
actual_width  = tex_w_mask[tmu][lod] + 1
actual_height = tex_h_mask[tmu][lod] + 1
```

For example, a 256x128 texture (aspect=1, S_IS_WIDER):
- LOD 0: w=256, h=128 (w_mask=255, h_mask=127)
- LOD 1: w=128, h=64
- LOD 2: w=64, h=32
- ...

### How data is laid out in the `data[]` buffer

The `texture_offset[]` table assumes SQUARE 256x256 mip chain spacing.
Each LOD's data starts at `data[texture_offset[lod]]` regardless of actual
texture dimensions. The actual pixels occupy only the first
`(w_mask+1) * (h_mask+1)` entries, but the full square allocation is reserved.

The decode loop writes:
```c
uint32_t *base = &voodoo->texture_cache[tmu][c].data[texture_offset[lod]];
// for y in 0..tex_h_mask[tmu][lod]:
//   for x in 0..tex_w_mask[tmu][lod]:
//     base[x] = makergba(...)
//   base += (1 << shift)  // shift = 8 - tex_lod (for square base)
```

The `shift` value (= `8 - params->tex_lod[tmu][lod]`) controls the row stride
in the decoded buffer. For a square texture at LOD 0, shift=8 means stride=256.
For LOD 1, shift=7 means stride=128. The actual texture width may be smaller
than the stride due to aspect ratio.

**KEY INSIGHT**: The row stride in `data[]` is always a power of 2 equal to
the mip level's maximum dimension (as if the texture were square), not the
actual width. This matters for upload: we must set `row_stride` correctly.

---

## 5. Texture Cache Lookup and Invalidation

### Cache lookup (hit path)

**File**: `src/video/vid_voodoo_texture.c:275-283`

On each triangle submission, `voodoo_use_texture()` is called per TMU.
It searches `texture_cache[tmu][0..63]` for a matching entry:

```c
for (c = 0; c < TEX_CACHE_MAX; c++) {
    if (cache[c].base == addr
        && cache[c].tLOD == (params->tLOD[tmu] & 0xf00fff)
        && cache[c].palette_checksum == palette_checksum)
    {
        params->tex_entry[tmu] = c;
        cache[c].refcount++;
        return;  // HIT
    }
}
```

Cache key = `(base_address, tLOD & 0xf00fff, palette_checksum)`.

### Cache miss (decode + fill)

On miss, it evicts the least-recently-used entry (round-robin) and decodes
the full mip chain into `data[]` from `voodoo->tex_mem[tmu]`.

### Invalidation

**File**: `src/video/vid_voodoo_texture.c:550-590` (`flush_texture_cache()`)

Called when texture memory is written (from FIFO writes at
`vid_voodoo_fifo.c:714,1005` or from blitter at `vid_voodoo_banshee.c:1710`).

```c
void flush_texture_cache(voodoo_t *voodoo, uint32_t dirty_addr, int tmu)
```

For each cache entry, if the dirty address falls within any of its 4 address
ranges, the entry's `base` is set to `-1` (invalidated). On the next
`voodoo_use_texture()` call, it won't match and will be re-decoded.

The `texture_present[tmu][16384]` array tracks which 1KB-aligned regions
of texture memory have active cache entries (for fast dirty checking).

---

## 6. Per-TMU Texture State in `voodoo_params_t`

**File**: `src/include/86box/vid_voodoo_common.h:124-230`

Fields that identify the current texture for each TMU:

| Field | Type | Description |
|-------|------|-------------|
| `textureMode[2]` | uint32_t | Mode register (filter, clamp, TC-zero, etc.) |
| `tLOD[2]` | uint32_t | LOD register (min/max LOD, aspect, split, etc.) |
| `texBaseAddr[2]` | uint32_t | Base address in texture memory |
| `texBaseAddr1[2]` | uint32_t | Alternate base for split/odd |
| `tformat[2]` | int | Texture format enum (0x0-0xe) |
| `tex_base[2][LOD_MAX+2]` | uint32_t | Per-LOD base address in tex_mem |
| `tex_end[2][LOD_MAX+2]` | uint32_t | Per-LOD end address |
| `tex_w_mask[2][LOD_MAX+2]` | int | Per-LOD width mask (width-1) |
| `tex_h_mask[2][LOD_MAX+2]` | int | Per-LOD height mask (height-1) |
| `tex_shift[2][LOD_MAX+2]` | int | Per-LOD row shift for data[] indexing |
| `tex_lod[2][LOD_MAX+2]` | int | Per-LOD actual LOD number |
| `tex_entry[2]` | int | Cache slot index (set by voodoo_use_texture) |

After `voodoo_use_texture()` completes:
- `params->tex_entry[tmu]` = index into `texture_cache[tmu][]`
- `texture_cache[tmu][tex_entry].data` = decoded BGRA8888 pixels

---

## 7. SW Renderer Connection

**File**: `src/video/vid_voodoo_render.c:724-725`

In `voodoo_half_triangle()`, the render state is set up:

```c
for (uint8_t c = 0; c <= LOD_MAX; c++) {
    state->tex[0][c] = &voodoo->texture_cache[0][params->tex_entry[0]].data[texture_offset[c]];
    state->tex[1][c] = &voodoo->texture_cache[1][params->tex_entry[1]].data[texture_offset[c]];
}
```

Then `tex_read()` fetches from `state->tex[tmu][lod]` using S/T coordinates.

---

## 8. Call Chain Summary

```
Guest writes triangle registers
    -> voodoo_reg_writel() [vid_voodoo_reg.c]
    -> voodoo_queue_triangle() [vid_voodoo_render.c:1854]
        -> IF Vulkan: vc_voodoo_submit_triangle()  [vid_voodoo_vk.c:300]
           (CURRENTLY DOES NOT CALL voodoo_use_texture!)
           (CURRENTLY DOES NOT UPLOAD ANY TEXTURE DATA!)
        -> ELSE (SW): voodoo_use_texture() for TMU0 and TMU1
           -> Cache hit: set tex_entry, return
           -> Cache miss: decode tex_mem into data[], set tex_entry
           -> memcpy params to render thread ring
           -> Render thread: voodoo_half_triangle() reads data[]
```

---

## 9. What the Vulkan Path Needs -- Concrete Recommendation

### The Problem

`vc_voodoo_submit_triangle()` in `vid_voodoo_vk.c:300` currently:
1. Converts vertex positions, colors, depth, texture coords to floats
2. Sends push constants (register state)
3. Submits triangle vertices

But it NEVER:
- Calls `voodoo_use_texture()` to populate the texture cache
- Uploads texture data to a VkImage
- Binds a texture descriptor set

Result: All triangles sample from the 1x1 white placeholder texture.

### The Solution

Before submitting each triangle, `vid_voodoo_vk.c` must:

#### Step 1: Populate the SW texture cache (reuse existing code)

```c
voodoo_use_texture(voodoo, params, 0);
if (voodoo->dual_tmus)
    voodoo_use_texture(voodoo, params, 1);
```

This decodes the Voodoo texture formats into BGRA8888 in the `data[]` buffer.
The Vulkan path gets this FOR FREE -- the decode logic already exists.

#### Step 2: Upload to VkImage when texture changes

Track the current texture identity per TMU (cache slot index + generation).
When it changes:

```c
int slot = params->tex_entry[tmu];
texture_t *tex = &voodoo->texture_cache[tmu][slot];

// Base level dimensions from params
int lod_min = (params->tLOD[tmu] >> 2) & 15;
if (lod_min > 8) lod_min = 8;
int base_w = params->tex_w_mask[tmu][lod_min] + 1;
int base_h = params->tex_h_mask[tmu][lod_min] + 1;

// Number of mip levels
int lod_max = (params->tLOD[tmu] >> 8) & 15;
if (lod_max > 8) lod_max = 8;
int mip_count = lod_max - lod_min + 1;

// Pixel data for base level
uint32_t *pixels = &tex->data[texture_offset[lod_min]];

// Row stride in the data[] buffer (NOT necessarily == base_w)
int row_stride = 1 << (8 - params->tex_lod[tmu][lod_min]);

// Upload
vc_texture_upload(ctx->texture, ctx, vk_slot,
                  base_w, base_h, mip_count,
                  pixels, row_stride);
```

**WARNING**: The current `vc_texture_upload()` only uploads the BASE level.
For mip maps, it would need to be extended to accept per-level data pointers
and dimensions, or upload each level separately. For Phase 5, uploading just
LOD 0 and using `VK_SAMPLER_MIPMAP_MODE_NEAREST` with `maxLod = 0` is
acceptable.

#### Step 3: Row stride handling

The `data[]` buffer has a row stride of `(1 << shift)` uint32_t values,
where `shift = 8 - tex_lod`. For a non-square texture (e.g., 256x64), the
row stride in the data buffer is 256, but the actual width is 256. For a
64x256 texture, the row stride is 64 (at LOD 0, shift=6), matching the width.

Wait -- let me re-examine. In `voodoo_recalc_tex12()`:
- shift starts at 8 (= log2(256))
- if `LOD_S_IS_WIDER`, height shrinks, width stays at 256, shift stays at 8
- if NOT `LOD_S_IS_WIDER`, width shrinks, shift decreases with aspect

So for S_IS_WIDER (e.g., 256x64): shift=8, stride=256, width=256. OK.
For T_IS_WIDER (e.g., 64x256): shift=6, stride=64, width=64. OK.

In both cases, `row_stride = 1 << shift = width` at LOD 0 for the base
aspect ratio. But at deeper LOD levels, the relationship can differ from
the standard mip-chain halving due to LOD_SPLIT.

**For Phase 5 (base level only)**: row_stride = `1 << (8 - tex_lod[tmu][lod_min])`.
This equals the width for standard (non-split) textures.

#### Step 4: Texture identity tracking

To avoid re-uploading every triangle, track per TMU:

```c
// In vid_voodoo_vk.c or a VK-specific state struct
struct {
    uint32_t base;           // texture_cache[tmu][slot].base
    uint32_t tLOD;           // texture_cache[tmu][slot].tLOD
    uint32_t palette_chksum; // texture_cache[tmu][slot].palette_checksum
    int      vk_slot;        // vc_texture pool slot (-1 = none)
} vk_tex_state[2];
```

On each triangle:
1. Call `voodoo_use_texture()` to get `params->tex_entry[tmu]`
2. Compare cache entry's `(base, tLOD, palette_checksum)` with tracked state
3. If changed: upload new texture, update `vk_slot`
4. Pass `vk_slot` to `vc_texture_bind()` before drawing

#### Step 5: Sampler creation

The sampler mode comes from `params->textureMode[tmu]`:
- Bit `TEXTUREMODE_TCLAMPS` (bit 3) -- S clamp vs wrap
- Bit `TEXTUREMODE_TCLAMPT` (bit 4) -- T clamp vs wrap
- Bit `TEXTUREMODE_MAGNIFICATION_FILTER` -- point vs bilinear mag
- Bit `TEXTUREMODE_MINIFICATION_FILTER` -- point vs bilinear min

Use `vc_texture_get_sampler()` (already in vc_texture.c) to get/create a
VkSampler with the correct mode.

---

## 10. Mip Level Upload Complexity (Future Work)

For full mip chain support, each LOD level needs individual upload because:

1. Each LOD has different dimensions: `(tex_w_mask[lod]+1) x (tex_h_mask[lod]+1)`
2. Each LOD's data starts at `data[texture_offset[lod]]` with its own row stride
3. LOD_SPLIT textures interleave odd/even mip levels from different base addresses

The `vc_texture_upload()` function would need extension to:
- Accept per-mip pixel data pointers and dimensions
- Issue separate `vkCmdCopyBufferToImage` for each mip level
- Handle the different row strides per level

For Phase 5, uploading only LOD 0 (base level) with `maxLod = 0.0f` on the
sampler is correct enough -- Voodoo games typically use mipmapping but the
GPU will handle LOD selection in the shader later.

---

## 11. Summary of Key Facts

| Item | Value |
|------|-------|
| Decoded pixel format | BGRA8888 (`makergba` = B low, A high) |
| VkFormat match | `VK_FORMAT_B8G8R8A8_UNORM` (already used) |
| Cache size | 64 entries per TMU (`TEX_CACHE_MAX`) |
| Max texture size | 256x256 |
| Max mip levels | 9 (LOD 0..8) |
| Data buffer size | 611,584 bytes per entry |
| Cache key | `(base_addr, tLOD & 0xf00fff, palette_checksum)` |
| Invalidation trigger | Write to texture memory region |
| Decode function | `voodoo_use_texture()` -- already exists, full format support |
| Missing call | `voodoo_use_texture()` is NOT called in the Vulkan path |
| Missing upload | `vc_texture_upload()` has ZERO callers |
| Missing bind | `vc_texture_bind()` has ZERO callers from vid_voodoo_vk.c |

**Bottom line**: The decoded BGRA8888 data is ready and waiting in the SW
texture cache. The Vulkan path just needs to:
1. Call `voodoo_use_texture()` to populate/lookup the cache
2. Call `vc_texture_upload()` with the `data[]` pointer and correct dimensions
3. Call `vc_texture_bind()` before drawing
4. Track texture identity to avoid redundant uploads
