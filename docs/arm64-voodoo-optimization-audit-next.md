# ARM64 Voodoo Optimization Audit Next

Status: 2026-05-31
Source head: `9b97143ca`

This audit starts from current source after P1-P7 closure. It does not overwrite
`docs/arm64-voodoo-optimization-audit-current.md`.

Primary source files:

- `src/include/86box/vid_voodoo_codegen_arm64.h`
- `src/include/86box/vid_voodoo_render.h`
- `src/video/vid_voodoo_render.c`
- `src/include/86box/vid_voodoo_common.h`

Semantic source of truth: interpreter paths and macros in
`src/video/vid_voodoo_render.c` and `src/include/86box/vid_voodoo_render.h`.
The x86-64 backend is not a correctness source. It may be used only as a rough
pipeline-shape reference when the interpreter contract is already clear.

## P1-P7 Closure Context

P1-P7 closed as ARM64-local generated-code optimizations with strict validator
coverage. The important post-closure baseline:

- P1: perspective texture-fetch constant loads corrected.
- P2: dither table base/offset hoist corrected.
- P3: pixel/texel counter batching corrected.
- P4: alpha blend multiply-round sequence factored with
  `ARM64_EMIT_ALPHA_BLEND_MUL_ROUND_V4H`.
- P5: texture `tex_s`, `tex_t`, `lod`, and `lod_frac` state made strict in
  validator and register lifetime improved.
- P6: prologue pinned GPR/NEON constants gated by feature predicates.
- P7: ARM64 JIT cache lookup now has direct MRU probe before remaining-slot scan.

The durable validation target remains:

```text
mismatch_spans=0
fb_mismatches=0
aux_mismatches=0
state_mismatches=0
```

Known guest noise remains ignored by itself:

```text
[0147:0000B9BD] Illegal instruction 00008B55 (FF)
```

## Current Hot-Path Map After P1-P7

### Call and Cache

- `src/video/vid_voodoo_render.c:904-909`: select `voodoo_get_block()` when
  `use_recompiler` is enabled.
- `src/include/86box/vid_voodoo_codegen_arm64.h:4506-4539`: ARM64 cache key
  store/match.
- `src/include/86box/vid_voodoo_codegen_arm64.h:4595-4677`: MRU probe, remaining
  scan, LRU victim, W^X toggle, emit, executable flip, I-cache flush.
- `src/include/86box/vid_voodoo_common.h:788-793`: per-instance codegen data,
  `jit_last_block[4]`, and `jit_generation[4]`.

### Generated Span Function

- `src/include/86box/vid_voodoo_codegen_arm64.h:1913-2004`: `voodoo_generate()`
  feature predicates and block setup.
- `src/include/86box/vid_voodoo_codegen_arm64.h:2059-2230`: fixed ABI save area,
  pinned GPR constants, pinned NEON constants, and hoisted deltas.
- `src/include/86box/vid_voodoo_codegen_arm64.h:2243-4401`: per-pixel loop.
- `src/include/86box/vid_voodoo_codegen_arm64.h:4403-4426`: post-loop batched
  pixel/texel counters.
- `src/include/86box/vid_voodoo_codegen_arm64.h:4432-4454`: fixed epilogue.

### Texture and TMU

- Interpreter truth:
  `src/video/vid_voodoo_render.c:215-382`,
  `src/video/vid_voodoo_render.c:384-422`,
  `src/video/vid_voodoo_render.c:425-660`.
- ARM64 texture fetch:
  `src/include/86box/vid_voodoo_codegen_arm64.h:1275-1889`.
- ARM64 dual-TMU combine:
  `src/include/86box/vid_voodoo_codegen_arm64.h:2625-3073`.
- Lookup tables:
  `src/include/86box/vid_voodoo_codegen_arm64.h:1174-1215`,
  `src/include/86box/vid_voodoo_codegen_arm64.h:4752-4826`.

### Tests, Blending, Fog, Writes

- Interpreter depth/alpha/fog/blend macros:
  `src/include/86box/vid_voodoo_render.h:13-271`.
- ARM64 depth/stipple/tiled address:
  `src/include/86box/vid_voodoo_codegen_arm64.h:2248-2610`.
- ARM64 color/alpha combine:
  `src/include/86box/vid_voodoo_codegen_arm64.h:3098-3566`.
- ARM64 fog:
  `src/include/86box/vid_voodoo_codegen_arm64.h:3568-3714`.
- ARM64 alpha test/blend:
  `src/include/86box/vid_voodoo_codegen_arm64.h:3716-4013`.
- ARM64 RGB/depth writes:
  `src/include/86box/vid_voodoo_codegen_arm64.h:4015-4243`.

### Validator

- State comparator:
  `src/video/vid_voodoo_render.c:703-712`.
- Mode buckets:
  `src/video/vid_voodoo_render.c:720-782`,
  `src/include/86box/vid_voodoo_common.h:32-51`.
- Verify shadow run:
  `src/video/vid_voodoo_render.c:1057-1112`,
  `src/video/vid_voodoo_render.c:1600-1715`.
- Validation state fields:
  `src/include/86box/vid_voodoo_common.h:760-782`.

## Active Queue Reset

Current state:

- P1-P7 are closed and committed.
- N1/N2/N3/N4/N5 implementation slices are closed or explicitly deferred.
- Worker D/E alpha+dither dither-base pinning is closed and committed in
  `9b97143ca`.
- N5 cold layout is closed. Keep S-wrap and S-clamp. Do not move T-edge or
  dither pointer fallback to cold tails without a fresh metric gate.
- N6 predicate-decode and metrics/coverage hardening are closed through the
  ARM64 generator plus ARM64 metrics/validation-counter slices. Do not continue
  into a broad shared-render struct rewrite without a fresh x86-64 compile plan.

Next slices:

1. Small ARM64 texture/TMU contract cleanup only:
   no semantic changes unless backed by interpreter-truth validation and strict
   short plus long VM proof.
2. Optional N7 state-validator counter extension, report-only at first.
3. Do not continue N5 register work unless a new workload proves shape `63` is
   material enough to justify broad register/frame redesign.

Closed slice summary:

## Worker D: ARM64 Dither Base Pinned-Reg Slice

Status: implemented; short and long VM validation passed.

Changed:

- Alpha+dither RGB-write path now tries to pin the dither base in spare
  callee-saved GPRs: `x22`, else `x23`, else `x25`, else `x19` when
  perspective logtable is unused.
- `x18`, `x20`, `x21`, and `x26` are not stolen. Alpha blend keeps `x26` for
  `rgb565`.
- If all candidate regs are live for existing table pointers, codegen keeps the
  old per-pixel `MOVZ`/`MOVK` materialization into `x7`.
- Dither2x2 vs 4x4 base selection and green-table offset logic are unchanged.
- Metrics now split alpha+dither dither-base coverage into pinned-base and true
  per-pixel fallback counters while preserving existing fallback counters as the
  total candidate count.

Validation:

- `git diff --check` passed.
- Build/sign passed with `./scripts/build-and-sign.sh`.
- Short VM validation passed with metrics enabled:
  - `verify=10240000`
  - `skipped=0`
  - `mismatch_spans=0`
  - `fb_mismatches=0`
  - `aux_mismatches=0`
  - `state_mismatches=0`
  - `rejects=0`
  - `code_bytes=42676`
  - `code_max=1864`
- Metric split from the short run:
  - `dither_ptr_fallback_pixels=270543915`
  - `dither_base_pinned_pixels=28840315`
  - `dither_ptr_true_fallback_pixels=241703600`
- Follow-up Worker E audit added `x19` as a final candidate when `need_x19` is
  false. Expected effect: reduce true fallback in alpha+dither modes that do not
  use perspective texture LOD.
