# 2026-03-09 `SAHF` / `LAHF` Audit

> Historical pre-implementation audit note (updated 2026-03-09): this audit answered the go/no-go question and the recommendation was then implemented in the same branch state. Base `0x9e` / `0x9f` (`SAHF` / `LAHF`) has since landed and guest-validated on `Windows 98 SE`; the confirming shutdown log is `/tmp/windows98_se_sahf_lahf_validation.log`. Treat the body below as the preserved audit that justified the landing, not as the current coverage snapshot. For current status, use [new-dynarec-executive-summary.md](./new-dynarec-executive-summary.md), [new-dynarec-changelog.md](./new-dynarec-changelog.md), and [new-dynarec-optimization-overview.md](./new-dynarec-optimization-overview.md).

## Purpose

This note audits whether base `0x9e` / `0x9f` (`SAHF` / `LAHF`) is truly the next safe CPU new dynarec landing target on the current stable `cpu-optimizations` branch state.

Stable branch assumptions for this audit:

- direct `BSF` / `BSR`, direct `0x0f 0xaf`, and direct base `D0`-`D3` `RCL` / `RCR` stay disabled
- host-side harness/debug support for `BSF` / `BSR`, `0x0f 0xaf`, and rotate-through-carry stays in tree
- `CMPS` (`0xa6` / `0xa7`) and `SCAS` (`0xae` / `0xaf`) are already in tree and guest-validated on `Windows 98 SE`
- the latest strict-i686 shutdown confirmation remains `/tmp/windows98_se_scas_validation.log`

Bottom line:

- **Yes:** `SAHF` / `LAHF` is still the next safest CPU new dynarec target.
- **But:** it is not identical in risk shape to `CMPS` / `SCAS`; `SAHF` touches lazy-flags materialization and compile-time flag-state bookkeeping, so that one coupling must be handled deliberately.

## 1. Current interpreter and dynarec state

### Interpreter semantics

386+ interpreter behavior is small and explicit in both `src/cpu/x86_ops_flag.h` and `src/cpu/x86_ops_flag_2386.h`:

- `SAHF`
  - calls `flags_rebuild()`
  - writes `cpu_state.flags = (cpu_state.flags & 0xff00) | (AH & 0xd5) | 2`
  - clears `codegen_flags_changed = 0`
- `LAHF`
  - calls `flags_rebuild()`
  - writes `AH = cpu_state.flags & 0xff`

Audit implication:

- `SAHF` is not just a byte move; it consumes lazy flags and then overwrites the low architectural flag byte with the x86 mask/reserved-bit rules
- `LAHF` is simpler; it only snapshots already-materialized flags into `AH`

### Current dynarec coverage state

Base opcode table entries in `src/codegen_new/codegen_ops.c` are still `NULL` for both 16-bit and 32-bit base rows at `0x9e` and `0x9f`.

`src/codegen_new/codegen_observability.c` also confirms they are not currently counted as direct base opcodes.

That means `SAHF` / `LAHF` is still a true table-hole pair, not a bailout-closure task inside an already-live handler.

## 2. Backend primitive audit

## x86-64 backend

The x86-64 backend already has all primitives needed for a direct guest implementation:

- byte/high-byte register moves through the normal `MOV`/`MOVZX` uops
- 16-bit and 32-bit immediate mask/or support
- explicit host `LAHF` support already exists in `src/codegen_new/codegen_backend_x86-64_ops.c` and is already used by the FPU compare lowering in `src/codegen_new/codegen_backend_x86-64_uops.c`

Important point:

- a guest `SAHF` / `LAHF` implementation would not need any new x86-64 helper-call ABI or any new host-only opcode shim

## arm64 backend

The arm64 backend also already has the required pieces:

- `codegen_MOV` supports `B`, `BH`, and `W` transfers, including `AH`-style high-byte extraction/insertion
- `codegen_MOVZX` supports zero-extending `BH` sources with `UBFX`
- `codegen_AND_IMM` and `codegen_OR_IMM` already operate on `IREG_flags` / `IREG_flags_W`
- high-byte insert/extract is handled with existing `BFI`, `LSR`, and `UBFX` lowering in `src/codegen_new/codegen_backend_arm64_uops.c`

Audit implication:

- there is no missing ARMv8.0-A primitive here
- there is no need for a new helper-call shape
- there is no repeat of the `D0`-`D3` arm64 arg2/arg3 backend failure mode

## 3. Hidden coupling and real risk class

`SAHF` / `LAHF` stays low risk, but not for exactly the same reason as `CMPS` / `SCAS`.

Why `CMPS` / `SCAS` were especially safe:

- plain table-hole closure
- reused existing string address/index helpers
- wrote lazy `SUB`-style flag state directly
- no forced materialization of prior lazy flags

Why `SAHF` / `LAHF` is still safe enough:

- still plain table-hole closure
- no memory access
- no helper ABI expansion
- no segment/control/device behavior
- no carry-chain or variable-count semantics
- only register and flags-word transfers

What makes it slightly different:

- both instructions force a `flags_rebuild()` first
- `SAHF` then replaces the low guest flag byte from `AH`
- the dynarec implementation must therefore respect both runtime flag materialization and compile-time `codegen_flags_changed` tracking

The key coupling is `SAHF`, not `LAHF`:

- `flags_rebuild()` in `src/cpu/x86_flags.h` sets `cpu_state.flags_op = FLAGS_UNKNOWN` at runtime
- but compile-time branch optimization keys off `codegen_flags_changed` plus the compile-time `cpu_state.flags_op` snapshot in `src/codegen_new/codegen_ops_branch.c`
- interpreter `opSAHF` explicitly clears `codegen_flags_changed = 0`

Practical conclusion:

- a future direct `ropSAHF` must do the same logical thing for the dynarec bookkeeping layer
- if that reset is skipped, later `Jcc` lowering could reason from stale lazy-flag metadata even though the architectural flags were overwritten from `AH`

This is a real caveat, but it is still a narrow, local caveat. It is not a backend-capability blocker.

## 4. Comparison against the remaining `D0`-`D3` bucket

`SAHF` / `LAHF` should still lead over `D0`-`D3`.

Why:

- `SAHF` / `LAHF` remains a clean two-opcode `helper_table_null` family in the latest strict-i686 shutdown log:
  - `0x9e = 49`
  - `0x9f = 36`
- the remaining `D0`-`D3` counts are still a mixed `helper_bailout` bucket:
  - `0xd3 = 921`
  - `0xd1 = 747`
  - `0xd0 = 214`
  - `0xd2 = 145`
- that bucket still combines:
  - the paused `RCL` / `RCR` path
  - zero-count exits on variable-count forms
  - `!block->ins` exits on variable-count forms

Risk comparison:

- `SAHF` / `LAHF`
  - small semantic surface
  - no memory
  - no helper ABI expansion
  - no guest-visible device/system coupling
  - one known lazy-flags bookkeeping caveat
- `D0`-`D3`
  - not a table-hole family anymore
  - mixed bailout reasons
  - already guest-regressed once in the exact area that still dominates the bucket
  - compare/debug evidence only cleared sampled `RCL` / `RCR` math/flags mismatch theories, not the broader integration risk

So even with the higher count, `D0`-`D3` is still the riskier next move. Its remaining work is debugging and reason-splitting, not low-risk closure.

## 5. Recommendation

## Yes/no answer

**Yes.** Base `0x9e` / `0x9f` (`SAHF` / `LAHF`) is still the next safe CPU new dynarec target.

## Narrow recommendation for a later implementation session

If a later session implements this pair, keep it narrow:

1. Add `ropSAHF` / `ropLAHF` in `src/codegen_new/codegen_ops_misc.c` or the nearest existing flag/misc home rather than introducing a new subsystem.
2. Wire only base `0x9e` / `0x9f` in `src/codegen_new/codegen_ops.c`.
3. Follow 386+ interpreter masks exactly:
   - `SAHF`: low-byte update with `(AH & 0xd5) | 2`
   - `LAHF`: copy low flags byte into `AH`
4. Materialize flags first with the existing `flags_rebuild()` path.
5. Treat `SAHF` as a compile-time lazy-flags barrier:
   - clear the dynarec-side `codegen_flags_changed` state after the architectural flag write, matching interpreter intent
6. Add focused policy assertions in `tests/codegen_new_opcode_coverage_policy_test.c`.
7. Run focused local build/test verification.
8. Do one narrow `Windows 98 SE` guest validation run.

## Non-recommendation

Do not let this session drift back into:

- another `D0`-`D3` guest enablement attempt
- another `0x0f 0xaf` or `BSF` / `BSR` retry
- I/O/control/system opcode side work

## Final assessment

`SAHF` / `LAHF` is not quite as mechanically boring as `CMPS` / `SCAS`, because `SAHF` crosses the lazy-flags materialization boundary.

But that coupling is:

- already visible in-tree
- local to existing flags machinery
- backend-safe on both x86-64 and arm64
- materially simpler than the still-mixed `D0`-`D3` bailout bucket

That keeps `SAHF` / `LAHF` in the low-risk lead.
