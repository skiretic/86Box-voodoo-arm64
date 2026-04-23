# ARM64 3DNow Authoritative Notes (Repo-Local)

## Purpose

This file captures the minimum authoritative 3DNow facts needed for ARM64 dynarec bring-up, so we do not repeatedly re-scan manuals.

Primary goal:
- keep implementation aligned to architectural opcode semantics (real dynarec lowering), not ad-hoc helper-only behavior.

## Authoritative Sources in This Repo

- AMD Architecture Programmer's Manual snapshot:
  - `docs/reference/x86-manuals/24594_3.37.pdf`
- Intel SDM snapshots (supporting context only for this topic):
  - `docs/reference/x86-manuals/intel-sdm-vol2a-253666-091us-march-2026.pdf`
  - `docs/reference/x86-manuals/intel-sdm-vol2b-253667-091us-march-2026.pdf`

## 3DNow Encoding Model (AMD Authoritative)

From AMD APM Vol. 3 (`24594_3.37.pdf`, Appendix A.1.2):

- 3DNow instructions use:
  - `0F 0F [ModRM] [SIB] [disp] imm8_opcode`
- The actual 3DNow opcode selector is the final `imm8`.
- Immediate-byte opcode maps are in:
  - Table A-13 (low nibble `0..7`)
  - Table A-14 (low nibble `8..F`)

Implementation implication:
- ARM64 dynarec dispatch must treat `imm8` as opcode key for `0F 0F` path.

## Canonical 3DNow Opcode Map Used by Bring-Up

From AMD Tables A-13/A-14:

- `0x0d` PI2FD
- `0x1d` PF2ID
- `0x0c` PI2FW (3DNowExt)
- `0x1c` PF2IW (3DNowExt)
- `0x8a` PFNACC (3DNowExt)
- `0x8e` PFPNACC (3DNowExt)
- `0x90` PFCMPGE
- `0x94` PFMIN
- `0x96` PFRCP
- `0x97` PFRSQRT
- `0x9a` PFSUB
- `0x9e` PFADD
- `0xa0` PFCMPGT
- `0xa4` PFMAX
- `0xa6` PFRCPIT1
- `0xa7` PFRSQIT1
- `0xaa` PFSUBR
- `0xae` PFACC
- `0xb0` PFCMPEQ
- `0xb4` PFMUL
- `0xb6` PFRCPIT2
- `0xb7` PMULHRW
- `0xbb` PSWAPD (3DNowExt)
- `0xbf` PAVGUSB

## CPUID Feature Gating (AMD Authoritative)

From AMD APM CPUID feature tables:

- `Fn8000_0001_EDX[31]` = `3DNow`
- `Fn8000_0001_EDX[30]` = `3DNowExt`
- `Fn8000_0001_ECX[8]` = `3DNowPrefetch` (PREFETCH/PREFETCHW support signal)

Bring-up gating rule:
- Do not enable dynarec mapping for extension opcodes unless `3DNowExt` capability is present in the emulated profile.

## Intel SDM Scope Note

Local Intel Vol. 2A/2B snapshots in repo provide `PREFETCHW` semantics, but do not provide canonical AMD 3DNow opcode semantics.

Therefore:
- opcode/semantic truth for 3DNow comes from AMD APM for this effort.
- Intel references are supplemental only where shared instruction behavior (for example `PREFETCHW` hint characteristics) is relevant.

## Practical Implementation Guidance

1. Keep ARM64 changes table-driven:
   - enable `recomp_opcodes_3DNOW` entries opcode-by-opcode (`imm8`) with explicit guards.
2. Keep unsupported opcodes as `NULL` to preserve interpreter fallback until lowered.
3. Use `tools/win98-3dnowcov` + host `COV3DNOW_SUMMARY` telemetry as the primary acceptance gate for each newly enabled opcode.
4. Only promote a new opcode mapping after deterministic `3DNOWCOV` pass and stable hash behavior.