- Worker E validation:
  - `./scripts/setup-and-build.sh build` passed.
  - Short VM validation with metrics passed:
    - `verify=10240000`
    - `skipped=0`
    - `mismatch_spans=0`
    - `fb_mismatches=0`
    - `aux_mismatches=0`
    - `state_mismatches=0`
    - `rejects=0`
    - `code_bytes=42756`
    - `code_max=1868`
  - Metric split:
    - `dither_ptr_fallback_pixels=500254361`
    - `dither_base_pinned_pixels=36009612`
    - `dither_ptr_true_fallback_pixels=464244749`
  - Long VM validation with metrics passed:
    - `verify=240782144`
    - `skipped=0`
    - `mismatch_spans=0`
    - `fb_mismatches=0`
    - `aux_mismatches=0`
    - `state_mismatches=0`
    - `rejects=0`
    - `code_bytes=11743276`
    - `code_max=1864`
  - Long-run metric split:
    - `dither_ptr_fallback_pixels=4454955422`
    - `dither_base_pinned_pixels=3635213944`
    - `dither_ptr_true_fallback_pixels=819741478`
- Result: pinned path remains live and correct in the Worker E short and long
  runs, with `x19` available only for alpha+dither modes that do not need
  perspective logtable.

Stop rules carried forward:

- Stop before any broad frame/save redesign.
- Stop before any `x18` use.
- Stop before any helper-backed dynarec path.
- Leave true fallback when `x22`/`x23`/`x25`/`x19` liveness is not free.
- No x86-64 edits.

Next: use a report-only fallback-mode audit before any broader register
allocation redesign.

### N1: ARM64 Texture Address Emit Refactor

Priority: 1

Scope: ARM64-local.

Exact files/functions/line areas:

- `src/include/86box/vid_voodoo_codegen_arm64.h:1275-1889`
  (`codegen_texture_fetch()`).
- Interpreter truth:
  `src/video/vid_voodoo_render.c:215-382`,
  `src/video/vid_voodoo_render.c:384-422`.

Opportunity:

- Factor repeated mask-base loads, clamp/wrap emission, mirror tests, and
  `tex_lod[tmu][lod]` addressing into small ARM64 emitter helpers.
- Keep generated instructions identical at first, then use the helpers to make
  small proven improvements: one mask load reused across adjacent S/T logic, one
  named helper for point-vs-bilinear shift contracts, and clearer live-register
  contracts for `w4=tex_s`, `w5=tex_t`, `w6=lod`.

Expected impact:

- Medium maintainability win immediately.
- Low to medium generated-code win after helper-equivalence baseline, mainly in
  bilinear paths where mask loads and address setup are still verbose.
- Lower register-pressure risk for later texture work because the contract
  becomes explicit.

Risk:

- Medium. Texture state was recently fixed by P5; accidental movement of
  `STATE_tex_s`, `STATE_tex_t`, `STATE_lod`, or `STATE_lod_frac_n(tmu)` stores
  would cause state-only mismatches.

Validation needed:

- Existing strict validator is sufficient for first helper-only slice.
- Require coverage buckets showing bilinear, point, mirror/clamp/wrap where
  available.
- If helper slice changes instruction selection, run both short verify and
  strong soak.

x86-64:

- Must not be touched for ARM64 emitter factoring.
- If a shared texture semantic helper is introduced later, x86-64 must either be
  updated to use the same helper or explicitly left unchanged with a proof that
  the helper is interpreter-only and not part of backend semantics.

Larger refactor justified:

- Yes, but only as source factoring first. Generated-code changes should be
  stacked after helper-equivalence proof.

First safe implementation slice:

- Add ARM64-local emitter helpers for `PARAMS_tex_*_n(tmu)` indexed load and
  S/T clamp/wrap emission. Convert bilinear path only. No instruction-count
  reduction target in first slice; target is exact behavior.

### N2: Validator Coverage and Codegen Metrics

Priority: 2

Scope: shared validation surface plus ARM64-local counters.

Exact files/functions/line areas:

- `src/video/vid_voodoo_render.c:720-782` (`voodoo_validate_mode_accum()`).
- `src/video/vid_voodoo_render.c:1057-1112` and
  `src/video/vid_voodoo_render.c:1600-1715` (verify shadow run and mismatch log).
- `src/include/86box/vid_voodoo_common.h:32-51`,
  `src/include/86box/vid_voodoo_common.h:760-782`.
- ARM64 cache/compile metrics:
  `src/include/86box/vid_voodoo_codegen_arm64.h:4595-4677`.

Opportunity:

- Add opt-in metrics that expose the signals the next refactors need:
  generated `code_size`, cache MRU hit vs scan hit vs miss, rejected slots, and
  per-bucket clean span counts by high-value flags (`bilinear`, `dual_tmu`,
  `trilinear`, `alpha_blend`, `fog`, `dither`, tiled).
- Keep metrics passive. No semantic changes, no helper-backed paths.

Expected impact:

- High validation clarity. It makes future larger refactors measurable before VM
  time is spent on guesswork.
- Low runtime impact when disabled.

Risk:

- Low to medium. `voodoo_t` is shared, and noisy logging can distort test runs if
  always-on.

Validation needed:

- Build/sign.
- Short verify with metrics disabled must preserve exact baseline.
- Short verify with metrics enabled must preserve exact baseline and show useful
  nonzero coverage data.

x86-64:

- Do not use x86-64 as truth.
- If metrics live in `voodoo_t` or shared validation printing, x86-64 does not
  need backend behavior changes. Any x86-64 compile counters should be absent or
  separately guarded unless deliberately implemented later.

Larger refactor justified:

- Yes. This is instrumentation that makes later source-backed refactors safer.

First safe implementation slice:

- Add an ARM64-only compile/cache metric block under a new env gate, plus one
  validation summary line. Do not change validator pass/fail policy.

### N3: Conditional Callee-Saved Save/Restore

Priority: 3

Scope: ARM64-local.

Exact files/functions/line areas:

- Feature predicates:
  `src/include/86box/vid_voodoo_codegen_arm64.h:1962-2002`.
- Prologue/constant load:
  `src/include/86box/vid_voodoo_codegen_arm64.h:2059-2230`.
- Loop users:
  `src/include/86box/vid_voodoo_codegen_arm64.h:2625-3073`,
  `src/include/86box/vid_voodoo_codegen_arm64.h:3098-4013`,
  `src/include/86box/vid_voodoo_codegen_arm64.h:4272-4373`.
- Epilogue:
  `src/include/86box/vid_voodoo_codegen_arm64.h:4432-4454`.

Opportunity:

- P6 gated many pinned constant loads but still saves/restores all callee-saved
  GPR and NEON registers in a fixed 176-byte frame.
- Add a reviewed register-use bitmap and conditionally omit saves/restores for
  unused `d8-d11`, `d14`, and selected `x19-x26` pairs where the generated block
  cannot write them.
- Keep stack alignment simple. Avoid variable frame sizes until a fixed-layout
  conditional pair model is proven.

Expected impact:

- Low to medium per span, higher for short spans and many block transitions.
- Better cache behavior from fewer stack stores/loads.

Risk:

- Medium-high. ABI errors crash or corrupt unrelated host state.

Validation needed:

- Build/sign before any VM run.
- Short verify first.
- Strong soak.
- A low-feature workload where many predicates are false, plus texture/alpha/fog
  workloads where most predicates are true.

x86-64:

- No x86-64 change required. This is ARM64 ABI-local.

Larger refactor justified:

- Yes, but only after N2 metrics or a static per-block save/use dump confirms
  common blocks leave registers unused.

First safe implementation slice:

- Introduce an ARM64-local register-use bitmap and assert/comment every
  callee-saved register use. Do not change generated saves/restores in that
  first slice.

### N4: TMU Combine Emitter Factoring

Priority: 4

Scope: ARM64-local.

Exact files/functions/line areas:

- `src/include/86box/vid_voodoo_codegen_arm64.h:2625-3073`
  (dual-TMU combine).
- Interpreter truth:
  `src/video/vid_voodoo_render.c:425-660`.

Opportunity:

- TMU1 and TMU0 combine paths repeat detail factor, `LOD_FRAC`, reverse blend,
  alpha factor, and add/local patterns.
- Factor repeated ARM64 emission into helpers parameterized by TMU, RGB-vs-alpha,
  trilinear flag, and local/other register assignment.
