# ARM64 New Dynarec Investigation

## Resume Here
- Current objective: keep this file as the canonical static-audit record; active implementation sequencing now lives in [docs/arm64-dynarec-wave1-implementation-plan.md](./arm64-dynarec-wave1-implementation-plan.md).
- Exact next file/module: for active coding, start from `S-03` follow-on churn tuning (`S-03c`) in `src/cpu/386_dynarec.c` (ARM64-guarded).
- Next 3 concrete actions:
  1. Use `a013i-tbxz-r1` as current baseline and parse S-only churn telemetry with `./scripts/dynarec/analyze-s03a-log.sh --s-only ...`.
  2. Implement one ARM64-guarded `S-03c` policy adjustment (no new A-template work) and keep x86-64 behavior untouched.
  3. Re-run locked workload flow and gate on `WL-05` hash lock + `unexpected_noimm_without_bmask=0` + improved/no-worse `promote_no_immediates_per_dirty_hit`.
- Active blockers:
- None for source discovery; blocker handling is now execution-time only (regression gates and workload comparability).
- Keep telemetry low-noise by default; detailed A-path tracing remains opt-in (`86BOX_A013_TRACE=1`).

### Post-Investigation Status Update (2026-04-21)
- Historical note: this file remains canonical static-audit record and reflects investigation snapshot state.
- Live execution plan now exists at [docs/arm64-dynarec-wave1-implementation-plan.md](./arm64-dynarec-wave1-implementation-plan.md).
- Execution progress after investigation handoff:
  - `S-01` implemented (ARM64 `codegen_MMX_ENTER()` patch target corrected to `block_write_data`) at commit `d88433828`.
  - `S-02a` implemented (`A-012` direct imm-store hooks + `CODEGEN_BACKEND_HAS_MOV_IMM`) at commit `626abd938`.
  - `S-02b` implemented (`A-011` bounded `host_arm64_mov_imm()` with `MOVN` + logical-immediate path) at commit `1b2185e94`.
  - `S-02` validation gate recorded in wave-1 plan:
    - `WL-05` quick/normal/smc all `status=OK` with locked totals.
    - 3DMark99 full completed stable (no crash/hang/visual corruption), score captured for trend only.
    - Quake III demo four captured (`1260 frames, 37.4 seconds: 33.7 fps`).
  - `S-03a` and `S-03b` are now implemented/validated in downstream execution work.
  - `A-013a+b` and extended `A-013c/d/e` telemetry work have now passed regression gates in downstream execution runs.
  - Latest locked-run checkpoint: `2026-04-21_21-09-06-Windows 98 Gaming PC-a013cde-r2` with stable WL-05 totals and `A013_PATH` relative-adoption ratio `0.899442`.
  - A-013 telemetry logging policy is now tightened for lower run overhead: summary cadence reduced to every `1,048,576` path events and detailed per-path trace is opt-in via `86BOX_A013_TRACE=1`.
  - Active residual tightening slice (`A-013g`) expands ARM64 conditional-branch shaping:
    - `host_arm64_BEQ()` now has imm19 direct, imm26 bridge, and absolute fallback tiers.
    - `host_arm64_CBNZ()` relative eligibility widened from local-only to any imm-reachable target.
    - additive telemetry counters include `beq_rel19`, `beq_rel26`, `beq_abs_nonlocal`, `beq_abs_range`, `beq_total`.
  - `A-013g` validation criteria:
    - build/sign/JIT launch gates pass with default trace-off policy.
    - `WL-05` hashes unchanged if microstress workload is run.
    - no `S-03` safety counter regressions (`unexpected_noimm_without_bmask=0`).
    - stronger branch-path visibility via BEQ/CBNZ counters without fallback correctness loss.
  - `A-013g` rollback triggers:
    - any control-flow correctness issue/crash tied to conditional branch dispatch.
    - any `WL-05` hash mismatch.
    - any harmful `S-03` safety counter regression.
  - `A-013g` exact execution commands:
    - `./scripts/build-and-sign.sh`
    - `RUN_TAG=a013g-beq-r1 ./scripts/dynarec/prepare-vm-telemetry-run.sh`
    - `./scripts/dynarec/launch-vm-telemetry-run.sh a013g-beq-r1`
    - post-workload parse: `./scripts/dynarec/analyze-s03a-log.sh "<a013g-log>" "docs/perf-artifacts/arm64-dynarec/2026-04-21_21-09-06-Windows 98 Gaming PC-a013cde-r2/86box.log.gz"`
  - `A-013g` lock-in checkpoint:
    - `run_tag=a013g-beq-r1` -> `run_dir=docs/perf-artifacts/arm64-dynarec/2026-04-21_22-20-39-Windows 98 Gaming PC-a013g-beq-r1/`
    - `run_tag=a013g-beq-r2` -> `run_dir=docs/perf-artifacts/arm64-dynarec/2026-04-21_22-33-57-Windows 98 Gaming PC-a013g-beq-r2/`
    - both runs retained locked `WL-05` totals and `unexpected_noimm_without_bmask=0`.
    - `A013_PATH` shows active BEQ shaping with zero BEQ absolute fallbacks (`beq_abs_nonlocal=0`, `beq_abs_range=0`) and continued zero CBNZ absolute fallbacks.
    - operator-observed host behavior improved in real-time stress sections (3DMark texture phase and Q3 demo-four normal playback), with more frequent sustained `100%` emulation speed.
  - `A-013g` 266 MHz checkpoint:
    - `run_tag=a013g-266-r1` -> `run_dir=docs/perf-artifacts/arm64-dynarec/2026-04-21_22-53-19-Windows 98 Gaming PC-a013g-266-r1/`
    - VM frequency set to `266666666` (`k6_2 x4`) and remained stable.
    - guest markers improved further (`Q3 timedemo 35.6 fps`, `3DMark99 2507`, `CPU 5885`) with locked `WL-05` hashes.
    - operator-observed real-time behavior stayed materially better than upstream in the same heavy sections.
  - Next implementation slice promoted:
    - `A-013h` big conditional-branch sweep via shared `host_arm64_branch_set_offset()` patch path.
    - scope: lift all `B.cond` template patch sites to direct imm19 conditional branches when reachable; keep relative `B` fallback.
    - additive telemetry fields: `bcond_rel19`, `bcond_rel26`, `bcond_total`.
    - fixed CPU baseline during this slice: `266666666`.
  - `A-013h` first result checkpoint:
    - `run_tag=a013h-bcond-r1` -> `run_dir=docs/perf-artifacts/arm64-dynarec/2026-04-21_23-11-46-Windows 98 Gaming PC-a013h-bcond-r1/`
    - `WL-05` remained locked; no S-03 safety regressions (`unexpected_noimm_without_bmask=0`).
    - new counters confirmed active at scale: `bcond_rel19=772357`, `bcond_rel26=549442`, `bcond_total=1321799`.
    - operator-observed 100%-speed stability remained clearly stronger than upstream baseline.
    - decision: keep CPU at `266666666` for now; do not bump frequency yet.
  - `A-013h` guarded consolidation pass (no-launch checkpoint):
    - tightened ARM64 patch-site detection so `bcond` collapse/telemetry only triggers on the exact helper-emitted `B.cond +8` + trailing `B` template.
    - fallback branch patch remains unchanged; safety intent is stricter matching, not broader rewriting.
    - build/sign validated; telemetry prep command staged for next run tag `a013h-bcond-r2`.
  - `A-013h` guarded consolidation run result:
    - `run_tag=a013h-bcond-r2` -> `run_dir=docs/perf-artifacts/arm64-dynarec/2026-04-22_16-30-52-Windows 98 Gaming PC-a013h-bcond-r2/`
    - guest marker: `3DMark99 2450`, `CPU 5893`; `WL-05` hashes stayed locked.
    - safety marker held (`unexpected_noimm_without_bmask=0`).
    - `bcond_total` stayed effectively flat vs `r1` (`1321799 -> 1319651`) with only small rel19/rel26 redistribution.
    - BEQ/CBNZ absolute fallbacks remained zero.
    - operator reported telemetry-wrapper run felt a bit slower, but normal double-click launch felt good in real-world use.
    - decision: keep guard-hardening change; continue fixed `266666666` baseline.
  - `A-013i` promoted next:
    - extend guarded patch-path shaping to `TBZ/TBNZ + B` templates in ARM64 backend.
    - add `imm14` allocator-range helper and additive telemetry counters `tbxz_rel14`, `tbxz_rel26`, `tbxz_total`.
    - parser extended to parse/print/delta `tbxz_*` fields without changing low-noise defaults.
  - `A-013i` result checkpoint:
    - `run_tag=a013i-tbxz-r1` -> `run_dir=docs/perf-artifacts/arm64-dynarec/2026-04-22_16-55-55-Windows 98 Gaming PC-a013i-tbxz-r1/`
    - guest marker: `3DMark99 2421`, `CPU 5887`; `WL-05` hashes stayed locked.
    - safety marker held (`unexpected_noimm_without_bmask=0`).
    - new telemetry fields confirmed active: `tbxz_rel14=55`, `tbxz_rel26=0`, `tbxz_total=55`.
    - existing A-013 paths remained stable; BEQ/CBNZ absolute fallback counters stayed zero.
    - operator decision: skip extra manual double-click run and accept this checkpoint.
  - `A-*` closeout decision:
    - `A-013` lane is now treated as complete/frozen for this wave after `A-013i`.
    - no further template expansion planned unless a correctness regression reopens the lane.
  - Tooling follow-through now included in this same checkpoint:
    - launcher hardened with retry/fallback launch paths (`open -a` retries plus direct-binary fallback).
    - parser fixed to read `A013_PATH total=` correctly when `cbnz_total=`/`beq_total=` fields are present.
  - Current next execution slice shifts back to `S-*` lane work: post-wave churn optimization at fixed `266 MHz`.
  - Churn telemetry remains active for rollback guardrails and becomes the active implementation focus.
  - `S-03c` kickoff (no-launch prep) is now in progress:
    - ARM64-only retry-state decay added so stale dirty-list retry debt is cleared after stable non-dirty-list execution.
    - new `DYNAREC_S03A_SUMMARY` field `retry_resets=` added for observability.
    - parser updated to print/delta `retry_resets` while keeping backward compatibility on old logs.

## Scope
- Campaign start: 2026-04-20 22:54:04 EDT
- Current continuation checkpoint: 2026-04-21 17:18:28 EDT
- Repository: `/Users/anthony/projects/code/86Box-voodoo-arm64`
- Branch: `ndr-analysis`
- HEAD: `2e725bf5d65de5e6778f722e645200ffdb4029c1`
- Note: current HEAD moved forward through docs-only investigation commits, an upstream sync merge, and the focused post-sync delta review in `U-012`; the implementation ordering remains unchanged, but the JIT-wrapper hazard and missing ARM64 `MOV` truncation cases are now fixed at baseline.
- Mission: investigate ARM64-specific issues in the new dynarec that may cause `performance loss`, `incorrect behavior/instability`, or `avoidable fallback/churn`, and end with a prioritized improvement plan backed by evidence.

## Goals
- Build a reliable ARM64 new-dynarec module map before deep analysis.
- Analyze one ARM64-relevant file/module at a time and capture evidence, risks, findings, and bounded fix proposals.
- Produce a decision-ready backlog with validation and rollback criteria.

## Non-Goals
- No source-code, build-script, config, test, or runtime-script changes in this session.
- No build or runtime execution in this session.
- No history rewrite; analysis starts from current HEAD and moves forward only.
- Keep blocked opcode families unchanged unless explicitly approved: `0F AF`, `6B`, `0F BA memory forms`.

## Constraints
- Docs-only scope: update only `docs/arm64-dynarec-investigation.md`.
- Prefer Intel SDM / AMD64 APM as semantic authority for x86 behavior claims.
- Internal docs are supporting context only.
- For any future proposed runtime test, document the test plan only; do not execute it here.
- For any referenced historical run, explicitly note whether logfile and metadata were written to disk.

## Working Assumptions
- A-ASSUME-001: `src/codegen_new/codegen_backend_arm64*.{c,h}` is the primary ARM64 new-dynarec backend surface, while `src/cpu/386_dynarec.c` is the integration boundary that decides when compiled blocks execute. Status: active.
- A-ASSUME-002: The four ARM64 backend translation units selected in `src/codegen_new/CMakeLists.txt` are sufficient to define the first-pass investigation order. Status: active.

## ARM64 New-Dynarec File Map
| Module | Files | Role in ARM64 investigation | Initial evidence |
| --- | --- | --- | --- |
| Build selection | `src/codegen_new/CMakeLists.txt` | Selects the ARM64 backend translation units compiled for `ARCH STREQUAL "arm64"`; establishes canonical scope for backend analysis. | `E-001` |
| Backend scaffolding / helper stubs | `src/codegen_new/codegen_backend_arm64.c` | Owns global helper entrypoints, host register pools, load/store helper stub generation, and likely block lifecycle code. | `E-002`, `E-003` |
| Backend ABI / constants | `src/codegen_new/codegen_backend_arm64.h`, `src/codegen_new/codegen_backend_arm64_defs.h` | Defines block/hash sizing, host register assignments, argument/temp conventions, and exported helper hooks. | `E-004`, `E-005` |
| Raw instruction emitters | `src/codegen_new/codegen_backend_arm64_ops.c`, `src/codegen_new/codegen_backend_arm64_ops.h` | Encodes AArch64 scalar/vector ops, branches, loads/stores, and literal/branch helpers used by higher layers. | `E-006` |
| Uop lowering / semantic bridge | `src/codegen_new/codegen_backend_arm64_uops.c` | Likely largest semantic lowering unit from IR/uops to ARM64; expected hotspot for correctness gaps, fallback, and flag mapping costs. | `E-007` |
| Immediate materialization | `src/codegen_new/codegen_backend_arm64_imm.c` | Encodes immediate selection/materialization; candidate source of instruction bloat or suboptimal constant handling. | `E-008` |
| Executable allocator / branch envelope | `src/codegen_new/codegen_allocator.c`, `src/codegen_new/codegen_allocator.h` | Proves generated ARM64 blocks and helper stubs live inside a single contiguous executable arena, which sharply constrains which relative branches and calls should always be reachable. | `E-013`, `E-035`, `E-036` |
| Reg writeback / imm-store contract | `src/codegen_new/codegen_reg.c`, `src/codegen_new/codegen_ir_defs.h` | Defines the generic direct-immediate-store hooks and shows why the ARM64 backend currently cannot opt into the same dead-end `MOV_IMM` fast path as x86-64. | `E-021`, `E-043`, `E-044` |
| Dynarec integration boundary | `src/cpu/386_dynarec.c`, `src/cpu/386_dynarec_ops.c` | Governs block lookup/validation/end conditions and can reveal avoidable recompile churn or cache invalidation behavior observable on ARM64 as backend symptoms. | `E-009` |
| Shared control-flow / flag-helper producers and wrapper layer | `src/codegen_new/codegen_ops_branch.c`, `src/codegen_new/codegen_ops_jump.c`, `src/codegen_new/codegen_ops_helpers.c`, `src/codegen_new/codegen_ops_helpers.h`, `src/codegen_new/codegen_ops_shift.c`, `src/codegen_new/codegen_ops_arith.c`, `src/codegen_new/codegen_ops_misc.c`, `src/codegen_new/codegen_ops_stack.c`, `src/codegen_new/codegen_ops_jit_wrappers.h`, `src/codegen_new/codegen.c` | Determines how often the shared frontend emits `codegen_exit_rout` jumps, helper-result calls, instruction callbacks, far-control-transfer helpers, and the wrapper layer that now makes address-taken flag helpers safe JIT call targets on ARM64. | `E-052`, `E-053`, `E-054`, `E-056`, `E-057`, `E-058`, `E-059`, `E-060`, `E-061`, `E-062`, `E-063`, `E-064`, `E-065`, `E-066`, `E-071` |

## Hypothesis Register
| Hypothesis ID | Status | Statement | Initial basis | Planned tie-break / next step |
| --- | --- | --- | --- | --- |
| H-001 | parked | ARM64 helper stub generation may be forcing too many slow-path calls on misaligned or lookup-miss memory accesses, causing measurable performance loss beyond expected x86 semantics. | `codegen_backend_arm64.c` emits dedicated load/store stubs with explicit `readlookup2`/`writelookup2` probes and helper-call fallback branches. | `codegen_backend_x86-64.c` uses the same misalignment-triggered helper pattern, so this is not an ARM64-specific first-slice explanation without stronger evidence. Revisit only if later ARM64-only behavior shows materially higher fallback frequency. |
| H-002 | parked | Raw ARM64 host register pool size may be a primary driver of backend slowdown. | Only 10 integer callee-saved registers and 8 FP registers are exposed as allocatable host pools, with `X29` pinned as `REG_CPUSTATE`. | Parked after the defs comparison: x86-64 exposes even fewer integer host regs, so any remaining ARM64 pressure is more likely second-order from helper/encoding gaps than from raw register count alone. Re-open only if future spill-aware telemetry contradicts this. |
| H-003 | confirmed | ARM64 immediate materialization inflates instruction count in common paths. | Dedicated immediate file exists and exported `host_arm64_find_imm()` suggests special-case search/materialization logic. | Confirmed in `U-004` / `U-005`: the primary gap is not the logical-immediate table itself, but that `host_arm64_mov_imm()` never uses logical-immediate or `MOVN` forms and the backend still lacks direct imm-store hooks. Track via `A-012` and `A-011`. |
| H-004 | open | Some user-visible ARM64 instability may originate at the block validation/recompile boundary rather than in instruction semantics, appearing as backend faults or random slowdowns. | `386_dynarec.c` contains extensive block validation, dirty-page, and block-end logic that can cause recompilation or fallback churn. | Static source audit is complete; only runtime telemetry on dirty-list hit rate, `BYTE_MASK` reuse, and recompile frequency should move this further. |
| H-005 | rejected | `codegen_allocator_clean_blocks()` in the ARM64 backend might be freeing or invalidating the shared helper block immediately after initialization, causing latent corruption. | ARM64 `codegen_backend_init()` calls `codegen_allocator_clean_blocks(block->head_mem_block)` while x86-64 init does not. | Rejected after allocator inspection: the function only performs `__clear_cache()` on ARM64 and does not free memory. |
| H-006 | open | ARM64 FP-to-int conversions may pay avoidable overhead because rounding is implemented as out-of-line helper calls plus an indirect jump-table dispatch. | `build_fp_round_routine()` builds shared rounding helpers, and ARM64 uops call them for `MOV_INT_DOUBLE` / `MOV_INT_DOUBLE_64`. | Static source audit is complete; keep open only for future FP-heavy workload traces or directed validation plans that record logfile/metadata destinations before execution. |
| H-007 | confirmed | ARM64 direct state/stack access helpers may become a latent instability source because they `fatal()` on immediate-range overflow instead of falling back to a generic address-materialization path. | `codegen_direct_read_*` / `codegen_direct_write_*` helpers encode fixed-range loads/stores from `REG_CPUSTATE` or `SP` and hard-fail when offsets exceed the encoding window. | Confirmed and widened in `U-005` / `U-006`: most helpers still hard-fail, while the F64 stack helpers currently skip validation entirely. Track through `A-009` and `A-015`. |
| H-008 | confirmed | ARM64 helper calls and direct jumps are paying avoidable overhead because the backend always materializes absolute destinations even when JIT-local targets are within AArch64 branch range. | `host_arm64_call()` / `host_arm64_jump()` use `MOVX_IMM` plus `BLR/BR`. | Confirmed in `U-005` / `U-007`: the JIT allocator reserves a contiguous ~120MB arena and the local helper stubs live inside it, so JIT-local targets are always within `B/BL` range. Track through `A-013`. |
| H-009 | open | ARM64 abort/control-flow checks may pay chronic overhead because the single global `codegen_exit_rout` and long-form patchable branch templates force extra branches once blocks drift beyond the 19-bit `B.cond` / `CBNZ` range. | `host_arm64_CBNZ()` and `host_arm64_Bxx_()` already synthesize long-form veneers. | Static source audit is complete; only future block-distance histograms and exit-frequency traces can determine whether local exit veneers outrank other performance slices. Track through `A-014`. |
| H-010 | confirmed | ARM64 backend interface drift in `codegen_backend_arm64.h` is masking missing emitter capabilities and raising implementation risk for the next slices. | Stale `STRB_IMM_W` declaration and unimplemented `LDR_LITERAL_*` prototypes in the public ARM64 header. | Confirmed as a maintenance / design-risk issue in `U-007`. Keep it as a low-priority cleanup via `A-016`; do not prioritize it ahead of user-visible performance/correctness slices. |
| H-011 | confirmed | Shared IR control-flow producers amplify the ARM64 helper-call and branch-range gaps enough that branch-heavy frontend work should stay above niche backend cleanup ideas. | `codegen_ops_branch.c` emits dense `uop_CALL_FUNC_RESULT`, `uop_CMP_IMM_J*`, and `uop_JMP(codegen_exit_rout)` sequences, with additional exit guards in adjacent producer modules. | Confirmed in `U-008` / `U-009`. The remaining tie-break is no longer whether this exists, but how to rank `A-013`, `A-014`, `A-018`, and `A-019` once future workload traces exist. |

