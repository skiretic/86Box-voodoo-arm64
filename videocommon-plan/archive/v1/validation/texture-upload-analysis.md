# Texture Upload Infrastructure Analysis

## Date: 2026-02-27

## Executive Summary

The VideoCommon texture infrastructure is **fully built** at the Vulkan level but has **zero callers**. All the pieces exist: `vc_texture_upload()`, `vc_texture_bind()`, sampler cache, descriptor set management, staging buffers, layout transitions. What's missing is the **bridge code** in `vid_voodoo_vk.c` that:

1. Calls `voodoo_use_texture()` to decode Voodoo textures into the CPU-side cache
2. Uploads that decoded BGRA8 data to Vulkan via the ring
3. Binds the real texture slot instead of the placeholder

The `VC_CMD_TEXTURE_UPLOAD` command type exists in the ring but is a no-op placeholder.

---

## 1. vc_texture API Documentation

### 1.1 `vc_texture_upload()`

**File**: `src/video/videocommon/vc_texture.c` line 586
**Header**: `src/video/videocommon/vc_texture.h` line 155

```c
int vc_texture_upload(vc_texture_t *tex, vc_context_t *ctx,
                      int slot,
                      uint32_t width, uint32_t height,
                      uint32_t mip_levels,
                      const void *pixels,
                      uint32_t row_stride);
```

**Parameters**:
- `slot`: Pool index 0..127, or -1 for auto-allocate (finds first free)
- `width`, `height`: Base level dimensions in texels
- `mip_levels`: Total mip levels (1 = no mipmaps, max 9)
- `pixels`: BGRA8 pixel data (`uint32_t` per texel), `width * height * 4` bytes
- `row_stride`: Texels per row in source (0 = tightly packed = width)

**Behavior**:
1. If slot occupied, destroys old VkImage
2. Creates new VkImage (BGRA8_UNORM, TRANSFER_DST | SAMPLED)
3. Copies pixels to staging buffer (256KB max)
4. Records command buffer: UNDEFINED -> TRANSFER_DST, copy, TRANSFER_DST -> SHADER_READ_ONLY
5. Submits to queue and **waits synchronously** (vkWaitForFences)
6. Creates VkImageView
7. Returns slot index on success, -1 on failure

**Constraint**: Max texture size is 256x256 (256KB staging buffer = 256*256*4). This matches Voodoo hardware limits.

**Thread safety**: Must be called from the render thread (owns the Vulkan device). The staging buffer, transfer command buffer, and transfer fence are all render-thread-owned resources.

### 1.2 `vc_texture_upload_mip()`

**File**: `src/video/videocommon/vc_texture.c` line 731

```c
int vc_texture_upload_mip(vc_texture_t *tex, vc_context_t *ctx,
                          int slot, uint32_t mip_level,
                          uint32_t width, uint32_t height,
                          const void *pixels,
                          uint32_t row_stride);
```

Uploads a single mip level to an existing slot without recreating the VkImage. Transitions just that mip level: SHADER_READ_ONLY -> TRANSFER_DST -> SHADER_READ_ONLY.

### 1.3 `vc_texture_bind()`

**File**: `src/video/videocommon/vc_texture.c` line 936

```c
VkDescriptorSet vc_texture_bind(vc_texture_t *tex, vc_context_t *ctx,
                                VkDescriptorSetLayout desc_layout,
                                int tmu0_slot, VkSampler tmu0_sampler,
                                int tmu1_slot, VkSampler tmu1_sampler);
```

**Behavior**:
1. Allocates a descriptor set from the pool
2. If `tmu0_slot` is -1 or invalid: uses placeholder view + sampler
3. If `tmu1_slot` is -1 or invalid: uses placeholder view + sampler
4. Fog table always uses its own view + sampler (binding 2)
5. Writes 3 `VkWriteDescriptorSet` entries (bindings 0, 1, 2)
6. Returns the allocated descriptor set

