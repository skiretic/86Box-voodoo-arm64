# ARM64 Voodoo Optimization Audit (Current Source)

Status: 2026-05-30
Source head: `6d4715b5ea438efdf2d529eb614d9c795f3f9754`

This audit intentionally starts from current source. It does not rely on the older optimization analysis documents.

Audited primary files:

- `src/include/86box/vid_voodoo_codegen_arm64.h`
- `src/include/86box/vid_voodoo_render.h`
- `src/video/vid_voodoo_render.c`
- `src/include/86box/vid_voodoo_common.h`

`src/include/86box/vid_voodoo_codegen_x86-64.h` was used only as pipeline-shape reference. The interpreter remains the semantic source of truth.

## Validation Baseline

Current validation logging supports:

- `VOODOO_VALIDATE=verify`
- `VOODOO_VALIDATE_LIMIT=<N>`
- `VOODOO_VALIDATE_LOG_LIMIT=<N>`
- `VOODOO_VALIDATE_MAX_SPAN=<N>` default `2048`
- `VOODOO_VALIDATE_FB_TOL=<N>` default `5`

Known clean caps before optimization:

- 3DMark99-style 100x cap: `verify=10240000`, `mismatch_spans=0`, `fb_mismatches=0`, `aux_mismatches=0`, `state_mismatches=0`
- Quake 3 demo four 100x cap: `verify=10240000`, `mismatch_spans=0`, `fb_mismatches=0`, `aux_mismatches=0`, `state_mismatches=0`

Any optimization should preserve exact zero mismatch counts. Do not treat the default framebuffer tolerance as permission for new drift.

Known recurring guest noise:

- `[0147:0000B9BD] Illegal instruction 00008B55 (FF)`

## Current Hot Path Shape

The ARM64 JIT emits one span function from `voodoo_generate()` and loops per pixel inside the generated code. Dominant verified texture modes exercise:

- dual TMU path
- perspective texture coordinates
- bilinear filtering
- trilinear / `LOD_FRAC` combine
- dithered RGB565 framebuffer write
- W fog
- no alpha blend in the dominant clean bucket

Important current parity fixes already present:

- fog multiply uses `(diff * (fog_a + 1)) >> 8`
- perspective texture coordinate rounding matches interpreter
- `lod_frac[tmu]` is stored before `lod >>= 8`
- texture coordinate scale uses `params->tex_lod[tmu][lod]`

## Candidates

### P1: Low-risk perspective texture-fetch peepholes

Expected impact: medium in current dominant textured modes; low elsewhere.

Risk: low. This is ARM64 instruction selection only, with no intended semantic change.

Exact paths:

- `src/include/86box/vid_voodoo_codegen_arm64.h:1312-1332`
- `src/include/86box/vid_voodoo_codegen_arm64.h:1471-1480`
- `src/include/86box/vid_voodoo_codegen_arm64.h:1558-1593`
- `src/include/86box/vid_voodoo_codegen_arm64.h:1612-1688`
- `src/include/86box/vid_voodoo_codegen_arm64.h:1784-1858`

Opportunity:

- Replace `MOVZ #1` + `LSL #13` with a single `MOVZ_X_HW(..., 0x2000, 0)` for the perspective pre-round constant.
- Replace `MOVZ #1` + `LSL #29` with a single `MOVZ_X_HW(..., 0x2000, 1)` for the post-multiply round constant.
- In bilinear paths, reduce duplicate indexed mask loads where `tex_w_mask[tmu][lod]` is loaded more than once for the same sample path.

Validation needed:

- full 100x 3DMark99-style verify
- Quake 3 demo four 100x verify
- inspect mode buckets for dominant dual-TMU bilinear/trilinear coverage

x86-64 parity/shared semantics:

- no x86-64 change required if kept as ARM64-local instruction selection
- shared semantic changes are not expected

### P2: Dither table pointer/offset hoist

Expected impact: medium to high when `FBZ_DITHER` is active. Current dominant clean bucket has dither enabled.

Risk: medium. Main risk is ARM64 register pressure and accidental clobbering across long generated loop bodies.