## Running Prioritized Backlog
| Priority | Action ID | Status | Candidate improvement | Expected win | Risk | Validation plan | Rollback trigger |
| --- | --- | --- | --- | --- | --- | --- | --- |
| P0 | A-001 | completed | Complete evidence-backed analysis of `codegen_backend_arm64.c` before proposing implementation slices. | Established that ARM64-specific cache maintenance and FP-round helper structure deserve follow-up, while generic misalignment fallback is not yet ARM64-specific. | Medium: scaffolding findings still need confirmation from uop usage. | Static audit completed for this module. | Re-open only if later evidence contradicts the module summary. |
| P0 | A-002 | completed | Analyze `codegen_backend_arm64_uops.c` hotspot paths for ARM64-specific helper traffic, correctness traps, and broad performance gaps. | Exposed one concrete correctness bug, one broad optimization gap, and one latent robustness concern; ruled memory-helper traffic as mostly backend-generic. | Medium: full semantic audit of every uop remains out of scope for this session. | Static hotspot audit completed with x86-64 comparisons. | Re-open if later workload evidence points at a different ARM64-only uop family. |
| P1 | A-003 | completed | Analyze `codegen_backend_arm64_imm.c` for immediate encoding inefficiencies and constant-materialization churn. | Confirmed that the logical-immediate table already covers common masks, shifting attention to missing emitter fast paths rather than the table itself. | Low-medium. | Static inspection completed with call-site follow-through into `arm64_ops.c` and `arm64_uops.c`. | Re-open only if later profiling shows the table lookup itself is a measurable compile-time bottleneck. |
| P0 | A-004 | completed | Analyze `src/cpu/386_dynarec.c` block validation / dirty-page flow for recompile churn that could magnify ARM64-only cache-flush cost. | Confirmed that dirty-list recompiles escalate blocks into shorter `BYTE_MASK` mode and then `NO_IMMEDIATES`, directly compounding ARM64 compile-time overhead. | Medium: any heuristic changes risk self-modifying-code regressions. | Static audit completed; future targeted churn test plan only. | Re-open if later evidence shows churn is dominated elsewhere. |
| P1 | A-005 | completed | Analyze raw emitters in `codegen_backend_arm64_ops.c` / `.h` for branch range, literal load, and load/store helper patterns that create avoidable bloat or latent correctness risk. | Surfaced one broad helper-call gap, one broad control-flow-veneer gap, one silent stack-offset correctness risk, and one low-priority header drift issue. | Medium. | Static inspection completed with allocator and x86-64 comparisons. | Re-open if later evidence shows these emitter costs are not exercised by real IR. |
| P1 | A-006 | pending | If uops confirm heavy use, design a bounded ARM64 optimization for FP-to-int rounding that avoids helper-call indirection where safe. | Medium: could reduce helper traffic in x87/MMX-heavy paths. | Medium-high: semantic drift risk around rounding, overflow, and tag handling. | Static semantic audit first; future targeted test plan only with explicit logfile/metadata requirements. | Roll back if manual cross-checks or future tests show rounding mismatches. |
| P0 | A-007 | pending | Fix `codegen_MMX_ENTER()` branch patching to use `block_write_data` consistently and audit the ARM64 backend for any other stale-buffer patch sites. | High: bounded correctness fix for a split-block patching bug that can cause instability. | Low. | Static audit plus future targeted split-block test plan only. | Roll back if branch-patch audit proves `block_write_data` can never differ in that path. |
| P1 | A-008 | pending | Add ARM64 support for the `CODEGEN_BACKEND_HAS_MOV_IMM` fast path by implementing direct immediate stores where safe. | Medium to high: reduces register pressure and instruction count for dead-end immediate writes. | Medium: needs careful handling of address-range limits and stack-vs-`cpu_state` destinations. | Static design now; future compile/runtime validation plan only. | Roll back if implementation increases code size or introduces range-related fragility without measurable benefit. |
| P2 | A-009 | pending | Add a generic ARM64 fallback path for direct state/stack reads and writes that exceed immediate encoding windows instead of `fatal()`ing. | Low to medium: robustness improvement and future-proofing. | Low-medium. | Static call-site audit first; future targeted layout-stress test plan only. | Defer or roll back if current layout margins remain large and no realistic overflow path exists. |
| P1 | A-010 | pending | Revisit dirty-list escalation (`BYTE_MASK` then `NO_IMMEDIATES`) and/or its trigger policy now that ARM64 recompiles pay cache flush and macOS JIT protection costs. | High on churn-heavy workloads: could reduce repeated short-block recompiles and immediate reload overhead. | Medium-high: heuristic changes can regress genuine self-modifying-code cases. | Static design now; future targeted SMC/churn validation plan only with logfile/metadata capture. | Roll back if invalidation frequency or correctness regressions rise in SMC-heavy workloads. |
| P1 | A-011 | pending | Teach `host_arm64_mov_imm()` to exploit one-instruction logical-immediate moves (`ORR wzr,#imm`) and `MOVN` before falling back to `MOVZ` / `MOVK`. | Medium: cuts instruction count and temp pressure in constant-heavy blocks and helper setup. | Low. | Static opcode-diff audit now; future block-size comparison on constant-heavy traces with logfile/metadata capture. | Roll back if selection complexity outweighs code-size wins or exposes partial-register semantic drift. |
| P1 | A-012 | pending | Implement ARM64 direct imm-store hooks (`codegen_direct_write_8_imm`, `_16_imm`, `_32_imm`, `_32_imm_stack`) and then enable `CODEGEN_BACKEND_HAS_MOV_IMM`. | Medium-high: broad dead-end immediate-store win across generic IR. | Medium. | Static design now; future compile/runtime validation plan should compare block size and helper-count deltas while recording logfile/metadata destinations. | Roll back if safe range handling requires a much larger addressing refactor than expected. |
| P1 | A-013 | pending | Add JIT-local relative `BL` / `B` fast paths to `host_arm64_call()` / `host_arm64_jump()`, falling back to `MOVX_IMM + BLR/BR` only for external or out-of-range targets. | High on helper-heavy workloads: avoids repeated 64-bit target materialization for JIT-local helpers and block jumps. | Low-medium. | Static target-class audit now; future helper-heavy block-size / instruction-count comparison with logfile/metadata capture. | Roll back if any supposedly local target can escape the contiguous code arena or if dual-path dispatch complicates external helpers too much. |
| P2 | A-014 | pending | Investigate local exit veneers or per-region exit/helper stubs so `CBNZ` and patchable conditionals stay within the 19-bit range more often. | Medium-high on far-from-block0, helper-heavy, or branch-heavy workloads. | Medium-high: control-flow plumbing is more invasive than the relative-call slice. | Future plan should gather block-distance histograms and branch-frequency evidence first, with explicit logfile/metadata destinations. | Roll back if most hot blocks stay within 1MB or if extra veneers increase I-cache pressure without measurable branch-count reduction. |
| P0 | A-015 | pending | Add validation and fallback for `host_arm64_LDR_IMM_F64` / `STR_IMM_F64` stack users (`codegen_direct_{read,write}_{64,double}_stack`) so large stack offsets cannot silently misencode. | High correctness / robustness upside; performance-neutral. | Low. | Static stack-offset audit now; future synthetic large-stack test plan only, with logfile/metadata destinations recorded before execution. | Roll back if a full audit proves all stack offsets are permanently bounded well inside the 12-bit scaled window. |
| P2 | A-016 | pending | Normalize `codegen_backend_arm64.h` to match the actual ARM64 ops surface (fix `STRB_IMM_W` drift, remove or implement `LDR_LITERAL_*`). | Low direct user-visible win, but it reduces implementation risk for the next emitter slices. | Low. | Static header-to-implementation diff now; future build-only validation plan if a cleanup patch is prepared later. | Roll back if the cleanup becomes entangled with higher-value emitter work and threatens schedule or focus. |
| P2 | A-017 | pending | Add generic temp-register fallbacks for `ADDX_IMM` and similar immediate encoders that currently assume large offsets can never occur. | Low-medium robustness upside and future-proofing. | Low-medium. | Static call-site audit now; future targeted large-offset IR plan only, with logfile/metadata destinations predeclared. | Roll back if current callers remain provably within range and the fallback path adds complexity with no plausible trigger. |
| P2 | A-018 | pending | Root-cause the disabled `JE/JNE` unroll path and, if safe, restore a bounded direct path so common equality branches do not always funnel through `codegen_exit_rout`. | Medium-high on branch-heavy code, especially equality-test loops and recompiled blocks. | High: the existing comment already documents a wrong-turn correctness bug in shared frontend control flow. | Static root-cause audit first; future branch-heavy directed validation plan only, with logfile/metadata destinations recorded before execution. | Roll back if re-enabling requires invasive shared IR changes or shows any path-selection mismatches. |
| P2 | A-019 | pending | Investigate reducing `uop_CALL_FUNC_RESULT` fallback density in `codegen_ops_branch.c` by reusing already-materialized flag state or tightening `FLAGS_UNKNOWN` rebuild paths before helper calls. | Medium on flag-heavy branch workloads; could cut ARM64 helper-call traffic beyond `A-013`. | High: shared flag semantics are subtle and mistakes risk silent control-flow bugs. | Static flag-provenance audit now; future branch-matrix validation plan only with logfile/metadata capture. | Roll back if the optimization duplicates logic already encoded in uops or risks semantic drift across corner cases. |
| P2 | A-020 | pending | Investigate bounded fast paths for shared `flags_rebuild` / `get_cf()` users in `codegen_ops_shift.c` and `codegen_ops_arith.c` so helper calls can be skipped when flag provenance is already materialized. | Medium on rotate/carry-heavy integer code; broader if helper-call cost still dominates after `A-013`. | High: carry/overflow semantics are delicate across ADC/SBB/rotate families. | Static flag-provenance audit now; future arithmetic/shift matrix validation plan only with logfile/metadata capture. | Roll back if the fast path duplicates existing flag machinery or risks silent semantic drift. |
| P2 | A-021 | pending | If post-`A-013` traces still show far-control-transfer or flag-serialization hotness, investigate bounded cleanup of `loadcs` / `loadcsjmp` and `flags_rebuild_c` / `flags_rebuild` tail helpers in `codegen_ops_misc.c`, `codegen_ops_jump.c`, and `codegen_ops_stack.c`. | Low-medium on niche kernel/OS/runtime code; mostly a tail optimization after cheaper ARM64 helper dispatch exists. | High: segment-transfer and flag-serialization semantics are delicate, and general workloads may never need it. | Future directed plan only: far `JMP/CALL/RET/IRET`, `PUSHF/PUSHFD`, `STC/CLC/CMC`, with logfile/metadata destinations declared before execution. | Roll back or defer if traces show those opcodes are rare or if `A-013` already removes most visible cost. |
| P2 | A-022 | pending | If repeatable same-process benchmarking becomes important, move the round-robin eviction probe into resettable state and clear it in `codegen_init()` / `codegen_reset()`. | Low runtime win, medium measurement-hygiene gain because eviction order becomes reproducible across dynarec resets as well as within one process. | Low: bounded allocator-local cleanup. | Future directed plan only: repeated same-process reset/recompile cycles with explicit logfile/metadata destinations, comparing eviction-order stability before vs after. | Roll back or defer if dirty/purgable reuse dominates enough that `codegen_delete_random_block()` rarely executes or if process-scoped probe continuity proves intentional. |

