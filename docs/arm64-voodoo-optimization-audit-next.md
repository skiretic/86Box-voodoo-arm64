# ARM64 Voodoo Optimization Audit Next

Status: 2026-05-31
Source head: `3f5cbc8090934b8df484eaf0d004bca81a26dc32`

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

## Candidates

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

- Medium I-cache and branch-predictability improvement if games mostly sample
  interior texels and use common dither table offsets.

Risk:

- High. Patch bookkeeping becomes more complex and block-size pressure may
  increase.

Validation needed:

- N2 metrics first: code size, branch/cold block count, and coverage of edge
  cases.
- Strong soak after any layout change.
- A targeted texture-edge workload if available.

x86-64:

- No x86-64 change for ARM64 generated layout.

Larger refactor justified:

- Not yet. Needs N2 metrics and N1 helper structure first.

First safe implementation slice:

- None yet. Track as design-only until metrics prove hot interior sampling
  dominates.

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

Opportunity:

- Convert scattered mode-bit locals into a named decoded-mode struct used by
  interpreter and codegen include boundaries.
- This would make ARM64 feature predicates and validation buckets less fragile.

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

- Add a read-only decoded-mode struct while retaining existing local names.
  Populate and log/validate selected fields. Do not switch codegen users yet.

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
- Do not implement cold-path block layout before N2 metrics prove code-size or
  hot-path density benefit.
- Do not conditionally skip ABI saves/restores before a register-use bitmap is
  reviewed.

## Recommended First Implementation Slice

Start with N2, then N1.

Reason:

- N2 gives proof signal for all larger refactors: code size, cache behavior,
  coverage buckets, and clean-span mode mix.
- N1 is the best first code-quality refactor after that because texture fetch is
  still the dominant ARM64 hot path, P5 made its state contract strict, and the
  existing validator can prove helper-only factoring.

First slice:

- Add ARM64-only opt-in metrics for cache MRU hit, scan hit, miss, reject, and
  generated code size.
- Keep pass/fail semantics unchanged.
- Build/sign.
- Launch VM for validation only after build/sign.
- Hand off VM run and inspect results only after guest run is reported done.

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
