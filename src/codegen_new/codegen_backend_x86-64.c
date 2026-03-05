#if defined __amd64__ || defined _M_X64

#    include <stdlib.h>
#    include <stdint.h>
#    include <86box/86box.h>
#    include "cpu.h"
#    include <86box/mem.h>

#    include "codegen.h"
#    include "codegen_allocator.h"
#    include "codegen_backend.h"
#    include "codegen_backend_x86-64_defs.h"
#    include "codegen_backend_x86-64_ops.h"
#    include "codegen_backend_x86-64_ops_helpers.h"
#    include "codegen_backend_x86-64_ops_sse.h"
#    include "codegen_reg.h"
#    include "x86.h"
#    include "x86seg_common.h"
#    include "x86seg.h"

#    if defined(__linux__) || defined(__APPLE__)
#        include <sys/mman.h>
#        include <unistd.h>
#    endif
#    if defined WIN32 || defined _WIN32 || defined _WIN32
#        include <windows.h>
#    endif
#    include <string.h>

void *codegen_mem_load_byte;
void *codegen_mem_load_word;
void *codegen_mem_load_long;
void *codegen_mem_load_quad;
void *codegen_mem_load_single;
void *codegen_mem_load_double;

void *codegen_mem_store_byte;
void *codegen_mem_store_word;
void *codegen_mem_store_long;
void *codegen_mem_store_quad;
void *codegen_mem_store_single;
void *codegen_mem_store_double;

void *codegen_gpf_rout;
void *codegen_exit_rout;

host_reg_def_t codegen_host_reg_list[CODEGEN_HOST_REGS] = {
    /*Note: while EAX and EDX are normally volatile registers under x86
    calling conventions, the recompiler will explicitly save and restore
    them across funcion calls*/
    { REG_EAX, 0 },
    { REG_EBX, 0 },
    { REG_EDX, 0 }
};

host_reg_def_t codegen_host_fp_reg_list[CODEGEN_HOST_FP_REGS] = {
#    if _WIN64
    /*Windows x86-64 calling convention preserves XMM6-XMM15*/
    { REG_XMM6, 0                      },
    { REG_XMM7, 0                      },
#    else
    /*System V AMD64 calling convention does not preserve any XMM registers*/
    { REG_XMM6, HOST_REG_FLAG_VOLATILE },
    { REG_XMM7, HOST_REG_FLAG_VOLATILE },
#    endif
    { REG_XMM1, HOST_REG_FLAG_VOLATILE },
    { REG_XMM2, HOST_REG_FLAG_VOLATILE },
    { REG_XMM3, HOST_REG_FLAG_VOLATILE },
    { REG_XMM4, HOST_REG_FLAG_VOLATILE },
    { REG_XMM5, HOST_REG_FLAG_VOLATILE }
};

static void
build_load_routine(codeblock_t *block, int size, int is_float)
{
    uint8_t *branch_offset;
    uint8_t *misaligned_offset = NULL;

    /*In - ESI = address
      Out - ECX = data, ESI = abrt*/
    /*MOV ECX, ESI
      SHR ESI, 12
      MOV RSI, [readlookup2+ESI*4]
      CMP ESI, -1
      JNZ +
      MOVZX ECX, B[RSI+RCX]
      XOR ESI,ESI
      RET
    * PUSH EAX
      PUSH EDX
      PUSH ECX
      CALL readmembl
      POP ECX
      POP EDX
      POP EAX
      MOVZX ECX, AL
      RET
    */
    host_x86_MOV32_REG_REG(block, REG_ECX, REG_ESI);
    host_x86_SHR32_IMM(block, REG_ESI, 12);
    host_x86_MOV64_REG_IMM(block, REG_RDI, (uint64_t) (uintptr_t) readlookup2);
    host_x86_MOV64_REG_BASE_INDEX_SHIFT(block, REG_RSI, REG_RDI, REG_RSI, 3);
    if (size != 1) {
        host_x86_TEST32_REG_IMM(block, REG_ECX, size - 1);
        misaligned_offset = host_x86_JNZ_short(block);
    }
    host_x86_CMP64_REG_IMM(block, REG_RSI, (uint32_t) -1);
    branch_offset = host_x86_JZ_short(block);
    if (size == 1 && !is_float)
        host_x86_MOVZX_BASE_INDEX_32_8(block, REG_ECX, REG_RSI, REG_RCX);
    else if (size == 2 && !is_float)
        host_x86_MOVZX_BASE_INDEX_32_16(block, REG_ECX, REG_RSI, REG_RCX);
    else if (size == 4 && !is_float)
        host_x86_MOV32_REG_BASE_INDEX(block, REG_ECX, REG_RSI, REG_RCX);
    else if (size == 4 && is_float)
        host_x86_CVTSS2SD_XREG_BASE_INDEX(block, REG_XMM_TEMP, REG_RSI, REG_RCX);
    else if (size == 8)
        host_x86_MOVQ_XREG_BASE_INDEX(block, REG_XMM_TEMP, REG_RSI, REG_RCX);
    else
        fatal("build_load_routine: size=%i\n", size);
    host_x86_XOR32_REG_REG(block, REG_ESI, REG_ESI);
    host_x86_RET(block);

    *branch_offset = (uint8_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) branch_offset) - 1;
    if (size != 1)
        *misaligned_offset = (uint8_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) misaligned_offset) - 1;
    host_x86_PUSH(block, REG_RAX);
    host_x86_PUSH(block, REG_RDX);
