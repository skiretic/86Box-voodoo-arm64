# ARM64 New Dynarec Investigation

## Resume Here
- Current objective: analyze ARM64-specific new-dynarec modules for performance loss, incorrect behavior/instability, and avoidable fallback/churn; convert evidence into a prioritized implementation plan.
- Exact next file/module: `src/codegen_new/codegen_backend_arm64_imm.c`
- Next 3 concrete actions:
  1. Inspect `codegen_backend_arm64_imm.c` for constant-materialization patterns that may worsen the missing ARM64 `MOV_IMM` fast path or inflate instruction count.
  2. Check whether immediate-selection logic already offers reusable pieces for `A-008`, or whether new direct-store helpers would need separate machinery.
  3. Update backlog, findings, and a fresh `State Snapshot` immediately after the immediate-materialization analysis completes.
- Active blockers: no runtime evidence collected in this session by design; any execution validation remains plan-only.

## Scope
- Session start: 2026-04-20 22:54:04 EDT
- Repository: `/Users/anthony/projects/code/86Box-voodoo-arm64`
- Branch: `ndr-analysis`
- HEAD: `123c135cf5ce703e0a406e88f6f49677370e7cd7`
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
| Dynarec integration boundary | `src/cpu/386_dynarec.c`, `src/cpu/386_dynarec_ops.c` | Governs block lookup/validation/end conditions and can reveal avoidable recompile churn or cache invalidation behavior observable on ARM64 as backend symptoms. | `E-009` |

## Hypothesis Register
| Hypothesis ID | Status | Statement | Initial basis | Planned tie-break / next step |
| --- | --- | --- | --- | --- |
| H-001 | parked | ARM64 helper stub generation may be forcing too many slow-path calls on misaligned or lookup-miss memory accesses, causing measurable performance loss beyond expected x86 semantics. | `codegen_backend_arm64.c` emits dedicated load/store stubs with explicit `readlookup2`/`writelookup2` probes and helper-call fallback branches. | `codegen_backend_x86-64.c` uses the same misalignment-triggered helper pattern, so this is not an ARM64-specific first-slice explanation without stronger evidence. Revisit only if later ARM64-only behavior shows materially higher fallback frequency. |
| H-002 | open | ARM64 host register pool size and fixed register assignments may create excess spills/reloads or limit scheduling freedom in hot blocks. | Only 10 integer callee-saved registers and 8 FP registers are exposed as allocatable host pools, with `X29` pinned as `REG_CPUSTATE`. | Confirm how allocator-facing pools are consumed in backend code and whether common helpers further reserve temps. |
| H-003 | open | ARM64 immediate materialization may inflate instruction count or churn literal construction in common paths, contributing to backend overhead. | Dedicated immediate file exists and exported `host_arm64_find_imm()` suggests special-case search/materialization logic. | Inspect `codegen_backend_arm64_imm.c` and call sites after backend scaffolding review. |
| H-004 | open | Some user-visible ARM64 instability may originate at the block validation/recompile boundary rather than in instruction semantics, appearing as backend faults or random slowdowns. | `386_dynarec.c` contains extensive block validation, dirty-page, and block-end logic that can cause recompilation or fallback churn. | Inspect block lifecycle paths after first backend module analysis and separate frontend vs backend causes. |
| H-005 | rejected | `codegen_allocator_clean_blocks()` in the ARM64 backend might be freeing or invalidating the shared helper block immediately after initialization, causing latent corruption. | ARM64 `codegen_backend_init()` calls `codegen_allocator_clean_blocks(block->head_mem_block)` while x86-64 init does not. | Rejected after allocator inspection: the function only performs `__clear_cache()` on ARM64 and does not free memory. |
| H-006 | open | ARM64 FP-to-int conversions may pay avoidable overhead because rounding is implemented as out-of-line helper calls plus an indirect jump-table dispatch. | `build_fp_round_routine()` builds shared rounding helpers, and ARM64 uops call them for `MOV_INT_DOUBLE` / `MOV_INT_DOUBLE_64`. | Current call-site inventory narrows this to three conversion sites in `codegen_backend_arm64_uops.c`; keep open, but prioritize lower than the MMX branch-patch bug and the missing ARM64 `MOV_IMM` fast path. |
| H-007 | open | ARM64 direct state/stack access helpers may become a latent instability source because they `fatal()` on immediate-range overflow instead of falling back to a generic address-materialization path. | `codegen_direct_read_*` / `codegen_direct_write_*` helpers encode fixed-range loads/stores from `REG_CPUSTATE` or `SP` and hard-fail when offsets exceed the encoding window. | Current call-site audit suggests existing uses stay within `cpu_state` fields and small stack slots; keep as a robustness item unless future layout growth reduces margin. |

