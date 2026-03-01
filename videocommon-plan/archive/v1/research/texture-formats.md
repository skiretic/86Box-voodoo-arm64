# Voodoo Texture Format Conversion Research

## Summary

The existing CPU-side texture cache (`voodoo_use_texture()` in `src/video/vid_voodoo_texture.c`) already decodes **all 15 Voodoo texture formats** into a uniform **BGRA8** (8 bits per channel, 32 bits per texel) representation stored as `uint32_t`. The Vulkan backend can upload this decoded data directly with no additional format conversion.

---

## Decoded Data Format

### Memory Layout (uint32_t)

The `makergba` macro at line 238 of `vid_voodoo_texture.c`:

```c
#define makergba(r, g, b, a) ((b) | ((g) << 8) | ((r) << 16) | ((a) << 24))
```

This produces `uint32_t` values with the bit layout `0xAARRGGBB`.

### Byte Order on Little-Endian (x86, ARM64)

The `rgba_u` union (defined in `vid_voodoo_common.h:58-67`):

```c
typedef union rgba_u {
    struct {
        uint8_t b;  // byte[0]
        uint8_t g;  // byte[1]
        uint8_t r;  // byte[2]
        uint8_t a;  // byte[3]
    } rgba;
    uint32_t u;
} rgba_u;
```

On little-endian systems, `makergba(R,G,B,A)` produces the same `uint32_t` as setting `rgba.b=B, rgba.g=G, rgba.r=R, rgba.a=A`. The byte order in memory is **B, G, R, A** (BGRA).

### Software Renderer Extraction

The software renderer's `tex_read()` (in `vid_voodoo_render.c:240-245`) reads the cache entries as `uint32_t` and extracts channels:

```c
dat = state->tex[tmu][state->lod][index];
state->tex_b[tmu] = dat & 0xff;          // byte 0 = B
state->tex_g[tmu] = (dat >> 8) & 0xff;   // byte 1 = G
state->tex_r[tmu] = (dat >> 16) & 0xff;  // byte 2 = R
state->tex_a[tmu] = (dat >> 24) & 0xff;  // byte 3 = A
```

The bilinear filter path (`tex_read_4()` at line 252) casts entries through `rgba_u` and accesses `.rgba.r`, `.rgba.g`, `.rgba.b`, `.rgba.a` directly. This confirms the byte layout is fully consistent.

---

## All Voodoo Texture Formats

| Enum | Value | Name | Bits/Texel | Source | Alpha | Decoding |
|------|-------|------|-----------|--------|-------|----------|
| `TEX_RGB332` | 0x0 | RGB 3:3:2 | 8 | `rgb332[]` LUT | A=0xFF | 3/3/2 bits expanded to 8/8/8 by bit replication |
| `TEX_Y4I2Q2` | 0x1 | NCC YIQ | 8 | `ncc_lookup[]` | A=0xFF | NCC palette, 4-bit Y + 2-bit I + 2-bit Q |
| `TEX_A8` | 0x2 | Alpha 8 | 8 | Direct | A=texel | All channels set to texel value (RGBA = D,D,D,D) |
| `TEX_I8` | 0x3 | Intensity 8 | 8 | Direct | A=0xFF | RGB set to texel value, alpha opaque |
| `TEX_AI8` | 0x4 | Alpha+Intensity 4:4 | 8 | Inline decode | High nibble | 4-bit A and 4-bit I, each expanded to 8-bit |
| `TEX_PAL8` | 0x5 | Palette 8 | 8 | `palette[]` | A=0xFF | 256-entry palette lookup, alpha forced opaque |
| `TEX_APAL8` | 0x6 | Alpha Palette 8 | 8 | `palette[]` | From palette | Palette stores ARGB6666 packed in 24 RGB bits |
| `TEX_ARGB8332` | 0x8 | ARGB 8:3:3:2 | 16 | `rgb332[]` LUT + high byte | High byte | Low 8 bits = RGB332, high 8 bits = alpha |
| `TEX_A8Y4I2Q2` | 0x9 | Alpha + NCC YIQ | 16 | `ncc_lookup[]` + high byte | High byte | Low 8 bits = NCC lookup, high 8 bits = alpha |
| `TEX_R5G6B5` | 0xA | RGB 5:6:5 | 16 | `rgb565[]` LUT | A=0xFF | 5/6/5 bits expanded to 8/8/8 by bit replication |
| `TEX_ARGB1555` | 0xB | ARGB 1:5:5:5 | 16 | `argb1555[]` LUT | 1-bit (0 or 0xFF) | 5-bit channels expanded to 8, 1-bit alpha |
| `TEX_ARGB4444` | 0xC | ARGB 4:4:4:4 | 16 | `argb4444[]` LUT | 4-bit expanded | 4-bit channels expanded to 8 by nibble replication |
| `TEX_A8I8` | 0xD | Alpha 8 + Intensity 8 | 16 | Inline decode | High byte | Low byte = intensity (all RGB), high byte = alpha |
| `TEX_APAL88` | 0xE | Alpha 8 + Palette 8 | 16 | `palette[]` + high byte | High byte | Low byte = palette index, high byte = alpha |

