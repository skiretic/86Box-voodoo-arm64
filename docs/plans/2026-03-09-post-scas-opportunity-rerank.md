# Post-SCAS Opportunity Re-rank

> Historical recommendation note (updated 2026-03-09): this rerank produced the later `SAHF` / `LAHF` audit and the recommendation was accepted. Base `0x9e` / `0x9f` has since landed and guest-validated on `Windows 98 SE`, so this document is now a closed decision record rather than the live priority list. For current status, use [new-dynarec-executive-summary.md](./new-dynarec-executive-summary.md), [new-dynarec-changelog.md](./new-dynarec-changelog.md), and [new-dynarec-optimization-overview.md](./new-dynarec-optimization-overview.md).

## Purpose

This note refreshes the next-target recommendation after `SCAS` (`0xae` / `0xaf`) landed and guest-validated on `Windows 98 SE`.

It is intentionally narrower than the earlier i686 review. Its job is to answer one question:

- after `CMPS` and `SCAS` are both closed, what is now the safest next CPU new dynarec target?

## Updated strict-i686 evidence

Latest confirming shutdown log:

- `/tmp/windows98_se_scas_validation.log`

Relevant shutdown lines:

- `CPU new dynarec fallback families [shutdown]: base=20856 0f=5565 x87=4568 rep=9016 3dnow=0`
- hottest remaining base fallback entries:
  - `0xee helper_table_null=5163`
  - `0xe6 helper_table_null=5088`
  - `0xec helper_table_null=3387`
  - `0xef helper_table_null=3170`
  - `0xd3 helper_bailout=921`
  - `0xd1 helper_bailout=747`
  - `0x9b helper_table_null=496`
  - `0xcd helper_table_null=378`
  - `0x8e helper_bailout=312`
  - `0xcf helper_table_null=304`
  - `0xd0 helper_bailout=214`
  - `0xd2 helper_bailout=145`
- smaller non-system table-hole entries still present:
  - `0x9e helper_table_null=49`
  - `0x9f helper_table_null=36`
  - `0xd5 helper_table_null=19`

Structural conclusion:

- the base bucket is still dominated by opcodes that are hot but unsafe:
  - port-I/O: `0xe4`, `0xe6`, `0xec`, `0xee`, `0xef`
  - interrupt/control: `0xcd`, `0xcf`
  - segment/control crossover: `0x8e`
  - wait/control crossover: `0x9b`
- the previous low-risk compare-string table-hole family is now exhausted:
  - `0xa6`, `0xa7`, `0xae`, and `0xaf` are all gone from the shutdown base-fallback report

## Re-check of the `D0`-`D3` idea

The earlier recommendation to revisit `D0`-`D3` still needs to be adjusted after code inspection.

Current code shape in `src/codegen_new/codegen_ops_shift.c`:

- non-`RCL` / non-`RCR` immediate-count forms are already direct
- non-`RCL` / non-`RCR` variable-count forms are already direct when:
  - `CL & 0x1f != 0`
  - `block->ins` is available
- explicit `RCL` / `RCR` guest dispatch remains disabled
- the compare/debug path in tree is scoped only to `RCL` / `RCR`

That means the remaining measured `0xd0`-`0xd3` fallback traffic is not a fresh low-risk family comparable to `CMPS` / `SCAS`.

Instead, it is a mixed structural bailout bucket made up of:

- the already-paused `RCL` / `RCR` semantics path
- zero-count exits on the variable-count forms
- `!block->ins` exits on the variable-count forms

Recommendation change:

- do not treat `D0`-`D3` as the next default implementation family
- if `D0`-`D3` is revisited, do it only after a dedicated measurement pass can separate:
  - `RCL` / `RCR`
  - zero-count traffic
  - `!block->ins` traffic

Without that separation, another `D0`-`D3` pass is not a clean low-risk closure candidate.

## Candidate comparison

### Rejected for next implementation step

#### Port-I/O and control families

- `0xe4`, `0xe6`, `0xec`, `0xee`, `0xef`
- `0xcd`, `0xcf`
- `0x8e`
- `0x9b`

Why rejected:

- high guest/system-model risk
- explicitly outside the current narrow CPU dynarec scope
- not appropriate side work for a low-risk follow-up session

#### Blocked `0F` retries

- `0x0f 0xaf`
- `0x0f 0xbc` / `0x0f 0xbd`
- bit-test family `0x0f 0xa3` / `0xab` / `0xb3` / `0xba`

Why rejected:

- already known guest-risky or explicitly paused
- not improved by the fresh `SCAS` validation result

#### `D0`-`D3` bailout cleanup

Why rejected as the immediate next landing:

- remaining traffic is bailout cleanup, not plain table-hole closure
- the measured bucket still includes the already-paused `RCL` / `RCR` path
- the branch does not currently log the remaining non-`RCL` / `RCR` bailout reasons as a separate exact shutdown slice

### Best next low-risk candidate

#### Base `0x9e` / `0x9f` (`SAHF` / `LAHF`)

Why this pair now leads:

- still plain `helper_table_null` traffic in the latest strict-i686 shutdown log
- coherent two-opcode family
- no memory access
- no device interaction
- no segment/control-register transfer semantics
- much smaller semantic surface than the remaining `D0`-`D3` bailout cleanup
- materially lower guest-risk profile than any remaining hot I/O/control base opcode

Measured payoff:

- `0x9e = 49`
- `0x9f = 36`
- combined = `85`

Why the lower count is still acceptable:

- the next choice should optimize for safety first, not raw count
- after `CMPS` and `SCAS`, the hotter remaining base items are hot for the wrong reason
- `SAHF` / `LAHF` is the clearest remaining plain base table-hole pair that does not reopen the paused risk classes

Secondary fallback candidate if `SAHF` / `LAHF` inspection finds hidden backend risk:

- base `0xd5` (`AAD`)

Why only secondary:

- lower count than `SAHF` / `LAHF`
- not a family
- flag behavior is less obviously trivial than the `AH`/flags transfer pair

## Recommendation

Recommended next target:

- base `0x9e` / `0x9f` (`SAHF` / `LAHF`)

Recommended workflow:

1. Audit current interpreter and backend support for `SAHF` / `LAHF`.
2. Confirm the arm64/x86-64 backend already supports the needed flag-register transfer primitives.
3. Add focused host-side coverage-policy assertions first.
4. Add the smallest coherent direct implementation for the pair.
5. Run focused local verification.
6. Do one narrow `Windows 98 SE` guest validation run.

Explicit non-recommendation:

- do not start the next implementation session with `D0`-`D3`
- do not treat the remaining `D0`-`D3` shutdown counts as proof that a safe non-`RCL` / `RCR` landing is ready without a new reason-splitting measurement pass

## Practical next step

The next session should begin with a short `SAHF` / `LAHF` code audit, not with another broad opportunity search and not with another `D0`-`D3` retry.
