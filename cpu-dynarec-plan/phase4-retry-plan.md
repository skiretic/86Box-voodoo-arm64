# Phase 4 Block Linking — Retry Plan

## Background

Phase 4 was implemented (8 commits, 387a88199..5919b02b5) and reverted (4d5fbbb42) due to 3 bugs:
1. W^X violation (SIGBUS on macOS ARM64)
2. No cycle guard in linked chains (infinite hang)
3. `host_arm64_branch_set_offset` encoding mismatch with B.cond

The design was sound. This retry fixes all 3 bugs from the start.

## ARM64 Backend Audit Findings (Key Constraints)

### Prologue (208 bytes of stack frame)
- 10 STP instructions: V14-V15, V12-V13, V10-V11, V8-V9 (SIMD callee-saved), X29-X30, X27-X28, X25-X26, X23-X24, X21-X22, X19-X20
- Last STP (X19/X20) uses offset -64 (not -16), creating 48-byte local area
- FPU TOP diff stored at `[SP+32]` (`IREG_TOP_diff_stack_offset`)
- After STPs: MOVX_IMM to set REG_CPUSTATE (X29), optional FPU TOP init

### Epilogue (44 bytes)
- 10 LDP instructions (reverse of prologue) + RET X30
- `codegen_exit_rout` is identical to the epilogue sequence

### Critical: `codegen_exit_rout` Location
- Lives in block 0's code buffer (init block with load/store routines)
- May be >128MB away from user blocks — **cannot assume B range**
- Current `codegen_JMP` uses `host_arm64_jump` (MOVX_IMM X16 + BR X16) which handles any distance
- **Unpatch cannot restore a single B instruction to codegen_exit_rout — may be out of range**

### W^X Handling
- Only done around recompilation in `386_dynarec.c` (lines 550, 641)
- Block linking runs AFTER `code()` returns, outside the W^X window
- Patch/unpatch MUST add their own W^X toggles

### Scratch Registers at Block Exit
- X7 (REG_TEMP), X6 (REG_TEMP2), X16 (IP0), X0-X5: all available
- Register allocator has spilled everything back to cpu_state before JMP UOP

### `host_arm64_branch_set_offset`
- Uses `OFFSET26` — ONLY correct for unconditional `B` instruction
- `BLE_`, `BGT_`, etc. return a pointer to the SECOND instruction (the unconditional B) in a 2-instruction pattern: `B.cond_inv +8` / `B target`
- So `branch_set_offset` IS actually safe when called on the pointer from `BGT_()` etc — it patches the unconditional B, not the conditional one
- **However**: for block linking, we should avoid this complexity entirely. Use raw instruction encoding for the fixed `B.GT +8` skip.

## Design

### Exit Stub: 5 Instructions (20 bytes) per Exit

```asm
LDR   W16, [X29, #_cycles_offset]  ; load cpu_state._cycles (4 bytes)
CMP   W16, #0                       ; compare against zero (4 bytes)
B.GT  +8                             ; skip bail if cycles > 0 (4 bytes, raw 0x5400004c)
B     codegen_exit_rout              ; bail to dispatcher (4 bytes, OFFSET26) [NOTE 1]
B     codegen_exit_rout              ; patchable — patched to B target_entry when linked (4 bytes)
```

**NOTE 1**: If `codegen_exit_rout` is out of B range (>128MB), fall back to the existing `host_arm64_jump` (MOVX_IMM+BR) sequence and do NOT emit a patchable stub. This block simply won't be linkable — acceptable for correctness, unlikely in practice since the code buffer is allocated contiguously.

**exit_patch_offset** points to the LAST instruction (the patchable B).

**When unlinked**: Both B instructions go to `codegen_exit_rout`. Cycles > 0 → take patchable B → exit. Cycles <= 0 → take bail B → exit. Either way, returns to dispatcher.

**When linked**: Patchable B is patched to `B target_block_entry`. Cycles > 0 → jump to target. Cycles <= 0 → bail to dispatcher.

### Epilogue Exit Stub

Same 5-instruction sequence, but the patchable B is initially `B +4` (0x14000001, skips to epilogue register restore). When linked, patched to `B target_block_entry`.

### `link_entry_offset` — Enter Past Prologue

Linked blocks jump to `link_entry_offset` which is AFTER the prologue STP sequence and MOVX_IMM (but BEFORE optional FPU TOP init). The caller's stack frame is shared.

**FPU guard**: `codegen_block_try_link_exit` must refuse to link blocks with mismatched `CODEBLOCK_HAS_FPU` flags, because the FPU prologue writes TOP diff to a stack slot that non-FPU blocks don't initialize.

### Patch/Unpatch — W^X Safe

```c
void codegen_backend_patch_link(codeblock_t *source, int exit_idx, codeblock_t *target)
{
    uint32_t *patch_addr = (uint32_t *)(source->data + source->exit_patch_offset[exit_idx]);
    uint32_t *target_addr = (uint32_t *)(target->data + target->link_entry_offset);
    int offset = (uintptr_t)target_addr - (uintptr_t)patch_addr;

    if (!offset_is_26bit(offset))
        return;  /* out of range, don't link */

    uint32_t new_insn = 0x14000000 | ((offset >> 2) & 0x03ffffff);  /* B target */

#if defined(__APPLE__) && defined(__aarch64__)
    if (__builtin_available(macOS 11.0, *))
        pthread_jit_write_protect_np(0);
#endif

    *patch_addr = new_insn;
    __clear_cache((char *)patch_addr, (char *)patch_addr + 4);

#if defined(__APPLE__) && defined(__aarch64__)
    if (__builtin_available(macOS 11.0, *))
        pthread_jit_write_protect_np(1);
#endif
}
```

