# SPSC Ring and Render Thread Audit

**Files audited:**
- `src/video/videocommon/vc_thread.h` (270 lines)
- `src/video/videocommon/vc_thread.c` (1005 lines)

**Date:** 2026-02-28
**Auditor:** vc-debug agent

---

## vc_thread.h -- 270 lines

### Issues Found

- [LOW] Lines 88-95: `vc_cmd_pipeline_key_t` is a reduced version of `vc_pipeline_key_t` (5 fields vs 8). Missing `color_blend_op`, `alpha_blend_op`, and `color_write_mask`. Not a bug -- the dispatch code correctly reconstructs the full key by hardcoding `VK_BLEND_OP_ADD` and pulling `color_write_mask` from depth state. However, if Voodoo subtract blend (ASUB) were ever implemented at the pipeline level (instead of in-shader), this struct would need updating. Comment documenting this design decision would be helpful.

- [LOW] Line 75: `vc_cmd_texture_upload_t.pixels` is a `void *` that crosses thread boundaries via the SPSC ring. Ownership transfer semantics (producer allocates, consumer frees) are correct but not documented in the struct definition. A comment like `/* Owned: consumer frees after upload. */` would prevent future confusion.

### Notes

- **Ring capacity**: 16384 entries at ~176 bytes each = ~2.8 MB. Adequate for 3DMark99 (10K+ commands/frame). The comment at line 131 accurately describes the sizing rationale.

- **Command union size**: Dominated by `vc_cmd_triangle_t` (3 vertices * 56 bytes = 168 bytes). The `vc_cmd_fog_upload_t` (128 bytes) is the next largest. All other payloads are significantly smaller. This means ~95% of ring entries waste ~100 bytes of union padding when carrying non-triangle commands. A tagged-pointer or separate ring design would be more memory-efficient, but the simplicity of the current union approach is appropriate for the data rates involved.

- **vc_thread_t layout**: The struct mixes atomics (`running`), render-thread-only state (`render_pass_begun`, `current_frame`, `last_*`), and shared state (`ring`). This is fine for SPSC where ownership is clear, but a comment separating "producer-side", "consumer-side", and "shared" fields would improve readability.

- **last_bound/last_pipeline/last_depth tracking** (lines 202-219): Exclusively accessed by the render thread (consumer). Correctly used for state restoration after emergency frame restart and swap boundaries. The `_valid` flags prevent stale re-binding.

- **VC_FRAMES_IN_FLIGHT = 2**: Standard double-buffering. The render thread waits on the fence before reusing a frame's resources, preventing GPU-side data races.

---

## vc_thread.c -- 1005 lines

### SPSC Ring Operations (Lines 36-69)

#### Memory Ordering Analysis

**vc_ring_is_full (lines 36-42) -- Called by producer:**
- `head`: `memory_order_relaxed` -- CORRECT. Only the producer writes head, so the producer always sees its own latest write.
- `tail`: `memory_order_acquire` -- CORRECT. Synchronizes-with consumer's `release` store on tail in `vc_ring_pop`. Ensures the producer sees the consumer's progress.

**vc_ring_is_empty (lines 44-50) -- Called by consumer:**
- `head`: `memory_order_acquire` -- CORRECT. Synchronizes-with producer's `release` store on head in `vc_ring_push`. Ensures the entry data written before the head advance is visible to the consumer.
- `tail`: `memory_order_relaxed` -- CORRECT. Only the consumer writes tail.

**vc_ring_push (lines 52-59) -- Called by producer:**
1. Load `head` with `relaxed` -- CORRECT. Producer owns head.
2. Write `entries[head] = *cmd` -- Non-atomic struct copy. Safe because no other thread reads this slot until head is advanced.
3. Store `head = (head+1) & MASK` with `release` -- CORRECT. The release ordering ensures the entry data write in step 2 is visible to any thread that subsequently loads head with acquire.

