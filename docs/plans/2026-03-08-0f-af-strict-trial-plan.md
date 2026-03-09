# `0F AF` Strict Trial Plan

> **For Claude/Codex:** follow the host-harness-first workflow. Do not start with a guest run. Do not mix this work with `0f ba`, the bit-test family, or any protected/system opcode work.

## Goal

Implement and validate only `0x0f 0xaf`:

- `IMUL r16/32, r/m16/32`

This plan exists because the first direct `0f af` landing attempt was backed out after guest boot failure, while later work proved that adjacent regressions in the same timeframe can hide inside broader `0F` batches.

## Why `0F AF` next

Current stable baseline facts:

- Stable branch state:
  - `SETcc` (`0x90`-`0x9f`) is enabled with corrected boolean normalization
  - `BSWAP` (`0xc8`-`0xcf`) is enabled and guest-validated
  - direct `BSF` / `BSR` dispatch is disabled again after guest-visible protection failure
- Latest stable `Windows 98 Gaming PC` shutdown log:
  - `/tmp/windows98_gaming_pc_new_dynarec.log`
  - `CPU new dynarec fallback families [shutdown]: base=7271 0f=7831 x87=3566 rep=13251 3dnow=0`
  - `CPU new dynarec 0F fallbacks [shutdown]: opcode=0xaf helper_table_null=2863 helper_bailout=0`
  - `CPU new dynarec 0F fallbacks [shutdown]: opcode=0xba helper_table_null=2067 helper_bailout=0`

Reason to pick `0f af` over `0f ba` first:

- `0f af` is a single opcode with one coherent arithmetic semantic family.
- It is lower-risk than `0f ba`, which is a four-way ModRM subgroup with bit addressing, memory-element selection, and read/modify/write semantics.
- It is still one of the hottest remaining `0F` `helper_table_null` entries on the new preferred guest VM.

## Scope

In scope:

- direct `0f af` only
- host-side synthetic semantics harness coverage first
- focused policy-surface assertions if the opcode becomes direct-covered
- one logged guest confirmation run after host-side validation passes

Out of scope:

- `0f ba`
- `0f a3` / `0f ab` / `0f b3`
- `0f 02` / `0f 03`
- any new `BSF` / `BSR` guest retry
- any REP/x87/softfloat work

## Required implementation shape

The direct path should stay narrow:

- support only the plain direct arithmetic form of `IMUL r16/32, r/m16/32`
- use helper-backed computation if that is the safest route
- do not combine this with `0x69`/`0x6b` refactors unless strictly necessary
- do not broaden to another opcode in the same change

Expected semantic obligations:

- signed multiply result truncation into destination width
- destination register writeback
- correct `CF` / `OF` behavior
- unchanged architectural behavior for the other flags relative to current model
- correct zero/non-zero high-half overflow detection for both 16-bit and 32-bit forms
- correct handling of register source and memory source forms

## Validation workflow

### Phase 1: host-side semantics harness first

Extend `tests/codegen_new_0f_semantics_test.c` and `src/codegen_new/codegen_test_support.c` to cover:

- 16-bit register-source cases
- 16-bit memory-equivalent source cases
- 32-bit register-source cases
- 32-bit memory-equivalent source cases
- positive * positive
- positive * negative
- negative * negative
- overflow and non-overflow examples
- edge-adjacent signed values

Minimum requirement before guest trial:

- host-side result helpers cover both result and overflow-mask semantics
- focused standalone test passes cleanly

### Phase 2: policy/coverage verification

If `0f af` becomes direct-covered, update the focused coverage-policy test to assert exactly that opcode and no speculative neighbors.

### Phase 3: one guest checkpoint only

Use only one logged guest confirmation run after the host-side path is green:

- VM:
  - `/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 Gaming PC`
- Logging:
  - `86BOX_NEW_DYNAREC_STATS=1`
  - `86BOX_NEW_DYNAREC_LOG_FALLBACK_FAMILIES=1`
  - `86BOX_NEW_DYNAREC_LOG_0F_FALLBACKS=1`

Success criteria:

- guest boots to desktop on the `Windows 98 Gaming PC` VM
- no protection error during boot
- shutdown log shows `0xaf` removed or materially reduced from the `0F` fallback report
- no new guest-visible regression compared with the current stable baseline

Failure criteria:

- boot hang
- Windows protection error
- guest-visible instability attributable to the new direct path

If failure happens:

- revert only `0xaf` direct dispatch
- keep any reusable harness improvements
- do not leave the unstable direct path enabled

## Exact verification commands

Required pre-guest commands:

```bash
cc -std=c11 -DUSE_NEW_DYNAREC -Isrc/cpu tests/codegen_new_dynarec_observability_test.c src/codegen_new/codegen_observability.c -o /tmp/codegen_new_dynarec_observability_test && /tmp/codegen_new_dynarec_observability_test
cc -std=c11 -DUSE_NEW_DYNAREC -Isrc/include -Isrc/cpu tests/codegen_new_opcode_coverage_policy_test.c src/codegen_new/codegen_observability.c -o /tmp/codegen_new_opcode_coverage_policy_test && /tmp/codegen_new_opcode_coverage_policy_test
cc -std=c11 -DUSE_NEW_DYNAREC -Isrc/include -Isrc/cpu tests/codegen_new_allocator_pressure_policy_test.c src/codegen_new/codegen_observability.c -o /tmp/codegen_new_allocator_pressure_policy_test && /tmp/codegen_new_allocator_pressure_policy_test
cc -std=c11 -DUSE_NEW_DYNAREC -Isrc/include -Isrc/cpu -Isrc/codegen_new tests/codegen_new_0f_semantics_test.c src/codegen_new/codegen_test_support.c -o /tmp/codegen_new_0f_semantics_test && /tmp/codegen_new_0f_semantics_test
cmake --build out/build/llvm-macos-aarch64.cmake --target 86Box cpu dynarec mem -j4
codesign -s - --force --deep out/build/llvm-macos-aarch64.cmake/src/86Box.app
```

