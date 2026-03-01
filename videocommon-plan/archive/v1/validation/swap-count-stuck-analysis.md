# swap_count Stuck at 3 in VK Mode -- Root Cause Analysis

## Symptom

3DMark99 freezes at test 8-9 transition. Guest polls SST_status, sees
swap_count=3 and busy=1, and spins forever. The swap_count never
decrements.

## Architecture Review

### swap_count Lifecycle (Normal Operation)

```
CPU thread                      FIFO thread                     Timer thread
-----------                     -----------                     ------------
Guest writes SST_swapbufferCMD
  -> voodoo_writel()
  -> addr 0x114 (register)
  -> swap_count++
  -> if CMDFIFO: return
                                CMDFIFO processing:
                                cmdfifo_get() reads packet
                                voodoo_cmdfifo_reg_writel()
                                  -> voodoo_reg_writel()
                                    -> vc_voodoo_swap_buffers()
                                    -> swap_pending = 1
                                    -> voodoo_wait_for_swap_complete()
                                       [BLOCKS, polling 1ms]
                                                                voodoo_callback()
                                                                  line == v_disp:
                                                                  retrace_count++
                                                                  swap_pending &&
                                                                    retrace_count >
                                                                    swap_interval:
                                                                  swap_count--
                                                                  swap_pending = 0
                                                                  wake_fifo_thread
                                [unblocks, continues]
                                cmd_read++
```

### Key Increment/Decrement Points

**swap_count++ (CPU thread, vid_voodoo.c:704)**
Happens IMMEDIATELY when guest writes to SST_swapbufferCMD register.
With CMDFIFO enabled, the function returns after the increment
without queuing to the register FIFO. The swap command is separately
present in the CMDFIFO buffer for the FIFO thread to process.

**swap_count-- (three paths)**
1. **Display callback** (vid_voodoo_display.c:720): Requires
   `swap_pending == 1 && retrace_count > swap_interval`. Fires once
   per frame when `voodoo->line == voodoo->v_disp`.
2. **Immediate swap** (vid_voodoo_reg.c:210): When `!(val & 1)` (no
   vsync sync). Decrements immediately on the FIFO thread.
3. **Emergency drain** (vid_voodoo_fifo.c:277): When
   `(swap_pending && flush) || FIFO_FULL`. Only fires if CPU thread
   is flushing the register FIFO or it is full.

## Root Cause: FIFO Thread Blocked Before swap_pending Assignment

The root cause is that the FIFO thread gets blocked in
`vc_thread_push()` (the SPSC ring push) while processing triangle
commands that PRECEDE the swap command in the CMDFIFO. Because it
never reaches the SST_swapbufferCMD handler, `swap_pending` is never
set to 1, and the display callback has nothing to clear.

### Detailed Chain of Events

1. **CPU thread writes 3 SST_swapbufferCMD** to the register address
   (0x114). Each write does `swap_count++` then returns immediately
   (CMDFIFO mode). swap_count = 3.

2. **CPU thread also writes CMDFIFO data** including triangle commands
   and swap commands to the CMDFIFO buffer (address 0x200000+). The
   CMDFIFO contains: [triangles...] [swap] [triangles...] [swap]
   [triangles...] [swap].

3. **FIFO thread processes the CMDFIFO**. It encounters triangle
   commands first. Each triangle calls
   `vc_voodoo_submit_triangle()`, which pushes multiple commands to
   the VideoCommon SPSC ring (vertices, push constants, pipeline key,
   texture bind, depth state, draw -- up to ~10 ring entries per
   triangle).

4. **VK render thread falls behind**. During heavy scenes (test
   transitions), the render thread is blocked in `vc_begin_frame()`
   waiting on `vkWaitForFences()` (line 276 of vc_thread.c) for the
   GPU to complete the previous frame. While blocked, the render
   thread does not drain the SPSC ring.