#    if _WIN64
    host_x86_SUB64_REG_IMM(block, REG_RSP, 0x20);
    // host_x86_MOV32_REG_REG(block, REG_ECX, uop->imm_data);
#    else
    host_x86_MOV32_REG_REG(block, REG_EDI, REG_ECX);
#    endif
    if (size == 1 && !is_float) {
        host_x86_CALL(block, (void *) readmembl);
        host_x86_MOVZX_REG_32_8(block, REG_ECX, REG_EAX);
    } else if (size == 2 && !is_float) {
        host_x86_CALL(block, (void *) readmemwl);
        host_x86_MOVZX_REG_32_16(block, REG_ECX, REG_EAX);
    } else if (size == 4 && !is_float) {
        host_x86_CALL(block, (void *) readmemll);
        host_x86_MOV32_REG_REG(block, REG_ECX, REG_EAX);
    } else if (size == 4 && is_float) {
        host_x86_CALL(block, (void *) readmemll);
        host_x86_MOVD_XREG_REG(block, REG_XMM_TEMP, REG_EAX);
        host_x86_CVTSS2SD_XREG_XREG(block, REG_XMM_TEMP, REG_XMM_TEMP);
    } else if (size == 8) {
        host_x86_CALL(block, (void *) readmemql);
        host_x86_MOVQ_XREG_REG(block, REG_XMM_TEMP, REG_RAX);
    }
#    if _WIN64
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x20);
#    endif
    host_x86_POP(block, REG_RDX);
    host_x86_POP(block, REG_RAX);
    host_x86_MOVZX_REG_ABS_32_8(block, REG_ESI, &cpu_state.abrt);
    host_x86_RET(block);
}

static void
build_store_routine(codeblock_t *block, int size, int is_float)
{
    uint8_t *branch_offset;
    uint8_t *misaligned_offset = NULL;

    /*In - ECX = data, ESI = address
      Out - ESI = abrt
      Corrupts EDI*/
    /*MOV EDI, ESI
      SHR ESI, 12
      MOV ESI, [writelookup2+ESI*4]
      CMP ESI, -1
      JNZ +
      MOV [RSI+RDI], ECX
      XOR ESI,ESI
      RET
    * PUSH EAX
      PUSH EDX
      PUSH ECX
      CALL writemembl
      POP ECX
      POP EDX
      POP EAX
      MOVZX ECX, AL
      RET
    */
    host_x86_MOV32_REG_REG(block, REG_EDI, REG_ESI);
    host_x86_SHR32_IMM(block, REG_ESI, 12);
    host_x86_MOV64_REG_IMM(block, REG_R8, (uint64_t) (uintptr_t) writelookup2);
    host_x86_MOV64_REG_BASE_INDEX_SHIFT(block, REG_RSI, REG_R8, REG_RSI, 3);
    if (size != 1) {
        host_x86_TEST32_REG_IMM(block, REG_EDI, size - 1);
        misaligned_offset = host_x86_JNZ_short(block);
    }
    host_x86_CMP64_REG_IMM(block, REG_RSI, (uint32_t) -1);
    branch_offset = host_x86_JZ_short(block);
    if (size == 1 && !is_float)
        host_x86_MOV8_BASE_INDEX_REG(block, REG_RSI, REG_RDI, REG_ECX);
    else if (size == 2 && !is_float)
        host_x86_MOV16_BASE_INDEX_REG(block, REG_RSI, REG_RDI, REG_ECX);
    else if (size == 4 && !is_float)
        host_x86_MOV32_BASE_INDEX_REG(block, REG_RSI, REG_RDI, REG_ECX);
    else if (size == 4 && is_float)
        host_x86_MOVD_BASE_INDEX_XREG(block, REG_RSI, REG_RDI, REG_XMM_TEMP);
    else if (size == 8)
        host_x86_MOVQ_BASE_INDEX_XREG(block, REG_RSI, REG_RDI, REG_XMM_TEMP);
    else
        fatal("build_store_routine: size=%i\n", size);
    host_x86_XOR32_REG_REG(block, REG_ESI, REG_ESI);
    host_x86_RET(block);

    *branch_offset = (uint8_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) branch_offset) - 1;
    if (size != 1)
        *misaligned_offset = (uint8_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) misaligned_offset) - 1;
    host_x86_PUSH(block, REG_RAX);
    host_x86_PUSH(block, REG_RDX);
