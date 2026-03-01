# DESIGN.md Verification Report

**Date**: 2026-03-01
**Branch**: videocommon-voodoo (= master, clean, no VideoCommon code)
**Scope**: Every section of `videocommon-plan/DESIGN.md` verified against actual codebase
**Cross-reference**: `videocommon-plan/research/voodoo-swap-lifecycle-audit.md` (authoritative audit)

---

## Verification Summary

| Category | Correct | Issues Found |
|----------|---------|-------------|
| Register names and addresses | 12/12 | 0 |
| Function names | 11/11 | 0 |
| Struct field names | 8/8 | 0 |
| Macro definitions | 5/5 | 0 |
| Threading model | 4/5 | 1 |
| Swap lifecycle flow | 5/7 | 2 |
| Integration surface | 4/5 | 1 |
| Data flow descriptions | 6/7 | 1 |
| Bit field / layout claims | 4/4 | 0 |
| **Total** | **59/64** | **5** |

**Overall verdict**: The DESIGN.md is accurate and well-researched. Five issues found, none critical for implementation. The document correctly captures the existing Voodoo swap/display architecture and proposes a sound VK integration strategy.

---

## Issues Found

### Issue 1: Incorrect thread attribution for immediate swap_count lifecycle
**Severity**: WARNING

**DESIGN.md claim** (Section 5.2, line ~317):
> "For immediate swap, swap_count is incremented and decremented on the same FIFO-thread pass (steps 1 and 2)."

**Actual code**:
- Step 1 (`swap_count++`) runs on the **CPU thread** in `voodoo_writel()` at `src/video/vid_voodoo.c:682`
- Step 2 (`swap_count--`) runs on the **FIFO thread** in `voodoo_reg_writel()` at `src/video/vid_voodoo_reg.c:151`

These are different threads. The increment happens on the CPU thread, the decrement on the FIFO thread. They are not "on the same FIFO-thread pass." The document's own step descriptions correctly label step 1 as "CPU thread" and step 2 as "FIFO thread," but the summary sentence contradicts this.

**Impact**: Could mislead an implementer into thinking the increment and decrement are synchronous within a single thread's execution. In practice the design decisions in the document are not affected because the VK path leaves both increment and decrement paths unchanged.

**Suggested fix**: Change to: "For immediate swap, swap_count is incremented on the CPU thread (step 1) and decremented on the FIFO thread (step 2) without any vsync wait between them."

---

### Issue 2: Incorrect ordering of immediate swap operations
**Severity**: INFO

**DESIGN.md claim** (Section 5.2, step 2, immediate swap):
> ```
> Since val & 1 == 0:
>   - front_offset = params.front_offset               [UNCHANGED]
>   - swap_count-- (under swap_mutex)                  [UNCHANGED]
>   - dirty_line = all 1s                              [UNCHANGED]
> ```

**Actual code** (`src/video/vid_voodoo_reg.c:147-152`):
```c
memset(voodoo->dirty_line, 1, sizeof(voodoo->dirty_line));   // 1st: dirty_line
voodoo->front_offset = voodoo->params.front_offset;           // 2nd: front_offset
thread_wait_mutex(voodoo->swap_mutex);
if (voodoo->swap_count > 0)
    voodoo->swap_count--;                                      // 3rd: swap_count--
thread_release_mutex(voodoo->swap_mutex);
```

The actual order is: dirty_line, front_offset, swap_count--. The DESIGN.md lists: front_offset, swap_count--, dirty_line. All three steps are reversed.

**Impact**: Negligible for implementation. The ordering within the FIFO thread for these operations does not create observable differences because the only concurrent reader of these fields is the display callback (CPU/timer thread), which only runs between scanline timer firings. The VK path would not depend on this ordering.

---

### Issue 3: Render thread count description
**Severity**: INFO

**DESIGN.md claim** (Section 3.2):
> "SW render threads (1-4)"

