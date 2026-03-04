# 86Box New Dynarec Correctness Audit

**Date**: 2026-03-03
**Auditor**: cpu-debug agent
**Scope**: `src/codegen_new/` — IR, register allocator, flag handling, block management, accumulator

---

## Executive Summary

The new dynarec is architecturally sound. It uses a clean three-stage pipeline (x86 decode -> UOPs -> native lowering) with a versioned SSA-like register model and reference-counted dead code elimination. The flag handling uses a lazy evaluation scheme consistent with the interpreter. Two definite bugs were found, along with several edge cases and design observations.

**Severity Legend**: BUG = confirmed incorrect behavior, EDGE = potential edge case, DESIGN = architectural observation, PERF = performance-only issue

---

## 1. IR Optimization Passes (`codegen_ir.c`)

### 1.1 Overview

The IR compiler (`codegen_ir_compile`) is a single-pass linear walk over the UOP array. It performs:
- Dead code elimination (via `codegen_reg_process_dead_list`)
- Register rename optimization for UOP_MOV (line 120-127)
- Immediate store optimization for UOP_MOV_IMM (line 114-118, gated by `CODEGEN_BACKEND_HAS_MOV_IMM`)
- Loop unrolling (lines 71-85, via `codegen_ir_set_unroll`)

### 1.2 Dead Code Elimination — Correct

**File**: `codegen_reg.c:876-912` (`codegen_reg_process_dead_list`)

The DCE is sound. It works by:
1. At `codegen_reg_write`, if the previous version of a register has zero refcount and isn't marked `REG_FLAGS_REQUIRED`, it's added to the dead list
2. `codegen_reg_process_dead_list` walks the dead list, setting UOPs to `UOP_INVALID`, and propagating refcount decrements to source registers
3. Barrier UOPs are correctly preserved (line 885: `!(uop->type & (UOP_TYPE_BARRIER | UOP_TYPE_ORDER_BARRIER))`)

Correctly handles:
- Transitive elimination (source registers cascaded into dead list)
- Barrier protection (side-effecting UOPs never eliminated)
- `REG_FLAGS_REQUIRED` marking for registers that must survive block exit

### 1.3 Register Rename Optimization — Correct with Caveat

**File**: `codegen_ir.c:120-127`

```c
if ((uop->type & UOP_MASK) == (UOP_MOV & UOP_MASK)
    && reg_version[...][...].refcount <= 1
    && reg_is_native_size(uop->src_reg_a)
    && reg_is_native_size(uop->dest_reg_a))
```

This renames the source register to the destination instead of emitting a MOV. Conditions are:
- Source will not be read again (refcount <= 1, where 1 is the current read)
- Both source and dest are native-sized (no partial register aliasing)

**[DESIGN-1]** The rename writes back the old value before renaming (line 800-801 in `codegen_reg_rename`). This is safe but generates an unnecessary store when the old value will never be read again. Minor performance opportunity.

### 1.4 Loop Unrolling — Correct

**File**: `codegen_ir.c:71-85`

Unrolling duplicates UOPs from `codegen_unroll_start` to `unroll_end`, adjusting jump destinations by the offset. The `duplicate_uop` function (line 38-63) correctly re-reads/re-writes register versions for each copy. Jump targets are adjusted by offset (line 61).

### 1.5 Jump Destination Linking — Correct

**File**: `codegen_ir.c:101-108, 157-178`

Forward jumps are tracked via a linked list (`jump_list_next`). When a UOP with `UOP_TYPE_JUMP_DEST` is encountered, all jumps in the list are resolved. Jumps to the end of the block are handled separately via `jump_target_at_end` (line 183-192). This avoids requiring a sentinel UOP at the end.

---

## 2. Flag Handling

### 2.1 Flag Architecture

The dynarec uses the same lazy flag evaluation as the interpreter, storing:
- `flags_op` — operation type (e.g., `FLAGS_ADD8`, `FLAGS_SUB32`, `FLAGS_ZN16`)
- `flags_res` — result value
- `flags_op1` — first operand
- `flags_op2` — second operand

Flag values are computed on-demand by `CF_SET()`, `ZF_SET()`, `NF_SET()`, `VF_SET()`, `AF_SET()`, `PF_SET()` in `src/cpu/x86_flags.h`.

### 2.2 `codegen_flags_changed` Tracking — Correct

**File**: `codegen_block.c:588`, `codegen.c:749`