**vc_ring_pop (lines 61-69) -- Called by consumer:**
1. Load `tail` with `relaxed` -- CORRECT. Consumer owns tail.
2. Read `entries[tail]` -- Non-atomic struct copy. Safe because the producer will not write to this slot until tail is advanced past it.
3. Store `tail = (tail+1) & MASK` with `release` -- CORRECT. The release ordering ensures the producer sees the slot as available.

**Happens-before chain for data visibility:**
```
Producer:  write entries[head] --sequenced-before--> release store head
Consumer:  acquire load head --sequenced-before--> read entries[tail]
```
The acquire-on-head in `is_empty` synchronizes-with the release-on-head in `push`, creating a happens-before edge that guarantees the entry data is visible. VERIFIED CORRECT.

**ARM64 weak memory model**: The C11 acquire/release semantics compile to the correct ARM64 barriers:
- `release store` -> `STLR` (store-release)
- `acquire load` -> `LDAR` (load-acquire)
These provide the necessary ordering on ARM64's weakly-ordered memory model.

**ABA safety**: Not a concern. SPSC rings use separate head/tail indices with monotonically-advancing values (masked to ring size). No CAS operations, no pointer reuse, no ABA scenario possible.

**Ring full/empty conditions**: Correct. Uses `(head+1) & MASK == tail` for full (wastes one slot to distinguish full from empty). Uses `head == tail` for empty.

**Integer overflow**: Not possible. Indices are always masked to `[0, VC_RING_CAPACITY-1]`.

**Single-producer invariant**: VERIFIED. All callers of `vc_thread_push` trace back to the Voodoo FIFO thread:
- `vc_voodoo_submit_triangle` -> called from `voodoo_half_triangle` (FIFO thread)
- `vc_voodoo_swap_buffers` -> called from `voodoo_reg_writel` for `SST_swapbufferCMD` (FIFO thread)
- `vc_lfb_write_flush` -> called from submit_triangle and swap_buffers (FIFO thread)
- `vc_thread_close` -> called during shutdown (single-threaded teardown)
- `vc_thread_sync` -> called from `vc_sync` (exposed but currently unused externally)

The previously-identified bug of `vc_sync` being called from the timer thread has been fixed by using `vc_thread_wait_idle` instead.

### Issues Found

- [MODERATE] Lines 947-966: **`vc_thread_wait_idle` has a gap between ring drain and GPU completion.** The function spin-waits until `vc_ring_is_empty` returns true, then calls `vkDeviceWaitIdle`. However, after `vc_ring_pop` advances the tail (making the ring appear empty), the render thread may still be in the middle of dispatching the last command. If that command involves `vc_end_frame` (which records and submits a command buffer), `vkDeviceWaitIdle` is called BEFORE that submission happens. This means:

  1. Ring appears empty (tail advanced past last command).
  2. CPU thread calls `vkDeviceWaitIdle` -- returns immediately (no pending GPU work from the in-progress command).
  3. Render thread finishes dispatch, calls `vkQueueSubmit` -- GPU work starts AFTER `vkDeviceWaitIdle` returned.

  **Impact**: For LFB readback, the CPU thread then calls `vc_readback_execute` which does its own `vkQueueSubmit` + `vkWaitForFences` under `queue_mutex`. The render thread's pending submit also uses `queue_mutex`, so there IS implicit serialization. But the readback might read from the framebuffer BEFORE the render thread's latest draw commands are on the GPU.

  **Severity**: In practice this is mitigated because:
  (a) LFB reads are from the FRONT buffer, which was last written by a COMPLETED frame.
  (b) The render thread's in-progress work is on the BACK buffer.
  (c) The readback path's own `vkQueueSubmit` will block on `queue_mutex` if the render thread is submitting.

  However, if a game does an LFB read from the back buffer (which is supported -- `vc_readback_color_sync(ctx, 1)`), the gap could cause stale reads.

  **Suggested fix**: Add a "dispatch complete" atomic flag that the render thread sets after each dispatch batch. `vc_thread_wait_idle` should spin-wait for both `ring_is_empty` AND `dispatch_complete`. Alternatively, wait for the render thread to enter the idle sleep (`wait_event`) before proceeding.