- First pass should preserve instruction stream shape, not change semantics.

Expected impact:

- Medium maintainability win.
- Low to medium generated-code win later, because helper factoring can expose
  duplicated `STATE_lod` and `STATE_lod_frac_n()` loads and repeated alpha
  extraction.

Risk:

- High. TMU combine has many mode-bit cross products and historical x86-64
  parity hazards. Interpreter only is truth.

Validation needed:

- Existing strict validator plus coverage buckets showing dual-TMU, trilinear,
  `TC_MSELECT_DETAIL`, `TC_MSELECT_LOD_FRAC`, alpha TCA paths.
- If current games do not hit detail modes, add a small validator coverage flag
  before optimizing those cases.

x86-64:

- No x86-64 change for ARM64 emitter helpers.
- If a shared mode decoder is extracted from render locals, x86-64 must be
  reviewed because the codegen header depends on enclosing locals from
  `vid_voodoo_render.c`.

Larger refactor justified:

- Yes, but after N1. Texture fetch contracts should be stable before TMU combine
  contracts are reorganized.

First safe implementation slice:

- Factor only `TC_MSELECT_DETAIL` and `TC_MSELECT_LOD_FRAC` factor emission into
  ARM64-local helpers for one TMU path. Keep generated behavior equivalent.

### N5: Cold-Path Generated Block Layout

Priority: 5

Scope: ARM64-local.

Current status:

- Superseded by active plan:
  `docs/arm64-voodoo-n5-cold-layout-plan.md`.
- N2/N1 prerequisites are already satisfied in current history.
- Slice 0 N5 metrics are implemented and validated.
- Slice 1 patch-to-target helpers are implemented and short-verified.
- Slice 1b cold-tail queue plumbing is implemented.
- Slice 2 S-wrap cold-tail emission is implemented and strong-soaked:
  `verify=339160449`, `mismatch_spans=0`, `fb_mismatches=0`,
  `aux_mismatches=0`, `state_mismatches=0`.
- Slice 3 S-clamp duplicate cold-tail emission is implemented and accepted:
  `verify=173693841`, `mismatch_spans=0`, `fb_mismatches=0`,
  `aux_mismatches=0`, `state_mismatches=0`.
- Slice 3 dynamic target coverage hit TMU0 S-clamp low/high after implementation;
  TMU1 post-change coverage is a low residual risk because the emitted cold block
  is shared by both TMUs.
- N5 cold-layout reassessment is closed: keep S-wrap and S-clamp, stop further
  N5 cold-tail moves unless a fresh metric gate exists.
- T-edge and dither pointer fallback are rejected for N5 cold layout because
  long-run metrics show they are hot.

Exact files/functions/line areas:

- Bilinear clamp/wrap cold cases:
  `src/include/86box/vid_voodoo_codegen_arm64.h:1571-1718`.
- Skip patch targets:
  `src/include/86box/vid_voodoo_codegen_arm64.h:2248-2318`,
  `src/include/86box/vid_voodoo_codegen_arm64.h:2525-2610`,
  `src/include/86box/vid_voodoo_codegen_arm64.h:3716-3782`,
  `src/include/86box/vid_voodoo_codegen_arm64.h:4245-4270`.
- Dither fallback pointer materialization:
  `src/include/86box/vid_voodoo_codegen_arm64.h:4080-4182`.

Opportunity:

- Keep common non-edge cases dense by moving uncommon wrap/clamp/fallback blocks
  to the end of the generated function with forward branches and patched returns
  into the hot path.
- This is not helper-backed execution; it is still native generated code.

Expected impact:

- Medium I-cache and branch-predictability improvement for S-wrap/S-clamp only
  where metrics show rare edge sampling.
- No N5 cold-layout work for dither pointer fallback or T-edge.

Risk:

- High. Patch bookkeeping becomes more complex and block-size pressure may
  increase.

Validation needed:

- Keep strict validator proof after any layout change.
- Use the active N5 plan for current metrics and slice status.
- A targeted texture-edge workload is optional only if broader workload mix is
  needed before commit.

x86-64:

- No x86-64 change for ARM64 generated layout.

Larger refactor justified:

- Not for the current S-wrap slice. Keep it reviewable and bisectable.

First safe implementation slice:

- Current safe slices are already implemented and validated: S-wrap cold-tail
  layout plus S-clamp duplicate cold-tail layout.
- Next action is outside N5 cold layout. T-edge and dither pointer fallback stay
  rejected/deferred for cold-tail layout.

### N6: Shared Mode Decode Struct for Auditability

Priority: 6

Scope: shared semantic/refactor candidate.

Exact files/functions/line areas:

- Enclosing render locals used by ARM64 header:
  `src/video/vid_voodoo_render.c:784-823` and surrounding mode decode locals in
  `voodoo_half_triangle()`.
- ARM64 dependency on those locals:
  `src/include/86box/vid_voodoo_codegen_arm64.h:1940-1950`.
- x86-64 include boundary:
  `src/video/vid_voodoo_render.c:662-665`.

Status:

- Closed for the safe ARM64-local scope. The ARM64 generator now consumes a
  decoded predicate helper, and ARM64 metrics/validation counters consume the
  same helper where relevant.
- Broad shared-render struct conversion remains deliberately unimplemented.

Opportunity:

- A future broader refactor could convert scattered mode-bit locals into a
  named decoded-mode struct used by interpreter and codegen include boundaries.
- The safe N6 scope already made ARM64 feature predicates and validation
  buckets less fragile without renaming shared render locals.

Expected impact:

- Medium maintainability.
- Low direct performance impact.
- High audit clarity for future shared semantic changes.

Risk:

- High blast radius. The x86-64 header likely depends on the same locals and has
  known issues, so correctness must still come from interpreter behavior.

Validation needed:

- This needs both non-codegen interpreter validation and ARM64 JIT verify.
- A compile-only x86-64 check is required if shared locals are removed or renamed,
  but x86-64 runtime behavior must not be used as semantic proof.

x86-64:

- Must be touched if shared locals are renamed or replaced by struct fields,
  because the x86-64 header is included from the same render function.
- Must not be used to decide semantics.

Larger refactor justified:

- Yes eventually, but not as first post-P7 work.

First safe implementation slice:

- Done. The implemented version kept the helper in `vid_voodoo_regs.h`, switched
  ARM64 `voodoo_generate()` predicate setup, then aligned ARM64 N5 metrics and
  TMU detail/LOD-frac coverage accounting.

### N7: State Validator Extension for Counters and Optional Fields

Priority: 7

Scope: shared validation candidate, ARM64 first consumer.

Exact files/functions/line areas:

- `src/video/vid_voodoo_render.c:703-712`
  (`voodoo_validate_state_mismatch()`).
- `src/video/vid_voodoo_render.c:1600-1715` mismatch logging.
- `src/include/86box/vid_voodoo_common.h:760-782` validation counters.
- JIT counter handoff:
  `src/video/vid_voodoo_render.c:1726-1728`,
  `src/include/86box/vid_voodoo_codegen_arm64.h:4403-4426`.

Opportunity:

- Add opt-in validation for `pixel_count`, `texel_count`, and selected
  post-span fields after P3 counter batching.
- Keep it separate from default strict state fields to avoid noisy failures from
  fields not intended to match during shadow restore.

Expected impact:

- Medium validation clarity for future batching or loop-exit refactors.

Risk:

- Medium. Verify mode restores and replays state, so counter comparison must
  happen at the correct boundary.

Validation needed:

- Short verify with counter-check env off must be unchanged.
- Short verify with counter-check env on must pass before any counter-related
  optimization is attempted.

x86-64:

- No x86-64 behavior change if validator compares only JIT-vs-interpreter state
  for the active backend.
- If counter semantics are redefined in shared render code, x86-64 must be
  updated or explicitly gated out.

Larger refactor justified:

- Only if future loop or counter work is planned. Otherwise N2 is enough.

First safe implementation slice:

- Add an opt-in counter check for ARM64 verify runs only.

## Do Not Do Yet