Required guest launch command:

```bash
/bin/zsh -lc 'env 86BOX_NEW_DYNAREC_STATS=1 86BOX_NEW_DYNAREC_LOG_FALLBACK_FAMILIES=1 86BOX_NEW_DYNAREC_LOG_0F_FALLBACKS=1 open -n -a "/Users/anthony/projects/code/86Box-voodoo-arm64/out/build/llvm-macos-aarch64.cmake/src/86Box.app" --args -L /tmp/windows98_gaming_pc_0f_af_validation.log -P "/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 Gaming PC"'
```

## Deliverables

- harness coverage for `0f af`
- minimal `0f af` direct implementation if the harness-backed path is clear
- updated policy assertions
- one logged guest result on the `Windows 98 Gaming PC` VM
- exact commands and exact results
- explicit keep/backout decision for `0f af`

## Trial result (2026-03-08)

Outcome:

- Host-side semantics harness: landed and passing
- Narrow helper-backed direct implementation: landed for trial only
- Guest result: failed
- Final keep/backout decision: back out only direct `0x0f 0xaf` table dispatch and policy exposure; keep the reusable harness/debugging code

Host-side result:

- `tests/codegen_new_0f_semantics_test.c` now covers:
  - 16-bit register-source and memory-equivalent source cases
  - 32-bit register-source and memory-equivalent source cases
  - positive/positive, positive/negative, negative/negative, and overflow/non-overflow examples
  - result truncation plus `CF` / `OF` overflow-mask behavior
- The direct-path trial used the narrowest available implementation shape:
  - decode/fetch/writeback in the direct handler
  - helper-backed signed-multiply result
  - helper-backed `CF` / `OF` overflow-mask computation
  - no widening into `0x69` / `0x6b`, bit-test-family, or protected/system work

Guest checkpoint result:

- Launch command used exactly:

```bash
/bin/zsh -lc 'env 86BOX_NEW_DYNAREC_STATS=1 86BOX_NEW_DYNAREC_LOG_FALLBACK_FAMILIES=1 86BOX_NEW_DYNAREC_LOG_0F_FALLBACKS=1 open -n -a "/Users/anthony/projects/code/86Box-voodoo-arm64/out/build/llvm-macos-aarch64.cmake/src/86Box.app" --args -L /tmp/windows98_gaming_pc_0f_af_validation.log -P "/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 Gaming PC"'
```

- The VM did not boot to desktop.
- The guest-visible failure was:
  - `Insufficient memory to initialize Windows.`
- The logged shutdown profile was:
  - `CPU new dynarec fallback families [shutdown]: base=688 0f=38 x87=0 rep=375 3dnow=0`
- The shutdown `0F` fallback report no longer listed `0xaf`, but that result is not treated as success because the run terminated far too early to compare against the stable baseline.

Final state to keep:

- keep:
  - `0x0f 0xaf` host-side semantics helpers and harness coverage
  - reusable helper-backed direct-handler code for future debugging
- do not keep enabled:
  - direct `0x0f 0xaf` table dispatch
  - direct-coverage policy exposure for `0xaf`

## Post-trial debug follow-up (2026-03-08)

Follow-up outcome:

- Added a temporary opt-in `0xaf`-only guest debug override (`86BOX_NEW_DYNAREC_DEBUG_0F_AF=1`) without re-exposing `0xaf` on the normal direct-coverage policy surface.
- Added shutdown summary logging for direct `0xaf` attempts, including sampled width, source, destination, result, and `CF` / `OF` mask.
- Added compare logging for helper-expected versus direct-path actual destination writeback and `CF` / `OF` mask.

Compare-debug result:

- The compare-enabled shutdown log at `/tmp/windows98_gaming_pc_0f_af_debug.log` reported:
  - `attempts=204 direct=204 helper_table_null=0 helper_bailout=0`
  - `dest_mismatches=0`
  - `flags_mismatches=0`
  - `last_expected_dest=0x00005100 last_actual_dest=0x00005100`
  - `last_expected_flags=0x00000801 last_actual_flags=0x00000801`
- The guest still failed with the same Windows boot error:
  - `Insufficient memory to initialize Windows.`

Updated conclusion:

- The retained direct `0xaf` implementation now has positive guest-side evidence that its sampled destination writeback and `CF` / `OF` behavior match the helper-visible expectation.
- That narrows the remaining regression away from the obvious `IMUL` result/overflow semantics and toward broader integration state around the direct path, or a neighboring trigger that is only correlated with `0xaf`.
- Stable kept branch state should therefore remain:
  - keep the harness/debugging support
  - keep direct `0xaf` dispatch disabled
  - remove the temporary guest override/compare logging once the debug pass is complete