#    if _WIN64
    host_x86_SUB64_REG_IMM(block, REG_RSP, 0x28);
    if (size == 4 && is_float)
        host_x86_MOVD_REG_XREG(block, REG_EDX, REG_XMM_TEMP); // data
    else if (size == 8)
        host_x86_MOVQ_REG_XREG(block, REG_RDX, REG_XMM_TEMP); // data
    else
        host_x86_MOV32_REG_REG(block, REG_EDX, REG_ECX); // data
    host_x86_MOV32_REG_REG(block, REG_ECX, REG_EDI);     // address
#    else
    host_x86_SUB64_REG_IMM(block, REG_RSP, 0x8);
    // host_x86_MOV32_REG_REG(block, REG_EDI, REG_ECX);  //address
    if (size == 4 && is_float)
        host_x86_MOVD_REG_XREG(block, REG_ESI, REG_XMM_TEMP); // data
    else if (size == 8)
        host_x86_MOVQ_REG_XREG(block, REG_RSI, REG_XMM_TEMP); // data
    else
        host_x86_MOV32_REG_REG(block, REG_ESI, REG_ECX); // data
#    endif
    if (size == 1)
        host_x86_CALL(block, (void *) writemembl);
    else if (size == 2)
        host_x86_CALL(block, (void *) writememwl);
    else if (size == 4)
        host_x86_CALL(block, (void *) writememll);
    else if (size == 8)
        host_x86_CALL(block, (void *) writememql);
#    if _WIN64
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x28);
#    else
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x8);
#    endif
    host_x86_POP(block, REG_RDX);
    host_x86_POP(block, REG_RAX);
    host_x86_MOVZX_REG_ABS_32_8(block, REG_ESI, &cpu_state.abrt);
    host_x86_RET(block);
}

static void
build_loadstore_routines(codeblock_t *block)
{
    codegen_mem_load_byte = &codeblock[block_current].data[block_pos];
    build_load_routine(block, 1, 0);
    codegen_mem_load_word = &codeblock[block_current].data[block_pos];
    build_load_routine(block, 2, 0);
    codegen_mem_load_long = &codeblock[block_current].data[block_pos];
    build_load_routine(block, 4, 0);
    codegen_mem_load_quad = &codeblock[block_current].data[block_pos];
    build_load_routine(block, 8, 0);
    codegen_mem_load_single = &codeblock[block_current].data[block_pos];
    build_load_routine(block, 4, 1);
    codegen_mem_load_double = &codeblock[block_current].data[block_pos];
    build_load_routine(block, 8, 1);

    codegen_mem_store_byte = &codeblock[block_current].data[block_pos];
    build_store_routine(block, 1, 0);
    codegen_mem_store_word = &codeblock[block_current].data[block_pos];
    build_store_routine(block, 2, 0);
    codegen_mem_store_long = &codeblock[block_current].data[block_pos];
    build_store_routine(block, 4, 0);
    codegen_mem_store_quad = &codeblock[block_current].data[block_pos];
    build_store_routine(block, 8, 0);
    codegen_mem_store_single = &codeblock[block_current].data[block_pos];
    build_store_routine(block, 4, 1);
    codegen_mem_store_double = &codeblock[block_current].data[block_pos];
    build_store_routine(block, 8, 1);
}

