# 2026-03-09 Guest Regression Planning Reset

> Historical workflow note (updated 2026-03-09): this reset remains valid as process guidance, but it is not the live branch-status summary anymore. Later the same day the branch deliberately pivoted back to low-risk table-hole work and guest-validated `CMPS`, `SCAS`, and `SAHF` / `LAHF` on `Windows 98 SE`, while the blocked guest-facing families named here remain paused. For current status, use [new-dynarec-executive-summary.md](./new-dynarec-executive-summary.md), [new-dynarec-changelog.md](./new-dynarec-changelog.md), and [new-dynarec-optimization-overview.md](./new-dynarec-optimization-overview.md).

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
- `0x0f 0xba` / `0xa3` / `0xab` / `0xb3`

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
- `0x8e`
- `0x9b`
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

### 1a. Actual arm64 helper-call/backend audit

The current codebase already makes the hard arm64 constraints explicit:

- `src/codegen_new/codegen_backend_arm64_uops.c`
  - `codegen_LOAD_FUNC_ARG0` and `codegen_LOAD_FUNC_ARG1` accept only `REG_IS_W` and `REG_IS_L` inputs
  - 8-bit helper-source registers are not legal helper arguments on arm64
  - `codegen_LOAD_FUNC_ARG2` and `codegen_LOAD_FUNC_ARG3` unconditionally `fatal(...)`
  - `codegen_LOAD_FUNC_ARG0_IMM` through `codegen_LOAD_FUNC_ARG3_IMM` do exist
  - `codegen_CALL_FUNC_RESULT` requires the destination to be an `L`-sized ireg and returns the helper value through `W0`
- Practical arm64 rule for CPU dynarec helper-backed paths:
  - at most two register-backed helper inputs
  - optional immediate metadata in helper arg slots 0-3
  - one 32-bit scalar helper return per call
  - multi-result semantics must be expressed as multiple helper calls or by packing auxiliary data into an existing scalar argument
- This is not just an arm64 quirk in practice:
  - the current x86-64 backend also only implements register-backed helper args 0 and 1
  - x86-64 is stricter on immediates because arg2/arg3 immediate loads also `fatal(...)` there
  - the common safe subset across both backends is therefore:
    - two register args max
    - preferably only arg0/arg1 immediates
    - one 32-bit return value per helper call

### 1b. Existing valid helper-backed templates versus invalid shapes

Valid templates already exercised in-tree:

- single-register, single-result helper
  - `ropBSWAP` uses arg0 register input plus one 32-bit result helper
- two-register, dual-helper-result pattern with explicit flag merge
  - `ropIMUL_0f_w_rm` / `ropIMUL_0f_l_rm`
  - `ropBSF` / `ropBSR`
  - these stay inside the backend ABI because they use only arg0/arg1 register loads and get result and flag-mask via separate helper calls
- one-register plus immediate metadata helper
  - `ropF6*`, `ropF7*`, and the immediate-IMUL handlers use arg0 register plus arg1 immediate and one scalar result
- two-register plus immediate metadata helper
  - far-call helpers such as `ropFF_16` / `ropFF_32` use arg0 register, arg1 register, and arg2 immediate
  - this shape is valid on arm64 because arg2 immediate loads exist

Invalid or backend-risky shapes for current CPU dynarec work:

- any helper path that needs a register-backed arg2 or arg3
  - that was the first `D0`-`D3` arm64 crash
- any helper call that tries to pass an 8-bit ireg directly as a helper argument
  - helper arg loaders only accept 16-bit or 32-bit source widths
- any helper design that assumes multiple native return registers
  - current helper-call lowering only captures a single 32-bit scalar result
- any candidate that requires a new helper shape not already visible in a successful path
  - that must be treated as backend work first, not as a guest trial

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

Proposed smallest useful workflow:

1. Reuse the existing decision-boundary filters already in `codegen.c` / `codegen_observability.c`
   - keep the scope to exact PC, exact opcode, and a small sample budget
2. Add one base-opcode compare mode only for the targeted bailout subgroup
   - start with `D0`-`D3` `RCL` / `RCR`
   - do not generalize to every opcode family yet
3. In the direct handler, capture only the minimal pre-state needed to explain the bug
   - opcode / ModRM subgroup
   - width
   - register-vs-memory form
   - original operand value
   - shift count
   - incoming `CF`
4. Compute the direct-path candidate result and direct flag mask without committing guest state yet
5. Compute helper-visible expected result and flag mask through the existing packed two-argument helper ABI
   - same helper harness helpers already used in `codegen_test_support.c`
   - no new helper-call shape
6. If the compare sample mismatches:
   - log one compact structured record
   - force helper fallback for that execution
   - keep the family guest-disabled
7. If the compare sample matches:
   - that narrows the bug to decode/writeback, flag merge, or larger block interaction
   - only then decide whether more instrumentation is needed

What this intentionally does not try to do:

- no whole-block shadow execution
- no always-on logging
- no new guest enablement by default
- no speculative retry of `D0`-`D3`, `0x0f 0xaf`, or `BSF` / `BSR`

Implementation now in tree:

- the compare-only path is enabled only when `86BOX_NEW_DYNAREC_DEBUG_D0D3_RCLRCR=1` is set
- site selection reuses the existing `86BOX_NEW_DYNAREC_VERIFY_PC`, `86BOX_NEW_DYNAREC_VERIFY_OPCODE`, and `86BOX_NEW_DYNAREC_VERIFY_BUDGET` filters
- compact mismatch and shutdown logging is enabled with `86BOX_NEW_DYNAREC_LOG_D0D3_COMPARE=1`
- first-hit per-site discovery logging is enabled with `86BOX_NEW_DYNAREC_LOG_D0D3_COMPARE_SITES=1` and is intended only to discover a later narrow `VERIFY_PC`
- on a sampled mismatch, the direct compare path exits back to helper execution for that exact run instead of committing guest state
- without the debug knob, base `D0`-`D3` `RCL` / `RCR` remains guest-disabled on this branch
- enough broad and locked-site match-only evidence now exists that this compare/debug mechanism should be treated as a completed prerequisite rather than an open-ended instrumentation campaign
- the next branch session should pivot back to low-risk implementation work instead of extending this `D0`-`D3` probe surface again

### 4. Cleaner strict-i686 baseline image

The current `Windows 98 TESTING` image is no longer a clean ranking source for base hotspots if recent installs materially increased device traffic.

That does not make the earlier measurements useless.

It does mean:

- I/O-heavy ranking on that image is now more workload-specific
- strict-i686 hotspot selection should not overfit to the post-install image

Recommended standing baseline going forward:

- `/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 SE`
  - `cpu_family = celeron_mendocino`
  - `cpu_speed = 300000000`
  - `machine = bf6`
  - same strict-i686 CPU/machine class as the existing strict Mendocino evidence
  - use this as the named strict-i686 validation VM for future work

Important caveat:

- this image should be treated as the new standing VM choice for future work, not as retroactive replacement evidence for earlier `Windows 98 TESTING` logs
- `Windows 98 SE copy` remains the alternate strict-i686 comparison candidate if later work needs a second Mendocino image

Working rule:

- use `Windows 98 SE` as the named strict-i686 VM for future planned validation and compare/debug work
- keep the existing `windows98_testing_i686_baseline.log` lineage as the default comparison point
- if hotspot ranking becomes sensitive to install/device churn again, take a fresh baseline snapshot from this VM or compare against `Windows 98 SE copy`

### 5. Hard stop rule for repeated guest failures

If a candidate family produces:

- one host-clean guest regression, and
- no guest-side compare/debug layer exists yet

then the next session should not retry guest enablement directly.

It should switch to infrastructure/debugging work first.

That rule was not enforced strongly enough on the recent retries.

## Candidate matrix

| Class | Family | Why it belongs there | Immediate rule |
|---|---|---|---|
| A | No current top-bucket strict-i686 hotspot family clearly qualifies | The remaining measured leaders are now helper-backed bailout cleanup or system/I/O-heavy work, not another `BSWAP`-style row | Do not force a Class A pick from the current hotspot list |
| B | base `0xd0`-`0xd3` `RCL` / `RCR` | Existing direct handlers, measurable bailout count, prior host-clean / guest-bad history, and one previously invalid helper shape | Debug-first only |
| B | `0x0f 0xaf` | Existing retained direct handlers and harness coverage, but repeated guest failure after host-clean semantics and compare pass | Keep guest-disabled; use only as a compare/debug reference |
| B | `0x0f 0xbc` / `0xbd` | Existing retained direct handlers and harness coverage, but guest-visible failure remained after obvious bugs were fixed | Keep guest-disabled; debug-first only |
| B | `0x0f 0xba` / `0xa3` / `0xab` / `0xb3` | Hot and coherent, but semantically wider RMW/bit-index family with prior guest-risk evidence on adjacent work | Do not attempt before compare/debug path exists |
| C | base I/O `0xe4` / `0xe6` / `0xec` / `0xee` / `0xef` | Hot because of system/device behavior; highly sensitive to guest I/O workload and image pollution | Do not choose as the next landing family |
| C | `0xcd` / `0xcf` | Interrupt/control-flow surface, guest-bring-up sensitive | Leave out of near-term work |
| C | `0x8e` | Segment-state mutation path | Leave out of near-term work |
| C | `0x9b` | FPU/control crossover with poor isolation value | Leave out of near-term work |
| C | `0x0f 0x02` / `0x03` and `0x0f 0x01` / `0x20` / `0x22` / `0x31` | Protected/control/system semantics | Block until a separate system-behavior plan exists |
| C | REP / x87 follow-up | Broad architectural surface rather than a narrow opcode closure | Not a near-term CPU dynarec guest trial target |

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
4. Use `/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 SE` as the named strict-i686 working baseline for future runs, while keeping the existing `windows98_testing_*` logs as historical comparison evidence.

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

## Next safest productive session

Implement only the narrow `D0`-`D3` direct-vs-helper compare path described above, keep guest dispatch for `RCL` / `RCR` disabled, and use `Windows 98 SE` as the named strict-i686 validation target for any later compare-only run.
