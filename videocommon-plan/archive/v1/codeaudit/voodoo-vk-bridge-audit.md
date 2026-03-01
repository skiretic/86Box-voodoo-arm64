# Voodoo VK Bridge Audit

Audit of the Voodoo-to-VideoCommon Vulkan bridge layer.

**Files audited:**
- `src/include/86box/vid_voodoo_vk.h` (66 lines)
- `src/video/vid_voodoo_vk.c` (1428 lines)

**Date:** 2026-02-28
**Auditor:** vc-debug agent

---

## src/include/86box/vid_voodoo_vk.h -- 66 lines

### Issues Found

None. The header is clean.

### Notes

- Well-structured public API with clear doc comments for every function.
- All functions take `voodoo_t*` as first parameter, maintaining a consistent interface.
- Forward declarations of `voodoo_t` and `voodoo_params_t` avoid pulling in the large common header.
- Properly guarded by `#ifdef USE_VIDEOCOMMON` and include guard.
- Return values for error conditions are documented (0xffff / 0xffffffff for reads).
- The `vc_voodoo_fb_writew` doc comment correctly notes the pipeline-mode fallthrough limitation.

---

## src/video/vid_voodoo_vk.c -- 1428 lines

### Issues Found

#### CRITICAL

**[C1] Line 960: `vc_voodoo_sync` does not reset texture bind tracking after descriptor pool reset**

`vc_voodoo_sync` calls `vc_sync(ctx)`, which on the render thread executes `vc_end_frame` / `vc_begin_frame`. The begin-frame path resets the descriptor pool via `vkResetDescriptorPool`, invalidating ALL descriptor sets from the previous frame. The render thread correctly invalidates its own tracking (lines 711-713 in `vc_thread.c`).

However, the producer side in `vc_voodoo_sync` only resets `vk_last_alpha_mode` and `vk_last_depth_bits` (lines 960-961). It does NOT reset:
- `vk_last_tmu0_slot`
- `vk_last_tmu1_slot`
- `vk_last_texmode[0]` / `vk_last_texmode[1]`

Compare with `vc_voodoo_swap_buffers` (lines 928-931) which correctly resets all of these, with an explanatory comment about why it's needed.

**Impact:** After any `vc_voodoo_sync` call (triggered by LFB reads, explicit register writes, etc.), subsequent triangles may skip the texture bind command because the producer thinks the textures are still bound. The render thread will draw with the placeholder descriptor set installed by `vc_begin_frame`, producing a black or corrupted scene until the next texture identity change forces a re-bind.

**Severity analysis:** This could manifest as texture loss after LFB-heavy operations (screen captures, collision detection). In practice, it may be partially masked if `vc_voodoo_sync` is rarely called while textured triangles are active, or if the next triangle happens to change textures anyway. But it is a correctness bug that will bite eventually.

**Suggested fix:**
```c
void
vc_voodoo_sync(voodoo_t *voodoo)
{
    vc_context_t *ctx = (vc_context_t *) atomic_load_explicit(&voodoo->vc_ctx, memory_order_acquire);
    if (ctx) {
        vc_sync(ctx);

        /* Sync triggers end/begin frame on the render thread, which
         * resets the descriptor pool and default pipeline state.
         * Force the producer to re-send everything on the next triangle. */
        vk_last_alpha_mode = 0xFFFFFFFF;
        vk_last_depth_bits = 0xFFFFFFFF;
        vk_last_tmu0_slot  = -2;
        vk_last_tmu1_slot  = -2;
        vk_last_texmode[0] = 0xFFFFFFFF;
        vk_last_texmode[1] = 0xFFFFFFFF;
    }
}
```

---

#### HIGH

**[H1] Lines 488-498: Double division in 1/W (OOW) reconstruction for vertices B and C**

The `oowA` value is computed correctly via `fix18_32_to_float(params->startW)`, which internally divides by `(double)(1LL << 32)`.

But `oowB` and `oowC` compute:
```c
float oowB = (float) (reconstruct_18_32(...) / (double) (1LL << 32));
```