5. **SPSC ring fills up**. With 16384 entries and ~10 entries per
   triangle, the ring fills after ~1600 triangles. Once full,
   `vc_thread_push()` enters a spin loop (line 977 of vc_thread.c):
   ```c
   while (vc_ring_is_full(&thread->ring)) {
       thread_set_event(thread->wake_event);
       plat_delay_ms(0);  // sched_yield
   }
   ```

6. **FIFO thread is stuck in `vc_thread_push()`**. It is inside the
   triangle processing for a CMDFIFO type 3/4 packet (vertex data),
   or a type 1 packet (register write for triangleCMD). It has NOT
   yet reached the SST_swapbufferCMD entry in the CMDFIFO.

7. **swap_pending is 0**. The FIFO thread never set it because it
   never reached the swap handler.

8. **Display callback fires but does nothing**. At v_disp,
   `voodoo_callback()` checks `swap_pending && retrace_count >
   swap_interval`. Since swap_pending = 0, the check fails.
   swap_count is NOT decremented.

9. **Guest polls SST_status**. Sees swap_count = 3, busy = 1
   (because `cmdfifo_depth_rd != cmdfifo_depth_wr` and
   `voodoo_busy = 1`). Loops forever.

### Why This Doesn't Happen in SW Mode

In software rendering mode:
- `voodoo_queue_triangle()` queues to the SW render thread's param
  buffer (PARAM_FULL check, very fast)
- SW render threads consume params quickly (CPU-only, no GPU waits)
- The FIFO thread never blocks on ring push
- It always reaches the swap command promptly
- swap_pending gets set, display callback clears it, swap_count drains

### Why This Surfaces at Test 8-9 Transitions

During benchmark transitions, 3DMark99:
- Renders a complex "outro" scene for the current test
- Submits several frames rapidly (multiple swapbufferCMDs)
- The scenes may have high triangle counts
- The GPU may be processing the previous frame's complex geometry
- Frame N-1 is still on the GPU when frame N+1 tries to begin
- `vkWaitForFences` blocks, ring fills, FIFO thread stalls

## Proposed Fix

### Primary Fix: Decouple swap_count from FIFO Thread Processing

The fundamental problem is that `swap_count++` happens immediately on
the CPU thread, but `swap_count--` depends on the FIFO thread
reaching the swap handler (to set swap_pending) AND the display
callback firing (to clear swap_pending). If the FIFO thread is
delayed for any reason, swap_count stays elevated.

**Option A: Move swap_pending and swap_count-- into the VK swap path
directly, bypassing voodoo_wait_for_swap_complete**

In `vid_voodoo_reg.c`, the SST_swapbufferCMD handler, when in VK
mode, should handle swap_count management differently. The VK path
does not need to wait for vsync via the display callback -- the VK
render thread handles buffer swapping asynchronously. The fix:

```c
// In voodoo_reg_writel(), SST_swapbufferCMD handler:
#ifdef USE_VIDEOCOMMON
            if (voodoo->use_gpu_renderer
                && atomic_load_explicit(&voodoo->vc_ctx, memory_order_acquire)) {
                vc_voodoo_swap_buffers(voodoo);

                /* VK path: decrement swap_count immediately.
                 * The VK render thread handles actual buffer swap
                 * asynchronously.  We don't need swap_pending / display
                 * callback flow control because the SPSC ring provides
                 * its own back-pressure.  Blocking the FIFO thread in
                 * voodoo_wait_for_swap_complete is both unnecessary and
                 * harmful (it creates a dependency chain that can deadlock
                 * with the ring-full condition). */
                thread_wait_mutex(voodoo->swap_mutex);
                if (voodoo->swap_count > 0)
                    voodoo->swap_count--;
                thread_release_mutex(voodoo->swap_mutex);
                voodoo->cmd_read++;

                /* Still need buffer flip and recalc. */
                if (TRIPLE_BUFFER) {
                    voodoo->disp_buffer = ...;
                    voodoo->draw_buffer = ...;
                } else {
                    voodoo->disp_buffer = !voodoo->disp_buffer;
                    voodoo->draw_buffer = !voodoo->draw_buffer;
                }
                voodoo_recalc(voodoo);
                voodoo->params.swapbufferCMD = val;
                memset(voodoo->dirty_line, 1, sizeof(voodoo->dirty_line));
                voodoo->front_offset = voodoo->params.front_offset;
                voodoo->frame_count++;
                break;  // Skip the SW swap_pending path entirely
            }
#endif
```