- Do not replace `SDIV` in perspective texture fetch with approximate reciprocal
  math.
- Do not add helper-backed dynarec paths.
- Do not use x86-64 generated behavior as correctness proof.
- Do not change shared mode locals used by both codegen headers before a
  compile-impact plan exists.
- Do not change alpha blend arithmetic beyond already-proven
  `ARM64_EMIT_ALPHA_BLEND_MUL_ROUND_V4H` factoring without directed exhaustive
  proof.
- Do not add further cold-path block layout before reviewing the S-wrap slice
  and keeping proof bisectable.
- Do not conditionally skip ABI saves/restores before a register-use bitmap is
  reviewed.

## Recommended Next Slice

Current next action is outside N5/N6: small ARM64 texture/TMU contract cleanup
or N7 validator counter-extension audit, both report-only first.

Reason:

- N5's remaining dither fallback is only shape `63` with all candidate regs
  live, so it is not a small spare-register slice.
- N6 now gives one decoded predicate source for ARM64 generator and ARM64
  metrics/coverage decisions. Further shared-render conversion would raise
  x86-64/shared-path risk without a current performance target.
  `dither_ptr_true_fallback_pixels=819741478` remain.
- A mode map can prove whether remaining fallback is concentrated in a few
  register-pressure shapes or too broad to justify more allocator work.
- This avoids widening into frame/save redesign or shared mode-local changes.

First slice:

- Inspect validation mode buckets and ARM64 predicates for the remaining true
  fallback shapes.
- Do not edit code.
- End with keep/stop recommendation and exact candidate predicates if a follow-up
  code slice is justified.

## Validation Commands and Pass Target

Build/sign after any source edit:

```sh
./scripts/build-and-sign.sh
```

Short verify:

```sh
VOODOO_VALIDATE=verify VOODOO_VALIDATE_LIMIT=10240000 VOODOO_VALIDATE_LOG_LIMIT=8 build/src/86Box.app/Contents/MacOS/86Box --vmpath '/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 Gaming PC'
```

Strong soak:

```sh
VOODOO_VALIDATE=verify VOODOO_VALIDATE_LIMIT=51200000 VOODOO_VALIDATE_LOG_LIMIT=8 build/src/86Box.app/Contents/MacOS/86Box --vmpath '/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 Gaming PC'
```

Pass target:

```text
verify=10240000
mismatch_spans=0
fb_mismatches=0
aux_mismatches=0
state_mismatches=0
```

Strong pass target:

```text
verify=51200000
mismatch_spans=0
fb_mismatches=0
aux_mismatches=0
state_mismatches=0
```

## Implementation Log

### 2026-05-31: N2 ARM64 JIT metrics started

- Added planned env gate: `VOODOO_ARM64_JIT_METRICS=1`.
- Intended summary counters: MRU hits, scan hits, misses, compiles, rejected
  blocks, total generated code bytes, and max generated block size.
- Gate is passive; validator pass/fail behavior remains unchanged.
- Counters are stored per render partition and summed at close, avoiding shared
  increments across render threads.
- Build/sign passed after source edits.
- Short verify with metrics passed:
  `verify=10240000`, `mismatch_spans=0`, `fb_mismatches=0`,
  `aux_mismatches=0`, `state_mismatches=0`.
- Metrics line emitted:
  `mru_hits=1477070`, `scan_hits=2611033`, `misses=29`, `compiles=29`,
  `rejects=0`, `code_bytes=42788`, `code_max=1868`.
- Known guest noise `[0147:0000B9BD] Illegal instruction 00008B55 (FF)`
  appeared and was ignored.

### 2026-05-31: N4 target-workload coverage probe

- Paused further N4 optimization because the latest workloads only proved
  TMU0 RGB/alpha `LOD_FRAC`; `DETAIL` and all TMU1 target counters stayed
  zero.
- Added passive global validation coverage counters independent of the
  16-entry per-mode mismatch buckets.
- New `Voodoo validate TMU coverage` summary records texture-disabled spans,
  TMU0 local/passthrough spans, actual dual-TMU combine spans, and sub-clocal
  mselect distributions for TMU0/TMU1 RGB and alpha.
- The target fields are derived from the global sub-clocal mselect histograms:
  `target_tmu*_rgb_detail`, `target_tmu*_rgb_lod_frac`,
  `target_tmu*_alpha_detail`, and `target_tmu*_alpha_lod_frac`.
- This is logging only; it does not change generated ARM64 code, x86-64 code,
  interpreter semantics, or any helper-backed dynarec path.
- Rebuilt with `scripts/setup-and-build.sh build` per local macOS app/icon
  requirement.
- Unreal Gold `Vortex2` with Glide detail textures enabled did not hit the N4
  target factor paths:
  `target_tmu0_rgb_detail=0`, `target_tmu0_rgb_lod_frac=0`,
  `target_tmu0_alpha_detail=0`, `target_tmu0_alpha_lod_frac=0`,
  `target_tmu1_rgb_detail=0`, `target_tmu1_rgb_lod_frac=0`,
  `target_tmu1_alpha_detail=0`, and `target_tmu1_alpha_lod_frac=0`.
- The run did exercise texture work, including `dual_tmu_combine=25239103`,
  but the sub-clocal mselect histograms were all zero, so it did not cover
  `GR_COMBINE_FACTOR_DETAIL_FACTOR`/`LOD_FRACTION` blend-factor state.
- Validation stayed clean:
  `verify=42832460`, `skipped=0`, `mismatch_spans=0`, `fb_mismatches=0`,
  `aux_mismatches=0`, `state_mismatches=0`; the `409600000` cap was not hit.
- Known guest noise `[0147:0000B9BD] Illegal instruction 00008B55 (FF)`
  appeared and was ignored.

### 2026-05-31: N1 ARM64 texture address emit refactor slice 1

- Added ARM64-local macro emitter helpers for texture parameter indexed loads,
  mirror tests, and clamp/wrap coordinate emission.
- Converted the bilinear path in `codegen_texture_fetch()` only.
- Kept generated-code size stable and preserved the live-register contract:
  `w4=tex_s`, `w5=tex_t`, `w6=lod`.
- Preserved `STATE_tex_s`, `STATE_tex_t`, `STATE_lod`, and
  `STATE_lod_frac_n(tmu)` store placement.
- Initial function-helper version failed short verify with a texture-state
  mismatch in the bilinear/trilinear fog bucket, so helpers were changed to
  macros that keep `addlong()` and patch helpers in the caller `block_pos`
  scope.
- Build/sign passed after source edits.
- Short verify with metrics passed:
  `verify=10240000`, `mismatch_spans=0`, `fb_mismatches=0`,
  `aux_mismatches=0`, `state_mismatches=0`.
- Metrics line emitted:
  `mru_hits=865095`, `scan_hits=1459499`, `misses=29`, `compiles=29`,
  `rejects=0`, `code_bytes=42788`, `code_max=1868`.
- Known guest noise `[0147:0000B9BD] Illegal instruction 00008B55 (FF)`
  appeared and was ignored.

### 2026-05-31: N1 ARM64 texture address emit refactor slice 2

- Reused the bilinear `tex_w_mask[tmu][lod]` load in the non-clamped S path.
- Kept the mask in `w16` after the initial S wrap and reused it for the later
  S-edge compare before the wrap case.
- Removed one later `PARAMS_tex_w_mask_n(tmu)` base setup plus indexed load in
  that path.
- Kept the live-register contract and texture state store placement unchanged.
- Build/sign passed after source edits.
- Short verify with metrics passed:
  `verify=10240000`, `mismatch_spans=0`, `fb_mismatches=0`,
  `aux_mismatches=0`, `state_mismatches=0`.
- Metrics line emitted:
  `mru_hits=466146`, `scan_hits=603065`, `misses=29`, `compiles=29`,
  `rejects=0`, `code_bytes=42436`, `code_max=1852`.
- Known guest noise `[0147:0000B9BD] Illegal instruction 00008B55 (FF)`
  appeared and was ignored.

### 2026-05-31: N1 ARM64 texture address emit refactor slice 3