`codegen_flags_changed` is set to 0 at block start and after any interpreter fallback (`codegen_generate_call` line 749). It's set to 1 by recompiled instructions that set `flags_op`/`flags_res`/`flags_op1`/`flags_op2`. Branch optimizations check `codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN` to decide whether to use fast-path comparisons or fall back to interpreter flag functions.

### 2.3 Branch Flag Optimizations — Correct but Incomplete

**File**: `codegen_ops_branch.c`

The branch handlers optimize for these flag types:
- `FLAGS_ZN{8,16,32}` — Zero/sign only (from AND, OR, XOR, TEST)
- `FLAGS_SUB{8,16,32}` — Full arithmetic flags (from SUB, CMP)
- `FLAGS_DEC{8,16,32}` — Like SUB but preserves CF (DEC)

**[DESIGN-2]** These flag types are NOT optimized in branch handlers and always fall through to the interpreter function call path:
- `FLAGS_ADD{8,16,32}` — handled only for JL/JNL/JLE/JNLE/JS/JNS (signed comparisons)
- `FLAGS_ADC{8,16,32}` — never optimized
- `FLAGS_SBC{8,16,32}` — never optimized
- `FLAGS_SHL{8,16,32}` — only optimized for JS/JNS (sign flag)
- `FLAGS_SHR{8,16,32}` — only optimized for JS/JNS
- `FLAGS_SAR{8,16,32}` — only optimized for JS/JNS
- `FLAGS_INC{8,16,32}` — handled like DEC
- `FLAGS_ROL{8,16,32}` / `FLAGS_ROR{8,16,32}` — never optimized

This is correct for safety — unhandled types fall to `default:` which calls the full interpreter flag function. It's a performance opportunity, not a bug. The most impactful improvement would be adding `FLAGS_ADD` support to `JB`/`JNB` (carry flag) paths, since ADD followed by JB/JNB is common (e.g., checking for overflow after addition).

### 2.4 NOT Instruction and `codegen_flags_changed`

**File**: `codegen_ops_misc.c:84-89`

```c
case 0x10: /*NOT*/
    uop_XOR_IMM(ir, reg, reg, 0xff);
    if ((fetchdat & 0xc0) != 0xc0)
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, reg);
    codegen_flags_changed = 1;
    return op_pc + 1;
```

**[DESIGN-3]** NOT sets `codegen_flags_changed = 1` but does NOT modify any flag registers (`flags_op`, `flags_res`, etc.). Per Intel SDM, NOT does not affect any flags. Setting `codegen_flags_changed = 1` here is harmless because it preserves the flag state from the previous instruction (which is correct — NOT shouldn't change it). However, if this NOT is the first recompiled instruction in a block, it would claim flags are valid when they haven't been set within the block. In practice this works because `cpu_state.flags_op` carries its interpreter-set value from before the block was entered, so the branch handler would still produce correct results. Not a bug, but misleading code.

### 2.5 INC/DEC Carry Flag Preservation — Correct

The interpreter's `CF_SET()` for `FLAGS_INC*`/`FLAGS_DEC*` returns `cpu_state.flags & C_FLAG` (line 477-484 of `x86_flags.h`), meaning it reads the previously-stored carry flag. The dynarec's recompiled INC/DEC handlers don't set `flags_op1`/`flags_op2` for carry calculation — they rely on the fact that the CF_SET function for INC/DEC reads the raw flags register, which is only updated at block boundaries via `flags_rebuild()`. This is consistent with the interpreter.

---

## 3. Block Boundary Handling (`codegen_block.c`)

### 3.1 Block Initialization — Correct

**File**: `codegen_block.c:504-536` (`codegen_block_init`)

Block initialization correctly sets up page tracking, hash table entry, and initial state. The block is allocated from the free list, which falls back to the dirty list, then random eviction.

### 3.2 Block Recompilation State — Correct

**File**: `codegen_block.c:547-612` (`codegen_block_start_recompile`)

State correctly reset:
- `codegen_flags_changed = 0` (line 588)
- `codegen_fpu_entered = 0` (line 589)
- `codegen_mmx_entered = 0` (line 590)
- All FPU loaded IQ flags cleared (line 592)
- Segment checked flags reset (line 594)
- TOP recorded (line 596)
- Register allocator reset (line 609)
- Accumulator reset (line 610)

### 3.3 Block End — Correct

**File**: `codegen_block.c:765-784` (`codegen_block_end_recompile`)

1. Timing block end called
2. Remaining cycles accumulated
3. Block removed from old position in page lists
4. Page masks regenerated
5. Block re-added to page lists
6. `CODEBLOCK_STATIC_TOP` cleared if no FPU ops
7. Accumulate flushed
8. IR compiled

### 3.4 Block Invalidation — Correct

**File**: `codegen_block.c:370-385` (`invalidate_block`)

Correctly removes block from page list and adds to dirty list. Memory is freed via `codegen_allocator_free`. The dirty list provides a 64-block history for self-modifying code recovery.

### 3.5 Dirty List Management — Correct with Edge Case

**File**: `codegen_block.c:91-130`

The dirty list is a doubly-linked list with a max size of 64. When exceeded, the oldest (tail) entry is evicted to the free list.

**[EDGE-1]** `block_dirty_list_remove` at line 135 accesses `codeblock[block->prev]` and `codeblock[block->next]` unconditionally. If `block->prev` or `block->next` is `BLOCK_INVALID` (which is checked right after), the access to `codeblock[BLOCK_INVALID]` could be a problem if `BLOCK_INVALID` is out of bounds. However, since the values are read but only used inside `if` guards checking for non-BLOCK_INVALID, and since the array access itself won't crash (just reads garbage that's never used), this is not a bug but slightly unsafe code.