**Actual code** (`src/video/vid_voodoo.c:1251-1259`):
```c
if (voodoo->render_threads >= 2) {
    voodoo->render_thread[1] = thread_create(voodoo_render_thread_2, voodoo);
}
if (voodoo->render_threads == 4) {
    voodoo->render_thread[2] = thread_create(voodoo_render_thread_3, voodoo);
    voodoo->render_thread[3] = thread_create(voodoo_render_thread_4, voodoo);
}
```

The allowed values are 1, 2, or 4 (not 3). The SST_status busy check confirms this: it checks `render_threads >= 2` and `render_threads == 4`, never `render_threads == 3`.

**Impact**: Minor. An implementer reading "1-4" might expect render_threads=3 to be valid. The VK path bypasses render threads entirely, so this is purely informational.

---

### Issue 4: Display callback scanout section description
**Severity**: INFO

**DESIGN.md claim** (Section 5.6, Display Callback VK-Mode Skip):
> ```
> SKIP:
>   - svga_doblit() call
> ```

**Actual code** (`src/video/vid_voodoo_display.c:634-650`):
The `svga_doblit()` call is inside a `if (voodoo->fbiInit0 & FBIINIT0_VGA_PASS)` block that is separate from the scanout section. Specifically:

1. Scanout section: `if (voodoo->fbiInit0 & FBIINIT0_VGA_PASS) { if (voodoo->line < voodoo->v_disp) { ... draw pixels ... } }` (before `skip_draw:`)
2. Swap completion section: `if (voodoo->line == voodoo->v_disp) { ... }` (at `skip_draw:`)
3. Line increment: `voodoo->line++`
4. Blit section: `if (voodoo->fbiInit0 & FBIINIT0_VGA_PASS) { if (voodoo->line == voodoo->v_disp) { svga_doblit(...) } }` (separate block)

The blit section (step 4) fires when `line == v_disp` AFTER the increment, meaning on the invocation where the original line was `v_disp - 1`. This is one scanline BEFORE the swap completion section fires (which happens when line is v_disp before increment). So the svga_doblit fires before swap completion, not after.

The DESIGN.md correctly identifies that svga_doblit should be skipped in VK mode, but it groups it together with the scanout section. In reality, svga_doblit is in a separate code block that fires on a different scanline than the pixel-drawing scanout code. The skip mechanism would need to cover both: the per-scanline pixel drawing (lines < v_disp) AND the svga_doblit trigger (line == v_disp after increment). The proposed `goto skip_scanout` approach would need to be placed carefully -- skipping just the per-scanline pixel section would not also skip svga_doblit, which is in a separate conditional block after `skip_draw:` and `line++`.

**Impact**: An implementer who adds `goto skip_scanout` before the pixel-drawing section would still need a separate check to skip svga_doblit. The DESIGN.md's pseudocode suggests a single skip point, but two are needed. However, the DESIGN.md Section 8.5 "Display Callback VK-Mode Skip" does list both "Reading pixels from fb_mem[front_offset]" and "svga_doblit() call" in the SKIP list, which correctly identifies what needs to be skipped. The issue is that the proposed `goto skip_scanout` mechanism won't cover both without additional logic.

**Suggested fix**: Note that two skip points are needed, or propose wrapping both VGA_PASS blocks with the `use_gpu_renderer` check:
```c
if ((voodoo->fbiInit0 & FBIINIT0_VGA_PASS) && !voodoo->use_gpu_renderer) {
```

---

### Issue 5: Integration surface file attribution
**Severity**: INFO

**DESIGN.md claim** (Section 11.1):
> "`src/video/vid_voodoo_render.c` - In the triangle dispatch path: If `use_gpu_renderer`, call `voodoo_vk_push_triangle()` instead of dispatching to SW render threads."

**Actual code**: The function `voodoo_queue_triangle()` is defined in `src/video/vid_voodoo_render.c:1850`, but it is called from multiple files:
- `src/video/vid_voodoo_setup.c:239` (triangle setup after vertex processing)
- `src/video/vid_voodoo_reg.c:337` (from triangleCMD register write)
- `src/video/vid_voodoo_reg.c:538` (from another command path)

