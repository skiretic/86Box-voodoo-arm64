# ARM64 Voodoo Codegen Performance Analysis

Scope: static analysis/report only. Target baseline: ARMv8-A + ASIMD/NEON. No ARMv8.1, LSE, SVE, SME, dotprod, crypto, Apple-only, or CPU-specific requirements unless guarded and fallback-safe.

Primary file: `src/include/86box/vid_voodoo_codegen_arm64.h`.

Important x86-64 note: `src/include/86box/vid_voodoo_codegen_x86-64.h` is useful only for semantic coverage parity and rough pipeline shape. Do not copy its lowering mechanically. The ARM64 backend already documents x86-64 correctness hazards, including negate-after-shift texture combine behavior around `src/include/86box/vid_voodoo_codegen_arm64.h:2650-2660` and the `tc_reverse_blend_1`/`tca_reverse_blend_1` mismatch note around `src/include/86box/vid_voodoo_codegen_arm64.h:2731-2735`.

## 1. Hot likely codegen patterns + cost

### Per-pixel state reload/store tail

Hot pattern:

- `voodoo_generate()` emits one span loop starting at `loop_jump_pos` in `src/include/86box/vid_voodoo_codegen_arm64.h:2140-2143`.
- Tail updates every pixel in `src/include/86box/vid_voodoo_codegen_arm64.h:4244-4370`.
- `STATE_ib` uses `ADD_IMM_X + LD1_V4S + ADD/SUB_V4S + ST1_V4S` in `src/include/86box/vid_voodoo_codegen_arm64.h:4244-4254`.
- `STATE_z` uses `LDR_W + LDR_W dZdX + ADD/SUB + STR_W` in `src/include/86box/vid_voodoo_codegen_arm64.h:4256-4266`.
- `STATE_tmu0_s` uses `LDR_Q + ADD/SUB_V2D + STR_Q` in `src/include/86box/vid_voodoo_codegen_arm64.h:4268-4276`.
- `STATE_tmu0_w`, `STATE_w`, `STATE_tmu1_w` use scalar load/add/store in `src/include/86box/vid_voodoo_codegen_arm64.h:4278-4296` and `4312-4320`.
- `STATE_tmu1_s` uses `ADD_IMM_X + LD1_V4S + ADD/SUB_V2D + ST1_V4S` in `src/include/86box/vid_voodoo_codegen_arm64.h:4298-4310`.
- Counters use load/add/store each pixel in `src/include/86box/vid_voodoo_codegen_arm64.h:4323-4343`.
- `STATE_x` is better already: loaded once into `w28` and updated in-register, but still stored every pixel at `src/include/86box/vid_voodoo_codegen_arm64.h:2135-2138` and `4352-4365`.

Cost:

- High. This is unconditional per-pixel memory traffic after every accepted or skipped pixel.
- The current generated span owns the full loop, called once per span from `src/video/vid_voodoo_render.c:943-947`, so register residency can amortize across many pixels.
- Some state must remain memory-visible for existing generated code paths, but not all fields need storeback every iteration.

### Texture fetch control flow and stores

Hot pattern:

- `codegen_texture_fetch()` starts at `src/include/86box/vid_voodoo_codegen_arm64.h:1259`.
- Perspective mode emits `SDIV`, `CLZ`, dynamic shifts, several stores to `STATE_tex_s`, `STATE_tex_t`, `STATE_lod` in `src/include/86box/vid_voodoo_codegen_arm64.h:1264-1387`.
- No-perspective mode still stores `STATE_tex_s`, `STATE_tex_t`, `STATE_lod` in `src/include/86box/vid_voodoo_codegen_arm64.h:1399-1428`.
- Bilinear mode has mirror, clamp/wrap, edge branches, 4 texel loads, table weight loads, multiply/sum in `src/include/86box/vid_voodoo_codegen_arm64.h:1431-1705`.

Cost:

- High for textured spans. `SDIV` is expensive, bilinear path is memory-heavy, and `STATE_tex_*` stores create extra memory dependencies.
- `STATE_lod` reloads appear later in dual-TMU/detail/trilinear paths, for example `src/include/86box/vid_voodoo_codegen_arm64.h:2568-2584`, `2711-2723`, `2770-2786`, `2820-2837`, `2916-2932`.

### Dither address setup inside pixel loop

Hot pattern:

- Dither table base pointer is rebuilt in the per-pixel path with MOVZ/MOVK sequence in `src/include/86box/vid_voodoo_codegen_arm64.h:4037-4056`.
- `dither_g - dither_rb` offset is rebuilt per pixel in `src/include/86box/vid_voodoo_codegen_arm64.h:4113-4129`.

Cost:

- Medium to high when `dither` is active. This is pure setup work inside the RGB write path.
- x86-64 also materializes dither base in its write path at `src/include/86box/vid_voodoo_codegen_x86-64.h:3090-3165`, but ARM64 should not copy that shape.

### Alpha/fog factor table loads

Hot pattern:

- `alookup`, `aminuslookup`, and related tables are initialized in `src/include/86box/vid_voodoo_codegen_arm64.h:4680-4751`.
- `alookup[c]` is a broadcast of `c`; `aminuslookup[c]` is a broadcast of `255 - c`.
- Fog uses `alookup[fog_a + 1]` in `src/include/86box/vid_voodoo_codegen_arm64.h:3592-3602`.
- Alpha blend uses repeated `ADD_REG_X_LSL + LDR_D` table loads in `src/include/86box/vid_voodoo_codegen_arm64.h:3774-3934`.

Cost:

- Medium. Each table lookup burns address-generation + load bandwidth. Some factors can be synthesized from scalar alpha plus `DUP`/subtract.
- Preserve exact `+1`, doubled-index, and rounding semantics.

### Prologue constant loads and saved register set

Hot pattern:

- Prologue saves x19-x28, d8-d15 in `src/include/86box/vid_voodoo_codegen_arm64.h:1966-1985`; epilogue restores in `4377-4396`.
- Static pointers use MOVZ/MOVK chains in `src/include/86box/vid_voodoo_codegen_arm64.h:2017-2046`.
- NEON constants materialize address in x16 then `LDR_Q` in `src/include/86box/vid_voodoo_codegen_arm64.h:2066-2092`.

Cost:

- Medium for short spans and state churn. Less important than per-pixel tail, but still visible when many tiny spans hit compiled blocks.

### Block cache lookup/miss cost

Hot pattern:

- `voodoo_get_block()` scans up to 32 entries from `voodoo->jit_last_block[odd_even]` in `src/include/86box/vid_voodoo_codegen_arm64.h:4522-4555`.
- Miss path scans LRU and toggles W/X + flushes I-cache in `src/include/86box/vid_voodoo_codegen_arm64.h:4557-4604`.
- Called once before scanline loop from `src/video/vid_voodoo_render.c:790-792`.

Cost:

- Low to medium per frame. Not per pixel. Important when state variants churn or codegen miss rate is high.

## 2. Safe ARMv8-A lowering improvements

### Keep more span state register-resident

Idea:

- Load `STATE_ib`, `STATE_z`, `STATE_tmu0_s`, `STATE_tmu0_w`, `STATE_w`, optionally `STATE_tmu1_s`, `STATE_tmu1_w`, `STATE_pixel_count`, `STATE_texel_count` before `loop_jump_pos`.
- Use loop-carried GPR/NEON regs.
- Store back once at exit, or at narrow points where following generated code truly reads the field from memory.

Safe ARMv8-A basis:

- GPR add/sub, NEON `ADD_V4S`, `ADD_V2D`, `SUB_*`, `LDR_Q`, `STR_Q`, `LD1/ST1` are baseline ASIMD.
- No LSE/SVE/dotprod needed.

Main constraint:

- Current body often reads `STATE_z`, `STATE_w`, `STATE_ia`, `STATE_lod`, `STATE_tex_a`, `STATE_tex_s`, `STATE_tex_t` from memory. First slice should make only one or two fields resident and update their consumers.

Best first resident fields:

- `STATE_z`: many reads (`depth`, fog Z, alpha local select), simple scalar lifetime.
- `STATE_x`: already mostly resident in `w28`; consider deferred storeback only after checking every path that loads `STATE_x_tiled`/`STATE_x`.
- `STATE_ib` vector: high payoff, but consumers pack iterated BGRA at `src/include/86box/vid_voodoo_codegen_arm64.h:3040-3054`, so consumer rewrite needed.

Risk: medium.

### Hoist dither table base and G offset

Idea:

- If `dither`, materialize selected `dither_rb`/`dither_rb2x2` once before loop.
- Materialize `dither_g - dither_rb` or selected `dither_g` base once before loop.
- Use a callee-saved GPR only when `dither` active, or use stack spill if register pressure blocks it.

Safe ARMv8-A basis:

- Same MOVZ/MOVK + register addressing already used. No ISA risk.