### 3.6 Block Hash Collision — Correct

**File**: `codegen_block.c:519-520`

The block hash uses a simple hash of the physical address. Collision handling replaces the old entry: `codeblock_hash[block_num] = block_current`. The old block at that hash position remains valid (just not directly reachable via hash lookup), so the tree-based lookup (`codeblock_tree`) provides the fallback.

### 3.7 No Race Conditions

The dynarec runs single-threaded on the CPU emulation thread. Block management (init, recompile, invalidate, delete) all happen within this thread. Page writes that trigger `codegen_check_flush` also happen on the same thread. No races are possible.

---

## 4. Register Allocator (`codegen_reg.c`)

### 4.1 Algorithm

The register allocator uses a simple **linear scan with LRU eviction**:

1. **Allocation**: For each UOP, `codegen_reg_alloc_register` locks the host registers containing source/dest operands
2. **Read**: `codegen_reg_alloc_read_reg` searches for the register in host regs; if not found, evicts an unlocked register with no pending reads (or any unlocked register as fallback)
3. **Write**: `codegen_reg_alloc_write_reg` searches for the previous version of the register; if not found, allocates a new host register
4. **Flush**: At barriers and block end, all dirty registers are written back

### 4.2 Host Register Sets

Two independent sets: integer (`host_reg_set`, `CODEGEN_HOST_REGS`) and FP (`host_fp_reg_set`, `CODEGEN_HOST_FP_REGS`). The set is chosen by `ireg_data[].type` (REG_INTEGER or REG_FP).

### 4.3 Version Tracking — Correct

Uses a version counter per IR register (`reg_last_version[]`), with metadata in `reg_version[IREG_COUNT][256]`. This caps at 256 versions per register per block (with `REG_VERSION_MAX = 250` causing early block termination).

### 4.4 Partial Register Handling — Correct

**File**: `codegen_reg.c:693-714` (`codegen_reg_alloc_write_reg`)

When writing to a non-native-size register (e.g., writing AL when the host reg holds EAX), the parent register is loaded first (line 698-701). This ensures the upper bits are preserved. The version is bumped but the host register now contains the merged value.

### 4.5 Register Locking — Correct

**File**: `codegen_reg.c:556-571, 573-602`

Before processing a UOP, all source and dest registers are locked (`locked |= (1 << c)`). This prevents eviction of registers needed by the current UOP. The lock bitmap is reset at the start of each UOP (line 609-610 of `codegen_reg_alloc_register`).

### 4.6 Volatile Register Optimization — Correct

**File**: `codegen_reg.c:403`

Volatile registers (temporaries `IREG_temp0`..`IREG_temp3`, `IREG_temp0d`, `IREG_temp1d`) are not written back if they have no remaining readers. They're stored on the stack at fixed offsets.

**[DESIGN-4]** The register allocator has no concept of register pairs or operand constraints. Instructions that need specific registers (e.g., x86 MUL requiring EAX/EDX) are handled at the UOP level via `UOP_CALL_INSTRUCTION_FUNC` (falling back to the interpreter). This is correct but means complex instructions can't benefit from the JIT.

---

## 5. Accumulator (`codegen_accumulate.c`)

### 5.1 Scope — Very Limited

