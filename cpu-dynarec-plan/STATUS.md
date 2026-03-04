# Status

## Current Phase: 0 — Audit & Planning (COMPLETE)

## Audit Status

- [x] Other dynarec research (cpu-arch) → `research/other-dynarecs.md`
- [x] Instruction coverage audit (cpu-x86ops) → `research/instruction-coverage.md`
- [x] Correctness audit (cpu-debug) → `research/correctness-audit.md`
- [x] ARM64 backend audit (cpu-arm64) → `research/arm64-backend-audit.md`
- [x] UOP catalog (cpu-lead) → `research/uop-catalog.md`
- [x] Prior work review (cpu-lead) → `research/prior-work.md`
- [x] Phase roadmap synthesized → `PHASES.md`

## Confirmed Bugs

| ID | File | Status | Description |
|----|------|--------|-------------|
| BUG-1 | `codegen_ir_defs.h:533` | OPEN | `is_a16` double-clear breaks 16-bit address wrapping |
| BUG-2 | `codegen.c:614` | OPEN | `jump_cycles` return value discarded |
| BUG-3 | `codegen_backend_arm64_uops.c:1879` | OPEN | PFRSQRT clobbers own result |
| BUG-4 | `codegen_backend_arm64.c:344` | OPEN | V8-V15 callee-saved SIMD not preserved |

## Next Step

Begin Phase 1: Bug Fixes & Quick Wins

## Branch

`cpu-dynarec-improvements` — based on latest master merge (1a4dd960c)