## Findings Summary
- F-001: The ARM64 new dynarec backend surface compiled for `ARCH STREQUAL "arm64"` is currently constrained to four translation units (`codegen_backend_arm64.c`, `_ops.c`, `_uops.c`, `_imm.c`), with additional ARM64 ABI/register definitions in headers and the runtime block-management boundary in `386_dynarec.c`. Confidence: high. Impact estimate: high for investigation prioritization because it bounds the code surface that can directly cause ARM64 backend-specific regressions. Evidence: `E-001`, `E-002`, `E-004`, `E-005`, `E-009`.
- F-002: ARM64 block finalization pays an architecture-specific instruction-cache maintenance tax: `codegen_backend_arm64.c` calls `codegen_allocator_clean_blocks()` after helper initialization and after each compiled block epilogue, and the allocator implementation shows that ARM64 path walks the generated mem-block chain and invokes `__clear_cache()` over each block. This is required for correctness, not a memory-lifetime bug, but it can amplify user-visible slowdown whenever block churn is high. Confidence: high. Impact estimate: medium-high for compile/recompile-heavy workloads. Evidence: `E-012`, `E-013`.
- F-003: ARM64 floating-point-to-integer rounding currently relies on shared out-of-line helper stubs (`codegen_fp_round`, `codegen_fp_round_quad`) selected through a runtime jump table, and ARM64 uops call those helpers in conversion paths where x86-64 emits direct conversion instructions bracketed by MXCSR loads. This creates an ARM64-specific extra call/dispatch cost worth validating in FP-heavy workloads. Confidence: medium. Impact estimate: medium until call frequency is measured from uop usage. Evidence: `E-010`, `E-011`, `E-015`.
- F-004: The load/store misalignment-triggered helper fallback observed in `codegen_backend_arm64.c` is mirrored by the x86-64 backend, so it should not be treated as an ARM64-specific first-slice explanation without stronger evidence. Confidence: high. Impact estimate: medium for prioritization because it removes a tempting but currently non-specific lead. Evidence: `E-003`, `E-014`.
- F-005: `codegen_MMX_ENTER()` in the ARM64 uops backend patches its CR0-guarded branch target with `&block->data[block_pos]` instead of `&block_write_data[block_pos]`, unlike `codegen_FP_ENTER()` and the x86-64 equivalent. Because ARM64 code emission can spill into chained mem blocks and update `block_write_data`, this is a real split-block correctness/instability risk: the patched branch can point at the wrong fragment when `MMX_ENTER` straddles a block boundary. Confidence: high. Impact estimate: high despite likely low trigger frequency, because failure mode is wrong control flow. Evidence: `E-016`, `E-017`, `E-018`.
- F-006: ARM64 currently misses the x86-64 direct `MOV_IMM`-to-memory optimization. `codegen_ir.c` enables that fast path only when `CODEGEN_BACKEND_HAS_MOV_IMM` is defined; x86-64 opts in, while ARM64 does not, so ARM64 forgoes a broad dead-end immediate-store optimization and likely pays extra register traffic and instructions. Confidence: high. Impact estimate: medium-high because the optimization is generic and low-level rather than workload-specific. Evidence: `E-019`, `E-020`, `E-021`, `E-004`.
- F-007: ARM64 direct state/stack access helpers rely on fixed immediate ranges and `fatal()` when offsets exceed those windows. Current call-site audit shows they are only used for `cpu_state` fields and small stack slots today, so this is a latent robustness issue rather than an active bug, but ARM64 lacks the generic fallback flexibility that x86-64 gets from absolute addressing. Confidence: medium. Impact estimate: low-medium, mainly as future-proofing against layout growth or new call sites. Evidence: `E-022`, `E-023`, `E-024`.
- F-008: Dirty-list recompiles are an explicit churn amplifier. When a valid block is still in the dirty list, `exec386_dynarec_dyn()` first promotes it to `CODEBLOCK_BYTE_MASK`, then on the next dirty-list hit adds `CODEBLOCK_NO_IMMEDIATES`; in `BYTE_MASK` mode the maximum block size drops from `1000` guest bytes to roughly `40-103` bytes depending on alignment, and `NO_IMMEDIATES` pushes decode/translation down slower immediate-from-RAM paths. On ARM64 this compounds both cache flush cost (`F-002`) and the missing `MOV_IMM` fast path (`F-006`). Confidence: high. Impact estimate: high on churn-heavy workloads. Evidence: `E-025`, `E-026`, `E-027`, `E-028`.
- F-009: On macOS AArch64, each block recompile also toggles `pthread_jit_write_protect_np(0/1)` around code generation. This is not a bug by itself, but it makes churn reduction even more valuable on the user’s current host class because ARM64 compile costs are not limited to cache flushing. Confidence: high for macOS-specific impact. Impact estimate: medium on Apple ARM64; none on non-Apple hosts. Evidence: `E-026`.
- F-010: `host_arm64_find_imm()` already knows common logical-immediate masks such as `0x01010101`, but `host_arm64_mov_imm()` never consults that table and has no `MOVN` path; it only emits `MOVZ` / `MOVK`. This turns one-instruction constants into two-instruction sequences in common sites such as `codegen_MMX_ENTER()` and general `UOP_MOV_IMM`, while x86-64 still uses a single `MOV reg, imm32`. Severity: medium. Confidence: high. Expected user-visible impact: medium code-size growth and extra backend work in constant-heavy blocks, especially MMX/FPU/helper-setup paths. Likely reproduction conditions: blocks with repeated fixed masks (`0x01010101`, dense negative masks, status words) or many `UOP_MOV_IMM` values. Candidate implementation files: `src/codegen_new/codegen_backend_arm64_ops.c`, `src/codegen_new/codegen_backend_arm64_imm.c`, `src/codegen_new/codegen_backend_arm64_uops.c`. Rollback trigger: if smarter selection raises compile-time complexity or perturbs partial-register semantics without reducing block size. Evidence: `E-029`, `E-030`, `E-031`, `E-032`, `E-033`.
- F-011: ARM64 JIT-local calls and direct jumps always materialize the destination into `X16` and use `BLR/BR`, whereas x86-64 first tries a relative `CALL/JMP` and falls back only when needed. The executable allocator reserves a contiguous ~120MB arena and the local helper stubs plus `codegen_exit_rout` all live inside that same arena, so JIT-local ARM64 targets are always within AArch64's 26-bit `B/BL` reach but the backend currently never exploits it. Severity: high. Confidence: high. Expected user-visible impact: high on helper-heavy workloads because every memory helper, FP-round helper, and JIT-local jump pays extra instructions. Likely reproduction conditions: blocks that frequently call `codegen_mem_*`, `codegen_fp_round*`, or chain to JIT-local jump targets. Candidate implementation files: `src/codegen_new/codegen_backend_arm64_ops.c`, `src/codegen_new/codegen_backend_arm64.c`, `src/codegen_new/codegen_allocator.h`, `src/codegen_new/codegen_allocator.c`. Rollback trigger: if any supposedly local target class can escape the contiguous arena in practice or if the fallback split becomes harder to reason about than the current conservative path. Evidence: `E-034`, `E-035`, `E-036`, `E-037`, `E-038`, `E-039`.
- F-012: ARM64 abort/control-flow plumbing expands more often than x86-64 once blocks drift away from block0. `host_arm64_CBNZ()` already degrades to `CBZ skip; B dest` when the target is outside the 19-bit range, the global `codegen_exit_rout` is created in the helper block at backend init, and the arena spans ~120MB, so far-from-helper blocks will frequently exceed 1MB. Separately, every patchable `host_arm64_Bxx_()` condition is emitted as inverted `B.cond` plus unconditional `B`, while x86-64 patches a single long Jcc. Severity: medium-high. Confidence: medium-high. Expected user-visible impact: medium-high extra instructions and branch-predictor pressure in helper-abort and branch-heavy blocks. Likely reproduction conditions: longer-lived sessions that compile many blocks plus workloads with lots of mem-helper abort checks or compare/jump uops. Candidate implementation files: `src/codegen_new/codegen_backend_arm64_ops.c`, `src/codegen_new/codegen_backend_arm64.c`, potentially `src/codegen_new/codegen_block.c`. Rollback trigger: if distance histograms later show hot blocks usually remain within 1MB or if local veneers inflate I-cache use more than they reduce branches. Evidence: `E-037`, `E-040`, `E-041`, `E-042`.
- F-013: The missing ARM64 `MOV_IMM` fast path is backed by a concrete interface gap: the IR layer declares generic direct imm-store hooks, `codegen_reg_write_imm()` depends on them, and x86-64 implements them, but the ARM64 backend currently only provides narrow `STORE_PTR_IMM{,_8}` uops that work for in-range `cpu_state` destinations. This keeps the broad dead-end immediate-store optimization disabled and leaves stack immediates without any backend hook at all. Severity: high. Confidence: high. Expected user-visible impact: medium-high extra register traffic and instruction count across generic IR, especially for short-lived constants that never need a host register. Likely reproduction conditions: address-generation and flag-update blocks with dead-end constants, plus churn paths that repeatedly re-materialize small immediates. Candidate implementation files: `src/codegen_new/codegen_backend_arm64_uops.c`, `src/codegen_new/codegen_backend_arm64_ops.c`, `src/codegen_new/codegen_backend_arm64.h`. Rollback trigger: if a first ARM64 implementation needs unsafe address assumptions or a much larger addressing refactor than expected. Evidence: `E-019`, `E-021`, `E-043`, `E-044`, `E-045`.
- F-014: ARM64 floating-point stack direct-access helpers currently have a correctness gap sharper than the earlier `fatal()` range family. `host_arm64_LDR_IMM_F64()` / `STR_IMM_F64()` do not validate `OFFSET12_Q`, and `codegen_direct_{read,write}_{64,double}_stack()` forwards stack offsets straight into them, while `codegen_reg.c` can route stack-backed `REG_QWORD` and `REG_DOUBLE` traffic through those helpers. If stack offsets grow beyond the 12-bit scaled window, these paths can silently encode the wrong address instead of cleanly failing. Severity: high correctness / latent. Confidence: medium-high. Expected user-visible impact: low-frequency but potentially severe corruption or instability if future stack layout growth crosses the encoding limit. Likely reproduction conditions: larger temporary-frame layouts, new FP temporaries, or future backend refactors that expand stack usage. Candidate implementation files: `src/codegen_new/codegen_backend_arm64_ops.c`, `src/codegen_new/codegen_backend_arm64_uops.c`. Rollback trigger: if a full stack-offset audit proves all possible offsets permanently remain within the current 12-bit scaled window. Evidence: `E-046`, `E-047`, `E-048`.
- F-015: `codegen_ops_branch.c` is the dominant shared IR producer of ARM64-costly control-flow shapes. Static counts over this one file show `28` explicit `uop_JMP(ir, codegen_exit_rout)` sites, `38` `uop_CALL_FUNC_RESULT` fallbacks, and `50` `uop_CMP_IMM_J{Z,NZ}_DEST` sites, with heavier cases such as `JLE`/`JNLE` and `LOOPE` stacking multiple helper calls before the terminal exit. On ARM64, those shared IR shapes map directly onto the already-confirmed backend weak spots: absolute helper calls (`F-011`), two-branch patchable conditionals / long-range fallbacks (`F-012`), and a single global exit stub. Severity: high. Confidence: high. Expected user-visible impact: high on branch-heavy, flag-unknown, and loop-heavy guest code because the shared frontend amplifies ARM64 backend costs at scale. Likely reproduction conditions: integer-heavy code with many Jcc/LOOP instructions, blocks that frequently enter `FLAGS_UNKNOWN` fallback, and workloads that force repeated helper abort/exit checks. Candidate implementation files: `src/codegen_new/codegen_ops_branch.c`, `src/codegen_new/codegen_backend_arm64_ops.c`, `src/codegen_new/codegen_backend_arm64_uops.c`, `src/codegen_new/codegen_backend_arm64.c`. Rollback trigger: if future workload telemetry shows another producer dominates dynamic helper/exit traffic despite this strong static density. Evidence: `E-052`, `E-056`, `E-057`.
- F-016: Shared unroll policy and churn flags systematically suppress the cheapest no-exit control-flow path exactly where ARM64 recompiles are already expensive. `codegen_can_unroll()` immediately returns false for `CODEBLOCK_BYTE_MASK`, forward/out-of-block targets, and other non-local cases; `codegen_can_unroll_full()` further caps unrolling at `1000` uops, `200` register references, and `10` iterations; and `ropJE_common()` / `ropJNE_common()` still carry a comment that JE/JNE unrolling is disabled because the code can “take the wrong turn.” Because dirty-list recompiles promote blocks into `BYTE_MASK`, this shared policy directly couples `F-008` churn escalation to more ARM64 `codegen_exit_rout` traffic. Severity: medium-high. Confidence: high. Expected user-visible impact: high on dirty/recompiled branch-heavy blocks and equality-test loops, where control flow repeatedly falls back to the global exit path instead of staying in-block. Likely reproduction conditions: recompiled tight loops, equality-heavy branch code, blocks with high uop density or version-reference count, and workloads that trigger `BYTE_MASK`. Candidate implementation files: `src/codegen_new/codegen_ops_branch.c`, `src/codegen_new/codegen_ops_helpers.c`, `src/codegen_new/codegen_ops_helpers.h`, `src/codegen_new/codegen_block.c`. Rollback trigger: if deeper audit proves the unroll suppression is semantically required for most hot paths or if restoring it reintroduces control-flow mismatches. Evidence: `E-053`, `E-054`, `E-055`, `E-058`.
- F-017: Shared helper-call pressure remains broad even outside the branch module. `codegen_ops_shift.c` contains `28` `uop_CALL_FUNC(ir, flags_rebuild)` sites plus `3` direct zero-count exits to `codegen_exit_rout`, while `codegen_ops_arith.c` hides a `uop_CALL_FUNC_RESULT(..., CF_SET)` helper behind `get_cf()` and calls that wrapper `38` times across carry-dependent arithmetic. This does not outrank `F-015`, but it means `A-013` has immediate leverage across multiple integer-op families and that any later helper-elimination work should not stay branch-only. Severity: medium. Confidence: high. Expected user-visible impact: medium on rotate/carry-heavy code and moderate reinforcement of ARM64 helper-call overhead in general integer workloads. Likely reproduction conditions: ADC/SBB-heavy code, rotate-heavy loops, and churn paths that also activate `CODEBLOCK_NO_IMMEDIATES` shift scaffolding. Candidate implementation files: `src/codegen_new/codegen_ops_shift.c`, `src/codegen_new/codegen_ops_arith.c`, `src/codegen_new/codegen_backend_arm64_ops.c`, `src/codegen_new/codegen_backend_arm64_uops.c`. Rollback trigger: if future workload telemetry shows these families are rare in representative traces or if `A-013` already removes most of the user-visible cost. Evidence: `E-059`, `E-060`, `E-061`.
- F-018: The remaining unexamined shared helper producers are tail-ranked rather than new first-order slices. Static ranking over `codegen_ops_misc.c`, `codegen_ops_jump.c`, and `codegen_ops_stack.c` finds only `6`, `6`, and `2` helper/exit sites respectively. `codegen_ops_misc.c` concentrates them in carry-preserving `INC/DEC` rebuilds, far `FF /5` jumps, and `CLC/CMC/STC`; `codegen_ops_jump.c` concentrates them in far control transfer (`JMP far`, `RETF`, `IRET`) while its near backward jumps already use the direct-return/unroll path; `codegen_ops_stack.c` only rebuilds flags for `PUSHF` / `PUSHFD`. Severity: low-medium. Confidence: high. Expected user-visible impact: low on general workloads, with niche relevance for privileged/far-control-transfer-heavy guests. Likely reproduction conditions: DOS extenders, kernels, monitors, or guests that frequently execute far returns/jumps or serialize flags with `PUSHF` / `PUSHFD` / `STC` / `CLC` / `CMC`. Candidate implementation files: none ahead of the existing backlog; if future traces justify it, revisit `src/codegen_new/codegen_ops_misc.c`, `src/codegen_new/codegen_ops_jump.c`, `src/codegen_new/codegen_ops_stack.c`, and `src/codegen_new/codegen_backend_arm64_ops.c`. Rollback trigger: if future telemetry shows far-control-transfer or flag-stack traffic materially hotter than this static tail ranking suggests. Evidence: `E-062`, `E-063`, `E-064`, `E-065`.
- F-019: Current HEAD already fixes a real ARM64 correctness hazard in shared flag-helper dispatch. `codegen_ops_jit_wrappers.h` now adds `__attribute__((noinline, used))` wrappers around the address-taken `static __inline` flag helpers in `x86_flags.h`, and the affected shared producer modules include that wrapper header instead of handing raw inline-helper addresses to `uop_CALL_FUNC*`. This removes a plausible ARM64/linker-optimization failure mode where emitted JIT code could call a merged or eliminated helper symbol. Severity: high before fix; current HEAD status: fixed upstream. Confidence: high. Expected user-visible impact: medium-high stability gain on helper-heavy control-flow and flag-rebuild paths, but no direct performance win. Likely reproduction conditions: ARM64 builds where compiler/linker optimization folds or suppresses out-of-line copies of `flags_rebuild*` or `ZF_SET` / `CF_SET` / `NF_SET` / `PF_SET` / `VF_SET`. Candidate implementation files: none for the next planning wave unless new unwrapped address-taken inline helpers are discovered; current coverage lives in `src/codegen_new/codegen_ops_jit_wrappers.h`, `src/codegen_new/codegen_ops_branch.c`, `src/codegen_new/codegen_ops_shift.c`, `src/codegen_new/codegen_ops_arith.c`, `src/codegen_new/codegen_ops_misc.c`, and `src/codegen_new/codegen_ops_stack.c`. Rollback trigger: if later audit finds other `static __inline` helpers still passed to `uop_CALL_FUNC*` without a wrapper or if the wrapper layer introduces symbol/visibility regressions. Evidence: `E-066`, `E-067`, `E-071`.
- F-020: Current HEAD also closes one missing ARM64 `MOV` truncation gap. `codegen_MOV()` now handles `W<-L`, `B<-L`, and `B<-W` with `BFI` instead of falling through to `fatal()`, removing one latent backend correctness/instability path from the earlier audit baseline. Severity: medium-high before fix; current HEAD status: fixed upstream. Confidence: high. Expected user-visible impact: low-medium but important for stability because the previous failure mode was backend fatal/abort on legal register-size combinations. Likely reproduction conditions: IR flows that copy a wider integer virtual register into 16-bit or 8-bit destinations during ARM64 lowering. Candidate implementation files: none for the current plan unless broader ARM64 `MOV` normalization work is later taken up in `src/codegen_new/codegen_backend_arm64_uops.c`. Rollback trigger: if later lowering audits expose additional size-pair holes or if the new `BFI` forms prove semantically wrong for any subregister case. Evidence: `E-068`.
- F-021: Upstream deterministic round-robin block eviction improves allocator noise, but current HEAD still keeps the probe state across `codegen_reset()` / `codegen_init()`. `codegen_delete_random_block()` now advances a `static int evict_probe`, yet neither init nor reset clears it, so eviction order is only deterministic within a process lifetime rather than across dynarec resets. Severity: low. Confidence: high. Expected user-visible impact: low direct runtime impact, medium measurement-hygiene impact for repeatability. Likely reproduction conditions: long-running same-process benchmarking or repeated reset/reinit cycles that exhaust the free list after dirty/purgable reuse fails. Candidate implementation files: `src/codegen_new/codegen_block.c`. Rollback trigger: if later instrumentation shows this fallback path is too cold to justify follow-up or if keeping process-scoped probe continuity is found to be intentional. Evidence: `E-069`, `E-070`, `E-072`.

## Progress Log
| Completed analysis unit | Key finding ID(s) | Evidence link(s) | Estimated impact | Decision ID | Follow-up action ID(s) |
| --- | --- | --- | --- | --- | --- |
| U-000 ARM64 backend surface map | `F-001` | `E-001`, `E-002`, `E-004`, `E-005`, `E-009` | High for scoping and prioritization | `D-001` | `A-001`, `A-002`, `A-003`, `A-004`, `A-005` |
| U-001 `codegen_backend_arm64.c` | `F-002`, `F-003`, `F-004` | `E-003`, `E-010`, `E-011`, `E-012`, `E-013`, `E-014`, `E-015` | Medium-high: identified one ARM64-specific compile-time cost, one ARM64-specific helper-cost candidate, and one deprioritized false lead | `D-002`, `D-003` | `A-002`, `A-004`, `A-006` |
| U-002 `codegen_backend_arm64_uops.c` hotspot audit | `F-005`, `F-006`, `F-007` | `E-016`, `E-017`, `E-018`, `E-019`, `E-020`, `E-021`, `E-022`, `E-023`, `E-024` | High: surfaced a bounded P0 correctness defect plus a broad ARM64-only optimization gap | `D-004`, `D-005`, `D-006` | `A-004`, `A-007`, `A-008`, `A-009` |
| U-003 `386_dynarec.c` recompile/churn audit | `F-008`, `F-009` | `E-025`, `E-026`, `E-027`, `E-028` | High: ties ARM64 compile-time taxes to specific churn/escalation mechanics | `D-007`, `D-008` | `A-003`, `A-010` |
| U-004 `codegen_backend_arm64_imm.c` | `F-010` | `E-029`, `E-030`, `E-031`, `E-032`, `E-033` | Medium: confirmed that the table is not the main issue, but register-immediate selection still leaves one-instruction ARM64 constants unused | `D-009` | `A-011`, `A-012` |
| U-005 `codegen_backend_arm64_ops.c` | `F-011`, `F-012`, `F-014` | `E-034`, `E-040`, `E-041`, `E-046` | High: surfaced one broad helper-dispatch gap, one branch-range/control-flow gap, and one latent correctness defect | `D-010`, `D-011`, `D-012` | `A-013`, `A-014`, `A-015`, `A-017` |
| U-006 remaining `codegen_backend_arm64_uops.c` support-path audit | `F-011`, `F-012`, `F-013`, `F-014` | `E-039`, `E-045`, `E-047`, `E-048` | High: tied emitter-level gaps to actual hot helper-call, abort-check, imm-store, and stack-writeback sites | `D-011`, `D-012` | `A-012`, `A-013`, `A-014`, `A-015` |
| U-007 supporting module audit (`codegen_allocator.c/h`, `codegen_reg.c`, `codegen_backend_arm64.h`) | `F-011`, `F-013` | `E-035`, `E-036`, `E-043`, `E-049`, `E-050`, `E-051` | High for implementation readiness: narrowed which new slices are low-risk/high-return versus control-flow-heavy follow-ons | `D-010`, `D-013` | `A-012`, `A-013`, `A-016` |
| U-008 `codegen_ops_branch.c` IR-frequency / branch-production audit | `F-015` | `E-052`, `E-053` | High: confirmed that the shared branch frontend is a first-order frequency amplifier for the ARM64 call/branch gaps | `D-014` | `A-013`, `A-014`, `A-018`, `A-019` |
| U-009 control-flow policy/support audit (`codegen_ops_helpers.c/h`, `codegen_ops_jump.c`, `codegen_ops_shift.c`, `codegen.c`, `codegen_block.c`) | `F-015`, `F-016` | `E-054`, `E-055`, `E-056`, `E-057`, `E-058` | High for prioritization: linked dirty-block policy and shared unroll limits directly to the branch-heavy ARM64 cost multipliers | `D-015` | `A-010`, `A-013`, `A-014`, `A-018`, `A-019` |
| U-010 secondary shared flag-helper producer audit (`codegen_ops_shift.c`, `codegen_ops_arith.c`) | `F-017` | `E-059`, `E-060`, `E-061` | Medium-high: proved that branch is not the only shared helper-call source, but that the next tier is still best addressed first by `A-013` rather than riskier semantic rewrites | `D-016` | `A-013`, `A-019`, `A-020` |
| U-011 remaining tail-producer sweep (`codegen_ops_misc.c`, `codegen_ops_stack.c`, residual `codegen_ops_jump.c`) | `F-018` | `E-062`, `E-063`, `E-064`, `E-065` | Medium for prioritization: closes the static source sweep and prevents over-promoting low-density helper sites into standalone early slices | `D-017`, `D-018` | `A-013`, `A-021` |
| U-012 post-sync delta review (`codegen_ops_jit_wrappers.h`, shared `codegen_ops_*`, `codegen_backend_arm64_uops.c`, `codegen_block.c`) | `F-019`, `F-020`, `F-021` | `E-066`, `E-067`, `E-068`, `E-069`, `E-070`, `E-071`, `E-072` | Medium for planning fidelity: current HEAD closes two correctness hazards and adds one low-priority reproducibility nit without changing implementation order | `D-019`, `D-020` | `A-013`, `A-022` |