The `reconstruct_18_32` function (lines 299-307) returns a `double` that is STILL in 18.32 fixed-point (it reconstructs the raw fixed-point value at vertex B). The caller then divides by `(1LL << 32)` to convert to float. This is correct.

However, the same pattern is repeated for ALL TMU0/TMU1 texture coordinates (lines 511-586), with each one doing `reconstruct_18_32(...) / (double)(1LL << 32)`. While functionally correct, the division is unnecessarily duplicated -- `fix18_32_to_float` already does exactly this. This inconsistency is a readability/maintenance risk: someone editing `fix18_32_to_float` would not realize the same conversion is done inline elsewhere.

**Impact:** Not a correctness bug, but a maintainability hazard. If `fix18_32_to_float` is ever updated (e.g., to handle edge cases), the inline conversions would diverge.

**Suggested fix:** Use `fix18_32_to_float` for ALL 18.32-to-float conversions, wrapping the `reconstruct_18_32` return:
```c
float oowB = (float) fix18_32_to_float((int64_t) reconstruct_18_32(
    params->startW, params->dWdX, params->dWdY, dxB, dyB));
```
Note: `reconstruct_18_32` returns `double`, and `fix18_32_to_float` takes `int64_t`, so a minor refactor is needed -- either change `fix18_32_to_float` to accept `double`, or add a `fix18_32_double_to_float(double v)` helper.

---

**[H2] Lines 1140-1148: `vc_voodoo_fb_readl` calls `vc_voodoo_lfb_read_pixel` twice, causing double sync**

Each call to `vc_voodoo_lfb_read_pixel` independently:
1. Calls `vc_readback_track_read(ctx)` (incrementing the read counter twice for one logical read)
2. Calls `vc_wait_idle(ctx)` (redundant second GPU fence wait)
3. Calls `vc_readback_color_sync(ctx, ...)` or `vc_readback_depth_sync(ctx, ...)` (redundant second staging buffer copy)

**Impact:** Performance -- `vc_wait_idle` involves spinning on the SPSC ring drain + `vkDeviceWaitIdle`, costing ~0.5-2ms per call. Doubling this for every 32-bit LFB read is wasteful. The double `vc_readback_track_read` also inflates the read counter, potentially triggering the async mode switch sooner than intended (threshold is 10 reads/frame, but a single `readl` counts as 2).

**Suggested fix:** Refactor `vc_voodoo_fb_readl` to decode both pixels from a single sync+readback:
```c
uint32_t
vc_voodoo_fb_readl(voodoo_t *voodoo, uint32_t addr)
{
    vc_context_t *ctx = (vc_context_t *) atomic_load_explicit(
        &voodoo->vc_ctx, memory_order_acquire);
    if (!ctx)
        return 0xffffffff;

    vc_readback_track_read(ctx);

    /* Decode both pixel addresses. */
    int x0, y0, x1, y1;
    /* ... decode addr and addr+2 ... */

    /* Single sync + readback for both pixels. */
    if (!vc_readback_is_async(ctx))
        vc_wait_idle(ctx);

    /* Read both pixels from the same staging buffer. */
    uint16_t lo = /* read pixel at (x0,y0) */;
    uint16_t hi = /* read pixel at (x1,y1) */;
    return (uint32_t) lo | ((uint32_t) hi << 16);
}
```

---

**[H3] Line 871: Unconditional `pclog()` call in `vc_voodoo_fastfill`**

```c
pclog("VK FASTFILL: fbzMode=%08x do_color=%d do_depth=%d color1=%08x zaColor=%08x\n",
      params->fbzMode, do_color, do_depth, params->color1, params->zaColor);
```

`pclog()` is an always-on logging function that writes to the log file. This fires on every fastfill command, which occurs at least once per frame in many games. Fastfill-heavy games (those that clear both color and depth every frame) will produce continuous log spam.

**Impact:** Performance degradation from file I/O on every frame. Log file bloat. This is clearly debug output that was left in after development.