**Descriptor layout** (from `vc_pipeline.c` line 37):
- Binding 0: TMU0 combined image sampler (fragment stage)
- Binding 1: TMU1 combined image sampler (fragment stage)
- Binding 2: Fog table combined image sampler (fragment stage)

### 1.4 `vc_texture_invalidate()`

**File**: `src/video/videocommon/vc_texture.c` line 882

```c
void vc_texture_invalidate(vc_texture_t *tex, int slot);
```

Simply bumps `entries[slot].generation++`. Does NOT destroy or re-upload. The caller is expected to compare generations to detect staleness.

### 1.5 `vc_texture_get_sampler()`

**File**: `src/video/videocommon/vc_texture.c` line 897

```c
VkSampler vc_texture_get_sampler(vc_texture_t *tex, vc_context_t *ctx,
                                 const vc_sampler_key_t *key);
```

Cache of up to 8 samplers. Key fields: min_filter, mag_filter, mip_mode, addr_u, addr_v. Linear scan lookup. Creates VkSampler on cache miss.

### 1.6 `vc_texture_reset_descriptors()`

**File**: `src/video/videocommon/vc_texture.c` line 1006

Called at frame boundaries (in `vc_begin_frame()` in vc_thread.c). Resets the entire descriptor pool so all 64 sets can be reallocated. This means descriptor sets are **per-frame transient** -- valid only within a single frame.

### 1.7 Texture Pool

- **Pool size**: 128 entries (`VC_TEX_POOL_SIZE`)
- **Entry struct** (`vc_tex_entry_t`): VkImage, VmaAllocation, VkImageView, width, height, mip_levels, generation, in_use
- **Placeholder**: 1x1 white BGRA8 texture, always available at `tex->placeholder`
- **Fog table**: 64x1 R8G8_UNORM image at `tex->fog_table`
- **Staging buffer**: 256KB, HOST_VISIBLE + HOST_COHERENT, persistently mapped

---

## 2. Ring Command Structure for Texture Upload

### Current State: PLACEHOLDER

**File**: `src/video/videocommon/vc_thread.h` line 39

```c
VC_CMD_TEXTURE_UPLOAD, /* (Placeholder) Upload texture data. */
```

The `vc_command_t` union currently has **no payload** for texture upload:

```c
typedef struct vc_command {
    vc_cmd_type_t type;
    union {
        vc_cmd_triangle_t       triangle;       /* 3 vertices = 168 bytes */
        vc_cmd_push_constants_t push_constants; /* 64 bytes */
        vc_cmd_clear_t          clear;          /* 24 bytes */
    };
} vc_command_t;
```

The dispatch in `vc_thread.c` line 269:

```c
case VC_CMD_TEXTURE_UPLOAD:
    /* Placeholder -- Phase 3 will implement. */
    break;
```

### Size Constraint

The `vc_command_t` struct size is dominated by `vc_cmd_triangle_t` (168 bytes = 3 * 56-byte vertices). The ring has 16384 entries, so the total ring size is `16384 * sizeof(vc_command_t)`. Adding a large texture payload would bloat the ring.

**Texture data cannot go through the ring directly**. A 256x256 BGRA8 texture is 256KB -- this would not fit in a command entry. The approach must be one of:

1. **Out-of-band data pointer**: The command contains a pointer to texture data (but the producer must ensure the data is valid when the consumer reads it)
2. **Pre-uploaded to staging**: The producer copies data to a shared staging area, the command just triggers the GPU upload
3. **Direct call**: The texture upload happens synchronously on the FIFO thread via a mutex-protected path (simpler but blocks)

---

## 3. Current State of `vid_voodoo_vk.c`

**File**: `src/video/vid_voodoo_vk.c`

### What Exists

1. **`vc_push_constants_t`** struct (64 bytes): Contains all 16 Voodoo register fields including `textureMode0` and `textureMode1`. VERIFIED CORRECT.

