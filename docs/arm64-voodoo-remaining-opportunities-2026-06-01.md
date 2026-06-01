# ARM64 Voodoo Remaining Optimization Opportunities

Status: 2026-06-01
Source head when opened: `7d49f1fa1`
Current accepted head: `7440c55ed`

Scope: discovery plus append-only execution log. The original ranked table is a
discovery queue; rank numbers are not closure claims. A rank is closed only when
the ledger below says `closed`.

Semantic source of truth remains the interpreter in
`src/video/vid_voodoo_render.c` and `src/include/86box/vid_voodoo_render.h`.
The x86 and x86-64 code generators are not correctness sources.

Current accepted stack:

- B11: bilinear weights `LDR q16` + `LDR q17` -> `LDP q16, q17`.
- B12: TMU0 LOD-frac alpha subtract uses `w13` directly for
  `textureMode0=4ec76a07` / `4ec76c07`.
- B13: alpha blend copies destination color to `v6` only when
  `src_afunc` needs destination color.
- Rank 1: bilinear shift setup const reuse.
- Ranks 2-4: guarded TMU0 LOD-frac alpha lane/dead-pack, shared reverse index,
  and `STATE_lod_frac[0]` scalar reuse.
- Rank 5: `AFUNC_AZERO` destination skip.
- Rank 6: perspective LOD shift temp kill.
- Rank 8: `src_afunc=4 dest_afunc=4` packed add.
- Rank 9a: fog table `UBFX` and W-fog byte load.
- Rank 12: alpha-test immediate compare.
- Rank 7a: alpha-out scalar no-write guard.

Current proof baseline:

```text
verify=769405607
skipped=0
mismatch_spans=0
fb_mismatches=0
aux_mismatches=0
state_mismatches=0
rejects=0
code_bytes=10861912
code_max=1776
```

Metric-era short delta:

```text
code_bytes 42788 -> 40884
code_max   1868  -> 1780
```

## Rank Closure Ledger

Status terms:

- `closed`: the candidate as written is implemented or explicitly rejected.
- `partial`: a safe sub-slice landed, but the rank still has live work.
- `deferred`: no current code work without stronger coverage or perf reason.

| Rank | Status | Notes |
| ---- | ------ | ----- |
| 1 | closed | Implemented and validated: bilinear shift setup const reuse. |
| 2 | closed | Implemented within guarded TMU0 LOD-frac bucket. |
| 3 | closed | Implemented with shared trilinear reverse index in the same guarded bucket. |
| 4 | closed | Implemented as scalar LOD-frac reuse stacked with rank 3. |
| 5 | closed | Implemented and validated: `dest_afunc == AFUNC_AZERO` skips dead destination add. |
| 6 | closed | Implemented and validated: perspective LOD shift uses BSR value directly. |
| 7 | closed | Rank 7a, 7b, 7c, and 7d landed; remaining source/destination alpha prep consumers audited against interpreter semantics and covered by probe validation. |
| 8 | closed | Implemented and validated: `src_afunc=4 dest_afunc=4` packed unsigned saturating add. |
| 9 | closed | Fog table `UBFX`, W-fog byte load, and FOG_Z `UBFX` implemented and validated; FOG_ALPHA preserved because it requires signed shift plus clamp. |
| 10 | deferred | Requires stronger perf reason for `x24`/dither true-fallback work. |
| 11 | closed | Implemented and validated: `v13` / color-before-fog gating and `d13` save/restore narrowing. |
| 12 | closed | Implemented and validated: alpha-test immediate compare from codegen key. |

## Active Queue

Next code work should not start from the raw rank table. Start from this queue:

1. Rank 10 stays deferred until a perf run shows dither true-fallback dominates.

Do not mark any future rank closed unless all sub-slices in the candidate are
implemented, rejected with evidence, or explicitly deferred in this ledger.

## Ranked Opportunities

