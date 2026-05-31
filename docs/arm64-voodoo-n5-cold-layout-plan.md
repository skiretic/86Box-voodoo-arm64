# ARM64 Voodoo N5 Cold-Path Generated Block Layout Plan

Status: Slice 3 S-clamp duplicate cold layout accepted
Source head audited: `7d3714784 Move ARM64 Voodoo S-wrap edge cold`
Scope: ARM64-local generated layout only

Progress:

- Done: initial N5 design-only audit and risk-ranked plan.
- Done: Slice 0 metric-only probe code added.
- Done: build/sign for Slice 0.
- Done: metrics-off short verify passed.
- Done: metrics-on short verify passed and emitted N5 metrics.
- Done: Slice 1 patch-to-target infrastructure added; no cold block movement.
- Done: Slice 1 build/sign.
- Done: Slice 1 short verify passed.
- Rejected: first Slice 2 attempt kept S-wrap inline and only added a branch; reverted.
- Done: Slice 1b cold-tail queue plumbing added; queue empty, no cold movement.
- Done: Slice 2 S-wrap real cold-tail emission; hot normal path falls through to join and S-edge wrap tail drains after generated `RET`.
- Done: Slice 2 build/sign.
- Done: Slice 2 short verify passed.
- Done: Slice 2 strong soak passed with metrics enabled.
- Done: Slice 3 S-clamp duplicate texel-load block moved to cold tail.
- Done: Slice 3 build/sign.
- Done: Slice 3 short verify passed with metrics enabled.
- Done: Slice 3 long metrics verify passed with S-clamp low/high coverage on TMU0.
- Accepted: TMU0 low/high dynamic coverage proves the shared S-clamp cold block; TMU1 post-change coverage is a low residual risk.

## Ground Rules

- Do not implement additional cold-layout slices from this document until metrics prove a cold-layout win.
- Do not use x86-64 codegen as semantic truth.
- Interpreter truth is:
  - `src/video/vid_voodoo_render.c:215-245` (`tex_read()`)
  - `src/video/vid_voodoo_render.c:250-291` (`tex_read_4()`)
  - `src/video/vid_voodoo_render.c:293-382` (`voodoo_get_texture()`)
  - `src/video/vid_voodoo_render.c:384-422` (`voodoo_tmu_fetch()`)
- Do not add helper-backed dynarec paths. Any future work remains native emitted ARM64 code.
- Preserve unrelated worktree changes.
- Build/sign and VM validation are required after implementation-stage source edits.

## Current Baseline Correction

The older N5 candidate text still says N5 needs N1 and N2 first. That prerequisite is now satisfied in the current history.

- N2 metrics exist at `ff4bd83e8 Add ARM64 Voodoo JIT metrics`.
- N1 texture-address emit refactor exists across:
  - `d84458260 Refactor ARM64 Voodoo texture address emits`
  - `fcd2018b7 Reuse ARM64 Voodoo bilinear S mask`
  - `2f05d0351 Refactor ARM64 Voodoo point texture emits`
  - `40ce20bf8 Name ARM64 Voodoo texture shift emits`
- Current head is after further N3/N4 work:
  - `24a16ad74 Factor ARM64 Voodoo TMU helper emits`

This means N5 is no longer blocked on N1/N2 existence. The metric gate has now been crossed for S-wrap only; remaining cold-layout candidates need separate proof.

## Readiness Decision

N5 readiness: ready only for the already-implemented S-wrap slice.

Design readiness: ready for a commit-ready review of the S-wrap slice. Further cold layout needs a separate proof decision.

Reason:

- Patch-to-target helpers and cold-tail queue now exist.
- S-wrap moved out of line and passed short verify plus strong soak.
- Long-run metrics show S-wrap remains a plausible cold-layout win.
- S-clamp now has coverage, but should remain a separate slice because current S-wrap changes are already large enough to commit/review alone.
- The dither pointer materialization candidate is not truly a runtime-cold fallback; it is a compile-time mode path that runs per pixel when emitted.
- T-edge remains too hot for cold-layout treatment.

## Candidate Risk Ranking

### 1. Bilinear S-Wrap Edge

Risk: medium
Recommended first implementation candidate if metrics justify N5.

Exact region:

- `src/include/86box/vid_voodoo_codegen_arm64.h:1889-1921`

Current shape:

- `CMP w4, w16`
- `B.EQ` placeholder to inline wrap block
- hot normal path:
  - `LSL w4, w4, #2`
  - `LDR d0, [x14, x4]`
  - `LDR d1, [x13, x4]`
  - `B normal_done`
- inline wrap block:
  - load row0[S]
  - load row0[0]
  - insert wrapped texel into `v0.S[1]`
  - load row1[S]
  - load row1[0]
  - insert wrapped texel into `v1.S[1]`
- join before `src/include/86box/vid_voodoo_codegen_arm64.h:1931`

Cold-layout design:

- Hot path remains straight-line normal adjacent load.
- `B.EQ` branches to `cold_s_wrap`.
- `cold_s_wrap` emits current `1907-1919`.
- `cold_s_wrap` branches back to the join before `UXTL v0.8H, v0.8B`.

Required patching:

- Patch conditional branch at original `wrap_skip` to `cold_s_wrap`.
- Emit cold block after hot path and after all normal hot fallthrough code has a stable join label.
- Emit unconditional branch from `cold_s_wrap` to join.
- Use explicit `patch_to_target(pos, target_pos)` rather than `PATCH_FORWARD_BCOND(pos)` against current `block_pos`.

Slice 2 implementation:

- `B.EQ` placeholder is recorded as `wrap_eq_pos`.
- Hot path keeps only normal adjacent load inline:
  - `LSL w4, w4, #2`
  - `LDR d0, [x14, x4]`
  - `LDR d1, [x13, x4]`
- `s_wrap_join_pos` is immediately after the normal loads, before `UXTL v0.8H, v0.8B`.
- `ARM64_COLD_BLOCK_S_WRAP` is enqueued with `wrap_eq_pos` and `s_wrap_join_pos`.
- `cold_s_wrap_pos` drains after the generated `RET` and contains only the old wrap texel assembly:
  - `row0[S]`
  - `row0[0]`
  - `row1[S]`
  - `row1[0]`
  - `INS` into `v0.S[1]` / `v1.S[1]`
- `arm64_codegen_patch_bcond_to_target(code_block, wrap_eq_pos, cold_s_wrap_pos)` patches the edge branch.
- `arm64_codegen_patch_b_to_target(code_block, cold_s_wrap_done_pos, s_wrap_join_pos)` patches cold return.
- Join remains immediately before `UXTL v0.8H, v0.8B`.
- No S-clamp, T-edge, dither, mirror, W/div, common skip, or state-store movement in Slice 2.

Equivalence risks:

- `w4` must still contain unclamped S texel coordinate when entering cold block.
- `w16` must still hold `tex_w_mask[tmu][lod]` at compare time.
- `x13` and `x14` row pointers must remain live.
- `v0` and `v1` must be populated exactly as the inline path before `UXTL`.
- Branch range must be checked.

Expected benefit:

- Removes uncommon edge-load sequence from hot bilinear wrap path.
- Benefit exists only if S-edge samples are rare relative to interior bilinear samples.

### 2. Bilinear S-Clamp Duplicate Edge

Risk: medium-high
Second candidate only after S-wrap proof.

Exact region:

- `src/include/86box/vid_voodoo_codegen_arm64.h:2005-2043`

Current shape:

- `CMP w4, #0`
- `CSEL w4, wzr, w4, LT`
- `B.LT` placeholder to duplicate block
- `CMP w4, w15`
- `CSEL w4, w15, w4, CS`
- `B.CS` placeholder to duplicate block
- hot normal adjacent-load path
- inline duplicate block:
  - load one texel from row0
  - duplicate to both halves of `v0`
  - load one texel from row1
  - duplicate to both halves of `v1`
- join before `UXTL`

Cold-layout design:

- Keep both `CSEL` operations exactly where they are.
- `B.LT` and `B.CS` both target one `cold_s_clamp_dup` block.
- `cold_s_clamp_dup` emits current `src/include/86box/vid_voodoo_codegen_arm64.h:1656-1662`.
- Return branch goes to join before `src/include/86box/vid_voodoo_codegen_arm64.h:2073`.

Slice 3 implementation:

- Added `ARM64_COLD_BLOCK_S_CLAMP_DUP` plus a two-source cold-queue entry helper.
- `B.LT` and `B.CS` patch to one cold block with explicit target patch helpers.
- Hot S-clamp interior path now falls through after adjacent `LDR d0` / `LDR d1`.
- Cold block loads one clamped texel per row, duplicates into `v0` / `v1`, then branches back to the join immediately before `UXTL`.
- `CMP` / `CSEL` / matching `B.cond` adjacency and flags remain unchanged.
- Validation state: accepted after long metrics verify with S-clamp low/high coverage on TMU0.

Metrics/defer:

- Compare metrics against Slice 2 S-wrap proof after validation.
- Defer optional compile/layout metric slice; proper proof needs shared metric struct/init/log plumbing beyond cheap N5 codegen-local scope.
- Defer T-edge, dither fallback, mirror, W/div, common skip, alpha/depth/fog skip.
- Defer any x86-64 or helper-backed dynarec work.

Next: final review and commit decision; no more N5 cold-layout targets in this slice.

Required patching:

- Multi-source branch patching: `clamp_lo_pos` and `clamp_hi_pos` both patch to one cold label.
- Cold return branch patches to join.

Equivalence risks:

- Do not insert any instruction between `CMP`, `CSEL`, and matching `B.cond` that changes flags.
- `CSEL` does not set flags; branch consumes original `CMP` flags.
- `w4` must be the clamped coordinate when cold block loads duplicated texels.
- `w15` must still hold `tex_w_mask[tmu][lod]` for high clamp.
- `x13`, `x14`, `v0`, and `v1` contracts match S-wrap.

Expected benefit:

- Similar to S-wrap, but branch/flag sensitivity is higher.
- Should be batched with S-wrap only if metrics show both edge forms are rare and code-size/I-cache pressure matters.

### 3. Bilinear T Clamp/Wrap Normalization

Risk: high
Defer.

Exact region:

- `src/include/86box/vid_voodoo_codegen_arm64.h:1798-1814`
- row address join at `src/include/86box/vid_voodoo_codegen_arm64.h:1827`

Possible design:

- Use interpreter-style predicate from `tex_read_4()`:
  - `((t | (t + 1)) & ~h_mask)`
- Hot path handles interior T0/T1 row addressing.
- Cold path handles clamp/wrap T0/T1 normalization and returns before row address setup.

Why defer:

- Requires new branch shape, not just moving an existing block.
- Increases live-register pressure around `w5`, `w13`, `w15`, `x12`, `x13`, `x14`, and `w7`.
- Mistakes affect both row pointers, not just S texel pair loading.
- Existing emitted code has separate clamp/wrap decisions based on compile-time `state->clamp_t[tmu]`.

### 4. Full Bilinear Edge Block

Risk: very high
Reject for first N5 implementation.

Interpreter truth:

- `src/video/vid_voodoo_render.c:250-291`
- edge predicate at `src/video/vid_voodoo_render.c:255`
- per-sample normalization at `src/video/vid_voodoo_render.c:256-279`

Possible design:

- Hot path emits only interior four-texel load.
- Cold path normalizes all four samples and assembles `v0`/`v1`.

Why defer:

- This replaces the current specialized S/T handling with a larger generated-code split.
- It needs more cold block assembly, more live state, and more correctness proof.
- It should follow a successful S-wrap/S-clamp slice, not lead N5.

### 5. Dither Pointer Fallback

Risk/benefit: low benefit, not a cold-layout fit
Reject for N5 layout.

Exact regions:

- Asked audit range includes `src/include/86box/vid_voodoo_codegen_arm64.h:4080-4182`.
- Actual pointer materialization fallback is `src/include/86box/vid_voodoo_codegen_arm64.h:4194-4216`.
- Dither base predicate is `src/include/86box/vid_voodoo_codegen_arm64.h:2141`.
- Alpha-blend destination decode uses `x26` for `rgb565` at `src/include/86box/vid_voodoo_codegen_arm64.h:3968-3973`.

Current behavior:

- `dither_base_in_x26 = dither && RGB_WMASK && !alpha_blend`.
- Non-alpha dither path uses `MOV x7, x26`.
- Alpha+dither path cannot use `x26` because alpha blend needs `x26` for `rgb565`.
- Alpha+dither fallback materializes dither table pointer into `x7` per pixel using `MOVZ` plus up to three `MOVK`.

