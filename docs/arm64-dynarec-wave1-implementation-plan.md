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
- Current branch/head: `ndr-analysis` @ `ce8e485bd`

### Slice Status Table (Authoritative)
| Slice | Status | Notes |
| --- | --- | --- |
| `S-01` | Completed | `codegen_MMX_ENTER()` stale-buffer patch-site fix landed; WL-05 quick/normal/smc baselines validated. |
| `S-02a` | Completed | `A-012` direct imm-store hooks + `CODEGEN_BACKEND_HAS_MOV_IMM` landed. |
| `S-02b` | Pending (next) | `A-011` bounded `host_arm64_mov_imm()` improvements not started. |
| `S-02` overall | In progress | Considered complete only when `S-02a` + `S-02b` + S-02 validation gate are all green. |
| `S-03` | Not started | Must wait until full `S-02` closure. |
| `A-013` | Not started | Must wait until `S-03` closure per locked order. |

### Order Lock (Do Not Skip)
- Fixed order remains mandatory: `S-01` -> `S-02` -> `S-03` -> `A-013`.
- Current executable next step is only: `S-02b`, then full `S-02` validation closeout.
- No `S-03` or `A-013` implementation work may start before `S-02` is marked complete.

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

Original first patch (completed):
- `S-01`: fixed `codegen_MMX_ENTER()` branch patching to use `block_write_data`.

Recommended next patch:
- `S-02b`: implement bounded `host_arm64_mov_imm()` improvements (`MOVN` + logical-immediate path), then run `S-02` validation closeout.

Recommended next files to edit:
- `src/codegen_new/codegen_backend_arm64.h`
- `src/codegen_new/codegen_backend_arm64_uops.c`

Exact next commands for the next implementation session:
```bash
cd /Users/anthony/projects/code/86Box-voodoo-arm64
git rev-parse --abbrev-ref HEAD
git rev-parse --short HEAD
rg -n "S-02|A-012|CODEGEN_BACKEND_HAS_MOV_IMM|codegen_direct_write_8_imm|codegen_direct_write_16_imm|codegen_direct_write_32_imm|codegen_direct_write_32_imm_stack" docs/arm64-dynarec-wave1-implementation-plan.md src/codegen_new/codegen_backend_arm64.h src/codegen_new/codegen_backend_arm64_uops.c src/codegen_new/codegen_reg.c src/codegen_new/codegen_ir_defs.h
sed -n '1,220p' docs/arm64-dynarec-wave1-implementation-plan.md
sed -n '500,570p' src/codegen_new/codegen_reg.c
sed -n '900,940p' src/codegen_new/codegen_ir_defs.h
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

### Run order (fixed)
1. `WL-00-smoke-boot`
2. `WL-01-3dmark99-full`
3. `WL-02-q3-demo-four`
4. `WL-03-perf-run-bat`
5. `WL-04-3dmark2000-full`
6. `WL-05-win98-microstress` (targeted and optional unless slice requires it)

### Session controls (fixed)
- Use canonical K6-2 VM profile for pass/fail; run MMX secondary only when requested by slice plan.
- Start each workload from the same VM state (cold boot or same pre-captured checkpoint policy; do not mix methods inside a slice run).
- Keep renderer/path settings unchanged (`vid_renderer`, Voodoo options, dynarec on).
- Keep host conditions stable:
  - no unrelated heavy host workloads
  - same power mode and display setup within a slice run set
- Do not change guest drivers, in-guest graphics settings, or toolkit versions mid-slice.

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
  - plan-only churn observation (`recompile count`, `BYTE_MASK`/`NO_IMMEDIATES` transition frequency); no benchmark execution in this session.
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
1. Add a bounded target-classification helper for JIT arena membership/range.
2. Update `host_arm64_call()`:
  - if target is JIT-local and in branch range -> emit direct `BL`
  - else preserve existing absolute fallback
3. Update `host_arm64_jump()` similarly with `B`/fallback `BR`.
4. Keep fallback path untouched for correctness.

### Recommended Commit Boundaries
- Commit 1 (`A-013a`): target-classification helper + `host_arm64_call()` dual-path.
- Commit 2 (`A-013b`): `host_arm64_jump()` dual-path + cleanup/tests/docs.

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
  - plan-only: compare emitted sequence shape for local helper call/jump sites before/after.
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
- Start with `S-01` in `src/codegen_new/codegen_backend_arm64_uops.c`.
- Keep changes scoped to one slice at a time and do not overlap slice commits.
- Do not reopen static investigation unless a slice hits a hard blocker.
- Do not run benchmarking without explicit approval; runtime plans in this doc remain plan-level guidance.
- Use this file plus the canonical audit as the execution contract for wave 1.