| Rank | Area | Candidate | Expected win | Coverage confidence | Risk | Recommendation |
| ---- | ---- | --------- | ------------ | ------------------- | ---- | -------------- |
| 1 | Texture fetch / bilinear | `ARM64_EMIT_TEX_BILINEAR_SHIFT_SETUP`: reuse `w7=#8` for texel bias, emit `LSL w10,w7,w16` before `SUB w7,w7,w16`; remove second `MOVZ #8`. | `-1` instr per bilinear fetch site; likely `code_bytes` down about 176-256, possible `code_max -8` when max block has both TMUs. | High: existing `tmu0_bilinear_pixels` and `tmu1_bilinear_pixels` are huge in normal proof. | Low. Same `w10=8<<tex_lod`, same `w7=8-tex_lod`. | Implement first as single narrow slice. |
| 2 | TMU combine | TMU0 alpha lanes + dead packs in known LOD-frac buckets. Replace staged packed-alpha extracts with `UMOV` from halfword lane, and skip dead `SQXTUN` packs not consumed by known path. | About `-5` instr / `-20` bytes per hit block; possible `code_max -20`. | High for `textureMode0=4ec76a07` / `4ec76c07` with nonzero `tmu0_rgb_lod_frac` and `tmu0_alpha_lod_frac`. | Low-medium. Lane numbering and `TCA_MSELECT_AOTHER` guard must be exact. | Good second slice if implementation stays bucket-gated and ARM64-local. |
| 3 | TMU combine | Trilinear reverse index: keep `lod&1` as index and use scaled `LDR q16,[x22,xN,LSL #4]`; remove separate RGB byte-offset construction. | About `-5` instr / `-20` bytes for both-reverse known buckets. | High for same `textureMode0=4ec76a07` / `4ec76c07` buckets. | Medium-low. Macro contract change must be built and validated. | Implement with rank 2 only if code stays simple; otherwise separate. |
| 4 | TMU combine | Reuse TMU0 `STATE_lod_frac[0]` scalar for RGB and alpha after rank 3 frees the scalar register. | `-1` instr / `-4` bytes, stacked after rank 3. | High when `tmu0_rgb_lod_frac` and `tmu0_alpha_lod_frac` are nonzero in same bucket. | Low if coupled to rank 3; medium standalone. | Piggyback only after rank 3. |
| 5 | Alpha blend | Skip `AFUNC_AZERO` destination zero vector and final add for `src_afunc=2 dest_afunc=0`. | `-2` instr / `-8` bytes for known `2/0` pair. | High: normal proof hit `src_afunc=2 dest_afunc=0`. | Low. Interpreter has `newdest_* = 0`; adding zero is dead. | Good small alpha slice after texture/TMU. |
| 6 | Texture fetch / perspective | Remove perspective LOD shift temp: use `w11` directly for `LSR x4,x4,x11`, then compute `w11 = bsr - 19`. | `-1` instr per perspective texture fetch site. | Medium-high: normal buckets are perspective textured. | Medium-low. LOD/state-sensitive path. | Defer until rank 1 lands cleanly. |
| 7 | Alpha blend | Gate src/dst alpha preparation and alpha-out scalar work by actual consumers; direct alpha-out forms for one/both/neither `AONE`. | Common pairs save at least `-2..-3` instr; aux-alpha no-write can save more. | Covered by consolidated alpha probe after Rank 7d source-alpha audit. | Low after sub-slice validation. | Closed. |
| 8 | Alpha blend | Specialize `src_afunc=4 dest_afunc=4`: packed saturating byte add instead of unpack, 16-bit add, saturating pack. | `-3` instr / `-12` bytes on known `4/4`. | High for `src_afunc=4 dest_afunc=4`. | Medium-low. Alpha byte becomes packed saturated value; scalar `w12` must remain truth for alpha write. | Candidate after rank 7 audit, or as its own small slice. |
| 9 | Fog | Replace fog mask/shift sequences with `UBFX`; W-fog byte can use `LDRB` from `STATE_w+4`. | Table fog `-3` instr; Z/W fog `-1` instr. | Medium: need existing fog bucket coverage in current workload. | Low. Matches interpreter masks. | Only if normal logs prove hot fog coverage. |
| 10 | Prologue/register pressure | Free `x24` from real_y pin, keep real_y in `x3`, then use `x24` as final dither base candidate for remaining shape63 fallback. | Dynamic win may be meaningful: shape63 true fallback can become `MOV x7,x24` instead of per-pixel address materialization; static `code_bytes` may be neutral or slightly up. | Medium: old true fallback was hot; current shape63 needs confirmation. | Medium. Must audit every `x3` emitter and future scratch assumptions. | Perf-motivated, not code-size-first. Do only after perf plan or if dither fallback dominates. |
| 11 | Prologue/fog-alpha | Gate `v13 = color-before-fog` and save only `d12` when `dest_afunc != AFUNC_ACOLORBEFOREFOG`. | `-1` loop instr and less save/restore memory for most non-ACOLORBEFOREFOG alpha/fog blocks. | Proven with consolidated probe, including `dest_afunc=15 fog_en=1`. | Low. Interpreter uses `colbfog_*` only in `dest_afunc == AFUNC_ACOLORBEFOREFOG`; no `src_afunc` consumes color-before-fog. | Closed. |
| 12 | Alpha test | Compare against immediate alpha reference instead of `LDRB` from `params->alphaMode+3`. | `-1` instr per active alpha test block. | Medium: needs active alpha-test buckets. | Low. `alphaMode` is codegen key. | Tiny; batch with other alpha work only. |

## Reject / Do Not Do

- No no-perspective-only work unless normal coverage proves the path. B10 already
  failed to get useful coverage.
- No helper-backed dynarec path.
- No x86 or x86-64 codegen edits.
- No `SDIV` -> `UDIV` retry in texture perspective path.
- No redo of B11 bilinear weight loads.
- No redo of B9 rare same-factor alpha table-pair work.
- No broad frame shrink or slot repack; risk is not worth current wins.
- No dropping `MOV x29, SP`; debug/unwind loss is not worth `-4` bytes.
- No more dither candidate register shuffling without freeing a real register.
- No TMU combine rewrites copied from x86-64.
- No `ASR` -> `LSR` changes in TMU combine math.
- No claim from clean counters alone; target coverage and `code_bytes` /
  `code_max` must support the slice.

## Perf Measurement Plan

`code_bytes` and `code_max` remain acceptance guards, not final perf proof.
For human-time decisions, use a small A/B perf harness:

1. Build/sign each rev:

   ```sh
   ./scripts/build-and-sign.sh
   ```

2. Correctness gate before perf:

   ```sh
   ./scripts/launch-voodoo-validate-vm.sh --mode verify --limit 51200000 --metrics 1
   ```

   Accept only:

   ```text
   mismatch_spans=0
   fb_mismatches=0
   aux_mismatches=0
   state_mismatches=0
   rejects=0
   ```

3. Perf run:

   ```sh
   ./scripts/launch-voodoo-validate-vm.sh --mode off --metrics 1
   sample <pid> 90 10 -file <sample-out>
   ```

4. Workload:

   - Primary: same Quake 3 timedemo loop, fixed cfg, fixed resolution, fixed
     audio/state.
   - Secondary: 3DMark99-style loop with fixed scene order.
   - Start sampling only after warm scene is active.
   - Close the VM after each run so JIT metrics flush.

5. Compare:

   - 1 warmup per rev, discarded.
   - 7 measured runs baseline and 7 measured runs candidate.
   - Interleave baseline/current runs.
   - Compare median, min, max.
   - Require more than 2-3% median gain before calling a real perf win.

6. Track:

   - Guest FPS, score, or wall time.
   - `sample` hot functions, especially generated span execution and Voodoo
     texture/fog/blend paths.
   - JIT metrics: `spans`, `jit`, `interp`, `compiles`, `misses`, `mru_hits`,
     `scan_hits`, `rejects`, `code_bytes`, `code_max`.
   - Normalized metrics: `code_bytes/compiles`, hit rate, `compiles/spans`,
     target pixel share.
   - Coverage counters: `tmu0_bilinear_pixels`, `tmu1_bilinear_pixels`,
     `tmu0_rgb_lod_frac`, `tmu0_alpha_lod_frac`, alpha mode buckets.

Micro-peeps are probably measurable only stacked. B11-B13 cut short-run
`code_bytes` by 1904 and `code_max` by 88 with hot coverage, so the stack may
show a small gain; a single `-1` instruction slice is likely below noise.

## Q3 Demo Four Host-Efficiency Baseline

Baseline date: 2026-06-01
Source head: `7d49f1fa1`
Workload: user-driven Quake 3 `demo four`
Mode: validation off, ARM64 JIT metrics on.

Build/sign:

```text
./scripts/build-and-sign.sh
ninja: no work to do.
build/src/86Box.app: replacing existing signature
BUILD + SIGN OK
```

Launch:

```sh
./scripts/launch-voodoo-validate-vm.sh --mode off --metrics 1
```

Sampling:

```sh
sample 96977 90 10 -file /tmp/86box-q3-demo-four-baseline-7d49f1fa1.sample.txt
```

The VM exited after the guest run and flushed JIT metrics.

JIT metrics:

```text
mru_hits=3395237
scan_hits=3427051
misses=20385
compiles=20385
rejects=0
code_bytes=25123524
code_max=1672
```

Top host sample symbols from the run:

| Symbol | Samples |
| ------ | ------- |
| `exec386_dynarec` | 1619 |
| `voodoo_half_triangle` | 306 |
| `voodoo_use_texture` | 268 |
| `voodoo_fifo_thread` | 254 |
| `voodoo_reg_writel` | 65 |
| `voodoo_fastfill` | 35 |
| `voodoo_queue_triangle` | 13 |
| `voodoo_triangle_setup` | 11 |

Notes:

- Guest FPS was intentionally not used as the primary metric.
- This baseline is for host-efficiency comparison after a stacked optimization
  batch, not for deciding a single micro-peep.
- The metrics log showed N5 path counters as zero in this validation-off run,
  so this baseline uses host sample profile plus JIT cache/code metrics.
- Compare future stacked run against the same workload window and sampling
  duration. Useful deltas: total Voodoo sample share, `voodoo_use_texture`,
  `voodoo_half_triangle`, JIT cache metrics, `code_bytes`, and `code_max`.

## Recommendation

Implement rank 1 next, alone:

- Allowed file: `src/include/86box/vid_voodoo_codegen_arm64.h`.
- No `src/video/vid_voodoo_render.c`.
- No x86 or x86-64 codegen files.
- No helper-backed dynarec.

Validation after source edit:

```sh
./scripts/build-and-sign.sh
./scripts/launch-voodoo-validate-vm.sh --limit 10240000 --log-limit 8 --metrics 1
```

VM workflow: launch, then stop polling until the guest run is reported done.

Acceptance:

- `mismatch_spans=0`
- `fb_mismatches=0`
- `aux_mismatches=0`
- `state_mismatches=0`
- `rejects=0`
- nonzero `tmu0_bilinear_pixels`
- nonzero `tmu1_bilinear_pixels`
- `code_bytes < 40884`
- `code_max <= 1780`

Stop rules:

- Stop if `code_bytes` does not improve.
- Stop if `code_max` regresses.
- Stop if target bilinear coverage is absent.
- Stop on any framebuffer, aux, state, or reject mismatch.
- Ignore `[0147:0000B9BD] Illegal instruction 00008B55 (FF)` only by itself.

## Rank 1 Validation

Status: accepted by short VM validation.

Change:

- `ARM64_EMIT_TEX_BILINEAR_SHIFT_SETUP` now reuses `tex_shift_reg` while it is
  still `8` to compute `texel_bias_reg = 8 << tex_lod`.
- Old sequence:
  `MOVZ tex_shift,#8`; load `tex_lod`; `MOVZ texel_bias,#8`;
  `SUB tex_shift,tex_shift,tex_lod`; `LSL texel_bias,texel_bias,tex_lod`.
- New sequence:
  `MOVZ tex_shift,#8`; load `tex_lod`;
  `LSL texel_bias,tex_shift,tex_lod`;
  `SUB tex_shift,tex_shift,tex_lod`.

Build/sign:

```text
./scripts/build-and-sign.sh
ninja: no work to do.
build/src/86Box.app: replacing existing signature
BUILD + SIGN OK
```

Validation launch:

```sh
./scripts/launch-voodoo-validate-vm.sh --limit 10240000 --log-limit 8 --metrics 1
```

Guest workload: Quake 3 `demo four`, stopped after verify limit was reached.

Validation result:

```text
verify=10240000
skipped=0
mismatch_spans=0
fb_mismatches=0
aux_mismatches=0
state_mismatches=0
rejects=0
```

Target coverage:

```text
tmu0_bilinear_pixels=86364606
tmu1_bilinear_pixels=240389455
```

JIT metrics:

```text
mru_hits=404249
scan_hits=374559
misses=2504
compiles=2504
rejects=0
code_bytes=3093612
code_max=1668
```

Known guest noise appeared and was ignored by itself:

```text
[0147:0000B9BD] Illegal instruction 00008B55 (FF)
```

Next: evaluate TMU ranks 2-4 as the next stacked slice before the next host
sample.

## TMU Ranks 2-4 Validation

Status: accepted by targeted max-window validation.

Implemented in `src/include/86box/vid_voodoo_codegen_arm64.h`:

- Rank 2: TMU0 alpha lane extraction and dead pack removal for the guarded
  LOD-frac bucket.
- Rank 3: shared trilinear reverse index for RGB and alpha when both reverse
  flags are true; RGB mask load uses scaled index.
- Rank 4: TMU0 `STATE_lod_frac[0]` scalar is loaded once and reused for RGB
  factor duplication and alpha factor.

Guard:

```text
textureMode0 == 4ec76a07 || textureMode0 == 4ec76c07
TC_MSELECT_LOD_FRAC
TCA_MSELECT_LOD_FRAC
trilinear
reverse blend enabled for RGB and alpha
add clocal enabled for RGB and alpha
no invert output
```

Build/sign:

```text
./scripts/build-and-sign.sh
ninja: no work to do.
build/src/86Box.app: replacing existing signature
BUILD + SIGN OK
```

Validation launch:

```sh
./scripts/launch-voodoo-validate-vm.sh --limit 409600000 --log-limit 8 --metrics 1
```

Validation result:

```text
verify=16307324
skipped=0
mismatch_spans=0
fb_mismatches=0
aux_mismatches=0
state_mismatches=0
rejects=0
```

Target coverage:

```text
textureMode0=4ec76a07
tmu0_rgb_lod_frac=4398704
tmu0_alpha_lod_frac=4398704

textureMode0=4ec76a07
tmu0_rgb_lod_frac=2068283
tmu0_alpha_lod_frac=2068283

textureMode0=4ec76a07
tmu0_rgb_lod_frac=1235505
tmu0_alpha_lod_frac=1235505

textureMode0=4ec76c07
tmu0_rgb_lod_frac=400438
tmu0_alpha_lod_frac=400438
```

Additional coverage:

```text
tmu0_bilinear_pixels=424396982
tmu1_bilinear_pixels=419513097
```

JIT metrics:

```text
mru_hits=577429
scan_hits=844145
misses=36
compiles=36
rejects=0
code_bytes=48708
code_max=1776
```

