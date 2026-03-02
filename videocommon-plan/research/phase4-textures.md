# Phase 4 Texture Research: Voodoo Texture System for VideoCommon

**Date**: 2026-03-01
**Author**: vc-arch agent
**Status**: Complete

## Table of Contents

1. [Texture Formats](#1-texture-formats)
2. [Texture Cache Structure](#2-texture-cache-structure)
3. [Texture Decode Path](#3-texture-decode-path)
4. [Texture Parameters (tLOD, textureMode)](#4-texture-parameters)
5. [TMU Organization in voodoo_params_t](#5-tmu-organization)
6. [Existing VK Bridge Code](#6-existing-vk-bridge-code)
7. [VkImage Strategy for Phase 4](#7-vkimage-strategy)
8. [Descriptor Set Management](#8-descriptor-set-management)
9. [Ring Command Design](#9-ring-command-design)
10. [Implementation Recommendations](#10-implementation-recommendations)

---

## 1. Texture Formats

### 1.1 Format Enumeration

All Voodoo texture formats are defined in `src/include/86box/vid_voodoo_regs.h` (line 386):

| Value | Name          | BPP | Description                          | Alpha |
|-------|---------------|-----|--------------------------------------|-------|
| 0x0   | TEX_RGB332    | 8   | 3-3-2 RGB                           | None (0xFF) |
| 0x1   | TEX_Y4I2Q2    | 8   | YIQ via NCC lookup table             | None (0xFF) |
| 0x2   | TEX_A8        | 8   | Alpha-only (R=G=B=A=val)            | Full |
| 0x3   | TEX_I8        | 8   | Intensity (R=G=B=val, A=0xFF)       | None |
| 0x4   | TEX_AI8       | 8   | 4-bit alpha + 4-bit intensity        | 4-bit |
| 0x5   | TEX_PAL8      | 8   | Palette lookup (256 entry, no alpha) | None (0xFF) |
| 0x6   | TEX_APAL8     | 8   | Palette with packed ARGB decode      | From palette |
| 0x8   | TEX_ARGB8332  | 16  | 8-bit alpha + 3-3-2 RGB             | Full 8-bit |
| 0x9   | TEX_A8Y4I2Q2  | 16  | 8-bit alpha + YIQ via NCC table      | Full 8-bit |
| 0xA   | TEX_R5G6B5    | 16  | 5-6-5 RGB (no alpha)                | None (0xFF) |
| 0xB   | TEX_ARGB1555  | 16  | 1-bit alpha + 5-5-5 RGB             | 1-bit |
| 0xC   | TEX_ARGB4444  | 16  | 4-4-4-4 ARGB                        | 4-bit |
| 0xD   | TEX_A8I8      | 16  | 8-bit alpha + 8-bit intensity        | Full 8-bit |
| 0xE   | TEX_APAL88    | 16  | 8-bit alpha + 8-bit palette index    | Full 8-bit |

**Key observation**: Bit 3 of the format value (`tformat & 8`) determines 8-bit vs
16-bit per texel. This is used throughout the code to calculate VRAM layout sizes.

- Formats 0x0-0x6: 8-bit (1 byte per texel in VRAM)
- Formats 0x8-0xE: 16-bit (2 bytes per texel in VRAM)
- Format 0x7 and 0xF: not defined (gap in enum)

### 1.2 Palette and NCC Table Dependencies

Three format groups require external lookup data:

1. **Palette formats** (TEX_PAL8, TEX_APAL8, TEX_APAL88): Depend on
   `voodoo->palette[tmu][256]` (type `rgba_u`). The palette is per-TMU.
   Identity tracked via XOR checksum of all 256 entries.

2. **YIQ/NCC formats** (TEX_Y4I2Q2, TEX_A8Y4I2Q2): Depend on
   `voodoo->ncc_lookup[tmu][sel][256]` where `sel` is
   `(textureMode & TEXTUREMODE_NCC_SEL) ? 1 : 0`. Two NCC tables per TMU.

3. **RGB lookup formats** (TEX_RGB332, TEX_ARGB8332): Use global `rgb332[256]`
   lookup table, initialized once at startup. Not dynamic.

### 1.3 Decoded Pixel Format

The `makergba(r, g, b, a)` macro at `vid_voodoo_texture.c:238`:
```c
#define makergba(r, g, b, a) ((b) | ((g) << 8) | ((r) << 16) | ((a) << 24))
```

This produces a uint32_t with byte layout (little-endian): **B, G, R, A**
In Vulkan terms: **VK_FORMAT_B8G8R8A8_UNORM** (BGRA byte order).

This matches our offscreen framebuffer format, which is convenient.

---

## 2. Texture Cache Structure

### 2.1 texture_t (Cache Entry)

Defined at `src/include/86box/vid_voodoo_common.h:244`:

```c
typedef struct texture_t {
    uint32_t   base;             /* VRAM base address (identity key) */
    uint32_t   tLOD;             /* tLOD register value, masked to 0xf00fff (identity key) */
    ATOMIC_INT refcount;         /* Incremented by FIFO thread on voodoo_use_texture() */
    ATOMIC_INT refcount_r[4];    /* Incremented by render thread(s) after consuming */
    int        is16;             /* Whether format has 16-bit texels (tformat & 8) */
    uint32_t   palette_checksum; /* XOR of palette[].u for palettized formats */
    uint32_t   addr_start[4];   /* VRAM address ranges (4 groups for dirty tracking) */
    uint32_t   addr_end[4];     /* End of VRAM address ranges */
    uint32_t  *data;             /* Decoded BGRA8 pixel data (all LODs) */
} texture_t;
```

### 2.2 Cache Organization

```
voodoo->texture_cache[2][TEX_CACHE_MAX]   // TEX_CACHE_MAX = 64
```

- **2 TMUs**: `texture_cache[0][]` for TMU0, `texture_cache[1][]` for TMU1
- **64 entries per TMU**: 128 total cache entries
- Each entry's `data` is heap-allocated at init time (never reallocated)

### 2.3 Cache Entry Data Size

Allocated at `vid_voodoo.c:1219`:
```c
malloc((256*256 + 256*256 + 128*128 + 64*64 + 32*32 + 16*16 + 8*8 + 4*4 + 2*2) * 4)
```

This is: `(65536 + 65536 + 16384 + 4096 + 1024 + 256 + 64 + 16 + 4) * 4`
= `152896 * 4` = **611,584 bytes** per cache entry.

NOTE: The first 256*256 slot is `texture_offset[0]` (LOD 0). The layout includes
an extra 256*256 at the front -- looking at `texture_offset[]`:

```c
static const uint32_t texture_offset[LOD_MAX + 3] = {
    0,                          // LOD 0: starts at 0
    256 * 256,                  // LOD 1: starts at 65536
    256*256 + 128*128,          // LOD 2: starts at 81920
    ...                         // etc., standard mipmap chain
    256*256+128*128+64*64+32*32+16*16+8*8+4*4+2*2+1*1,  // LOD 9
    256*256+128*128+64*64+32*32+16*16+8*8+4*4+2*2+1*1+1 // LOD 10
};
```

Total per-cache-entry data: **~600 KB** (all LOD levels, decoded to BGRA8).
Total for all cache entries: `64 * 2 * 600KB` = ~**75 MB** of decoded textures.

**For VkImage upload**: We only need to upload the LOD range actually used by
the current texture (lod_min to lod_max). Most games use LOD 0 only (256x256
or smaller), so typical uploads are 256KB or less.

### 2.4 Identity Tracking (Cache Lookup)

A texture cache hit requires matching all three keys (`vid_voodoo_texture.c:274`):

```c
if (cache[c].base == addr &&
    cache[c].tLOD == (params->tLOD[tmu] & 0xf00fff) &&
    cache[c].palette_checksum == palette_checksum)
```

**Identity key = {base_address, tLOD_masked, palette_checksum}**

- `base`: texBaseAddr (or texBaseAddr1 when LOD_SPLIT + LOD_ODD + TMULTIBASEADDR)
- `tLOD & 0xf00fff`: Mask keeps LOD min/max (bits 2-11), S_IS_WIDER/aspect (bits
  20-22), LOD_ODD/SPLIT (bits 18-19), TMULTIBASEADDR (bit 24), TMIRROR_S/T
  (bits 28-29). Discards bits 12-17 (unused/reserved).
- `palette_checksum`: XOR of all 256 palette entries (0 for non-paletted)

### 2.5 Refcount / Eviction

The refcount mechanism tracks in-flight texture usage across threads:

- **`refcount`**: Incremented by FIFO thread every time `voodoo_use_texture()`
  is called (both cache hit and miss). This happens for every triangle batch.
- **`refcount_r[i]`**: Incremented by render thread `i` after it finishes
  using the texture for a batch (in `voodoo_triangle()` at line 1628).

**Eviction rule** (`vid_voodoo_texture.c:286`): A cache entry is evictable when
`refcount == refcount_r[0]` (and `== refcount_r[1]` if 2 render threads).
This means all triangles referencing this texture have been rendered.

**Round-robin eviction**: `texture_last_removed` cycles through entries.

**For VideoCommon VK path**: The GPU thread must increment `refcount_r[0]` for
every texture reference, matching the FIFO thread's `refcount` increments. This
prevents the FIFO thread from evicting a texture the GPU thread is still using.
See DESIGN.md section 7.7.

### 2.6 Dirty Tracking / Invalidation

`voodoo->texture_present[2][16384]` is a bitmap indexed by
`(addr & texture_mask) >> TEX_DIRTY_SHIFT` where `TEX_DIRTY_SHIFT = 10`.
This means 1 dirty bit per 1KB of texture VRAM.

When a texture VRAM write occurs (`voodoo_tex_writel()`):
1. Check if `texture_present[tmu][dirty_idx]` is set
2. If so, call `flush_texture_cache()` which invalidates overlapping entries
   (sets `base = -1`) and rebuilds the present bitmap for surviving entries.

**For VideoCommon**: Texture VRAM writes invalidate the CPU-side decode cache.
The VK path needs its own invalidation tracking for VkImages -- when a texture
cache entry is re-decoded, the corresponding VkImage must be re-uploaded.

---

## 3. Texture Decode Path

### 3.1 Call Chain

```
voodoo_queue_triangle()
  -> voodoo_use_texture(voodoo, params, 0)   // TMU0
  -> voodoo_use_texture(voodoo, params, 1)   // TMU1 (if dual TMU)
```

`voodoo_use_texture()` either:
- **Cache hit**: Sets `params->tex_entry[tmu] = c`, increments `refcount`, returns
- **Cache miss**: Evicts oldest unused entry, decodes all LODs, stores in `data[]`

### 3.2 Decode Details

For each LOD level from `lod_min` to `lod_max`:

1. **Source address**: `params->tex_base[tmu][lod] & voodoo->texture_mask`
2. **Dimensions**: `tex_w_mask[tmu][lod]+1` x `tex_h_mask[tmu][lod]+1`
3. **Destination**: `&cache[c].data[texture_offset[lod]]` (flat 256x256 atlas,
   rows are always 256 pixels wide regardless of actual texture width)
4. **Row stride**: Source uses `tex_shift[tmu][lod]`, destination uses
   `1 << (8 - tex_lod[tmu][lod])` (256 >> lod_level)

The decode loop reads from `voodoo->tex_mem[tmu][]` (raw VRAM bytes) and writes
decoded BGRA8 uint32_t values to the cache data buffer.

### 3.3 Decoded Data Layout

Each cache entry's `data[]` is a **flat 256x256 pixel atlas** (regardless of
actual texture dimensions). LOD levels are stored at offsets given by
`texture_offset[]`:

```
data[0..65535]        = LOD 0 (up to 256x256)
data[65536..81919]    = LOD 1 (up to 128x128)
data[81920..86015]    = LOD 2 (up to 64x64)
data[86016..87039]    = LOD 3 (up to 32x32)
data[87040..87295]    = LOD 4 (up to 16x16)
data[87296..87359]    = LOD 5 (up to 8x8)
data[87360..87375]    = LOD 6 (up to 4x4)
data[87376..87379]    = LOD 7 (up to 2x2)
data[87380]           = LOD 8 (1x1)
```

**IMPORTANT**: Within each LOD, rows are padded to `(256 >> lod)` pixels wide.
For a 64x64 texture at LOD 0, only the first 64 pixels of each 256-pixel row
contain valid data. This means we **cannot** treat the buffer as a tightly-packed
2D image. For VkImage upload, we need to either:
- (a) Copy row-by-row with correct stride, or
- (b) Use a staging buffer with `bufferRowLength` set in `VkBufferImageCopy`

### 3.4 Aspect Ratio and Non-Square Textures

The Voodoo supports non-square textures via the `aspect` field in tLOD (bits 21-22):

```c
int aspect = (tLOD >> 21) & 3;  // 0=1:1, 1=2:1, 2=4:1, 3=8:1
if (tLOD & LOD_S_IS_WIDER)
    height >>= aspect;   // e.g., 256x128, 256x64, 256x32
else
    width >>= aspect;    // e.g., 128x256, 64x256, 32x256
```

Maximum texture dimension: **256x256** (LOD 0).
Minimum texture dimension: **1x1** (LOD 8).
Aspect ratios: 1:1, 2:1, 4:1, 8:1 (either direction).

So valid LOD 0 sizes include: 256x256, 256x128, 256x64, 256x32, 128x256,
64x256, 32x256.

---

## 4. Texture Parameters

### 4.1 textureMode Register

Defined via flags at `vid_voodoo_regs.h:403-408`:

```c
TEXTUREMODE_NCC_SEL   = (1 << 5)   // Select NCC table 0 or 1
TEXTUREMODE_TCLAMPS   = (1 << 6)   // Clamp S coordinate
TEXTUREMODE_TCLAMPT   = (1 << 7)   // Clamp T coordinate
TEXTUREMODE_TRILINEAR = (1 << 30)  // Trilinear filtering mode
```

The textureMode register also contains the texture combine unit (TCU) fields
that control how texture color is combined. These are already passed through
push constants as `textureMode0`/`textureMode1` -- the uber-shader handles
them. They don't affect texture upload.

Additional textureMode bits relevant to the shader (from regs.h):
- `TEXTUREMODE_MASK = 0x3ffff000` -- mask for all TCU fields
- `TEXTUREMODE_PASSTHROUGH = 0` -- TCU passes texture color through
- `TEXTUREMODE_LOCAL_MASK/LOCAL` -- for local color selection

Bit 0 (`textureMode & 1`): **Perspective correction enable**. When set, use
perspective-correct texture coordinate interpolation. When clear, affine.

Bit 31 (`textureMode & (1 << 31)`): In `voodoo_tex_writel`, changes the S
address calculation for Voodoo 2+ texture writes.

### 4.2 tLOD Register

Key bit fields:

| Bits    | Field            | Description |
|---------|------------------|-------------|
| [3:2]   | LOD min          | Minimum LOD level (0-8, extracted as `(tLOD >> 2) & 15`) |
| [11:8]  | LOD max          | Maximum LOD level (0-8, extracted as `(tLOD >> 8) & 15`) |
| [18]    | LOD_ODD          | Use odd LODs (for split LOD) |
| [19]    | LOD_SPLIT        | Split LOD mode (even/odd in separate VRAM regions) |
| [20]    | LOD_S_IS_WIDER   | S dimension >= T dimension |
| [22:21] | Aspect ratio     | 0=1:1, 1=2:1, 2=4:1, 3=8:1 |
| [24]    | LOD_TMULTIBASEADDR | Multiple base addresses per LOD |
| [28]    | LOD_TMIRROR_S    | Mirror S coordinate |
| [29]    | LOD_TMIRROR_T    | Mirror T coordinate |

**For identity matching**: `tLOD & 0xf00fff` masks to bits [31:20] and [11:0],
capturing aspect, S_IS_WIDER, MIRROR_S, MIRROR_T, SPLIT, ODD, LOD_min, LOD_max,
and TMULTIBASEADDR.

### 4.3 texBaseAddr Registers

```c
texBaseAddr[2]    // Base address for LOD 0 (per TMU)
texBaseAddr1[2]   // Base address for LOD 1 (when TMULTIBASEADDR)
texBaseAddr2[2]   // Base address for LOD 2
texBaseAddr38[2]  // Base address for LOD 3-8
```

### 4.4 tformat

```c
int tformat[2];   // Per-TMU texture format (TEX_* enum value)
```

Set when the textureMode register is written. The low nibble (bits 0-3) is
the format type. Bit 3 distinguishes 8-bit from 16-bit formats.

---

## 5. TMU Organization in voodoo_params_t

The per-triangle texture coordinate gradients are in `voodoo_params_t`
(`vid_voodoo_common.h:155-170`):

```c
struct {
    int64_t startS;   // Start S coordinate (18.32 fixed-point)
    int64_t startT;   // Start T coordinate (18.32 fixed-point)
    int64_t startW;   // Start 1/W (18.32 fixed-point)
    int64_t p1;       // Padding
    int64_t dSdX;     // S gradient per X pixel
    int64_t dTdX;     // T gradient per X pixel
    int64_t dWdX;     // 1/W gradient per X pixel
    int64_t p2;
    int64_t dSdY;
    int64_t dTdY;
    int64_t dWdY;
    int64_t p3;
} tmu[2];            // Index 0 = TMU0, 1 = TMU1
```

Plus the global W coordinate (not texture-specific):
```c
int64_t startW;  // Main 1/W for perspective correction
int64_t dWdX;
int64_t dWdY;
```

**For vertex extraction in vid_voodoo_vk.c**: The S/T/W values use the same
gradient-reconstruction technique as colors. Convert from 18.32 fixed-point:
`float_val = (double)int64_val / (double)(1LL << 32)`.

Per-TMU params relevant to Phase 4:
```c
uint32_t textureMode[2];   // TCU control, clamp, perspective
uint32_t tLOD[2];          // LOD range, aspect, mirror, split
uint32_t texBaseAddr[2];   // Base addresses
uint32_t texBaseAddr1[2];
uint32_t texBaseAddr2[2];
uint32_t texBaseAddr38[2];
uint32_t tex_base[2][LOD_MAX+2];  // Computed per-LOD base addresses
uint32_t tex_end[2][LOD_MAX+2];   // Computed per-LOD end addresses
int      tex_w_mask[2][LOD_MAX+2]; // Width-1 per LOD
int      tex_h_mask[2][LOD_MAX+2]; // Height-1 per LOD
int      tex_shift[2][LOD_MAX+2];  // Row shift per LOD
int      tex_lod[2][LOD_MAX+2];    // Actual LOD index per LOD
int      tex_entry[2];             // Index into texture_cache[]
int      tformat[2];               // Texture format (TEX_* enum)
int      tex_width[2];             // Base width
```

---

## 6. Existing VK Bridge Code

### 6.1 Current State of vid_voodoo_vk.c

The VK bridge (`src/video/vid_voodoo_vk.c`) currently handles:

1. **Vertex extraction** (`voodoo_vk_extract_vertices`): Reconstructs 3 vertices
   from gradient data. Positions, colors. **Texture coords are NOT extracted yet**
   -- s0/t0/w0/s1/t1/w1 are left as zero (memset). z and w are hardcoded to
   0.5 and 1.0 respectively.

2. **Push constant extraction** (`voodoo_vk_extract_push_constants`): Copies
   fbzMode, fbzColorPath, alphaMode, fogMode, textureMode[0], textureMode[1],
   color0, color1, chromaKey, fogColor, zaColor, stipple, detail0/1, fb_width/height.
   No texture-specific data beyond the register values.

3. **Triangle push** (`voodoo_vk_push_triangle`): Packages push constants +
   3 vertices into ring command VC_CMD_TRIANGLE.

4. **Swap push** (`voodoo_vk_push_swap`): Sends VC_CMD_SWAP.

### 6.2 What Needs Adding for Phase 4

1. **Texture coordinate extraction**: Reconstruct s0/t0/w0 (and s1/t1/w1) from
   the 18.32 fixed-point gradients in `params->tmu[0]` and `params->tmu[1]`.

2. **Texture upload ring command**: When `voodoo_use_texture()` decodes a new
   texture (cache miss), push VC_CMD_TEXTURE_UPLOAD to the ring with the decoded
   BGRA8 data.

3. **Texture bind ring command**: Before each triangle (or batch), push
   VC_CMD_TEXTURE_BIND to tell the GPU thread which texture slots to use.

4. **Refcount tracking**: GPU thread must increment `refcount_r[0]` for each
   texture reference consumed.

### 6.3 vc_vertex_t Already Has Texture Fields

```c
typedef struct vc_vertex_t {
    float x, y;             // screen-space position
    float z;                // depth
    float w;                // 1/W for perspective
    float r, g, b, a;       // iterated color
    float s0, t0, w0;       // TMU0 texture coords   <-- Phase 4
    float s1, t1, w1;       // TMU1 texture coords   <-- Phase 4
    float fog;              // fog coordinate
    float pad[3];           // align to 72 bytes
} vc_vertex_t;             // 72 bytes
```

### 6.4 vc_push_constants_t Already Has Texture Fields

```c
typedef struct vc_push_constants_t {
    // ... existing fields ...
    uint32_t textureMode0;   // Already populated from params->textureMode[0]
    uint32_t textureMode1;   // Already populated from params->textureMode[1]
    uint32_t detail0;        // Currently hardcoded to 0
    uint32_t detail1;        // Currently hardcoded to 0
    // ... etc ...
} vc_push_constants_t;     // 64 bytes total
```

---

## 7. VkImage Strategy for Phase 4

### 7.1 One VkImage per Cache Entry

Per DESIGN.md section 7.7:
- 2 TMUs x 64 cache entries = **128 VkImage slots**
- Format: **VK_FORMAT_B8G8R8A8_UNORM** (matches `makergba` byte order)
- Each VkImage = 256x256 with up to 9 mip levels (LOD 0-8)

### 7.2 Upload Approach

The existing `voodoo_use_texture()` already decodes textures to BGRA8 on the
FIFO thread. The VK path piggybacks on this:

1. After `voodoo_use_texture()` returns (cache hit or miss), check if the VK
   texture for this slot needs uploading.
2. **On cache miss** (new decode): Copy decoded pixels to a malloc'd buffer,
   push VC_CMD_TEXTURE_UPLOAD to ring.
3. **On cache hit**: Just push VC_CMD_TEXTURE_BIND.

### 7.3 VK Texture State Tracking

For each of the 128 slots, track:
```c
typedef struct vc_tex_slot_t {
    VkImage       image;
    VkImageView   view;
    VmaAllocation allocation;
    uint32_t      identity_base;        // Matches texture_cache[].base
    uint32_t      identity_tLOD;        // Matches texture_cache[].tLOD
    uint32_t      identity_pal_checksum; // Matches texture_cache[].palette_checksum
    uint32_t      width;                // LOD 0 width
    uint32_t      height;               // LOD 0 height
    uint32_t      mip_levels;           // Number of mip levels uploaded
    bool          valid;                // Has been uploaded
} vc_tex_slot_t;
```

### 7.4 Decoded Data Row Padding

**CRITICAL**: The decoded data in `texture_t.data[]` uses row stride of
`(256 >> lod)` pixels, NOT the actual texture width. For a 64x32 texture at
LOD 0, each row in the decode buffer is 256 pixels (1024 bytes) but only the
first 64 pixels (256 bytes) are valid.

For VkImage upload via `vkCmdCopyBufferToImage`, use `VkBufferImageCopy`:
```c
VkBufferImageCopy region = {
    .bufferOffset = staging_offset,
    .bufferRowLength = 256 >> lod,     // Source row stride in PIXELS
    .bufferImageHeight = 0,            // Tightly packed vertically
    .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, lod, 0, 1 },
    .imageOffset = { 0, 0, 0 },
    .imageExtent = { actual_width, actual_height, 1 }
};
```

The `bufferRowLength` parameter handles the padding correctly without needing
to repack the data.

### 7.5 VkImage Sizing

**Option A**: Create each VkImage at the exact LOD 0 dimensions for the texture.
Pros: Saves VRAM. Cons: Need to recreate VkImage when a cache slot gets reused
with a different-sized texture.

**Option B**: Always create 256x256 VkImages with 9 mip levels.
Pros: Never need to recreate. Cons: Wastes VRAM for small textures.

**Recommendation**: Option A -- create at exact size. Texture format changes
require VkImage recreation anyway (the VK format is always B8G8R8A8_UNORM but
the mip count and dimensions change). Destruction is cheap since we use VMA.

Total VRAM for worst case (128 slots at 256x256 with full mip chain):
`128 * 256*256*4 * 1.33` (mipmaps) = ~44 MB. Acceptable on all targets.

---

## 8. Descriptor Set Management

### 8.1 Layout

Per DESIGN.md, the descriptor set layout for textured rendering:
```
Set 0, Binding 0: combined image sampler (TMU0)
Set 0, Binding 1: combined image sampler (TMU1)
Set 0, Binding 2: combined image sampler (fog table -- future, Phase 6)
```

### 8.2 Per-Frame vs Per-Draw Descriptors

**Option A**: Per-frame descriptor pool, reset at frame start.
Each draw call that changes textures allocates a new descriptor set.

**Option B**: Use VK_KHR_push_descriptors (already listed in DESIGN.md as
a supported extension). This avoids descriptor pool management entirely --
descriptors are pushed inline with the command buffer.

**Recommendation**: Use push descriptors (`vkCmdPushDescriptorSetKHR`).
Already confirmed supported on MoltenVK and desktop. Avoids descriptor pool
sizing/reset complexity. Perfect for our use case where texture bindings
change frequently.

### 8.3 Sampler Management

Voodoo texture sampling parameters:
- **Filtering**: Nearest (point) or bilinear. Controlled by per-pixel LOD
  calculation in the software path. For VK, use VK_FILTER_NEAREST for Phase 4,
  add bilinear in Phase 5 (core pipeline).
- **Wrap modes**: Clamp (TEXTUREMODE_TCLAMPS/T) or repeat.
  Wrap = VK_SAMPLER_ADDRESS_MODE_REPEAT
  Clamp = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
- **Mirror**: LOD_TMIRROR_S/T.
  Mirror = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT

Possible sampler combinations: 3 S modes x 3 T modes x 2 filters = 18.
Pre-create all at init, index by runtime state. VkSampler objects are tiny.

---

## 9. Ring Command Design

### 9.1 VC_CMD_TEXTURE_UPLOAD

```c
// Ring payload:
struct {
    uint8_t  tmu;           // 0 or 1
    uint8_t  slot;          // 0-63 (index into texture_cache)
    uint16_t width;         // LOD 0 width
    uint16_t height;        // LOD 0 height
    uint8_t  lod_min;       // First LOD to upload
    uint8_t  lod_max;       // Last LOD to upload
    uint32_t *data;         // Pointer to malloc'd BGRA8 copy (GPU thread frees)
};
```

The FIFO thread mallocs a copy of the decoded data (or just passes a pointer to
the texture_cache[].data with appropriate ownership rules). The GPU thread:
1. Creates/recreates VkImage if dimensions changed
2. Transitions image to TRANSFER_DST_OPTIMAL
3. Copies data via staging buffer to VkImage (per-LOD regions)
4. Transitions image to SHADER_READ_ONLY_OPTIMAL
5. Frees the data buffer (if malloced copy)

**Optimization**: Instead of malloc/copy, the FIFO thread can pass the
`texture_cache[tmu][slot].data` pointer directly. The GPU thread must finish
the upload before the FIFO thread can evict that cache entry. The refcount
mechanism already prevents eviction while in-flight, so this is safe.

### 9.2 VC_CMD_TEXTURE_BIND

```c
// Ring payload:
struct {
    uint8_t  tmu;        // 0 or 1
    uint8_t  slot;       // Cache slot index (0-63)
    uint8_t  clamp_s;    // 1 if TEXTUREMODE_TCLAMPS
    uint8_t  clamp_t;    // 1 if TEXTUREMODE_TCLAMPT
    uint8_t  mirror_s;   // 1 if LOD_TMIRROR_S
    uint8_t  mirror_t;   // 1 if LOD_TMIRROR_T
};
```

Or, more simply: the sampler parameters can be derived from the textureMode and
tLOD values already in the push constants. The bind command just needs the slot
index. The GPU thread selects the appropriate pre-created sampler based on the
push constant values at draw time.

**Simplified VC_CMD_TEXTURE_BIND**:
```c
struct {
    uint8_t tmu;         // 0 or 1
    uint8_t slot;        // Cache slot index (0-63)
    uint16_t padding;
};
```

### 9.3 Alternative: Embed Texture Info in VC_CMD_TRIANGLE

Instead of separate bind commands, embed the texture slot indices directly in
the triangle command (extend VC_CMD_TRIANGLE payload):

```c
// Extended triangle payload:
struct {
    vc_push_constants_t pc;  // 64 bytes
    vc_vertex_t verts[3];    // 216 bytes
    uint8_t tex_slot[2];     // TMU0 and TMU1 slot indices (2 bytes)
    uint8_t tex_needs_upload[2]; // Whether GPU thread should check upload (2 bytes)
    uint32_t padding;        // Align to multiple of 8
};
```

**Recommendation**: Use the embedded approach. It avoids separate ring commands
and keeps the FIFO-to-GPU protocol simple. The GPU thread checks if the current
texture slots differ from the last draw call and updates descriptors accordingly.
Upload commands remain separate since they carry large payloads.

---

## 10. Implementation Recommendations

### 10.1 Phase 4 Scope

Based on the analysis, Phase 4 should implement:

1. **vc_texture.c/h**: VkImage pool (128 slots), staging buffer, upload logic
2. **Texture coordinate extraction in vid_voodoo_vk.c**: Reconstruct s/t/w from
   gradients for both TMUs
3. **Texture upload detection**: After voodoo_use_texture() returns, check if VK
   slot is stale and push VC_CMD_TEXTURE_UPLOAD
4. **Texture slot indices in triangle command**: Extend VC_CMD_TRIANGLE
5. **Descriptor set layout + push descriptors**: For sampler binding
6. **Sampler pool**: Pre-create all 18 sampler variants
7. **GPU thread texture handling**: Process upload commands, manage VkImage
   lifecycle, push descriptors at draw time

### 10.2 Texture Coordinate Extraction

Add to `voodoo_vk_extract_vertices()`:

```c
// Texture coordinates: 18.32 fixed-point -> float
#define VC_TEX_SCALE (1.0 / 4294967296.0)  /* 1.0 / (1 << 32) */

// TMU0
float s0A = (double)p->tmu[0].startS * VC_TEX_SCALE;
float t0A = (double)p->tmu[0].startT * VC_TEX_SCALE;
float w0A = (double)p->tmu[0].startW * VC_TEX_SCALE;

float s0B = s0A + ((double)p->tmu[0].dSdX * dx_ba + (double)p->tmu[0].dSdY * dy_ba) * VC_TEX_SCALE;
float t0B = t0A + ((double)p->tmu[0].dTdX * dx_ba + (double)p->tmu[0].dTdY * dy_ba) * VC_TEX_SCALE;
float w0B = w0A + ((double)p->tmu[0].dWdX * dx_ba + (double)p->tmu[0].dWdY * dy_ba) * VC_TEX_SCALE;

// ... same for C vertex and TMU1 ...
```

Note: S and T are in texel-space (0 to texture_width/height). The Voodoo HW
clamps/wraps after division by W. In the shader, we normalize to [0,1] by
dividing by texture dimensions, or we can pass texture dimensions as additional
push constant data (currently detail0/detail1 are unused -- could repurpose).

### 10.3 Depth and Perspective Extraction (Prerequisite)

Phase 4 also requires adding proper depth and perspective W extraction, which
are currently stubbed to 0.5/1.0. The Z gradient is `startZ/dZdX/dZdY` (20.12
fixed-point) and the W gradient is the main `startW/dWdX/dWdY` (18.32).

This is documented in `videocommon-plan/research/perspective-correction.md`.

### 10.4 Staging Buffer Strategy

For texture uploads, use a ring-style staging buffer:
- Size: 2 MB (enough for ~8 concurrent 256x256 BGRA8 textures with mipmaps)
- VMA allocation: HOST_VISIBLE | HOST_COHERENT, mapped persistently
- Write offset advances linearly, wraps around
- Fence tracking per upload to know when staging region is reusable

Or simpler: allocate a transient staging buffer per upload via VMA, free after
the copy command completes. VMA's buddy allocator handles fragmentation.

**Recommendation**: Use VMA transient allocations for Phase 4 simplicity.
Optimize to a ring staging buffer in Phase 8 (polish) if profiling shows it
matters.

### 10.5 Key Risks

1. **Texture upload latency**: Uploading textures via the ring adds latency.
   If the GPU thread is still uploading when the next triangle needs the texture,
   we stall. Mitigation: batch uploads before draws.

2. **Row stride mismatch**: The decoded data has 256-pixel row padding.
   Must use `bufferRowLength` correctly in VkBufferImageCopy. This is the most
   likely source of visual corruption if gotten wrong.

3. **Refcount synchronization**: The VK path must increment refcount_r[0]
   for every texture reference, even cache hits. Missing this causes the FIFO
   thread to evict textures that are still in-flight on the GPU.

4. **Palette/NCC changes**: When palette or NCC table changes, textures using
   those lookups are invalidated (flushed). The VK path must detect this and
   re-upload. The existing `palette_checksum` in the identity key handles this
   for palette formats. NCC table changes trigger a full cache flush.

---

## Appendix A: Texture Memory Sizes

| Voodoo Type    | TMUs | Tex Mem per TMU | Total Tex Mem |
|----------------|------|-----------------|---------------|
| Voodoo 1       | 1    | 1-4 MB          | 1-4 MB        |
| Voodoo SB50    | 2    | 1-4 MB          | 2-8 MB        |
| Voodoo 2       | 2    | 2-4 MB          | 4-8 MB        |
| Banshee        | 1    | Shared with FB  | N/A           |
| Voodoo 3       | 2*   | Shared with FB  | N/A           |

*Voodoo 3 has `dual_tmus=1` but both TMUs share the same physical memory
(Banshee architecture with unified memory).

## Appendix B: Format Decode Summary (for makergba Output)

All formats decode to uint32_t with layout: `0xAARRGGBB` when read as uint32_t
on little-endian, which is byte order B, G, R, A = VK_FORMAT_B8G8R8A8_UNORM.

| Format       | Source BPP | Has Alpha    | Uses Lookup Table    |
|--------------|-----------|--------------|----------------------|
| RGB332       | 8         | No (0xFF)    | rgb332[256]          |
| Y4I2Q2       | 8         | No (0xFF)    | ncc_lookup[tmu][sel] |
| A8           | 8         | Yes (full)   | None                 |
| I8           | 8         | No (0xFF)    | None                 |
| AI8          | 8         | Yes (4-bit)  | None                 |
| PAL8         | 8         | No (0xFF)    | palette[tmu]         |
| APAL8        | 8         | Yes (packed) | palette[tmu]         |
| ARGB8332     | 16        | Yes (full)   | rgb332[256]          |
| A8Y4I2Q2     | 16        | Yes (full)   | ncc_lookup[tmu][sel] |
| R5G6B5       | 16        | No (0xFF)    | rgb565[65536]        |
| ARGB1555     | 16        | Yes (1-bit)  | argb1555[65536]      |
| ARGB4444     | 16        | Yes (4-bit)  | argb4444[65536]      |
| A8I8         | 16        | Yes (full)   | None                 |
| APAL88       | 16        | Yes (full)   | palette[tmu]         |

## Appendix C: Source File Reference

| File | Purpose |
|------|---------|
| `src/video/vid_voodoo_texture.c` | Texture decode, cache lookup, invalidation |
| `src/include/86box/vid_voodoo_texture.h` | texture_offset[] array, function declarations |
| `src/include/86box/vid_voodoo_common.h` | texture_t, voodoo_params_t, TEX_CACHE_MAX |
| `src/include/86box/vid_voodoo_regs.h` | TEX_* format enum, TEXTUREMODE_*, LOD_* flags |
| `src/video/vid_voodoo.c` | Cache init/alloc (line 1219+), lookup table init (line 1265+) |
| `src/video/vid_voodoo_render.c` | voodoo_use_texture() call sites (line 1889) |
| `src/video/vid_voodoo_vk.c` | VK bridge (needs texture extension for Phase 4) |
| `src/video/videocommon/vc_pipeline.h` | vc_vertex_t, vc_push_constants_t |
| `videocommon-plan/DESIGN.md` | Section 7.7 (texture management), 7.8 (vertex format) |