2. **`vc_push_constants_update()`**: Extracts all relevant register state from `voodoo_params_t` into push constants. Includes textureMode0/1 extraction from `params->textureMode[0]` and `params->textureMode[1]`.

3. **`vc_voodoo_submit_triangle()`**: Full vertex extraction from gradients:
   - Positions (12.4 -> float)
   - Colors (12.12 -> [0,1])
   - Depth (20.12 -> [0,1])
   - TMU0 texture coordinates (18.32 S/W, T/W, W)
   - Push constant deduplication (static `last_pc` comparison)
   - Submits via `vc_submit_triangle()` and `vc_push_constants()`

4. **TMU1 coordinates**: Hardcoded to zeros (`s1A = t1A = w1A = 0.0f`)

5. **`vc_voodoo_swap_buffers()`** and **`vc_voodoo_sync()`**: Wrappers that call the public API.

### What's Missing

1. **No `voodoo_use_texture()` call**: The GPU path in `voodoo_queue_triangle()` returns immediately after `vc_voodoo_submit_triangle()`, bypassing the `voodoo_use_texture()` calls that happen on lines 1884-1886 for the software path. This means:
   - Voodoo texture data is never decoded from raw VRAM into the BGRA8 cache
   - Even if it were decoded, nothing uploads it to Vulkan

2. **No texture slot tracking**: No mapping from Voodoo `texture_cache[tmu][entry]` to VideoCommon `vc_tex_entry_t` pool slots.

3. **No sampler creation**: No code to create samplers based on `textureMode` filter/clamp settings.

4. **No descriptor set binding per-batch**: The render thread binds placeholder descriptors once at render pass begin and never updates them.

5. **No fog table upload**: `params->fogTable[64]` data is never uploaded to the fog VkImage.

6. **TMU1 coordinates not reconstructed**: All zeros instead of actual gradient reconstruction.

---

## 4. How the Software Renderer Uses Textures

Understanding the existing flow is critical for wiring up the Vulkan path.

### Call Flow (Software Path)

```
voodoo_queue_triangle(voodoo, params)
  -> voodoo_use_texture(voodoo, params, 0)    // line 1884
  -> voodoo_use_texture(voodoo, params, 1)    // line 1886 (if dual_tmus)
  -> memcpy(params_new, params, sizeof(...))  // copies to ring buffer
  -> wake render thread
```

### `voodoo_use_texture()` (vid_voodoo_texture.c:241)

1. Computes a cache key from `texBaseAddr`, `tLOD`, and `palette_checksum`
2. Searches `texture_cache[tmu][0..63]` for a matching entry
3. If found: sets `params->tex_entry[tmu] = c`, bumps refcount, returns
4. If not found:
   - Evicts an old entry (refcount == refcount_r)
   - Decodes raw VRAM texture data into BGRA8 `uint32_t` array
   - Handles all Voodoo texture formats: RGB332, Y4I2Q2, A8, I8, AI44, PAL8, APAL8, APAL88, ARGB8332, RGB565, ARGB1555, ARGB4444, AI88, ARGB8888
   - Stores result in `voodoo->texture_cache[tmu][c].data[]`
   - Sets `params->tex_entry[tmu] = c`

### Texture Data Layout

Each `texture_cache[tmu][c].data` is a flat `uint32_t` array, allocated as:
```
256*256 + 128*128 + 64*64 + 32*32 + 16*16 + 8*8 + 4*4 + 2*2 = total texels
```
(Plus an extra 256*256 at the start -- the allocation is doubled for LOD 0.)

The `texture_offset[lod]` array provides byte offsets into this flat buffer for each LOD level:
- LOD 0: offset 0, size 256*256
- LOD 1: offset 256*256, size 128*128
- ...
- LOD 8: offset sum, size 1*1

In the render thread, textures are accessed as:
```c
state->tex[tmu][lod] = &voodoo->texture_cache[tmu][params->tex_entry[tmu]].data[texture_offset[lod]];
```

### Key Insight: Pre-decoded BGRA8