Known guest noise appeared and was ignored by itself:

```text
[0147:0000B9BD] Illegal instruction 00008B55 (FF)
```

Next: run the stacked host-efficiency sample against the same Q3 `demo four`
sampling process used for the baseline.

## Q3 Demo Four Stacked Host-Efficiency Sample

Stack sampled: rank 1 + TMU ranks 2-4.
Mode: validation off, ARM64 JIT metrics on.

Launch:

```sh
./scripts/launch-voodoo-validate-vm.sh --mode off --metrics 1
```

Sampling:

```sh
sample 8231 90 10 -file /tmp/86box-q3-demo-four-stacked-r1-tmu234.sample.txt
```

JIT metrics:

```text
mru_hits=3004072
scan_hits=3262438
misses=20527
compiles=20527
rejects=0
code_bytes=25285828
code_max=1668
```

Top host sample symbols from the stacked run. `exec386_dynarec` is listed only
as workload/context noise; it is CPU dynarec time and is not part of the Voodoo
host-efficiency comparison for this slice.

| Symbol | Baseline samples | Stacked samples | Delta |
| ------ | ---------------- | --------------- | ----- |
| `exec386_dynarec` | 1619 | 1726 | +107, excluded |
| `voodoo_half_triangle` | 306 | 281 | -25 |
| `voodoo_use_texture` | 268 | 289 | +21 |
| `voodoo_fifo_thread` | 254 | 228 | -26 |
| `voodoo_reg_writel` | 65 | 57 | -8 |
| `voodoo_fastfill` | 35 | 26 | -9 |
| `voodoo_queue_triangle` | 13 | 11 | -2 |
| `voodoo_triangle_setup` | 11 | 12 | +1 |

Voodoo-only listed-symbol sum:

```text
baseline=952
stacked=904
delta=-48 samples
```

JIT metric comparison:

| Metric | Baseline | Stacked | Delta |
| ------ | -------- | ------- | ----- |
| `mru_hits` | 3395237 | 3004072 | -391165 |
| `scan_hits` | 3427051 | 3262438 | -164613 |
| `misses` | 20385 | 20527 | +142 |
| `compiles` | 20385 | 20527 | +142 |
| `rejects` | 0 | 0 | 0 |
| `code_bytes` | 25123524 | 25285828 | +162304 |
| `code_max` | 1672 | 1668 | -4 |

Notes:

- Guest FPS was intentionally not used.
- Validation-off N5 counters were zero in both baseline and stacked runs, so
  this comparison uses host sample symbols plus JIT cache/code metrics only.
- The host sample is noisy for a single 90-second run. It suggests a small
  Voodoo-side reduction in the listed-symbol sum, but does not prove a final
  host-efficiency win from the stack.
- `voodoo_half_triangle`, `voodoo_fifo_thread`, `voodoo_reg_writel`, and
  `voodoo_fastfill` samples decreased; `voodoo_use_texture` increased.
- `exec386_dynarec` increased, but that is CPU dynarec time and not evidence
  against this Voodoo generated-code slice.
- `code_max` improved slightly; `code_bytes` increased in this validation-off
  run because the compile mix differed (`compiles` also increased).
- Treat this as a baseline-vs-stacked smoke comparison, not a final perf claim.

## Ranks 5, 6, and 8 Validation

Status: accepted by max-window VM validation.

Implemented in `src/include/86box/vid_voodoo_codegen_arm64.h`:

- Rank 5: `AFUNC_AZERO` destination color is not zeroed and the final
  destination add is skipped when `dest_afunc == AFUNC_AZERO`.
- Rank 6: perspective texture LOD shift uses the BSR value in `w11` directly,
  then computes `w11 = bsr - 19`.
- Rank 8: `src_afunc=4 dest_afunc=4` uses packed unsigned saturating byte add
  and skips the 16-bit unpack/add/pack sequence; scalar `w12` remains the alpha
  output source of truth.

Build/sign:

```text
./scripts/build-and-sign.sh
ninja: no work to do.
build/src/86Box.app: replacing existing signature
BUILD + SIGN OK
```

Validation launch:

```sh
./scripts/launch-voodoo-validate-vm.sh --limit 51200000 --log-limit 8 --metrics 1
```

Validation result:

```text
verify=51200000
skipped=0
mismatch_spans=0
fb_mismatches=0
aux_mismatches=0
state_mismatches=0
rejects=0
```

Target coverage:

```text
src_afunc=2 dest_afunc=0
spans=1261978
textureMode0=4ec76a07
tmu0_rgb_lod_frac=1261978
tmu0_alpha_lod_frac=1261978

src_afunc=2 dest_afunc=0
spans=4748726
textureMode0=4ec76a07
tmu0_rgb_lod_frac=4748726
tmu0_alpha_lod_frac=4748726

src_afunc=4 dest_afunc=4
spans=3942912
textureMode0=4ec76a07
tmu0_rgb_lod_frac=3942912
tmu0_alpha_lod_frac=3942912

src_afunc=4 dest_afunc=4
spans=104681
textureMode0=00000a07

src_afunc=4 dest_afunc=4
spans=24804
textureMode0=00000a07
```

Additional coverage:

```text
tmu0_bilinear_pixels=547014735
tmu1_bilinear_pixels=853034118
```

JIT metrics:

```text
mru_hits=1530176
scan_hits=2050174
misses=5458
compiles=5458
rejects=0
code_bytes=6668644
code_max=1716
```

Known guest noise appeared and was ignored by itself:

```text
[0147:0000B9BD] Illegal instruction 00008B55 (FF)
```

Notes:

- This run proves clean semantics and target coverage for the batched small
  slice.
- There is no same-window prepatch baseline for this 51.2M validation run, so
  the JIT metrics above are validation evidence, not a standalone perf claim.
- Static generated-code effects are `-8` bytes for covered rank 5 blocks, `-4`
  bytes per perspective texture fetch site for rank 6, and `-12` bytes for
  covered rank 8 blocks.

Next: run a same-workload host-efficiency sample only after deciding whether to
keep this three-rank batch together or split it for stricter metric attribution.

## Q3 Demo Four Ranks 5, 6, and 8 Host-Efficiency Sample

Stack sampled: rank 1 + TMU ranks 2-4 + ranks 5, 6, and 8.
Mode: validation off, ARM64 JIT metrics on.

Launch:

```sh
./scripts/launch-voodoo-validate-vm.sh --mode off --metrics 1
```