## Decision Ledger
| Decision ID | Option chosen | Alternatives rejected | Rationale | Evidence | Reversal trigger |
| --- | --- | --- | --- | --- | --- |
| D-001 | Analyze in this order: `codegen_backend_arm64.c` -> `codegen_backend_arm64_uops.c` -> `codegen_backend_arm64_imm.c` -> `386_dynarec.c` -> `codegen_backend_arm64_ops.c`/headers. | Starting with emitters first; starting with `386_dynarec.c` first. | Begin where ARM64-specific scaffolding and helper stubs meet block lifecycle, then inspect semantic lowering before lower-level emitters. This maximizes evidence value per unit and helps separate backend-local issues from global dynarec churn. | `E-001`, `E-002`, `E-004`, `E-005`, `E-009` | Reverse if later evidence shows churn is dominated by frontend block management or a lower-level emitter defect. |
| D-002 | Treat `codegen_allocator_clean_blocks()` as correctness-mandated ARM64 cache maintenance, not a memory-lifetime bug; optimize around flush scope or churn, not by removing the flush outright. | Treat the init/epilogue cleanup call as a probable use-after-free or helper invalidation bug. | The allocator implementation only runs `__clear_cache()` across generated blocks on ARM64, which explains the ARM64-only call sites without indicating freed memory. | `E-012`, `E-013` | Reverse if later evidence shows helper memory is actually recycled or overwritten unexpectedly. |
| D-003 | Deprioritize generic load/store misalignment fallback as an ARM64-specific first implementation slice and move next to `codegen_backend_arm64_uops.c`. | Spend the next slice optimizing helper misalignment fallback first. | The same fallback structure exists in `codegen_backend_x86-64.c`, so the investigation should focus first on truly ARM64-specific costs such as cache maintenance and FP-round helper usage. | `E-003`, `E-014` | Reverse if later evidence shows ARM64 fallback frequency or cost is uniquely higher than other backends. |
| D-004 | Treat `codegen_MMX_ENTER()` branch patching as the top correctness slice candidate. | Fold it into a later emitter cleanup pass; defer until runtime evidence appears. | The stale `block->data` target is a line-precise ARM64-only inconsistency against both `FP_ENTER` and x86-64, and the failure mode is wrong control flow when a mem-block split occurs. | `E-016`, `E-017`, `E-018` | Reverse only if a deeper audit proves the function can never cross a mem-block boundary in practice. |
| D-005 | Prioritize the ARM64 missing `MOV_IMM` fast path above FP-round helper tuning. | Optimize FP-round helper first. | The `MOV_IMM` fast path is broad, low-level, and already proven valuable enough to exist on x86-64; its absence on ARM64 is a generic inefficiency, while FP-round helper traffic is currently limited to a small conversion family. | `E-011`, `E-019`, `E-020`, `E-021` | Reverse if later workload evidence shows FP-round conversions dominate and immediate stores are rare. |
| D-006 | Keep immediate-range direct state access as a robustness backlog item, not a first implementation slice. | Prioritize range-fallback work ahead of MMX branch fix or `MOV_IMM` fast path. | Current call sites stay within `cpu_state` fields and small stack slots, so the risk is latent rather than active; it is worth future-proofing but not first. | `E-022`, `E-023`, `E-024` | Reverse if later audits find current call sites near the range limit or any observed failures tied to range overflow. |
| D-007 | Treat dirty-list escalation/churn reduction as the third implementation slice candidate after the MMX-enter bug and ARM64 `MOV_IMM` fast path. | Defer churn work until after emitter/imm micro-optimizations. | `BYTE_MASK` + `NO_IMMEDIATES` escalation shortens blocks and forces slower immediate handling exactly where ARM64 recompiles already pay extra cache/JIT overhead. | `E-025`, `E-026`, `E-027`, `E-028` | Reverse if later workload evidence shows invalidation frequency is negligible outside synthetic SMC cases. |
| D-008 | Treat macOS `pthread_jit_write_protect_np` toggling as further justification for reducing recompiles, not as a standalone optimization target. | Pursue platform-specific write-protect batching first. | The toggle is required platform behavior; the highest-leverage response is to reduce how often the backend enters the recompile path. | `E-026` | Reverse if later investigation reveals safe batching or a broader JIT permission window is already available to the project. |
| D-009 | Treat the logical-immediate table as supporting machinery, not the primary optimization target; focus immediate work on `host_arm64_mov_imm()` and direct imm-store hooks instead. | Rewrite or special-case the table first. | `codegen_backend_arm64_imm.c` already knows common masks such as `0x01010101`; the remaining inefficiency is that the emitter path never consumes that information for plain constant materialization. | `E-029`, `E-030`, `E-031`, `E-032` | Reverse if future profiling shows compile-time binary-search cost dominates over emitted instruction count. |
| D-010 | Add JIT-local relative `BL` / `B` fast paths to the backlog ahead of branch-veneer work, but still behind `S-01` and the broader `S-02` imm-store slice. | Ignore the call/jump gap for now; prioritize exit veneers first. | It is the newer low-risk/high-return ARM64-specific gap: the allocator guarantees range for local targets, helper call sites are abundant, and x86-64 already uses a relative-first strategy. | `E-034`, `E-035`, `E-036`, `E-037`, `E-038`, `E-039` | Reverse if target classification turns out to be more ambiguous than the current static audit indicates. |
| D-011 | Keep the global-exit / conditional-veneer optimization as a second-wave performance slice rather than moving it ahead of direct imm-store work. | Promote veneer work ahead of `S-02`. | The likely win is real, but the control-flow plumbing is more invasive and needs IR-frequency plus distance evidence to rank it confidently against the simpler broad-based imm-store gap. | `E-040`, `E-041`, `E-042` | Reverse if future IR or distance evidence shows abort branches dominate hot paths more than dead-end immediates do. |
| D-012 | Elevate the ARM64 F64 stack offset issue into its own correctness backlog item instead of leaving it buried under the general range-fallback work. | Leave it implicit inside `A-009`. | Unlike the other direct-access helpers, these paths currently omit validation entirely and can silently misencode rather than stopping loudly. | `E-046`, `E-047`, `E-048` | Reverse if a complete stack-offset audit proves the current window can never be exceeded. |
| D-013 | Park `H-002` as a primary lead. | Continue treating raw host-register count as an active ARM64-only root cause. | The raw allocator-facing register count is not a compelling ARM64-only explanation after the defs comparison, so investigation effort should stay on helper, branch, and imm-store gaps instead. | `E-005`, `E-051` | Reverse if later spill-aware evidence shows ARM64 register pressure still dominates despite the wider raw pool. |
| D-014 | Treat `codegen_ops_branch.c` as the dominant shared frequency amplifier for `F-011` / `F-012`, and keep `A-013` ahead of smaller helper-specific ideas. | Start the next shared-frontend pass with scattered producer modules first. | Among shared ops producers, the branch frontend is uniquely dense with helper-result fallbacks and explicit global-exit traffic, so it deserves to anchor control-flow prioritization. | `E-052`, `E-056`, `E-057` | Reverse if future whole-trace telemetry shows another producer dominates dynamic helper/exit traffic despite this static density. |
| D-015 | Keep JE/JNE unroll restoration as a later, higher-risk shared-frontend slice behind relative local `BL/B` and the broader direct-imm-store work. | Promote `A-018` ahead of `A-013` or `A-012`. | The wrong-turn comment and `BYTE_MASK` / unroll gating make re-enabling equality-branch unroll riskier than the simpler ARM64-local call/jump fast path. | `E-053`, `E-054`, `E-055`, `E-058` | Reverse if future static or dynamic evidence shows equality-branch exits dominate enough to justify shared-frontend surgery sooner. |
| D-016 | Keep shift/arithmetic helper-elimination work as a later shared-frontend slice behind `A-013`. | Promote `A-020` ahead of the ARM64-local relative-call/jump work. | The shift/arithmetic helper density is real, but `A-013` improves every existing helper family immediately with backend-local risk, while `A-020` would touch delicate carry/rotate semantics. | `E-059`, `E-060`, `E-061` | Reverse if future traces still show helper-call overhead dominating after `A-013` is addressed. |
| D-017 | Treat `codegen_ops_misc.c`, `codegen_ops_jump.c`, and `codegen_ops_stack.c` as tail producers that mainly strengthen `A-013`, not as new dedicated first-wave slices. | Promote a new dedicated misc/jump/stack helper slice into the first planning wave. | The remaining modules have only `6`, `6`, and `2` helper/exit sites respectively, concentrated in far control transfer and flag serialization rather than dense branch/shift/arithmetic hot paths. | `E-062`, `E-063`, `E-064`, `E-065` | Reverse if future traces show far-control-transfer or flag-stack traffic materially hotter than the static ranking suggests. |
| D-018 | Declare the static ARM64 new-dynarec source audit complete and move next to implementation planning instead of further source discovery. | Continue source-by-source static discovery before planning. | Every suggested ARM64 backend module plus the remaining shared helper producers has now been inspected, and the final tail sweep did not uncover a new slice that outranks the current backlog. | `E-052`, `E-059`, `E-062`, `E-063`, `E-064`, `E-065` | Reverse only if later code changes land substantial new ARM64 backend/frontend logic before planning begins. |
| D-019 | Keep the implementation order unchanged after the upstream sync: `S-01`, `S-02`, `S-03`, then `A-013`. | Re-open backlog ordering because current HEAD landed the wrapper and `MOV` truncation fixes. | The upstream wrapper and `MOV` truncation patches remove two correctness gaps from the current baseline, but they do not materially change helper-call density, the direct imm-store gap, recompile churn, or the JIT-local call/jump ranking that drives the top slices. | `E-066`, `E-068`, `E-039`, `E-043` | Reverse if later synced code materially changes ARM64 helper dispatch, the imm-store contract, or churn policy again. |
| D-020 | Record the round-robin eviction probe reset issue as a low-priority measurement-hygiene backlog item (`A-022`), not a new early slice. | Promote allocator eviction cleanup into the first planning wave; ignore it entirely. | The issue is real and low-risk to fix, but it only affects a fallback path and mostly changes benchmark reproducibility rather than first-order ARM64 user-visible behavior. | `E-069`, `E-070`, `E-072` | Reverse if future benchmarking depends on reset-level determinism or if free-list exhaustion proves hot enough to matter user-visibly. |