The Voodoo texture cache stores **pre-decoded BGRA8** data (uint32_t per texel). This is exactly what `vc_texture_upload()` expects (`VK_FORMAT_B8G8R8A8_UNORM`). No format conversion is needed on the Vulkan side.

---

## 5. Concrete Plan for Wiring Texture Upload

### Architecture Decision: Direct Upload vs Ring Command

**Recommended: Direct upload from FIFO thread with synchronization.**

The FIFO thread already calls `voodoo_use_texture()` before submitting triangles in the software path. We should:

1. Call `voodoo_use_texture()` in `vc_voodoo_submit_triangle()` to decode textures
2. Upload the decoded data to Vulkan **through the ring** using `VC_CMD_TEXTURE_UPLOAD`
3. The ring command carries: slot ID, width, height, mip_levels, and a **pointer** to the decoded data

The pointer-based approach works because:
- The `texture_cache[tmu][entry].data` buffer is stable (not freed until another texture evicts it)
- The render thread processes commands in order, so the upload command will be processed before the triangle that uses the texture
- The FIFO thread holds a refcount preventing eviction

### Step-by-Step Implementation Plan

#### Step 1: Add texture upload ring command payload

In `vc_thread.h`, add a new payload struct:

```c
typedef struct vc_cmd_texture_upload {
    int         slot;        /* VC texture pool slot to upload into. */
    uint32_t    width;       /* Base level width. */
    uint32_t    height;      /* Base level height. */
    uint32_t    mip_levels;  /* Number of mip levels. */
    const void *pixels;      /* Pointer to BGRA8 data (stable until consumed). */
    uint32_t    row_stride;  /* Texels per row (0 = tightly packed). */
} vc_cmd_texture_upload_t;
```

Add it to the `vc_command_t` union.

#### Step 2: Add texture bind ring command

Need a command to update descriptor sets when texture bindings change:

```c
typedef enum {
    ...
    VC_CMD_TEXTURE_BIND,   /* Bind texture slots to descriptor set. */
} vc_cmd_type_t;

typedef struct vc_cmd_texture_bind {
    int tmu0_slot;           /* VC pool slot for TMU0 (-1 = placeholder). */
    int tmu1_slot;           /* VC pool slot for TMU1 (-1 = placeholder). */
    uint8_t tmu0_min_filter; /* 0=nearest, 1=linear */
    uint8_t tmu0_mag_filter;
    uint8_t tmu0_mip_mode;   /* 0=nearest, 1=linear, 2=none */
    uint8_t tmu0_clamp_s;    /* 0=repeat, 1=clamp */
    uint8_t tmu0_clamp_t;
    uint8_t tmu1_min_filter;
    uint8_t tmu1_mag_filter;
    uint8_t tmu1_mip_mode;
    uint8_t tmu1_clamp_s;
    uint8_t tmu1_clamp_t;
    uint8_t pad[2];
} vc_cmd_texture_bind_t;
```

#### Step 3: Implement dispatch handlers in `vc_thread.c`

```c
case VC_CMD_TEXTURE_UPLOAD:
    vc_texture_upload(&thread->ctx->texture, thread->ctx,
                      cmd->texture_upload.slot,
                      cmd->texture_upload.width,
                      cmd->texture_upload.height,
                      cmd->texture_upload.mip_levels,
                      cmd->texture_upload.pixels,
                      cmd->texture_upload.row_stride);
    break;

case VC_CMD_TEXTURE_BIND:
    // Flush pending triangles first
    vc_batch_flush(&thread->batch, fr->cmd_buf);
    // Create samplers from filter settings
    // Allocate and bind descriptor set
    // vkCmdBindDescriptorSets()
    break;
```

#### Step 4: Add Voodoo-to-VC texture slot mapping in `vid_voodoo_vk.c`

Need a mapping table: `voodoo->texture_cache[tmu][entry]` -> VC pool slot.