- [MODERATE] Lines 806-811: **Wake event reset-before-check pattern allows up to 1ms latency.** The render thread resets the wake event AFTER waking up:
  ```c
  thread_wait_event(wake_event, 1);
  thread_reset_event(wake_event);
  continue;
  ```
  If the producer signals the event between the `ring_is_empty` check (line 806) and the `thread_wait_event` call (line 809), the event is already signaled, so wait returns immediately. No lost wakeup. The 1ms timeout provides a safety net. However, this means the render thread polls at 1ms intervals when idle -- 1000 spurious wakeups/second. This is benign on desktop but wasteful on battery-powered devices.

  **Suggested optimization**: Use a longer timeout (e.g., 10ms or 100ms) when idle. The event signal from the producer provides immediate wakeup for actual commands. The timeout only matters for shutdown detection (checking `running` flag), which is not latency-sensitive.

- [LOW] Lines 529-537: **Verbose per-clear log message** (`pclog` in `vc_dispatch_clear`). This logs EVERY clear operation, which in some games happens every frame. The log is not gated by `ENABLE_VIDEOCOMMON_LOG`. During normal operation with logging disabled, `pclog` is still called (just filtered by the log subsystem). Consider gating with `vc_log` or removing for release.

- [LOW] Line 655: **Verbose per-swap log message** (`pclog` in `VC_CMD_SWAP` handler). Same concern as above -- `pclog("VK SWAP: ...")` is called every frame, not gated by `ENABLE_VIDEOCOMMON_LOG`.

- [LOW] Line 332: **`frames_completed` is set to 1 unconditionally** via `atomic_store(&thread->ctx->frames_completed, 1)`. After the first frame, this is redundant (already 1). Minor inefficiency -- the atomic store has no ordering specification (defaults to `memory_order_seq_cst`), which is the most expensive on ARM64. Could use `atomic_store_explicit(..., memory_order_release)` or skip after first frame.

### Vulkan Correctness

**Render pass begin (lines 128-253):**
- Clear values in `VkRenderPassBeginInfo` are correctly specified but noted as fallback only (render pass uses `LOAD_OP_LOAD`). CORRECT.
- First-frame explicit clear via `vkCmdClearAttachments` (lines 165-189) -- CORRECT workaround for MoltenVK/Metal undefined initial contents with `LOAD_OP_LOAD`.
- Viewport/scissor set as dynamic state -- CORRECT.
- Default pipeline bind (blend disabled) -- CORRECT.
- Extended dynamic state defaults (depth test OFF, write OFF, compare ALWAYS) -- CORRECT as safe fallback. Per-triangle `VC_CMD_DEPTH_STATE` overrides before first draw.
- Placeholder descriptor set bind -- CORRECT.

**Render pass end (lines 255-264):**
- Guard prevents double-end. CORRECT.

**Frame begin (lines 270-302):**
- Fence wait/reset for frame reuse -- CORRECT. Fence created as signaled, so first wait succeeds.
- Command buffer reset + begin -- CORRECT. `ONE_TIME_SUBMIT` flag appropriate.
- Batch reset + descriptor pool reset -- CORRECT. Ensures clean state.

**Frame end (lines 304-337):**
- Batch flush before render pass end -- CORRECT.
- `vkEndCommandBuffer` -- CORRECT.
- `vkQueueSubmit` under `queue_mutex` -- CORRECT. Shared with Qt GUI thread.
- Fence passed to submit for frame completion tracking -- CORRECT.
- Framebuffer swap after submit -- CORRECT. Previous frame's GPU work continues on old back buffer while new frame draws to new back buffer.