Sampling:

```sh
sample 18649 90 10 -file /tmp/86box-q3-demo-four-r5-r6-r8-retry.sample.txt
```

JIT metrics:

```text
mru_hits=3145415
scan_hits=3280433
misses=20666
compiles=20666
rejects=0
code_bytes=25277888
code_max=1652
```

Top host sample symbols from the run. `exec386_dynarec` is listed only as
workload/context noise; it is CPU dynarec time and is not part of the Voodoo
host-efficiency comparison for this slice.

| Symbol | Baseline samples | R1+TMU234 samples | R5/R6/R8 samples |
| ------ | ---------------- | ----------------- | ---------------- |
| `exec386_dynarec` | 1619 | 1726 | 1691, excluded |
| `voodoo_half_triangle` | 306 | 281 | 280 |
| `voodoo_use_texture` | 268 | 289 | 247 |
| `voodoo_fifo_thread` | 254 | 228 | 211 |
| `voodoo_reg_writel` | 65 | 57 | 46 |
| `voodoo_fastfill` | 35 | 26 | 29 |
| `voodoo_queue_triangle` | 13 | 11 | 22 |
| `voodoo_triangle_setup` | 11 | 12 | 15 |

Voodoo-only listed-symbol sum:

```text
baseline=952
r1_tmu234=904
r5_r6_r8=850
delta_vs_baseline=-102 samples
delta_vs_r1_tmu234=-54 samples
```

JIT metric comparison:

| Metric | Baseline | R1+TMU234 | R5/R6/R8 | Delta vs baseline | Delta vs R1+TMU234 |
| ------ | -------- | --------- | -------- | ----------------- | ------------------ |
| `mru_hits` | 3395237 | 3004072 | 3145415 | -249822 | +141343 |
| `scan_hits` | 3427051 | 3262438 | 3280433 | -146618 | +17995 |
| `misses` | 20385 | 20527 | 20666 | +281 | +139 |
| `compiles` | 20385 | 20527 | 20666 | +281 | +139 |
| `rejects` | 0 | 0 | 0 | 0 | 0 |
| `code_bytes` | 25123524 | 25285828 | 25277888 | +154364 | -7940 |
| `code_max` | 1672 | 1668 | 1652 | -20 | -16 |

Notes:

- This is one host sample, so it is directional, not final perf proof.
- Validation-off N5 counters were zero again, so coverage interpretation uses
  the earlier max-window validation run plus host sample/JIT cache metrics here.
- Listed Voodoo sample share improved versus both the original baseline and the
  rank 1 + TMU ranks 2-4 stack.
- `code_max` improved versus both comparison points; `code_bytes` improved
  versus the previous stack but remains higher than original baseline because
  the compile mix differs.

## Rank 9 and Rank 12 Validation

Status: accepted as a clean, coverage-backed micro-peep. Aggregate JIT byte
metrics from this user-driven validation window are not treated as directly
comparable because the exact scene/compile mix was not identical.

Implemented in `src/include/86box/vid_voodoo_codegen_arm64.h`:

- Rank 9: fog table index/fraction extraction uses `UBFX` instead of
  `LSR` + `AND`.
- Rank 9: W-fog byte extraction uses `LDRB` from `STATE_w + 4` instead of
  `LDR_W` plus `AND 0xff`.
- Rank 12: alpha-test compares against the constant alpha reference byte from
  `params->alphaMode` with `CMP_IMM` instead of loading the ref byte and using
  `CMP_REG`.

Build/sign:

```text
./scripts/build-and-sign.sh
ninja: no work to do.
build/src/86Box.app: replacing existing signature
BUILD + SIGN OK
```

Validation launch:

```sh
./scripts/launch-voodoo-validate-vm.sh --limit 51200000 --log-limit 8 --metrics 1
```

Validation result:

```text
verify=51200000
skipped=0
mismatch_spans=0
fb_mismatches=0
aux_mismatches=0
state_mismatches=0
rejects=0
```

Target coverage:

```text
fogMode=00000059
fog_en=1
fog_src=18
spans=4463449
textureMode0=4ec76a07
tmu0_rgb_lod_frac=4463449
tmu0_alpha_lod_frac=4463449

alphaMode=00005119
alpha_test=1
alpha_func=4
spans=117066
textureMode0=00000016
```

JIT metrics:

```text
mru_hits=1605775
scan_hits=2083969
misses=5461
compiles=5461
rejects=0
code_bytes=6686820
code_max=1720
```

Known guest noise appeared and was ignored by itself:

```text
[0147:0000B9BD] Illegal instruction 00008B55 (FF)
```

Notes:

- Static emitted-code effects are `-2` instructions for covered fog-table
  blocks and `-1` instruction for covered alpha-test blocks.
- W-fog was not separately proven hot in the top printed mode buckets, but the
  byte load matches the interpreter expression `(w >> 32) & 0xff`.
- The aggregate metrics are recorded for audit only. They should not be used as
  a rejection signal for this slice without same-block attribution because this
  51.2M-span guest-driven window did not prove an identical compile mix.

## Rank 9b FOG_Z UBFX

Status: accepted and closed. Rank 9 is closed.

Audit result:

- Prior Rank 9 work already replaced fog-table index/fraction masks with
  `UBFX` and W-fog byte extraction with `LDRB`.
- Remaining safe mask/shift cleanup is only `FOG_Z`.
- Interpreter semantics are `fog_a = (z >> 20) & 0xff`.
- `FOG_ALPHA` remains signed shift plus clamp, so there is no applicable
  `UBFX` cleanup there; the existing code is preserved unchanged.
- Fog-table post-multiply `LSR #10` remains live and matches
  `(dfog * frac) >> 10`.

Implemented in `src/include/86box/vid_voodoo_codegen_arm64.h`:

- In `case FOG_Z`, replaced `LSR #20` plus `AND 0xff` with
  `UBFX w4, w4, #20, #8`.

Validation:

```text
./scripts/build-and-sign.sh
build/src/86Box.app: replacing existing signature
BUILD + SIGN OK
```

```text
Voodoo validate (type=4 verify=1): spans=8311987 jit=8311987 interp=0 verify=8311987 skipped=0 mismatch_spans=0 fb_mismatches=0 fb_within_tol=0 fb_over_tol=0 fb_zero_nonzero=0 fb_tol=5 fb_max_d565=(0,0,0) aux_mismatches=0 state_mismatches=0
Voodoo ARM64 JIT metrics (type=4): mru_hits=84348 scan_hits=124404 misses=22 compiles=22 rejects=0 code_bytes=25880 code_max=1764
```