**Suggested fix:** Either remove the line, or wrap it in the module's conditional logging macro:
```c
vk_log("VK FASTFILL: fbzMode=%08x do_color=%d do_depth=%d color1=%08x zaColor=%08x\n",
       params->fbzMode, do_color, do_depth, params->color1, params->zaColor);
```

---

**[H4] Lines 812-813: Push constant deduplication uses function-scope statics, not reset on sync**

```c
static vc_push_constants_t last_pc;
static int                 pc_valid = 0;
```

`pc_valid` is never reset to 0 after `vc_voodoo_sync` or `vc_voodoo_swap_buffers`. Since `vc_sync` / `vc_swap_buffers` restart the render pass with default state, the render thread expects push constants to be re-sent. If the first triangle after a sync happens to have the same push constants as the last triangle before the sync, the `memcmp` will succeed and the push constant update will be skipped.

In practice this is likely masked because `vc_voodoo_swap_buffers` also changes `vk_last_alpha_mode` and `vk_last_depth_bits`, forcing other ring commands that implicitly cause a batch flush and re-application of push constants. But it is a latent bug.

**Suggested fix:** Reset `pc_valid` in both `vc_voodoo_swap_buffers` and `vc_voodoo_sync`. Since `pc_valid` is function-local, it would need to be hoisted to file scope:
```c
static vc_push_constants_t vk_last_pc;
static int                 vk_pc_valid = 0;
```
Then reset `vk_pc_valid = 0;` alongside the other tracker resets.

---

#### MODERATE

**[M1] Lines 88-96: File-scope static tracking state is not per-Voodoo-instance**

All deduplication tracking state (`vk_tex_state`, `vk_last_texmode`, `vk_last_tmu0_slot`, `vk_last_tmu1_slot`, `vk_last_alpha_mode`, `vk_last_depth_bits`, `vk_fog_table_checksum`, `last_pc`, `pc_valid`) is file-scope static. If two Voodoo cards were emulated simultaneously (e.g., SLI pair), they would corrupt each other's tracking state.

**Impact:** Low in practice -- 86Box currently only instantiates one Voodoo card at a time. But the code comments and design doc describe SLI support as a future possibility. If SLI were ever implemented, this would silently cause incorrect deduplication (skipping texture binds, blend state changes, etc.).

**Suggested fix:** Move all tracking state into a per-instance struct stored in `voodoo_t` (or alongside `vc_ctx`). This is a larger refactor and should be deferred until SLI is actually implemented.

---

**[M2] Lines 1155-1183: `vc_voodoo_lfb_decode_addr` always returns 1 despite documenting bounds failure**

The function comment says "Returns non-zero on success, 0 on bounds failure" but the function unconditionally returns 1. The caller in `vc_voodoo_fb_writel` (line 1416) checks the return value: `if (!vc_voodoo_lfb_decode_addr(...)) return;`. This check is dead code -- the function never returns 0.

The read path (`vc_voodoo_lfb_read_pixel`, lines 1069-1070) has its own inline bounds check against `fb_w`/`fb_h`, and the downstream `vc_lfb_write_color_pixel` (in vc_readback.c, line 1352) also bounds-checks. So there is no out-of-bounds access. But the dead bounds check in the write path is misleading.

**Impact:** No correctness issue (downstream bounds checking exists), but the misleading return value and dead check reduce code clarity.

**Suggested fix:** Either implement bounds checking in `vc_voodoo_lfb_decode_addr` (requires passing framebuffer dimensions), or change the function to void and remove the dead check.

---

**[M3] Lines 1047-1057 vs 1169-1176: Duplicated LFB address decode logic**

The LFB address-to-pixel decoding logic exists in two places:
1. Inline in `vc_voodoo_lfb_read_pixel` (lines 1050-1057)
2. In `vc_voodoo_lfb_decode_addr` (lines 1169-1176)

Both implement the same Banshee vs V1/V2 address layout with the same SLI halving. The read path does not use the shared helper function, creating a maintenance risk if the decoding logic ever needs to change.

**Impact:** If address decoding is modified (e.g., to handle tiled LFB modes), the change must be applied in both places.