## Running Prioritized Backlog
| Priority | Action ID | Status | Candidate improvement | Expected win | Risk | Validation plan | Rollback trigger |
| --- | --- | --- | --- | --- | --- | --- | --- |
| P0 | A-001 | completed | Complete evidence-backed analysis of `codegen_backend_arm64.c` before proposing implementation slices. | Established that ARM64-specific cache maintenance and FP-round helper structure deserve follow-up, while generic misalignment fallback is not yet ARM64-specific. | Medium: scaffolding findings still need confirmation from uop usage. | Static audit completed for this module. | Re-open only if later evidence contradicts the module summary. |
| P0 | A-002 | completed | Analyze `codegen_backend_arm64_uops.c` hotspot paths for ARM64-specific helper traffic, correctness traps, and broad performance gaps. | Exposed one concrete correctness bug, one broad optimization gap, and one latent robustness concern; ruled memory-helper traffic as mostly backend-generic. | Medium: full semantic audit of every uop remains out of scope for this session. | Static hotspot audit completed with x86-64 comparisons. | Re-open if later workload evidence points at a different ARM64-only uop family. |
| P1 | A-003 | in_progress | Analyze `codegen_backend_arm64_imm.c` for immediate encoding inefficiencies and constant-materialization churn. | Medium performance upside if common immediates are emitted suboptimally, especially alongside `F-006`. | Low-medium. | Static inspection of encoding strategy and call density in backend/uops code. | Drop if use sites show low frequency or already-optimal encoding. |
| P0 | A-004 | completed | Analyze `src/cpu/386_dynarec.c` block validation / dirty-page flow for recompile churn that could magnify ARM64-only cache-flush cost. | Confirmed that dirty-list recompiles escalate blocks into shorter `BYTE_MASK` mode and then `NO_IMMEDIATES`, directly compounding ARM64 compile-time overhead. | Medium: any heuristic changes risk self-modifying-code regressions. | Static audit completed; future targeted churn test plan only. | Re-open if later evidence shows churn is dominated elsewhere. |
| P2 | A-005 | pending | Analyze raw emitters in `codegen_backend_arm64_ops.c` / `.h` for branch range, literal load, or NEON helper patterns that create avoidable bloat. | Medium. | Medium. | Static inspection after higher-level semantic files. | Defer if higher-level fixes subsume emitter concerns. |
| P1 | A-006 | pending | If uops confirm heavy use, design a bounded ARM64 optimization for FP-to-int rounding that avoids helper-call indirection where safe. | Medium: could reduce helper traffic in x87/MMX-heavy paths. | Medium-high: semantic drift risk around rounding, overflow, and tag handling. | Static semantic audit first; future targeted test plan only with explicit logfile/metadata requirements. | Roll back if manual cross-checks or future tests show rounding mismatches. |
| P0 | A-007 | pending | Fix `codegen_MMX_ENTER()` branch patching to use `block_write_data` consistently and audit the ARM64 backend for any other stale-buffer patch sites. | High: bounded correctness fix for a split-block patching bug that can cause instability. | Low. | Static audit plus future targeted split-block test plan only. | Roll back if branch-patch audit proves `block_write_data` can never differ in that path. |
| P1 | A-008 | pending | Add ARM64 support for the `CODEGEN_BACKEND_HAS_MOV_IMM` fast path by implementing direct immediate stores where safe. | Medium to high: reduces register pressure and instruction count for dead-end immediate writes. | Medium: needs careful handling of address-range limits and stack-vs-`cpu_state` destinations. | Static design now; future compile/runtime validation plan only. | Roll back if implementation increases code size or introduces range-related fragility without measurable benefit. |
| P2 | A-009 | pending | Add a generic ARM64 fallback path for direct state/stack reads and writes that exceed immediate encoding windows instead of `fatal()`ing. | Low to medium: robustness improvement and future-proofing. | Low-medium. | Static call-site audit first; future targeted layout-stress test plan only. | Defer or roll back if current layout margins remain large and no realistic overflow path exists. |
| P1 | A-010 | pending | Revisit dirty-list escalation (`BYTE_MASK` then `NO_IMMEDIATES`) and/or its trigger policy now that ARM64 recompiles pay cache flush and macOS JIT protection costs. | High on churn-heavy workloads: could reduce repeated short-block recompiles and immediate reload overhead. | Medium-high: heuristic changes can regress genuine self-modifying-code cases. | Static design now; future targeted SMC/churn validation plan only with logfile/metadata capture. | Roll back if invalidation frequency or correctness regressions rise in SMC-heavy workloads. |

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

