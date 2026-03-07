/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the code generator.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020 Miran Grca.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#ifndef _CODEGEN_PUBLIC_H_
#define _CODEGEN_PUBLIC_H_

#include <stdint.h>

#ifndef USE_NEW_DYNAREC
#    define PAGE_MASK_INDEX_MASK  3
#    define PAGE_MASK_INDEX_SHIFT 10
#    define PAGE_MASK_SHIFT       4
#else
#    define PAGE_MASK_SHIFT 6
#endif
#define PAGE_MASK_MASK 63

#ifdef USE_NEW_DYNAREC
#    define BLOCK_PC_INVALID 0xffffffff
#    define BLOCK_INVALID    0

typedef struct new_dynarec_stats_t {
    uint64_t blocks_marked;
    uint64_t blocks_recompiled;
    uint64_t direct_recompiled_instructions;
    uint64_t helper_call_fallbacks;
    uint64_t direct_opcode_table_null_hits;
    uint64_t direct_handler_bailouts;
    uint64_t invalidations;
    uint64_t byte_mask_transitions;
    uint64_t no_immediates_transitions;
    uint64_t allocator_pressure_events;
    uint64_t purgable_flush_attempts;
    uint64_t purgable_flush_successes;
    uint64_t random_evictions;
} new_dynarec_stats_t;

typedef enum new_dynarec_trace_kind_t {
    NEW_DYNAREC_TRACE_BLOCK_MARKED,
    NEW_DYNAREC_TRACE_BLOCK_RECOMPILED,
    NEW_DYNAREC_TRACE_BLOCK_INVALIDATED,
    NEW_DYNAREC_TRACE_BLOCK_BECAME_BYTE_MASK,
    NEW_DYNAREC_TRACE_BLOCK_BECAME_NO_IMMEDIATES,
    NEW_DYNAREC_TRACE_ALLOCATOR_PRESSURE,
    NEW_DYNAREC_TRACE_RANDOM_EVICTION,
} new_dynarec_trace_kind_t;

typedef struct new_dynarec_trace_event_t {
    new_dynarec_trace_kind_t kind;
    uint32_t                 pc;
    uint32_t                 phys;
    uint32_t                 detail;
    uint16_t                 flags;
} new_dynarec_trace_event_t;

typedef void (*new_dynarec_trace_hook_t)(const new_dynarec_trace_event_t *event, void *opaque);

typedef enum new_dynarec_helper_fallback_reason_t {
    NEW_DYNAREC_HELPER_FALLBACK_NONE = 0,
    NEW_DYNAREC_HELPER_FALLBACK_DIRECT_TABLE_NULL,
    NEW_DYNAREC_HELPER_FALLBACK_DIRECT_HANDLER_BAILOUT,
} new_dynarec_helper_fallback_reason_t;

extern new_dynarec_stats_t new_dynarec_stats;

extern void new_dynarec_stats_reset(void);
extern void new_dynarec_stats_snapshot(new_dynarec_stats_t *out);
extern void new_dynarec_set_trace_hook(new_dynarec_trace_hook_t hook, void *opaque);

extern void new_dynarec_note_block_marked(uint32_t pc, uint32_t phys, uint32_t detail);
extern void new_dynarec_note_block_recompiled(uint32_t pc, uint32_t phys, uint32_t detail);
extern void new_dynarec_note_block_invalidated(uint32_t pc, uint32_t phys, uint16_t flags);
extern void new_dynarec_note_block_became_byte_mask(uint32_t pc, uint32_t phys, uint16_t flags);
extern void new_dynarec_note_block_became_no_immediates(uint32_t pc, uint32_t phys, uint16_t flags);
extern void new_dynarec_note_allocator_pressure(uint32_t detail);
extern void new_dynarec_note_random_eviction(uint32_t pc, uint32_t phys, uint16_t flags);

static inline void
new_dynarec_note_direct_recompiled_instruction(void)
{
    new_dynarec_stats.direct_recompiled_instructions++;
}

static inline void
new_dynarec_note_helper_call_fallback(new_dynarec_helper_fallback_reason_t reason)
{
    if (reason == NEW_DYNAREC_HELPER_FALLBACK_NONE)
        return;

    new_dynarec_stats.helper_call_fallbacks++;
    if (reason == NEW_DYNAREC_HELPER_FALLBACK_DIRECT_TABLE_NULL)
        new_dynarec_stats.direct_opcode_table_null_hits++;
    else if (reason == NEW_DYNAREC_HELPER_FALLBACK_DIRECT_HANDLER_BAILOUT)
        new_dynarec_stats.direct_handler_bailouts++;
}

static inline void
new_dynarec_note_purgable_flush_attempt(void)
{
    new_dynarec_stats.purgable_flush_attempts++;
}

static inline void
new_dynarec_note_purgable_flush_success(void)
{
    new_dynarec_stats.purgable_flush_successes++;
}
#endif

extern void codegen_init(void);
extern void codegen_flush(void);

/*Current physical page of block being recompiled. -1 if no recompilation taking place */
extern uint32_t recomp_page;
extern int      codegen_in_recompile;

#endif
