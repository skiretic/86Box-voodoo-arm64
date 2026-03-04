# Block Linking Research Report

## Phase 4: Block Linking / Jump Patching

### Executive Summary

Block linking is the single highest-impact optimization remaining for the 86Box new dynarec,
estimated at 15-30% overall speedup. Currently, every compiled block exits back to C code
(`exec386_dynarec_dyn`), which performs a hash lookup + validation to find the next block.
Block linking eliminates this overhead by patching the block's exit branch to jump directly
to the next block's entry point.

---

## 1. Current 86Box Block Execution Flow

### Dispatcher Loop (`exec386_dynarec_dyn` in `src/cpu/386_dynarec.c`)

Every block execution follows this path:

```
1. Hash lookup:  phys_addr = get_phys(cs + cpu_state.pc)
                 hash = HASH(phys_addr)
                 block = &codeblock[codeblock_hash[hash]]
2. Validate:     Match pc, cs, phys, status flags
3. Tree fallback: If hash misses, walk codeblock_tree_find()
4. Dirty check:  Check page_mask vs dirty_mask
5. Execute:      code = (void *)&block->data[BLOCK_START]; code();
6. Return:       Block returns to C, goto step 1
```

Steps 1-4 happen on **every** block transition, even for tight loops. This is the overhead
block linking eliminates.

### Block Exit Paths (ARM64)

Currently, compiled blocks exit via `codegen_exit_rout`, which restores callee-saved
registers and executes `RET`. This returns to the `code()` call in `exec386_dynarec_dyn`.

The exit is triggered by `UOP_CALL_INSTRUCTION_FUNC`:
```c
// codegen_backend_arm64_uops.c:217
static int codegen_CALL_INSTRUCTION_FUNC(codeblock_t *block, uop_t *uop)
{
    host_arm64_mov_imm(block, REG_ARG0, uop->imm_data);
    host_arm64_call(block, uop->p);           // BLR to interpreter func
    host_arm64_CBNZ(block, REG_X0, codegen_exit_rout);  // if nonzero, exit
    return 0;
}
```

When the interpreter function returns nonzero (block ended), CBNZ branches to
`codegen_exit_rout` which does the full epilogue (restore regs, RET).

### Code Buffer Layout

- **Allocator**: Single contiguous 120 MB mmap (`MEM_BLOCK_NR=131072 * MEM_BLOCK_SIZE=0x3c0`)
- **Per-block size**: 960 bytes max
- **Block 0** is special: contains load/store routines, GPF/exit routines, FP rounding
- **Each block**: prologue (save regs) + compiled UOPs + epilogue (restore regs + RET)
- **`block->data`** points into the mmap; `BLOCK_START=0`

### Key Observation: Branch Range

ARM64 B/BL instruction: +-128 MB range (26-bit signed offset * 4).
Total code buffer: ~120 MB. This means blocks at opposite ends of the buffer
are within range of each other, but barely. Any growth would exceed range.

**Recommendation**: Use B instruction for linking. The 120 MB buffer fits within +-128 MB.
If the buffer ever grows, fall back to indirect branch (MOVX_IMM + BR).

---

## 2. Reference Emulator Approaches

### QEMU TCG

**Data structures:**
```c
struct TranslationBlock {
    // ...
    uintptr_t jmp_target_addr[2];  // Destination addresses for 2 exits
    uint16_t jmp_reset_offset[2];  // Offset to revert patched branch
    uintptr_t jmp_dest[2];        // Pointers to linked TBs (or self)
    uintptr_t jmp_list_head;      // Head of incoming jump list
    uintptr_t jmp_list_next[2];   // Next in incoming jump list
};
```

**Key design:**
- Each TB has up to 2 exit slots (taken/not-taken for conditional jumps)
- `jmp_reset_offset` stores the offset within the TB code where the branch lives
- When linking: `tb_set_jmp_target()` patches the branch at that offset
- When unlinking: reverts the branch to jump to the exit stub
- ARM64: patches the immediate field of a B instruction atomically
- Same-page restriction: QEMU only links blocks on the same guest page (4 KB)

**Linking flow:**
```
tb_add_jump(src_tb, slot, dest_tb):
  1. Store dest in src->jmp_dest[slot]
  2. Patch branch at src code + jmp_reset_offset[slot] to jump to dest code entry
  3. Add src to dest's incoming list (jmp_list_head chain)
```