The accumulator currently only handles **cycle counting** (`ACCREG_cycles`). There is only one accumulator register.

```c
static struct {
    int count;
    int dest_reg;
} acc_regs[] = {
    [ACCREG_cycles] = {0, IREG_cycles}
};
```

### 5.2 Mechanism — Correct

- `codegen_accumulate(ir, ACCREG_cycles, delta)` adds `delta` to a running counter
- `codegen_accumulate_flush(ir)` emits a single `uop_ADD_IMM` to apply the accumulated delta
- `codegen_accumulate_reset()` zeros the counter at block start

This coalesces multiple cycle deductions into a single ADD instruction at block boundaries and before jump instructions.

### 5.3 NOT a Flag Accumulator

**[DESIGN-5]** Despite the name suggesting generality, the accumulator does NOT handle flags. There is no lazy flag accumulation in the IR layer. The lazy flag scheme is entirely in the `flags_op`/`flags_res`/`flags_op1`/`flags_op2` state machine evaluated by `CF_SET()`/`ZF_SET()` etc. The dynarec participates in this by setting these registers via UOPs and using `codegen_flags_changed` to track whether the current instruction has set them. The branch handlers then use this information to emit efficient comparison UOPs instead of calling the interpreter's flag functions.

### 5.4 USE_ACYCS — Conditional Feature

When `USE_ACYCS` is defined, each cycle accumulate also emits an `ADD_IMM` to `IREG_acycs`. This appears to be a debugging/profiling feature (accumulated cycle counter).

---

## 6. CONFIRMED BUGS

### BUG-1: `is_a16` Double-Assignment in `uop_gen_reg_dst_src2_imm`

**File**: `codegen_ir_defs.h:520-536`
**Severity**: BUG (incorrect 16-bit address wrapping on ARM64 for segment load instructions)

```c
static inline void
uop_gen_reg_dst_src2_imm(uint32_t uop_type, ir_data_t *ir, int dest_reg, int src_reg_a, int src_reg_b, uint32_t imm)
{
    uop_t *uop = uop_alloc(ir, uop_type);

    uop->type       = uop_type;
    uop->is_a16     = 0;           // Line 526: initial clear
    uop->src_reg_a  = codegen_reg_read(src_reg_a);
    if (src_reg_b == IREG_eaa16) {
        uop->src_reg_b  = codegen_reg_read(IREG_eaaddr);
        uop->is_a16     = 1;       // Line 530: correctly set for 16-bit EA
    } else
        uop->src_reg_b  = codegen_reg_read(src_reg_b);
    uop->is_a16     = 0;           // Line 533: BUG — unconditionally clears is_a16
    uop->dest_reg_a = codegen_reg_write(dest_reg, ir->wr_pos - 1);
    uop->imm_data   = imm;
}
```

Line 533 unconditionally clears `is_a16`, overwriting the `is_a16 = 1` set on line 530 when `src_reg_b == IREG_eaa16`.

**Impact**: The `is_a16` flag controls 16-bit address wrapping in the ARM64 backend (`codegen_backend_arm64_uops.c:936-937`):
```c
if (uop->is_a16)
    host_arm64_AND_IMM(block, REG_X0, REG_X0, 0xffff);
```

And in the x86-64 backend (`codegen_backend_x86-64_uops.c:1008-1010`):
```c
if (uop->is_a16) {
    host_x86_AND32_REG_IMM(block, REG_ESI, 0x0000ffff);
}
```

With `is_a16` always 0, memory accesses using 16-bit effective addresses with offsets will not wrap at 0xFFFF. This affects `LES`/`LDS`/`LFS`/`LGS`/`LSS` instructions in 16-bit address mode, where the second load (at offset +2 or +4) could read from the wrong address if the base EA is near 0xFFFF.

**Fix**: Remove line 533 (`uop->is_a16 = 0;`).

### BUG-2: `jump_cycles` Return Value Discarded

**File**: `codegen.c:611-620`
**Severity**: BUG (timing-only — affects cycle accuracy, not computational correctness)

```c
int jump_cycles = 0;

if (codegen_timing_jump_cycles)
    codegen_timing_jump_cycles();    // Line 614: return value DISCARDED

if (jump_cycles)                     // Line 616: always 0
    codegen_accumulate(ir, ACCREG_cycles, -jump_cycles);
codegen_accumulate_flush(ir);
if (jump_cycles)                     // Line 619: always 0
    codegen_accumulate(ir, ACCREG_cycles, jump_cycles);
```