```c
/* Mapping from Voodoo texture cache to VideoCommon texture pool.
 * vc_slot[tmu][entry] = VC pool slot index, or -1 if not uploaded. */
static int vc_tex_slot[2][TEX_CACHE_MAX];
static uint32_t vc_tex_generation[2][TEX_CACHE_MAX]; /* tracks staleness */
```

#### Step 5: Wire `vc_voodoo_submit_triangle()` to upload textures

Before submitting the triangle, call `voodoo_use_texture()` and check if the texture needs uploading:

```c
void vc_voodoo_submit_triangle(voodoo_t *voodoo, voodoo_params_t *params)
{
    // ... existing code ...

    // Decode textures into CPU cache (same as software path)
    voodoo_use_texture(voodoo, params, 0);
    if (voodoo->dual_tmus)
        voodoo_use_texture(voodoo, params, 1);

    // Upload TMU0 texture if needed
    int tmu0_entry = params->tex_entry[0];
    texture_t *tex0 = &voodoo->texture_cache[0][tmu0_entry];
    if (vc_tex_slot[0][tmu0_entry] < 0 ||
        vc_tex_generation[0][tmu0_entry] != tex0->some_generation) {
        // Need to upload
        int lod_min = (params->tLOD[0] >> 2) & 15;
        int w = params->tex_w_mask[0][lod_min] + 1;
        int h = params->tex_h_mask[0][lod_min] + 1;
        // Push upload command through ring
        vc_cmd_texture_upload_t upload = {
            .slot = vc_tex_slot[0][tmu0_entry],  // or -1 for auto
            .width = w, .height = h,
            .mip_levels = 1,  // start with base only
            .pixels = &tex0->data[texture_offset[lod_min]],
            .row_stride = (1 << (8 - params->tex_lod[0][lod_min])),
        };
        // Push to ring...
    }

    // Push texture bind command if bindings changed
    // Push triangle command
}
```

#### Step 6: Handle TMU1 texture coordinates

Replace the hardcoded zeros with actual gradient reconstruction (same pattern as TMU0).

#### Step 7: Upload fog table

When `fogMode` is active and fog table data changes, upload via `vc_texture_upload_fog()`.

### Concerns and Risks

#### Threading: Pointer Lifetime

The `pixels` pointer in the ring command points to `voodoo->texture_cache[tmu][entry].data[]`. This data is stable as long as:
- The texture cache entry is not evicted
- `voodoo_use_texture()` bumps the refcount, preventing eviction
- The render thread must process the upload command before the next `voodoo_use_texture()` call that might evict the same entry

**Risk**: If the ring drains slowly and the FIFO thread pushes many commands, a texture entry could be evicted before the render thread processes its upload command. **Mitigation**: After pushing the upload command, the FIFO thread should NOT release the refcount until a sync point.

Actually, looking more carefully: `voodoo_use_texture()` bumps `refcount`. The SW render thread bumps `refcount_r[thread]` when done. But the VK path doesn't have a render thread refcount mechanism. This means:
- The FIFO thread calls `voodoo_use_texture()`, which bumps `refcount`
- But nobody bumps `refcount_r` for the VK path
- So `refcount > refcount_r[0]` always, meaning the entry will NEVER be evicted
- This will eventually fill the 64-entry texture cache and call `fatal()`

**BUG**: The refcount_r mechanism is designed for the SW render threads. The VK path needs to either:
1. Bump `refcount_r[0]` after the VK render thread consumes the texture, OR
2. Copy the texture data to an intermediate buffer owned by the VK path

**Recommended**: Copy the data. The max texture size is 256x256x4 = 256KB, and copying is fast. This eliminates all lifetime concerns.

#### Descriptor Set Pool Exhaustion

The pool allows 64 descriptor sets per frame. Each texture binding change allocates a new set. If a game changes textures frequently (>64 times per frame), the pool will be exhausted.

**Mitigation**: Track current bindings and only allocate a new set when bindings actually change. Most games use far fewer than 64 unique texture combinations per frame.

#### Staging Buffer Contention