void
codegen_backend_init(void)
{
    codeblock_t *block;
    int          c;

    codeblock      = malloc(BLOCK_SIZE * sizeof(codeblock_t));
    codeblock_hash = malloc(HASH_SIZE * sizeof(codeblock_t *));

    memset(codeblock, 0, BLOCK_SIZE * sizeof(codeblock_t));
    memset(codeblock_hash, 0, HASH_SIZE * sizeof(codeblock_t *));

    for (c = 0; c < BLOCK_SIZE; c++)
        codeblock[c].pc = BLOCK_PC_INVALID;

    block_current                           = 0;
    block_pos                               = 0;
    block                                   = &codeblock[block_current];
    codeblock[block_current].head_mem_block = codegen_allocator_allocate(NULL, block_current);
    codeblock[block_current].data           = codeblock_allocator_get_ptr(codeblock[block_current].head_mem_block);
    block_write_data                        = codeblock[block_current].data;
    build_loadstore_routines(&codeblock[block_current]);

    codegen_gpf_rout = &codeblock[block_current].data[block_pos];
#    if _WIN64
    host_x86_XOR32_REG_REG(block, REG_ECX, REG_ECX);
    host_x86_XOR32_REG_REG(block, REG_EDX, REG_EDX);
#    else
    host_x86_XOR32_REG_REG(block, REG_EDI, REG_EDI);
    host_x86_XOR32_REG_REG(block, REG_ESI, REG_ESI);
#    endif
    host_x86_CALL(block, (void *) x86gpf);
    codegen_exit_rout = &codeblock[block_current].data[block_pos];
#    ifdef _WIN64
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x38);
#    else
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x48);
#    endif
    host_x86_POP(block, REG_R15);
    host_x86_POP(block, REG_R14);
    host_x86_POP(block, REG_R13);
    host_x86_POP(block, REG_R12);
#    ifdef _WIN64
    host_x86_POP(block, REG_RDI);
    host_x86_POP(block, REG_RSI);
#    endif
    host_x86_POP(block, REG_RBP);
    host_x86_POP(block, REG_RBX);
    host_x86_RET(block);

    block_write_data = NULL;

    asm(
        "stmxcsr %0\n"
        : "=m"(cpu_state.old_fp_control));
    cpu_state.trunc_fp_control = cpu_state.old_fp_control | 0x6000;
}

void
codegen_set_rounding_mode(int mode)
{
    cpu_state.new_fp_control = (cpu_state.old_fp_control & ~0x6000) | (mode << 13);
}

void
codegen_backend_prologue(codeblock_t *block)
{
    block_pos = BLOCK_START; /*Entry code*/
    host_x86_PUSH(block, REG_RBX);
    host_x86_PUSH(block, REG_RBP);
#    ifdef _WIN64
    host_x86_PUSH(block, REG_RSI);
    host_x86_PUSH(block, REG_RDI);
#    endif
    host_x86_PUSH(block, REG_R12);
    host_x86_PUSH(block, REG_R13);
    host_x86_PUSH(block, REG_R14);
    host_x86_PUSH(block, REG_R15);
#    ifdef _WIN64
    host_x86_SUB64_REG_IMM(block, REG_RSP, 0x38);
#    else
    host_x86_SUB64_REG_IMM(block, REG_RSP, 0x48);
#    endif
    host_x86_MOV64_REG_IMM(block, REG_RBP, ((uintptr_t) &cpu_state) + 128);
    if (block->flags & CODEBLOCK_HAS_FPU) {
        host_x86_MOV32_REG_ABS(block, REG_EAX, &cpu_state.TOP);
        host_x86_SUB32_REG_IMM(block, REG_EAX, block->TOP);
        host_x86_MOV32_BASE_OFFSET_REG(block, REG_RSP, IREG_TOP_diff_stack_offset, REG_EAX);
    }
    if (block->flags & CODEBLOCK_NO_IMMEDIATES)
        host_x86_MOV64_REG_IMM(block, REG_R12, ((uintptr_t) ram) + 2147483648ULL);

    /* Record the entry point past prologue for block linking.
       Linked blocks jump here, sharing the caller's stack frame. */
    block->link_entry_offset = (uint16_t) block_pos;
}