Unpatch is the same but restores the original instruction (either `B codegen_exit_rout` for JMP exits or `B +4` for epilogue exits). Store the original instruction in `exit_original_insn[i]`.

### codeblock_t Fields to Add

```c
/* Block linking */
uint16_t link_entry_offset;              /* entry past prologue */
uint32_t exit_pc[BLOCK_EXIT_MAX];        /* target CS+EIP per exit */
uint32_t exit_patch_offset[BLOCK_EXIT_MAX]; /* byte offset of patchable B */
uint32_t exit_original_insn[BLOCK_EXIT_MAX]; /* original instruction for unpatch */
uint16_t link_target_nr[BLOCK_EXIT_MAX]; /* block index of linked target (BLOCK_INVALID if not linked) */
uint8_t  exit_count;                     /* number of recorded exits */
uint32_t _pending_exit_pc;               /* compile-time: set by MOV_IMM to IREG_pc */

/* Incoming links (other blocks jumping to us) */
uint16_t link_incoming_block[BLOCK_LINK_INCOMING_MAX];
uint8_t  link_incoming_exit[BLOCK_LINK_INCOMING_MAX];
uint8_t  link_incoming_count;
```

### Constants

```c
#define BLOCK_EXIT_MAX           2
#define BLOCK_LINK_INCOMING_MAX  8
#define BLOCK_PC_INVALID         0xffffffff
```

## Implementation Steps

### Step 1: Core Infrastructure (codegen.h, codegen_block.c)

1. Add all codeblock_t fields listed above
2. Implement `codegen_block_link_init(block)` — zero all link fields
3. Implement `codegen_block_try_link_exit(source, exit_idx)` — hash lookup, validate PC/CS/phys/status, check FPU flag match, range check, call `codegen_backend_patch_link`
4. Implement `codegen_block_unlink(block)` — revert all incoming links, remove from targets' incoming lists
5. Hook unlink into `invalidate_block`, `delete_block`, `codegen_block_start_recompile` — BEFORE code is freed

### Step 2: ARM64 Backend (codegen_backend_arm64.c, codegen_backend_arm64_uops.c)

1. Record `link_entry_offset` in prologue (after MOVX_IMM, before FPU init)
2. Implement cycle-guarded exit stub in `codegen_JMP` (5 instructions when target is codegen_exit_rout and in B range)
3. Implement cycle-guarded epilogue exit stub in `codegen_backend_epilogue`
4. Implement `codegen_backend_patch_link` with W^X guards + __clear_cache
5. Implement `codegen_backend_unpatch_link` with W^X guards + __clear_cache
6. Store `exit_original_insn[i]` when recording each exit

### Step 3: IR Integration (codegen_ir.c)

1. Track `_pending_exit_pc` — set when `UOP_MOV_IMM` writes to `IREG_pc`
2. Record `link_entry_offset` and `link_epilogue_offset` during IR compile

### Step 4: x86-64 Backend (codegen_backend_x86-64.c, codegen_backend_x86-64_uops.c)

1. Same cycle guard concept but using x86-64 encoding:
   ```
   CMP DWORD [cpu_state._cycles], 0
   JLE codegen_exit_rout
   JMP codegen_exit_rout          ; patchable (5-byte JMP rel32)
   ```
2. No W^X issue on x86-64 (code pages are RWX or coherent)
3. No I-cache flush needed (x86-64 is coherent)

### Step 5: Dispatcher Integration (386_dynarec.c)

1. After `code()` returns, iterate `block->exit_count` exits
2. For each unlinked exit with valid `exit_pc`, call `codegen_block_try_link_exit`
3. This runs outside W^X window — patch_link handles its own W^X toggling

### Step 6: Test

1. Build + sign
2. Boot Voodoo 3 VM — must not crash or hang
3. Run 3DMark99 — measure performance vs unlinked baseline
4. Extended stability run (30+ minutes)
5. DOS game test (16-bit code paths)

## Key Differences from Previous Attempt

| Aspect | Previous | Retry |
|--------|----------|-------|
| W^X | Not handled | pthread_jit_write_protect_np in patch/unpatch |
| Cycle guard | None | 5-instruction guard before patchable B |
| B.cond encoding | Used branch_set_offset (OFFSET26) on BLE | Raw 0x5400004c for B.GT +8, unconditional B for exits |
| Unpatch target | Assumed B range to codegen_exit_rout | Store original instruction in exit_original_insn[] |
| FPU guard | None | Refuse link when CODEBLOCK_HAS_FPU differs |
| Range check | in_range_b26 on initial emit only | Also in patch_link (target may be far) |

## Estimated Performance Gain

15-30% reduction in dispatcher overhead. Every block execution currently:
1. Returns to C dispatcher
2. Hash lookup for next block
3. Validates PC/CS/phys/status
4. Calls block via function pointer

With linking, hot paths skip steps 1-4 entirely. The 5-instruction cycle guard (20 bytes) adds ~2-3 cycles per exit but saves ~50-100 cycles of dispatcher overhead per linked transition.
