# ARM64 Voodoo Remaining Optimization Opportunities

Status: 2026-06-01
Source head: `7d49f1fa1`

Scope: report-only discovery. No source edits, no VM launch, no commit.

Semantic source of truth remains the interpreter in
`src/video/vid_voodoo_render.c` and `src/include/86box/vid_voodoo_render.h`.
The x86 and x86-64 code generators are not correctness sources.

Current accepted stack:

- B11: bilinear weights `LDR q16` + `LDR q17` -> `LDP q16, q17`.
- B12: TMU0 LOD-frac alpha subtract uses `w13` directly for
  `textureMode0=4ec76a07` / `4ec76c07`.
- B13: alpha blend copies destination color to `v6` only when
  `src_afunc` needs destination color.

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

## Ranked Opportunities

| Rank | Area | Candidate | Expected win | Coverage confidence | Risk | Recommendation |
| ---- | ---- | --------- | ------------ | ------------------- | ---- | -------------- |
| 1 | Texture fetch / bilinear | `ARM64_EMIT_TEX_BILINEAR_SHIFT_SETUP`: reuse `w7=#8` for texel bias, emit `LSL w10,w7,w16` before `SUB w7,w7,w16`; remove second `MOVZ #8`. | `-1` instr per bilinear fetch site; likely `code_bytes` down about 176-256, possible `code_max -8` when max block has both TMUs. | High: existing `tmu0_bilinear_pixels` and `tmu1_bilinear_pixels` are huge in normal proof. | Low. Same `w10=8<<tex_lod`, same `w7=8-tex_lod`. | Implement first as single narrow slice. |
| 2 | TMU combine | TMU0 alpha lanes + dead packs in known LOD-frac buckets. Replace staged packed-alpha extracts with `UMOV` from halfword lane, and skip dead `SQXTUN` packs not consumed by known path. | About `-5` instr / `-20` bytes per hit block; possible `code_max -20`. | High for `textureMode0=4ec76a07` / `4ec76c07` with nonzero `tmu0_rgb_lod_frac` and `tmu0_alpha_lod_frac`. | Low-medium. Lane numbering and `TCA_MSELECT_AOTHER` guard must be exact. | Good second slice if implementation stays bucket-gated and ARM64-local. |
| 3 | TMU combine | Trilinear reverse index: keep `lod&1` as index and use scaled `LDR q16,[x22,xN,LSL #4]`; remove separate RGB byte-offset construction. | About `-5` instr / `-20` bytes for both-reverse known buckets. | High for same `textureMode0=4ec76a07` / `4ec76c07` buckets. | Medium-low. Macro contract change must be built and validated. | Implement with rank 2 only if code stays simple; otherwise separate. |
| 4 | TMU combine | Reuse TMU0 `STATE_lod_frac[0]` scalar for RGB and alpha after rank 3 frees the scalar register. | `-1` instr / `-4` bytes, stacked after rank 3. | High when `tmu0_rgb_lod_frac` and `tmu0_alpha_lod_frac` are nonzero in same bucket. | Low if coupled to rank 3; medium standalone. | Piggyback only after rank 3. |
| 5 | Alpha blend | Skip `AFUNC_AZERO` destination zero vector and final add for `src_afunc=2 dest_afunc=0`. | `-2` instr / `-8` bytes for known `2/0` pair. | High: normal proof hit `src_afunc=2 dest_afunc=0`. | Low. Interpreter has `newdest_* = 0`; adding zero is dead. | Good small alpha slice after texture/TMU. |
| 6 | Texture fetch / perspective | Remove perspective LOD shift temp: use `w11` directly for `LSR x4,x4,x11`, then compute `w11 = bsr - 19`. | `-1` instr per perspective texture fetch site. | Medium-high: normal buckets are perspective textured. | Medium-low. LOD/state-sensitive path. | Defer until rank 1 lands cleanly. |
| 7 | Alpha blend | Gate src/dst alpha preparation and alpha-out scalar work by actual consumers; direct alpha-out forms for one/both/neither `AONE`. | Common pairs save at least `-2..-3` instr; aux-alpha no-write can save more. | Medium: common alpha pairs covered, but `src_aafunc` / `dest_aafunc` and aux-write coverage must be audited. | Medium. Consumer audit required. | Do not do before simpler alpha/TMU wins. |
| 8 | Alpha blend | Specialize `src_afunc=4 dest_afunc=4`: packed saturating byte add instead of unpack, 16-bit add, saturating pack. | `-3` instr / `-12` bytes on known `4/4`. | High for `src_afunc=4 dest_afunc=4`. | Medium-low. Alpha byte becomes packed saturated value; scalar `w12` must remain truth for alpha write. | Candidate after rank 7 audit, or as its own small slice. |
| 9 | Fog | Replace fog mask/shift sequences with `UBFX`; W-fog byte can use `LDRB` from `STATE_w+4`. | Table fog `-3` instr; Z/W fog `-1` instr. | Medium: need existing fog bucket coverage in current workload. | Low. Matches interpreter masks. | Only if normal logs prove hot fog coverage. |
| 10 | Prologue/register pressure | Free `x24` from real_y pin, keep real_y in `x3`, then use `x24` as final dither base candidate for remaining shape63 fallback. | Dynamic win may be meaningful: shape63 true fallback can become `MOV x7,x24` instead of per-pixel address materialization; static `code_bytes` may be neutral or slightly up. | Medium: old true fallback was hot; current shape63 needs confirmation. | Medium. Must audit every `x3` emitter and future scratch assumptions. | Perf-motivated, not code-size-first. Do only after perf plan or if dither fallback dominates. |
| 11 | Prologue/fog-alpha | Gate `v13 = color-before-fog` and save only `d12` when `dest_afunc != AFUNC_ACOLORBEFOREFOG`. | `-1` loop instr and less save/restore memory for most non-ACOLORBEFOREFOG alpha/fog blocks. | Medium: need alpha/fog bucket coverage and `dest_afunc` visibility. | Low-medium. Must ensure no invalid blend factor path consumes `v13`. | Safe-ish but smaller; defer. |
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
