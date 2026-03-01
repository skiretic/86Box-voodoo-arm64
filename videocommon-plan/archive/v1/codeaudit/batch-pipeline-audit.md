# vc_batch + vc_pipeline Code Audit

**Date**: 2026-02-28
**Files audited**: `vc_batch.h`, `vc_batch.c`, `vc_pipeline.h`, `vc_pipeline.c`
**Cross-referenced**: `vc_thread.c`, `vc_thread.h`, `vc_core.h`, `vc_internal.h`, `vc_render_pass.h`

---

## vc_batch.h -- 89 lines

### Issues Found

- **None.**

### Notes

- `vc_vertex_t` layout verified by compiler: 14 floats = 56 bytes, all offsets match the `VkVertexInputAttributeDescription` array in `vc_pipeline.c` (0, 8, 12, 28, 40, 52).
- `VC_MAX_VERTICES` = 18724 (1048544 / 56). Integer division truncates; `18724 * 56 = 1048544 <= 1048576 (1MB)`. Correct.
- The `(int)` cast in the `VC_MAX_VERTICES` macro is for signedness matching with `uint32_t vertex_offset`. The division itself is fine.
- Clean header with proper include guard and forward declaration.

---

## vc_batch.c -- 127 lines

### Issues Found

- **[MODERATE] Line 73: Ring overflow returns -1 with no logging.** When `vertex_offset + 3 > VC_MAX_VERTICES`, the function silently returns -1. The caller (`vc_dispatch_triangle` in `vc_thread.c`) handles this correctly by flushing and retrying, then doing an emergency frame submit. However, if the emergency retry at line 467 of `vc_thread.c` also fails (returns -1), that return value is silently ignored. In practice this cannot happen because `vc_begin_frame()` calls `vc_batch_reset()` setting `vertex_offset` back to 0, so there is always room for 3 vertices. The scenario is unreachable, but the unchecked return value on the final `vc_batch_push_triangle` call (line 467) is a defensive-coding gap.

- **[LOW] Line 94: VkDeviceSize multiplication could overflow on 32-bit.** `batch->flush_offset * sizeof(vc_vertex_t)` -- `flush_offset` is `uint32_t`, `sizeof(vc_vertex_t)` is `size_t`. On 32-bit platforms, if `flush_offset` were near `UINT32_MAX`, this could overflow before assignment to `VkDeviceSize` (64-bit). In practice, `flush_offset <= 18724` (max vertices), so `18724 * 56 = 1048544` fits comfortably. This is a theoretical concern, not a real bug.

### Notes

- **Thread safety**: `vc_batch_t` is embedded in `vc_thread_t` and accessed exclusively by the render thread. No concurrent access. Correct.
- **Persistent mapping**: `VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT` with `HOST_COHERENT` -- writes are immediately visible to the GPU without explicit flush. Correct for Vulkan 1.2.
- **No barrier needed** between CPU vertex writes and GPU vertex read because HOST_COHERENT makes CPU stores visible to device without `vkFlushMappedMemoryRanges`, and the `vkQueueSubmit` in `vc_end_frame` includes an implicit host-write availability operation (Vulkan spec 7.9).
- **`vc_batch_close` double-cleanup**: `memset(batch, 0, ...)` at line 126 is redundant since the check at line 119 already sets individual fields to zero/NULL. Harmless but unnecessary.
- **`vc_batch_close` NULL check on `ctx`**: Needed because `vc_thread_close` calls `vc_batch_close` and `ctx` could be NULL during partial init cleanup.
- The batch uses a simple linear ring with no wraparound -- `vertex_offset` only grows until `vc_batch_reset`. This is fine because the ring is reset every frame via `vc_begin_frame -> vc_batch_reset`.

---

## vc_pipeline.h -- 115 lines

### Issues Found

- **None.**

### Notes

- `vc_pipeline_key_t` is exactly 8 bytes (8 x `uint8_t`) with no padding. Verified by compiler. `memcmp` in `vc_pipeline_lookup` is safe and correct.
- `VC_PIPELINE_CACHE_MAX = 32` is well-sized for Voodoo games (5-15 unique blend configs). No hash map needed.
- The `caps` field (line 72) stores `vc_capability_flags_t` as `uint32_t`, matching the `caps` field in `vc_context_t`. This is stored at pipeline cache creation time so the pipeline creation code doesn't need the full context for capability queries. Good design.
- `vc_pipeline_get_layout` and `vc_pipeline_get_desc_set_layout` are `static inline` -- no function call overhead. Correct.
- Comment "VkBlendFactor (0-18)" on `src_color_factor` et al. is accurate: `VK_BLEND_FACTOR_ZERO=0` through `VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA=18`, all fit in `uint8_t`.