`vc_texture_upload()` uses a single 256KB staging buffer and waits synchronously. If multiple textures need uploading in the same frame, each one blocks until the GPU completes the transfer.

**Acceptable for now**: Voodoo games typically touch <10 unique textures per frame. Sequential uploads of ~10 256x256 textures would take <1ms total on modern hardware.

#### Row Stride Mismatch

The Voodoo texture cache uses `(1 << shift)` row stride where `shift = 8 - tex_lod[tmu][lod]`. For a 256x256 texture, stride = 256. For a 128x128, stride = 256 (because the data is stored in a flat 256-wide buffer). Wait, no -- looking at the texture decode more carefully:

```c
int shift = 8 - params->tex_lod[tmu][lod];
// ...
base += (1 << shift);  // advance by (1 << shift) texels per row
```

So for LOD 0 (256x256), shift=0, stride=1?? No, `tex_lod` is the log2 of the texture size. Let me re-examine.

Actually, `tex_lod[tmu][lod]` represents the LOD level's log2 size. For a 256x256 base:
- LOD 0: tex_lod = 0, shift = 8, stride = 256
- LOD 1: tex_lod = 1, shift = 7, stride = 128

Wait, that doesn't match either. The `tex_lod` array might store something different. Need to verify by reading the recalc functions. For safety, the row_stride parameter to `vc_texture_upload()` should be set based on `tex_w_mask[tmu][lod] + 1` or derived from the actual decode loop stride.

**Key observation**: The texture data in the cache is stored with a stride of `(1 << shift)` texels per row, where `shift = 8 - tex_lod`. The actual image width may be smaller (e.g., for non-power-of-2 or smaller textures). The `row_stride` parameter in `vc_texture_upload()` must match this.

#### Static State Not SLI-Safe

The `vid_voodoo_vk.c` file uses static variables for push constant deduplication (`last_pc`, `pc_valid`) and will need static variables for texture slot tracking. If two Voodoo cards are emulated simultaneously (SLI), these statics will corrupt. For now this is acceptable since 86Box only emulates one Voodoo at a time, but should be refactored to per-voodoo state eventually.

---

## 6. Summary of Required Changes

| File | Change | Priority |
|------|--------|----------|
| `vc_thread.h` | Add `vc_cmd_texture_upload_t` and `vc_cmd_texture_bind_t` payloads | HIGH |
| `vc_thread.c` | Implement `VC_CMD_TEXTURE_UPLOAD` and `VC_CMD_TEXTURE_BIND` dispatch | HIGH |
| `vid_voodoo_vk.c` | Call `voodoo_use_texture()` before triangle submit | HIGH |
| `vid_voodoo_vk.c` | Add Voodoo-to-VC texture slot mapping table | HIGH |
| `vid_voodoo_vk.c` | Push `VC_CMD_TEXTURE_UPLOAD` when texture needs uploading | HIGH |
| `vid_voodoo_vk.c` | Push `VC_CMD_TEXTURE_BIND` when bindings change | HIGH |
| `vid_voodoo_vk.c` | Reconstruct TMU1 texture coordinates (not zeros) | MEDIUM |
| `vid_voodoo_vk.c` | Upload fog table when fogMode active | MEDIUM |
| `videocommon.h` | Add public API for texture upload/bind through ring | HIGH |
| `vc_thread.c` | Remove hardcoded placeholder bind in `vc_begin_render_pass()` | LOW (can coexist) |

### Critical Path

1. Add ring command payloads (vc_thread.h)
2. Implement dispatch handlers (vc_thread.c)
3. Add `voodoo_use_texture()` + upload logic to `vid_voodoo_vk.c`
4. Add texture bind logic with sampler creation
5. Handle refcount_r or copy texture data to avoid cache corruption
6. Test with textured geometry

### Estimated Complexity

- Ring command changes: ~50 lines
- Dispatch handlers: ~60 lines
- vid_voodoo_vk.c texture integration: ~150 lines
- Total: ~260 lines of new code
