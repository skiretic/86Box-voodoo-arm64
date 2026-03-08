# 3DNow Final Coverage Batch Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Close the remaining direct CPU new dynarec 3DNow/Enhanced 3DNow opcode coverage gap on both x86-64 and arm64, with cumulative regression tests that remain in tree.

**Architecture:** Reuse the existing shared 3DNow frontend path in `src/codegen_new/codegen.c` and complete the missing direct suffix handlers in `src/codegen_new/codegen_ops_3dnow.c`, then add the required IR/backend support on both host backends. Keep testing cumulative by asserting exact direct-coverage membership for all supported 3DNow suffixes rather than only “some 3DNow exists”.

**Tech Stack:** C11, existing CPU new dynarec frontend/backend code, focused standalone tests, CMake target build.

### Task 1: Lock the remaining gap in a cumulative failing test

**Files:**
- Modify: `tests/codegen_new_opcode_coverage_policy_test.c`
- Modify: `src/cpu/codegen_public.h`
- Modify: `src/codegen_new/codegen_observability.c`

**Step 1: Write the failing test**

Expand the coverage-policy test so it asserts exact direct 3DNow suffix coverage, not just a boolean:
- already-supported suffixes still return direct support
- remaining missing suffixes now must also return direct support
- the direct-support count matches the full intended table size

**Step 2: Run test to verify it fails**

Run:
```bash
cc -std=c11 -DUSE_NEW_DYNAREC -Isrc/include -Isrc/cpu tests/codegen_new_opcode_coverage_policy_test.c src/codegen_new/codegen_observability.c -o /tmp/codegen_new_opcode_coverage_policy_test && /tmp/codegen_new_opcode_coverage_policy_test
```

Expected: fail because the new helper reports the missing suffixes as unsupported.

**Step 3: Write minimal implementation**

Add a small query helper layer in `codegen_public.h` / `codegen_observability.c`:
- `new_dynarec_has_direct_3dnow_opcode_recompile(uint8_t opcode)`
- `new_dynarec_direct_3dnow_opcode_count(void)`

Initially wire it to the current supported set so the test gives a precise red signal for the missing suffixes.

**Step 4: Run test to verify the red case is correct**

Re-run the standalone test and confirm it fails only on the currently missing suffixes.

### Task 2: Implement the remaining 3DNow frontend handlers

**Files:**
- Modify: `src/codegen_new/codegen_ops_3dnow.c`
- Modify: `src/codegen_new/codegen_ops_3dnow.h`
- Modify: `src/codegen_new/codegen_ops.c`

**Step 1: Add missing recompiler handlers**

Implement direct recompilers for:
- `0x0c` `PI2FW`
- `0x1c` `PF2IW`
- `0x8a` `PFNACC`
- `0x8e` `PFPNACC`
- `0xae` `PFACC`
- `0xb7` `PMULHRW`
- `0xbb` `PSWAPD`
- `0xbf` `PAVGUSB`

Use existing 3DNow/MMX load patterns so reg-reg and reg-mem forms behave like the current direct subset.

**Step 2: Wire the shared 3DNow table**

Populate the matching `recomp_opcodes_3DNOW[256]` entries in `src/codegen_new/codegen_ops.c`.

**Step 3: Keep the coverage helper cumulative**

Update the coverage helper implementation so the new suffixes are reported as direct-supported without removing the old ones.

### Task 3: Add IR/backend support for the new operations

**Files:**
- Modify: `src/codegen_new/codegen_ir_defs.h`
- Modify: `src/codegen_new/codegen_backend_arm64_uops.c`
- Modify: `src/codegen_new/codegen_backend_x86-64_uops.c`
- Modify as needed: `src/codegen_new/codegen_backend_arm64_ops.c`
- Modify as needed: `src/codegen_new/codegen_backend_arm64_ops.h`
- Modify as needed: `src/codegen_new/codegen_backend_x86-64_ops_sse.c`
- Modify as needed: `src/codegen_new/codegen_backend_x86-64_ops_sse.h`

**Step 1: Add only the uops actually needed**

Introduce the minimal new uops required for the eight missing semantics. Reuse existing MMX/vector uops when possible instead of inventing parallel abstractions.

**Step 2: Implement x86-64 backend lowering**

Use the existing SSE/MMX lowering style already used for the current direct 3DNow set.

**Step 3: Implement arm64 backend lowering**

Use only ARMv8.0-A-compatible NEON/scalar sequences. Do not rely on newer ARM ISA features.

### Task 4: Verify cumulatively

**Files:**
- Test: `tests/codegen_new_opcode_coverage_policy_test.c`

**Step 1: Run focused standalone tests**

Run:
```bash
cc -std=c11 -DUSE_NEW_DYNAREC -Isrc/include -Isrc/cpu tests/codegen_new_opcode_coverage_policy_test.c src/codegen_new/codegen_observability.c -o /tmp/codegen_new_opcode_coverage_policy_test && /tmp/codegen_new_opcode_coverage_policy_test
cc -std=c11 -DUSE_NEW_DYNAREC -Isrc/include -Isrc/cpu tests/codegen_new_allocator_pressure_policy_test.c src/codegen_new/codegen_observability.c -o /tmp/codegen_new_allocator_pressure_policy_test && /tmp/codegen_new_allocator_pressure_policy_test
cc -std=c11 -DUSE_NEW_DYNAREC -Isrc/cpu tests/codegen_new_dynarec_observability_test.c src/codegen_new/codegen_observability.c -o /tmp/codegen_new_dynarec_observability_test && /tmp/codegen_new_dynarec_observability_test
```

Expected: all pass.

**Step 2: Run target build**

Run:
```bash
cmake --build build --target 86Box cpu dynarec mem -j4
```

Expected: success, with only the existing macOS/Homebrew deployment-target linker warnings.

### Task 5: Update docs

**Files:**
- Modify: `docs/plans/new-dynarec-executive-summary.md`
- Modify: `docs/plans/new-dynarec-changelog.md`
- Modify: `docs/plans/new-dynarec-optimization-overview.md`

**Step 1: Record the batch strategy**

Document that direct 3DNow closure is now being completed as a cumulative batch program with regression checks that keep earlier opcode support visible.

**Step 2: Record the final-gap batch status**

Document the eight-op remaining gap, what landed in the current batch, and what still remains if implementation stops short.

## Follow-up validation status

- The direct generator batch is now landed.
- The cumulative coverage-policy test locks the full 24-suffix direct 3DNow set.
- Runtime validation also grew beyond one-opcode verify filters:
  - `86BOX_NEW_DYNAREC_LOG_3DNOW_HITS=1` now emits one shutdown line per exercised 3DNow suffix.
  - Guest runs have so far observed these suffixes staying on the direct path with no `helper_table_null` or `helper_bailout` hits:
    - `0x0d`, `0x1d`, `0x90`, `0x94`, `0x96`, `0x97`, `0x9a`, `0x9e`, `0xa0`, `0xa4`, `0xa6`, `0xaa`, `0xae`, `0xb0`, `0xb4`, `0xb6`
- Still-unhit direct-capable suffixes as of the latest pause point:
  - `0x0c`, `0x1c`, `0x8a`, `0x8e`, `0xa7`, `0xb7`, `0xbb`, `0xbf`
- Next logical extension if this plan resumes:
  - broaden the workload corpus rather than adding more 3DNow table work