**Suggested fix:** Refactor `vc_voodoo_lfb_read_pixel` to use `vc_voodoo_lfb_decode_addr`.

---

**[M4] Lines 622-627: Texture row_stride calculation uses `tex_lod` lookup for width, not actual texture dimensions**

```c
int shift      = 8 - params->tex_lod[0][lod_min];
int row_stride = 1 << shift;
```

The `row_stride` is computed as `1 << (8 - tex_lod[tmu][lod_min])`. This gives the row stride in texels for the texture cache data layout. However, `w` is computed from `params->tex_w_mask[0][lod_min] + 1`, and these two values may differ for non-square or non-power-of-two textures.

The `vc_texture_upload_async` function receives both `w` and `row_stride` separately, which is correct -- `w` is the visible width and `row_stride` is the pitch. But the `copy_size` is computed as `row_stride * h * sizeof(uint32_t)`, meaning it copies `row_stride` texels per row, not `w` texels. This is correct because the texture cache data is stored with `row_stride` pitch.

After closer inspection, this is actually correct for the software texture cache data layout. The `texture_offset` table and the `tex_lod` / `tex_shift` arrays define the cache layout, and `row_stride` matches the stored data pitch. No bug here.

**Impact:** None -- the calculation is correct, but the relationship between `w`, `h`, `row_stride`, and the texture cache layout is non-obvious. A comment explaining why `row_stride` differs from `w` would help.

---

**[M5] Lines 630-634: Texture data pointer passed directly to async upload without copy**

```c
uint32_t *src       = &tex->data[texture_offset[lod_min]];
size_t    copy_size = (size_t) row_stride * h * sizeof(uint32_t);

tex->refcount_r[0]++;

int vc_slot = 0 * TEX_CACHE_MAX + entry;

vc_texture_upload_async(ctx, vc_slot,
                        (uint32_t) w, (uint32_t) h, 1,
                        src, copy_size, (uint32_t) row_stride);
```

The `src` pointer points directly into the texture cache entry's data buffer. The `vc_texture_upload_async` function is documented as copying the data internally (via `malloc` + `memcpy`), so the pointer remains valid during the async upload. The `refcount_r[0]++` prevents the cache entry from being evicted while the data is being copied.

After checking the `vc_texture_upload_async` implementation in `videocommon.h` line 140-141: "The pixel data is copied (malloc'd) and ownership of the copy is transferred to the render thread, which frees it after upload."

**Impact:** No bug -- the API contract guarantees a copy. But the code relies on this contract being maintained; if `vc_texture_upload_async` were ever changed to take ownership of the pointer without copying, the texture cache would be corrupted.

This is a note, not an issue -- the API is correctly documented and used.

---

#### LOW

**[L1] Lines 268-276 vs 283-290: `reconstruct_12_12` and `reconstruct_20_12` are identical**

Both functions have the same implementation:
```c
return (int64_t) start
    + ((int64_t) dVdX * (int64_t) pos_dx
       + (int64_t) dVdY * (int64_t) pos_dy)
    / 16;
```

The only difference is the comment and the function name. They exist as separate functions for documentation purposes (to indicate the different fixed-point formats of their inputs), but the arithmetic is identical.

**Impact:** None -- this is a style/maintenance choice. Having separate functions with descriptive names is arguably better for clarity, even if the implementation is duplicated.

---

**[L2] Lines 272-275: Division semantics differ from software renderer's right-shift**

The `reconstruct_12_12` function uses `/16` (truncation toward zero for negative values), while the software renderer's `FBZ_PARAM_ADJUST` path (vid_voodoo_render.c line 1682) uses `>> 4` (arithmetic right shift, rounding toward negative infinity for negative values on typical platforms).

For a negative intermediate value like `-17`, `/16 = -1` but `>> 4 = -2`. The difference is at most 1 LSB of the 12.12 fixed-point result, which maps to `1/(4096*255) = ~0.00000096` in normalized color. This is sub-LSB precision and undetectable.

**Impact:** Acceptable precision difference. Less than 1 LSB in the final 8-bit color. Not a bug.