---

## vc_pipeline.c -- 383 lines

### Issues Found

- **[MODERATE] Lines 289-296: Partial creation leak relies on external cleanup.** If `vc_create_desc_set_layout` succeeds (line 289) but `vc_create_pipeline_layout` fails (line 294), `vc_pipeline_create` returns -1 without destroying the descriptor set layout. The layout is stored in `cache->desc_set_layout`. The caller (`vc_init` in `vc_core.c`) calls `vc_close -> vc_pipeline_destroy`, which does check and destroy partial state. So this is NOT a leak in practice. However, the function's internal contract implicitly requires the caller to call `vc_pipeline_destroy` on failure. This is the established pattern throughout VideoCommon but is undocumented -- worth noting for future contributors.

- **[MODERATE] Lines 339-342: Pipeline cache full returns VK_NULL_HANDLE with no fallback.** When `cache->count >= VC_PIPELINE_CACHE_MAX`, the lookup returns `VK_NULL_HANDLE`. The caller in `vc_thread.c` (`vc_dispatch_pipeline_key`, line 570-572) checks for `VK_NULL_HANDLE` and skips the `vkCmdBindPipeline`. This means the PREVIOUS pipeline remains bound, which could produce incorrect rendering (wrong blend mode). For a soft failure this is acceptable, but the user gets no indication beyond a single pclog line. No crash, but visually wrong output. With 32 slots and typical Voodoo games using 5-15 blend configs, this is unlikely to trigger in practice.

  **Impact assessment**: Could a game actually hit 32 unique pipeline keys? The key has 8 fields: blend_enable (2 values), src/dst color/alpha factors (19 values each), two blend ops (5 values each), and color write mask (16 values). Voodoo hardware has limited blend mode combinations -- realistically 8-20 unique keys per game session. The limit is safe.

- **[LOW] Line 236: VK_DYNAMIC_STATE_CULL_MODE declared but never used.** The pipeline includes `VK_DYNAMIC_STATE_CULL_MODE` in the dynamic state list when extended dynamic state is available (line 236), and `vc_begin_render_pass` (vc_thread.c line 234) calls `vkCmdSetCullModeEXT(fr->cmd_buf, VK_CULL_MODE_NONE)`. However, cull mode is ALWAYS `VK_CULL_MODE_NONE` (Voodoo never culls -- it does winding-based triangle rejection in software). This dynamic state is set once and never changed. It's harmless but adds unnecessary dynamic state overhead. Not a bug -- just dead dynamic state.

- **[LOW] Line 200: Baked depth compare op is VK_COMPARE_OP_ALWAYS, comment explains rationale.** When extended dynamic state is NOT available, the pipeline uses ALWAYS as the depth compare op. This means depth testing is effectively disabled at the pipeline level, and without extended dynamic state there is no way to change it per-draw. On implementations without `VK_EXT_extended_dynamic_state` (unlikely on modern Vulkan 1.2 targets), all depth testing would be broken. The code comments acknowledge this ("if dynamic state somehow doesn't fire, triangles still render (just without depth rejection)"). This is a conscious design tradeoff, not a bug. MoltenVK, Mesa, and all target platforms support this extension.

- **[LOW] Lines 264-266: `vkCreateGraphicsPipelines` can return VK_PIPELINE_COMPILE_REQUIRED_EXT.** The current code only checks for `VK_SUCCESS` via `VK_CHECK`. On Vulkan 1.2 without pipeline caching extensions, this is fine -- `VK_PIPELINE_COMPILE_REQUIRED_EXT` only applies to `VK_EXT_pipeline_creation_cache_control`, which is not enabled. Not a real issue.

### Notes

- **Vertex input attribute descriptions** (lines 133-152) match `vc_vertex_t` layout exactly. Verified by compiler offset computation:
  - location 0: position (vec2) at offset 0 -- CORRECT
  - location 1: depth (float) at offset 8 -- CORRECT
  - location 2: color (vec4) at offset 12 -- CORRECT
  - location 3: texcoord0 (vec3) at offset 28 -- CORRECT
  - location 4: texcoord1 (vec3) at offset 40 -- CORRECT
  - location 5: oow (float) at offset 52 -- CORRECT