## Evidence Index
| Evidence ID | Reference | Exact location | Related IDs | Why it matters |
| --- | --- | --- | --- | --- |
| E-001 | `src/codegen_new/CMakeLists.txt` | lines 56-62 | `F-001`, `D-001` | Establishes the authoritative ARM64 backend translation units selected by the build. |
| E-002 | `src/codegen_new/codegen_backend_arm64.c` | lines 30-72 | `F-001`, `H-001`, `H-002` | Shows exported helper globals plus the fixed integer/FP host register pools used by the ARM64 backend. |
| E-003 | `src/codegen_new/codegen_backend_arm64.c` | lines 74-120 | `H-001` | Reveals the memory helper stub pattern: lookup probe, misalignment branch, fallback call, and abort capture. |
| E-004 | `src/codegen_new/codegen_backend_arm64.h` | lines 3-12 | `F-001` | Shows ARM64 backend-local block/hash constants and exported helper hooks. |
| E-005 | `src/codegen_new/codegen_backend_arm64_defs.h` | lines 102-115 | `F-001`, `H-002` | Defines pinned argument/temp/cpustate registers and allocatable host pool sizes. |
| E-006 | `src/codegen_new/codegen_backend_arm64_ops.h` | lines 1-220 | `F-001`, `A-005` | Demonstrates the breadth of raw ARM64 emitter helpers exposed to higher layers. |
| E-007 | `src/codegen_new/codegen_backend_arm64_uops.c` | file presence from build selection; detailed lines pending deep analysis | `A-002` | Marks the semantic lowering unit as a required deep-analysis target. |
| E-008 | `src/codegen_new/codegen_backend_arm64.h` | line 30 | `H-003`, `A-003` | Export of `host_arm64_find_imm()` confirms dedicated immediate-selection logic worth investigating. |
| E-009 | `src/cpu/386_dynarec.c` | lines 31-43 and 52-58 | `F-001`, `H-004`, `D-001` | Shows the new dynarec/backend integration boundary and runtime block-end globals that can influence churn. |
| E-010 | `src/codegen_new/codegen_backend_arm64.c` | lines 240-281 | `F-003`, `H-006` | Defines ARM64 shared FP-round helper stubs with runtime jump-table dispatch based on `cpu_state.new_fp_control`. |
| E-011 | `src/codegen_new/codegen_backend_arm64_uops.c` | lines 1288-1323 | `F-003`, `H-006`, `A-002` | Shows ARM64 uop lowering calling `codegen_fp_round` / `codegen_fp_round_quad` in FP-to-int conversion paths. |
| E-012 | `src/codegen_new/codegen_backend_arm64.c` | lines 323-325 and 363-374 | `F-002`, `H-005`, `D-002` | Identifies the ARM64-only call sites that clean generated blocks after helper init and block epilogues. |
| E-013 | `src/codegen_new/codegen_allocator.c` | lines 190-200 | `F-002`, `H-005`, `D-002` | Confirms `codegen_allocator_clean_blocks()` is an ARM64 cache-flush walk using `__clear_cache()`, not a free. |
| E-014 | `src/codegen_new/codegen_backend_x86-64.c` | lines 72-125 and 160-214 | `F-004`, `H-001`, `D-003` | Demonstrates that misalignment-triggered helper fallback is shared with the x86-64 backend, reducing its value as an ARM64-specific lead. |
| E-015 | `src/codegen_new/codegen_backend_x86-64_uops.c` | lines 1400-1445 | `F-003` | Provides the comparison point where x86-64 emits direct FP-to-int conversions with MXCSR loads rather than calling shared helper stubs. |
| E-016 | `src/codegen_new/codegen_backend_arm64_uops.c` | lines 781-805 | `F-005`, `D-004` | Shows the ARM64 `codegen_MMX_ENTER()` branch patch using `&block->data[block_pos]` instead of the mutable `block_write_data` buffer. |
| E-017 | `src/codegen_new/codegen_backend_arm64_ops.c` | lines 287-310 | `F-005` | Confirms ARM64 code emission can allocate chained mem blocks and update `block_write_data`, making stale-buffer patching a real hazard. |
| E-018 | `src/codegen_new/codegen_backend_x86-64_uops.c` | lines 829-845 | `F-005`, `D-004` | Provides the x86-64 comparison where the analogous MMX-enter branch is patched against the current write buffer. |
| E-019 | `src/codegen_new/codegen_ir.c` | lines 113-118 | `F-006`, `D-005` | Shows the direct `MOV_IMM`-to-memory optimization is guarded by `CODEGEN_BACKEND_HAS_MOV_IMM`. |
| E-020 | `src/codegen_new/codegen_backend_x86-64.h` | line 14 | `F-006`, `D-005` | Demonstrates that x86-64 opts into the `MOV_IMM` fast path. |
| E-021 | `src/codegen_new/codegen_reg.c` | lines 512-540 | `F-006`, `F-013` | Shows the backend-specific direct immediate-store helpers that become available when the fast-path flag is enabled and the generic hook contract is satisfied. |
| E-022 | `src/codegen_new/codegen_backend_arm64_uops.c` | lines 3373-3566 | `F-007`, `H-007`, `D-006` | Captures the fixed-range direct read/write helpers and their `fatal()` behavior when offsets exceed AArch64 immediate windows. |
| E-023 | `src/codegen_new/codegen_reg.c` | lines 307-540 | `F-007`, `D-006` | Audits the current ARM64 direct-access call sites: `cpu_state` fields, FPU tag/MM/ST arrays, and small stack slots. |
| E-024 | `src/cpu/cpu.h` | lines 332-417 | `F-007`, `D-006` | Shows the relevant `cpu_state` fields currently used by direct-access helpers live inside the core state struct, supporting the “latent, not active” classification. |
| E-025 | `src/cpu/386_dynarec.c` | lines 492-498 | `F-008`, `D-007` | Shows the dirty-list escalation path that promotes blocks to `BYTE_MASK` and then `NO_IMMEDIATES`. |
| E-026 | `src/cpu/386_dynarec.c` | lines 539-552 and 639-642 | `F-008`, `F-009`, `D-007`, `D-008` | Shows the shortened `BYTE_MASK` block-size cap and the macOS AArch64 `pthread_jit_write_protect_np` toggles around recompilation. |
| E-027 | `src/codegen_new/codegen_block.c` | lines 370-384 and 548-568 | `F-008` | Confirms dirty invalidation frees the compiled mem blocks and that recompilation allocates fresh executable memory again. |
| E-028 | `src/codegen_new/codegen.c` | lines 217-240 | `F-008` | Representative decode path showing how `CODEBLOCK_NO_IMMEDIATES` replaces embedded immediates with memory loads, increasing translation work. |
| E-029 | `src/codegen_new/codegen_backend_arm64_imm.c` | lines 1313-1329 | `F-010`, `H-003`, `D-009` | Shows that ARM64 logical immediates are resolved through a 1302-entry binary search table rather than on-the-fly synthesis. |
| E-030 | `src/codegen_new/codegen_backend_arm64_imm.c` | lines 345-347 | `F-010`, `D-009` | Demonstrates that `0x01010101` is already encodable by the ARM64 logical-immediate table. |
| E-031 | `src/codegen_new/codegen_backend_arm64_uops.c` | lines 800-805 | `F-010` | Shows a hot ARM64 MMX path materializing `0x01010101` through `host_arm64_mov_imm()` before multiple stores. |
| E-032 | `src/codegen_new/codegen_backend_arm64_ops.c` | lines 1089-1115 and 1532-1539 | `F-010`, `H-003` | Confirms plain ARM64 immediate materialization only uses `MOVZ` / `MOVK`, with no `MOVN` or logical-immediate move path. |
| E-033 | `src/codegen_new/codegen_backend_x86-64_ops.c` | lines 1057-1066 | `F-010` | Provides the x86-64 comparison where a 32-bit register immediate is handled by a single emitter helper/instruction. |
| E-034 | `src/codegen_new/codegen_backend_arm64_ops.c` | lines 1518-1528 | `F-011`, `H-008`, `D-010` | Shows ARM64 call and jump helpers always materialize a full 64-bit destination into `X16` and dispatch via `BLR/BR`. |
| E-035 | `src/codegen_new/codegen_allocator.h` | lines 4-21 | `F-011`, `F-012`, `H-008`, `H-009` | States that the executable allocator is bounded by ARMv8's +/-128MB branch range and allocates 131072 blocks of size `0x3c0` (~120MB total). |
| E-036 | `src/codegen_new/codegen_allocator.c` | lines 90-103 and 185-187 | `F-011`, `F-012` | Confirms the allocator mmaps one contiguous executable arena and derives all code pointers as offsets into that arena. |
| E-037 | `src/codegen_new/codegen_backend_arm64.c` | lines 298-315 | `F-011`, `F-012` | Shows helper stubs and `codegen_exit_rout` are emitted into the same JIT arena during backend initialization. |
| E-038 | `src/codegen_new/codegen_backend_x86-64_ops.c` | lines 31-46 and 49-65 | `F-011` | Provides the x86-64 comparison where relative `CALL/JMP` is attempted first and absolute-register fallback is used only when necessary. |
| E-039 | `src/codegen_new/codegen_backend_arm64_uops.c` | lines 197-223 and 905-1139 | `F-011` | Shows hot ARM64 helper-call sites that all funnel through `host_arm64_call()`, including instruction callbacks and memory helpers. |
| E-040 | `src/codegen_new/codegen_backend_arm64_ops.c` | lines 608-621 | `F-012`, `H-009`, `D-011` | Shows `host_arm64_CBNZ()` emitting a direct `CBNZ` only when the target fits 19 bits and otherwise degrading to `CBZ skip; B dest`. |
| E-041 | `src/codegen_new/codegen_backend_arm64_ops.c` | lines 469-585 | `F-012`, `H-009`, `D-011` | Shows every patchable ARM64 conditional branch template as inverted `B.cond` over an unconditional `B`. |
| E-042 | `src/codegen_new/codegen_backend_x86-64_ops.c` | lines 339-345 and 376-462 | `F-012` | Provides the x86-64 comparison where direct and patchable conditional branches use single long Jcc encodings. |
| E-043 | `src/codegen_new/codegen_ir_defs.h` | lines 915-918 | `F-013`, `A-012` | Declares the generic direct imm-store hooks the backend must provide to enable the broader `MOV_IMM` fast path. |
| E-044 | `src/codegen_new/codegen_backend_x86-64_uops.c` | lines 3510-3527 | `F-013`, `A-012` | Shows x86-64 implementing all four direct imm-store hooks expected by `codegen_reg_write_imm()`. |
| E-045 | `src/codegen_new/codegen_backend_arm64_uops.c` | lines 2724-2745 | `F-013` | Shows ARM64's current ad-hoc imm-store lowering is limited to `STORE_PTR_IMM{,_8}` and only covers in-range `cpu_state` destinations. |
| E-046 | `src/codegen_new/codegen_backend_arm64_ops.c` | lines 987-990 and 1325-1328 | `F-014`, `A-015`, `D-012` | Shows the ARM64 floating-point immediate load/store emitters accept raw offsets without the range checks used by the integer and pointer variants. |
| E-047 | `src/codegen_new/codegen_backend_arm64_uops.c` | lines 3550-3576 | `F-014`, `A-015`, `D-012` | Shows stack-based 64-bit / double helpers forwarding stack offsets straight into the unvalidated F64 load/store emitters. |
| E-048 | `src/codegen_new/codegen_reg.c` | lines 324-352 and 438-467 | `F-014`, `A-015` | Shows stack-backed `REG_QWORD` / `REG_DOUBLE` loads and writebacks can reach the unvalidated ARM64 stack helpers. |
| E-049 | `src/codegen_new/codegen_backend_arm64.h` | lines 19-25 | `H-010`, `A-016` | Captures stale or unimplemented public ARM64 helper declarations (`LDR_LITERAL_*`, `STRB_IMM_W`) that drift from the actual ops surface. |
| E-050 | `src/codegen_new/codegen_backend_arm64_ops.h` | lines 214-215 | `H-010`, `A-016` | Shows the actual exported byte-store helper name is `host_arm64_STRB_IMM`, not `host_arm64_STRB_IMM_W`. |
| E-051 | `src/codegen_new/codegen_backend_x86-64_defs.h` | lines 47-50 | `H-002`, `D-013` | Shows x86-64 exposes only 3 integer host regs and 7 FP regs, weakening raw register-count as an ARM64-only performance explanation. |
| E-052 | `src/codegen_new/codegen_ops_branch.c` | lines 31-1016 | `F-015`, `H-011`, `D-014` | Static count over this file found `28` explicit `uop_JMP(ir, codegen_exit_rout)` sites, `38` `uop_CALL_FUNC_RESULT` fallbacks, and `50` `uop_CMP_IMM_J{Z,NZ}_DEST` sites, proving that the shared branch frontend heavily amplifies ARM64 backend costs. |
| E-053 | `src/codegen_new/codegen_ops_branch.c` | lines 208-239 | `F-016`, `A-018`, `D-015` | Shows JE/JNE unrolling is still disabled behind `ENABLE_UNROLL` because the code can “take the wrong turn,” forcing exit-rout sequences in common equality branches. |
| E-054 | `src/codegen_new/codegen_ops_helpers.h` and `src/codegen_new/codegen_ops_helpers.c` | lines 115-126 and 38-76 | `F-016`, `H-004`, `D-015` | Shows unroll is disabled for `CODEBLOCK_BYTE_MASK`, forward/out-of-block targets, and several non-local cases, then further capped by `1000` uops, `200` register references, and `10` iterations. |
| E-055 | `src/codegen_new/codegen_block.c` | lines 563-568 and 603-606 | `F-016`, `H-004`, `D-015` | Shows recompiles allocate fresh mem blocks while preserving `CODEBLOCK_BYTE_MASK`, directly coupling dirty-list churn to the no-unroll guard. |
| E-056 | `src/codegen_new/codegen_ops_shift.c` | lines 578-582, 698-702, and 818-822 | `F-015`, `D-014` | Shows adjacent non-branch producer modules also emit direct `codegen_exit_rout` guards, so branch-shaped exit pressure is not completely isolated to `codegen_ops_branch.c`. |
| E-057 | `src/codegen_new/codegen.c` | lines 742-749 | `F-015`, `A-013` | Shows generic instruction dispatch still funnels slow or unknown instruction bodies through `uop_CALL_INSTRUCTION_FUNC`, a helper-call path that inherits ARM64 absolute-call cost. |
| E-058 | `src/codegen_new/codegen_ops_jump.c` | lines 17-29 | `F-016`, `D-015` | Shows backward unconditional jumps already consult `codegen_can_unroll()` and return the destination directly, providing a contrast where the shared frontend can avoid the global exit path when semantics allow it. |
| E-059 | `src/codegen_new/codegen_ops_shift.c` | lines 19-430 and 572-822 | `F-017`, `A-020`, `D-016` | Static counts over this file found `28` `uop_CALL_FUNC(ir, flags_rebuild)` sites and `3` direct `uop_CMP_IMM_JZ(..., codegen_exit_rout)` guards, making shift/rotate the next shared helper-heavy producer after the branch module. |
| E-060 | `src/codegen_new/codegen_ops_shift.c` | lines 495-506 | `F-017`, `A-010` | Shows `CODEBLOCK_NO_IMMEDIATES` turns immediate-count shifts into RAM loads plus compare/jump scaffolding, extending the churn cost model into another shared producer. |
| E-061 | `src/codegen_new/codegen_ops_arith.c` | lines 18-22 and call sites at 29-2219 | `F-017`, `A-020`, `D-016` | Shows `get_cf()` is just `uop_CALL_FUNC_RESULT(..., CF_SET)` and is referenced `38` times across carry-dependent arithmetic, broadening helper-call pressure beyond branch/shift paths. |
| E-062 | `src/codegen_new/codegen_ops_misc.c` | lines 257-280, 309-369, 411-471, and 578-597 | `F-018`, `D-017` | Shows the remaining helper sites in `misc` are limited to `flags_rebuild_c` for carry-preserving `INC/DEC`, far `FF /5` transfers via `loadcsjmp`, and `CLC/CMC/STC` flag rebuilds. |
| E-063 | `src/codegen_new/codegen_ops_stack.c` | lines 382-422 | `F-018`, `D-017` | Shows `stack` is mostly direct load/store lowering; only `PUSHF` / `PUSHFD` call `flags_rebuild`. |
| E-064 | `src/codegen_new/codegen_ops_jump.c` | lines 17-29, 57-81, and 195-289 | `F-018`, `D-017` | Shows near backward jumps already use the direct-return/unroll path, while the helper calls in `jump` are concentrated in far control transfers (`loadcsjmp`, `loadcs`). |
| E-065 | repo query over remaining tail producer files | 2026-04-21 static count using `rg -c "uop_CALL_FUNC\\(|uop_CALL_FUNC_RESULT\\(|uop_JMP\\(ir, codegen_exit_rout\\)|uop_CMP_IMM_JZ\\(ir, .*codegen_exit_rout\\)|uop_CMP_IMM_JNZ\\(ir, .*codegen_exit_rout\\)"` over `codegen_ops_misc.c`, `codegen_ops_stack.c`, and `codegen_ops_jump.c` returned `misc:6`, `jump:6`, `stack:2` | `F-018`, `D-017`, `D-018` | Gives the numeric tail ranking needed to close the static sweep without inventing another first-order slice. |
| E-066 | `src/codegen_new/codegen_ops_jit_wrappers.h` | lines 1-68 | `F-019`, `D-019` | Defines the `noinline, used` wrapper layer for JIT-callable flag helpers and records the exact ARM64/linker optimization hazard it fixes. |
| E-067 | `src/cpu/x86_flags.h` | lines 60-61, 119-120, 186-187, 245-246, 408-409, and 496-525 | `F-019` | Shows the underlying flag-test and flag-rebuild helpers are `static __inline`, explaining why taking their address directly is unsafe for JIT function-pointer use. |
| E-068 | `src/codegen_new/codegen_backend_arm64_uops.c` | lines 1170-1177 | `F-020`, `D-019` | Shows current HEAD now handles three previously missing ARM64 `MOV` truncation cases instead of falling through to `fatal()`. |
| E-069 | `src/codegen_new/codegen_block.c` | lines 436-446 | `F-021`, `D-020` | Shows round-robin eviction now advances a function-local static `evict_probe`, making the algorithm deterministic within one process. |
| E-070 | `src/codegen_new/codegen_block.c` | lines 218-258 | `F-021`, `D-020` | Shows `codegen_init()` and `codegen_reset()` rebuild block state without clearing `evict_probe`, so determinism does not extend across dynarec resets. |
| E-071 | repo query over `src/codegen_new` | 2026-04-21 `rg -n "codegen_ops_jit_wrappers.h|uop_CALL_FUNC\\(ir, (flags_rebuild|flags_rebuild_c)\\)|uop_CALL_FUNC_RESULT\\(ir, [^,]+, (CF_SET|NF_SET|PF_SET|VF_SET|ZF_SET)\\)" src/codegen_new` returned wrapper-header includes in `branch`, `shift`, `arith`, `misc`, and `stack`, with no remaining direct raw flag-helper JIT call sites | `F-019`, `D-019` | Confirms the wrapper layer is actually adopted across the affected shared producer modules rather than existing as unused scaffolding. |
| E-072 | `src/codegen_new/codegen_block.c` | lines 184-207 | `F-021`, `D-020` | Shows `codegen_delete_random_block()` is only reached after free-list exhaustion and failed dirty/purgable reuse, keeping the eviction probe issue in the measurement-hygiene tier. |

## Analysis Units

### U-000 ARM64 backend surface map
- Timestamp: 2026-04-20 22:54:04 EDT
- What this unit covers: identifying the ARM64 backend files that are actually compiled, the supporting headers that define the ABI/register model, and the global dynarec runtime boundary that can masquerade as backend regression.
- Result:
  - Confirmed that `CMakeLists.txt` compiles exactly four ARM64 backend `.c` files for `ARCH STREQUAL "arm64"`.
  - Confirmed that `codegen_backend_arm64.c` owns helper globals and host register pools, making it the correct first deep-dive target.
  - Confirmed that `386_dynarec.c` remains part of the investigation because block-end and validation behavior can surface as backend churn.
- New/updated hypotheses: `H-001`, `H-002`, `H-003`, `H-004`.
- Confidence: high.
- Impact estimate: high, because this narrows the code surface for the rest of the campaign.

### U-001 `codegen_backend_arm64.c`
- Timestamp: 2026-04-20 22:58:23 EDT
- What this unit covers: ARM64 backend scaffolding, including shared memory helper stubs, FP-round helpers, backend init, and per-block prologue/epilogue behavior.
- What it does:
  - Builds shared direct-memory load/store helpers for byte/word/long/quad and float/double accesses.
  - Builds shared FP-round helpers keyed by `cpu_state.new_fp_control`.
  - Initializes shared helper entrypoints (`codegen_mem_*`, `codegen_fp_round*`, `codegen_gpf_rout`, `codegen_exit_rout`) and establishes block prologue/epilogue register-save policy.
- ARM64-specific logic observed:
  - ARM64 emits shared FP-round helper stubs with a runtime jump table rather than changing FP state inline.
  - ARM64 explicitly calls `codegen_allocator_clean_blocks()` after helper generation and after block epilogue emission; allocator inspection shows this is an `__clear_cache()` sweep required for I-cache coherency on self-modifying/generated code.
- Risk points:
  - `R-001`: compile-time/recompile-time cost from full mem-block cache flushes (`F-002`).
  - `R-002`: ARM64-only helper-call overhead for FP-to-int rounding (`F-003`).
  - `R-003`: misalignment-triggered helper fallback exists, but current comparison indicates it is shared with x86-64 and therefore parked as a non-specific lead (`F-004`).
- Candidate fixes (proposal only; no implementation here):
  - `A-004` follow-up: reduce block churn before attacking cache maintenance, since ARM64 must flush generated code but may not need to do so as often if recompilation drops.
  - `A-006` follow-up: if `codegen_backend_arm64_uops.c` shows material FP-round helper usage, prototype a bounded alternative that reduces helper-call indirection while preserving x87 rounding semantics.
  - Keep misalignment fast-path work out of the first slice unless later evidence shows uniquely high ARM64 pain.
- New/updated hypotheses:
  - `H-001` moved to `parked`.
  - `H-005` rejected.
  - `H-006` opened.
- Confidence: medium-high.
- Impact estimate: medium-high, because this module exposed one genuine ARM64-only compile-time cost and one plausible ARM64-only runtime cost candidate.

### U-002 `codegen_backend_arm64_uops.c` hotspot audit
- Timestamp: 2026-04-20 23:06:23 EDT
- What this unit covers: ARM64 uop-lowering hotspots most likely to explain architecture-specific issues, with emphasis on helper calls, direct state access helpers, and control-flow patching.
- What it does:
  - Lowers IR/uops into ARM64 control-flow, memory-helper, FP/MMX, and direct state-load/store sequences.
  - Owns the main emission sites for `codegen_exit_rout` branches, shared memory helper calls, and the ARM64 FP-round helper usage previously found in `codegen_backend_arm64.c`.
- Key observations:
  - Most explicit memory-helper call/exit patterns in ARM64 mirror x86-64 one-for-one, which keeps them off the top-priority ARM64-only list.
  - The shared FP-round helpers are referenced only in the `MOV_INT_DOUBLE` / `MOV_INT_DOUBLE_64` family here, narrowing the likely impact scope of `F-003`.
  - `codegen_MMX_ENTER()` contains a unique stale-buffer branch patch that can mis-target control flow when ARM64 emission spills into a chained mem block.
  - ARM64 does not participate in the x86-64-only `CODEGEN_BACKEND_HAS_MOV_IMM` optimization path, leaving a broad immediate-store performance gap on the table.
  - ARM64 direct state/stack helpers have no fallback when immediate-range assumptions stop holding; current call sites keep this latent rather than active.
- Candidate fixes (proposal only; no implementation here):
  - `A-007`: patch `codegen_MMX_ENTER()` to use `block_write_data` consistently and audit for any similar stale-buffer destinations.
  - `A-008`: add ARM64 direct immediate-store helpers and enable the `MOV_IMM` fast path once range handling is designed.
  - `A-009`: add generic fallback addressing for out-of-range direct state/stack accesses to turn latent fatal paths into safe slow paths.
  - Keep `A-006` lower priority until workload evidence shows the conversion family is hot enough to matter.
- New/updated hypotheses:
  - `H-006` remains open but narrowed to a small conversion family.
  - `H-007` opened as a latent robustness concern.
- Confidence: high for `F-005` and `F-006`; medium for `F-007`.
- Impact estimate: high overall because the unit surfaced a bounded P0 correctness defect and a broad P1 optimization gap.

### U-003 `386_dynarec.c` recompile/churn audit
- Timestamp: 2026-04-20 23:09:58 EDT
- What this unit covers: block lookup/validation, dirty-page invalidation, recompile entry, and the policy that escalates recompiling blocks into more conservative modes.
- What it does:
  - Validates cached blocks against PC/CS/status/page dirtiness and either executes, recompiles, or marks a new block.
  - Invalidates dirty blocks by freeing their generated code while keeping metadata on the dirty list for later recompilation.
  - Escalates dirty-listed blocks into `BYTE_MASK` and later `NO_IMMEDIATES` modes before recompiling them.
- Key observations:
  - Dirty invalidation is not cheap on ARM64: invalidation frees the generated mem-block chain, and the next execution allocates fresh executable memory again.
  - The first dirty-list recompile shortens the future block-size budget dramatically by enabling `BYTE_MASK`; the next dirty-list hit adds `NO_IMMEDIATES`, which increases decode/translation work by replacing some embedded immediates with RAM loads.
  - On macOS AArch64, every such recompile additionally toggles JIT write protection off and back on.
- Candidate fixes (proposal only; no implementation here):
  - `A-010`: revisit the escalation heuristic or threshold before forcing `BYTE_MASK` / `NO_IMMEDIATES`, especially for ARM64 hosts where each recompile is more expensive.
  - Keep churn reduction coupled with metrics/test plans so genuine self-modifying-code workloads are not regressed.
- New/updated hypotheses:
  - `H-004` remains open, but the current evidence now strongly favors “recompile churn amplifies ARM64-specific cost” over a purely semantic backend explanation for at least some performance regressions.
- Confidence: high for the escalation mechanism; medium for workload impact until logs/counters are available.
- Impact estimate: high, because this unit links ARM64-only compile-time taxes to specific recompile behavior and explains why some regressions may look disproportionately bad on Apple ARM64.

### U-004 `codegen_backend_arm64_imm.c`
- Timestamp: 2026-04-20 23:16:08 EDT
- What this unit covers: the ARM64 logical-immediate table itself and how much of that machinery is actually consumed by the higher-level emitter paths.
- What it does:
  - Stores a precomputed 1302-entry table of valid AArch64 logical-immediate encodings.
  - Exposes `host_arm64_find_imm()` so other emitter helpers can ask whether a 32-bit value is representable as a logical immediate.