- Converted the point-sampled texture path to the same ARM64-local texture
  emitter macros used by the bilinear path.
- Kept the point-path instruction selection, live-register contract, and
  texture state store placement unchanged.
- Build/sign passed after source edits.
- First short verify failed in a bilinear/trilinear fog bucket unrelated to the
  point-path conversion, with `mismatch_spans=1`, `fb_mismatches=1`, and
  `state_mismatches=1`; reran before changing code.
- Rerun short verify with metrics passed:
  `verify=10240000`, `mismatch_spans=0`, `fb_mismatches=0`,
  `aux_mismatches=0`, `state_mismatches=0`.
- Metrics line emitted on the passing run:
  `mru_hits=741294`, `scan_hits=1209631`, `misses=29`, `compiles=29`,
  `rejects=0`, `code_bytes=42356`, `code_max=1848`.
- Known guest noise `[0147:0000B9BD] Illegal instruction 00008B55 (FF)`
  appeared and was ignored.

### 2026-05-31: N1 ARM64 texture address emit refactor slice 4

- Added named ARM64-local shift setup macros for bilinear and point texture
  sampling.
- Converted the bilinear and point shift setup code to those helpers without an
  instruction-count target.
- Kept texture state store placement unchanged.
- Build/sign passed after source edits.
- First short verify failed in the bilinear/trilinear fog bucket with
  `mismatch_spans=2` and `state_mismatches=2`; reran before changing code.
- Rerun short verify with metrics passed:
  `verify=10240000`, `mismatch_spans=0`, `fb_mismatches=0`,
  `aux_mismatches=0`, `state_mismatches=0`.
- Metrics line emitted on the passing run:
  `mru_hits=460427`, `scan_hits=595704`, `misses=29`, `compiles=29`,
  `rejects=0`, `code_bytes=42436`, `code_max=1852`.
- Known guest noise `[0147:0000B9BD] Illegal instruction 00008B55 (FF)`
  appeared and was ignored.

### 2026-05-31: N3 conditional callee-saved proof groundwork

- Added an ARM64-local callee-saved register-use bitmap for generated blocks.
- Added passive assertions tying the bitmap to existing feature predicates for
  `x19-x23`, `x25-x26`, `d8-d11`, and `d14`.
- Documented the current generated-block use of fixed callee-saved registers:
  `x24`, `x27-x28`, `d12-d13`, and `d15`.
- Explicitly kept the 176-byte frame, save/restore layout, prologue, and
  epilogue behavior unchanged.
- Build/sign passed after source edits.
- Short verify with metrics passed:
  `verify=10240000`, `mismatch_spans=0`, `fb_mismatches=0`,
  `aux_mismatches=0`, `state_mismatches=0`.
- Metrics line emitted:
  `mru_hits=452309`, `scan_hits=586480`, `misses=29`, `compiles=29`,
  `rejects=0`, `code_bytes=42436`, `code_max=1852`.
- Known guest noise `[0147:0000B9BD] Illegal instruction 00008B55 (FF)`
  appeared and was ignored.

### 2026-05-31: N3 conditional d10/d11 save/restore slice

- Used the callee-saved bitmap to gate the `d10,d11` save/restore pair.
- Kept the 176-byte frame size and fixed slot offsets unchanged.
- `d10` remains tied to `cc_invert_output`; `d11` remains tied to fogColor use.
- Left all other callee-saved GPR/NEON save/restore behavior unchanged.
- Build/sign passed after source edits.
- Short verify with metrics passed:
  `verify=10240000`, `mismatch_spans=0`, `fb_mismatches=0`,
  `aux_mismatches=0`, `state_mismatches=0`.
- Metrics line emitted:
  `mru_hits=469365`, `scan_hits=607370`, `misses=29`, `compiles=29`,
  `rejects=0`, `code_bytes=42140`, `code_max=1840`.
- Known guest noise `[0147:0000B9BD] Illegal instruction 00008B55 (FF)`
  appeared and was ignored.

### 2026-05-31: N3 conditional d8/d9 save/restore slice

- Used the callee-saved bitmap to gate the `d8,d9` save/restore pair.
- Kept the 176-byte frame size and fixed slot offsets unchanged.
- `d8` remains tied to `neon_01_w` use; `d9` remains tied to `neon_ff_w` use.
- Left all GPR save/restore and other NEON save/restore behavior unchanged.
- Build/sign passed after source edits.
- Short verify with metrics passed:
  `verify=10240000`, `mismatch_spans=0`, `fb_mismatches=0`,
  `aux_mismatches=0`, `state_mismatches=0`.
- Metrics line emitted:
  `mru_hits=474658`, `scan_hits=617799`, `misses=29`, `compiles=29`,
  `rejects=0`, `code_bytes=42116`, `code_max=1840`.
- Known guest noise `[0147:0000B9BD] Illegal instruction 00008B55 (FF)`
  appeared and was ignored.

### 2026-05-31: N4 TMU combine factor emitter slice 1

- Added ARM64-local TMU combine factor emitter helpers for
  `TC_MSELECT_DETAIL` and `TC_MSELECT_LOD_FRAC`.
- Converted only the TMU1 RGB `tc_mselect_1` path.
- Kept the emitted instruction order and live-register contract explicit:
  detail uses `w4` for the factor, `w10` for `STATE_lod`, and `w11` for
  `detail_max`; LOD-frac loads the factor into `w4`.
- Did not move texture state stores for `STATE_tex_s`, `STATE_tex_t`,
  `STATE_lod`, or `STATE_lod_frac_n(tmu)`.
- Build/sign passed after source edits.
- Short verify with metrics passed:
  `verify=10240000`, `mismatch_spans=0`, `fb_mismatches=0`,
  `aux_mismatches=0`, `state_mismatches=0`.
- Metrics line emitted:
  `mru_hits=450940`, `scan_hits=583007`, `misses=29`, `compiles=29`,
  `rejects=0`, `code_bytes=42196`, `code_max=1844`.
- Known guest noise `[0147:0000B9BD] Illegal instruction 00008B55 (FF)`
  appeared and was ignored.

### 2026-05-31: N4 UT GOTY detail-texture exploratory run

- Ran Unreal Tournament GOTY with Glide/detail textures to look for N4 target
  coverage.
- The run did not hit the factored TMU1 RGB detail/LOD-frac path:
  `textureMode1=8c26151f` and `textureMode1=8c261a1f` both decode with
  `tc_sub_clocal_1=0` and `tc_mselect_1=0`.
- The workload did expose an existing texture-state mismatch in
  `textureMode0=8c26151f`: JIT and interpreter RGB were initially identical,
  but `tex_s`/`tex_t` state diverged around negative coordinates.
- A trial change from signed `SDIV` to unsigned `UDIV` for the perspective
  reciprocal was tested and reverted; it worsened the run into framebuffer and
  aux mismatches and changed JIT LOD to `1` where interpreter state had LOD
  `8`.
- Rebuilt/signed after reverting the trial change, leaving the N4 helper-only
  source slice intact.

### 2026-05-31: N4 target coverage logging

- Added passive per-mode validation counters for TMU combine target coverage:
  RGB detail, RGB LOD-frac, alpha detail, and alpha LOD-frac for TMU0 and TMU1.
- The counters only accumulate spans where the corresponding `*_sub_clocal`
  predicate is active and the mselect value is `DETAIL` or `LOD_FRAC`.
- Printed the counters on existing `Voodoo validate mode[...]` lines so future
  game runs can prove whether the factored cases were emitted.
- Build/sign passed after source edits.
- No VM launched in this step.

### 2026-05-31: N4 target coverage run with UT GOTY

- Ran Unreal Tournament GOTY again with the new target coverage counters.
- Long verify window reached `verify=51200000`.
- The target counters stayed zero in all logged mode buckets:
  `tmu0_rgb_detail=0`, `tmu0_rgb_lod_frac=0`, `tmu1_rgb_detail=0`,
  `tmu1_rgb_lod_frac=0`, `tmu0_alpha_detail=0`,
  `tmu0_alpha_lod_frac=0`, `tmu1_alpha_detail=0`, and
  `tmu1_alpha_lod_frac=0`.