**Command dispatch Vulkan correctness:**
- Push constants: Flush before update, correct stage flags (`VERTEX | FRAGMENT`), correct size (64 bytes). CORRECT.
- Clear: Flush before clear, correct `VkClearAttachment` setup. CORRECT.
- Pipeline key: Flush before bind, correct pipeline lookup and bind. CORRECT.
- Depth state: Flush before state change, correct EXT function calls. CORRECT.
- Texture upload: Calls `vc_texture_upload` then `free` -- CORRECT.
- Texture bind: Flush before bind, correct descriptor set allocation and bind. CORRECT.
- Fog upload: Calls `vc_texture_upload_fog` -- CORRECT.
- LFB flush: Ends render pass before transfer commands, flushes both color and depth, clears dirty flag. CORRECT.

**SWAP handler (lines 654-690):**
- `vc_end_frame` submits GPU work.
- Async readback initiated from front buffer after swap -- CORRECT.
- `vc_begin_frame` starts new frame.
- Last-bound tracking invalidated -- CORRECT. Prevents stale descriptor sets after pool reset.

**SYNC handler (lines 692-714):**
- `vc_end_frame` + wait for previous frame's fence -- CORRECT.
- Signals sync_event to unblock producer -- CORRECT.
- Starts new frame + invalidates tracking -- CORRECT.

**CLOSE handler (lines 818-834):**
- `vc_end_frame` + wait for final submission fence -- CORRECT.
- Sets `running = 0` and returns -- CORRECT.

### Thread Safety Analysis

**Producer-only state** (FIFO thread):
- All `vc_thread_push` calls originate from FIFO thread. VERIFIED.
- `vc_thread_sync` pushes CMD_SYNC + waits -- called from FIFO thread. VERIFIED.
- `vc_thread_close` pushes CMD_CLOSE + thread_wait -- called during shutdown. VERIFIED.

**Consumer-only state** (render thread):
- `current_frame`, `render_pass_begun`, `first_render_pass_done` -- only accessed in render thread. CORRECT.
- `last_bound_*`, `last_pipeline_key*`, `last_depth_state*` -- only accessed in render thread. CORRECT.
- Frame resources (`cmd_pool`, `cmd_buf`, `fence`) -- only accessed in render thread (after init). CORRECT.
- `batch` -- only accessed in render thread. CORRECT.

**Shared state:**
- `ring.head` -- written by producer, read by consumer. Protected by acquire/release. CORRECT.
- `ring.tail` -- written by consumer, read by producer. Protected by acquire/release. CORRECT.
- `ring.entries[]` -- written by producer to slot [head], read by consumer from slot [tail]. Mutually exclusive access guaranteed by head/tail protocol. CORRECT.
- `running` -- written by producer (`vc_thread_start`, `vc_thread_close`), read by consumer. Uses `_Atomic int`. CORRECT.
- `wake_event` -- signaled by producer, waited by consumer. Thread-safe (pthread mutex/cond internally). CORRECT.
- `sync_event` -- signaled by consumer, waited by producer. Thread-safe. CORRECT.
- `ctx` -- read-only pointer set during init, used by both threads. CORRECT (no mutation).
- `ctx->queue_mutex` -- protects `vkQueueSubmit`. Used by render thread (end_frame) and GUI thread (present). CORRECT.

**`vc_thread_wait_idle`** (called from CPU thread):
- Reads `ring.head` and `ring.tail` -- both atomic, safe from any thread.
- Signals `wake_event` -- thread-safe.
- Calls `vkDeviceWaitIdle` -- thread-safe per Vulkan spec.
- Does NOT access any consumer-only state. CORRECT.

### Resource Management

**Frame resources lifecycle:**
1. Created in `vc_thread_init` (before thread start). CORRECT.
2. Used exclusively by render thread during operation. CORRECT.
3. Destroyed in `vc_thread_close` (after thread join). CORRECT.
4. `memset(fr, 0, ...)` in destroy -- prevents dangling handle access. CORRECT.