- ARM64-specific logic observed:
  - The table already contains common backend masks such as `0x01010101`, so the immediate layer is more capable than the current register-immediate materialization path suggests.
  - `host_arm64_find_imm()` is only a lookup routine; it does not by itself decide whether `MOVZ` / `MOVK`, logical-immediate `ORR`, or a future `MOVN` would be the cheapest move form.
- Risk points:
  - `R-004`: constant materialization overhead comes from how emitters consume the table, not from a lack of encodable masks (`F-010`).
- Code examples:
  - Problematic ARM64 support that already exists (`src/codegen_new/codegen_backend_arm64_imm.c:345-347`):
    ```c
    { 0xa00, 0x01000000},
    { 0xe20, 0x01000100},
    { 0xe30, 0x01010101},
    ```
    Why it matters: `0x01010101` is already encodable as a logical immediate, so later two-instruction materialization is avoidable.
  - Problematic ARM64 consumer (`src/codegen_new/codegen_backend_arm64_ops.c:1532-1539`):
    ```c
    if (imm_is_imm16(imm_data))
        host_arm64_MOVZ_IMM(block, reg, imm_data);
    else {
        host_arm64_MOVZ_IMM(block, reg, imm_data & 0xffff);
        host_arm64_MOVK_IMM(block, reg, imm_data & 0xffff0000);
    }
    ```
    Why it matters: plain register-immediate setup never tries the cheaper logical-immediate form it already knows how to encode.
  - Hot ARM64 use site (`src/codegen_new/codegen_backend_arm64_uops.c:800-805`) with x86-64 contrast (`src/codegen_new/codegen_backend_x86-64_ops.c:1057-1066`):
    ```c
    host_arm64_mov_imm(block, REG_TEMP, 0x01010101);
    host_arm64_STR_IMM_W(block, REG_TEMP, REG_CPUSTATE, ...tag[0]...);
    host_arm64_STR_IMM_W(block, REG_TEMP, REG_CPUSTATE, ...tag[4]...);
    ```
    Why it matters: a common MMX-entry constant is still emitted through a heavier ARM64 path while x86-64 uses a single immediate move helper.
- Candidate fixes (proposal only; no implementation here):
  - `A-011`: add logical-immediate and `MOVN` selection to `host_arm64_mov_imm()`.
  - `A-012`: keep the broader direct imm-store slice separate; the table already provides reusable encoding knowledge for that work.
- New/updated hypotheses:
  - `H-003` moved to `confirmed`.
- Confidence: high.
- Impact estimate: medium, because the immediate layer itself is not broken but it now clearly feeds a broad emitter inefficiency.

### U-005 `codegen_backend_arm64_ops.c`
- Timestamp: 2026-04-20 23:21:14 EDT
- What this unit covers: raw ARM64 emitter helpers for branches, calls, immediate loads/stores, block chaining, and stack-access primitives.
- What it does:
  - Encodes scalar/vector arithmetic, compare, branch, call, and load/store instructions.
  - Allocates chained executable mem blocks and patches in inter-block jumps when a block fragment overflows `BLOCK_MAX`.
  - Provides the low-level helpers that every higher ARM64 uop-lowering path relies on.
- ARM64-specific logic observed:
  - `host_arm64_call()` and `host_arm64_jump()` always materialize absolute destinations into `X16` before `BLR` / `BR`.
  - `host_arm64_CBNZ()` already contains a built-in long-range fallback sequence (`CBZ skip; B dest`) when the 19-bit range is exceeded.
  - Patchable conditional branches (`host_arm64_Bxx_()`) are always emitted as inverted `B.cond` over an unconditional `B`.
  - `host_arm64_LDR_IMM_F64()` / `host_arm64_STR_IMM_F64()` do not validate their scaled-immediate offset, unlike the integer and pointer forms in the same file.
- Risk points:
  - `R-005`: avoidable helper-call and JIT-local jump bloat from never using relative `B/BL` even when the allocator guarantees range (`F-011`).
  - `R-006`: extra abort/control-flow branches once targets drift out of the 19-bit conditional range (`F-012`).
  - `R-007`: silent stack-offset misencoding for F64 load/store helpers if frame offsets grow beyond the 12-bit scaled window (`F-014`).
- Code examples:
  - Problematic ARM64 call/jump site (`src/codegen_new/codegen_backend_arm64_ops.c:1518-1528`) with x86-64 contrast (`src/codegen_new/codegen_backend_x86-64_ops.c:31-46`):
    ```c
    host_arm64_MOVX_IMM(block, REG_X16, (uint64_t) dst_addr);
    host_arm64_BLR(block, REG_X16);
    ```
    Why it matters: JIT-local targets inside the shared code arena still pay a full 64-bit materialization sequence instead of one relative branch.
  - Problematic ARM64 conditional-range fallback (`src/codegen_new/codegen_backend_arm64_ops.c:608-621`):
    ```c
    if (offset_is_19bit(offset)) {
        codegen_addlong(block, OPCODE_CBNZ | OFFSET19(offset) | Rt(reg));
    } else {
        codegen_addlong(block, OPCODE_CBZ | OFFSET19(8) | Rt(reg));
        codegen_addlong(block, OPCODE_B | OFFSET26(offset));
    }
    ```
    Why it matters: once `codegen_exit_rout` or another target sits more than 1MB away, every abort check grows into two branches.
  - Problematic ARM64 patchable branch template (`src/codegen_new/codegen_backend_arm64_ops.c:549-554`) with x86-64 contrast (`src/codegen_new/codegen_backend_x86-64_ops.c:425-430`):
    ```c
    codegen_addlong(block, OPCODE_BCOND | COND_EQ | OFFSET19(8));
    codegen_addlong(block, OPCODE_B);
    ```
    Why it matters: ARM64 patchable Jccs always start as a two-branch sequence, while x86-64 reserves a single long Jcc slot.
  - Problematic ARM64 F64 load/store helpers (`src/codegen_new/codegen_backend_arm64_ops.c:987-990` and `1325-1328`):
    ```c
    codegen_addlong(block, OPCODE_LDR_IMM_F64 | OFFSET12_Q(offset) | Rn(base_reg) | Rt(dest_reg));
    codegen_addlong(block, OPCODE_STR_IMM_F64 | OFFSET12_Q(offset) | Rn(base_reg) | Rt(src_reg));
    ```
    Why it matters: unlike `LDR_IMM_W`, `LDR_IMM_X`, `STR_IMM_W`, and `STR_IMM_Q`, these helpers never check whether `offset` actually fits.
- Candidate fixes (proposal only; no implementation here):
  - `A-013`: add JIT-local relative `BL` / `B` fast paths with conservative fallback for external targets.
  - `A-014`: if frequency justifies it, add local exit veneers or per-region helper stubs to keep more branches in the 19-bit range.
  - `A-015`: add validation and fallback for F64 stack helpers before layout growth turns this into silent corruption.
  - `A-016`: clean up the public ARM64 header while the emitter surface is fresh in context.
- New/updated hypotheses:
  - `H-007` moved to `confirmed`.
  - `H-008` moved to `confirmed`.
  - `H-009` opened.
  - `H-010` opened.
- Confidence: high for `F-011`, medium-high for `F-012`, medium-high for `F-014`.
- Impact estimate: high overall, because this unit exposed both broad low-risk performance work and a distinct latent correctness defect.

### U-006 remaining `codegen_backend_arm64_uops.c` support-path audit
- Timestamp: 2026-04-20 23:25:12 EDT
- What this unit covers: the remaining ARM64 uop-lowering paths that actually exercise the newly identified emitter gaps, especially helper calls, imm stores, and stack-backed register traffic.
- What it does:
  - Drives most JIT helper calls (`CALL_FUNC`, `CALL_INSTRUCTION_FUNC`, mem load/store helpers, FP-round helpers).
  - Emits the ARM64-specific ad-hoc imm-store uops (`STORE_PTR_IMM`, `STORE_PTR_IMM_8`).
  - Implements stack and `cpu_state` direct-access helpers that the register allocator/writeback layer reaches indirectly.
- ARM64-specific logic observed:
  - Hot mem-helper and instruction-helper paths all funnel through `host_arm64_call()`, so the absolute-call cost from `F-011` is not isolated to a rare opcode family.
  - The only ARM64 imm-store lowering sites found in the backend are the narrow `STORE_PTR_IMM{,_8}` uops, both limited to in-range `cpu_state` destinations.
  - Stack-backed 64-bit / double reads and writes forward offsets directly into the unvalidated F64 load/store emitters, while 32-bit stack helpers still guard range explicitly.
- Risk points:
  - `R-008`: helper-heavy uops inherit the absolute-call and far-exit overheads from `F-011` / `F-012`.
  - `R-009`: the missing generic imm-store hook set is not theoretical; current ARM64 lowering only covers a small subset of destinations (`F-013`).
  - `R-010`: stack-backed FP/QWORD traffic can reach the unvalidated F64 stack helpers through allocator-driven load/writeback paths (`F-014`).
- Code examples:
  - Hot ARM64 helper-call site (`src/codegen_new/codegen_backend_arm64_uops.c:219-223`) with x86-64 contrast (`src/codegen_new/codegen_backend_x86-64_uops.c:220-229`):
    ```c
    host_arm64_mov_imm(block, REG_ARG0, uop->imm_data);
    host_arm64_call(block, uop->p);
    host_arm64_CBNZ(block, REG_X0, (uintptr_t) codegen_exit_rout);
    ```
    Why it matters: one callback path already combines all three ARM64-only costs discovered here: absolute call, possible long-range `CBNZ`, and a global exit stub.
  - Narrow ARM64 imm-store substitute (`src/codegen_new/codegen_backend_arm64_uops.c:2724-2745`):
    ```c
    host_arm64_mov_imm(block, REG_W16, uop->imm_data);
    if (in_range12_w((uintptr_t) uop->p - (uintptr_t) &cpu_state))
        host_arm64_STR_IMM_W(block, REG_W16, REG_CPUSTATE, ...);
    else
        fatal("codegen_STORE_PTR_IMM - not in range\n");
    ```
    Why it matters: ARM64 currently has a special-case imm-store path, but it is much narrower than the generic hook set x86-64 exposes.
  - Problematic ARM64 stack helpers (`src/codegen_new/codegen_backend_arm64_uops.c:3550-3576`):
    ```c
    void codegen_direct_read_64_stack(..., int stack_offset)
    {
        host_arm64_LDR_IMM_F64(block, host_reg, REG_XSP, stack_offset);
    }
    ```
    Why it matters: the stack helper itself performs no validation, so the correctness risk from `F-014` is fully wired into normal allocator traffic.
- Candidate fixes (proposal only; no implementation here):
  - `A-012`: implement the generic imm-store hook family so ARM64 can participate in the same dead-end immediate optimization as x86-64.
  - `A-013`: once relative `BL/B` exists, route the hot helper-call sites through it automatically for JIT-local targets.
  - `A-015`: validate or fall back the F64 stack helper path before new stack users expand it further.
- New/updated hypotheses:
  - `H-009` remains open.
  - `H-010` remains open.
- Confidence: high for `F-013`, medium-high for `F-014`.
- Impact estimate: high, because this audit ties the new emitter gaps directly to existing hot lowering paths.

### U-007 supporting module audit (`codegen_allocator.c/h`, `codegen_reg.c`, `codegen_ir_defs.h`, `codegen_backend_arm64.h`)
- Timestamp: 2026-04-20 23:28:50 EDT
- What this unit covers: the supporting modules that determine whether the newly surfaced ARM64 gaps are real implementation opportunities or merely theoretical emitter curiosities.
- What it does:
  - `codegen_allocator.c/h` defines the executable arena geometry and therefore the true branch/call reach available to the backend.
  - `codegen_reg.c` and `codegen_ir_defs.h` define the generic direct imm-store contract and the stack/direct-access paths used by allocator-driven register loads and writebacks.
  - `codegen_backend_arm64.h` exposes the public ARM64 backend surface seen by generic code.
- ARM64-specific logic observed:
  - The executable allocator maps one contiguous arena of `MEM_BLOCK_NR * MEM_BLOCK_SIZE`, which is ~120MB and therefore wholly inside ARM64's +/-128MB unconditional branch reach.
  - `codegen_reg_write_imm()` already expects four backend imm-store hooks; x86-64 satisfies that contract, but ARM64 still does not.
  - `codegen_backend_arm64.h` has drifted from the actual ops surface: it advertises `host_arm64_STRB_IMM_W` and `host_arm64_LDR_LITERAL_*`, while the active low-level header exports `host_arm64_STRB_IMM` and no literal-load implementation exists at this continuation point.
  - The suggested `src/codegen_new/codegen_timing.c` module is absent at both the original audited checkpoint and current HEAD `2e725bf5d`; timing hooks currently surface through `codegen_block.c` and `codegen.h` instead.
- Risk points:
  - `R-011`: JIT-local range guarantees make the missing relative-call/jump fast path a concrete missed optimization, not just a speculative idea (`F-011`).
  - `R-012`: the imm-store contract gap is broad enough to justify its own slice because it blocks a generic IR optimization end-to-end (`F-013`).
  - `R-013`: header drift can raise implementation risk and hide missing helpers during follow-up work (`H-010`).
- Code examples:
  - Range guarantee (`src/codegen_new/codegen_allocator.h:13-21` and `src/codegen_new/codegen_allocator.c:92-95`):
    ```c
    #define MEM_BLOCK_NR 131072
    #define MEM_BLOCK_SIZE 0x3c0
    mem_block_alloc = plat_mmap(MEM_BLOCK_NR * MEM_BLOCK_SIZE, 1);
    ```
    Why it matters: the whole JIT arena is ~120MB, so JIT-local ARM64 targets are always inside unconditional `B/BL` reach.
  - Generic imm-store contract (`src/codegen_new/codegen_ir_defs.h:915-918` and `src/codegen_new/codegen_reg.c:514-540`) with x86-64 contrast (`src/codegen_new/codegen_backend_x86-64_uops.c:3510-3527`):
    ```c
    void codegen_direct_write_8_imm(...);
    void codegen_direct_write_16_imm(...);
    void codegen_direct_write_32_imm(...);
    void codegen_direct_write_32_imm_stack(...);
    ```
    Why it matters: the generic optimizer is already wired for backend-specific imm stores; ARM64 is missing the actual implementation layer.
  - Header drift (`src/codegen_new/codegen_backend_arm64.h:19-25` versus `src/codegen_new/codegen_backend_arm64_ops.h:214-215`):
    ```c
    void host_arm64_LDR_LITERAL_W(...);
    void host_arm64_LDR_LITERAL_X(...);
    void host_arm64_STRB_IMM_W(...);
    ```
    Why it matters: the public ARM64 surface no longer matches the active emitter header, which increases follow-up implementation risk even if it is not yet a user-visible bug.
- Candidate fixes (proposal only; no implementation here):
  - `A-012`: use the already-existing generic imm-store contract rather than inventing a one-off ARM64-only fast path.
  - `A-013`: rely on the allocator geometry to make a relative-call/jump fast path bounded and reversible.
  - `A-016`: clean the public ARM64 header once the higher-value emitter slices are underway.
- New/updated hypotheses:
  - `H-002` moved to `parked`.
  - `H-010` moved to `confirmed`.
- Confidence: high.
- Impact estimate: high for implementation readiness, because these supporting modules turn several candidate ideas into concrete bounded slices.

### U-008 `codegen_ops_branch.c`
- Timestamp: 2026-04-21 16:24:31 EDT
- What this unit covers: the shared IR producer for conditional branches, `LOOP*`, and `JCXZ`, with emphasis on how often it emits the exact helper-call and exit-rout shapes that the ARM64 backend currently handles expensively.
- What it does:
  - Lowers most x86 conditional branches into shared compare/jump IR.
  - Chooses specialized `uop_CMP_J*` forms when flag provenance is known, but otherwise falls back to `uop_CALL_FUNC_RESULT`, immediate compare-branch uops, and a terminal `uop_JMP(ir, codegen_exit_rout)`.
  - Handles counter-based branch families (`JCXZ`, `LOOP`, `LOOPE`, `LOOPNE`) that can stack multiple compare branches before the final exit.
- ARM64-specific logic observed:
  - The frontend is shared, but its emission shape maps directly onto confirmed ARM64 backend costs: every fallback helper call eventually goes through `host_arm64_call()` (`F-011`), every compare-dest branch uses the patchable ARM64 branch templates (`F-012`), and every terminal exit targets the shared `codegen_exit_rout`.
  - Static counts over this one file found `28` explicit `uop_JMP(ir, codegen_exit_rout)` sites, `38` `uop_CALL_FUNC_RESULT` fallbacks, and `50` `uop_CMP_IMM_J{Z,NZ}_DEST` sites, making this a first-order frequency amplifier rather than a corner case.
  - `JE` / `JNE` retain a long-standing disabled-unroll note due to “the code sometimes taking the wrong turn,” so common equality branches are currently forced through the generic exit path even before backend lowering.
- Risk points:
  - `R-014`: branch-heavy guest code amplifies ARM64 absolute-call and far-exit costs because the shared frontend emits those expensive shapes at high density (`F-015`).
  - `R-015`: the disabled `JE` / `JNE` unroll path is both a latent shared correctness concern and a persistent performance tax (`F-016`).
- Code examples:
  - Problematic shared branch fallback (`src/codegen_new/codegen_ops_branch.c:57-65`):
    ```c
    uop_CALL_FUNC_RESULT(ir, IREG_temp0, VF_SET);
    jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
    uop_MOV_IMM(ir, IREG_pc, dest_addr);
    uop_JMP(ir, codegen_exit_rout);
    ```
    Why it matters: on ARM64, one fallback branch becomes an absolute helper call, a patchable compare/branch sequence, and then a jump to the global exit stub.
  - Problematic equality-branch path (`src/codegen_new/codegen_ops_branch.c:208-236`):
    ```c
    /* Temporarily disable the unrolling of JZ/JNZ due to the code sometimes taking the wrong turn. */
    ...
    uop_MOV_IMM(ir, IREG_pc, dest_addr);
    uop_JMP(ir, codegen_exit_rout);
    ```
    Why it matters: this is an explicit shared-frontend admission that a potentially cheaper no-exit path is disabled because of unresolved control-flow correctness problems.
  - Heavy helper-stacking case (`src/codegen_new/codegen_ops_branch.c:731-747`):
    ```c
    uop_CALL_FUNC_RESULT(ir, IREG_temp0, ZF_SET);
    jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
    uop_CALL_FUNC_RESULT(ir, IREG_temp0, NF_SET_01);
    uop_CALL_FUNC_RESULT(ir, IREG_temp1, VF_SET_01);
    ...
    uop_JMP(ir, codegen_exit_rout);
    ```
    Why it matters: a single guest signed branch can already stack three helper calls and multiple branches before the backend even begins lowering to ARM64.
