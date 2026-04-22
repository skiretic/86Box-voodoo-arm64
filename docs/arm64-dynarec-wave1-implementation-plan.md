# ARM64 Dynarec Wave 1 Implementation Plan

## Scope and Canonical Inputs
- Repository: `/Users/anthony/projects/code/86Box-voodoo-arm64`
- Branch baseline for this plan: `ndr-analysis`
- Starting commit for execution: `05b05e97f` (move forward only; no history rewrite)
- Canonical audit source of truth: [docs/arm64-dynarec-investigation.md](./arm64-dynarec-investigation.md)
- Audit anchors used by this plan:
  - [Decision-Ready Plan](./arm64-dynarec-investigation.md#decision-ready-plan)
  - [Running Prioritized Backlog](./arm64-dynarec-investigation.md#running-prioritized-backlog)
  - [Findings Summary](./arm64-dynarec-investigation.md#findings-summary)

## Baseline Assumptions (Locked)
- `docs/arm64-dynarec-investigation.md` is canonical for slice intent and ranking.
- Current branch baseline already includes the post-sync delta review context.
- `F-019` and `F-020` are resolved baseline items and are not first-wave implementation slices.
- `A-022` remains optional measurement hygiene and stays out of wave 1.
- Wave-1 execution order is fixed: `S-01` -> `S-02` -> `S-03` -> `A-013`.
- Code-comment rule for this wave is strict:
  - every non-trivial changed code block must include developer-facing comments at change time.
  - comments must state both what the block does and why it was added/changed.
  - do not defer comments because code "looks obvious"; ambiguity should be treated as a defect.
  - comment cleanup/compaction is allowed later, after behavior is validated and stable.

## Execution Status (Current)
- Current branch/head: `ndr-analysis` @ `working-tree` (`A-013` frozen at `A-013i`; `S-03c` retry-decay validated in `r1/r2`)

### Slice Status Table (Authoritative)
| Slice | Status | Notes |
| --- | --- | --- |
| `S-01` | Completed | `codegen_MMX_ENTER()` stale-buffer patch-site fix landed; WL-05 quick/normal/smc baselines validated. |
| `S-02a` | Completed | `A-012` direct imm-store hooks + `CODEGEN_BACKEND_HAS_MOV_IMM` landed. |
| `S-02b` | Completed | `A-011` landed: bounded `host_arm64_mov_imm()` now tries `MOVN` and logical-immediate `ORR` before `MOVZ/MOVK` fallback. |
| `S-02` overall | Completed | `S-02a` + `S-02b` code landed and validation gate passed (WL-05 + workload checks). |
| `S-03` | Completed + follow-on tuned | `S-03a` telemetry + `S-03b` delayed `NO_IMMEDIATES` + `S-03c` ARM64-only retry-decay validated (`r1/r2`) with safety gates intact. |
| `A-013` | Completed (frozen at `A-013i`) | Relative branch/call shaping stack completed through `BL/B`, `CBNZ`, `BEQ`, shared `B.cond` patch path, and guarded `TBZ/TBNZ` patch path; gates passed with locked WL-05 hashes and safety markers. |

### Order Lock (Do Not Skip)
- Fixed order remains mandatory: `S-01` -> `S-02` -> `S-03` -> `A-013`.
- `A-013` implementation lane is now closed/frozen for this wave unless a correctness regression reopens it.
- Current executable next step after `A-*` closeout is post-wave churn optimization work (`S-03`-adjacent follow-on), not new template expansion.

## Recommended Execution Order and Scope Boundaries
1. `S-01` (correctness guardrail; smallest write set; unblocker for safe backend work)
2. `S-02` (direct imm-store fast path plus bounded immediate materialization improvements)
3. `S-03` (policy/churn changes after lower-level backend wins are in place)
4. `A-013` (JIT-local relative call/jump optimization after core wave slices are stable)

What can split into sub-steps:
- `S-02`:
  - `S-02a`: `A-012` direct imm-store hooks + `CODEGEN_BACKEND_HAS_MOV_IMM` enablement.
  - `S-02b`: `A-011` bounded `host_arm64_mov_imm()` improvements (`MOVN` + logical-immediate path) in same slice family.
- `S-03`:
  - `S-03a`: observability/state plumbing for churn-policy decision support.
  - `S-03b`: policy behavior change and threshold tuning.

What should stay bundled:
- `S-01` stale-buffer patch + local stale-site audit in one patch series.
- `A-013` call and jump target-classification behavior should land together (single behavioral unit).

What explicitly waits until after wave 1:
- `A-014`, `A-018`, `A-019`, `A-020`, `A-021`, `A-022`
- Also defer non-wave slices from the audit backlog unless a wave slice hard-dep appears during implementation.

## S-* Kickoff Pack (Prepared, No Launch)
- Active coding lane: `S-03` follow-on churn tuning (`S-03c`) at fixed `266666666`.
- `A-013` is frozen at `A-013i`; do not expand template surface unless correctness regression forces reopen.

S-03c target scope (ARM64-guarded):
- `src/cpu/386_dynarec.c`
- `src/codegen_new/codegen.h`
- optional telemetry-only touch: `scripts/dynarec/analyze-s03a-log.c`

S-03c change intent:
- reduce unnecessary `CODEBLOCK_NO_IMMEDIATES` promotions while preserving safety behavior.
- keep default low-noise logging policy (`86BOX_NEW_DYNAREC_STATS=1`, `86BOX_NEW_DYNAREC_TELEMETRY=0`, `86BOX_A013_TRACE=0`).
- keep x86-64 behavior unchanged.

S-03c pre-code baseline (exact commands, no VM launch):
```bash
cd /Users/anthony/projects/code/86Box-voodoo-arm64
git rev-parse --abbrev-ref HEAD
git rev-parse --short HEAD
./scripts/dynarec/analyze-s03a-log.sh --s-only "docs/perf-artifacts/arm64-dynarec/2026-04-22_16-55-55-Windows 98 Gaming PC-a013i-tbxz-r1/86box.log" "docs/perf-artifacts/arm64-dynarec/2026-04-21_19-44-43-Windows 98 Gaming PC-s03b/86box.log.gz"
```

S-03c validation criteria (next run):
- `WL-05` hashes unchanged.
- `unexpected_noimm_without_bmask=0`.
- `promote_no_immediates_per_dirty_hit` lower or equal versus `a013i-tbxz-r1`.
- no crash/hang/regression in locked workload flow.

S-03c rollback triggers:
- any WL-05 hash mismatch.
- any nonzero `unexpected_noimm_without_bmask`.
- materially higher `promote_no_immediates` churn with no compensating stability/perf gain.

### S-03c Retry-Decay Implementation (No-Launch Prep) (2026-04-22)
Change summary:
- Added ARM64-only retry-state decay in `exec386_dynarec_dyn()`:
  - when a `BYTE_MASK` block is executing stably outside the dirty list and not yet in `NO_IMMEDIATES`, clear stale `dirty_list_recompile_hits`.
  - intent: prevent distant, non-consecutive dirty events from inheriting old retry debt and prematurely promoting `NO_IMMEDIATES`.
- Added ARM64-only summary counter:
  - `retry_resets=<N>` in `DYNAREC_S03A_SUMMARY`.
- Updated parser to read/print/delta the new field:
  - `retry_resets`
  - `retry_resets_delta`

Validation criteria:
- Build/sign passes.
- `--s-only` parser output includes `retry_resets` on new logs.
- Existing gates still hold: `WL-05` unchanged, `unexpected_noimm_without_bmask=0`.

Rollback triggers:
- Any correctness issue linked to block recompilation policy.
- Unexpected rise in `promote_no_immediates_per_dirty_hit` with no measurable stability/perf benefit.
- Any safety marker regression (`unexpected_noimm_without_bmask` nonzero).

Exact commands (prep + verify, no VM launch):
```bash
cd /Users/anthony/projects/code/86Box-voodoo-arm64
git status --short --branch
./scripts/build-and-sign.sh
./scripts/dynarec/analyze-s03a-log.sh --s-only "docs/perf-artifacts/arm64-dynarec/2026-04-22_16-55-55-Windows 98 Gaming PC-a013i-tbxz-r1/86box.log" "docs/perf-artifacts/arm64-dynarec/2026-04-21_19-44-43-Windows 98 Gaming PC-s03b/86box.log.gz"
```

### S-03c Result Checkpoint (R1/R2) (2026-04-22)
- Runs:
  - `s03c-retry-decay-r1`: `docs/perf-artifacts/arm64-dynarec/2026-04-22_17-36-19-Windows 98 Gaming PC-s03c-retry-decay-r1/`
  - `s03c-retry-decay-r2`: `docs/perf-artifacts/arm64-dynarec/2026-04-22_17-51-22-Windows 98 Gaming PC-s03c-retry-decay-r2/`
- Guest markers:
  - `r1` Q3 timedemo: `1260 frames, 36.5 seconds: 34.5 fps`
  - `r2` Q3 timedemo: `1260 frames, 36.5 seconds: 34.5 fps`
  - `r2` 3DMark99: `2460 3DMarks`, `5873 CPU 3DMarks`
  - `WL-05` locked totals unchanged in both runs:
    - quick `45db7b65`
    - normal `2520dd5e`
    - smc `b86f22a1`
- Host telemetry summary:
  - safety marker held in both runs: `unexpected_noimm_without_bmask=0`
  - new `S-03c` counter active:
    - `r1 retry_resets=7106`
    - `r2 retry_resets=7242`
  - churn signal vs `a013i-tbxz-r1` baseline:
    - `ratio_promote_no_immediates_per_dirty_hit`:
      - baseline `0.009377`
      - `r1 0.001379`
      - `r2 0.001507`
  - `r2` repeatability vs `r1`:
    - ratio delta `+0.000128` (small; still far below baseline)
    - no new safety regressions.
- Decision:
  - lock `S-03c` as stable at current `266666666` profile.
  - keep ARM64-only retry-decay change.
  - proceed to next S-lane tuning slice (`S-03d`) without reopening `A-*`.

### S-03d Next Slice Prep (No-Launch) (2026-04-22)
Change intent:
- tune delayed `NO_IMMEDIATES` escalation threshold for ARM64-only churn behavior:
  - raise `DYNAREC_S03B_NO_IMM_THRESHOLD` from `2` to `3`.
  - keep all x86-64 behavior unchanged.
  - keep low-noise telemetry defaults.

Validation criteria:
- `WL-05` hashes unchanged.
- `unexpected_noimm_without_bmask=0`.
- `ratio_promote_no_immediates_per_dirty_hit` not materially higher than `S-03c r1/r2` lock range.
- no crash/hang regression in Q3/3DMark flow.

Rollback triggers:
- any safety marker regression.
- any WL-05 hash mismatch.
- clear churn regression (for example promotion ratio spike with no observable stability gain).

Exact no-launch prep commands:
```bash
cd /Users/anthony/projects/code/86Box-voodoo-arm64
git status --short --branch
rg -n "DYNAREC_S03B_NO_IMM_THRESHOLD|dirty_list_recompile_hits|retry_resets" src/cpu/386_dynarec.c src/codegen_new/codegen.h scripts/dynarec/analyze-s03a-log.c
./scripts/build-and-sign.sh
```

### S-03d Threshold-Refinement Implementation (No-Launch Checkpoint) (2026-04-22)
Change summary:
- Implemented ARM64-only threshold refinement:
  - `DYNAREC_S03B_NO_IMM_THRESHOLD: 2 -> 3` in `src/cpu/386_dynarec.c`.
  - x86-64 path remains unchanged.
- Intent:
  - require one additional consecutive dirty-list retry before `NO_IMMEDIATES` promotion.
  - pair with existing `S-03c` retry-decay so transient churn clears without premature escalation.

Validation criteria (next run):
- `WL-05` hashes unchanged.
- `unexpected_noimm_without_bmask=0`.
- `ratio_promote_no_immediates_per_dirty_hit` remains in `S-03c` lock band (no harmful regression).

Rollback triggers:
- any safety counter regression.
- any WL-05 hash mismatch.
- clear churn regression without compensating stability/perf gain.

Exact command for next telemetry launch:
```bash
./scripts/dynarec/launch-vm-telemetry-run.sh s03d-threshold3-r1
```

## Cross-Slice Validation Framework

### Build/Sign Gate (Required for every slice)
- Build command: `./scripts/setup-and-build.sh build`
- Gate expectation: build completes, app bundle produced at `build/src/86Box.app`.
- If build fails, do not advance; fix or roll back current slice.

### Codesign Verification (Required for every slice)
- Verify signature:
  - `codesign --verify --deep --strict build/src/86Box.app`
- Inspect signed entitlements:
  - `codesign -d --entitlements :- build/src/86Box.app`
- Gate expectation: verification passes and entitlements are present.

### JIT Entitlement Verification (Required for every slice)
- Confirm entitlement keys include:
  - `com.apple.security.cs.allow-jit`
  - `com.apple.security.cs.disable-library-validation`
- Source baseline is `src/mac/entitlements.plist`; signed app output must match intent.

### Minimum Smoke-Run Expectations (Required for every slice)
- Non-benchmark smoke only (no perf campaign in this wave):
  - `./build/src/86Box.app/Contents/MacOS/86Box --help`
  - `./build/src/86Box.app/Contents/MacOS/86Box -P` (or current project-equivalent non-benchmark sanity launch)
- Gate expectation: no immediate crash, abort loop, or startup regression.

### Artifact Retention Rules
- Store run artifacts under:
  - `docs/perf-artifacts/<YYYY-MM-DD>/arm64-dynarec-wave1/<slice-id>/`
- Retain at least:
  - build log
  - codesign verify output
  - entitlements dump
  - smoke-run stdout/stderr capture
  - metadata manifest

### Logfile and Metadata Naming Rules
- Logfile prefix format:
  - `<slice-id>-<scenario>-macos-arm64-<git_short>-rNN`
- Suggested files:
  - `<prefix>.build.log`
  - `<prefix>.codesign.txt`
  - `<prefix>.entitlements.plist.txt`
  - `<prefix>.smoke.log`
  - `<prefix>.metadata.json`
- Metadata minimum keys:
  - `slice_id`, `branch`, `commit`, `host`, `timestamp_utc`, `build_cmd`, `run_cmd`, `result`, `notes`

### Slice-Advance Gate (When it is legal to proceed)
- A slice may advance only when all are true:
  - acceptance criteria satisfied
  - shared build/sign/JIT/smoke gates satisfied
  - no unresolved correctness regression from targeted checks
  - rollback trigger not hit

## Canonical VM Target (Locked)

Source: `/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 Gaming PC/86box.cfg` (read on 2026-04-21).

Locked profile for wave-1 validation:
- Machine: `p5a`
- CPU family: `k6_2`
- CPU multiplier/speed: `3.5` / `233333333`
- Dynarec: `cpu_use_dynarec = 1`
- FPU: `internal`
- RAM: `131072` (128 MB)
- Video card: `voodoo3_3k_agp`
- Voodoo options: `render_threads = 1`, `recompiler = 1`, `gpu_rendering = 1`
- Disk image: `windows98gamingpc.vhd`
- CD image path: `/Users/anthony/Desktop/86box-perf-assets-2026-04-14/win98-perf-kit-final.iso`

Locking rules:
- Do not change canonical VM hardware config during wave-1 slice validation.
- If config must change, treat it as a new profile and rerun the full slice gate for comparability.
- Keep one config snapshot copy with artifacts (for example `vm-config-rNN.cfg`).

Secondary profile policy (optional):
- Keep an optional MMX-focused profile only for targeted checks (mainly `S-01` path stress).
- Do not replace canonical K6-2 baseline with MMX-only config for overall slice pass/fail decisions.
- If MMX profile is used, store artifacts separately under the same slice with a distinct scenario name.

### MMX Secondary Profile (Approved)
- Purpose: targeted validation only (especially `S-01` MMX/FPU entry behavior).
- Profile source: duplicate canonical VM config and change CPU fields only.
- Locked CPU target for this profile: Mobile Pentium MMX 300 MHz.
- Config fields for this profile:
  - `cpu_family = pentium_tillamook`
  - `cpu_multi = 4.5`
  - `cpu_speed = 300000000`
  - `cpu_use_dynarec = 1`
  - `fpu_type = internal`
- Hard rule: all non-CPU settings stay identical to canonical profile for comparability.
- Artifact naming:
  - use scenario tag `mmx-secondary` in logfile prefix.
  - example prefix: `<slice-id>-mmx-secondary-macos-arm64-<git_short>-rNN`.
- Gate rule:
  - canonical K6-2 profile decides pass/fail for slice advancement.
  - MMX secondary results are supplemental; they can block advancement only for MMX-targeted correctness regressions (not raw perf deltas).

## Repeatable Workload Protocol (Wave 1)

### Why this is required
- Slice validation must be comparable across sessions; workload drift invalidates before/after conclusions.

### Workload classes to lock
- `WL-00-smoke-boot`: baseline launch + boot-to-idle + clean exit.
- `WL-01-3dmark99-full`: 3DMark99 full benchmark (MMX/FPU-path touch; primary for `S-01` targeted checks).
- `WL-02-q3-demo-four`: Quake III demo four timedemo run (branch/helper density; primary relevance for `A-013`).
- `WL-03-perf-run-bat`: deterministic file-copy churn script from perf-kit ISO (primary relevance for `S-03`).
- `WL-04-3dmark2000-full`: 3DMark2000 full benchmark (secondary graphics stress and regression cross-check).
- `WL-05-win98-microstress`: custom Win98 test app built with MinGW to target specific dynarec paths directly.

### Workload definitions (locked)
- `WL-01-3dmark99-full`:
  - guest action: run full benchmark suite from installed 3DMark99 profile.
  - success marker: benchmark completes and returns to results/summary screen without crash.
- `WL-02-q3-demo-four`:
  - guest action: run Quake III timedemo using demo four with fixed in-game settings.
  - config lock: use your current Q3 settings exactly as currently configured; no setting changes during wave 1.
  - success marker: timedemo completes and prints/records result line.
- `WL-03-perf-run-bat`:
  - source: `win98-perf-kit-final.iso` -> `SCRIPTS/PERF_RUN.BAT`.
  - guest command: `D:\SCRIPTS\PERF_RUN.BAT D:` (adjust drive letter only if needed).
  - required markers in guest output:
    - `PERF_RUN_START`
    - `PERF_PREP_START`
    - `PERF_PREP_END`
    - `PERF_LOOP_START 1`
    - `PERF_LOOP_END 1`
    - `PERF_LOOP_START 2`
    - `PERF_LOOP_END 2`
    - `PERF_DONE`
  - failure marker: any `PERF_ERROR ...` line.
- `WL-04-3dmark2000-full`:
  - guest action: run full benchmark suite from installed 3DMark2000 profile.
  - success marker: benchmark completes and returns to results/summary screen without crash.
- `WL-05-win98-microstress`:
  - guest action: run a custom Win98 console app that executes deterministic stress phases with explicit start/end markers.
  - source path: `tools/win98-microstress/microstress_win98.c`
  - host build helper: `tools/win98-microstress/build-win98-microstress.sh`
  - host ISO helper: `tools/win98-microstress/create-win98-microstress-iso.sh`
  - generated ISO: `tools/win98-microstress/win98-microstress-kit.iso`
  - guest binary name: `MICROSTR.EXE`
  - required phase coverage:
    - immediate-heavy stores and reloads (supports `S-02`)
    - branch/helper-dense control flow (supports `A-013`)
    - optional MMX/FPU entry-touch phase (supports `S-01` targeted validation)
    - optional self-modifying/churn-touch phase guarded behind explicit flag (supports `S-03` targeted validation)
  - output contract:
    - prints `MICROSTRESS_START`
    - prints `PHASE_START <name>` / `PHASE_END <name>`
    - prints deterministic counters/checksums per phase
    - prints `MICROSTRESS_DONE`
  - failure marker: nonzero exit code or any `MICROSTRESS_ERROR` line.

### WL-05 Baseline Anchor (Validated)
- Validation checkpoint date: `2026-04-21`
- VM profile: `k6_2_233_canonical`
- Quick command path: `D:\SCRIPTS\MRUNQ.BAT D:`
- Accepted quick baseline lines:
  - `MICROSTRESS_DONE total=45db7b65`
  - `MICRO_SUMMARY status=OK mode=quick log=C:\PERF_LOG\wl05-micro.log`
- Normal command path: `D:\SCRIPTS\MRUN.BAT D:`
- Accepted normal baseline lines:
  - `MICROSTRESS_DONE total=2520dd5e`
  - `MICRO_SUMMARY status=OK mode=normal log=C:\PERF_LOG\wl05-micro.log`
- SMC command path: `D:\SCRIPTS\MRUNS.BAT D:`
- Accepted SMC baseline lines:
  - `MICROSTRESS_DONE total=b86f22a1`
  - `MICRO_SUMMARY status=OK mode=smc log=C:\PERF_LOG\wl05-micro.log`
- Reporting contract: for routine runs, only these two lines are required from the VM console.

### S-02 Validation Closeout (2026-04-21)
- `WL-05` one-shot result (`MRUNALL.BAT`) matched locked baselines:
  - quick total `45db7b65` / `status=OK`
  - normal total `2520dd5e` / `status=OK`
  - smc total `b86f22a1` / `status=OK`
- `WL-01` 3DMark99 full benchmark:
  - completed without crash/hang/visual corruption
  - score snapshot: `2519 3DMarks`, `5163 CPU 3DMarks`
- `WL-02` Quake III demo four:
  - `1260 frames, 37.4 seconds: 33.7 fps`
- Interpretation note for this host class:
  - absolute benchmark numbers are secondary because VM may run below 100% emulation speed on Apple Silicon host load conditions.
  - gate priority remains correctness/stability + gross-regression detection, not absolute FPS/score movement.
- Cross-arch safety note:
  - `S-02b` touched `src/codegen_new/codegen_backend_arm64_ops.c` under ARM64 build guard (`#if defined __aarch64__ || defined _M_ARM64`).
  - x86-64 backend behavior remains unchanged by this slice.

### S-03 Validation Closeout (2026-04-21)
- `S-03a` telemetry and launcher plumbing landed at commit `b9d3c2f48`.
- `S-03b` code status:
  - implemented in working tree (pending commit), guarded so policy/state changes are ARM64-only.
  - touched files:
    - `src/cpu/386_dynarec.c`
    - `src/codegen_new/codegen.h`
    - `src/codegen_new/codegen_block.c`
  - non-ARM64 keeps baseline promotion behavior.
- Host log used for validated run:
  - `docs/perf-artifacts/arm64-dynarec/2026-04-21_19-44-43-Windows 98 Gaming PC-s03b/86box.log`
- Parser/manual agreement (key churn metrics):
  - `dirty_list_hits=19381`
  - `promote_byte_mask=8795`
  - `promote_no_immediates=272`
  - `defer_no_immediates=415`
  - transition actions:
    - `BYTE_MASK=8795`
    - `NO_IMMEDIATES=272`
    - `DEFER_NO_IMMEDIATES=415`
- Delta versus `S-03a` baseline log:
  - `promote_no_immediates_delta=-18`
  - `defer_no_immediates_delta=+415`
  - `ratio_promote_no_immediates_per_dirty_hit_delta=-0.000536`
  - `rebuild_paths_delta=-56291`
- Correctness/stability gates from guest runs:
  - `WL-05 MRUNALL` all modes unchanged and `status=OK`:
    - quick `45db7b65`
    - normal `2520dd5e`
    - smc `b86f22a1`
  - Quake III timedemo: `1260 frames, 36.3 seconds: 34.7 fps`
  - 3DMark99 full: `2686 3DMarks`, `5155 CPU 3DMarks`
- Decision:
  - `S-03` gate passes for wave-1 progression.
  - next slice is `A-013`.

### A-013a+b Regression Gate (2026-04-21)
- Code implemented (working tree):
  - `codegen_allocator_contains_host_ptr()` and `codegen_allocator_can_branch_imm26()` in allocator module.
  - `host_arm64_call()` uses direct `BL` for local+in-range targets, else fallback `MOVX+BLR`.
  - `host_arm64_jump()` uses direct `B` for local+in-range targets, else fallback `MOVX+BR`.
- Regression run artifact root:
  - `docs/perf-artifacts/arm64-dynarec/2026-04-21_20-37-30-Windows 98 Gaming PC-a013ab-regress-rerun4/`
- Guest workload gate:
  - `MRUNALL` remained stable:
    - quick `45db7b65`
    - normal `2520dd5e`
    - smc `b86f22a1`
  - Quake III timedemo: `1260 frames, 37.1 seconds: 34.0 fps`
  - 3DMark99: `2598 3DMarks`, `5150 CPU 3DMarks`
- Host telemetry/parsing (manual + parser agreed):
  - `dirty_list_hits=24276`
  - `promote_byte_mask=9881`
  - `promote_no_immediates=356`
  - `defer_no_immediates=550`
  - `unexpected_noimm_without_bmask=0`
- Decision:
  - no regression blocker observed for `A-013a+b`.
  - proceed to deeper `A-013c/d/e`.

### A-013c/d/e Telemetry Gate (2026-04-21)
- Regression run artifact root:
  - `docs/perf-artifacts/arm64-dynarec/2026-04-21_21-09-06-Windows 98 Gaming PC-a013cde-r2/`
- Guest workload gate:
  - `MRUNALL` remained stable:
    - quick `45db7b65`
    - normal `2520dd5e`
    - smc `b86f22a1`
  - Quake III timedemo: `1260 frames, 37.0 seconds: 34.0 fps`
  - 3DMark99: `2644 3DMarks`, `5152 CPU 3DMarks`
- Host telemetry/parsing (manual + parser agreed):
  - `dirty_list_hits=21899`
  - `promote_byte_mask=8274`
  - `promote_no_immediates=193`
  - `defer_no_immediates=309`
  - `unexpected_noimm_without_bmask=0`
  - `A013_PATH total=4980736`
  - `A013_PATH call_rel=3693134`
  - `A013_PATH jump_rel=786750`
  - `A013_PATH call_abs_nonlocal=500852`
  - `A013_PATH jump_abs_nonlocal=0`
  - `A013_PATH call_abs_range=0`
  - `A013_PATH jump_abs_range=0`
  - `A013_PATH ratio_relative_total=0.899442`
- Interpretation:
  - S-03 safety counters remain healthy after `A-013c/d/e`.
  - `A-013` achieved high relative-path adoption with zero range-fallback hits in this run.
  - Logging policy updated to reduce measurement perturbation:
    - default `A013_PATH_SUMMARY` cadence is now every `1,048,576` path events (previously `262,144`).
    - detailed `A013_PATH_TRACE` is now explicit opt-in only via `86BOX_A013_TRACE=1`.
- Decision:
  - `A-013c/d/e` passes current regression gate on locked workloads.
  - proceed to remaining `A-013` deepening only if we need extra branch-shape tightening; otherwise prepare wave-1 closeout.

### A-013f Conditional-Branch Deepening Gate Plan (2026-04-22)
- Change summary:
  - ARM64-only `host_arm64_CBNZ()` now uses three-tier shaping:
    - local + imm19 range: direct `CBNZ`.
    - local + imm26 (but not imm19): `CBZ skip` + relative `B`.
    - non-local or out-of-range: `CBZ skip` + absolute `MOVX_IMM + BR`.
  - Added allocator helper `codegen_allocator_can_branch_imm19()` for bounded CBZ/CBNZ range checks.
  - Added additive `A013_PATH_SUMMARY` counters for conditional-branch shaping:
    - `cbnz_rel19`, `cbnz_rel26`, `cbnz_abs_nonlocal`, `cbnz_abs_range`, `cbnz_total`
  - Updated parser `scripts/dynarec/analyze-s03a-log.c` to parse and report new counters while remaining backward-compatible with old logs.
- Validation criteria:
  - build/sign/JIT launch gates pass with default low-noise logging (`86BOX_A013_TRACE=0`).
  - no `S-03` safety regression in parser output versus locked baseline:
    - `unexpected_noimm_without_bmask=0`
    - no harmful drift in dirty-list / promotion counters.
  - `WL-05` hashes remain locked if workload run includes microstress:
    - quick `45db7b65`
    - normal `2520dd5e`
    - smc `b86f22a1`
  - `A013_PATH_SUMMARY` shows nonzero `cbnz_total` and healthy relative usage (`cbnz_rel19 + cbnz_rel26`), with no correctness signal from fallback path.
- Rollback triggers:
  - any control-flow correctness anomaly, crash, or dispatch misroute after CBNZ-path change.
  - any `WL-05` hash mismatch.
  - any new `S-03` safety regression or unstable churn signature tied to this slice.
  - if standard run shows suspicious conditional fallback pattern (for example large unexpected `cbnz_abs_range`), revert A-013f and re-check.
- Exact commands:
  - `./scripts/build-and-sign.sh`
  - `RUN_TAG=a013f-cbnz-r3 ./scripts/dynarec/prepare-vm-telemetry-run.sh`
  - `./scripts/dynarec/launch-vm-telemetry-run.sh a013f-cbnz-r3`
  - after guest workload:
  - `./scripts/dynarec/analyze-s03a-log.sh "<a013f-log>" "docs/perf-artifacts/arm64-dynarec/2026-04-21_21-09-06-Windows 98 Gaming PC-a013cde-r2/86box.log"`
- Launch status (2026-04-21):
  - launch completed: `run_tag=a013f-cbnz-r3`
  - active run dir: `docs/perf-artifacts/arm64-dynarec/2026-04-21_21-48-59-Windows 98 Gaming PC-a013f-cbnz-r3/`
  - host log target: `docs/perf-artifacts/arm64-dynarec/2026-04-21_21-48-59-Windows 98 Gaming PC-a013f-cbnz-r3/86box.log`
  - guest workload input pending.

### A-013g Conditional-Branch Expansion Gate Plan (2026-04-22)
- Change summary:
  - ARM64-only `host_arm64_BEQ()` now uses three-tier shaping:
    - imm19 range: direct `B.EQ`.
    - imm26 range: inverse-conditional skip + relative `B`.
    - out-of-range: inverse-conditional skip + absolute `MOVX_IMM + BR`.
  - `host_arm64_CBNZ()` relative eligibility is widened from local-only to any imm-reachable target (`imm19` then `imm26` bridge), preserving absolute fallback when needed.
  - Added additive `A013_PATH_SUMMARY` counters:
    - `beq_rel19`, `beq_rel26`, `beq_abs_nonlocal`, `beq_abs_range`, `beq_total`
  - Updated parser `scripts/dynarec/analyze-s03a-log.c` to parse/report new BEQ counters while staying backward-compatible.
- Validation criteria:
  - ARM64 build/sign + launch pass with low-noise defaults (`86BOX_NEW_DYNAREC_STATS=1`, `86BOX_NEW_DYNAREC_TELEMETRY=0`, `86BOX_A013_TRACE=0`).
  - No `S-03` regression markers (`unexpected_noimm_without_bmask=0`).
  - `WL-05` hashes remain locked if microstress workload is run:
    - quick `45db7b65`
    - normal `2520dd5e`
    - smc `b86f22a1`
  - `A013_PATH_SUMMARY` shows nonzero `beq_total` and high BEQ relative usage (`beq_rel19 + beq_rel26`) with no correctness signal from fallback.
- Rollback triggers:
  - any control-flow correctness anomaly after BEQ/CBNZ path widening.
  - any `WL-05` hash mismatch.
  - any harmful `S-03` safety drift tied to this slice.
  - unexpected growth of `beq_abs_range` or `cbnz_abs_range` in representative workload runs.
- Exact commands:
  - `./scripts/build-and-sign.sh`
  - `RUN_TAG=a013g-beq-r1 ./scripts/dynarec/prepare-vm-telemetry-run.sh`
  - `./scripts/dynarec/launch-vm-telemetry-run.sh a013g-beq-r1`
  - after guest workload:
  - `./scripts/dynarec/analyze-s03a-log.sh "<a013g-log>" "docs/perf-artifacts/arm64-dynarec/2026-04-21_21-09-06-Windows 98 Gaming PC-a013cde-r2/86box.log.gz"`

### A-013g Lock-In Checkpoint (2026-04-21)
- Runs:
  - `a013g-beq-r1`: `docs/perf-artifacts/arm64-dynarec/2026-04-21_22-20-39-Windows 98 Gaming PC-a013g-beq-r1/`
  - `a013g-beq-r2`: `docs/perf-artifacts/arm64-dynarec/2026-04-21_22-33-57-Windows 98 Gaming PC-a013g-beq-r2/`
- Guest workload markers:
  - `WL-05` locked totals remained unchanged in both runs:
    - quick `45db7b65`
    - normal `2520dd5e`
    - smc `b86f22a1`
  - Quake III demo four timedemo markers:
    - `r1`: `1260 frames, 38.3 seconds: 32.9 fps`
    - `r2`: `1260 frames, 38.0 seconds: 33.1 fps`
  - 3DMark99 full score markers:
    - `r1`: `2434 3DMarks`, `5152 CPU 3DMarks`
    - `r2`: `2461 3DMarks`, `5146 CPU 3DMarks`
- Host telemetry summary:
  - `unexpected_noimm_without_bmask=0` in both runs.
  - New BEQ-path counters active and healthy:
    - `r2`: `beq_rel19=21721`, `beq_rel26=313839`, `beq_abs_nonlocal=0`, `beq_abs_range=0`, `beq_total=335560`.
  - CBNZ-path counters remain fully relative (`cbnz_abs_nonlocal=0`, `cbnz_abs_range=0`).
  - A-013 call/jump relative share improved vs `a013f-cbnz-r3`:
    - `a013f-cbnz-r3`: `0.899981`
    - `a013g-beq-r1`: `0.900601`
    - `a013g-beq-r2`: `0.901420`
  - Low-noise telemetry policy held (`86BOX_NEW_DYNAREC_STATS=1`, `86BOX_NEW_DYNAREC_TELEMETRY=0`, `86BOX_A013_TRACE=0`), with small host logs (~6-7MB for full run).
- Operator observation captured:
  - In 3DMark99 texture-heavy section, emulator speed floor improved from ~`60%` (upstream sanity run) to sustained `80%+` on this branch.
  - In Q3 demo four non-timedemo playback, emulator maintained `100%` significantly more often.
  - This branch therefore favors real-time emulation stability even when synthetic benchmark score deltas are mixed.
- Tooling hardening completed during lock-in:
  - telemetry launcher retry/fallback path hardened in `scripts/dynarec/launch-vm-telemetry-run.sh`.
  - parser `A013 total` key-match bug fixed in `scripts/dynarec/analyze-s03a-log.c`.
- Decision:
  - lock in `A-013g` as current validated baseline on `ndr-analysis`.
  - next experimentation should focus on guest CPU frequency headroom using this baseline, not reverting branch-shape work.

### A-013g 266 MHz Headroom Checkpoint (2026-04-21)
- Run:
  - `a013g-266-r1`: `docs/perf-artifacts/arm64-dynarec/2026-04-21_22-53-19-Windows 98 Gaming PC-a013g-266-r1/`
- VM config checkpoint:
  - `cpu_family = k6_2`
  - `cpu_multi = 4`
  - `cpu_speed = 266666666`
- Guest workload markers:
  - Q3 demo four timedemo: `1260 frames, 35.4 seconds: 35.6 fps`
  - 3DMark99 full: `2507 3DMarks`, `5885 CPU 3DMarks`
  - `WL-05` hashes still locked:
    - quick `45db7b65`
    - normal `2520dd5e`
    - smc `b86f22a1`
- Host telemetry summary:
  - `unexpected_noimm_without_bmask=0`
  - BEQ/CBNZ fallback counters remain zero (`beq_abs_nonlocal=0`, `beq_abs_range=0`, `cbnz_abs_nonlocal=0`, `cbnz_abs_range=0`)
  - low-noise logging policy held (single-digit MB log, no trace flood).
- Operator observation captured:
  - at 266 MHz, emulator maintained `100%` speed significantly more often than upstream in the same heavy sections, while expectedly below the easier 233 MHz load level.
- Decision:
  - continue active dev baseline at `266 MHz`.
  - only increase CPU frequency after `266 MHz` shows consistent `100%` in targeted heavy scenes while keeping `WL-05` hashes and stability clean.

### A-013h Big Conditional-Branch Sweep Plan (2026-04-22)
- Change summary:
  - Extend A-013 conditional shaping across the shared `B.cond` patch path (not just `BEQ` direct callsites).
  - `host_arm64_branch_set_offset()` now upgrades `B.cond-skip + B` templates to direct single `B.cond` when final target is imm19-reachable; otherwise keeps relative `B` fallback.
  - Add additive summary counters:
    - `bcond_rel19`, `bcond_rel26`, `bcond_total`
  - Keep existing low-noise policy (`stats=1`, `telemetry=0`, `A013_TRACE=0`), no trace flood.
- Validation criteria:
  - ARM64 build/sign + launch passes at fixed `266 MHz` baseline.
  - `WL-05` hashes remain locked.
  - `unexpected_noimm_without_bmask=0`.
  - New `bcond_*` counters present and nonzero in branch-heavy runs, with healthy relative usage.
- Rollback triggers:
  - control-flow correctness anomaly in Jcc-heavy paths.
  - any `WL-05` hash mismatch.
  - any harmful S-03 safety regression.
  - suspicious surge in fallback branch shape inconsistent with prior runs.
- Exact commands:
  - `./scripts/build-and-sign.sh`
  - `RUN_TAG=a013h-bcond-r1 ./scripts/dynarec/prepare-vm-telemetry-run.sh`
  - `./scripts/dynarec/launch-vm-telemetry-run.sh a013h-bcond-r1`
  - after guest workload:
  - `./scripts/dynarec/analyze-s03a-log.sh "<a013h-log>" "docs/perf-artifacts/arm64-dynarec/2026-04-21_22-53-19-Windows 98 Gaming PC-a013g-266-r1/86box.log"`

### A-013h Result Checkpoint (2026-04-21)
- Run:
  - `a013h-bcond-r1`: `docs/perf-artifacts/arm64-dynarec/2026-04-21_23-11-46-Windows 98 Gaming PC-a013h-bcond-r1/`
- Guest markers:
  - Q3 timedemo: `1260 frames, 36.7 seconds: 34.4 fps`
  - 3DMark99: `2484 3DMarks`, `5891 CPU 3DMarks`
  - `WL-05` locked totals unchanged:
    - quick `45db7b65`
    - normal `2520dd5e`
    - smc `b86f22a1`
- Host telemetry summary:
  - `unexpected_noimm_without_bmask=0`
  - new A-013h counters active:
    - `bcond_rel19=772357`
    - `bcond_rel26=549442`
    - `bcond_total=1321799`
  - BEQ/CBNZ absolute fallback counters remained zero in this run.
- Operator observation:
  - sustained `100%` emulation speed behavior remains materially improved versus upstream baseline in the same heavy sections.
- Decision:
  - lock in `A-013h` as current best branch-shaping state at `266 MHz`.
  - no CPU frequency bump now (explicitly hold at `266 MHz`).
  - next code step should be correctness-safe consolidation/cleanup before any higher-frequency experimentation.

### A-013h Guarded Consolidation (Prelaunch) (2026-04-21)
- Change summary:
  - Harden `host_arm64_branch_set_offset()` so A-013h collapse/telemetry only applies to the exact `B.cond +8` then `B` template emitted by ARM64 branch helpers.
  - Keep fallback patching behavior unchanged (`B` rel26 still emitted), but avoid treating unrelated neighboring instruction shapes as `bcond` template events.
  - Added explicit comment-level guard intent in ARM64 backend code for safer future maintenance.
- Validation criteria:
  - build/sign passes on ARM64.
  - no x86-64 behavior change (ARM64-only backend file touched).
  - telemetry prelaunch prep succeeds with default low-noise policy.
- Rollback triggers:
  - any guest control-flow anomaly or branch mispatch symptoms after launch.
  - `WL-05` hash drift.
  - S-03 safety counter regression (`unexpected_noimm_without_bmask` nonzero).
- Exact commands (stop before VM launch):
  - `./scripts/build-and-sign.sh`
  - `RUN_TAG=a013h-bcond-r2 ./scripts/dynarec/prepare-vm-telemetry-run.sh`
  - do not launch in this step (operator requested sleep hold).

### A-013h Guarded Consolidation Result (2026-04-22)
- Run:
  - `a013h-bcond-r2`: `docs/perf-artifacts/arm64-dynarec/2026-04-22_16-30-52-Windows 98 Gaming PC-a013h-bcond-r2/`
- Guest markers:
  - 3DMark99: `2450 3DMarks`, `5893 CPU 3DMarks`
  - `WL-05` locked totals unchanged:
    - quick `45db7b65`
    - normal `2520dd5e`
    - smc `b86f22a1`
- Host telemetry summary:
  - `unexpected_noimm_without_bmask=0`
  - A-013 counters remained healthy at scale:
    - `call_rel=3478667`, `call_abs_nonlocal=453496`
    - `cbnz_rel19=237895`, `cbnz_rel26=3488609`, `cbnz_abs_nonlocal=0`, `cbnz_abs_range=0`
    - `beq_rel19=20147`, `beq_rel26=297379`, `beq_abs_nonlocal=0`, `beq_abs_range=0`
    - `bcond_rel19=766867`, `bcond_rel26=552784`, `bcond_total=1319651`
  - log volume remained low-noise (`86box.log` ~`7.2M`).
- Delta vs `a013h-bcond-r1`:
  - no safety regression (`unexpected_noimm_without_bmask` stayed `0`).
  - `bcond_total` near-flat (`1321799 -> 1319651`), with small rel19/rel26 redistribution only.
  - no BEQ/CBNZ absolute fallback regression (all absolute fallback counters stayed `0`).
- Operator real-world observation:
  - telemetry-wrapper run felt slightly slower than expected.
  - subsequent normal double-click launch (no telemetry wrapper flow) felt good in real-world play.
- Decision:
  - keep `a79adfceb` guard-hardening in place.
  - treat wrapper-vs-normal-launch feel difference as expected run-path variance unless repeated objective regressions appear.
  - continue on active `266 MHz` baseline.

### A-013i TBZ/TBNZ Patch-Path Sweep (2026-04-22)
- Change summary:
  - Extend guarded conditional patch shaping to `TBZ/TBNZ + B` template sites in `host_arm64_branch_set_offset()`.
  - Add ARM64 allocator range helper for `imm14` reach checks (`codegen_allocator_can_branch_imm14()`).
  - Template collapse behavior:
    - if final target is imm14-reachable from the `TBZ/TBNZ` instruction, collapse to direct single-op `TBZ/TBNZ` and NOP the trailing `B`.
    - otherwise keep trailing `B` rel26 fallback.
  - Add additive telemetry counters:
    - `tbxz_rel14`, `tbxz_rel26`, `tbxz_total`
  - Parser updated to parse/print/delta new `tbxz_*` fields.
- Validation criteria:
  - ARM64 build/sign passes.
  - low-noise telemetry defaults unchanged (`stats=1`, `telemetry=0`, `trace=0`).
  - `WL-05` hash lock unchanged.
  - `unexpected_noimm_without_bmask=0`.
  - `tbxz_*` counters present and nonzero in workloads that hit those patch paths.
- Rollback triggers:
  - any branch correctness anomaly in FPU/MMX/tag-test heavy paths.
  - `WL-05` hash mismatch.
  - S-03 safety regression or unexpected absolute fallback growth in existing A-013 paths.
- Exact commands:
  - `./scripts/build-and-sign.sh`
  - `RUN_TAG=a013i-tbxz-r1 ./scripts/dynarec/prepare-vm-telemetry-run.sh`
  - `./scripts/dynarec/launch-vm-telemetry-run.sh a013i-tbxz-r1`
  - after guest workload:
  - `./scripts/dynarec/analyze-s03a-log.sh "<a013i-log>" "docs/perf-artifacts/arm64-dynarec/2026-04-22_16-30-52-Windows 98 Gaming PC-a013h-bcond-r2/86box.log"`

### A-013i Result Checkpoint (2026-04-22)
- Run:
  - `a013i-tbxz-r1`: `docs/perf-artifacts/arm64-dynarec/2026-04-22_16-55-55-Windows 98 Gaming PC-a013i-tbxz-r1/`
- Guest markers:
  - 3DMark99: `2421 3DMarks`, `5887 CPU 3DMarks`
  - `WL-05` locked totals unchanged:
    - quick `45db7b65`
    - normal `2520dd5e`
    - smc `b86f22a1`
- Host telemetry summary:
  - `unexpected_noimm_without_bmask=0`
  - new A-013i counters active:
    - `tbxz_rel14=55`
    - `tbxz_rel26=0`
    - `tbxz_total=55`
  - existing A-013 path mix remained stable:
    - `bcond_total=1320472` (near-flat vs `a013h-bcond-r2`)
    - BEQ/CBNZ absolute fallback counters stayed zero.
  - low-noise log policy held (`86box.log` ~`7.5M`).
- Operator decision:
  - skip extra double-click manual feel run for this slice.
  - accept `a013i-tbxz-r1` as valid checkpoint from telemetry + workload gates.
- Decision:
  - lock in A-013i as current branch-shaping state.
  - continue at fixed `266 MHz`.
  - next step is optimization-focused A-013j (raise `tbxz` coverage volume and/or reduce branch-path churn) rather than telemetry plumbing changes.

### Run order (fixed)
1. `WL-00-smoke-boot`
2. `WL-01-3dmark99-full`
3. `WL-02-q3-demo-four`
4. `WL-03-perf-run-bat`
5. `WL-04-3dmark2000-full`
6. `WL-05-win98-microstress` (targeted and optional unless slice requires it)

### Session controls (fixed)
- Use canonical K6-2 VM profile for pass/fail at active baseline `266 MHz` (`cpu_speed = 266666666`); run MMX secondary only when requested by slice plan.
- Start each workload from the same VM state (cold boot or same pre-captured checkpoint policy; do not mix methods inside a slice run).
- Keep renderer/path settings unchanged (`vid_renderer`, Voodoo options, dynarec on).
- Keep host conditions stable:
  - no unrelated heavy host workloads
  - same power mode and display setup within a slice run set
- Do not change guest drivers, in-guest graphics settings, or toolkit versions mid-slice.
- CPU-frequency progression rule:
  - keep `266 MHz` until heavy-scene checks are near-consistently `100%`.
  - only then bump frequency one step and re-run `WL-05` + Q3 + 3DMark gates.

### Repetition and timing rules
- Per workload/scenario:
  - `1` warm-up run (discarded)
  - `2` recorded runs minimum (`r01`, `r02`)
- If `r01` and `r02` disagree materially on correctness behavior, run `r03` and treat slice as unstable until explained.
- For wave 1, correctness/stability gates are primary; performance numbers are secondary and plan-only unless explicitly approved.

### Per-run capture minimum
- VM config snapshot file used for that run.
- Build/sign/JIT verification outputs from the same binary under test.
- Workload start marker and end marker timestamps.
- Exit status plus any crash/fatal signatures.
- Metadata file including:
  - `workload_id`, `workload_class`, `vm_profile`, `run_index`, `guest_start_mode`, `artifact_prefix`.

### Naming conventions for workload artifacts
- Prefix pattern:
  - `<slice-id>-<workload-id>-<vm-profile>-macos-arm64-<git_short>-rNN`
- `vm-profile` values:
  - `k6_2_233_canonical`
  - `pentium_tillamook_300_mmx_secondary`

### Slice gating with workloads
- `S-01`: `WL-00` and `WL-01` are required; `WL-02` optional; `WL-03` optional; `WL-04` optional.
- `S-02`: `WL-00` and `WL-02` required; `WL-01` optional; `WL-03` optional; `WL-04` optional.
- `S-03`: `WL-00` and `WL-03` required; `WL-01` optional; `WL-02` optional; `WL-04` optional.
- `A-013`: `WL-00` and `WL-02` required; `WL-01` optional; `WL-03` optional; `WL-04` optional.
- `WL-05` requirement policy:
  - optional for wave-1 slice pass/fail by default.
  - may be promoted to required for a slice if a bug only reproduces in microstress phases.

### WL-05 Test Intent Mapping (Explicit)
- `MRUNQ` (`--quick`):
  - fast regression gate; reduced iterations, same phase mix.
- `MRUN` (normal):
  - full-iteration stability gate for main execution path.
- `MRUNS` (`--smc`):
  - full-iteration plus SMC-touch phase for churn/SMC guardrail checks.
- Phase-to-slice mapping:
  - `mmx_touch` -> `S-01` targeted correctness confidence.
  - `imm_store` -> `S-02` direct imm-store fast-path validation.
  - `branch_helper` -> `A-013` control-flow/helper-dispatch sensitivity.
  - `smc_touch` -> `S-03` self-modifying/churn-policy guardrail sensitivity.

## Slice Plan: S-01

### Problem Statement
- `codegen_MMX_ENTER()` patches one branch target against `block->data` instead of `block_write_data`, risking wrong-target patching when block emission crosses mem-block boundaries.

### Why Prioritized Here
- Highest-confidence correctness bug with smallest write surface; improves safety before larger performance slices.

### Audit Linkage
- Findings/decisions: `F-005`, `D-004`
- Evidence anchors: `E-016`, `E-017`, `E-018`

### Exact Files/Modules Likely to Change
- `src/codegen_new/codegen_backend_arm64_uops.c`

### Expected Codegen/Behavior Change
- Branch patch site in `codegen_MMX_ENTER()` consistently uses the active write buffer (`block_write_data`) for offset fixup.
- No intended policy/perf behavior change; correctness/stability only.

### Dependencies and Prerequisites
- None; first slice by design.

### Implementation Outline
1. Replace stale patch target argument in `codegen_MMX_ENTER()`:
   - from `&block->data[block_pos]`
   - to `&block_write_data[block_pos]`
2. Audit nearby ARM64 patch sites in the same file/module for stale-buffer pattern.
3. Keep patch minimal and localized.

### Recommended Commit Boundaries
- Commit 1 (`S-01`): code fix plus any adjacent stale-site parity fix in same module (if discovered).

### Risk Level
- Low.

### Acceptance Criteria
- `codegen_MMX_ENTER()` uses `block_write_data` for branch offset patching.
- No equivalent stale-buffer patching remains in immediate ARM64 neighbor patch sites.
- Build/sign/JIT/smoke gates pass.

### Rollback Trigger
- Evidence that this path cannot span a mem-block boundary and fix changes behavior unexpectedly, or any new branch-target instability appears.

### Slice-Specific Validation Plan
- Validation goals:
  - correctness: ensure patched branch lands on intended post-call path.
  - stability: remove stale-buffer patch hazard.
  - performance/churn: not primary for this slice.
- Static verification:
  - inspect `codegen_MMX_ENTER()` in `src/codegen_new/codegen_backend_arm64_uops.c`.
  - scan for stale pattern: `host_arm64_branch_set_offset(..., &block->data[block_pos])`.
  - expected code shape: single pointer-site correction to `block_write_data`.
- Build/sign verification:
  - apply shared framework gates.
- Runtime smoke-check plan:
  - smoke launch only; include one MMX/FPU-touching quick workload if available in standard smoke pack.
- Targeted validation:
  - force/observe a split-block emission scenario in a controlled debug run plan and confirm no bad branch target symptom.
- Performance-check plan:
  - none beyond smoke (correctness-first slice).
- Rollback criteria:
  - any branch patching regression in MMX/FPU entry sequence.

## Slice Plan: S-02

### Problem Statement
- ARM64 backend lacks `CODEGEN_BACKEND_HAS_MOV_IMM` support because direct imm-store hooks are missing, preventing generic dead-end `MOV_IMM` fast path.
- `host_arm64_mov_imm()` currently misses bounded one-instruction opportunities (`MOVN`, logical-immediate via `ORR wzr,#imm`).

### Why Prioritized Here
- Broad, low-to-medium risk performance leverage with contained backend-only scope after `S-01` correctness fix.

### Audit Linkage
- Findings/decisions: `F-006`, `F-010`, `F-013`, `D-005`, `D-009`
- Evidence anchors: `E-019`, `E-021`, `E-029`, `E-032`, `E-043`, `E-044`, `E-045`

### Exact Files/Modules Likely to Change
- `src/codegen_new/codegen_backend_arm64.h`
- `src/codegen_new/codegen_backend_arm64_uops.c`
- `src/codegen_new/codegen_backend_arm64_ops.c`
- `src/codegen_new/codegen_backend_arm64_ops.h`
- Optional if helper sharing requires it: `src/codegen_new/codegen_backend_arm64_imm.c`

### Expected Codegen/Behavior Change
- Dead-end immediate writes for byte/word/dword paths can emit direct stores without register round-trips.
- 32-bit immediate materialization should use fewer instructions when `MOVN` or logical immediate encoding applies.
- No semantic change to address-range safety contracts.

### Dependencies and Prerequisites
- `S-01` complete.
- Keep range handling explicit and conservative in direct-write helpers.

### Implementation Outline
1. `S-02a` (`A-012`):
  - define `CODEGEN_BACKEND_HAS_MOV_IMM` for ARM64 backend.
  - implement:
    - `codegen_direct_write_8_imm`
    - `codegen_direct_write_16_imm`
    - `codegen_direct_write_32_imm`
    - `codegen_direct_write_32_imm_stack`
  - keep in-range checks aligned with existing direct write helpers.
2. `S-02b` (`A-011`) in same slice family:
  - teach `host_arm64_mov_imm()` to prefer:
    - one-instruction logical immediate (`ORR` from `WZR`) when encodable
    - `MOVN` when complement form is cheaper
    - fallback to existing `MOVZ`/`MOVK` path
3. Keep emitter changes bounded to avoid widening into generic addressing refactor.

### Recommended Commit Boundaries
- Commit 1 (`S-02a`): hook enablement + 4 direct imm-store helpers.
- Commit 2 (`S-02b`): bounded `host_arm64_mov_imm()` improvements.

### Risk Level
- Medium.

### Acceptance Criteria
- ARM64 backend now compiles with `CODEGEN_BACKEND_HAS_MOV_IMM` path active.
- All 4 direct imm-store hook functions exist and are used by the generic path.
- `host_arm64_mov_imm()` includes bounded fast-path selection without semantic drift.
- Build/sign/JIT/smoke gates pass.

### Rollback Trigger
- Any addressing/range regression, immediate truncation/misencode, or outsized complexity growth beyond bounded emitter work.

### Slice-Specific Validation Plan
- Validation goals:
  - correctness: preserve store destinations, widths, and immediate values.
  - stability: no new range-related fatal path in normal workloads.
  - performance/churn: reduce instruction count and avoid dead-end immediate overhead.
- Static verification:
  - inspect new hook implementations in `src/codegen_new/codegen_backend_arm64_uops.c`.
  - inspect `CODEGEN_BACKEND_HAS_MOV_IMM` enablement in `src/codegen_new/codegen_backend_arm64.h`.
  - inspect `host_arm64_mov_imm()` in `src/codegen_new/codegen_backend_arm64_ops.c`.
  - expected code shape:
    - direct imm-store functions map width to matching `STR*` emitter with explicit range checks.
    - `host_arm64_mov_imm()` has logical/MOVN short paths before fallback.
- Build/sign verification:
  - apply shared framework gates.
- Runtime smoke-check plan:
  - smoke launch set covering instruction decode, integer ops, and ordinary UI startup.
- Targeted validation:
  - validate direct imm-store hook coverage via symbol/source checks and targeted trace plan for dead-end immediate writes.
  - validate no addressing/range regressions with in-range boundary-focused test plan.
- Performance-check plan:
  - plan-only: compare instruction-count/block-shape deltas in constant-heavy helper paths; no benchmark execution in this session.
- Rollback criteria:
  - any mismatch in write width/target, or regressed immediate encoding correctness.

## Slice Plan: S-03

### Problem Statement
- Dirty-list escalation currently promotes blocks through `CODEBLOCK_BYTE_MASK` then `CODEBLOCK_NO_IMMEDIATES`, increasing recompile churn, codegen overhead, and macOS ARM64 JIT write-protect toggles.

### Why Prioritized Here
- High potential impact but policy-heavy and regression-prone; placed after backend-local fixes (`S-01`, `S-02`) to reduce confounding factors.

### Audit Linkage
- Findings/decisions: `F-008`, `F-009`, `D-007`, `D-008`
- Evidence anchors: `E-025`, `E-026`, `E-027`, `E-028`

### Exact Files/Modules Likely to Change
- `src/cpu/386_dynarec.c`
- `src/codegen_new/codegen_block.c`
- Optional if state semantics change: `src/codegen_new/codegen.h`

### Expected Codegen/Behavior Change
- More conservative escalation behavior on dirty-list recompiles.
- Fewer unnecessary transitions to `CODEBLOCK_NO_IMMEDIATES` on non-pathological churn patterns.
- Preserve self-modifying-code correctness.

### Dependencies and Prerequisites
- `S-01` and `S-02` complete.
- Guardrails for self-modifying-code behavior defined before policy activation.

### Implementation Outline
1. `S-03a`: add/confirm minimal state needed to support bounded escalation policy (without changing semantics yet).
2. `S-03b`: apply policy change in `386_dynarec.c` escalation block:
  - delay or gate transition to `CODEBLOCK_NO_IMMEDIATES`
  - keep `BYTE_MASK` handling conservative and explicit
3. Keep rollback path simple (feature-flag or isolated logic block for quick revert).

### Recommended Commit Boundaries
- Commit 1 (`S-03a`): state/observability support only.
- Commit 2 (`S-03b`): behavioral policy change.

### Risk Level
- Medium-high.

### Acceptance Criteria
- Escalation logic is explicit, reviewable, and narrower than baseline broad promotion.
- No self-modifying-code correctness regression in targeted smoke/test plan.
- Build/sign/JIT/smoke gates pass.

### Rollback Trigger
- Evidence of self-modifying-code regression, increased invalidation churn, or instability linked to the new escalation policy.

### Slice-Specific Validation Plan
- Validation goals:
  - correctness: preserve block validity and invalidation semantics.
  - stability: avoid regressions in dirty-page and recompile behavior.
  - performance/churn: reduce unnecessary escalation frequency.
- Static verification:
  - inspect escalation block in `src/cpu/386_dynarec.c` around `CODEBLOCK_IN_DIRTY_LIST`, `CODEBLOCK_BYTE_MASK`, and `CODEBLOCK_NO_IMMEDIATES`.
  - inspect `src/codegen_new/codegen_block.c` for interactions with recompile/free/dirty-list flow.
  - expected code shape: explicit gated promotion path rather than unconditional promotion-on-hit behavior.
- Build/sign verification:
  - apply shared framework gates.
- Runtime smoke-check plan:
  - smoke run with block generation/recompile activity expected; capture startup + short workload stability.
- Targeted validation:
  - dedicated SMC-sensitive validation plan with explicit abort/invalidation observation.
  - include guardrails: if mismatch, abort test and treat as rollback signal.
- Performance-check plan:
  - host-log churn observation (`recompile count`, `BYTE_MASK`/`NO_IMMEDIATES` transition frequency) using `DYNAREC_S03A_TRANSITION` and `DYNAREC_S03A_SUMMARY`.
  - parser-required fields for decision:
  - `ratio_promote_no_immediates_per_dirty_hit`
  - `transitions_action_no_immediates`
  - `transitions_action_defer_no_immediates`
  - `S03_TAGS` (`transition` vs `periodic` mix sanity)
  - `S03_RETRIES_HIST` (`before`/`after`) for escalation-threshold behavior evidence
- Rollback criteria:
  - any self-modifying-code correctness anomaly, churn increase, or instability tied to policy change.

## Slice Plan: A-013

### Problem Statement
- `host_arm64_call()` and `host_arm64_jump()` always use absolute target materialization plus `BLR/BR`, even when target is JIT-local and within relative branch range.

### Why Prioritized At This Position
- Remains part of first wave per audit ordering, but intentionally after `S-03` in the locked sequence.
- Strong low-risk/high-return optimization once prior slices are stable.

### Audit Linkage
- Findings/decisions: `F-011`, `D-010`, `D-014`, `D-019`
- Evidence anchors: `E-034`, `E-035`, `E-036`, `E-037`, `E-038`, `E-039`, `E-057`

### Exact Files/Modules Likely to Change
- `src/codegen_new/codegen_backend_arm64_ops.c`
- `src/codegen_new/codegen_backend_arm64_ops.h`
- `src/codegen_new/codegen_allocator.c`
- `src/codegen_new/codegen_allocator.h`

### Expected Codegen/Behavior Change
- JIT-local call/jump targets use relative `BL`/`B` when in range.
- External or out-of-range targets retain current `MOVX_IMM + BLR/BR` fallback.
- Net reduction in unnecessary absolute call/jump sequences for local helper/block targets.

### Dependencies and Prerequisites
- `S-01`, `S-02`, `S-03` complete and stable.
- Reliable local-target classification helper available (allocator arena containment/range check).

### Implementation Outline
1. `A-013a` (core): add bounded target-classification helper for JIT arena membership/range.
2. `A-013b` (core): update `host_arm64_call()` and `host_arm64_jump()`:
  - if target is JIT-local and in branch range -> emit direct `BL` / `B`
  - else preserve existing absolute fallback
3. `A-013c` (extended): add safe conditional-branch tightening for proven-local in-range branch destinations where callsite shape allows.
4. `A-013d` (extended): strengthen far-target guardrails (explicit range checks + optional veneer/thunk strategy if needed), while preserving absolute fallback path.
5. `A-013e` (extended observability): add low-overhead telemetry counters for branch-path selection ratios (`relative` vs `absolute`) for post-run review.

### Recommended Commit Boundaries
- Commit 1 (`A-013a+b`): target-classification helper + `host_arm64_call()` / `host_arm64_jump()` dual-path core behavior.
- Commit 2 (`A-013c`): conditional-branch tightening (if safe proof obtained in static review).
- Commit 3 (`A-013d+e`): far-target hardening + path telemetry + docs.

### Risk Level
- Low-medium.

### Acceptance Criteria
- Correct local/external target classification.
- Relative path used for JIT-local in-range targets; fallback path preserved for others.
- Build/sign/JIT/smoke gates pass.

### Rollback Trigger
- Any misclassified target causing bad dispatch/call target, or evidence that local-target assumptions are violated.

### Slice-Specific Validation Plan
- Validation goals:
  - correctness: never misroute control flow due to misclassification.
  - stability: fallback remains reliable under all target classes.
  - performance/churn: reduce absolute call/jump sequence density where safe.
- Static verification:
  - inspect `host_arm64_call()` and `host_arm64_jump()` in `src/codegen_new/codegen_backend_arm64_ops.c`.
  - inspect classification helper contract in allocator module.
  - expected code shape: explicit two-path decision (`relative local` vs `absolute fallback`).
- Build/sign verification:
  - apply shared framework gates.
- Runtime smoke-check plan:
  - smoke run including helper-heavy startup paths where call/jump emitters are active.
- Targeted validation:
  - classify representative call targets (JIT helper stubs, `codegen_exit_rout`, external C helpers) and validate path selection.
  - fallback correctness checks: force/observe non-local target path still emitting absolute sequence.
- Performance-check plan:
  - compare emitted sequence shape for local helper call/jump sites before/after.
  - for extended phase, include relative-path adoption ratio from telemetry (`A-013e`).
- Rollback criteria:
  - any control-flow correctness issue or ambiguous target-classification behavior.

## Immediate Post-Slice Verification Checklist
- After `S-01`:
  - stale-buffer patch site corrected
  - no nearby stale-buffer patch pattern remains
  - build/sign/JIT/smoke gates pass
- After `S-02`:
  - direct imm-store hooks are active and referenced by generic path
  - immediate materialization fast paths present and bounded
  - no new range failures in targeted checks
  - build/sign/JIT/smoke gates pass
- After `S-03`:
  - churn policy change is gated and reviewable
  - self-modifying-code guardrail checks pass
  - build/sign/JIT/smoke gates pass
- After `A-013`:
  - JIT-local classification works
  - relative local call/jump path emits where expected
  - fallback path still correct for non-local targets
  - build/sign/JIT/smoke gates pass

## Next Execution Session Handoff
- Start with telemetry capture run:
  - continue deep `A-013` with `A-013f` conditional-branch shaping on top of validated `A-013c/d/e`.
  - keep path-selection telemetry for `A-013` (`relative` vs `fallback`) and include new CBNZ counters.
  - keep ARM64-only behavior guardrails for new `A-013` code paths.
  - run:
  - `./scripts/dynarec/launch-vm-telemetry-run.sh a013f-cbnz-r3`
  - default launch keeps detailed A-013 trace disabled (`86BOX_A013_TRACE=0`) to avoid avoidable logging overhead during perf-sensitive checks.
  - for focused debug-only tracing, run:
  - `86BOX_A013_TRACE=1 ./scripts/dynarec/launch-vm-telemetry-run.sh a013f-cbnz-r3-trace`
  - after guest run, parse with:
  - `./scripts/dynarec/analyze-s03a-log.sh "<a013f-log>" "docs/perf-artifacts/arm64-dynarec/2026-04-21_21-09-06-Windows 98 Gaming PC-a013cde-r2/86box.log"`
  - and review `A013_PATH_SUMMARY` lines in host log for:
  - `call_rel`, `jump_rel`, `call_abs_nonlocal`, `jump_abs_nonlocal`, `call_abs_range`, `jump_abs_range`
  - `cbnz_rel19`, `cbnz_rel26`, `cbnz_abs_nonlocal`, `cbnz_abs_range`, `cbnz_total`
  - large-log retention rule:
  - after parser review is accepted, finalize with:
  - `./scripts/dynarec/finalize-s03-log.sh "<current-log>" "<optional-baseline-log>"`
  - for cleanup mode (safe delete after validation):
  - `./scripts/dynarec/finalize-s03-log.sh --delete-raw "<current-log>" "<optional-baseline-log>"`
  - safety guarantee: raw log deletion is blocked unless required summary fields are present.
- Keep changes scoped to one slice at a time and do not overlap slice commits.
- Do not reopen static investigation unless a slice hits a hard blocker.
- Do not run benchmarking without explicit approval; runtime plans in this doc remain plan-level guidance.
- Use this file plus the canonical audit as the execution contract for wave 1.