Why reject for N5 layout:

- This is compile-time mode selection, not a runtime branch with rare taken edge.
- If alpha+dither mode is emitted, fallback materialization runs per pixel.
- Moving it out of line adds a branch and still executes the same per-pixel work.
- Better future direction, if metrics prove enough cost, is register-allocation/prologue redesign, not cold layout.

## Patch Infrastructure Required Before Code

Current patch helpers:

- `src/include/86box/vid_voodoo_codegen_arm64.h:820-828` placeholders
- `src/include/86box/vid_voodoo_codegen_arm64.h:854-899` forward patch helpers

Problem:

- Original helpers patch placeholders to current `block_pos`.
- Cold-tail layout needs placeholders patched to a saved label that may not equal current `block_pos` when patching is performed.
- Existing inline patterns assume cold/edge block immediately follows hot block, then a local `normal_done` patch rejoins at current `block_pos`.

Implemented design:

- Added explicit target patch helpers:
  - patch `B.cond` to arbitrary target byte offset
  - patch `B` to arbitrary target byte offset
  - patch `CBZ/CBNZ` to arbitrary target byte offset
  - patch `TBZ/TBNZ` to arbitrary target byte offset, with range checks
- Added a small cold-block queue:
  - source branch placeholder offset
  - cold block start offset
  - hot join offset
  - branch kind
  - condition/register metadata only if needed for validation/debug
- Keep range checks:
  - `B.cond`: imm19 scaled by 4
  - `B`: imm26 scaled by 4
  - `CBZ/CBNZ`: imm19 scaled by 4
  - `TBZ/TBNZ`: imm14 scaled by 4, most fragile
- Do not use TBZ/TBNZ cold splitting first because imm14 range is easiest to overflow.

## Existing Patch Sites And Risk Map

Texture-local patch sites:

- `src/include/86box/vid_voodoo_codegen_arm64.h:1377-1383`
  - `ARM64_EMIT_TEX_MIRROR`
  - local `TBZ` patch
  - do not choose first; TBZ range risk
- `src/include/86box/vid_voodoo_codegen_arm64.h:1544-1551`
  - `div_skip_pos`
  - `CBZ_X` over coordinate division
  - do not choose first; semantic-sensitive negative/zero W area
- `src/include/86box/vid_voodoo_codegen_arm64.h:1846-1882`
  - bilinear S clamp duplicate
  - candidate 2
- `src/include/86box/vid_voodoo_codegen_arm64.h:1891-1921`
  - bilinear S wrap
  - candidate 1

Main pixel-loop patch sites:

- `src/include/86box/vid_voodoo_codegen_arm64.h:2522-2536`
  - stipple skip
  - branches to common skip target
- `src/include/86box/vid_voodoo_codegen_arm64.h:2617-2677`
  - W-depth local got-depth branches
  - must rejoin with `w10` valid
- `src/include/86box/vid_voodoo_codegen_arm64.h:2808-2824`
  - depth-test skip
  - branches to common skip target
- `src/include/86box/vid_voodoo_codegen_arm64.h:3308-3309`
  - chroma skip
  - branches to common skip target
- `src/include/86box/vid_voodoo_codegen_arm64.h:3379-3380`
  - alpha-mask skip
  - branches to common skip target
- `src/include/86box/vid_voodoo_codegen_arm64.h:3456-3470`
  - local select override skip/done
  - local shape, not first N5 target
- `src/include/86box/vid_voodoo_codegen_arm64.h:3866-3892`
  - alpha-test skip
  - branches to common skip target
- `src/include/86box/vid_voodoo_codegen_arm64.h:4370-4384`
  - common skip target patching
  - do not move as a layout experiment

Do-not-move regions:

- Prologue, pinned constants, ABI frame:
  - `src/include/86box/vid_voodoo_codegen_arm64.h:2248-2318`
- Texture state stores:
  - `src/include/86box/vid_voodoo_codegen_arm64.h:1643-1655`
  - `src/include/86box/vid_voodoo_codegen_arm64.h:1733-1734`
