# 2026-03-09 Guest Regression Planning Reset

## Purpose

This note resets the CPU new dynarec near-term workflow after the recent run of host-clean but guest-regressed opcode trials.

It is intentionally not another opcode recommendation memo.

Its job is to answer four questions:

1. Why did earlier work land cleanly while recent work keeps failing?
2. Which classes of work are still safe enough to attempt directly?
3. Which classes are blocked on missing backend/debugging/planning steps?
4. What exact prerequisites must be met before another guest-facing opcode enablement is allowed?

## Problem statement

Recent sessions have repeated the same pattern:

- host-side semantics harness passes
- target build passes
- narrow guest validation still regresses during boot
- the opcode path is backed out again

That has now happened across three different instruction areas:

- `0x0f 0xbc` / `0xbd` (`BSF` / `BSR`)
- `0x0f 0xaf` (`IMUL r16/32, r/m16/32`)
- base `0xd0`-`0xd3` `RCL` / `RCR`

This is no longer a one-off bad candidate selection problem.

It is a workflow problem.

## Why the earlier runs succeeded

The earlier successful sequence mostly stayed inside the easiest direct-recompile class:

- table-hole closure rather than bailout-closure inside existing handlers
- coherent opcode families
- register-only or low-side-effect semantics
- small or predictable flag surfaces
- minimal interaction with carry chains, protected state, or device behavior
- no dependence on helper-call ABI shapes beyond the backend’s already-exercised path

Examples that fit this class well:

- `SETcc`
- `BSWAP`
- several earlier base-table-hole / narrow helper-backed steps that validated cleanly

Those wins were real, but they were not evidence that all remaining hotspots are equally safe.

## Why the recent runs failed

The recent failures came from a harder work class:

- bailout-closure inside already-present direct handlers
- helper-backed semantics with more integration points
- lazy-flags or carry-sensitive behavior
- guest-visible interaction with system bring-up rather than only ring-3 arithmetic
- backend assumptions that were not audited before guest enablement

The `D0` / `D1` / `D2` / `D3` retry makes the problem clear:

- first failure:
  - arm64 backend fatal on `codegen_LOAD_FUNC_ARG2 113`
  - root cause: the new helper path assumed a third helper argument was legal on this backend
- second failure:
  - backend fatal fixed
  - guest still failed with the same class of early Windows boot regression
  - root cause therefore moved from pure helper ABI misuse to deeper guest integration behavior

That is the same broad shape already seen on the `BSF` / `BSR` and `0x0f 0xaf` retries:

- host semantics signal is necessary
- host semantics signal is not sufficient

## Working classification

### Class A: still safe enough for direct landing

Definition:

- mostly table-null closure
- register-only or otherwise low-side-effect
- no protected-mode, segment, carry-chain, or device interaction
- no new helper-call shape beyond already-proven backend usage
- one coherent family

Expected workflow:

- host harness if useful
- one logged guest validation
- keep enabled if the guest run is clean

Examples:

- `BSWAP`-style register-only families
- narrow table-hole families that resemble earlier successful work

### Class B: measurable but blocked on debugging infrastructure

Definition:

- helper-backed bailout closure inside existing direct handlers
- lazy-flags or carry-sensitive behavior
- arithmetic where unit semantics can pass while guest integration still fails
- any path where guest-visible failure has already been observed once

Expected workflow:

- do not guest-enable by default
- require compare/debug instrumentation first
- require explicit backend capability audit first

Current examples:

- base `0xd0`-`0xd3` `RCL` / `RCR`
- `0x0f 0xaf`
- `0x0f 0xbc` / `0xbd`

### Class C: blocked on backend or system-model risk

Definition:

- I/O, interrupts, segment/control-register, far-transfer, REP, or protected/control behavior
- broad family surface
- heavy device/timing sensitivity
- difficult to isolate with current guest workflow

Expected workflow:

- do not choose as the next implementation family unless a new infrastructure pass explicitly changes the risk profile

Current examples:

- base I/O opcodes: `0xe4`, `0xe6`, `0xec`, `0xee`, `0xef`
- `INT` / `IRET`
- `LAR` / `LSL`
- REP
- control/system `0F` opcodes

## Missing planning and infrastructure steps

### 1. Backend capability checklist

Before another helper-backed guest enablement, the plan must state:

- how many helper arguments the active backend actually supports
- whether the operand sizes used in helper argument loading are legal on that backend
- whether the helper result/flag merge pattern has already been exercised elsewhere on arm64
- whether the candidate depends on backend-illegal IR forms already seen in prior failures

This step was missing on the `D0`-`D3` attempt.

### 2. Risk-class gate before implementation

Every candidate should be labeled before code changes as one of:

- Class A: guest-enable candidate
- Class B: debug-first candidate
- Class C: blocked candidate

Recent sessions skipped this and treated some Class B work as if it were Class A.

### 3. Base-opcode compare/debug path

The branch already has evidence that compare-style debugging helps:

- the `0x0f 0xaf` compare pass cleared obvious destination and `CF` / `OF` mismatch theories

There is no equivalent current workflow for base-opcode bailout families.

That gap matters because `D0`-`D3` is not a table-hole problem.

Needed next:

- a narrow direct-vs-helper compare hook for selected base-opcode families
- opcode-filtered logging for direct attempts, expected result, actual result, and relevant flag mask
- enough observability to decide whether the bug is:
  - wrong math
  - wrong flag state
  - wrong decode/writeback integration
  - or a broader block/IR interaction

### 4. Cleaner strict-i686 baseline image

The current `Windows 98 TESTING` image is no longer a clean ranking source for base hotspots if recent installs materially increased device traffic.

That does not make the earlier measurements useless.

It does mean:

- I/O-heavy ranking on that image is now more workload-specific
- strict-i686 hotspot selection should not overfit to the post-install image

Needed next:

- keep the current log as historical evidence
- create or identify a cleaner pre-install strict-i686 baseline image for future ranking work

### 5. Hard stop rule for repeated guest failures

If a candidate family produces:

- one host-clean guest regression, and
- no guest-side compare/debug layer exists yet

then the next session should not retry guest enablement directly.

It should switch to infrastructure/debugging work first.

That rule was not enforced strongly enough on the recent retries.

## New preconditions for any guest-facing opcode trial

No new guest-facing opcode enablement should be attempted unless all of the following are written down in the plan first:

1. Candidate classification
   - explicit Class A / B / C label

2. Backend audit
   - helper-call ABI shape checked on arm64
   - no unsupported helper arg count
   - no backend-illegal width/form assumptions

3. Validation strategy
   - host harness coverage if semantics are non-trivial
   - guest compare/debug path if the family is Class B
   - one exact VM/log target named in advance

4. Backout criteria
   - exact guest-visible failure condition that forces immediate disablement again

5. Baseline quality
   - confirmation that the chosen VM image is still appropriate for the claim being made

If any item is missing, the work is not ready for guest enablement.

## What is actually worth doing next

### Recommended next session type

Planning/debugging infrastructure only.

Not another opcode landing.

### Highest-value concrete next steps

1. Document arm64 helper-call constraints for CPU dynarec helper-backed paths.
2. Add a base-opcode direct-vs-helper compare path for narrow families like `D0`-`D3`.
3. Build a small candidate matrix that classifies remaining hotspots into Class A / B / C.
4. Identify a cleaner strict-i686 baseline image for future ranking work.

### What not to do next

- do not pick the next opcode straight from current fallback counts
- do not guest-enable another helper-backed bailout family without compare/debug support
- do not choose I/O opcodes just because the current image makes them look hot
- do not retry `0x0f 0xaf`, `BSF` / `BSR`, or `D0`-`D3` guest enablement unchanged

## Explicit recommendation

The next productive milestone is not “land another opcode”.

It is:

- one debugging/validation workflow document
- one backend-capability note for arm64 helper-backed paths
- one base-family compare infrastructure step

Only after those are in place should another guest-facing bailout-closure attempt be considered.