Why first:

- It removes pure invariant setup from `src/include/86box/vid_voodoo_codegen_arm64.h:4037-4056` and `4113-4129`.
- Minimal semantic risk: selected by compile-time `dither`/`dither2x2` booleans already embedded in block.

Risk: low.

### Synthesize simple alpha vectors

Idea:

- Replace some `alookup`/`aminuslookup` loads with:
  - scalar factor in W reg
  - `DUP_V4H_GPR(vN, wFactor)`
  - for inverse factors, `MOV_V(vN, v9)` then `SUB_V4H(vN, vN, vFactor)` or scalar `EOR/ADD` only where exact `255 - a` semantics match.

Candidate sites:

- `AFUNC_ASRC_ALPHA`, `AFUNC_ADST_ALPHA`, `AFUNC_AOMSRC_ALPHA`, `AFUNC_AOMDST_ALPHA` in `src/include/86box/vid_voodoo_codegen_arm64.h:3774-3934`.
- Fog factor around `src/include/86box/vid_voodoo_codegen_arm64.h:3592-3602`, with exact `fog_a + 1` preservation.

Safe ARMv8-A basis:

- `DUP_V4H_GPR`, `SUB_V4H`, `MUL_V4H`, `USHR_V4H` are baseline ASIMD.

Risk: medium.

### Avoid needless stores of `STATE_tex_s`, `STATE_tex_t`, `STATE_lod`

Idea:

- Make `codegen_texture_fetch()` return live tex coords/LOD in caller-known regs for immediate bilinear/point sample use.
- Store `STATE_lod` only if later generated combine stage needs memory-visible LOD.
- In dual-TMU, avoid store/reload cycles between TMU1 fetch, TMU1 combine, TMU0 fetch, and trilinear/detail paths.

Safe ARMv8-A basis:

- Register plumbing only.

Risk: medium-high because `STATE_lod` is shared between TMU fetch and several combine modes.

### Literal-pool or per-block constant island

Idea:

- Replace repeated pointer MOVZ/MOVK setup in prologue with PC-relative literal loads or a small constant island inside/adjacent to `code_block`.
- Load pointer constants with baseline ARMv8-A literal `LDR Xt, literal` encoding, plus bounds checks.

Safe ARMv8-A basis:

- PC-relative literal loads are ARMv8-A baseline.

Risk: medium. Need patching/range discipline and W/X handling, but no platform-specific ISA.

## 3. Reg alloc / spill / temp lifetime issues

Current pinned GPRs in `voodoo_generate()`:

- `x0` = `voodoo_state_t *state`, `x1` = `voodoo_params_t *params`.
- `x8` = `fb_mem`, `x9` = `aux_mem`, loaded at `src/include/86box/vid_voodoo_codegen_arm64.h:2129-2133`.
- `x19` = `logtable`, `x20` = `alookup`, `x21` = `aminuslookup`, `x22` = `neon_00_ff_w`, `x23` = `i_00_ff_w`, `x24` = `real_y`, `x25` = `bilinear_lookup`, `x26` = `rgb565`, `w27` = `STATE_x2`, `w28` = cached `STATE_x` in `src/include/86box/vid_voodoo_codegen_arm64.h:2038-2050` and `2135-2138`.

Current pinned NEON:

- `v8` = `neon_01_w`, `v9` = `neon_ff_w`, `v10` = `neon_ff_b`, `v11` = fog color if enabled, `v12` = color deltas, `v15` = TMU0 ST deltas, `v14` = TMU1 ST deltas if dual TMU, from `src/include/86box/vid_voodoo_codegen_arm64.h:2052-2127`.

Issue:

- Many callee-saved regs are pinned globally even if block variant does not use them. Example: `x20`/`x21` only matter for alpha/fog factor table loads, `x25` only for bilinear, `x26` only for alpha blend RGB565 decode.
- Short-span prologue cost grows from broad save/restore at `src/include/86box/vid_voodoo_codegen_arm64.h:1966-1985` and `4377-4396`.
- Scratch regs `x16`/`x17` are used heavily. `x17` holds bilinear index at `src/include/86box/vid_voodoo_codegen_arm64.h:1515-1522`; this is okay intra-function, but longer live temp expansion can collide with later temporary use.
- `v0` is overloaded as color, texture, increment vector, and blend product scratch. This blocks simple register-resident `STATE_ib` unless color-combine and tail get a clearer vreg ownership plan.

Improvements:

- Build feature-conditioned pinned set:
  - no texture: skip `x25`, maybe `x19` depending fog W table.
  - no alpha blend: skip `x26`, many alpha table uses.
  - no dither: skip future dither-base pins.
- Make per-slice local register maps in comments before changing code. Avoid hidden reuse of `v0`, `v3`, `v4`, `v6`, `v13`, `v16`, `v17`.
- Prefer one resident scalar first (`z` or `w`) before resident vector fields.

Risk: medium.

## 4. Branch / flags / addressing / immediates / const-load opts

### Branches

- Existing use of `TBZ`, `CBZ`, `CBNZ`, and `CSEL` is good ARM64 baseline practice:
  - mirror bits in `src/include/86box/vid_voodoo_codegen_arm64.h:1462-1477`
  - depth W guards in `2294-2315`
  - stipple in `2171-2215`
  - alpha/depth skip branches in `2451-2507` and `3641-3681`
- Bilinear S clamp/wrap branches in `src/include/86box/vid_voodoo_codegen_arm64.h:1590-1684` are likely hot when bilinear. Consider split fast path:
  - common: S not edge, T already masked/clamped, two `LDR_D_REG` loads.
  - rare: clamp/wrap edge block.

Risk: medium-high. Edge exactness matters.

### Flags

- Current code often uses `CMP + CSEL` for clamps. Good baseline.
- Repeated clamp idiom `BIC_REG_ASR` + `CMP_IMM 0xff` + `CSEL` occurs for alpha in `src/include/86box/vid_voodoo_codegen_arm64.h:3137-3145`, `3173-3179`, `3347-3355`, `3570-3577`.
- Possible helper macro for emission consistency, but report-only: do not change code now.

Risk: low for macro cleanup, medium if changing arithmetic.

### Addressing

- Good: paired loads/stores where offsets fit, e.g. `STATE_fb_mem`/`STATE_aux_mem` at `src/include/86box/vid_voodoo_codegen_arm64.h:2132-2133`, counters at `4325-4337`.
- Limitation: `STATE_ib = 472` and `STATE_tmu1_s = 520` are not Q-aligned, causing `ADD + LD1/ST1` at `4244-4254` and `4298-4310`.
- `STATE_tmu0_s = 496` is Q-aligned and uses `LDR_Q/STR_Q` at `4270-4276`.

Improvement:

- Struct layout/padding in `src/video/vid_voodoo_render.c:45-97` can align hot vector groups:
  - `ib/ig/ir/ia`
  - `tmu1_s/tmu1_t`
  - maybe `pixel_count/texel_count`

Risk: high. `voodoo_state_t` layout is private to `src/video/vid_voodoo_render.c`, but ARM64 header has hard-coded offsets and `VOODOO_ASSERT_OFFSET` checks at `src/include/86box/vid_voodoo_codegen_arm64.h:1104-1156`. Any layout shift touches many offsets.

### Immediates and constant loads

- `EMIT_MOV_IMM64` already skips zero halfwords in `src/include/86box/vid_voodoo_codegen_arm64.h:2017-2046`.
- Dither path lacks equivalent hoist and rebuilds constants per pixel in `4037-4056` and `4113-4129`.
- `EMIT_LOAD_NEON_CONST` still materializes address then loads from static memory in `2066-2092`; possible literal pool or `MOVI`-style constants could help, but current macro is portable.

Risk: low for dither hoist; medium for literal-pool work.

## 5. ARM64 vs x86-64 backend parity notes

Parity rule:

- Use x86-64 backend for "does ARM64 cover same semantic variants?" only.
- Do not copy x86-64 lowering. It has known correctness problems and old x86-specific tradeoffs.

Relevant parity observations:

- x86-64 `codegen_texture_fetch()` starts at `src/include/86box/vid_voodoo_codegen_x86-64.h:78`; ARM64 counterpart starts at `src/include/86box/vid_voodoo_codegen_arm64.h:1259`.
- x86-64 prologue starts at `src/include/86box/vid_voodoo_codegen_x86-64.h:650-765`; ARM64 prologue is more explicit about platform W/X, callee-saved regs, and pinned constants at `src/include/86box/vid_voodoo_codegen_arm64.h:1861-2138`.
- x86-64 dither path at `src/include/86box/vid_voodoo_codegen_x86-64.h:3077-3221` also does per-pixel dither address setup; treat this as a parity smell, not a pattern to clone.
- x86-64 tail at `src/include/86box/vid_voodoo_codegen_x86-64.h:3256-3470` also reloads/stores span state every pixel. ARM64 can do better because it has more architectural GPRs and NEON regs.
- x86-64 cache is 8 slots with ring replacement in `src/include/86box/vid_voodoo_codegen_x86-64.h:3486-3527`; ARM64 has 32 slots, MRU-start scan, LRU, rejected slots, W/X toggles in `src/include/86box/vid_voodoo_codegen_arm64.h:4522-4604`.
- If a semantic lowering changes shared behavior, update/test both ARM64 and x86-64. If change is ARM64-only perf plumbing with same semantics, do not touch x86-64.