- Depth/alpha writes before skip target:
  - `src/include/86box/vid_voodoo_codegen_arm64.h:4129-4155`
  - depth write near `src/include/86box/vid_voodoo_codegen_arm64.h:4356`
- Common skip target and per-pixel increments:
  - `src/include/86box/vid_voodoo_codegen_arm64.h:4360-4384`
  - increment block after `src/include/86box/vid_voodoo_codegen_arm64.h:4386`

## Required Metrics Before N5 Code

Metric-only probe should be added before any cold-layout implementation.

Required per-block compile metrics:

- generated `code_size`
- max generated block size
- number of emitted branch placeholders
- number of cold candidates found by mode
- predicted hot bytes removed if each candidate moved cold
- current cache MRU hits, scan hits, misses, compiles, rejects

Required dynamic span/pixel metrics:

- total verified spans
- total verified pixels
- bilinear spans
- bilinear pixels
- bilinear interior S samples
- bilinear S-wrap edge samples
- bilinear S-clamp-low samples
- bilinear S-clamp-high samples
- bilinear T-edge samples
- both-S-and-T edge samples
- per-TMU split for TMU0/TMU1
- per-mode split for point/bilinear/trilinear/fog/alpha-blend/dither/tiled

Required dither-specific metrics before revisiting dither:

- spans and pixels where `dither` is set
- spans and pixels where `dither2x2` is set
- spans and pixels where `FBZ_RGB_WMASK` is set
- spans and pixels where `alpha_blend` is active
- spans and pixels where `dither_base_in_x26` is true
- spans and pixels where dither pointer fallback materialization is emitted
- estimated fallback dynamic instruction count:
  - fallback pixels multiplied by emitted `MOVZ`/`MOVK` count

Optional host performance metrics:

- cycles
- instructions retired
- I-cache misses
- branch misses
- L1I refill or platform equivalent

Metrics acceptance gate:

- S-wrap/S-clamp cold layout is worth implementation only if:
  - bilinear interior samples dominate edge samples in real workloads
  - S-wrap/S-clamp edge taken rate is low enough to justify a branch to cold tail
  - generated hot-path bytes shrink meaningfully
  - `code_max` remains below block-size risk thresholds
  - no increase in rejects

## Future Implementation Slice Order

### Slice 0: Metric-Only Probe

Status: code added and validated.

Purpose:

- Prove whether N5 is worth code layout work.
- No generated-code behavior change.

Files touched by the metric implementation:

- `src/include/86box/vid_voodoo_codegen_arm64.h`
- `src/video/vid_voodoo_render.c`
- possibly `src/include/86box/vid_voodoo_common.h` if counters need persistent storage

Files touched:

- Done: `src/include/86box/vid_voodoo_common.h`
- Done: `src/video/vid_voodoo_render.c`
- Done: `src/video/vid_voodoo.c`

Counters added:

- Done: span/pixel totals under `VOODOO_ARM64_JIT_METRICS=1` plus validation active.
- Done: dither mode share:
  - `dither`
  - `dither2x2`
  - `RGB_WMASK`
  - `alpha_blend`
  - `dither_base_x26`
  - `dither_ptr_fallback`
- Done: bilinear edge frequencies per TMU:
  - total bilinear pixels
  - S-wrap edge pixels
  - S-clamp-low pixels
  - S-clamp-high pixels
  - T-edge pixels
  - combined S/T-edge pixels
- Done: one summary line at close:
  - `Voodoo ARM64 JIT N5 metrics`
- Done: build/sign.
- Done: short verify with metrics disabled.
- Done: short verify with metrics enabled.
- Done: initial short-run metric capture.
- Done: long-run metric capture after Slice 2.

Short-run result:

```text
verify=10240000
mismatch_spans=0
fb_mismatches=0
aux_mismatches=0
state_mismatches=0
```

N5 metric line:

```text
spans=14026981
pixels=422584099
dither_spans=14026981
dither_pixels=422584099
dither2x2_spans=14026981
dither2x2_pixels=422584099
rgb_wmask_spans=14026981
rgb_wmask_pixels=422584099
alpha_blend_spans=6733403
alpha_blend_pixels=278415930
dither_base_x26_spans=7293578
dither_base_x26_pixels=144168169
dither_ptr_fallback_spans=6733403
dither_ptr_fallback_pixels=278415930
tmu0_bilinear_pixels=172739543
tmu0_s_wrap_edge=1649732
tmu0_s_clamp_low=0
tmu0_s_clamp_high=0
tmu0_t_edge=48436746
tmu0_st_edge=595125
tmu1_bilinear_pixels=193012763
tmu1_s_wrap_edge=1339274
tmu1_s_clamp_low=0
tmu1_s_clamp_high=0
tmu1_t_edge=48567910
tmu1_st_edge=422727
```

Derived short-run rates:

- TMU0 S-wrap edge: 0.955% of TMU0 bilinear pixels.
- TMU1 S-wrap edge: 0.694% of TMU1 bilinear pixels.
- TMU0 T-edge: 28.040% of TMU0 bilinear pixels.
- TMU1 T-edge: 25.163% of TMU1 bilinear pixels.
- Dither pointer fallback pixels: 65.884% of all counted pixels.

Short-run implication:

- S-wrap looks dynamically cold enough to remain the first candidate.
- S-clamp did not get coverage in this run; do not implement or claim proof yet.
- T-edge is not cold in this run; keep deferred.
- Dither pointer fallback is hot, not cold; keep rejected for layout.

Long-run result after Slice 2:

```text
verify=339160449
mismatch_spans=0
fb_mismatches=0
aux_mismatches=0
state_mismatches=0
```

Long-run N5 metric line after Slice 2:

```text
spans=339160449
pixels=9333355377
dither_spans=339102849
dither_pixels=9314923377
dither2x2_spans=339102849
dither2x2_pixels=9314923377
rgb_wmask_spans=339160449
rgb_wmask_pixels=9333355377
alpha_blend_spans=102472581
alpha_blend_pixels=3613509126
dither_base_x26_spans=236687868
dither_base_x26_pixels=5719846251
dither_ptr_fallback_spans=102414981
dither_ptr_fallback_pixels=3595077126
tmu0_bilinear_pixels=5337067229
tmu0_s_wrap_edge=30389974
tmu0_s_clamp_low=2289262
tmu0_s_clamp_high=2585380
tmu0_t_edge=537837081
tmu0_st_edge=10903822
tmu1_bilinear_pixels=5909413394
tmu1_s_wrap_edge=127326203
tmu1_s_clamp_low=13286399
tmu1_s_clamp_high=9586512
tmu1_t_edge=3394014951
tmu1_st_edge=86872409
```

Derived long-run rates:

- TMU0 S-wrap edge: 0.569% of TMU0 bilinear pixels.
- TMU1 S-wrap edge: 2.155% of TMU1 bilinear pixels.
- TMU0 S-clamp total: 0.091% of TMU0 bilinear pixels.
- TMU1 S-clamp total: 0.387% of TMU1 bilinear pixels.
- TMU0 T-edge: 10.077% of TMU0 bilinear pixels.
- TMU1 T-edge: 57.434% of TMU1 bilinear pixels.
- Dither pointer fallback pixels: 38.519% of all counted pixels.

Long-run implication:

- S-wrap cold layout has strict correctness proof and enough coldness to keep.
- S-clamp has real coverage and is very cold, but should be a later standalone slice, not bundled into the current S-wrap proof.
- T-edge is still not cold enough for cold layout.
- Dither pointer fallback remains too hot and structurally wrong for cold layout.

Validation:

- Build/sign.
- Short verify with metrics disabled.
- Short verify with metrics enabled.
- Require:
  - `mismatch_spans=0`
  - `fb_mismatches=0`
  - `aux_mismatches=0`
  - `state_mismatches=0`

### Slice 1: Patch-To-Target Infrastructure

Purpose:

- Add explicit patch helpers.
- No cold layout conversion yet.

Files likely touched:

- `src/include/86box/vid_voodoo_codegen_arm64.h`

Validation:

- Done: build/sign.
- Done: short verify.
- Generated code bytes should remain unchanged except possible debug-only assertions if enabled.
- Require strict zero mismatches.

Short verify result:

```text
verify=10240000
mismatch_spans=0
fb_mismatches=0
aux_mismatches=0
state_mismatches=0
```

N5 short-run signal after Slice 1:

- TMU0 S-wrap edge: 1,630,764 / 172,174,962 bilinear pixels.
- TMU1 S-wrap edge: 1,330,239 / 192,359,493 bilinear pixels.
- No S-clamp coverage.
- T-edge still high.
- Dither pointer fallback still hot.

### Slice 1b: Cold-Tail Queue Plumbing

Purpose:

- Add minimal queue infrastructure for future cold-tail block emission.
- Thread queue pointer through `codegen_texture_fetch()` call sites.
- Drain queue after generated epilogue `RET`.
- Leave queue empty and emit no cold blocks yet.

Files touched:

- `src/include/86box/vid_voodoo_codegen_arm64.h`
- `docs/arm64-voodoo-n5-cold-layout-plan.md`

Implementation:

- Added `ARM64_COLD_BLOCK_S_WRAP` kind for future Slice 2 use.
- Added fixed-capacity queue entries with branch placeholder, hot join, kind, and TMU fields.
- Added init/add/drain helpers with fatal overflow.
- Drain helper currently emits nothing for an empty queue and fatals if a non-empty S-wrap entry is drained before Slice 2 implements emission.

Guarantee:

- No S-wrap/S-clamp/T-edge/dither/mirror/W/div/common skip movement.
- No generated behavior or code-size change while queue is empty.

### Slice 2: S-Wrap Cold Layout

Status: implemented and strong-soaked. First attempt rejected because the moved block still sat inline; retry uses the cold-tail queue so normal hot flow does not carry the cold block in line.

Purpose:

- Move only `src/include/86box/vid_voodoo_codegen_arm64.h:1907-1919` out of line.

Prerequisite:

- Done: add a cold-tail queue that can emit cold blocks after normal generated flow, after the generated function epilogue `RET`, then branch back to saved hot joins.
- Do not retry S-wrap by placing the cold block immediately after the hot adjacent-load path; that does not reduce hot layout footprint and only adds cold-path branch cost.

Validation:

- Done: build/sign.
- Done: short verify with bilinear coverage:
  - `verify=10240000`
  - `mismatch_spans=0`
  - `fb_mismatches=0`
  - `aux_mismatches=0`
  - `state_mismatches=0`
  - `tmu0_s_wrap_edge=1643061`
  - `tmu1_s_wrap_edge=1339025`
- Done: strong soak with metrics enabled:
  - `verify=339160449`
  - `mismatch_spans=0`
  - `fb_mismatches=0`
  - `aux_mismatches=0`
  - `state_mismatches=0`
  - `tmu0_s_wrap_edge=30389974`
  - `tmu1_s_wrap_edge=127326203`
- Pending: targeted texture-edge workload only if a broader workload mix is needed before commit.

### Slice 3: S-Clamp Duplicate Cold Layout

Purpose:

- Move only S-clamp duplicate load/dup sequence (`src/include/86box/vid_voodoo_codegen_arm64.h:1656-1662`) out of hot S-clamp region (`src/include/86box/vid_voodoo_codegen_arm64.h:2005-2043`).
- Branch low/high clamp to same cold block.

Current proof status:

- Long-run coverage exists:
  - `tmu0_s_clamp_low=2289262`
  - `tmu0_s_clamp_high=2585380`
  - `tmu1_s_clamp_low=13286399`
  - `tmu1_s_clamp_high=9586512`
- Dynamic rate is cold:
  - TMU0 total S-clamp: 0.091% of bilinear pixels.
  - TMU1 total S-clamp: 0.387% of bilinear pixels.
- Defer until after the S-wrap slice is reviewed/committed, so proof stays bisectable.

Validation:

- Same as Slice 2.
- Add explicit coverage that both low and high clamp cases were exercised, or do not claim those cases proved.
- Short verify with metrics enabled passed after implementation:
  - `verify=10240000`
  - `skipped=0`
  - `mismatch_spans=0`
  - `fb_mismatches=0`
  - `aux_mismatches=0`
  - `state_mismatches=0`
  - `code_bytes=42644`
  - `code_max=1864`
- Target coverage was absent in that short run:
  - `tmu0_s_clamp_low=0`
  - `tmu0_s_clamp_high=0`
  - `tmu1_s_clamp_low=0`
  - `tmu1_s_clamp_high=0`