**Unlinking flow:**
```
tb_jmp_unlink(tb):
  for each TB in tb->jmp_list_head:
    Revert that TB's branch back to exit stub
  Clear jmp_list_head
```

### box64

**Approach:**
- Intra-block jumps: direct native branch to translated address
- Inter-block jumps: jump table lookup or indirect branch
- No formal "link slot" mechanism; uses a table-based approach
- Block sizes are larger (configurable "bigblock" mode)

### FEX-Emu

- Full SSA IR with block linking
- Shared code buffers for related blocks
- Call/return optimization is a major focus

---

## 3. Proposed Design for 86Box

### 3.1 New Fields in `codeblock_t`

Add to `struct codeblock_t` in `src/codegen_new/codegen.h`:

```c
struct codeblock_t {
    // ... existing fields ...

    /*Block linking: exit stubs that can be patched to jump directly
      to successor blocks, skipping the dispatcher.*/
    uint16_t link_exit_count;       /* Number of patchable exits (0-2) */
    uint16_t link_exit_offset[2];   /* Byte offset within data[] where
                                       the patchable branch lives */
    uint16_t link_target[2];        /* Block index of linked target,
                                       or BLOCK_INVALID if unlinked */

    /*Incoming link tracking: list of blocks that jump TO this block.
      Needed to unlink predecessors on invalidation.*/
    uint16_t link_incoming_head;    /* Head of incoming link list */
    uint16_t link_incoming_next[2]; /* Per-exit: next in incoming list
                                       for the TARGET block */
    uint16_t link_incoming_from[2]; /* Block index + slot that links here */
};
```

**Design notes:**
- `link_exit_offset[slot]` records where in the emitted code the patchable branch instruction is.
  For ARM64, this is a 4-byte B instruction. For x86-64, this is a 5-byte JMP rel32.
- `link_target[slot]` is BLOCK_INVALID when unlinked (the branch goes to the exit stub).
- The incoming list uses block indices (uint16_t) since codeblock[] is indexed by uint16_t.
- Up to 2 exits per block (slot 0 = fall-through/unconditional, slot 1 = conditional taken).
- We use the same BLOCK_INVALID sentinel (0) for "no link".

### 3.2 Where Blocks Exit

There are **two categories** of block exits in 86Box:

**Category A: Recompiled instruction exits** (eligible for linking)
These are blocks where the last instruction is a recompiled JMP/Jcc/CALL/RET/LOOP.
The recomp handler sets `IREG_pc` to a known target and returns nonzero.
The block epilogue then runs, and control returns to the dispatcher.

**Category B: Interpreter fallback exits** (NOT eligible for linking)
When `UOP_CALL_INSTRUCTION_FUNC` invokes the interpreter and it returns nonzero,
the block exits via CBNZ → codegen_exit_rout. The target PC is dynamic and
unknown at compile time. These cannot be linked.

**Important**: Only Category A exits can be linked, because the target PC is a
compile-time constant embedded in the IR.

### 3.3 Exit Stub Design

For each linkable exit, the backend emits a **patchable exit stub** at the end
of the block, just before the epilogue. The stub has two states:

**Unlinked state** (initial):
```
[patchable B instruction → exit_stub_target]
exit_stub_target:
   <full epilogue: restore regs, RET>
```

Actually, a simpler approach: the patchable branch is placed where the block
would normally branch to `codegen_exit_rout`. Initially, it branches to
the block's own epilogue. When linked, it is patched to branch to the
**prologue** of the target block.