---

**[L3] Lines 750-758: `vc_set_depth_state` color write mask is always all-or-nothing**

```c
int vk_color_wmask = (params->fbzMode & FBZ_RGB_WMASK) ? 0x0F : 0;
```

The mask is either 0x0F (RGBA all enabled) or 0 (nothing). Voodoo hardware only has a single RGB write mask bit (FBZ_RGB_WMASK), not per-channel control. The alpha channel is written to the auxiliary buffer, not the color buffer, in Voodoo hardware.

In the Vulkan path, the framebuffer uses RGBA8, so including A in the write mask (bit 3 of 0x0F) is correct -- we always want to write the alpha channel when color writes are enabled, because the Vulkan blending pipeline reads/writes RGBA atomically.

**Impact:** None -- this is correct for the Vulkan rendering model. Noted for documentation.

---

**[L4] Line 727: Fog table checksum uses XOR-fold, which has collision risk**

```c
const uint32_t *fog_words = (const uint32_t *) params->fogTable;
uint32_t        cksum     = 0;
for (int i = 0; i < 32; i++)
    cksum ^= fog_words[i];
```

XOR-fold is a weak checksum -- it cannot detect swapped words, and many patterns of changes will produce the same checksum (e.g., changing two words that XOR to the same delta). A simple CRC32 or FNV-1a hash would be more robust.

**Impact:** Extremely low. The comment acknowledges this: "collisions are harmless (worst case: one redundant upload)". The fog table is 128 bytes and changes rarely. A collision would just skip a needed re-upload until the next frame when the checksum changes again. In practice, XOR-fold is fine for this use case because fog table changes are infrequent and collisions are self-correcting on the next modification.

---

**[L5] Lines 452-455: Color values cast to int64_t without sign consideration**

```c
float rA = fix12_12_color_to_float((int64_t) params->startR);
```

`params->startR` is `uint32_t` (12.12 unsigned). Casting to `int64_t` preserves the value exactly (uint32_t fits in int64_t without loss). The `fix12_12_color_to_float` function then divides by `4096.0 * 255.0`. Since the input is unsigned (colors are non-negative in Voodoo), the cast is safe.

However, after gradient reconstruction via `reconstruct_12_12`, the result CAN be negative (color extrapolation beyond the triangle vertices can produce negative values). The `fix12_12_color_to_float` function does not clamp to [0,1]. Negative float colors will be passed to the GPU, which will clamp them naturally during rasterization (Vulkan clamps fragment outputs to the attachment format range for UNORM attachments). So this is harmless.

**Impact:** None -- GPU-side clamping handles it. But adding a comment noting that negative extrapolated colors are expected and GPU-clamped would improve clarity.

---

**[L6] Line 816: Push constants use `voodoo->h_disp` / `voodoo->v_disp` for framebuffer dimensions**

```c
vc_push_constants_update(&pc, params, voodoo->h_disp, voodoo->v_disp);
```

These are the display dimensions (horizontal/vertical display counters), not the framebuffer allocation size. In most cases they match, but during mode changes or with unusual CRTC configurations, they could differ from the Vulkan framebuffer dimensions. The vertex shader uses these values for the pixel-to-NDC conversion, so incorrect values would cause geometry to be rendered at the wrong position/scale.

**Impact:** Low -- mode changes during active rendering are extremely rare. The framebuffer is sized to match `h_disp` / `v_disp` at init time, so they normally agree.

---

### Notes

1. **Gradient vertex reconstruction is correct.** The formula `val_B = start + (dVdX * dxB + dVdY * dyB) / 16` correctly reconstructs attribute values at vertices B and C from vertex A's start value and per-pixel gradients, accounting for the 12.4 subpixel position deltas. All fixed-point format conversions (12.4 positions, 12.12 colors, 20.12 depth, 18.32 texture/W) use appropriate scaling constants. The use of `int64_t` intermediates for 12.12 and 20.12, and `double` intermediates for 18.32, prevents overflow.