## Progress Log
| Completed analysis unit | Key finding ID(s) | Evidence link(s) | Estimated impact | Decision ID | Follow-up action ID(s) |
| --- | --- | --- | --- | --- | --- |
| U-000 ARM64 backend surface map | `F-001` | `E-001`, `E-002`, `E-004`, `E-005`, `E-009` | High for scoping and prioritization | `D-001` | `A-001`, `A-002`, `A-003`, `A-004`, `A-005` |
| U-001 `codegen_backend_arm64.c` | `F-002`, `F-003`, `F-004` | `E-003`, `E-010`, `E-011`, `E-012`, `E-013`, `E-014`, `E-015` | Medium-high: identified one ARM64-specific compile-time cost, one ARM64-specific helper-cost candidate, and one deprioritized false lead | `D-002`, `D-003` | `A-002`, `A-004`, `A-006` |
| U-002 `codegen_backend_arm64_uops.c` hotspot audit | `F-005`, `F-006`, `F-007` | `E-016`, `E-017`, `E-018`, `E-019`, `E-020`, `E-021`, `E-022`, `E-023`, `E-024` | High: surfaced a bounded P0 correctness defect plus a broad ARM64-only optimization gap | `D-004`, `D-005`, `D-006` | `A-004`, `A-007`, `A-008`, `A-009` |
| U-003 `386_dynarec.c` recompile/churn audit | `F-008`, `F-009` | `E-025`, `E-026`, `E-027`, `E-028` | High: ties ARM64 compile-time taxes to specific churn/escalation mechanics | `D-007`, `D-008` | `A-003`, `A-010` |

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
| E-021 | `src/codegen_new/codegen_reg.c` | lines 512-540 | `F-006` | Shows the backend-specific direct immediate-store helpers that become available when the fast-path flag is enabled. |
| E-022 | `src/codegen_new/codegen_backend_arm64_uops.c` | lines 3373-3566 | `F-007`, `H-007`, `D-006` | Captures the fixed-range direct read/write helpers and their `fatal()` behavior when offsets exceed AArch64 immediate windows. |
| E-023 | `src/codegen_new/codegen_reg.c` | lines 307-540 | `F-007`, `D-006` | Audits the current ARM64 direct-access call sites: `cpu_state` fields, FPU tag/MM/ST arrays, and small stack slots. |
| E-024 | `src/cpu/cpu.h` | lines 332-417 | `F-007`, `D-006` | Shows the relevant `cpu_state` fields currently used by direct-access helpers live inside the core state struct, supporting the “latent, not active” classification. |
| E-025 | `src/cpu/386_dynarec.c` | lines 492-498 | `F-008`, `D-007` | Shows the dirty-list escalation path that promotes blocks to `BYTE_MASK` and then `NO_IMMEDIATES`. |
| E-026 | `src/cpu/386_dynarec.c` | lines 539-552 and 639-642 | `F-008`, `F-009`, `D-007`, `D-008` | Shows the shortened `BYTE_MASK` block-size cap and the macOS AArch64 `pthread_jit_write_protect_np` toggles around recompilation. |
| E-027 | `src/codegen_new/codegen_block.c` | lines 370-384 and 548-568 | `F-008` | Confirms dirty invalidation frees the compiled mem blocks and that recompilation allocates fresh executable memory again. |
| E-028 | `src/codegen_new/codegen.c` | lines 217-240 | `F-008` | Representative decode path showing how `CODEBLOCK_NO_IMMEDIATES` replaces embedded immediates with memory loads, increasing translation work. |

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