- Seen texture modes remained limited to:
  `textureMode0=80000a1f/84824a1f/8c26151f/8c261a1f/8c261c19` and
  `textureMode1=8c26151f/8c261a1f`.
- The run still exposed the known UT texture-state mismatch:
  `mismatch_spans=5407`, `fb_mismatches=0`, `aux_mismatches=0`,
  `state_mismatches=5407`.
- Conclusion: UT GOTY is useful for the separate negative-coordinate
  texture-state issue, but it does not cover the N4 detail/LOD-frac combine
  target.

### 2026-05-31: UT GOTY negative-W texture-state fix

- Fixed the ARM64 perspective texture-coordinate path for negative `tmu_w`.
- Interpreter truth uses `(1ULL << 48) / tmu_w` for coordinate recovery, so a
  negative `tmu_w` produces a zero S/T reciprocal. ARM64 had reused the signed
  `SDIV` quotient for S/T and left bilinear `STATE_tex_s`/`STATE_tex_t` at
  negative post-bias coordinates such as `fffffc88`/`fffffaf5` instead of the
  interpreter's `fffff800`/`fffff800`.
- Kept the existing signed quotient for LOD, avoiding the earlier blind
  `UDIV` regression where framebuffer/aux mismatches appeared and LOD changed
  from `8` to `1`.
- Added a 64-bit ARM64 `CSEL` encoding helper and selected a zero coordinate
  reciprocal only when `tmu_w <= 0`; no helper-backed dynarec path was added.
- Clean build/sign passed after source edits.
- UT GOTY Glide/detail-textures verify run:
  `verify=47386125`, `mismatch_spans=0`, `fb_mismatches=0`,
  `aux_mismatches=0`, `state_mismatches=0`.
- Metrics line emitted:
  `mru_hits=560647`, `scan_hits=1256527`, `misses=804`, `compiles=804`,
  `rejects=0`, `code_bytes=944384`, `code_max=1820`.
- Known guest noise `[0147:0000B9BD] Illegal instruction 00008B55 (FF)`
  appeared and was ignored.
- Follow-up broader game sweep used `VOODOO_VALIDATE_LIMIT=204800000` and hit
  the cap within a longer run:
  `spans=277620239`, `verify=204800000`, `skipped=0`,
  `mismatch_spans=0`, `fb_mismatches=0`, `aux_mismatches=0`,
  `state_mismatches=0`.
- Follow-up metrics line emitted:
  `mru_hits=6626350`, `scan_hits=9819941`, `misses=4604`,
  `compiles=4604`, `rejects=0`, `code_bytes=5886664`, `code_max=1868`.

### 2026-05-31: N4 TMU combine factor emitter slice 2

- Converted the remaining ARM64 `DETAIL` and `LOD_FRAC` factor emission sites
  to the existing helper macros:
  TMU1 alpha `tca_mselect_1`, TMU0 RGB `tc_mselect`, and TMU0 alpha
  `tca_mselect`.
- Kept helper use limited to ARM64-local codegen emission; no helper-backed
  dynarec path was added.
- Preserved the existing live-register contracts:
  RGB factors still land in `w4` before vector duplication, TMU1 alpha uses
  `w4`, and TMU0 alpha uses `w5`; `w10` remains the `STATE_lod` scratch and
  `w11` remains the `detail_max` scratch.
- Build/sign passed after source edits.
- No VM launched in this step per user direction to continue only up to launch.
- Follow-up VM verify passed:
  `verify=51200000`, `skipped=0`, `mismatch_spans=0`, `fb_mismatches=0`,
  `aux_mismatches=0`, `state_mismatches=0`.
- Coverage showed the TMU0 `LOD_FRAC` helper paths were exercised:
  representative buckets had `tmu0_rgb_lod_frac=4369106` and
  `tmu0_alpha_lod_frac=4369106`; `DETAIL` and TMU1 target counters remained
  zero in this run.
- Metrics line emitted:
  `mru_hits=13365309`, `scan_hits=8681811`, `misses=15942`,
  `compiles=15942`, `rejects=0`, `code_bytes=20412416`, `code_max=1868`.
- Known guest noise `[0147:0000B9BD] Illegal instruction 00008B55 (FF)`
  appeared and was ignored.

### 2026-05-31: N4 TMU combine reverse-blend helper slice 3

- Added ARM64-local helper macros for repeated TMU combine reverse-blend
  emission:
  RGB vector factors use `ARM64_EMIT_TMU_COMBINE_RGB_REVERSE_BLEND`, scalar
  alpha factors use `ARM64_EMIT_TMU_COMBINE_ALPHA_REVERSE_BLEND`.
- Converted TMU1 RGB, TMU1 alpha, TMU0 RGB, and TMU0 alpha reverse-blend
  emission sites.
- Preserved existing trilinear and non-trilinear behavior:
  RGB paths still use `neon_00_ff_w` for trilinear or `neon_ff_w` for
  non-trilinear inversion; alpha paths still use `i_00_ff_w` for trilinear or
  `EOR #0xff` for non-trilinear inversion.
- Kept the existing TMU1 alpha note that ARM64 intentionally uses
  `tca_reverse_blend_1`, not the x86-64 RGB reverse-blend flag.
- Build/sign passed after source edits.
- VM verify passed:
  `verify=51200000`, `skipped=0`, `mismatch_spans=0`, `fb_mismatches=0`,
  `aux_mismatches=0`, `state_mismatches=0`.
- The run hit the configured verify cap:
  `spans=248327323`, `verify=51200000`.
- Coverage again exercised TMU0 `LOD_FRAC` helper paths, with representative
  buckets showing `tmu0_rgb_lod_frac=4312302` and
  `tmu0_alpha_lod_frac=4312302`.
- `DETAIL` and TMU1 target counters remained zero, so those target modes still
  need a different workload or directed coverage probe.
- Metrics line emitted:
  `mru_hits=12672010`, `scan_hits=8767387`, `misses=8902`,
  `compiles=8902`, `rejects=0`, `code_bytes=11112364`, `code_max=1868`.
- Known guest noise `[0147:0000B9BD] Illegal instruction 00008B55 (FF)`
  appeared and was ignored.

### 2026-05-31: N4 directed Glide coverage probe prep

- Added `tools/voodoo_n4_probe/voodoo_n4_probe.c`, a minimal Win32 Glide2
  console probe for the Windows 98 Gaming PC VM.
- The probe dynamically loads `glide2x.dll`, configures dual-TMU
  `GR_COMBINE_FUNCTION_BLEND` with `GR_COMBINE_FACTOR_DETAIL_FACTOR` and
  `GR_COMBINE_FACTOR_LOD_FRACTION`, and draws bilinear textured triangles to
  force target TMU combine states under validator coverage.
- Built `tools/voodoo_n4_probe/N4PROBE.EXE` as a K6-class PE32 console binary
  with no CRT dependency; import table contains only `KERNEL32.dll`.
- Created `/tmp/86box-voodoo-n4/n4probe.iso` for Windows 98 CD transfer.
- No emulator semantic change in this prep step; the next VM run should mount
  the probe ISO and inspect whether target TMU detail/LOD-frac counters rise
  while keeping framebuffer/aux/state validation clean.

### 2026-05-31: N4 directed Glide coverage probe result

- Updated the probe to create a real Win32 window for `grSstWinOpen`, write
  `C:\N4PROBE.TXT`, and show a completion message box.
- The updated Win98/K6-class probe imports only `KERNEL32.dll` and `USER32.dll`
  and still has no CRT dependency.
- Rebuilt with `scripts/setup-and-build.sh build` before VM validation.
- Windows 98 Gaming PC VM was run with `VOODOO_VALIDATE_LIMIT=409600000` and
  the probe ISO mounted.