2. **Push constant layout is correct.** The 64-byte `vc_push_constants_t` struct matches the GLSL `PushConstants` block in both `voodoo_uber.vert` and `voodoo_uber.frag` exactly -- same field order, same types, same offsets. The `_Static_assert` on sizeof guarantees compile-time validation.

3. **Blend factor mapping is correct.** The `voodoo_afunc_to_vk_blend_factor` function (lines 141-173) correctly maps all 8 defined Voodoo AFUNC values to VkBlendFactor, including the directional swap for `AFUNC_A_COLOR` / `AFUNC_AOM_COLOR` (source vs destination interpretation). The ACOLORBEFOREFOG case (AFUNC value 15 as dst) correctly maps to `VK_BLEND_FACTOR_SRC1_COLOR` (value 15) for dual-source blending.

4. **Deferred init pattern is robust.** Every public function loads `voodoo->vc_ctx` with `atomic_load_explicit(..., memory_order_acquire)` and returns early (with safe defaults) if NULL. This correctly handles the window between `voodoo_init()` and `vc_init()` completing on the background thread.

5. **LFB read/write format handling is complete.** The 16-bit write path handles RGB565, RGB555, ARGB1555, and DEPTH. The 32-bit write path additionally handles ARGB8888, XRGB8888, DEPTH_RGB565, DEPTH_RGB555, and DEPTH_ARGB1555. These cover all defined LFB format values in the Voodoo register spec.

6. **Thread safety model is sound.** Triangle submission, texture upload/bind, push constants, blend state, and depth state all run on the FIFO thread and push to the SPSC ring. LFB reads use `vc_wait_idle` (safe from any thread). LFB writes go to a mutex-protected shadow buffer (safe from the CPU emulation thread). The only cross-thread concern is the readback sync pointers returned by `vc_readback_color_sync` / `vc_readback_depth_sync`, which are valid until the next call -- the single-threaded LFB read path naturally serializes these.

7. **Texture deduplication logic is well-designed.** The multi-level deduplication (cache entry identity, textureMode changes, bound slot changes) minimizes ring traffic. The `need_bind` flag aggregates all possible reasons for a re-bind into a single check, avoiding redundant bind commands.

8. **The `FBZ_PARAM_ADJUST` flag is intentionally not checked.** The VK bridge reconstructs vertex positions for the GPU rasterizer, which handles subpixel snapping internally. The SW renderer's `FBZ_PARAM_ADJUST` compensates for its own pixel-center iteration start point, which is not applicable to the GPU path. This is correct by design.

9. **Depth clamping is handled.** The `fix20_12_depth_to_float` function (lines 233-242) clamps the result to [0,1], which is necessary because Voodoo depth values can overflow during gradient reconstruction (extrapolation beyond the triangle). Without clamping, out-of-range values would cause undefined behavior in the Vulkan depth test.

10. **Color clamping is NOT handled on the CPU side**, but this is acceptable because the GPU's UNORM framebuffer attachment format clamps fragment shader output to [0,1] automatically. The vertex shader passes unclamped colors, and `noperspective` interpolation preserves them linearly, which matches Voodoo's Gouraud shading behavior.

---

### Summary

| Severity | Count | Description |
|----------|-------|-------------|
| CRITICAL | 1 | Missing texture bind tracking reset in `vc_voodoo_sync` |
| HIGH | 3 | Double sync in readl, unconditional pclog, push constant tracking not reset |
| MODERATE | 5 | Static state not per-instance, dead bounds check, duplicated decode logic, documentation gaps |
| LOW | 6 | Identical functions, rounding difference, write mask, XOR checksum, sign notes, fb dims |

**Priority fixes:**
1. **[C1]** Add texture tracking reset to `vc_voodoo_sync` (1-minute fix, prevents post-sync texture loss)
2. **[H3]** Remove or conditionalize the `pclog` in `vc_voodoo_fastfill` (30-second fix)
3. **[H4]** Hoist `pc_valid` to file scope and reset in sync/swap (5-minute fix)
4. **[H2]** Refactor `vc_voodoo_fb_readl` to avoid double sync (15-minute fix)