**Key observation**: Formats 0x0-0x6 are 8-bit (1 byte per texel in VRAM). Formats 0x8-0xE are 16-bit (2 bytes per texel). The format enum bit 3 (`& 8`) distinguishes them: `if (params->tformat[tmu] & 8)` selects 16-bit path for address/offset calculations.

---

## NCC Palette Decoding (Y4I2Q2)

### NCC Table Structure

Each TMU has 2 NCC tables (selected by `TEXTUREMODE_NCC_SEL` bit 5). Each table contains:

```c
struct {
    uint32_t y[4];  // 16 Y values packed 4 per uint32 (8 bits each)
    uint32_t i[4];  // 4 I vectors, each with 9-bit signed R, G, B components
    uint32_t q[4];  // 4 Q vectors, each with 9-bit signed R, G, B components
} nccTable[2][2];  // [tmu][table]
```

Stored in `voodoo->nccTable[tmu][tbl]` and programmed via registers `SST_nccTable0_Y0..Q3` and `SST_nccTable1_Y0..Q3`.

### NCC Lookup Table

`voodoo_update_ncc()` in `vid_voodoo_display.c:60-102` pre-computes a 256-entry lookup table:

```c
rgba_u ncc_lookup[2][2][256];  // [tmu][table][index]
```

For each 8-bit texel value:
- **Y index** = bits [7:4] (selects one of 16 Y values from 4 packed uint32s)
- **I index** = bits [3:2] (selects one of 4 I vectors)
- **Q index** = bits [1:0] (selects one of 4 Q vectors)

Each I and Q vector has three 9-bit signed components (R, G, B), packed in a uint32:
- `i_r = bits [26:18]`, sign-extended from 9-bit
- `i_g = bits [17:9]`, sign-extended from 9-bit
- `i_b = bits [8:0]`, sign-extended from 9-bit

Final color: `R = CLAMP(Y + i_r + q_r)`, `G = CLAMP(Y + i_g + q_g)`, `B = CLAMP(Y + i_b + q_b)`, `A = 0xFF`

The CLAMP macro clamps to [0, 255].

### NCC Usage in Texture Cache

In `voodoo_use_texture()`, NCC formats use the pre-computed lookup directly:

```c
case TEX_Y4I2Q2:
    pal = voodoo->ncc_lookup[tmu][(params->textureMode[tmu] & TEXTUREMODE_NCC_SEL) ? 1 : 0];
    // ...
    base[x] = makergba(pal[dat].rgba.r, pal[dat].rgba.g, pal[dat].rgba.b, 0xff);
```

The 16-bit variant `TEX_A8Y4I2Q2` uses the same NCC lookup for the low byte but takes alpha from the high byte:

```c
case TEX_A8Y4I2Q2:
    base[x] = makergba(pal[dat & 0xff].rgba.r, pal[dat & 0xff].rgba.g, pal[dat & 0xff].rgba.b, dat >> 8);
```

### NCC Invalidation

NCC tables are updated via `voodoo_update_ncc()` which is called from register writes (`vid_voodoo_reg.c:332-535`) and triangle setup (`vid_voodoo_setup.c:234-236`). The `ncc_dirty[tmu]` flag tracks when NCC tables change.

**Important for Vulkan path**: When NCC tables change, any cached textures using NCC formats become stale. Since the texture cache keys on `base + tLOD + palette_checksum`, and NCC textures always have `palette_checksum = 0`, NCC table changes will NOT automatically invalidate the texture cache. The NCC lookup is baked into the decoded cache data at upload time. This means the Vulkan backend must track NCC table changes and invalidate NCC-format textures when they change.