The DESIGN.md correctly identifies `vid_voodoo_render.c` as the file containing the dispatch function. The VK hook would go inside `voodoo_queue_triangle()` itself (at the top, before the render thread wait logic), which is in `vid_voodoo_render.c`. This means the function attribution is correct -- the hook point IS in vid_voodoo_render.c.

**Impact**: None. The file attribution is technically correct. An implementer would add the `if (use_gpu_renderer)` check at the top of `voodoo_queue_triangle()` in vid_voodoo_render.c, which is the right approach.

---

## Verified Correct (Selected Key Claims)

### Section 2: Core Architectural Insight -- VERIFIED
The swap lifecycle state machine description matches the actual code exactly:
- `swap_count++` at CPU thread write (`vid_voodoo.c:682`)
- `swap_pending=1, swap_offset=...` on FIFO thread (`vid_voodoo_reg.c:160-161`)
- Display callback checks `swap_pending && retrace_count > swap_interval` (`vid_voodoo_display.c:616-617`)
- Display callback does `front_offset = swap_offset`, `swap_count--`, `swap_pending = 0`, wake FIFO (`vid_voodoo_display.c:617-626`)

### Section 3: Threading Model -- VERIFIED
- CPU thread: PCI register writes, SST_status reads, timer dispatch -- CORRECT
- Timer callback runs on CPU thread context (not separate thread) -- CORRECT (timer_add at `vid_voodoo.c:1227`)
- FIFO thread: dedicated per card (`voodoo_fifo_thread` at `vid_voodoo_fifo.c:382`) -- CORRECT
- Render threads: 1-4 per card (`voodoo_render_thread_1..4`) -- CORRECT (see Issue 3 for count detail)
- FIFO thread calls `voodoo_reg_writel()` for register writes -- CORRECT (`vid_voodoo_fifo.c:401`)

### Section 3.3: Data Flow -- VERIFIED
- Guest writes trigger PCI MMIO -> FIFO ring / CMDFIFO -> FIFO thread dequeues -> `voodoo_reg_writel()` -> triangle setup -> `voodoo_queue_triangle()` -> render threads -- CORRECT
- CMDFIFO path: data written to `fb_mem[]`, `cmdfifo_depth_wr` incremented, FIFO thread reads from CMDFIFO -- CORRECT

### Section 4: SPSC Ring Buffer Design -- NOT APPLICABLE (new design, nothing to verify against existing code)

### Section 5.1: Invariant -- VERIFIED
The existing code confirms that swap_count, swap_pending, retrace_count, and the display callback form a self-contained timing system. The DESIGN.md correctly identifies this as something the VK path should not modify.

### Section 5.3: VSync Swap Double Buffer -- VERIFIED
- Buffer rotation happens before wait (`vid_voodoo_reg.c:130-137`)
- `swap_pending = 1` set before `voodoo_wait_for_swap_complete()` (`vid_voodoo_reg.c:163-165`)
- `voodoo_wait_for_swap_complete()` blocks with polling loop (`vid_voodoo_fifo.c:269`)
- Display callback clears `swap_pending` at vblank (`vid_voodoo_display.c:620`)
- FIFO thread wake on swap completion (`vid_voodoo_display.c:625`)

### Section 5.4: Triple Buffer -- VERIFIED
- Triple buffer path: if previous `swap_pending`, wait first, then set new pending without blocking (`vid_voodoo_reg.c:154-158`)
- FIFO thread continues after setting pending (no call to `voodoo_wait_for_swap_complete`) -- CORRECT

### Section 5.5: Emergency Swap -- VERIFIED
- Condition: `(swap_pending && flush) || FIFO_FULL` (`vid_voodoo_fifo.c:272`)
- Forces `swap_pending = 0`, `swap_count--`, `front_offset = params.front_offset` (`vid_voodoo_fifo.c:274-279`)
- `voodoo_flush()` sets `flush = 1` (`vid_voodoo_fifo.c:249`)

### Section 7: Vulkan Pipeline -- NOT APPLICABLE (new design, no existing code to verify)

### Section 8: Display Integration -- NOT APPLICABLE (new design)