**Vertex batch lifecycle:**
1. Created in `vc_thread_init`. CORRECT.
2. Used exclusively by render thread. CORRECT.
3. Destroyed in `vc_thread_close`. CORRECT.

**Event lifecycle:**
1. Created in `vc_thread_init`. CORRECT.
2. Destroyed in `vc_thread_close` (after thread join). CORRECT.

**Cleanup on init failure** (lines 872-893):
- If frame resource creation fails, calls `vc_thread_close` which destroys already-created resources. CORRECT.
- If batch init fails, calls `vc_thread_close`. CORRECT.
- If event creation fails, calls `vc_thread_close`. CORRECT.
- `vc_thread_close` handles partially-initialized state via NULL checks. CORRECT.

### Error Handling

- **`vc_begin_frame` failure** (line 798-802): If `vkBeginCommandBuffer` fails, sets `running = 0` and returns from thread. The render thread exits. No further commands are processed. The producer will spin forever in `vc_ring_is_full` if the ring fills up. This is a fatal error scenario -- acceptable to hang since the GPU is unusable.

- **`vkQueueSubmit` failure** (line 328-329): Logged but execution continues. The fence is never signaled, so `vkWaitForFences` on the next frame reuse will hang forever. This is acceptable for a fatal GPU error.

- **Pipeline lookup failure** (lines 220, 446, 570, etc.): If `vc_pipeline_lookup` returns `VK_NULL_HANDLE`, the pipeline bind is skipped. Subsequent draw calls will use whatever pipeline was previously bound. This is a graceful degradation -- the wrong pipeline is better than a crash.

- **Descriptor set allocation failure** (lines 246, 382): If `vc_texture_bind` returns `VK_NULL_HANDLE`, the bind is skipped. The placeholder (or previously bound set) remains active. This is the descriptor pool exhaustion scenario previously identified and fixed by increasing pool size.

### Emergency Frame Restart (Lines 425-468)

The emergency path when vertex ring overflows is complex but correct:

1. `vc_end_frame` -- Submits current work to GPU. CORRECT.
2. `vc_begin_frame` -- Resets vertex batch to offset 0, resets descriptor pool. CORRECT.
3. `vc_begin_render_pass` -- Sets up fresh render pass with defaults. CORRECT.
4. Re-issue last pipeline key -- Flushes empty batch (no-op), binds correct pipeline. CORRECT.
5. Re-issue last depth state -- Calls `vc_dispatch_depth_state` which handles all depth dynamic state + color write mask pipeline variant. CORRECT.
6. Re-issue last texture bind -- Creates new descriptor set from re-uploaded textures, binds it. CORRECT.
7. Retry triangle push -- Should succeed since batch was reset to offset 0. CORRECT.

State restoration order matters: pipeline first, then depth (may trigger pipeline rebind for color write mask), then textures. This is the correct order because depth state dispatch may look up a new pipeline variant based on `last_pipeline_key`.

### Summary

| Severity | Count | Description |
|----------|-------|-------------|
| CRITICAL | 0 | -- |
| HIGH | 0 | -- |
| MODERATE | 2 | wait_idle gap, wake event polling interval |
| LOW | 3 | verbose logging, redundant atomic store, missing ownership comments |

The SPSC ring implementation is **correct and well-designed**. The C11 atomic memory ordering is precise and appropriate for ARM64. The single-producer invariant is maintained by all callers. The ring full/empty conditions are textbook correct. The command dispatch handles all state transitions properly, including the complex emergency frame restart path.

The most notable finding is the `vc_thread_wait_idle` gap (MODERATE), which could theoretically cause stale back-buffer reads but is mitigated by the typical usage pattern (reading from front buffer) and queue_mutex serialization.