- Directed probe hit every remaining N4 target coverage counter:
  `target_tmu0_rgb_detail=383280`,
  `target_tmu0_rgb_lod_frac=383280`,
  `target_tmu0_alpha_detail=383280`,
  `target_tmu0_alpha_lod_frac=383280`,
  `target_tmu1_rgb_detail=383280`,
  `target_tmu1_rgb_lod_frac=383280`,
  `target_tmu1_alpha_detail=383280`, and
  `target_tmu1_alpha_lod_frac=383280`.
- Coverage summary:
  `texture_disabled=57600`, `tmu0_local=1355347`,
  `tmu0_passthrough=0`, `dual_tmu_combine=766560`,
  `tmu0_rgb_mselect=[0,0,0,0,383280,383280,0,0]`,
  `tmu0_alpha_mselect=[0,0,0,0,383280,383280,0,0]`,
  `tmu1_rgb_mselect=[0,0,0,0,383280,383280,0,0]`,
  `tmu1_alpha_mselect=[0,0,0,0,383280,383280,0,0]`.
- Validation stayed strict-clean:
  `verify=2179507`, `skipped=0`, `mismatch_spans=0`,
  `fb_mismatches=0`, `aux_mismatches=0`, `state_mismatches=0`.
- The run did not hit the configured cap: `verify=2179507` out of
  `409600000`.
- Metrics line emitted:
  `mru_hits=69004`, `scan_hits=124404`, `misses=6`, `compiles=6`,
  `rejects=0`, `code_bytes=8292`, `code_max=1864`.
- Known guest noise `[0147:0000B9BD] Illegal instruction 00008B55 (FF)`
  appeared and was ignored.

### 2026-05-31: N4 directed coverage cleanup

- Removed the temporary global TMU coverage counters and
  `Voodoo validate TMU coverage` runtime log from the emulator before commit.
- Kept the directed Win32 Glide probe source as the reproducible workload for
  N4 `DETAIL`/`LOD_FRAC` coverage.
- Kept the existing per-mode validation buckets and strict mismatch validator
  behavior unchanged.

### 2026-05-31: N4 ARM64 TMU helper factoring

- Subagent A proposed and checked a mechanical trilinear reverse-blend setup
  helper for the duplicated TMU1/TMU0 `STATE_lod`, `lod & 1`,
  `tc_reverse_blend`, and `tca_reverse_blend` setup.
- Subagent B proposed and checked a mechanical RGB multiply helper for the
  repeated `SMULL` -> `SSHR #8` -> `SQXTN` TMU combine sequence, preserving the
  TMU1 pre-multiply clocal negation.
- Subagent C proposed and checked a two-shape alpha clamp helper, preserving
  the current TMU1 upper-clamp `CSEL` shape and the TMU0 negative-zero plus
  upper-clamp shape.
- Applied the three accepted ARM64-local mechanical helpers in
  `src/include/86box/vid_voodoo_codegen_arm64.h`.
- No x86-64 codegen changes; interpreter remains semantic truth.
- Build/sign passed with `./scripts/setup-and-build.sh build`.
- Added `scripts/launch-voodoo-validate-vm.sh` so future validation launches use
  `-L` logfile capture, LaunchServices-compatible validator env, duplicate
  process refusal, and default `VOODOO_VALIDATE_LIMIT=409600000`.
- VM verify passed:
  `verify=409600000`, `skipped=0`, `mismatch_spans=0`, `fb_mismatches=0`,
  `aux_mismatches=0`, `state_mismatches=0`.
- The run exceeded the configured verify cap:
  `spans=546016911`, `verify=409600000`.
- Metrics line emitted:
  `mru_hits=11330491`, `scan_hits=18191366`, `misses=9510`,
  `compiles=9510`, `rejects=0`, `code_bytes=12062128`, `code_max=1868`.
- Known guest noise `[0147:0000B9BD] Illegal instruction 00008B55 (FF)`
  appeared and was ignored.

### 2026-05-31: N5 alpha+dither dither-base x21/x20 candidates launched

- Extended the alpha+dither dither-base pinned-register candidate list in
  `src/include/86box/vid_voodoo_codegen_arm64.h`: after `x22`, `x23`, `x25`,
  and `x19`, the generator now tries `x21` then `x20` when their original
  lookup roles are not live.
- Preserved `x20` for non-constant fog or alpha `alookup`, and preserved `x21`
  for `aminuslookup`; the dither base is loaded there only when the matching
  `need_x20` or `need_x21` predicate is false before candidate selection.
- Updated callee-saved need bits and pointer loads so `x20`/`x21` receive
  `dither_rb` or `dither_rb2x2` only when selected as the dither-base register.
- Updated `src/video/vid_voodoo_render.c` N5 metrics so pinned-base and true
  fallback predicates match the codegen candidate list, including the existing
  true-fallback shape counters.
- Preserved the metric-only true-fallback shape counter work already present in
  `src/include/86box/vid_voodoo_common.h`, `src/video/vid_voodoo_render.c`, and
  `src/video/vid_voodoo.c`.
- `git diff --check` passed before build/sign.
- `./scripts/build-and-sign.sh` passed; linker emitted the existing macOS
  deployment-target dylib warnings and finished with `BUILD + SIGN OK`.
- Launched short VM validation with metrics:
  `./scripts/launch-voodoo-validate-vm.sh --limit 10240000 --log-limit 8 --metrics 1`.
- Launch result: PID `29234`, `VOODOO_VALIDATE=verify`,
  `VOODOO_VALIDATE_LIMIT=10240000`, `VOODOO_VALIDATE_LOG_LIMIT=8`,
  `VOODOO_ARM64_JIT_METRICS=1`.
- Per workflow, no log polling or validation-result inspection was done after
  launch.

### 2026-05-31: N5 alpha+dither x21/x20 validation result

- Short VM validation passed after the `x21`/`x20` candidate slice:
  `verify=10240000`, `skipped=0`, `mismatch_spans=0`, `fb_mismatches=0`,
  `aux_mismatches=0`, `state_mismatches=0`, `rejects=0`.
- Short-run N5 metric split:
  `dither_ptr_fallback_pixels=301723361`,
  `dither_base_pinned_pixels=289821309`,
  `dither_ptr_true_fallback_pixels=11902052`.
- Compared with the pre-`x21`/`x20` short run
  `dither_ptr_true_fallback_pixels=242303696`, the new candidates removed
  about 95.1% of the remaining true fallback in that workload.
- The only remaining short-run true fallback shape was shape `63`:
  `need_x19=1`, `need_x20=1`, `need_x21=1`, `need_x22=1`,
  `need_x23=1`, `need_x25=1`.
- Longer VM validation passed cleanly before the configured cap:
  `verify=215440463`, `skipped=0`, `mismatch_spans=0`, `fb_mismatches=0`,
  `aux_mismatches=0`, `state_mismatches=0`, `rejects=0`.
- Longer-run N5 metric split:
  `dither_ptr_fallback_pixels=4171342558`,
  `dither_base_pinned_pixels=4147282436`,
  `dither_ptr_true_fallback_pixels=24060122`.
- In the longer run, true fallback is now about 0.577% of alpha+dither
  fallback pixels, and the only remaining true fallback shape is again shape
  `63` with all candidate registers live.
- Result: `x21`/`x20` candidate pinning is accepted. Remaining fallback is no
  longer a small spare-register slice; it requires broad register/frame
  redesign or should stay as the per-pixel fallback.
- Known guest noise `[0147:0000B9BD] Illegal instruction 00008B55 (FF)`
  appeared and was ignored.

### 2026-05-31: N6 ARM64 predicate decode first slice

- Added an ARM64 generator predicate decode helper in
  `src/include/86box/vid_voodoo_regs.h` and consumed it from
  `src/include/86box/vid_voodoo_codegen_arm64.h`.
- The helper names the mode predicates that previously sat as repeated local
  setup in `voodoo_generate()`: TMU fetch shape, alpha blend functions,
  `x20`/`x21` lookup liveness, dither/RGB-write state, `x26` dither-base
  eligibility, and alpha+dither dither-pointer fallback eligibility.
- This slice is an audit/guardrail refactor, not a runtime speed win. The point
  is to make later register/layout decisions consume one decoded ARM64 mode
  shape instead of retyping fragile predicate logic around the generator.