## Known Unknowns
- Whether recent historical ARM64 regressions were accompanied by preserved logfiles and metadata on disk; none have been referenced yet in this session.
- Which helper or fallback sites in `codegen_backend_arm64_uops.c` dominate real workloads.
- Whether any ARM64 correctness issues stem from x86 flag reconstruction, address-size corner cases, FP rounding, or block invalidation/re-entry sequencing.
- Whether immediate materialization overhead is frequent enough to compete with helper-call or spill costs.
- How often ARM64 `codegen_fp_round` / `codegen_fp_round_quad` calls actually occur in representative workloads relative to total block execution.
- How much user-visible slowdown is driven by compile/recompile churn that magnifies the mandatory ARM64 `__clear_cache()` cost.
- Whether any existing workload is known to emit `UOP_MMX_ENTER` near a mem-block boundary often enough to make `F-005` user-visible today.
- Whether enabling ARM64 `MOV_IMM` fast-path support would require a generic out-of-range fallback first, or whether current destinations are already range-safe enough for an initial slice.
- How often real workloads drive the dirty-list progression all the way to `NO_IMMEDIATES`, and whether that correlates with observed ARM64 slowdowns.
- Whether Apple ARM64 workloads are bottlenecked more by cache flushes, JIT write-protect toggles, or the shorter `BYTE_MASK` block size after invalidations.

## Rejected Hypotheses
- H-005 rejected at 2026-04-20 22:58:23 EDT: `codegen_allocator_clean_blocks()` is not freeing or invalidating helper memory. `src/codegen_new/codegen_allocator.c:190-200` shows an ARM64-only `__clear_cache()` loop over generated blocks, which makes the ARM64 init/epilogue call sites a cache-coherency requirement rather than a lifetime bug. Evidence: `E-012`, `E-013`.

## State Snapshot
### Snapshot 2026-04-20 23:12:37 EDT
- Current focus: session checkpoint complete; next recommended unit remains `A-003` / `src/codegen_new/codegen_backend_arm64_imm.c`
- Completed units: `U-000`, `U-001`, `U-002`, `U-003`
- Active findings: `F-001` through `F-009`
- Active decisions: `D-001` through `D-008`
- Active hypotheses:
  - `H-001` parked
  - `H-002` open
  - `H-003` open
  - `H-004` open
  - `H-005` rejected
  - `H-006` open
  - `H-007` open
- Next commit-ready investigation move: inspect `codegen_backend_arm64_imm.c` specifically for reusable immediate/store machinery that could lower the cost or implementation risk of `S-02`.

### Snapshot 2026-04-20 23:09:58 EDT
- Current focus: `A-003` / `src/codegen_new/codegen_backend_arm64_imm.c`
- Completed units: `U-000`, `U-001`, `U-002`, `U-003`
- Active findings: `F-001`, `F-002`, `F-003`, `F-004`, `F-005`, `F-006`, `F-007`, `F-008`, `F-009`
- Active hypotheses:
  - `H-001` parked
  - `H-002` open
  - `H-003` open
  - `H-004` open
  - `H-005` rejected
  - `H-006` open
  - `H-007` open
- Next commit-ready investigation move: inspect `codegen_backend_arm64_imm.c` to see whether ARM64 already has reusable constant-materialization machinery for `A-008`, or whether the missing `MOV_IMM` fast path truly needs new helper plumbing.

### Snapshot 2026-04-20 23:06:23 EDT
- Current focus: `A-004` / `src/cpu/386_dynarec.c`
- Completed units: `U-000`, `U-001`, `U-002`
- Active findings: `F-001`, `F-002`, `F-003`, `F-004`, `F-005`, `F-006`, `F-007`
- Active hypotheses:
  - `H-001` parked
  - `H-002` open
  - `H-003` open
  - `H-004` open
  - `H-005` rejected
  - `H-006` open
  - `H-007` open
- Next commit-ready investigation move: inspect `386_dynarec.c` block validation, dirty-page invalidation, and recompilation flow to determine whether ARM64’s mandatory cache-flush cost is likely being amplified by avoidable churn.

### Snapshot 2026-04-20 22:58:23 EDT
- Current focus: `A-002` / `src/codegen_new/codegen_backend_arm64_uops.c`
- Completed units: `U-000`, `U-001`
- Active findings: `F-001`, `F-002`, `F-003`, `F-004`
- Active hypotheses:
  - `H-001` parked
  - `H-002` open
  - `H-003` open
  - `H-004` open
  - `H-005` rejected
  - `H-006` open