void
codegen_backend_epilogue(codeblock_t *block)
{
    /* Cycle-guarded epilogue exit stub for block linking.
       When unlinked, the patchable JMP falls through to the register
       restore sequence below. When linked, it jumps to the target block. */
    /* If we're in a continuation mem_block, exit_patch_offset would be wrong.
       Skip linkable exit -- the epilogue will just run normally (non-linkable). */
    if (block_write_data != block->data) {
        if (block->_pending_exit_pc != BLOCK_PC_INVALID)
            block->_pending_exit_pc = BLOCK_PC_INVALID;
    } else if (block->exit_count < BLOCK_EXIT_MAX && block->_pending_exit_pc != BLOCK_PC_INVALID) {
        int      exit_idx = block->exit_count;
        uint32_t patchable_jmp_pos;
        int32_t  skip_rel32;

        block->exit_pc[exit_idx] = block->_pending_exit_pc;
        block->_pending_exit_pc  = BLOCK_PC_INVALID;

        /* MOV EAX, [RBP + _cycles_offset] — load cpu_state._cycles */
        host_x86_MOV32_REG_BASE_OFFSET(block, REG_EAX, REG_RBP, cpu_state_offset(_cycles));

        /* CMP EAX, 0 */
        host_x86_CMP32_REG_IMM(block, REG_EAX, 0);

        /* JLE codegen_exit_rout — bail if cycles <= 0 */
        codegen_alloc_bytes(block, 6);
        codegen_addbyte2(block, 0x0f, 0x8e); /* JLE rel32 */
        codegen_addlong(block, (uint32_t) ((uintptr_t) codegen_exit_rout - (uintptr_t) &block_write_data[block_pos + 4]));

        /* MOV EAX, [RBP + _chain_remaining_offset] */
        host_x86_MOV32_REG_BASE_OFFSET(block, REG_EAX, REG_RBP, cpu_state_offset(_chain_remaining));

        /* SUB EAX, 1 (sets flags) */
        host_x86_SUB32_REG_IMM(block, REG_EAX, 1);

        /* MOV [RBP + _chain_remaining_offset], EAX */
        host_x86_MOV32_BASE_OFFSET_REG(block, REG_RBP, cpu_state_offset(_chain_remaining), REG_EAX);

        /* JLE codegen_exit_rout — bail if chain_remaining <= 0 */
        codegen_alloc_bytes(block, 6);
        codegen_addbyte2(block, 0x0f, 0x8e); /* JLE rel32 */
        codegen_addlong(block, (uint32_t) ((uintptr_t) codegen_exit_rout - (uintptr_t) &block_write_data[block_pos + 4]));

        /* Patchable JMP rel32 — initially jumps to next instruction (fall through
           to epilogue). When linked, patched to jump to target block's entry. */
        patchable_jmp_pos = block_pos;
        codegen_alloc_bytes(block, 5);
        codegen_addbyte(block, 0xe9); /* JMP rel32 */
        skip_rel32 = 0;               /* rel32 = 0 means jump to next instruction */
        codegen_addlong(block, (uint32_t) skip_rel32);

        block->exit_patch_offset[exit_idx]  = patchable_jmp_pos;
        block->exit_original_insn[exit_idx] = (uint32_t) skip_rel32;
        block->exit_count++;
    }

    /* Actual register restore / return sequence. */
#    ifdef _WIN64
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x38);
#    else
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x48);
#    endif
    host_x86_POP(block, REG_R15);
    host_x86_POP(block, REG_R14);
    host_x86_POP(block, REG_R13);
    host_x86_POP(block, REG_R12);
#    ifdef _WIN64
    host_x86_POP(block, REG_RDI);
    host_x86_POP(block, REG_RSI);
#    endif
    host_x86_POP(block, REG_RBP);
    host_x86_POP(block, REG_RBX);
    host_x86_RET(block);
}

/*
 * Block linking: patch/unpatch a 5-byte JMP rel32 instruction.
 *
 * On x86-64, code pages are coherent (no I-cache flush needed) and
 * typically RWX or made writable as needed (no W^X toggle needed).
 *
 * exit_original_insn[exit_idx] stores the original rel32 displacement
 * (the opcode byte 0xE9 is constant and does not need to be stored).
 */
void
codegen_backend_patch_link(codeblock_t *source, int exit_idx, codeblock_t *target)
{
    uint8_t *patch_addr  = source->data + source->exit_patch_offset[exit_idx];
    uint8_t *target_addr = target->data + target->link_entry_offset;
    int32_t  rel         = (int32_t) (target_addr - (patch_addr + 5));

    /* Write the 5-byte JMP rel32 to the target block's entry point. */
    patch_addr[0]                 = 0xe9;
    *(int32_t *) (patch_addr + 1) = rel;
}

void
codegen_backend_unpatch_link(codeblock_t *source, int exit_idx)
{
    uint8_t *patch_addr = source->data + source->exit_patch_offset[exit_idx];
    int32_t  orig_rel32 = (int32_t) source->exit_original_insn[exit_idx];

    /* Restore the original 5-byte JMP rel32. */
    patch_addr[0]                 = 0xe9;
    *(int32_t *) (patch_addr + 1) = orig_rel32;
}
#endif