Target coverage:

```text
fogMode=000000d1
fog_en=1
fog_src=10
spans=383280
fb=0
aux=0
state=0
```

Expected static effect:

```text
-1 instruction per covered FOG_Z block
```

Closure evidence:

- Fog-table index/fraction: implemented and validated in the Rank 9/12 slice.
- W-fog byte extraction: implemented in the Rank 9/12 slice; no broader mask
  cleanup remains.
- FOG_Z: implemented and validated here with `fog_src=10`.
- FOG_ALPHA: audited and preserved unchanged because interpreter semantics
  require signed shift plus clamp, not unsigned extraction.

Known guest noise appeared and was ignored by itself:

```text
[0147:0000B9BD] Illegal instruction 00008B55 (FF)
```

## Rank 7 Alpha-Out Guard and Consolidated Probe

Status: accepted as a Rank 7 partial slice. This covers only the alpha-out
no-write guard, not the full Rank 7 candidate family.

Implemented in `src/include/86box/vid_voodoo_codegen_arm64.h`:

- Skip the alpha-out scalar blend block when the blended alpha value cannot be
  consumed by an alpha-buffer write.
- Guard:

```c
((params->fbzMode & (FBZ_DEPTH_WMASK | FBZ_ALPHA_ENABLE)) ==
 (FBZ_DEPTH_WMASK | FBZ_ALPHA_ENABLE))
```

Implemented in `tools/voodoo_alpha_probe/`:

- Consolidated the alpha blend coverage probe into one repo-owned guest tool.
- Preserved alpha blend factor pairs `1/1`, `3/3`, `5/5`, and `7/7`.
- Added aux-alpha write attempts and confirmed the `NODEPTH_COLOR_ALPHA` route
  produces `FBZ_ALPHA_ENABLE` coverage.
- Added TMU detail and LOD-frac coverage cases so prior one-off probe intent is
  covered from the same tool.
- `build-voodoo-alpha-probe.sh` now builds both `ALPHAPRB.EXE` and a local
  `alphaprb.iso`.

Build/sign:

```text
./scripts/build-and-sign.sh
build/src/86Box.app: replacing existing signature
BUILD + SIGN OK
```

First validation launch:

```sh
./scripts/launch-voodoo-validate-vm.sh --limit 51200000 --log-limit 8 --metrics 1
```

First validation result:

```text
verify=51200000
skipped=0
mismatch_spans=0
fb_mismatches=0
aux_mismatches=0
state_mismatches=0
rejects=0
```

Natural workload coverage for the false guard path:

```text
alpha_blend=1
depth_w=0
alpha_en=0
src_afunc=4 dest_afunc=4
spans=3908513

alpha_blend=1
depth_w=0
alpha_en=0
src_afunc=2 dest_afunc=0
spans=1248739
```

Consolidated probe validation launch:

```sh
./scripts/launch-voodoo-validate-vm.sh --limit 51200000 --log-limit 64 --metrics 1
```

Consolidated probe validation result:

```text
verify=6778867
skipped=0
mismatch_spans=0
fb_mismatches=0
aux_mismatches=0
state_mismatches=0
rejects=0
```

Consolidated probe true/false guard coverage:

```text
alpha_blend=1
depth_w=0
alpha_en=0
src_afunc=1 dest_afunc=1
spans=383280

alpha_blend=1
depth_w=1
alpha_en=1
src_afunc=1 dest_afunc=1
spans=383280

alpha_blend=1
depth_w=1
alpha_en=1
src_afunc=5 dest_afunc=5
spans=383280
```

Additional consolidated probe coverage:

```text
tmu0_rgb_detail=383280
tmu0_alpha_detail=383280
tmu1_rgb_detail=383280
tmu1_alpha_detail=383280

tmu0_rgb_lod_frac=383280
tmu0_alpha_lod_frac=383280
tmu1_rgb_lod_frac=383280
tmu1_alpha_lod_frac=383280
```

Known guest noise appeared and was ignored by itself:

```text
[0147:0000B9BD] Illegal instruction 00008B55 (FF)
```

Notes:

- The Rank 7 alpha-out no-write guard is proven on both sides: skipped when
  `alpha_en=0`, preserved when `depth_w=1 alpha_en=1`.
- This does not close all Rank 7 opportunities. Remaining Rank 7 work still
  needs explicit sub-slice names and coverage requirements.

## Rank 7b Destination Alpha Prep Guard

Status: accepted as a Rank 7 partial slice. This covers only the destination
alpha load/double guard, not the full Rank 7 candidate family.

Audit result:

- `w5` destination alpha is needed by RGB blend only when `dest_afunc` or
  `src_afunc` consumes destination alpha:
  - `AFUNC_ADST_ALPHA`
  - `AFUNC_AOMDST_ALPHA`
  - `AFUNC_ASATURATE` on the source side
- `w5` destination alpha is needed by alpha-out only when the alpha buffer is
  writable and `dest_aafunc == AFUNC_AONE`.
- `FBZ_ALPHA_ENABLE` alone is not enough reason to load old alpha; if neither
  RGB blend nor alpha-out consumes it, the load and double are dead.

Implemented in `src/include/86box/vid_voodoo_codegen_arm64.h`:

- Added `need_dst_alpha` inside the ARM64 alpha-blend block.
- Guarded the aux/default destination-alpha setup.
- Guarded `w5 = dst_alpha * 2`.
- Kept `w12 = src_alpha * 2` unconditional.

Implemented in `tools/voodoo_alpha_probe/`:

- Added a `NODEPTH_COLOR_ALPHA` alpha-out case with `src_aafunc=0` and
  `dest_aafunc=4`.
- Rebuilt `alphaprb.iso` and copied the updated image to the Desktop for the
  guest run.

Validation:

```text
Voodoo validate (type=4 verify=1): spans=7162147 jit=7162147 interp=0 verify=7162147 skipped=0 mismatch_spans=0 fb_mismatches=0 fb_within_tol=0 fb_over_tol=0 fb_zero_nonzero=0 fb_tol=5 fb_max_d565=(0,0,0) aux_mismatches=0 state_mismatches=0
Voodoo ARM64 JIT metrics (type=4): mru_hits=81471 scan_hits=124404 misses=19 compiles=19 rejects=0 code_bytes=22652 code_max=1768
```

Rank 7b target coverage:

```text
alpha_blend=1
depth_w=1
alpha_en=1
alphaMode=00401110
src_afunc=1
dest_afunc=1
spans=383280
fb=0
aux=0
state=0
```

Existing RGB destination-alpha consumer coverage remained clean:

```text
src_afunc=3 dest_afunc=3 spans=383280 fb=0 aux=0 state=0
src_afunc=7 dest_afunc=7 spans=383280 fb=0 aux=0 state=0
```

Known guest noise appeared and was ignored by itself:

```text
[0147:0000B9BD] Illegal instruction 00008B55 (FF)
```

Notes:

- Rank 7 remains partial; Rank 7c is still open.

## Rank 7c Alpha-Out Direct Forms Blocker Fix

Status: accepted as a correctness fix that unblocks later Rank 7c optimization
work. No Rank 7c direct-form optimization is accepted yet.

Audit result:

- Interpreter alpha-out semantics are:
  `src_a = (((dest_aafunc == 4) ? dest_a * 256 : 0) + ((src_aafunc == 4) ? src_a * 256 : 0)) >> 8`.
- Direct forms appeared valid for three expanded probe cases:
  - neither alpha-out factor consumes `AONE`: `alphaMode=00001110`
  - only destination alpha-out consumes `AONE`: `alphaMode=00401110`
  - only source alpha-out consumes `AONE`: `alphaMode=00041110`
- The both-`AONE` case is not currently safe:
  `alphaMode=00441110` produces aux-buffer mismatches with the existing
  pre-Rank-7c ARM64 codegen.
- Root cause: the interpreter stores `dest_a` as `uint8_t`, but ARM64 loaded
  the alpha-buffer value with `LDRH` and kept the full 16-bit aux value for
  destination-alpha consumers.

Implemented in `src/include/86box/vid_voodoo_codegen_arm64.h`:

- Added `UXTB w5, w5` after the aux-buffer destination-alpha `LDRH`.
- Left depth reads/writes unchanged.

Implemented in `src/include/86box/vid_voodoo_common.h`:

- Raised `VOODOO_VALIDATE_MODE_BUCKETS` from 16 to 24 so the expanded alpha
  probe prints the fourth alpha-out bucket explicitly.

Expanded probe source coverage in `tools/voodoo_alpha_probe/`:

```text
alphaMode=00001110
alphaMode=00401110
alphaMode=00041110
alphaMode=00441110
```

All four cases use:

```text
alpha_blend=1
depth_w=1
alpha_en=1
```

Isolation validation after reverting the Rank 7c codegen attempt back to the
pre-Rank-7c alpha-out block:

```text
Voodoo validate (type=4 verify=1): spans=7545427 jit=7545427 interp=0 verify=7545427 skipped=0 mismatch_spans=309360 fb_mismatches=0 fb_within_tol=0 fb_over_tol=0 fb_zero_nonzero=0 fb_tol=5 fb_max_d565=(0,0,0) aux_mismatches=54549040 state_mismatches=0
Voodoo ARM64 JIT metrics (type=4): mru_hits=82430 scan_hits=124404 misses=20 compiles=20 rejects=0 code_bytes=23780 code_max=1768
```

Failing mode:

```text
alphaMode=00441110
fbzMode=00044fe1
alpha_blend=1
depth_w=1
alpha_en=1
fb=0
state=0
aux_mismatches=54549040
```

Validation after the fix:

```text
Voodoo validate (type=4 verify=1): spans=7545427 jit=7545427 interp=0 verify=7545427 skipped=0 mismatch_spans=0 fb_mismatches=0 fb_within_tol=0 fb_over_tol=0 fb_zero_nonzero=0 fb_tol=5 fb_max_d565=(0,0,0) aux_mismatches=0 state_mismatches=0
Voodoo ARM64 JIT metrics (type=4): mru_hits=82430 scan_hits=124404 misses=20 compiles=20 rejects=0 code_bytes=23788 code_max=1768
```

Expanded Rank 7c target coverage:

```text
alphaMode=00001110 spans=383280 fb=0 aux=0 state=0
alphaMode=00401110 spans=383280 fb=0 aux=0 state=0
alphaMode=00041110 spans=383280 fb=0 aux=0 state=0
alphaMode=00441110 spans=383280 fb=0 aux=0 state=0
```

Known guest noise appeared and was ignored by itself:

```text
[0147:0000B9BD] Illegal instruction 00008B55 (FF)
```

Notes:

- Do not call Rank 7c optimization closed. The direct alpha-out forms still
  need a separate implementation and validation slice.
- Rank 7 remains partial.

## Rank 7c Alpha-Out Direct Forms

Status: accepted as a narrow ARM64-local optimization after the aux
destination-alpha `UXTB` blocker fix.

Implemented in `src/include/86box/vid_voodoo_codegen_arm64.h`:

- Replaced the generic alpha-out accumulator sequence for
  `need_blended_alpha_write` with direct forms:
  - neither `src_aafunc` nor `dest_aafunc` is `AFUNC_AONE`: `w12 = 0`
  - destination only: `w12 = dest_a`
  - source only: `w12 = src_a`
  - both: `w12 = src_a + dest_a`
- The implementation uses the existing doubled RGB blend factor registers:
  `w12 = src_a * 2` and, when needed, `w5 = dest_a * 2`, then divides by two.

Interpreter equivalence:

- Matches the interpreter expression:
  `(((dest_aafunc == 4) ? dest_a * 256 : 0) + ((src_aafunc == 4) ? src_a * 256 : 0)) >> 8`.
- The both-`AONE` form intentionally does not clamp after adding two `uint8_t`
  alpha inputs, matching the interpreter.

Validation:

```text
Voodoo validate (type=4 verify=1): spans=7545427 jit=7545427 interp=0 verify=7545427 skipped=0 mismatch_spans=0 fb_mismatches=0 fb_within_tol=0 fb_over_tol=0 fb_zero_nonzero=0 fb_tol=5 fb_max_d565=(0,0,0) aux_mismatches=0 state_mismatches=0
Voodoo ARM64 JIT metrics (type=4): mru_hits=82430 scan_hits=124404 misses=20 compiles=20 rejects=0 code_bytes=23656 code_max=1768
```

Expanded Rank 7c target coverage:

```text
alphaMode=00001110 spans=383280 fb=0 aux=0 state=0
alphaMode=00401110 spans=383280 fb=0 aux=0 state=0
alphaMode=00041110 spans=383280 fb=0 aux=0 state=0
alphaMode=00441110 spans=383280 fb=0 aux=0 state=0
```