- **Push constant range** (lines 79-83): 64 bytes, VERTEX | FRAGMENT stage flags. This matches the `vkCmdPushConstants` call in `vc_thread.c` line 487 and the push constant layout spec in `videocommon-plan/research/push-constant-layout.md`.

- **Descriptor set layout** (lines 37-56): 3 combined image samplers at bindings 0, 1, 2 (TMU0, TMU1, fog table), fragment-only. Correct for the uber-shader architecture.

- **Rasterization state**: `VK_CULL_MODE_NONE`, `VK_FRONT_FACE_COUNTER_CLOCKWISE`, `VK_POLYGON_MODE_FILL`. Voodoo does not perform hardware culling (the triangle setup engine already handles winding rejection). Front face is irrelevant when culling is disabled. Correct.

- **Multisampling disabled**: `VK_SAMPLE_COUNT_1_BIT`. Voodoo 1/2 has no MSAA. Correct.

- **`depthBoundsTestEnable` defaults to VK_FALSE** (not explicitly set, but zero-initialized by C designated initializer). This is correct -- Voodoo does not use depth bounds testing.

- **Pipeline cache** (`VkPipelineCache`, lines 298-306): Created but never persisted to disk. The comment mentions "disk persistence" but no save/load code exists. This is fine for now -- the pipeline cache provides within-session compilation deduplication.

- **Thread safety of `vc_pipeline_lookup`**: Called exclusively from the render thread (via `vc_dispatch_pipeline_key` and `vc_dispatch_depth_state` in `vc_thread.c`). Also called from `vc_begin_render_pass`, which runs on the render thread. No concurrent access. Correct.

---

## Cross-File Observations

### Batch + Pipeline Interaction (via vc_thread.c)

1. **Flush-before-state-change pattern**: Every state change (`VC_CMD_PUSH_CONSTANTS`, `VC_CMD_PIPELINE_KEY`, `VC_CMD_DEPTH_STATE`, `VC_CMD_TEXTURE_BIND`) calls `vc_batch_flush` before recording the state change. This ensures that pending triangles use the OLD state, and subsequent triangles use the NEW state. This is the correct Vulkan command buffer recording pattern.

2. **Emergency frame restart** (vc_thread.c lines 434-468): When the vertex ring overflows, the code correctly: (a) ends the frame (submits command buffer), (b) begins a new frame (resets batch), (c) re-issues pipeline key, depth state, and texture binds. This is robust.

3. **Pipeline lookup during emergency restart** (vc_thread.c line 442): Calls `vc_batch_flush` before `vc_pipeline_lookup`, but the batch was just reset by `vc_begin_frame`. This flush is a no-op (vertex_count == 0, early return at vc_batch.c line 90-91). Harmless but unnecessary.

### Memory Model

- The batch module uses plain (non-atomic) memory access, which is correct because it's single-threaded (render thread only).
- The pipeline cache uses plain memory access, also correct (render thread only).
- Both modules properly rely on the SPSC ring's memory ordering for cross-thread communication (producer writes to ring entries with release semantics; consumer reads with acquire semantics).

### Resource Lifecycle

- `vc_batch_t` is embedded in `vc_thread_t` (not heap-allocated). Created in `vc_thread_init`, destroyed in `vc_thread_close`. No leak possible.
- `vc_pipeline_cache_t` is embedded in `vc_context_t`. Created in `vc_init`, destroyed in `vc_close`. No leak possible.
- `vc_pipeline_destroy` correctly destroys: all cached pipelines, the VkPipelineCache, the VkPipelineLayout, and the VkDescriptorSetLayout. Complete cleanup.

---

## Summary

| Severity | Count | Description |
|----------|-------|-------------|
| CRITICAL | 0 | None |
| HIGH | 0 | None |
| MODERATE | 2 | Unchecked final push_triangle return (unreachable); partial creation cleanup relies on caller |
| LOW | 4 | VkDeviceSize overflow (theoretical); unused cull mode dynamic state; ALWAYS depth fallback; VK_PIPELINE_COMPILE_REQUIRED_EXT |

**Overall assessment**: These modules are clean and well-structured. No correctness bugs found. The vertex attribute layout is verified correct. The pipeline cache design is appropriate for the workload. Thread safety is correct (single-threaded access on render thread). The flush-before-state-change pattern is consistently applied. The emergency frame restart recovery is robust.