- No `src/video/vid_voodoo_render.c` changes were made in this first slice, so
  the shared render path and x86-64 JIT-visible render behavior were left
  untouched.
- No x86 or x86-64 codegen files changed. No emitted arithmetic or TMU
  detail/LOD-frac arithmetic changed.
- `git diff --check` passed.
- `./scripts/build-and-sign.sh` passed with `BUILD + SIGN OK`.
- Short VM validation passed:
  `verify=10240000`, `skipped=0`, `mismatch_spans=0`, `fb_mismatches=0`,
  `aux_mismatches=0`, `state_mismatches=0`, `rejects=0`,
  `code_bytes=61804`, `code_max=1868`.
- Soak VM validation hit the configured verify cap and passed:
  `verify=51200000`, `spans=374562938`, `skipped=0`, `mismatch_spans=0`,
  `fb_mismatches=0`, `aux_mismatches=0`, `state_mismatches=0`, `rejects=0`,
  `code_bytes=12039596`, `code_max=1868`.
- Shape `63` remains the only true fallback shape:
  `dither_ptr_true_fallback_pixels=36527831`, with
  `need_x19=1`, `need_x20=1`, `need_x21=1`, `need_x22=1`, `need_x23=1`, and
  `need_x25=1`.
- Known guest noise `[0147:0000B9BD] Illegal instruction 00008B55 (FF)`
  appeared and was ignored.

### 2026-05-31: N6 metrics decode and coverage hardening

- Updated `voodoo_arm64_jit_n5_count_span()` in
  `src/video/vid_voodoo_render.c` to consume the shared ARM64 predicate decode
  helper from `src/include/86box/vid_voodoo_regs.h`.
- Removed the local duplicate ARM64 alpha lookup predicate helpers from the
  render metrics path. The N5 dither-base pinned/fallback and true-fallback
  shape counters now use the same decoded TMU fetch, alpha, lookup-liveness,
  and dither predicates as the ARM64 generator.
- Hardened TMU detail/LOD-frac coverage accounting in
  `voodoo_validate_mode_accum()`: TMU0 RGB/alpha detail and LOD-frac counters
  now count actual dual-TMU factor switch use, while TMU1 remains gated by the
  TMU1 sub-clocal path that guards the interpreter switch.
- No new coverage fields or log format changes were needed.
- No x86 or x86-64 codegen files changed. No ARM64 emitted arithmetic changed.
  Render pixel math and interpreter semantics were unchanged.
- `git diff --check` passed.
- `./scripts/build-and-sign.sh` passed with `BUILD + SIGN OK`.
- VM validation hit the configured cap and passed:
  `verify=51200000`, `spans=116660354`, `skipped=0`, `mismatch_spans=0`,
  `fb_mismatches=0`, `aux_mismatches=0`, `state_mismatches=0`, `rejects=0`,
  `code_bytes=4814308`, `code_max=1868`.
- The top coverage buckets include TMU0 LOD-frac proof, for example
  `tmu0_rgb_lod_frac=4319892` and `tmu0_alpha_lod_frac=4319892`.
- Shape `63` remains the only true fallback shape:
  `dither_ptr_true_fallback_pixels=12037650`, with all candidate registers
  live.
- Known guest noise `[0147:0000B9BD] Illegal instruction 00008B55 (FF)`
  appeared and was ignored.

### 2026-06-01: B3 bilinear weight-index lookup cleanup

- Optimized the ARM64 bilinear weight-index setup in
  `codegen_texture_fetch()` in `src/include/86box/vid_voodoo_codegen_arm64.h`.
- Scope was limited to the hot `frac_s`/`frac_t` to `bilinear_lookup` address
  setup. Interpreter-visible `STATE_tex_s`, `STATE_tex_t`, `STATE_lod`, and
  `STATE_lod_frac_n(tmu)` behavior was unchanged.
- Previous emitted sequence copied S/T fractions into two scratch registers,
  masked and shifted them, ORed the scaled bilinear index, shifted it by 5, and
  copied it to `w17` before the lookup-base add.
- New sequence keeps the unscaled bilinear index in `w10`: `AND w10,w4,#0xf`,
  `BFI w10,w5,#4,#4`, then later `ADD x11,x25,x10,LSL #5`.
- Local instruction map changed from 11 emitted instructions to 5 emitted
  instructions for the bilinear weight-index/lookup-base setup.
- No sampling-address math, bilinear weights, bilinear blend arithmetic, or
  T-edge cold tails changed. No x86 or x86-64 codegen files changed.
- Worker validation passed: `git diff --check` and `./scripts/build-and-sign.sh`
  both succeeded; build/sign ended with `BUILD + SIGN OK`.
- Short VM validation passed:
  `verify=10240000`, `skipped=0`, `mismatch_spans=0`, `fb_mismatches=0`,
  `aux_mismatches=0`, `state_mismatches=0`, `rejects=0`,
  `code_bytes=41724`, `code_max=1820`.
- Compared with the current committed comparable short-run baseline
  `code_bytes=42116`, `code_max=1840`, B3 reduced `code_bytes` by 392 and
  `code_max` by 20.
- Extended near-unbounded-cap VM validation passed:
  `verify=484850615`, `spans=484850615`, `skipped=0`, `mismatch_spans=0`,
  `fb_mismatches=0`, `aux_mismatches=0`, `state_mismatches=0`, `rejects=0`,
  `code_bytes=9777472`, `code_max=1816`.
- Long-run bilinear coverage was substantial:
  `tmu0_bilinear_pixels=3492218775` and
  `tmu1_bilinear_pixels=5368473521`.
- Result: B3 is accepted. It is a metric-backed ARM64-local generated-code
  reduction with clean framebuffer, aux, state, and reject gates.
- Known guest noise `[0147:0000B9BD] Illegal instruction 00008B55 (FF)`
  appeared and was ignored.

### 2026-06-01: B4 bilinear T1 setup cleanup

- Optimized the ARM64 bilinear T1 setup in `codegen_texture_fetch()` in
  `src/include/86box/vid_voodoo_codegen_arm64.h`.
- Scope was limited to forming the next-row coordinate before T clamp/wrap.
  The old sequence emitted `MOV w13,w5`, then later `ADD w13,w13,#1`; the new
  sequence emits `ADD w13,w5,#1` directly.
- Local instruction map changed by one emitted instruction per bilinear fetch
  site. The T1 value remains exactly `T + 1`.
- Interpreter truth in `src/video/vid_voodoo_render.c` samples `t` and `t + 1`
  for bilinear fetches; no state store, sample address, clamp/wrap edge, weight,
  or blend arithmetic changed.
- No T-edge cold tails changed. No x86 or x86-64 codegen files changed.
- Worker validation passed: `git diff --check` and `./scripts/build-and-sign.sh`
  both succeeded; build/sign ended with `BUILD + SIGN OK`.
- Short VM validation passed:
  `verify=10240000`, `skipped=0`, `mismatch_spans=0`, `fb_mismatches=0`,
  `aux_mismatches=0`, `state_mismatches=0`, `rejects=0`,
  `code_bytes=41548`, `code_max=1812`.
- Compared with the B3 accepted short-run result `code_bytes=41724`,
  `code_max=1820`, B4 reduced `code_bytes` by 176 and `code_max` by 8.
- Extended near-unbounded-cap VM validation passed:
  `verify=245110407`, `spans=245110407`, `skipped=0`, `mismatch_spans=0`,
  `fb_mismatches=0`, `aux_mismatches=0`, `state_mismatches=0`, `rejects=0`,
  `code_bytes=9424360`, `code_max=1812`.
- Long-run bilinear coverage was substantial:
  `tmu0_bilinear_pixels=2618420397` and
  `tmu1_bilinear_pixels=3134785144`.
- Result: B4 is accepted. It is a metric-backed ARM64-local generated-code
  reduction with clean framebuffer, aux, state, and reject gates.
- Known guest noise `[0147:0000B9BD] Illegal instruction 00008B55 (FF)`
  appeared and was ignored.