---

## Texture Cache Structure

### Per-Entry (texture_t in vid_voodoo_common.h:244-254)

```c
typedef struct texture_t {
    uint32_t   base;            // Base address in texture VRAM
    uint32_t   tLOD;            // LOD configuration (masked to 0xf00fff)
    ATOMIC_INT refcount;        // Reference count (incremented on use)
    ATOMIC_INT refcount_r[4];   // Per-render-thread reference count
    int        is16;            // 1 if 16-bit source format (format & 8)
    uint32_t   palette_checksum; // XOR of all 256 palette entries (for palette formats)
    uint32_t   addr_start[4];  // VRAM address ranges covered (4 LOD groups)
    uint32_t   addr_end[4];
    uint32_t  *data;            // Decoded BGRA8 texel data (heap-allocated)
} texture_t;
```

### Organization

- **2 TMUs**: `voodoo->texture_cache[2][TEX_CACHE_MAX]`
- **64 entries per TMU**: `TEX_CACHE_MAX = 64`
- **Data allocation per entry**: `(256*256 + 256*256 + 128*128 + ... + 2*2) * 4 = 611,664 bytes` (~597 KB)
  - Note: the allocation has 256*256 duplicated (LOD 0 uses first 256*256, subsequent LODs use the rest)
  - Total allocation covers LOD 0 (256x256) through LOD 8 (1x1) using `texture_offset[]` table
- **Total memory per TMU**: 64 entries * 597 KB = ~37.3 MB
- **Total for dual-TMU**: ~74.6 MB of decoded texture cache

### LOD Mip Levels

The `texture_offset[]` array (in `vid_voodoo_texture.h:19-31`) maps LOD index to offset within the data buffer:

| LOD | Size | Offset (texels) |
|-----|------|-----------------|
| 0 | 256x256 | 0 |
| 1 | 128x128 | 65,536 |
| 2 | 64x64 | 81,920 |
| 3 | 32x32 | 86,016 |
| 4 | 16x16 | 87,040 |
| 5 | 8x8 | 87,296 |
| 6 | 4x4 | 87,360 |
| 7 | 2x2 | 87,376 |
| 8 | 1x1 | 87,380 |

The software renderer accesses decoded texels as:
```c
state->tex[tmu][lod][s + (t << shift)]
```
where `shift = 8 - tex_lod` and the pointer is `&cache.data[texture_offset[lod]]`.

### Cache Lookup and Eviction

**Lookup** (`voodoo_use_texture()` lines 273-279): Linear scan of all 64 entries, matching on:
- `base` (texture VRAM base address)
- `tLOD & 0xf00fff` (LOD configuration bits)
- `palette_checksum` (for palette-based formats only)

**Eviction** (lines 282-293): Round-robin (`texture_last_removed`) seeking an entry where `refcount == refcount_r[0]` (and `refcount_r[1]` for dual render threads), meaning no render thread is currently using it. Spins with `voodoo_wait_for_render_thread_idle()` if all entries are in use.

### Invalidation

**Dirty tracking** uses `texture_present[2][16384]` bitmap (1 bit per 1KB region, since `TEX_DIRTY_SHIFT = 10`).

**`voodoo_tex_writel()`** (line 594): On each texture VRAM write:
1. Computes final address in texture memory
2. Checks `texture_present[tmu][addr >> TEX_DIRTY_SHIFT]`
3. If dirty region is present, calls `flush_texture_cache()` which:
   - Scans all 64 entries
   - Invalidates (sets `base = -1`) any entry whose address range overlaps the dirty address
   - Waits for render threads to release if entry has outstanding references
   - Rebuilds `texture_present[]` bitmap from remaining valid entries

---

## Vulkan Upload Format Recommendation

### Primary Recommendation: VK_FORMAT_B8G8R8A8_UNORM

The decoded cache data has byte order `[B, G, R, A]` in memory on little-endian. The correct Vulkan upload approach is to copy the decoded data into a staging buffer, then transfer to a `VkImage` created with `VK_FORMAT_B8G8R8A8_UNORM`:

```c
/* Copy decoded texel data into staging buffer */
memcpy(staging_mapped, &cache->data[texture_offset[lod]], width * height * 4);

/* Transfer from staging buffer to VkImage */
VkBufferImageCopy region = {
    .bufferOffset      = staging_offset,
    .bufferRowLength   = 0,  /* tightly packed (or set to row_stride if padded) */
    .bufferImageHeight = 0,  /* tightly packed */
    .imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, lod, 0, 1 },
    .imageOffset       = { 0, 0, 0 },
    .imageExtent       = { width, height, 1 },
};
vkCmdCopyBufferToImage(cmd, staging_buf, texture_image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
```

- **VkImage format**: `VK_FORMAT_B8G8R8A8_UNORM` -- byte order B, G, R, A matches the decoded cache data exactly
- **Staging buffer**: Persistently mapped `VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT` buffer (VMA `VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT`)
- **No format conversion needed**: The `makergba` byte layout is an exact match for `VK_FORMAT_B8G8R8A8_UNORM`

### Why VK_FORMAT_B8G8R8A8_UNORM

1. **Exact byte match**: `VK_FORMAT_B8G8R8A8_UNORM` defines the byte order as `[B, G, R, A]` at byte offsets `[0, 1, 2, 3]`. This is identical to the `makergba` output on little-endian. No data conversion or swizzling is needed.

2. **Mandatory format support**: `VK_FORMAT_B8G8R8A8_UNORM` is required by the Vulkan 1.2 specification to support `VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT` on all conformant implementations. It is universally available.

3. **Cross-platform correctness**: Vulkan format definitions specify byte order directly (not uint32 bit packing), so the mapping is endian-agnostic. Byte 0 is always Blue, byte 1 is Green, etc.

4. **Performance**: BGRA8 is the GPU's native swapchain/surface format on most hardware (Windows, macOS, many Linux drivers). Using it for textures avoids internal swizzle overhead.

### Alternative: VK_FORMAT_R8G8B8A8_UNORM

`VK_FORMAT_R8G8B8A8_UNORM` defines byte order `[R, G, B, A]` which does NOT match the decoded data. Using this format would swap red and blue channels. Do not use without first swizzling the data. The only scenario where `VK_FORMAT_R8G8B8A8_UNORM` would be appropriate is if the `makergba` macro were changed to produce RGBA byte order, which is not planned.

### Avoid

- **`VK_FORMAT_R8G8B8A8_UNORM` without swizzle**: Byte order `[R,G,B,A]` but memory has `[B,G,R,A]`. Red and blue channels would be swapped.
- **`VK_FORMAT_A8B8G8R8_UNORM_PACK32`**: This is a packed 32-bit format that defines channels by bit position within a uint32, not by byte offset. On little-endian it happens to produce the same byte order as `VK_FORMAT_R8G8B8A8_UNORM`, so it would also swap R/B.

### Endianness Concerns

86Box targets **x86-64 and ARM64 only**, both of which are little-endian (ARM64 in its default LE configuration). The `makergba` macro and `rgba_u` union are only consistent on little-endian systems. There are no big-endian concerns for the target platforms.

Vulkan format definitions are byte-order-based (not uint32-packing-based), so `VK_FORMAT_B8G8R8A8_UNORM` means "byte 0 = B" on all platforms regardless of endianness. This is a non-issue for the target platforms.

---

## Vulkan Texture Object Strategy

### Recommendation: One VkImage per Cache Entry

For maximum simplicity and compatibility with the existing cache structure:

1. **Allocate a VkImage per cache entry** (or lazily on first use) with `VkImageCreateInfo.mipLevels` set to the number of active LODs
2. **Upload all active LODs as mip levels** via `vkCmdCopyBufferToImage()` with per-level `VkBufferImageCopy` regions
3. **On cache invalidation**: destroy the VkImage and VkImageView (deferred until the GPU is no longer referencing them, e.g., after the current frame's fence signals)
4. **On cache re-population** (new `voodoo_use_texture()` decode): create a new VkImage and re-upload via staging buffer copy

Each VkImage has an associated `VkImageView` for shader sampling. Both must be tracked together per cache entry.

### Texture Dimensions

Voodoo textures are always power-of-two, max 256x256, with aspect ratios controlled by the LOD register:
- Aspect 0: square (256x256, 128x128, etc.)
- Aspect 1: 2:1 (256x128, 128x64, etc.)
- Aspect 2: 4:1 (256x64, 128x32, etc.)
- Aspect 3: 8:1 (256x32, 128x16, etc.)

The `LOD_S_IS_WIDER` flag determines whether S (width) or T (height) is the larger dimension.

### Mip Level Upload

The decoded data buffer stores all mip levels contiguously. For Vulkan upload, copy each level into the staging buffer and record a `VkBufferImageCopy` per level:

```c
VkBufferImageCopy regions[9]; /* max 9 LOD levels (256x256 down to 1x1) */
uint32_t region_count = 0;
VkDeviceSize staging_offset = 0;

for (int lod = lod_min; lod <= lod_max; lod++) {
    int width      = params->tex_w_mask[tmu][lod] + 1;
    int height     = params->tex_h_mask[tmu][lod] + 1;
    int row_stride = 1 << (8 - params->tex_lod[tmu][lod]); /* texels per row in decoded data */

    regions[region_count] = (VkBufferImageCopy){
        .bufferOffset      = staging_offset,
        .bufferRowLength   = row_stride,  /* handles non-square aspect ratio padding */
        .bufferImageHeight = 0,           /* tightly packed rows */
        .imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, lod - lod_min, 0, 1 },
        .imageOffset       = { 0, 0, 0 },
        .imageExtent       = { width, height, 1 },
    };

    /* Copy decoded data to staging buffer (full rows including padding) */
    memcpy(staging_mapped + staging_offset,
           &cache->data[texture_offset[lod]],
           row_stride * height * sizeof(uint32_t));
    staging_offset += row_stride * height * sizeof(uint32_t);
    region_count++;
}

vkCmdCopyBufferToImage(cmd, staging_buf, texture_image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       region_count, regions);
```

**Important**: The decoded data uses a fixed stride of `(1 << shift)` texels per row, which may be wider than the actual texture width for non-square aspect ratios. When uploading to Vulkan, set `VkBufferImageCopy.bufferRowLength` to `(1 << shift)` so Vulkan reads the correct row pitch from the staging buffer. This field controls how many texels per row Vulkan reads from the buffer, analogous to a row-length unpack parameter.

---

## Key Files Referenced

| File | Relevance |
|------|-----------|
| `src/video/vid_voodoo_texture.c` | Texture cache decode (all formats), cache management, write handler |
| `src/include/86box/vid_voodoo_texture.h` | `texture_offset[]` table, function declarations |
| `src/include/86box/vid_voodoo_common.h` | `rgba_u`, `rgba8_t`, `texture_t`, `voodoo_t` structures |
| `src/include/86box/vid_voodoo_regs.h` | `TEX_*` format enums, mode bits |
| `src/video/vid_voodoo_display.c:60-102` | `voodoo_update_ncc()` - NCC palette decode |
| `src/video/vid_voodoo_render.c:217-292` | `tex_read()`, `tex_read_4()` - software renderer texel fetch |
| `src/video/vid_voodoo.c:1264-1309` | Lookup table initialization (rgb332, rgb565, etc.) |
| `src/video/vid_voodoo.c:1217-1226` | Texture cache data allocation |

---

## Potential Issues for Vulkan Backend

1. **Row stride mismatch**: Decoded data uses power-of-2 row stride (always 256 texels for LOD 0) even for non-square textures. Must set `VkBufferImageCopy.bufferRowLength` to the padded row stride when uploading from the staging buffer.

2. **NCC invalidation gap**: The texture cache does not key on NCC table contents. When NCC tables change, cached NCC-format textures are stale but not automatically invalidated. The Vulkan backend must either:
   - Track NCC table versions and invalidate on change, or
   - Include NCC table state in the cache key somehow

3. **Palette invalidation**: Handled correctly by `palette_checksum`. When the palette changes, the checksum changes, and texture lookups will miss the cache and re-decode. However, the existing cache entries with stale palette data remain until evicted. The Vulkan backend should invalidate VkImage/VkImageView objects corresponding to stale cache entries.

4. **Cache entry reuse**: Cache entries are evicted by round-robin. The Vulkan backend must sync VkImage destruction with the cache entry lifecycle. Since `vkDestroyImage`/`vkDestroyImageView` must not be called while the GPU is still referencing the resource, destruction should be deferred until the frame fence signals (e.g., via a per-frame deletion queue).

5. **Thread safety**: `voodoo_use_texture()` is called from the FIFO thread context. Vulkan staging buffer copies and `vkCmdCopyBufferToImage()` calls must happen on the render thread that owns the command buffer. The Vulkan backend needs a mechanism to detect cache changes (dirty flag, generation counter) and issue uploads on the render thread side.