- Candidate fixes (proposal only; no implementation here):
  - `A-013`: keep relative local `BL/B` as the first low-risk ARM64 response because this module proves the helper/jump sites are plentiful.
  - `A-014`: keep exit-veneer work on the table because this module emits enough exit-oriented control flow to justify future distance-based planning.
  - `A-018`: root-cause and, if safe, selectively restore the disabled `JE` / `JNE` unroll path.
  - `A-019`: investigate whether some `uop_CALL_FUNC_RESULT` fallbacks can be reduced by reusing already-materialized flag state.
- New/updated hypotheses:
  - `H-011` moved to `confirmed`.
- Confidence: high.
- Impact estimate: high, because this unit turns the ARM64 call/jump and control-flow gaps into front-and-center branch-workload issues rather than niche backend cleanup ideas.

### U-009 control-flow policy/support audit (`codegen_ops_helpers.c/h`, `codegen_ops_jump.c`, `codegen_ops_shift.c`, `codegen.c`, `codegen_block.c`)
- Timestamp: 2026-04-21 16:26:23 EDT
- What this unit covers: the shared policy and adjacent producer modules that determine when the frontend can avoid `codegen_exit_rout`, and when it must keep feeding helper calls and exit guards into the ARM64 backend.
- What it does:
  - `codegen_ops_helpers.h` / `.c` define whether backward branches can be unrolled and how aggressively that unrolling is capped.
  - `codegen_ops_jump.c` provides the contrast path where unconditional backward jumps can stay in-block and return the destination directly.
  - `codegen_ops_shift.c` and `codegen.c` show adjacent producer modules that still feed `codegen_exit_rout` guards and helper/instruction callbacks into the backend.
  - `codegen_block.c` proves that dirty-list recompiles preserve `CODEBLOCK_BYTE_MASK`, the exact flag that disables unroll at the frontend gate.
- ARM64-specific logic observed:
  - The logic is shared, but it disproportionately hurts ARM64 because the cheap no-exit path is disabled by `BYTE_MASK` exactly where ARM64 recompiles already pay cache/JIT overhead (`F-008`, `F-009`).
  - `codegen_can_unroll()` hard-stops on `CODEBLOCK_BYTE_MASK`, forward/out-of-block destinations, and cross-block cases before even reaching the fuller cap logic.
  - `codegen_can_unroll_full()` further caps successful unroll attempts at `1000` uops, `200` register references, and `10` iterations, shrinking the set of loops that can avoid exit-rout traffic.
  - `codegen_ops_shift.c` still emits direct `uop_CMP_IMM_JZ(..., codegen_exit_rout)` guards, and `codegen.c` keeps generic instruction bodies behind `uop_CALL_INSTRUCTION_FUNC`, so `codegen_ops_branch.c` is dominant but not literally alone.
- Risk points:
  - `R-016`: dirty/recompiled blocks lose unroll eligibility right when ARM64 recompilation is already more expensive, directly coupling frontend policy to backend pain (`F-016`).
  - `R-017`: the shared `JE` / `JNE` wrong-turn comment indicates a still-unresolved correctness issue blocks a potentially valuable performance path (`F-016`).
  - `R-018`: secondary producer modules continue to feed ARM64 absolute-call and exit-guard traffic outside the branch module (`F-015`).
- Code examples:
  - Problematic unroll gate (`src/codegen_new/codegen_ops_helpers.h:115-126`):
    ```c
    if (block->flags & CODEBLOCK_BYTE_MASK)
        return 0;
    if (dest_addr > next_pc)
        return 0;
    if ((cs + dest_addr) < block->pc)
        return 0;
    ```
    Why it matters: once a block has been promoted into `BYTE_MASK`, the shared frontend refuses the no-exit path outright.
  - Problematic unroll cap (`src/codegen_new/codegen_ops_helpers.c:38-74`):
    ```c
    #define UNROLL_MAX_REG_REFERENCES 200
    #define UNROLL_MAX_UOPS           1000
    #define UNROLL_MAX_COUNT          10
    ...
    codegen_ir_set_unroll(max_unroll, start, first_instruction);
    ```
    Why it matters: even backward branches that clear the early gate are tightly capped, limiting how often the cheaper direct path can be used.
  - Contrasting direct path (`src/codegen_new/codegen_ops_jump.c:26-29`):
    ```c
    if (offset < 0)
        codegen_can_unroll(block, ir, op_pc + 1, dest_addr);
    ...
    return dest_addr;
    ```
    Why it matters: when semantics are simple enough, the shared frontend already has a cheaper no-exit route; the policy problem is deciding how often other branches can safely use something similar.
  - Secondary producer examples (`src/codegen_new/codegen_ops_shift.c:581-582` and `src/codegen_new/codegen.c:749`):
    ```c
    uop_CMP_IMM_JZ(ir, IREG_temp2, 0, codegen_exit_rout);
    uop_CALL_INSTRUCTION_FUNC(ir, op, fetchdat);
    ```
    Why it matters: branch-heavy costs dominate, but adjacent producers still keep ARM64 helper-call and exit traffic alive outside `codegen_ops_branch.c`.
- Candidate fixes (proposal only; no implementation here):
  - `A-010`: keep churn reduction active because `BYTE_MASK` promotion now has a direct shared-frontend cost beyond code size and decode work.
  - `A-013`: retain relative local `BL/B` as the first low-risk response because even the remaining unavoidable helper-call traffic still funnels through ARM64 absolute-call paths today.
  - `A-018`: treat equality-branch unroll restoration as a later, shared-frontend slice that needs a correctness-first audit.
  - `A-019`: keep flag-helper call reduction as a broader, riskier follow-on if branch-heavy workloads remain dominated by `FLAGS_UNKNOWN` fallbacks after `A-013`.
- New/updated hypotheses:
  - `H-004` remains open, but the performance side is now clearly linked to `BYTE_MASK`/unroll policy as well as backend-local emitters.
- Confidence: high.
- Impact estimate: high for prioritization, because this unit shows why recompile churn and shared control-flow policy directly magnify ARM64 backend costs.

### U-010 secondary shared flag-helper producer audit (`codegen_ops_shift.c`, `codegen_ops_arith.c`)
- Timestamp: 2026-04-21 16:38:26 EDT
- What this unit covers: the next shared producer tier after `codegen_ops_branch.c`, focusing on rotate/shift flag rebuilds and carry-dependent arithmetic helper fetches.
- What it does:
  - `codegen_ops_shift.c` lowers rotate/shift families, including variable-count forms and `SHLD/SHRD`.
  - `codegen_ops_arith.c` lowers carry-dependent arithmetic and uses `get_cf()` as the shared helper entrypoint for CF materialization.
- ARM64-specific logic observed:
  - `codegen_ops_shift.c` contains `28` `uop_CALL_FUNC(ir, flags_rebuild)` sites and `3` direct zero-count exits to `codegen_exit_rout`, making it the clear second-tier helper/exit producer after the branch module.
  - In `CODEBLOCK_NO_IMMEDIATES` mode, `ropC1_l()` converts an immediate-count shift into a RAM load plus compare/jump scaffolding, so churn policy now affects shared shift lowering too.
  - `codegen_ops_arith.c` hides its helper traffic behind `get_cf()`, but that wrapper is just `uop_CALL_FUNC_RESULT(..., CF_SET)` and is referenced `38` times across ADC/SBB-style arithmetic.
  - These modules do not outrank `codegen_ops_branch.c`, but they broaden the scope of any later helper-elimination work and strengthen the case that `A-013` helps more than just branches.
- Risk points:
  - `R-019`: rotate/shift and carry-dependent arithmetic remain a broad second-tier source of ARM64 helper-call overhead (`F-017`).
  - `R-020`: `CODEBLOCK_NO_IMMEDIATES` adds extra shift-count load/compare scaffolding, extending the churn cost model beyond the branch module (`F-017`).
- Code examples:
  - Problematic shift helper path (`src/codegen_new/codegen_ops_shift.c:27-35`):
    ```c
    uop_CALL_FUNC(ir, flags_rebuild);
    uop_ROL_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), count);
    ```
    Why it matters: rotate families still pay an out-of-line helper call before the backend lowers the actual data-path instruction.
  - Problematic `NO_IMMEDIATES` shift path (`src/codegen_new/codegen_ops_shift.c:495-506`):
    ```c
    LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp2, cs + op_pc + 1);
    uop_AND_IMM(ir, IREG_temp2, IREG_temp2, 0x1f);
    jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp2, 0);
    ```
    Why it matters: once churn policy disables embedded immediates, even a simple immediate shift picks up extra memory-load and control-flow scaffolding.
  - Direct zero-count exit guard (`src/codegen_new/codegen_ops_shift.c:581-582`):
    ```c
    uop_AND_IMM(ir, IREG_temp2, REG_ECX, 0x1f);
    uop_CMP_IMM_JZ(ir, IREG_temp2, 0, codegen_exit_rout);
    ```
    Why it matters: variable-count shifts still feed direct exit branches into the same global ARM64 exit path.
  - Problematic arithmetic helper wrapper (`src/codegen_new/codegen_ops_arith.c:18-22` and `29-33`):
    ```c
    static inline void
    get_cf(ir_data_t *ir, int dest_reg)
    {
        uop_CALL_FUNC_RESULT(ir, dest_reg, CF_SET);
    }
    ```
    Why it matters: carry-dependent arithmetic hides a broad helper-call surface behind one wrapper, so its ARM64 cost is easy to underestimate if only direct `uop_CALL_FUNC_RESULT` sites are counted.
- Candidate fixes (proposal only; no implementation here):
  - `A-013`: keep relative local `BL/B` ahead of semantic rewrites because it benefits every helper family already found in branch, shift, arithmetic, and generic instruction dispatch.
  - `A-019`: if branch-heavy traces remain dominated by fallback helpers after `A-013`, widen the flag-helper audit beyond branches.
  - `A-020`: if helper-call overhead still matters after `A-013`, investigate bounded fast paths for `flags_rebuild` and `get_cf()` users.
- Confidence: high.
- Impact estimate: medium-high, because this unit proves the helper-call problem is broader than the branch module even though the branch module still dominates explicit exit traffic.

### U-011 remaining tail-producer sweep (`codegen_ops_misc.c`, `codegen_ops_stack.c`, residual `codegen_ops_jump.c`)
- Timestamp: 2026-04-21 16:52:32 EDT
- What this unit covers: the final shared-helper source sweep needed to close the static audit, focusing on the remaining unranked tail modules and the already-sampled `jump` contrast path.
- What it does:
  - `codegen_ops_misc.c` lowers grouped integer/control opcodes (`F6/F7/FF`), segment-load forms, and flag-toggle instructions.
  - `codegen_ops_stack.c` lowers push/pop, segment stack transfers, `PUSHA`/`POPA`, `LEAVE`, and flag-stack serialization.
  - `codegen_ops_jump.c` lowers near/far `JMP`, `CALL`, `RET`, `RETF`, and related control transfers.
- ARM64-specific logic observed:
  - These modules are shared, but their helper sites still map onto confirmed ARM64-local costs because every `loadcs` / `loadcsjmp` / `flags_rebuild*` call inherits the absolute `host_arm64_call()` path from `F-011`.
  - `codegen_ops_misc.c` contains only six relevant helper sites, concentrated in carry-preserving `INC/DEC` rebuilds, far `FF /5` jumps, and `CLC` / `CMC` / `STC`.
  - `codegen_ops_jump.c` also contains only six helper sites, and they are all in far control-transfer paths; near backward jumps already use `codegen_can_unroll()` and return the destination directly.
  - `codegen_ops_stack.c` is almost entirely direct memory traffic; only `PUSHF` and `PUSHFD` force a `flags_rebuild` helper call before serializing flags to the guest stack.
- Risk points:
  - `R-021`: even tail helper sites still benefit from `A-013`, so the relative `BL/B` slice has slightly wider reach than the earlier branch/shift/arith units alone suggested (`F-018`).
  - `R-022`: any dedicated optimization inside these modules would be niche and semantically risky because it would touch far control transfer, segment loading, or flag serialization rather than broad integer hot paths (`F-018`).
- Code examples:
  - Tail helper in `misc` (`src/codegen_new/codegen_ops_misc.c:278-280` and `366-368`):
    ```c
    if (needs_rebuild) {
        uop_CALL_FUNC(ir, flags_rebuild_c);
    }
    ...
    uop_LOAD_FUNC_ARG_REG(ir, 0, IREG_temp1_W);
    uop_LOAD_FUNC_ARG_IMM(ir, 1, op_pc + 1);
    uop_CALL_FUNC(ir, loadcsjmp);
    ```
    Why it matters: the remaining `misc` helpers are real, but they are tied to carry-preserving `INC/DEC` and far jumps rather than dense general-purpose hot paths.
  - Tail helper in `stack` (`src/codegen_new/codegen_ops_stack.c:390-396` and `408-422`):
    ```c
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_CALL_FUNC(ir, flags_rebuild);
    ...
    uop_MEM_STORE_REG(ir, IREG_SS_base, sp_reg, IREG_flags);
    ```
    Why it matters: the stack module does not create a broad new helper family; it mostly stays on direct loads/stores and only serializes flags through a helper in `PUSHF` / `PUSHFD`.
  - Contrast in `jump` between the cheap path and the helper-heavy far path (`src/codegen_new/codegen_ops_jump.c:26-29` and `211-213`):
    ```c
    if (offset < 0)
        codegen_can_unroll(block, ir, op_pc + 1, dest_addr);
    return dest_addr;
    ...
    uop_LOAD_FUNC_ARG_REG(ir, 0, IREG_temp1_W);
    uop_CALL_FUNC(ir, loadcs);
    ```
    Why it matters: `jump` already demonstrates the cheaper shared path for simple backward branches, while its helper calls are isolated to far control transfer instead of dominating normal near-branch traffic.
- Candidate fixes (proposal only; no implementation here):
  - Keep `A-013` as the relevant ARM64-local response because it improves every helper family still present in these tail modules without adding a dedicated niche slice.
  - Do not promote `misc` / `jump` / `stack` into a standalone first-wave optimization area ahead of `S-01`, `S-02`, `S-03`, `A-014`, `A-018`, or `A-020`.
  - `A-021`: only if post-`A-013` workload traces still show far control transfer or flag serialization as hot, investigate bounded cleanups for `loadcs` / `loadcsjmp` and `flags_rebuild_c` / `flags_rebuild`.
- New/updated hypotheses:
  - `H-004`, `H-006`, and `H-009` remain open, but this unit closes static source discovery for them; only runtime telemetry can move their ranking further.
- Confidence: high.
- Impact estimate: medium for planning readiness, because this unit closes the last remaining static source sweep and prevents overcommitting to low-density tail modules.

### U-012 post-sync delta review (`codegen_ops_jit_wrappers.h`, shared `codegen_ops_*`, `codegen_backend_arm64_uops.c`, `codegen_block.c`)
- Timestamp: 2026-04-21 17:18:28 EDT
- What this unit covers: the focused current-HEAD recheck required after upstream sync moved the branch beyond the finished static-audit baseline.
- What it does:
  - Re-audits the new `codegen_ops_jit_wrappers.h` layer and its adoption in the shared flag-helper producer modules.
  - Rechecks the ARM64 `codegen_MOV()` lowering changes that landed after the original audit closeout.
  - Reviews the new deterministic round-robin block-eviction change in `codegen_block.c` to decide whether it affects ARM64 planning priorities.
- ARM64-specific logic observed:
  - `codegen_ops_jit_wrappers.h` now creates stable `noinline, used` call targets around address-taken `static __inline` flag helpers, and the affected producer modules include that header instead of passing raw inline-helper addresses into `uop_CALL_FUNC*`.
  - ARM64 `codegen_MOV()` now covers `W<-L`, `B<-L`, and `B<-W` truncation pairs with `BFI`, reducing one latent fatal path in the backend.
  - The new round-robin eviction logic improves intra-run determinism, but the function-local `evict_probe` state survives `codegen_init()` and `codegen_reset()`, so reproducibility does not extend across resets within the same process.
- Risk points:
  - `R-023`: the wrapper layer removes a real ARM64 correctness hazard from the current baseline, so planning should not allocate a new slice to it unless later audits uncover more unwrapped address-taken inline helpers (`F-019`).
  - `R-024`: the missing ARM64 `MOV` truncation gap has narrowed at current HEAD, reducing one earlier latent backend correctness risk from the audit baseline (`F-020`).
  - `R-025`: block eviction is still only process-deterministic, not reset-deterministic, so measurement reproducibility remains slightly incomplete (`F-021`).
- Code examples:
  - New wrapper site with underlying inline target (`src/codegen_new/codegen_ops_jit_wrappers.h:24-33` and `src/cpu/x86_flags.h:496-525`):
    ```c
    static JIT_WRAPPER void
    jit_flags_rebuild(void)
    {
        flags_rebuild();
    }
    ...
    static __inline void
    flags_rebuild(void)
    {
        if (cpu_state.flags_op != FLAGS_UNKNOWN) {
    ```
    Why it matters: current HEAD now routes JIT-callable flag rebuilds through a stable out-of-line symbol instead of taking the address of a `static __inline` body directly.
  - New ARM64 truncation coverage (`src/codegen_new/codegen_backend_arm64_uops.c:1170-1175`):
    ```c
    } else if (REG_IS_W(dest_size) && REG_IS_L(src_size)) {
        host_arm64_BFI(block, dest_reg, src_reg, 0, 16);
    } else if (REG_IS_B(dest_size) && REG_IS_L(src_size)) {
        host_arm64_BFI(block, dest_reg, src_reg, 0, 8);
    } else if (REG_IS_B(dest_size) && REG_IS_W(src_size)) {
        host_arm64_BFI(block, dest_reg, src_reg, 0, 8);
    }
    ```
    Why it matters: three previously unhandled truncation paths no longer fall through to `fatal()`.
  - Partially deterministic eviction state (`src/codegen_new/codegen_block.c:438-446` and `218-258`):
    ```c
    static int evict_probe = 0;
    ...
    evict_probe = (block_nr + 1) & BLOCK_MASK;
    ```
    ```c
    void codegen_reset(void)
    {
        ...
        memset(codeblock, 0, BLOCK_SIZE * sizeof(codeblock_t));
    }
    ```
    Why it matters: the round-robin state is advanced, but it is never reset alongside the rest of dynarec block state.
- Candidate fixes (proposal only; no implementation here):
  - Treat the wrapper and `MOV` truncation fixes as resolved baseline, not as new planning slices.
  - `A-022`: if benchmarking reproducibility becomes important, move `evict_probe` into resettable file scope and clear it from `codegen_init()` / `codegen_reset()`.
  - Keep the implementation order unchanged: `S-01`, `S-02`, `S-03`, then `A-013`.
- New/updated hypotheses:
  - No new hypothesis IDs added in this unit.
  - `H-004`, `H-006`, and `H-009` remain open; `U-012` only changes the current-HEAD baseline, not the runtime-only tie-breaks for those hypotheses.