## 6. Risk per idea

Low:

- Hoist dither base pointer and `dither_g` offset out of per-pixel dither path.
- Feature-condition pinned pointer loads and callee-saved saves only after liveness audit.
- Add host-only emitted-code instrumentation/counters for code size and cache hit/miss behavior.

Medium:

- Synthesize `alookup`/`aminuslookup` vectors for alpha blend/fog factors.
- Keep `STATE_z` or `STATE_w` register-resident and update direct consumers.
- Literal-pool/per-block constants for pointer setup.
- Split bilinear common path from edge fallback without changing semantics.

High:

- Full register-resident span core for `ib/ig/ir/ia`, `tmu0_s/t`, `tmu1_s/t`, `w`, counters, and `x`.
- Repack `voodoo_state_t` for 16-byte alignment of hot fields.
- Change `codegen_texture_fetch()` interface broadly across dual-TMU/trilinear/detail paths.

## 7. Smallest impl slices first

1. Dither hoist only.
   - Move dither RB base and G offset setup out of loop when `dither` is true.
   - No semantic coverage change. ARM64-only.
   - Verify no extra reg conflict in `src/include/86box/vid_voodoo_codegen_arm64.h:4000-4169`.

2. Alpha/fog factor synthesis pilot.
   - Pick one site: `AFUNC_ASRC_ALPHA` at `src/include/86box/vid_voodoo_codegen_arm64.h:3857-3866` or fog at `3592-3602`.
   - Preserve `src_alpha * 2` indexing semantics and rounding sequence.
   - ARM64-only if exact output stays same.

3. Resident `STATE_z` pilot.
   - Load `STATE_z` before loop, update tail in-register, store at exit.
   - Convert `STATE_z` consumers in same block (`2373-2390`, `3186-3197`, `3561-3566`) to read resident reg.
   - Keep memory store before any path not converted.

4. Resident `STATE_ib` pilot for non-textured/no-fog/no-alpha-blend block class.
   - Narrow cache-key feature class first.
   - Convert iterated BGRA pack at `src/include/86box/vid_voodoo_codegen_arm64.h:3040-3054`.
   - Store once at exit.

5. Texture fetch store reduction.
   - Keep LOD live within one `codegen_texture_fetch()` call.
   - Then handle TMU0/TMU1/trilinear/detail consumers.

6. Bilinear fast/edge split.
   - Point-sample/no-perspective first if there is a clean feature class.
   - Bilinear wrap/clamp split later.

7. Layout/alignment experiment.
   - Only after perf counters prove tail memory traffic is still bottleneck.
   - Update `STATE_*` constants and `VOODOO_ASSERT_OFFSET`.

## 8. Validation plan: build, host probes if possible, VM batch plan if needed

Build:

- Run normal project build after any runtime codegen edit.
- Build must cover ARM64 host. If shared semantic coverage changes, also build x86-64 backend where available.

Host probes:

- Add temporary debug-only emitted-code counters if needed:
  - emitted byte size per block
  - dither block count
  - alpha blend factor-table path count
  - block cache hits/misses/rejects
- For perf slices, compare generated code size before/after for same pipeline keys.
- If adding small host probe, keep it temporary unless user asks to keep telemetry.

VM batch plan:

- Run VM validation when emitted runtime behavior changes.
- Batch isolated ARM64 dynarec sites. Do not burn one VM per site unless bisecting confirmed failure.
- Start with batches:
  - dither on/off RGB write paths
  - alpha blend factor paths
  - fog modes: constant, W table, Z, alpha
  - texture: point/no-perspective, point/perspective, bilinear wrap/clamp, dual-TMU if touched
- If failure appears, bisect by feature slice, not by whole report.
- Treat `[0147:0000B9BD] Illegal instruction 00008B55 (FF)` as known recurring guest noise, not failure by itself.

Next: implement slice 1, dither hoist in `src/include/86box/vid_voodoo_codegen_arm64.h`.