Code-size delta from the blocker-fix baseline:

```text
code_bytes 23788 -> 23656 (-132)
code_max   1768  -> 1768  (+0)
```

Known guest noise appeared and was ignored by itself:

```text
[0147:0000B9BD] Illegal instruction 00008B55 (FF)
```

Risks:

- Relies on `w12` and `w5` still being doubled before alpha-out.
- Relies on the prior `UXTB w5, w5` fix for aux destination alpha.
- Any future alpha-blend register reuse must preserve `w12`/`w5` through the
  alpha-out block.

## Rank 7d Source Alpha Prep Guard

Status: accepted as the final Rank 7 sub-slice. Rank 7 is closed.

Audit result:

- Interpreter RGB blend consumes `src_a` only for:
  - destination factor `AFUNC_ASRC_ALPHA`
  - destination factor `AFUNC_AOMSRC_ALPHA`
  - source factor `AFUNC_ASRC_ALPHA`
  - source factor `AFUNC_AOMSRC_ALPHA`
  - source factor `AFUNC_ASATURATE`
- Interpreter alpha-out consumes `src_a` only when alpha-buffer write is live
  and `src_aafunc == AFUNC_AONE`.
- Therefore `w12 = src_alpha * 2` is dead when neither RGB blend nor alpha-out
  consumes source alpha.
- No broader raw/doubled split was taken; source-only alpha-out still uses the
  existing direct form when `src_aafunc == AFUNC_AONE`.

Implemented in `src/include/86box/vid_voodoo_codegen_arm64.h`:

- Added `rgb_blend_needs_src_alpha`.
- Added `alpha_out_needs_src_alpha`.
- Added `need_src_alpha_doubled`.
- Guarded only `w12 = src_alpha * 2`.

Implemented in `tools/voodoo_alpha_probe/`:

- Added `GR_BLEND_SATURATE`.
- Added a `SATURATE/ZERO` probe case to cover the source-side
  `AFUNC_ASATURATE` consumer.

Validation:

```text
./scripts/build-and-sign.sh
BUILD + SIGN OK
```

```text
Voodoo validate (type=4 verify=1): spans=8311987 jit=8311987 interp=0 verify=8311987 skipped=0 mismatch_spans=0 fb_mismatches=0 fb_within_tol=0 fb_over_tol=0 fb_zero_nonzero=0 fb_tol=5 fb_max_d565=(0,0,0) aux_mismatches=0 state_mismatches=0
Voodoo ARM64 JIT metrics (type=4): mru_hits=84348 scan_hits=124404 misses=22 compiles=22 rejects=0 code_bytes=25804 code_max=1764
```

Rank 7d target coverage:

```text
alphaMode=00401110 alpha_blend=1 depth_w=1 alpha_en=1 src_afunc=1 dest_afunc=1 spans=383280 fb=0 aux=0 state=0
alphaMode=00043310 alpha_blend=1 depth_w=0 alpha_en=0 src_afunc=3 dest_afunc=3 spans=383280 fb=0 aux=0 state=0
alphaMode=00047710 alpha_blend=1 depth_w=0 alpha_en=0 src_afunc=7 dest_afunc=7 spans=383280 fb=0 aux=0 state=0
alphaMode=0004ff10 alpha_blend=1 depth_w=0 alpha_en=0 src_afunc=15 dest_afunc=15 spans=383280 fb=0 aux=0 state=0
```

Closure evidence:

- Rank 7a: alpha-out no-write guard, validated with live and dead alpha-buffer
  write paths.
- Rank 7b: destination alpha prep guard, validated with RGB destination-alpha
  consumers and alpha-out destination-alpha consumer.
- Rank 7c: alpha-out direct forms, validated for neither/source/destination/both
  `AONE` forms.
- Rank 7d: source alpha prep guard, validated for no-source-alpha consumer and
  source-alpha RGB consumers including `AFUNC_ASATURATE`.

Known guest noise appeared and was ignored by itself:

```text
[0147:0000B9BD] Illegal instruction 00008B55 (FF)
```

## Rank 11 Color-Before-Fog Gating

Status: accepted and closed.

Audit result:

- Interpreter stores `colbfog_r/g/b` before fog.
- `ALPHA_BLEND` consumes `colbfog_*` only in
  `dest_afunc == AFUNC_ACOLORBEFOREFOG`.
- No `src_afunc` consumes color-before-fog.

Implemented in `src/include/86box/vid_voodoo_codegen_arm64.h`:

- Added `need_v13 = (dest_afunc == AFUNC_ACOLORBEFOREFOG)`.
- Emit `MOV v13, v0` only when `need_v13`.
- Preserve `d13` only when `need_v13`; otherwise save/restore only `d12` in
  the existing stack slot.
- Kept frame size and slot layout unchanged.

Implemented in `tools/voodoo_alpha_probe/`:

- Added a `COLORBEFOREFOG_DEST` case using `GR_BLEND_COLORBEFOREFOG`.
- Added `grFogColorValue` / `grFogMode` dynamic resolves.
- Enabled iterated-alpha fog for that case, then disabled fog after the draw.

Validation:

```text
Voodoo validate (type=4 verify=1): spans=7928707 jit=7928707 interp=0 verify=7928707 skipped=0 mismatch_spans=0 fb_mismatches=0 fb_within_tol=0 fb_over_tol=0 fb_zero_nonzero=0 fb_tol=5 fb_max_d565=(0,0,0) aux_mismatches=0 state_mismatches=0
Voodoo ARM64 JIT metrics (type=4): mru_hits=83389 scan_hits=124404 misses=21 compiles=21 rejects=0 code_bytes=24708 code_max=1764
```

Target coverage:

```text
alphaMode=0004f410
alpha_blend=1
src_afunc=4
dest_afunc=15
fogMode=000000d1
fog_en=1
fog_src=10
spans=383280
fb=0
aux=0
state=0
```

Non-`ACOLORBEFOREFOG` fog and alpha buckets were also clean in the same run,
including `fog_en=1 dest_afunc=5` and the expanded Rank 7c alpha-out buckets.

Code-size delta from the Rank 7c baseline with the expanded probe:

```text
code_bytes 24712 -> 24708 (-4)
code_max   1764  -> 1764  (+0)
```

Known guest noise appeared and was ignored by itself:

```text
[0147:0000B9BD] Illegal instruction 00008B55 (FF)
```