Exact paths:

- `src/include/86box/vid_voodoo_codegen_arm64.h:4050-4192`
- interpreter reference: `src/video/vid_voodoo_render.c:1490-1506`
- dither tables: `src/include/86box/vid_voodoo_dither.h`

Opportunity:

- Current dither path materializes the `dither_rb`/`dither_rb2x2` base pointer inside the pixel loop.
- It also materializes the `dither_g` relative offset inside the pixel loop.
- Hoist dither base data for dither-enabled blocks, or assign a block-local pinned register only when the active pipeline does not need one of the existing pinned lookup registers.

Validation needed:

- full 100x verify in dithered 2x2 and 4x4 modes
- verify no drift in `fb_mismatches`, including `VOODOO_VALIDATE_FB_TOL=0` classification runs
- Quake/3DMark coverage because both exercise dithered framebuffer writes

x86-64 parity/shared semantics:

- no x86-64 change required if ARM64-local
- x86-64 should be touched only if a shared dither abstraction is introduced

### P3: Move pixel/texel count accumulation out of the pixel loop

Expected impact: medium to high across nearly all JIT spans. Removes per-pixel counter memory traffic.

Risk: medium. Counters are visible through Voodoo stats, and same-run verifier does not fully prove JIT counter equivalence when it shadows with the interpreter.

Exact paths:

- ARM64 per-pixel counter updates: `src/include/86box/vid_voodoo_codegen_arm64.h:4346-4366`
- JIT span counter consumption: `src/video/vid_voodoo_render.c:1707-1709`
- interpreter per-pixel counter updates: `src/video/vid_voodoo_render.c:1116-1118`

Opportunity:

- Compute span pixel count once from `STATE_x`/`STATE_x2` and `xdir`.
- Add `pixel_count += span_count` once after the loop.
- Add `texel_count += span_count * texels_per_pixel` once after the loop.
- Preserve current texture count rules: `1` for pass-through/local single-fetch modes, `2` for dual-fetch mode.

Validation needed:

- full framebuffer/aux/state verify still required
- separate non-shadow JIT counter check, because verify mode restores state and runs interpreter for comparison
- compare `pixel_count`, `texel_count`, and `fbiPixelsIn` totals before/after on same guest workload

x86-64 parity/shared semantics:

- no x86-64 change required for ARM64-local counter accumulation
- if changing shared counter contract, both ARM64 and x86-64 must be updated

### P4: Alpha blend multiply-round lowering cleanup

Expected impact: high only on alpha-blended spans; low in the current dominant clean bucket where alpha blend is off.

Risk: medium-high. The blend math is exact 8-bit hardware-style rounding, and small drift is visible.

Exact paths:

- ARM64 alpha blend: `src/include/86box/vid_voodoo_codegen_arm64.h:3744-3993`
- interpreter macro: `src/include/86box/vid_voodoo_render.h:169-271`
- x86-64 reference shape: `src/include/86box/vid_voodoo_codegen_x86-64.h:2469-3058`

Opportunity:

- Current ARM64 repeats `MUL`, `USHR`, `ADD 1`, `ADD high`, `USHR` sequences across `dest_afunc` and `src_afunc`.
- First safe cleanup is an emitter helper that emits the same instruction sequence consistently.
- Any shorter sequence must be proven bit-exact for all `0..255` products before use.

Validation needed:

- targeted alpha-heavy runs with mode buckets showing `alpha_blend=1`
- full 100x verify after helper extraction
- extra directed proof if the arithmetic sequence changes, not just source factoring

x86-64 parity/shared semantics:

- no x86-64 change required for ARM64-only source factoring
- x86-64 must be included if the shared blend formula or rounding contract changes

### P5: Keep texture fetch intermediates in registers longer

Expected impact: high in textured spans if done well.

Risk: high. This touches recent parity fixes and interpreter-visible texture state fields.

Exact paths:

- ARM64 stores/reloads: `src/include/86box/vid_voodoo_codegen_arm64.h:1357-1406`
- bilinear reload: `src/include/86box/vid_voodoo_codegen_arm64.h:1479-1480`
- point reload: `src/include/86box/vid_voodoo_codegen_arm64.h:1799-1800`
- interpreter texture semantics: `src/video/vid_voodoo_render.c:294-421`

Opportunity:

- Avoid writing `tex_s`, `tex_t`, and `lod` only to reload them immediately for sampling.
- Preserve final `state->tex_s`, `state->tex_t`, `state->lod`, and `lod_frac[tmu]` semantics.
- Do not weaken the current `params->tex_lod[tmu][lod]` parity fix.

Validation needed:

- expand state validation temporarily to include `tex_s`, `tex_t`, `lod`, and `lod_frac`
- full 100x verify for dual-TMU trilinear modes
- Quake four-run verify

x86-64 parity/shared semantics:

- no x86-64 change required if final state contract remains identical
- if state field lifetime/meaning changes, x86-64 parity is required

### P6: Prologue and pinned-constant specialization

Expected impact: low to medium overall; higher on many short spans or frequent block transitions.

Risk: medium. ABI save/restore and callee-saved NEON usage must remain exact.

Exact paths:

- prologue save/load: `src/include/86box/vid_voodoo_codegen_arm64.h:1992-2153`
- epilogue restore: `src/include/86box/vid_voodoo_codegen_arm64.h:4400-4419`
- pointer constants: `src/include/86box/vid_voodoo_codegen_arm64.h:2036-2073`

Opportunity:

- Avoid loading unused pinned lookup pointers for blocks that cannot reach those paths.
- Consider conditional save/restore of callee-saved NEON registers only when used.
- Keep ABI simple until a register-use bitmap is explicit and reviewed.

Validation needed:

- build/sign first, because ABI mistakes may crash immediately
- full verify plus short-span workloads
- stress with texture off, alpha off, fog off, and simple solid-color spans

x86-64 parity/shared semantics:

- no x86-64 change required

### P7: JIT block cache lookup shaping

Expected impact: low to medium. This is per span/block lookup, not per pixel.

Risk: low-medium. Main risk is rejected-slot semantics and LRU behavior.

Exact paths:

- cache key storage: `src/include/86box/vid_voodoo_codegen_arm64.h:4474-4490`
- lookup/compile: `src/include/86box/vid_voodoo_codegen_arm64.h:4545-4628`

Opportunity:

- Add a direct MRU slot check before the 32-entry scan.
- Consider compact hash/tag only after measuring miss/scan cost.
- Preserve rejected-slot fast return and LRU eviction behavior.

Validation needed:

- full verify
- cache hit/miss instrumentation before/after if this becomes a real slice

x86-64 parity/shared semantics:

- no x86-64 change required

## Not Recommended Yet

Do not replace per-pixel `SDIV` in perspective texture fetch with approximate reciprocal math. That path is semantically hot and recently fixed for rounding/LOD parity.

Do not add helper-backed dynarec paths for Voodoo optimization. Temporary debug scaffolding is acceptable only if it directly proves a real lowering.

Do not trust x86-64 backend behavior where ARM64 comments already identify x86-64 parity hazards. Interpreter macros and current clean validator results are the guardrails.

## Recommended First Slice

Start with P1: ARM64-local texture-fetch peepholes.

Why:

- exercises current dominant dual-TMU perspective/bilinear path
- small patch surface
- no x86-64/shared semantic change
- validation directly covers expected output
- avoids the counter-validation gap in P3 and register-pressure risk in P2

First validation command after the slice:

```sh
VOODOO_VALIDATE=verify VOODOO_VALIDATE_LIMIT=10240000 VOODOO_VALIDATE_LOG_LIMIT=8 build/src/86Box.app/Contents/MacOS/86Box --vmpath '/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 Gaming PC'
```

Pass condition:

```text
verify=10240000
mismatch_spans=0
fb_mismatches=0
aux_mismatches=0
state_mismatches=0
```

Then repeat the same cap on the Quake 3 demo four-run validation before stacking P2 or P3.

## Correction Log

### 2026-05-30: P1 ARM64-local perspective texture-fetch peepholes corrected