This makes the VK swap handler fully self-contained: push the VK
swap command, decrement swap_count, flip buffers, and continue. No
blocking, no swap_pending, no dependency on the display callback.

The key insight is that `voodoo_wait_for_swap_complete()` is a
**vsync throttle** for the SW renderer. The VK path has its own
throttling via `vkWaitForFences` in `vc_begin_frame`. Using both
throttles creates the deadlock-prone dependency chain.

**Option B: Skip swap_pending entirely in VK mode (minimal change)**

If Option A is too invasive, a simpler approach: in the
SST_swapbufferCMD handler, after calling `vc_voodoo_swap_buffers()`,
skip the `swap_pending = 1` / `voodoo_wait_for_swap_complete()`
path and just do the immediate `swap_count--` path:

```c
#ifdef USE_VIDEOCOMMON
            if (voodoo->use_gpu_renderer
                && atomic_load_explicit(&voodoo->vc_ctx, memory_order_acquire)) {
                vc_voodoo_swap_buffers(voodoo);
                /* Immediate swap_count-- in VK mode: don't wait for
                 * display callback.  The VK render thread handles
                 * vsync-like throttling via fence waits. */
                thread_wait_mutex(voodoo->swap_mutex);
                if (voodoo->swap_count > 0)
                    voodoo->swap_count--;
                thread_release_mutex(voodoo->swap_mutex);
                memset(voodoo->dirty_line, 1, sizeof(voodoo->dirty_line));
                voodoo->front_offset = voodoo->params.front_offset;
                voodoo->frame_count++;
            }
#endif
```

Then let the existing buffer flip and cmd_read++ code run normally.
The critical change is: `swap_count--` happens immediately on the
FIFO thread when it processes the swap, instead of waiting for the
display callback.

### Secondary Fix: Ring Back-Pressure Safety

Even with the primary fix, the FIFO thread can still block in
`vc_thread_push()` when the ring is full. To prevent this from
causing other issues:

1. **Increase ring batch limit awareness**: When the ring is nearly
   full, the FIFO thread should yield to let the render thread
   catch up, rather than blocking indefinitely.

2. **Add ring fullness diagnostic**: Log when the ring is more than
   75% full to detect back-pressure issues early.

## Files to Modify

| File | Change |
|------|--------|
| `src/video/vid_voodoo_reg.c:168-227` | VK path: skip swap_pending, do immediate swap_count-- |
| `src/video/vid_voodoo_display.c:715-726` | (Optional) Skip swap_pending check in VK mode to avoid stale state |

## Verification Plan

1. Build with fix, run 3DMark99 through all tests
2. Monitor diagnostic logs: swap_count should oscillate 0-1 (not accumulate to 3)
3. Verify FIFO thread never blocks on swap_pending in VK mode
4. Verify frame pacing is acceptable (no tearing, no stutter)
5. Verify SW renderer path is unchanged (regression test)

## Risk Assessment

- **Low risk**: The fix only changes the VK path (guarded by
  `use_gpu_renderer && vc_ctx`). SW rendering is completely
  unaffected.
- **Frame pacing**: Without vsync throttling via swap_pending, the
  guest can submit frames faster than the GPU can render them. The
  VK render thread's `vkWaitForFences` provides natural throttling
  (2 frames in flight), and the SPSC ring provides back-pressure.
  This should be sufficient.
- **Buffer flip correctness**: The disp_buffer/draw_buffer toggle
  must still happen for the display callback to scan out the correct
  buffer. The proposed fix preserves this.