Wait -- we need to be more careful. If we link to the target block's prologue,
the target block will save callee-saved registers again (they're already saved).
This would corrupt the stack. So we need a **linkable entry point** that skips
the prologue.

**Solution: Link Entry Point**

Each compiled block has two entry points:
1. **Normal entry** (offset 0 / `BLOCK_START`): Full prologue, saves all regs
2. **Link entry** (new): Skips the prologue, starts at the first UOP

The link entry offset is stored in the block. When linking, the branch is patched
to jump to the link entry, not the normal entry.

```c
uint16_t link_entry_offset;  /* Offset within data[] for linked entry */
```

This is simply `block_pos` right after `codegen_backend_prologue()` completes.

### 3.4 ARM64 Patchable Stub

```
; End of block, before epilogue:
; Slot 0 exit stub:
  B epilogue_label      ; 4 bytes, patchable. Initially → own epilogue.
                        ; When linked: patched to → target block's link_entry

; Epilogue:
epilogue_label:
  LDP X19, X20, [SP], #64  ; restore callee-saved regs
  LDP X21, X22, [SP], #16
  ...
  RET
```

**Patching mechanism** (ARM64):
```c
void codegen_block_link_patch_arm64(codeblock_t *src, int slot, codeblock_t *dst)
{
    uint32_t *branch_insn = (uint32_t *)(src->data + src->link_exit_offset[slot]);
    uint8_t  *target = dst->data + dst->link_entry_offset;
    int offset = (int)((uintptr_t)target - (uintptr_t)branch_insn);

    // ARM64 B instruction: imm26 * 4 byte range = +-128MB
    *branch_insn = 0x14000000 | ((offset >> 2) & 0x03FFFFFF);

    // Flush I-cache for patched instruction
    __builtin___clear_cache((char *)branch_insn, (char *)branch_insn + 4);
}
```

**Unpatching mechanism** (revert to exit stub):
```c
void codegen_block_unlink_patch_arm64(codeblock_t *src, int slot)
{
    uint32_t *branch_insn = (uint32_t *)(src->data + src->link_exit_offset[slot]);
    uint8_t  *epilogue = src->data + src->link_epilogue_offset;
    int offset = (int)((uintptr_t)epilogue - (uintptr_t)branch_insn);

    *branch_insn = 0x14000000 | ((offset >> 2) & 0x03FFFFFF);

    __builtin___clear_cache((char *)branch_insn, (char *)branch_insn + 4);
}
```

### 3.5 x86-64 Patchable Stub

```
; End of block, before epilogue:
; Slot 0 exit stub:
  JMP rel32 epilogue    ; 5 bytes (E9 xx xx xx xx), patchable
                        ; When linked: rel32 is rewritten to target

; Epilogue:
epilogue:
  ADD RSP, 0x48
  POP R15..R12, RBP, RBX
  RET
```

**Patching** (x86-64):
```c
void codegen_block_link_patch_x86(codeblock_t *src, int slot, codeblock_t *dst)
{
    uint8_t *jmp_insn = src->data + src->link_exit_offset[slot];
    uint8_t *target = dst->data + dst->link_entry_offset;
    int32_t rel = (int32_t)((uintptr_t)target - ((uintptr_t)jmp_insn + 5));

    // x86-64 JMP rel32: opcode E9 + 4-byte relative
    jmp_insn[0] = 0xE9;
    *(int32_t *)(jmp_insn + 1) = rel;
}
```

x86-64 JMP rel32 has +-2 GB range, always sufficient within a single 120 MB code buffer.
No I-cache flush needed on x86-64 (coherent I-cache).

### 3.6 Conditional Branch Handling

For conditional branches (Jcc), there are two exits:
- **Slot 0**: Fall-through (branch not taken) → next sequential block
- **Slot 1**: Taken path → branch target block

The recomp handler for Jcc (e.g., `recomp_JO`, `recomp_JNO`, etc.) already sets up:
1. A conditional jump within the IR for the taken path
2. IREG_pc is set to the taken-target address on the taken path
3. IREG_pc is set to the fall-through address on the not-taken path

Both paths eventually reach the block epilogue. For block linking, we need
**two separate patchable branches** at the block exit:

```
; Taken path code:
  MOV [IREG_pc], taken_target
  B exit_stub_1             ; Slot 1: patchable → target block

; Fall-through path code:
  MOV [IREG_pc], fallthrough_target
  B exit_stub_0             ; Slot 0: patchable → next sequential block

exit_stub_0:
exit_stub_1:
  <epilogue>
```

However, this is complex to implement in the current IR compiler because
both paths currently converge at the same epilogue. A simpler initial approach:

**Phase 4a (initial): Only link unconditional exits (1 slot)**

This captures:
- Unconditional JMP
- Fall-through at end of block (block ends because max instruction count reached)
- CALL (the return path continues at the next sequential address)

This is ~60-70% of block transitions (tight loops, sequential code).

**Phase 4b (future): Link conditional exits (2 slots)**

This adds Jcc linking for both taken and not-taken paths.

### 3.7 Linking Flow

**When to link** (`codegen_block_end_recompile`):

After a block is compiled, check if its target PC(s) correspond to already-compiled blocks.
If yes, patch the exit to jump directly to them.

```c
void codegen_block_end_recompile(codeblock_t *block)
{
    // ... existing code ...
    codegen_ir_compile(ir_data, block);

    // NEW: Try to link this block's exits to existing successor blocks
    for (int slot = 0; slot < block->link_exit_count; slot++) {
        uint32_t target_pc = block->link_target_pc[slot];  // NEW field
        codeblock_t *target = codegen_find_block(target_pc);
        if (target && (target->flags & CODEBLOCK_WAS_RECOMPILED)) {
            codegen_block_link(block, slot, target);
        }
    }

    // NEW: Also check if any existing blocks target THIS block's PC.
    // If so, link them to us. (Reverse linking)
    codegen_try_link_predecessors(block);
}
```

**When to unlink** (`delete_block` / `invalidate_block`):

When a block is invalidated (SMC) or deleted, we must:
1. Unlink all of this block's outgoing links (revert branches to exit stubs)
2. Unlink all incoming links from other blocks that jump to this block

```c
static void unlink_block(codeblock_t *block)
{
    // 1. Unlink outgoing: revert our exit branches
    for (int slot = 0; slot < block->link_exit_count; slot++) {
        if (block->link_target[slot] != BLOCK_INVALID) {
            codeblock_t *target = &codeblock[block->link_target[slot]];
            // Remove us from target's incoming list
            codegen_incoming_list_remove(target, block, slot);
            // Revert our branch to exit stub
            codegen_block_unlink_patch(block, slot);
            block->link_target[slot] = BLOCK_INVALID;
        }
    }

    // 2. Unlink incoming: revert other blocks' branches that point to us
    uint16_t incoming = block->link_incoming_head;
    while (incoming != BLOCK_INVALID) {
        uint16_t block_idx = incoming >> 1;
        int slot = incoming & 1;
        codeblock_t *pred = &codeblock[block_idx];
        codegen_block_unlink_patch(pred, slot);
        pred->link_target[slot] = BLOCK_INVALID;
        incoming = pred->link_incoming_next[slot];
        pred->link_incoming_next[slot] = BLOCK_INVALID;
    }
    block->link_incoming_head = BLOCK_INVALID;
}
```

This must be called from `invalidate_block()` and `delete_block()` before
the block's code memory is freed.

### 3.8 Same-Page Restriction

QEMU limits block linking to same-page blocks. This is because cross-page
blocks may have different dirty masks, and invalidating one page shouldn't
require scanning all blocks in other pages.

For 86Box, we should adopt the same restriction initially:

```c
// Only link if source and target are on the same physical page
if ((src_block->phys >> 12) == (dst_block->phys >> 12)) {
    codegen_block_link(src, slot, dst);
}
```

This simplifies invalidation significantly: when a page is dirtied, only
blocks on that page need to be scanned for link removal.

### 3.9 I-Cache Coherency

**ARM64**: Requires explicit I-cache invalidation after patching code.
Use `__builtin___clear_cache()` or inline DC CVAU + DSB ISH + IC IVAU + DSB ISH + ISB.
On macOS, `__builtin___clear_cache` calls `sys_icache_invalidate()`.

**macOS JIT write-protect**: Apple Silicon requires toggling `pthread_jit_write_protect_np(0)`
before writing to JIT code and `pthread_jit_write_protect_np(1)` after.
The `exec386_dynarec_dyn` function already toggles this around recompilation.
For runtime link patching (linking a block after compilation), we need to
toggle it around the patch call. This is a system call, so it has overhead —
but it only happens once per link establishment, not per block execution.

**x86-64**: No I-cache invalidation needed (coherent I-cache/D-cache).

### 3.10 Thread Safety

86Box's dynarec runs on a single CPU thread. There are no concurrent writers
or readers of JIT code. Therefore, no atomicity requirements for branch
patching. We only need to ensure I-cache coherency (ARM64).

---

## 4. Implementation Plan

### Phase 4a: Unconditional Block Linking (Recommended First Step)

1. **Add fields to codeblock_t**: `link_entry_offset`, `link_exit_count`,
   `link_exit_offset[1]`, `link_target[1]`, `link_target_pc[1]`,
   `link_incoming_head`, incoming list fields.

2. **Backend changes**: In `codegen_backend_epilogue()`, record `link_entry_offset`
   (right after prologue). Emit a patchable B/JMP before the epilogue.
   Record its offset as `link_exit_offset[0]`.

3. **IR changes**: In `codegen_ir_compile()`, after the epilogue, record the
   exit stub position. Set `link_exit_count = 1`.

4. **Block management**: In `codegen_block_end_recompile()`, attempt to link.
   In `invalidate_block()` and `delete_block()`, call `unlink_block()`.

5. **Block lookup helper**: Add `codegen_find_block_by_pc()` that searches
   the hash table and tree for a block matching a given PC.

### Phase 4b: Conditional Block Linking (Future Enhancement)

1. Extend to 2 exit slots for Jcc instructions.
2. Requires modifying the recomp handlers to emit separate exit stubs.
3. More complex but captures the remaining 30-40% of transitions.

### Estimated Performance Impact

- **Phase 4a alone**: 10-20% overall speedup (eliminates dispatcher overhead
  for sequential and loop-back transitions)
- **Phase 4a + 4b**: 15-30% overall speedup (covers nearly all block transitions)
- **Workload dependent**: Tight loops benefit most (e.g., memcpy, string ops)

### Risks and Mitigations

1. **SMC correctness**: Invalidation must unlink all incoming/outgoing links.
   Mitigation: Same-page restriction + mandatory unlink before delete.

2. **Stale links**: If a block is recompiled (dirty list), its code changes.
   Any incoming links become stale. Mitigation: `codegen_block_start_recompile`
   must unlink the old block before overwriting its code.

3. **Code buffer exhaustion**: Patchable stubs add ~4-5 bytes per block exit.
   With 960 bytes per block, this is negligible (<1%).

4. **Branch range (ARM64)**: 120 MB buffer fits within B range (+-128 MB).
   If buffer grows, add range check + indirect branch fallback.

---

## 5. Summary of Concrete Changes

### Files to Modify

| File | Changes |
|------|---------|
| `src/codegen_new/codegen.h` | Add link fields to `codeblock_t`, new flag `CODEBLOCK_LINKABLE` |
| `src/codegen_new/codegen_block.c` | Add `unlink_block()`, call from `invalidate_block`/`delete_block`. Add `codegen_try_link()` in `codegen_block_end_recompile()` |
| `src/codegen_new/codegen_ir.c` | Record `link_entry_offset` after prologue. Emit patchable stub. |
| `src/codegen_new/codegen_backend_arm64.c` | `codegen_block_link_patch_arm64()`, `codegen_block_unlink_patch_arm64()` |
| `src/codegen_new/codegen_backend_x86-64.c` | `codegen_block_link_patch_x86()`, `codegen_block_unlink_patch_x86()` |
| `src/codegen_new/codegen_backend.h` (new or existing) | Declare backend-agnostic linking API |

### New API Functions

```c
// codegen_block.c
void codegen_block_link(codeblock_t *src, int slot, codeblock_t *dst);
void codegen_block_unlink(codeblock_t *block);
void codegen_try_link_predecessors(codeblock_t *block);
codeblock_t *codegen_find_block_by_pc(uint32_t pc, uint32_t cs);

// Backend-specific (codegen_backend_arm64.c / codegen_backend_x86-64.c)
void codegen_link_patch(codeblock_t *src, int slot, codeblock_t *dst);
void codegen_unlink_patch(codeblock_t *src, int slot);
```

### New codeblock_t Fields (Final Recommended Set)

```c
struct codeblock_t {
    // ... existing fields ...

    /* Block linking */
    uint16_t link_entry_offset;     /* Offset past prologue for linked entry */
    uint16_t link_exit_count;       /* 0, 1, or 2 patchable exits */
    uint16_t link_exit_offset[2];   /* Where patchable branch lives in data[] */
    uint16_t link_epilogue_offset;  /* Where epilogue starts (unlink target) */
    uint32_t link_target_pc[2];     /* Guest PC of each exit target */
    uint16_t link_target[2];        /* Block index of linked target */

    /* Incoming link list (singly-linked) */
    uint16_t link_incoming_head;    /* First block+slot linking to us */
    /* Encoded as: (block_index << 1) | slot */
};
```

For the incoming list, we can use a simple array-based approach:
each block has a `link_incoming_next` field that chains through blocks
linking to the same target. This avoids malloc/free.

### Size Impact

New fields add approximately 22 bytes per codeblock_t. With 131072 blocks,
this is ~2.7 MB additional memory. Acceptable.