- Confidence: high.
- Impact estimate: medium for planning fidelity, because this unit aligns the finished audit with current HEAD and removes two stale correctness concerns without reopening the broader backlog.

## Known Unknowns
- Static source sweep and current-HEAD delta review are complete; remaining unknowns are runtime-only except for the optional allocator reproducibility cleanup in `A-022`.
- Whether recent historical ARM64 regressions were accompanied by preserved logfiles and metadata on disk; no historical run was referenced in this session, so logfile/metadata status remains unknown.
- Whether hot compiled blocks regularly end up more than 1MB from `codegen_exit_rout`, or whether block reuse keeps most hot paths close enough that `F-012` matters less than the static structure suggests.
- Whether ARM64 helper-heavy workloads are bottlenecked more by absolute-call materialization (`F-011`), long abort branches (`F-012`), or compile/recompile churn (`F-008` / `F-009`).
- Whether ARM64 `codegen_fp_round` / `codegen_fp_round_quad` calls occur often enough in representative workloads to compete with the newly found helper-dispatch and imm-store gaps.
- Whether enabling ARM64 `MOV_IMM` fast-path support can safely ship in two bounded sub-steps (direct imm-store hooks first, smarter constant materialization second) without needing the broader range-fallback work up front.
- Whether shift/arithmetic helper pressure remains materially visible after `A-013`, or if it mainly serves to strengthen the case for that backend-local call/jump optimization.
- Whether the JE/JNE “wrong turn” bug is frontend-generic, block-layout-sensitive, or meaningfully worse on ARM64 because of mem-block splits and higher exit costs.
- Whether stack layouts can realistically grow enough to trigger `F-014` without deliberate stress, or whether that slice should remain correctness hygiene rather than a hot bug fix.
- Whether any existing workload is known to emit `UOP_MMX_ENTER` near a mem-block boundary often enough to make `F-005` user-visible today.
- Whether far-control-transfer or flag-serialization-heavy workloads are common enough to justify `A-021` after `A-013`, or whether the tail-module helper sites should remain a purely ranked footnote.
- Whether making round-robin eviction reset-deterministic is worth a cleanup patch for benchmarking hygiene, or whether `codegen_delete_random_block()` is too cold for `A-022` to matter in practice.

## Rejected Hypotheses
- H-005 rejected at 2026-04-20 22:58:23 EDT: `codegen_allocator_clean_blocks()` is not freeing or invalidating helper memory. `src/codegen_new/codegen_allocator.c:190-200` shows an ARM64-only `__clear_cache()` loop over generated blocks, which makes the ARM64 init/epilogue call sites a cache-coherency requirement rather than a lifetime bug. Evidence: `E-012`, `E-013`.

## State Snapshot
### Snapshot 2026-04-21 17:18:28 EDT
- Current focus: static ARM64 audit complete and delta-reviewed against current HEAD; handoff ready for implementation planning
- Completed units: `U-000`, `U-001`, `U-002`, `U-003`, `U-004`, `U-005`, `U-006`, `U-007`, `U-008`, `U-009`, `U-010`, `U-011`, `U-012`
- Active findings: `F-001` through `F-021`
- Active decisions: `D-001` through `D-020`
- Active hypotheses:
  - `H-001` parked
  - `H-002` parked
  - `H-003` confirmed
  - `H-004` open
  - `H-005` rejected
  - `H-006` open
  - `H-007` confirmed
  - `H-008` confirmed
  - `H-009` open
  - `H-010` confirmed
  - `H-011` confirmed
- Next commit-ready investigation move: none required for source discovery; start the written implementation plan from `S-01`, `S-02`, `S-03`, and `A-013`, treating `F-019` / `F-020` as current-HEAD fixes and keeping `A-022` as optional measurement-hygiene follow-up.

### Snapshot 2026-04-21 16:52:32 EDT
- Current focus: static ARM64 source audit complete; handoff ready for implementation planning
- Completed units: `U-000`, `U-001`, `U-002`, `U-003`, `U-004`, `U-005`, `U-006`, `U-007`, `U-008`, `U-009`, `U-010`, `U-011`
- Active findings: `F-001` through `F-018`
- Active decisions: `D-001` through `D-018`
- Active hypotheses:
  - `H-001` parked
  - `H-002` parked
  - `H-003` confirmed
  - `H-004` open
  - `H-005` rejected
  - `H-006` open
  - `H-007` confirmed
  - `H-008` confirmed
  - `H-009` open
  - `H-010` confirmed
  - `H-011` confirmed
- Next commit-ready investigation move: none required for static discovery; start the written implementation plan from `S-01`, `S-02`, `S-03`, `A-013`, and the final tail ranking in `F-018`.

### Snapshot 2026-04-21 16:39:15 EDT
- Current focus: session end checkpoint complete; next recommended unit is `U-011` / `src/codegen_new/codegen_ops_misc.c`
- Completed units: `U-000`, `U-001`, `U-002`, `U-003`, `U-004`, `U-005`, `U-006`, `U-007`, `U-008`, `U-009`, `U-010`
- Active findings: `F-001` through `F-017`
- Active decisions: `D-001` through `D-016`
- Active hypotheses:
  - `H-001` parked
  - `H-002` parked
  - `H-003` confirmed
  - `H-004` open
  - `H-005` rejected
  - `H-006` open
  - `H-007` confirmed
  - `H-008` confirmed
  - `H-009` open
  - `H-010` confirmed
  - `H-011` confirmed
- Next commit-ready investigation move: inspect `codegen_ops_misc.c` first, then compare it with `codegen_ops_stack.c` and the already-sampled `codegen_ops_jump.c`, to finish ranking the remaining shared helper producers.

### Snapshot 2026-04-21 16:38:26 EDT
- Current focus: `U-010` / secondary shared flag-helper producer audit
- Completed units: `U-000`, `U-001`, `U-002`, `U-003`, `U-004`, `U-005`, `U-006`, `U-007`, `U-008`, `U-009`
- Active findings: `F-001` through `F-017`
- Active decisions: `D-001` through `D-016`
- Active hypotheses:
  - `H-001` parked
  - `H-002` parked
  - `H-003` confirmed
  - `H-004` open
  - `H-005` rejected
  - `H-006` open
  - `H-007` confirmed
  - `H-008` confirmed
  - `H-009` open
  - `H-010` confirmed
  - `H-011` confirmed
- Next commit-ready investigation move: quantify `codegen_ops_shift.c` and `codegen_ops_arith.c` helper density, then decide whether any remaining shared producer deserves promotion above a footnote.

### Snapshot 2026-04-21 16:26:23 EDT
- Current focus: session end checkpoint complete; next recommended unit is `U-010` / `src/codegen_new/codegen_ops_shift.c`
- Completed units: `U-000`, `U-001`, `U-002`, `U-003`, `U-004`, `U-005`, `U-006`, `U-007`, `U-008`, `U-009`
- Active findings: `F-001` through `F-016`
- Active decisions: `D-001` through `D-015`
- Active hypotheses:
  - `H-001` parked
  - `H-002` parked
  - `H-003` confirmed
  - `H-004` open
  - `H-005` rejected
  - `H-006` open
  - `H-007` confirmed
  - `H-008` confirmed
  - `H-009` open
  - `H-010` confirmed
  - `H-011` confirmed
- Next commit-ready investigation move: inspect `codegen_ops_shift.c` first, then `codegen_ops_arith.c`, to determine how much non-branch shared-frontend helper/exit traffic remains after the dominant `codegen_ops_branch.c` findings.

### Snapshot 2026-04-21 16:24:31 EDT
- Current focus: `U-009` / control-flow policy and supporting producer audit
- Completed units: `U-000`, `U-001`, `U-002`, `U-003`, `U-004`, `U-005`, `U-006`, `U-007`, `U-008`
- Active findings: `F-001` through `F-015`
- Active hypotheses:
  - `H-001` parked
  - `H-002` parked
  - `H-003` confirmed
  - `H-004` open
  - `H-005` rejected
  - `H-006` open
  - `H-007` confirmed
  - `H-008` confirmed
  - `H-009` open
  - `H-010` confirmed
  - `H-011` confirmed
- Next commit-ready investigation move: audit `codegen_ops_helpers.c/h`, `codegen_ops_jump.c`, `codegen_ops_shift.c`, `codegen.c`, and `codegen_block.c` together to explain when the shared frontend can avoid `codegen_exit_rout` and when it cannot.

### Snapshot 2026-04-20 23:28:50 EDT
- Current focus: session end checkpoint complete; next recommended unit is `U-008` / `src/codegen_new/codegen_ops_branch.c`
- Completed units: `U-000`, `U-001`, `U-002`, `U-003`, `U-004`, `U-005`, `U-006`, `U-007`
- Active findings: `F-001` through `F-014`
- Active decisions: `D-001` through `D-013`
- Active hypotheses:
  - `H-001` parked
  - `H-002` parked
  - `H-003` confirmed
  - `H-004` open
  - `H-005` rejected
  - `H-006` open
  - `H-007` confirmed
  - `H-008` confirmed
  - `H-009` open
  - `H-010` confirmed
- Next commit-ready investigation move: inspect `codegen_ops_branch.c` first to map how often the newly confirmed ARM64 helper-call and long-branch patterns are produced before deciding whether `A-013` or `A-014` should follow `S-02`.

### Snapshot 2026-04-20 23:25:12 EDT
- Current focus: `U-007` / supporting module audit
- Completed units: `U-000`, `U-001`, `U-002`, `U-003`, `U-004`, `U-005`, `U-006`
- Active findings: `F-001` through `F-014`
- Active hypotheses:
  - `H-001` parked
  - `H-002` open
  - `H-003` confirmed
  - `H-004` open
  - `H-005` rejected
  - `H-006` open
  - `H-007` confirmed
  - `H-008` confirmed
  - `H-009` open
  - `H-010` open
- Next commit-ready investigation move: fold allocator and reg-contract evidence back into the canonical backlog, then choose between `codegen_ops_branch.c` and `codegen_block.c` for the next frequency/telemetry-focused unit.

### Snapshot 2026-04-20 23:21:14 EDT
- Current focus: `U-006` / remaining `codegen_backend_arm64_uops.c` support-path audit
- Completed units: `U-000`, `U-001`, `U-002`, `U-003`, `U-004`, `U-005`
- Active findings: `F-001` through `F-012`
- Active hypotheses:
  - `H-001` parked
  - `H-002` open
  - `H-003` confirmed
  - `H-004` open
  - `H-005` rejected
  - `H-006` open
  - `H-007` confirmed
  - `H-008` confirmed
  - `H-009` open
- Next commit-ready investigation move: trace the helper-call and stack-writeback paths that actually reach the new emitter gaps, then update findings before switching into the supporting modules.

### Snapshot 2026-04-20 23:16:08 EDT
- Current focus: `U-005` / `src/codegen_new/codegen_backend_arm64_ops.c`
- Completed units: `U-000`, `U-001`, `U-002`, `U-003`, `U-004`
- Active findings: `F-001` through `F-010`
- Active hypotheses:
  - `H-001` parked
  - `H-002` open
  - `H-003` confirmed
  - `H-004` open
  - `H-005` rejected
  - `H-006` open
  - `H-007` open
- Next commit-ready investigation move: inspect the raw emitter layer for missing relative-call/jump paths, long-range branch templates, and any load/store helpers that skip the range validation seen elsewhere.

### Snapshot 2026-04-20 22:54:04 EDT
- Current focus: `A-001` / `src/codegen_new/codegen_backend_arm64.c`
- Completed units: `U-000`
- Active findings: `F-001`
- Active hypotheses:
  - `H-001` open
  - `H-002` open
  - `H-003` open
  - `H-004` open
- Next commit-ready investigation move: capture `codegen_backend_arm64.c` entry points, helper generation, and any block lifecycle code with line-precise evidence, then update backlog and resume state before switching files.

## Decision-Ready Plan
### Top 3 Implementation Slices
| Slice ID | Priority | Problem to address | Exact files to touch | Gate criteria | Rollback conditions |
| --- | --- | --- | --- | --- | --- |
| S-01 | P0 | Fix the ARM64 `codegen_MMX_ENTER()` stale-buffer branch patch so split-block emission cannot jump to the wrong fragment. | `src/codegen_new/codegen_backend_arm64_uops.c` | Confirm the branch patch uses `block_write_data` consistently; audit nearby ARM64 patch sites for the same pattern; define a targeted future test plan that forces `MMX_ENTER` near a mem-block boundary and records whether logfile and metadata are written to disk. | If a deeper audit proves `MMX_ENTER` can never span a mem-block boundary in practice, or if the fix reveals a different required patching abstraction. |
| S-02 | P1 | Add ARM64 support for the missing `MOV_IMM` direct-to-memory fast path, then immediately fold in cheaper constant materialization where the same slice already touches the emitter. | `src/codegen_new/codegen_backend_arm64.h`, `src/codegen_new/codegen_backend_arm64_uops.c`, `src/codegen_new/codegen_backend_arm64_ops.c`, optionally `src/codegen_new/codegen_backend_arm64_imm.c` if helper selection is shared | Land the four direct imm-store hooks first; keep current range-safety assumptions explicit; if the same patch series touches `host_arm64_mov_imm()`, prefer bounded additions (`MOVN`, logical-immediate move) over larger refactors; define a future validation plan that compares code size / helper count before vs after and records logfile/metadata destinations. | If implementation needs a large generic addressing refactor first, or if combining imm-store work with smarter materialization makes the slice materially riskier than expected. |
| S-03 | P1 | Reduce churn from dirty-list escalation that currently drives shorter `BYTE_MASK` blocks, `NO_IMMEDIATES`, cache flushes, and macOS JIT toggles. | `src/cpu/386_dynarec.c`, `src/codegen_new/codegen_block.c`, optionally `src/codegen_new/codegen.h` if flag semantics change | Define the exact heuristic change first: delay escalation, add counters/thresholds, or constrain it to confirmed hot dirty blocks; pair it with a future SMC-focused test plan and explicit logfile/metadata capture. | If self-modifying-code workloads regress, invalidation frequency rises, or the heuristic increases correctness risk more than it reduces ARM64 recompile cost. |

### First Slice Recommended
- Recommend `S-01` first.
- Why first:
  - It is the highest-confidence correctness issue found in this session.
  - The write set is tiny and bounded.
  - The rollback surface is low compared with the broader heuristic/performance slices.

### Next Slice Recommended
- Recommend `S-02` immediately after `S-01`.
- Suggested internal sub-order:
  - `A-012` direct imm-store hooks first.
  - `A-011` smarter ARM64 constant materialization second if the emitter files are already open.
  - `A-013` relative `BL/B` fast paths next as the strongest new low-risk/high-return non-top-3 slice.

### Sequencing Notes
- `S-02` remains the best broad performance follow-up once `S-01` is closed because it improves a generic inefficiency without needing to change dynarec policy.
- `A-013` relative `BL/B` is now the best additional low-risk/high-return slice outside the retained top three, but it is narrower than `S-02` and therefore still ranks just after it.
- `A-014` exit/conditional veneers now has clear evidence behind it, but it should stay behind `S-02` and `A-013` until IR-frequency evidence from `codegen_ops_branch.c` confirms how often the long-range forms are emitted.
- `A-018` JE/JNE unroll restoration is now a real backlog item, but it should stay behind `A-013` and `A-014` because the current shared-frontend comment already documents a correctness risk.
- `A-019` flag-helper reduction should remain a later shared-frontend slice: the branch producer density now justifies investigating it, but it is still riskier than the simpler ARM64-local call/jump improvements.
- `A-020` shift/arithmetic flag-helper elimination should stay behind `A-013` as well; the new evidence broadens the helper-call problem, but the lowest-risk answer is still to cheapen helper dispatch before rewriting flag semantics.
- `A-021` should stay behind `A-013` and the existing shared-frontend work; the final tail sweep showed `misc` / `jump` / `stack` helper sites are real but too sparse and niche to justify a dedicated early slice.
- Current HEAD already includes the wrapper fix captured in `F-019` and the missing ARM64 `MOV` truncation coverage in `F-020`, so the implementation plan should treat those as resolved baseline rather than as new slices.
- `A-022` is intentionally outside the main wave; it can improve benchmarking reproducibility, but it does not change expected end-user performance or correctness nearly as much as `S-01`, `S-02`, `S-03`, or `A-013`.
- `S-03` should still come after `S-01` and ideally after an initial `S-02` design pass, because the churn slice is the most policy-heavy and benefits from having the lower-level ARM64 cost model already improved.

## Session Delta
- Added/changed in this session:
  - Advanced the audited baseline to HEAD `2e725bf5d65de5e6778f722e645200ffdb4029c1` after the upstream sync merge and completed `U-012`, a focused delta review over the new wrapper layer, ARM64 `MOV` lowering changes, and block-eviction updates.
  - Refreshed `Resume Here`, `Scope`, the ARM64/shared file map, `Running Prioritized Backlog`, `State Snapshot`, `Decision-Ready Plan`, and `Session Delta` so the document now describes current HEAD rather than the pre-sync docs-only checkpoint.
  - Added findings `F-019` through `F-021`, decisions `D-019` and `D-020`, evidence `E-066` through `E-072`, progress entry `U-012`, and backlog item `A-022`.
  - Recorded that current HEAD already fixes the ARM64 JIT-wrapper hazard and the missing ARM64 `MOV` truncation cases, while the round-robin eviction change remains only partly reset-deterministic.
- Unresolved:
  - No runtime validation plan has been executed; all validation remains plan-only, and no historical run logfile/metadata pair was referenced in this investigation.
  - `H-004`, `H-006`, and `H-009` remain open because churn frequency, FP-round hotness, and far-branch prevalence are runtime-only questions at this point.
  - The JE/JNE “wrong turn” root cause is still unknown, so `A-018` remains deliberately behind lower-risk ARM64-local slices.
  - Whether `A-022` is worth implementing remains open; it is currently classified as measurement hygiene rather than a first-order ARM64 slice.
  - Future telemetry work still needs a new anchor because `src/codegen_new/codegen_timing.c` does not exist at HEAD `2e725bf5d`.
- Exact first commands/files for next session:
  - `rg -n "Running Prioritized Backlog|Findings Summary|Decision-Ready Plan|Session Delta" docs/arm64-dynarec-investigation.md`
  - `rg -n "S-01|S-02|S-03|A-013|A-022|F-019|F-020|F-021|U-012" docs/arm64-dynarec-investigation.md`
  - `sed -n '1,140p' docs/arm64-dynarec-investigation.md`
  - File focus: `docs/arm64-dynarec-investigation.md`
- Recommended immediate next investigation unit: none. Recommended immediate next phase: implementation planning from `S-01`, `S-02`, `S-03`, and `A-013`, using `U-012` only as the current-HEAD guardrail against reopening already fixed upstream items.
- Broader issue inventory sufficient for multi-slice implementation planning: yes. The static audit plus current-HEAD delta review now appears complete and broad enough to support multi-slice implementation planning without another discovery pass.