`codegen_timing_jump_cycles` is a function pointer typed as `int (*)(void)` (codegen.c:66). It returns a cycle count (e.g., K6 returns 1 if decode buffer has uops). But line 614 calls it as a statement, discarding the return value. The local `jump_cycles` stays 0, making the entire jump cycle adjustment a dead no-op.

**Impact**: On K6, K5, and P6 timing models, jump instructions that can pair with subsequent instructions won't have their cycles properly deducted when the jump is taken. This can lead to zero-cycle blocks (e.g., a tight `JMP $` loop consuming no cycles). The comment on lines 606-610 explicitly describes this as the scenario the code is meant to prevent.

**Fix**: Change line 614 to:
```c
jump_cycles = codegen_timing_jump_cycles();
```

---

## 7. Edge Cases and Observations

### EDGE-2: `flags_res_valid()` Returns 0 for ROL/ROR

**File**: `x86_flags.h:536-543`

```c
static __inline int flags_res_valid(void) {
    if ((cpu_state.flags_op == FLAGS_UNKNOWN)
        || ((cpu_state.flags_op >= FLAGS_ROL8) && (cpu_state.flags_op <= FLAGS_ROR32)))
        return 0;
    return 1;
}
```

After ROL/ROR instructions, `flags_res_valid()` returns 0, causing branch handlers like `ropJE_common` (line 214-215) to fall back to calling `ZF_SET()`. This is correct because ROL/ROR only affect CF and OF, not ZF — so `flags_res` doesn't carry ZF information. However, `ZF_SET()` for `FLAGS_ROL*`/`FLAGS_ROR*` returns `cpu_state.flags & Z_FLAG`, meaning it reads from the raw flags register which may not reflect the latest ZF from before the ROL/ROR. This could be incorrect if the flags register hasn't been rebuilt since a previous flag-modifying instruction. In practice, the `codegen_flags_changed` variable would be set by the ROL/ROR handler, but `flags_res_valid()` returns 0, so the branch handler calls `ZF_SET()` which reads the raw flags. If the raw flags haven't been rebuilt since the last flag-modifying instruction, this could produce a stale result.

However, this matches the interpreter behavior exactly (both use the same `ZF_SET()` function), so it's not a divergence between JIT and interpreter.

### EDGE-3: `reg_version` Array Size

**File**: `codegen_reg.c:16`

```c
reg_version_t reg_version[IREG_COUNT][256];
```

Each register has 256 version slots. `REG_VERSION_MAX = 250` triggers early block termination (line 382-383 of `codegen_reg.h`). Since versions start at 0 and increment by 1, this provides a safety margin of 6 versions. Overflow past 256 would silently wrap due to `uint8_t` arithmetic, but `CPU_BLOCK_END()` prevents this.

### EDGE-4: `refcount` is `uint8_t`

**File**: `codegen_reg.h:291`

```c
typedef struct {
    uint8_t refcount;
    ...
} reg_version_t;
```

The refcount is a `uint8_t`, meaning it wraps at 256. `REG_REFCOUNT_MAX = 250` triggers early block termination (line 347-348). The debug build catches underflow (line 678-679 of `codegen_reg.c`). This is safe but imposes a limit on how many times a single register version can be read within a block.

### DESIGN-6: No Block Linking

The dynarec does not implement block chaining/linking. Every block exits through `codegen_exit_rout` which returns to the main CPU loop. This means block transitions always go through the dispatch overhead. Block linking would be a significant performance improvement for tight loops and sequential code.

### DESIGN-7: Single Static Assignment Without Phi Nodes

The versioned register scheme is SSA-like but doesn't have phi nodes for merge points. Since blocks are straight-line code with possible early exits (jumps that leave the block) but no internal control flow merges, this is correct. Loop unrolling creates linear copies without merge points.

---

## 8. Verification Infrastructure

### 8.1 Existing Branch

**Branch**: `feature/x86-64-jit-verify`

A single commit (`7b629d167 — "Voodoo JIT: add debug logging and verify mode for x86-64"`) exists on this branch. This appears to be related to the Voodoo graphics JIT, not the CPU dynarec. No CPU dynarec verification infrastructure exists.

### 8.2 Debug Infrastructure

- `#ifdef DEBUG_EXTRA`: Enables `instr_counts[]` array for instruction frequency tracking (`codegen_block.c:57`, `codegen.c:734-735`)
- `#ifndef RELEASE_BUILD`: Numerous fatal() assertions throughout the codebase for invariant checking
- `#if 0` blocks: Several commented-out pclog() debugging statements throughout the IR and register allocator
- `#ifdef USE_ACYCS`: Optional accumulated cycle counter for timing validation
- No JIT-vs-interpreter comparison mode exists for the CPU dynarec

