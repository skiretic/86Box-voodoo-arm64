# ARM64 Voodoo Validation Debug Notes

Status as of 2026-05-30.

This note tracks the current validation/debug slice only. It intentionally does not update the optimization analysis document.

## Scope

- Validate ARM64 Voodoo JIT correctness against the interpreter before optimization work.
- Use same-run JIT-vs-interpreter comparison inside the renderer.
- Focus current mismatch work on framebuffer/color output. Aux/state validation is clean so far.
- Preserve unrelated worktree deletes and unrelated docs/scripts changes.

## Validation Logging Added

Files touched:

- `src/include/86box/vid_voodoo_common.h`
- `src/video/vid_voodoo.c`
- `src/video/vid_voodoo_render.c`

Environment:

- `VOODOO_VALIDATE=verify`
- `VOODOO_VALIDATE_LIMIT=<N>`
- `VOODOO_VALIDATE_LOG_LIMIT=<N>`
- `VOODOO_VALIDATE_MAX_SPAN=<N>` default `2048`
- `VOODOO_VALIDATE_FB_TOL=<N>` default `5`

Runtime behavior:

- Each JIT span can be shadowed by interpreter execution in the same run.
- Before JIT, the validator saves framebuffer/aux span contents and the full `voodoo_state_t`.
- After JIT, it saves JIT framebuffer/aux output and JIT state.
- It restores original framebuffer/aux contents and original state, then runs the interpreter for the same span.
- It compares:
  - RGB565 framebuffer output
  - aux output
  - selected state currently limited to `stipple`
- It counts framebuffer mismatches by:
  - total mismatches
  - within RGB565 tolerance
  - over tolerance
  - zero-vs-nonzero
  - max per-channel RGB565 delta
- It logs the first few mismatch spans, controlled by `VOODOO_VALIDATE_LOG_LIMIT`.
- It aggregates top mode buckets by:
  - `fbzMode`
  - `fbzColorPath`
  - `alphaMode`
  - `fogMode`
  - `textureMode[0]`
  - `textureMode[1]`

Close-time summary prints:

- total spans, JIT spans, interpreter spans
- verified spans and skipped spans
- mismatch span count
- framebuffer mismatch counts
- aux/state mismatch counts
- top mode buckets with decoded key fields

Known ignored guest noise:

- `[0147:0000B9BD] Illegal instruction 00008B55 (FF)`

## Baseline 100x Run

Command shape:

```sh
VOODOO_VALIDATE=verify VOODOO_VALIDATE_LIMIT=10240000 VOODOO_VALIDATE_LOG_LIMIT=8 build/src/86Box.app/Contents/MacOS/86Box --vmpath '/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 Gaming PC'
```

Baseline result before ARM64 fixes:

```text
verify=10240000
mismatch_spans=4433696
fb_mismatches=43583970
fb_within_tol=38212245
fb_over_tol=5371725
fb_zero_nonzero=971134
fb_tol=5
fb_max_d565=(31,63,31)
aux_mismatches=0
state_mismatches=0
```

Dominant bucket was Mode[0]:

```text
spans=2850658
fb=23720030
within_tol=18597102
over_tol=5122928
zero_nonzero=953903
max_d565=(31,63,31)
fbzMode=00010f71
fogMode=00000059
fog_src=18
fbzColorPath=0c002401
textureMode0=4ec76a07
textureMode1=48241a07
aux=0
state=0
```

Interpretation:

- Most diffs are small enough to fit the old `+/-5` tolerance idea.
- Hard over-tolerance and zero-vs-nonzero tail is real.
- Aux/state are clean; bug class is color/framebuffer output, not depth/state.
- Mode[0] dominates hard errors.
- `fog_src=18` is `FOG_W`, but first logged mismatches had `init_w=0`, making fog factor likely tiny/1 there.

## Fix 1: ARM64 FOG_W/Fog Multiply Parity

File:

- `src/include/86box/vid_voodoo_codegen_arm64.h`

Finding:

- ARM64 FOG_W source extraction already matched interpreter: `(w >> 32) & 0xff`.
- x86-64 clamps `w >> 32`, but that is unsafe as a semantic reference.
- Actual ARM64 mismatch was the fog scale math.

Interpreter:

```c
fog_a++;
fog_r = (fog_r * fog_a) >> 8;
```

Old ARM64:

```text
diff >>= 1
diff *= alookup[fog_a + 1]
diff >>= 7
```

Problem:

- `(diff >> 1) * factor >> 7` is not the same as `(diff * factor) >> 8`.
- Odd and negative values differ because arithmetic right shift floors before multiply.

Fix:

- Remove the pre-shift.
- Use `SMULL_4S_4H` for signed 16x16 to 32-bit multiply.
- Shift the 32-bit product by 8.
- Narrow back with `SQXTN_4H_4S`.

100x result after Fix 1:

```text
verify=10240000
mismatch_spans=4406447
fb_mismatches=43573516
fb_within_tol=38186951
fb_over_tol=5386565
fb_zero_nonzero=964583
aux_mismatches=0
state_mismatches=0
```

Mode[0] after Fix 1:

```text
spans=2819623
fb=23623623
within_tol=18485684
over_tol=5137939
zero_nonzero=947553
```

Impact:

- Small improvement, not root cause.
- Confirms fog multiply was a real semantic bug but not the dominant tail.

## Fix 2: ARM64 Perspective Texture Coordinate Rounding

File:

- `src/include/86box/vid_voodoo_codegen_arm64.h`

Finding:

- ARM64 perspective texture fetch skipped interpreter rounding constants.
- This feeds Mode[0] before final color combine and before fog.

Interpreter:

```c
state->tex_s = (int32_t) (((((state->tmu*_s + (1 << 13)) >> 14) * _w) + (1 << 29)) >> 30);
state->tex_t = (int32_t) (((((state->tmu*_t + (1 << 13)) >> 14) * _w) + (1 << 29)) >> 30);
```

Old ARM64:

```text
tmu_s/t >>= 14
tmu_s/t *= reciprocal
tmu_s/t >>= 30
```

Fix:

- Add `(1 << 13)` before the first `>> 14`.
- Add `(1 << 29)` before the final `>> 30`.
- ARM64 backend-local parity fix only.

100x result after Fix 2:

```text
verify=10240000
mismatch_spans=3168322
fb_mismatches=34392668
fb_within_tol=29066968
fb_over_tol=5325700
fb_zero_nonzero=948290
fb_tol=5
fb_max_d565=(31,63,31)
aux_mismatches=0
state_mismatches=0
```

Mode[0] after Fix 2:

```text
spans=2213081
fb=18247113
within_tol=13170722
over_tol=5076391
zero_nonzero=939408
```

Impact:

- Large improvement in mismatch spans and total framebuffer mismatches.
- Hard tail remains mostly in Mode[0].
- Confirms this was a real validation bug, not an optimization artifact.

## Fix 3: ARM64 LOD Fraction Store

File:

- `src/include/86box/vid_voodoo_codegen_arm64.h`

Finding:

- Interpreter stores `state->lod_frac[tmu] = state->lod & 0xff` after LOD clamp and before `state->lod >>= 8`.
- ARM64 stored only integer `state->lod`.
- Mode[0] uses `TC_MSELECT_LOD_FRAC` and `TCA_MSELECT_LOD_FRAC` on TMU0, so stale `lod_frac[0]` was a real semantic mismatch.

Fix:

- Store `STATE_lod_frac_n(tmu)` from the low 8 bits of the clamped LOD before shifting to integer LOD.

100x result after Fix 3:

```text
verify=10240000
mismatch_spans=3256970
fb_mismatches=33482814
fb_within_tol=28065129
fb_over_tol=5417685
fb_zero_nonzero=941194
fb_tol=5
fb_max_d565=(31,63,31)
aux_mismatches=0
state_mismatches=0
```

Mode[0] after Fix 3:

```text
spans=2290717
fb=18025296
within_tol=12855934
over_tol=5169362
zero_nonzero=931639
```

Impact:

- Total framebuffer mismatches improved versus Fix 2.
- Mismatch spans and over-tolerance did not improve; over-tolerance slightly worsened.
- This fix is still semantic parity, but it is not the hard-tail root cause.

## Fix 4: ARM64 Texture Coordinate LOD Scale

File:

- `src/include/86box/vid_voodoo_codegen_arm64.h`

Finding:

- The stage trace showed the first divergence at raw TMU1 fetch.
- Raw TMU0 fetch matched.
- Color combine, fog, and framebuffer output were downstream of the TMU1 difference.
- Interpreter uses `state->tex_lod[tmu][state->lod]` as the texture coordinate scale.
- ARM64 used integer `state->lod` directly as the coordinate scale.
- In the dominant mode, TMU0 happened to have `texlod0 == lod`, but TMU1 had `texlod1 == lod + 1`:
  - `lod=0`: `texlod0=0`, `texlod1=1`
  - `lod=2`: `texlod0=2`, `texlod1=3`

Fix:

- Keep `state->lod` for mip pointer and mask selection.
- Load `params->tex_lod[tmu][lod]` for texture coordinate scaling.
- Use that value for:
  - bilinear `tex_shift = 8 - tex_lod`
  - bilinear half-texel bias and coordinate shifts
  - point-sample coordinate shifts

100x result after Fix 4:

```text
verify=10240000
mismatch_spans=0
fb_mismatches=0
fb_within_tol=0
fb_over_tol=0
fb_zero_nonzero=0
fb_tol=5
fb_max_d565=(0,0,0)
aux_mismatches=0
state_mismatches=0
```

Impact:

- Hard tail is gone in the 100x validation cap.
- Clean run after removing temporary stage trace had zero mismatches.
- Dominant Mode[0] no longer appears as a hard-error bucket.

## Current Dominant Mode

Mode[0] current key:

```text
fbzMode=00010f71
depth=3
rgb_w=1
depth_w=1
alpha_en=0
fogMode=00000059
fog_en=1
fog_add=0
fog_mult=0
fog_src=18
fog_const=0
fbzColorPath=0c002401
tex_en=1
cc_mselect=1
cc_add=0
cca_mselect=0
cca_add=0
alphaMode=0004040e
alpha_test=0
alpha_blend=0
alpha_func=7
src_afunc=4
dest_afunc=0
textureMode0=4ec76a07
tex0_kind=0ec76000
tex0_local=0
tex0_tri=1
textureMode1=48241a07
tex1_kind=08241000
tex1_local=1
tex1_tri=1
```

Decoded focus:

- dual-TMU path
- perspective texture fetch
- bilinear filtering enabled by texture mode bits
- TMU0 trilinear / `LOD_FRAC` combine
- final color combine uses texture as color other and color0/iter local as factor
- FOG_W enabled, but early mismatch logs show `init_w=0`, so fog source itself is unlikely to be the only cause

Current suspicion:

- Current hard-error root was ARM64 TMU1 texture coordinate LOD scale.
- Remaining observed mismatch after Fix 4 is only within tolerance.
- Do not optimize yet.
- Do not use helper-backed dynarec paths except temporary debug scaffolding.
- If shared semantic lowering changes, ARM64 and x86-64 coverage is required. Current fixes were ARM64 backend-local parity fixes.

## Build/Validation Notes

Build command used:

```sh
cmake --build build -j$(sysctl -n hw.ncpu)
```

Signing command used:

```sh
codesign -s - --entitlements src/mac/entitlements.plist --force build/src/86Box.app
```

Observed build status:

- Builds succeeded after both fixes.
- Linker emitted existing macOS deployment-version dylib warnings.
- Warning also seen: `ld: warning: reducing alignment of section __DATA,__common from 0x8000 to 0x4000 because it exceeds segment maximum alignment`

## Next

Decide next validation cap or remove same-run validator before optimization work.