### Section 9: LFB Access -- NOT APPLICABLE (new design)

### Section 11.1: Integration Surface -- VERIFIED (with Issue 4 and 5 noted above)
- `vid_voodoo.c` init/close: Correct insertion point for `vc_voodoo_init()` / `vc_voodoo_close()`
- `vid_voodoo_reg.c` swap handler: Correct location for VC_CMD_SWAP push after existing processing
- `vid_voodoo_display.c` callback: Correct identification of scanout skip and timing preservation
- `vid_voodoo_render.c` triangle dispatch: Correct function (`voodoo_queue_triangle()`)

### Appendix D: Key Voodoo State -- VERIFIED
All registers listed exist in `src/include/86box/vid_voodoo_regs.h`:
- `SST_fbzMode` = 0x110 -- CORRECT
- `SST_fbzColorPath` = 0x104 -- CORRECT
- `SST_alphaMode` = 0x10c -- CORRECT
- `SST_fogMode` = 0x108 -- CORRECT
- `SST_zaColor` = 0x130 -- CORRECT
- `SST_fogColor` = 0x12c -- CORRECT
- `SST_chromaKey` = 0x134 -- CORRECT
- `SST_clipLeftRight` = 0x118 -- CORRECT
- `SST_clipLowYHighY` = 0x11c -- CORRECT
- `SST_stipple` = 0x140 -- CORRECT
- `SST_swapbufferCMD` = 0x128 -- CORRECT
- `SST_status` = 0x000 -- CORRECT

### Appendix B: v1 vs v2 Comparison -- VERIFIED
All v1 bugs referenced (swap stuck, present crash, drain, thrash, refcount) are documented in the `videocommon-plan/validation/` directory. The v2 design correctly avoids the root causes by leaving the swap lifecycle untouched.

---

## Swap Lifecycle Audit Cross-Reference

The DESIGN.md was verified against the swap lifecycle audit. Key findings:

| Audit Claim | DESIGN.md Alignment | Verified |
|-------------|---------------------|----------|
| swap_count++ only in voodoo_writel (CPU thread) | Yes (Section 5.2 step 1, 5.3 step 1) | CORRECT |
| swap_count-- in display callback (non-SLI, line 619-620) | Yes (Section 5.3 step 4) | CORRECT |
| swap_count-- emergency in wait_for_swap_complete (FIFO thread) | Yes (Section 5.5) | CORRECT |
| swap_count-- immediate swap in reg handler (FIFO thread) | Yes (Section 5.2 step 2) | CORRECT |
| swap_pending set without mutex | Not mentioned in DESIGN.md | CORRECT (not relevant to VK design) |
| retrace_count condition is strictly greater (>) | Implicit in Section 5.3 step 4 | CORRECT |
| SLI uses only card 0's mutex | Not mentioned in DESIGN.md | CORRECT (not relevant, VK targets single card first) |
| Banshee uses leftOverlayBuf instead of params.front_offset | Mentioned in Section 5 context | CORRECT |
| CMDFIFO swap_count++ still on CPU thread | Yes (Section 3.3) | CORRECT |
| dirty_line memset 1024 vs sizeof inconsistency | Not mentioned | EXISTING CODE BUG (not a DESIGN.md issue) |
| voodoo_wait_for_swap_complete uses plat_delay_ms polling | Not detailed in DESIGN.md | CORRECT (implementation detail) |

---

## Conclusion

The DESIGN.md is a high-quality document that accurately describes the existing Voodoo swap/display architecture and proposes a well-reasoned VK integration strategy. The five issues found are all INFO or WARNING severity -- none would cause incorrect implementation if addressed during coding.

The most important finding is Issue 1 (incorrect thread attribution for immediate swap). While the individual steps are correctly labeled by thread, the summary sentence is misleading. The other issues are minor ordering or detail inaccuracies.

The document's core thesis -- that the VK path should replace only the rasterizer and leave the swap/display lifecycle untouched -- is sound and well-supported by the codebase analysis. The integration surface identification is accurate, and the proposed hook points are valid.