### 8.3 Recommendations for Verification

1. **Per-instruction flag comparison**: After each recompiled instruction, compare `flags_op`/`flags_res`/`flags_op1`/`flags_op2` against interpreter-computed values
2. **Block result comparison**: At block exit, compare all register values against interpreter execution of the same instruction sequence
3. **Random block invalidation**: Periodically invalidate JIT blocks to force interpreter fallback, checking for behavioral differences
4. **Coverage instrumentation**: Count how often each recompiled handler is used vs. interpreter fallback

---

## 9. Files Examined

| File | Lines | Purpose |
|------|-------|---------|
| `src/codegen_new/codegen_ir.c` | 200 | IR compilation, loop unrolling |
| `src/codegen_new/codegen_ir.h` | 7 | IR interface |
| `src/codegen_new/codegen_ir_defs.h` | 920 | UOP definitions, uop_alloc, all UOP macros |
| `src/codegen_new/codegen_accumulate.c` | 44 | Cycle accumulator |
| `src/codegen_new/codegen_accumulate.h` | 12 | Accumulator interface |
| `src/codegen_new/codegen_reg.c` | 912 | Register allocator, load/store, rename |
| `src/codegen_new/codegen_reg.h` | 427 | Register definitions, IREG enums, version tracking |
| `src/codegen_new/codegen_block.c` | 841 | Block management, page tracking, invalidation |
| `src/codegen_new/codegen.c` | 771 | Top-level compilation, instruction dispatch |
| `src/codegen_new/codegen_ops_arith.c` | ~1800 | ADD/ADC/SUB/SBB/CMP handlers |
| `src/codegen_new/codegen_ops_logic.c` | ~500 | AND/OR/XOR/TEST handlers |
| `src/codegen_new/codegen_ops_misc.c` | ~450 | LEA/NOT/NEG/TEST handlers |
| `src/codegen_new/codegen_ops_mov.c` | ~400 | MOV/MOVZX/MOVSX handlers |
| `src/codegen_new/codegen_ops_branch.c` | ~1050 | Conditional branch handlers |
| `src/codegen_new/codegen_ops_helpers.h` | 128 | Stack, FPU, segment limit helpers |
| `src/codegen_new/codegen_backend_arm64_uops.c` | (partial) | ARM64 backend is_a16 usage |
| `src/codegen_new/codegen_backend_x86-64_uops.c` | (partial) | x86-64 backend is_a16 usage |
| `src/cpu/x86_flags.h` | 826 | Flag evaluation functions |
| `src/cpu/codegen_timing_k6.c` | (partial) | K6 timing model jump_cycles |

---

## 10. Summary of Findings

| ID | Type | Severity | Component | Description |
|----|------|----------|-----------|-------------|
| BUG-1 | Bug | Medium | `codegen_ir_defs.h:533` | `is_a16` unconditionally cleared — 16-bit address wrap broken for segment loads |
| BUG-2 | Bug | Low | `codegen.c:614` | `jump_cycles` return value discarded — zero-cycle blocks possible |
| DESIGN-1 | Design | Info | `codegen_reg.c:800` | Unnecessary writeback before rename |
| DESIGN-2 | Design | Info | `codegen_ops_branch.c` | Many flag types not optimized in branch handlers |
| DESIGN-3 | Design | Info | `codegen_ops_misc.c:89` | NOT sets `codegen_flags_changed` without modifying flags |
| DESIGN-4 | Design | Info | Register allocator | No operand constraint support |
| DESIGN-5 | Design | Info | Accumulator | Only accumulates cycles, not flags |
| DESIGN-6 | Design | Info | Block management | No block linking/chaining |
| DESIGN-7 | Design | Info | IR | SSA-like without phi nodes (correct for linear blocks) |
| EDGE-1 | Edge | Low | `codegen_block.c:135` | Dirty list accesses potentially invalid indices before guard |
| EDGE-2 | Edge | Low | `x86_flags.h:537` | ROL/ROR flag state and `flags_res_valid` interaction |
| EDGE-3 | Edge | Info | `codegen_reg.c:16` | 256-version limit with safety margin |
| EDGE-4 | Edge | Info | `codegen_reg.h:291` | uint8_t refcount with 250 safety limit |