- Next commit-ready investigation move: inspect `codegen_backend_arm64_uops.c` around `codegen_exit_rout` and `codegen_fp_round*` use sites first, then expand outward to high-frequency helper exits and flag/semantic paths before updating the backlog again.

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
| S-02 | P1 | Add ARM64 support for the missing `MOV_IMM` direct-to-memory fast path to reduce dead-end immediate-store overhead. | `src/codegen_new/codegen_backend_arm64.h`, `src/codegen_new/codegen_backend_arm64_uops.c`, `src/codegen_new/codegen_backend_arm64_ops.c` | Decide whether existing ARM64 immediate/store helpers are sufficient for byte/word/dword destinations; keep current range-safety assumptions explicit; define a future validation plan that compares code size / helper count before vs after and records logfile/metadata destinations. | If implementation needs a large generic addressing refactor first, or if range handling turns the slice into a higher-risk change than expected. |
| S-03 | P1 | Reduce churn from dirty-list escalation that currently drives shorter `BYTE_MASK` blocks, `NO_IMMEDIATES`, cache flushes, and macOS JIT toggles. | `src/cpu/386_dynarec.c`, `src/codegen_new/codegen_block.c`, optionally `src/codegen_new/codegen.h` if flag semantics change | Define the exact heuristic change first: delay escalation, add counters/thresholds, or constrain it to confirmed hot dirty blocks; pair it with a future SMC-focused test plan and explicit logfile/metadata capture. | If self-modifying-code workloads regress, invalidation frequency rises, or the heuristic increases correctness risk more than it reduces ARM64 recompile cost. |

### First Slice Recommended
- Recommend `S-01` first.
- Why first:
  - It is the highest-confidence correctness issue found in this session.
  - The write set is tiny and bounded.
  - The rollback surface is low compared with the broader heuristic/performance slices.

### Sequencing Notes
- `S-02` is the best broad performance follow-up once `S-01` is closed; it improves a generic inefficiency without needing to change dynarec policy.
- `S-03` should come after `S-01` and ideally after an initial `S-02` design pass, because the churn slice is the most policy-heavy and benefits from having the lower-level ARM64 cost model already improved.

## Session Delta
- Added/changed in this session:
  - Created the canonical investigation document.
  - Recorded scope, constraints, goals, and non-goals for a docs-only ARM64 dynarec campaign.
  - Built the initial ARM64 module map and seeded the evidence index, hypothesis register, backlog, progress log, decision ledger, resume state, and first state snapshot.
  - Completed `U-001` for `src/codegen_new/codegen_backend_arm64.c`, adding findings for ARM64 cache-maintenance cost, FP-round helper structure, and a deprioritized misalignment fallback lead.
  - Added allocator evidence showing `codegen_allocator_clean_blocks()` is a cache flush, not a free, and recorded the rejection of `H-005`.
  - Completed `U-002` for `src/codegen_new/codegen_backend_arm64_uops.c`, adding a P0 correctness finding (`F-005`), a broad ARM64-only optimization gap (`F-006`), and a latent robustness finding (`F-007`).
  - Completed `U-003` for `src/cpu/386_dynarec.c`, tying ARM64 compile-time taxes to dirty-list recompilation, shorter `BYTE_MASK` blocks, `NO_IMMEDIATES`, and macOS JIT write-protect toggling.
  - Added the decision-ready top-3 implementation slice plan with files, gates, rollback conditions, and a recommended first slice.
- Unresolved:
  - `codegen_backend_arm64_imm.c` has not yet been inspected to determine how much reusable machinery exists for the proposed ARM64 `MOV_IMM` fast path.
  - No runtime validation plan tied to specific suspected failure points has been drafted yet.
- Exact first commands/files for next session:
  - `rg -n "host_arm64_find_imm|MOVX_IMM|MOVZ_IMM|MOVK_IMM|mov_imm|immediate|literal" src/codegen_new/codegen_backend_arm64_imm.c src/codegen_new/codegen_backend_arm64_uops.c src/codegen_new/codegen_backend_arm64_ops.c`
  - `nl -ba src/codegen_new/codegen_backend_arm64_imm.c | sed -n '1,260p'`
  - `nl -ba src/codegen_new/codegen_backend_arm64_imm.c | sed -n '260,520p'`
  - File focus: `src/codegen_new/codegen_backend_arm64_imm.c`
- Recommended immediate next investigation unit: `U-004 codegen_backend_arm64_imm.c`