- Result: general validator pass only; S-clamp cold block still needs a targeted or broader workload with nonzero clamp-low/high counters.
- Long metrics verify passed after relaunch:
  - `verify=173693841`
  - `skipped=0`
  - `mismatch_spans=0`
  - `fb_mismatches=0`
  - `aux_mismatches=0`
  - `state_mismatches=0`
  - `rejects=0`
  - `code_bytes=923392`
  - `code_max=1868`
- Target coverage in that run:
  - `tmu0_s_clamp_low=2501358`
  - `tmu0_s_clamp_high=2810423`
  - `tmu1_s_clamp_low=0`
  - `tmu1_s_clamp_high=0`
- Result: S-clamp cold block accepted. TMU0 low/high dynamic coverage proves the shared emitted cold block with strict zero-mismatch proof; TMU1-specific post-change coverage did not occur and is tracked as low residual risk.

### Slice 4: Reassess

Purpose:

- Compare metrics before/after S-wrap and S-clamp:
  - code bytes
  - code max
  - branch/cold counts
  - cache hits/misses
  - mismatch result
  - optional PMU data

Decision:

- Stop N5 cold-layout work after S-wrap and S-clamp.
- Keep S-wrap and S-clamp cold-tail layout:
  - strict validator proof stayed clean after both slices.
  - `rejects=0`.
  - `code_max=1868`, unchanged from the Slice 2 long run.
  - average compiled block bytes in the Slice 3 long run were `1160.0`, versus `1276.8` in the Slice 2 long run. This is not a controlled performance comparison because the workloads differed, but it does not show code-size pressure.
- Do not move T-edge cold:
  - Slice 3 long run still had hot T-edge rates: TMU0 `25.492%`, TMU1 `26.309%`.
  - Slice 2 long run had TMU1 T-edge at `57.434%`.
- Do not move dither pointer fallback cold:
  - Slice 3 long run fallback pixels were `58.846%` of counted pixels.
  - This remains a prologue/register-allocation issue, not a cold-layout candidate.
- Do not add more cold-tail kinds in N5 without a fresh metric gate.

Reassess result:

- N5 accepted scope: S-wrap edge cold tail and S-clamp duplicate cold tail.
- N5 rejected/deferred scope: T-edge, dither pointer fallback, mirror, W/div, common skip, alpha/depth/fog skip, and TBZ/TBNZ-heavy cold splitting.
- Next optimization direction should be outside N5 cold layout: likely dither register/prologue redesign or a separate measured code-size/pass cleanup.

## Validation Plan

Design-only phase:

- No build.
- No VM.
- No commit.

Metric/probe phase:

- Build/sign before VM validation.
- Run short verify with metrics disabled.
- Run short verify with metrics enabled.
- Inspect summary counters only after guest run is complete.

Implementation phase:

- Validate in batches:
  - patch infrastructure alone
  - S-wrap alone
  - S-clamp alone or S-wrap+S-clamp only after S-wrap passes
- Do not burn one VM run per isolated tiny site unless bisecting a confirmed failing batch.
- Known guest noise ignored by itself:
  - `[0147:0000B9BD] Illegal instruction 00008B55 (FF)`

Proof bar:

```text
mismatch_spans=0
fb_mismatches=0
aux_mismatches=0
state_mismatches=0
```

Strong soak target:

```text
verify>=51200000
```

Completed strong soak after Slice 2:

```text
verify=339160449
mismatch_spans=0
fb_mismatches=0
aux_mismatches=0
state_mismatches=0
```

## Reject And Defer List

Reject for N5 first pass:

- dither pointer fallback layout
- full `tex_read_4()` cold edge block
- W==0 or negative-W texture reciprocal path
- mirror-bit cold splitting
- common skip target relocation
- alpha-test/depth-test skip target layout experiments

Defer until after S-wrap/S-clamp proof:

- T clamp/wrap normalization
- fog source cold splitting
- dither register-allocation/prologue redesign
- any TBZ/TBNZ-heavy cold block work

## Recommended Next Action

Next concrete action:

- N5 cold-layout scope is closed at S-wrap plus S-clamp.
- Do not start more N5 cold-tail movement without a fresh metric gate.
- Next optimization work should move outside N5 cold layout, with dither register/prologue redesign as the leading measured candidate.