Implemented the constant-load half of P1 only:

- replaced `MOVZ #1` + `LSL #13` with `ARM64_MOVZ_X_HW(10, 0x2000, 0)`
- replaced `MOVZ #1` + `LSL #29` with `ARM64_MOVZ_X_HW(10, 0x2000, 1)`
- left bilinear mask-load cleanup unchanged because same-semantics reuse was not clear enough
- made no x86-64 or shared semantic changes

Validation:

- build/sign passed
- baseline no-code-diff verify passed: `verify=10240000`, `mismatch_spans=0`, `fb_mismatches=0`, `aux_mismatches=0`, `state_mismatches=0`
- `1 << 13` isolated verify passed: `verify=10240000`, `mismatch_spans=0`, `fb_mismatches=0`, `aux_mismatches=0`, `state_mismatches=0`
- full P1 constants verify passed: `verify=10240000`, `mismatch_spans=0`, `fb_mismatches=0`, `aux_mismatches=0`, `state_mismatches=0`
- 5x 3DMark soak passed: `verify=51200000`, `mismatch_spans=0`, `fb_mismatches=0`, `aux_mismatches=0`, `state_mismatches=0`

Status: P1 constant-load peepholes corrected and closed by 5x 3DMark soak. Quake 3 is not required for P1 closure.

### 2026-05-30: P2 dither table pointer/offset hoist corrected

Implemented ARM64-local dither hoist only:

- x26 holds `dither_rb`/`dither_rb2x2` base for dithered RGB-write blocks when alpha blend is off
- alpha-blend blocks keep x26 as `rgb565`, preserving the destination decode path
- `dither_g`/`dither_g2x2` relative offset uses immediate addressing when encodable, with the prior materialized-offset fallback retained
- made no x86-64 or shared semantic changes

Validation:

- build/sign passed
- 3DMark verify passed: `verify=10240000`, `fb_tol=5`, `mismatch_spans=0`, `fb_mismatches=0`, `aux_mismatches=0`, `state_mismatches=0`
- 3DMark exact-fb classification passed: `verify=10240000`, `fb_tol=0`, `mismatch_spans=0`, `fb_mismatches=0`, `aux_mismatches=0`, `state_mismatches=0`
- 5x 3DMark exact-fb soak passed: `verify=51200000`, `fb_tol=0`, `mismatch_spans=0`, `fb_mismatches=0`, `aux_mismatches=0`, `state_mismatches=0`

Status: P2 dither hoist corrected and closed by 5x exact-fb 3DMark soak.

### 2026-05-30: P3 pixel/texel counter accumulation corrected

Implemented ARM64-local counter batching only:

- removed per-pixel `pixel_count`/`texel_count` memory update from the loop body
- computes span count after loop exit from original start X and final one-past-end X
- adds `pixel_count += span_count` once per generated span
- adds `texel_count += span_count` for pass-through/local single-fetch modes, or `span_count * 2` for dual-fetch modes
- preserves existing JIT handoff where `vid_voodoo_render.c` adds `state->pixel_count` to both `voodoo->pixel_count[odd_even]` and `voodoo->fbiPixelsIn`
- made no x86-64 or shared semantic changes

Validation:

- build/sign passed
- 3DMark verify rerun passed: `verify=10240000`, `fb_tol=5`, `mismatch_spans=0`, `fb_mismatches=0`, `aux_mismatches=0`, `state_mismatches=0`
- 5x 3DMark soak passed: `verify=51200000`, `fb_tol=5`, `mismatch_spans=0`, `fb_mismatches=0`, `aux_mismatches=0`, `state_mismatches=0`

Status: P3 counter batching corrected and closed by 5x 3DMark soak.

### 2026-05-30: P4 alpha blend multiply-round lowering cleanup corrected

Implemented ARM64-local source factoring only:

- added `ARM64_EMIT_ALPHA_BLEND_MUL_ROUND_V4H` for the existing alpha-blend `MUL`, `USHR`, `ADD 1`, `ADD high`, `USHR` sequence
- replaced repeated dest/src alpha blend multiply-round tails with the macro
- changed no arithmetic, blend factors, lookup table use, x86-64 code, or shared semantic contract
- made validation mode buckets accumulate clean verified spans too, so clean runs can prove `alpha_blend=1` coverage instead of only mismatch modes

Validation:

- build/sign passed
- Quake 3 verify passed: `verify=51200000`, `fb_tol=5`, `mismatch_spans=0`, `fb_mismatches=0`, `aux_mismatches=0`, `state_mismatches=0`
- clean coverage buckets included multiple `alpha_blend=1` modes, including `src_afunc=1 dest_afunc=5`, `src_afunc=4 dest_afunc=0`, `src_afunc=4 dest_afunc=4`, `src_afunc=2 dest_afunc=2`, and `src_afunc=2 dest_afunc=4`

Status: P4 alpha blend multiply-round factoring corrected and closed by 51200000-span Quake 3 alpha-blend coverage.

### 2026-05-30: P5 ARM64 texture fetch register lifetime corrected

Implemented ARM64-local texture intermediate retention only:

- removed the immediate `tex_s`/`tex_t` store-to-`LDP` reload pair before bilinear sampling
- removed the immediate `tex_s`/`tex_t` store-to-`LDP` reload pair before point sampling
- kept `tex_s`, `tex_t`, and integer `lod` in registers until sampling starts
- stores final interpreter-visible `state->tex_s` and `state->tex_t` after mirror/bilinear offset adjustment and before destructive sample-coordinate shifts
- preserved `state->lod`, `lod_frac[tmu]`, and `params->tex_lod[tmu][lod]` behavior
- expanded verify state comparison to include `tex_s`, `tex_t`, `lod`, and `lod_frac[0..1]`
- made no reciprocal math, x86-64, or shared semantic changes

Validation:

- build/sign passed
- strict state verify passed: `verify=10240000`, `fb_tol=5`, `mismatch_spans=0`, `fb_mismatches=0`, `aux_mismatches=0`, `state_mismatches=0`
- 5x strict state soak passed: `verify=51200000`, `fb_tol=5`, `mismatch_spans=0`, `fb_mismatches=0`, `aux_mismatches=0`, `state_mismatches=0`
- clean coverage included dual-TMU trilinear/fog modes with `tex0_tri=1` and `tex1_tri=1`

Status: P5 texture intermediate register lifetime corrected and closed by 51200000-span strict state soak.

### 2026-05-31: P6 ARM64 prologue and pinned-constant specialization corrected

Implemented ARM64-local prologue specialization only:

- added feature-bit predicates for pinned GPR lookup pointers `x19`, `x20`, `x21`, `x22`, `x23`, `x25`, and `x26`
- skipped unused `logtable`, alpha/fog lookup, trilinear reverse-blend, bilinear, and `rgb565`/dither pointer materialization when the generated block cannot reach those paths
- added feature-bit predicates for pinned NEON constants `v8`, `v9`, and `v10`
- skipped `fogColor` load into `v11` for fog modes that do not consume the fog color
- kept ABI save/restore exact and unchanged
- did not attempt conditional NEON save/restore or gate `v12`/`v14`/`v15` delta loads
- made no x86-64 or shared semantic changes

Validation:

- build/sign passed
- first gated-GPR verify passed: `verify=10240000`, `fb_tol=5`, `mismatch_spans=0`, `fb_mismatches=0`, `aux_mismatches=0`, `state_mismatches=0`
- gated-`v10` verify passed: `verify=10240000`, `fb_tol=5`, `mismatch_spans=0`, `fb_mismatches=0`, `aux_mismatches=0`, `state_mismatches=0`
- final P6 strong soak passed: `verify=51200000`, `fb_tol=5`, `mismatch_spans=0`, `fb_mismatches=0`, `aux_mismatches=0`, `state_mismatches=0`
- known guest noise `[0147:0000B9BD] Illegal instruction 00008B55 (FF)` appeared and was ignored

Status: P6 prologue and pinned-constant specialization corrected and closed by 51200000-span strong soak.
