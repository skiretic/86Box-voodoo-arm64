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

#include <stddef.h>
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
#    define NEW_DYNAREC_RANDOM_EVICTION_MARK_DEFERRAL_BUDGET 8

typedef struct new_dynarec_stats_t {
    uint64_t blocks_marked;
    uint64_t deferred_block_marks;
    uint64_t blocks_recompiled;
    uint64_t direct_recompiled_instructions;
    uint64_t helper_call_fallbacks;
    uint64_t direct_opcode_table_null_hits;
    uint64_t direct_handler_bailouts;
    uint64_t invalidations;
    uint64_t byte_mask_transitions;
    uint64_t no_immediates_transitions;
    uint64_t allocator_pressure_events;
    uint64_t allocator_pressure_empty_purgable_list;
    uint64_t purgable_page_enqueues;
    uint64_t purgable_page_enqueues_write;
    uint64_t purgable_page_enqueues_codegen;
    uint64_t purgable_page_enqueues_bulk_dirty;
    uint64_t purgable_page_dequeues_stale;
    uint64_t purgable_page_dequeues_flush;
    uint64_t purgable_page_dequeues_no_blocks;
    uint64_t purgable_flush_attempts;
    uint64_t purgable_flush_successes;
    uint64_t purgable_flush_no_overlap;
    uint64_t purgable_flush_no_free_block;
    uint64_t purgable_flush_no_blocks;
    uint64_t purgable_flush_dirty_list_reuses;
    uint64_t random_evictions;
    uint64_t verify_samples;
    uint64_t verify_direct_samples;
    uint64_t verify_helper_table_null_samples;
    uint64_t verify_helper_bailout_samples;
} new_dynarec_stats_t;

typedef enum new_dynarec_post_purge_state_t {
    NEW_DYNAREC_POST_PURGE_HAVE_FREE_BLOCK,
    NEW_DYNAREC_POST_PURGE_RETRY_DIRTY_LIST,
    NEW_DYNAREC_POST_PURGE_RANDOM_EVICTION,
} new_dynarec_post_purge_state_t;

typedef enum new_dynarec_trace_kind_t {
    NEW_DYNAREC_TRACE_BLOCK_MARKED,
    NEW_DYNAREC_TRACE_BLOCK_MARK_DEFERRED,
    NEW_DYNAREC_TRACE_BLOCK_RECOMPILED,
    NEW_DYNAREC_TRACE_BLOCK_INVALIDATED,
    NEW_DYNAREC_TRACE_BLOCK_BECAME_BYTE_MASK,
    NEW_DYNAREC_TRACE_BLOCK_BECAME_NO_IMMEDIATES,
    NEW_DYNAREC_TRACE_ALLOCATOR_PRESSURE,
    NEW_DYNAREC_TRACE_RANDOM_EVICTION,
    NEW_DYNAREC_TRACE_VERIFY_SAMPLE,
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

typedef enum new_dynarec_d0d3_compare_bailout_reason_t {
    NEW_DYNAREC_D0D3_COMPARE_BAILOUT_NONE = 0,
    NEW_DYNAREC_D0D3_COMPARE_BAILOUT_NO_BLOCK_INS,
    NEW_DYNAREC_D0D3_COMPARE_BAILOUT_ZERO_COUNT,
} new_dynarec_d0d3_compare_bailout_reason_t;

typedef enum new_dynarec_verify_outcome_t {
    NEW_DYNAREC_VERIFY_DIRECT = 1,
    NEW_DYNAREC_VERIFY_HELPER_TABLE_NULL,
    NEW_DYNAREC_VERIFY_HELPER_BAILOUT,
} new_dynarec_verify_outcome_t;

typedef enum new_dynarec_gap_family_t {
    NEW_DYNAREC_GAP_FAMILY_NONE = 0,
    NEW_DYNAREC_GAP_FAMILY_REP,
    NEW_DYNAREC_GAP_FAMILY_SOFTFLOAT_X87,
} new_dynarec_gap_family_t;

typedef enum new_dynarec_fallback_family_t {
    NEW_DYNAREC_FALLBACK_FAMILY_NONE = 0,
    NEW_DYNAREC_FALLBACK_FAMILY_BASE,
    NEW_DYNAREC_FALLBACK_FAMILY_0F,
    NEW_DYNAREC_FALLBACK_FAMILY_X87,
    NEW_DYNAREC_FALLBACK_FAMILY_REP,
    NEW_DYNAREC_FALLBACK_FAMILY_3DNOW,
} new_dynarec_fallback_family_t;

typedef struct new_dynarec_verify_config_t {
    uint32_t pc;
    uint16_t opcode;
    uint8_t  match_pc;
    uint8_t  match_opcode;
    uint32_t budget;
} new_dynarec_verify_config_t;

extern new_dynarec_stats_t new_dynarec_stats;

extern void new_dynarec_stats_reset(void);
extern void new_dynarec_stats_snapshot(new_dynarec_stats_t *out);
extern int  new_dynarec_stats_logging_enabled(void);
extern int  new_dynarec_3dnow_hit_logging_enabled(void);
extern int  new_dynarec_gap_family_logging_enabled(void);
extern int  new_dynarec_fallback_family_logging_enabled(void);
extern int  new_dynarec_base_fallback_logging_enabled(void);
extern int  new_dynarec_0f_fallback_logging_enabled(void);
extern int  new_dynarec_rep_fallback_logging_enabled(void);
extern int  new_dynarec_d0d3_compare_enabled_for_site(uint32_t pc, uint8_t opcode);
extern int  new_dynarec_d0d3_compare_logging_enabled(void);
extern int  new_dynarec_d0d3_compare_site_logging_enabled(void);
extern int  new_dynarec_rep_scas_debug_enabled_for_site(uint32_t pc, uint8_t opcode);
extern int  new_dynarec_rep_scas_debug_logging_enabled(void);
extern int  new_dynarec_format_stats_summary(char *buffer, size_t size, const new_dynarec_stats_t *stats);
extern int  new_dynarec_format_3dnow_hit_summary(char *buffer, size_t size, uint8_t opcode);
extern int  new_dynarec_format_gap_family_summary(char *buffer, size_t size);
extern int  new_dynarec_format_fallback_family_summary(char *buffer, size_t size);
extern int  new_dynarec_format_base_fallback_summary(char *buffer, size_t size, uint8_t opcode);
extern int  new_dynarec_format_0f_fallback_summary(char *buffer, size_t size, uint8_t opcode);
extern int  new_dynarec_format_rep_fallback_summary(char *buffer, size_t size, uint8_t opcode);
extern int  new_dynarec_format_d0d3_compare_summary(char *buffer, size_t size);
extern int  new_dynarec_format_d0d3_compare_site_summary(char *buffer, size_t size, uint32_t index);
extern int  new_dynarec_format_d0d3_compare_sample_summary(char *buffer, size_t size, uint32_t index);
extern int  new_dynarec_format_d0d3_compare_bailout_summary(char *buffer, size_t size, uint32_t index);
extern int  new_dynarec_format_rep_scas_debug_summary(char *buffer, size_t size);
extern int  new_dynarec_format_rep_scas_debug_site_summary(char *buffer, size_t size, uint32_t index);
extern void new_dynarec_set_trace_hook(new_dynarec_trace_hook_t hook, void *opaque);
extern void new_dynarec_set_verify_config(const new_dynarec_verify_config_t *config);
extern void new_dynarec_get_verify_config(new_dynarec_verify_config_t *out);
extern int  new_dynarec_note_verify_sample(uint32_t pc, uint16_t opcode, new_dynarec_verify_outcome_t outcome);
extern void new_dynarec_note_3dnow_opcode_hit(uint8_t opcode, new_dynarec_verify_outcome_t outcome);
extern void new_dynarec_note_gap_family_hit(new_dynarec_gap_family_t family);
extern void new_dynarec_note_fallback_family_hit(new_dynarec_fallback_family_t family);
extern void new_dynarec_note_base_fallback_opcode_hit(uint8_t opcode, new_dynarec_verify_outcome_t outcome);
extern void new_dynarec_note_0f_fallback_opcode_hit(uint8_t opcode, new_dynarec_verify_outcome_t outcome);
extern void new_dynarec_note_rep_fallback_opcode_hit(uint8_t opcode, new_dynarec_verify_outcome_t outcome);
extern void new_dynarec_note_d0d3_compare_site(uint32_t pc, uint32_t packed_compare);
extern void new_dynarec_note_d0d3_compare_site_bailout(uint32_t pc, uint32_t packed_compare,
                                                       new_dynarec_d0d3_compare_bailout_reason_t reason);
extern void new_dynarec_note_d0d3_compare_site_zero_count_bailout(uint32_t pc, uint32_t packed_compare);
extern void new_dynarec_note_d0d3_compare_attempt(uint32_t packed_compare, uint32_t original_operand,
                                                  uint32_t direct_result, uint32_t direct_flag_mask,
                                                  uint32_t helper_result, uint32_t helper_flag_mask);
extern void new_dynarec_note_rep_scas_debug_site(uint32_t pc, uint8_t opcode);
extern int  new_dynarec_should_defer_marking_new_block(void);
extern int  new_dynarec_should_remove_aborted_mark_block(int mark_block_initialized, int unexpected_abrt);
extern int  new_dynarec_has_direct_pmaddwd_recompile(void);
extern int  new_dynarec_has_direct_3dnow_recompile(void);
extern int  new_dynarec_has_direct_3dnow_opcode_recompile(uint8_t opcode);
extern int  new_dynarec_direct_3dnow_opcode_count(void);
extern int  new_dynarec_has_direct_0f_opcode_recompile(uint8_t opcode);
extern int  new_dynarec_direct_0f_setcc_opcode_count(void);
extern int  new_dynarec_direct_0f_bswap_opcode_count(void);
extern int  new_dynarec_direct_0f_bitscan_opcode_count(void);
extern int  new_dynarec_has_direct_base_opcode_recompile(uint8_t opcode);
extern int  new_dynarec_direct_base_string_opcode_count(void);
extern int  new_dynarec_has_direct_rep_opcode_recompile(uint16_t opcode);
extern int  new_dynarec_direct_rep_string_opcode_count(void);

extern void new_dynarec_note_block_marked(uint32_t pc, uint32_t phys, uint32_t detail);
extern void new_dynarec_note_deferred_block_mark(uint32_t pc, uint32_t phys);
extern void new_dynarec_note_block_recompiled(uint32_t pc, uint32_t phys, uint32_t detail);
extern void new_dynarec_note_block_invalidated(uint32_t pc, uint32_t phys, uint16_t flags);
extern void new_dynarec_note_block_became_byte_mask(uint32_t pc, uint32_t phys, uint16_t flags);
extern void new_dynarec_note_block_became_no_immediates(uint32_t pc, uint32_t phys, uint16_t flags);
extern void new_dynarec_note_allocator_pressure(uint32_t detail);
extern void new_dynarec_note_allocator_pressure_empty_purgable_list(void);
extern void new_dynarec_note_random_eviction(uint32_t pc, uint32_t phys, uint16_t flags);
extern new_dynarec_post_purge_state_t new_dynarec_classify_post_purge_state(int free_list_nonempty, uint16_t dirty_list_tail);

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

static inline void
new_dynarec_note_purgable_flush_no_overlap(void)
{
    new_dynarec_stats.purgable_flush_no_overlap++;
}

static inline void
new_dynarec_note_purgable_flush_no_free_block(void)
{
    new_dynarec_stats.purgable_flush_no_free_block++;
}

static inline void
new_dynarec_note_purgable_flush_no_blocks(void)
{
    new_dynarec_stats.purgable_flush_no_blocks++;
}

static inline void
new_dynarec_note_purgable_flush_dirty_list_reuse(void)
{
    new_dynarec_stats.purgable_flush_dirty_list_reuses++;
}

static inline void
new_dynarec_note_purgable_page_enqueued(void)
{
    new_dynarec_stats.purgable_page_enqueues++;
}

static inline uint32_t
new_dynarec_pack_d0d3_rotate_compare(uint8_t opcode, uint8_t subgroup, uint8_t width, uint8_t is_memory,
                                     uint8_t count, uint32_t flags)
{
    uint32_t packed     = opcode;
    uint32_t width_code = 2;

    if (width == 8)
        width_code = 0;
    else if (width == 16)
        width_code = 1;

    packed |= ((uint32_t) ((subgroup == 0x18) ? 1u : 0u)) << 8;
    packed |= width_code << 9;
    packed |= ((uint32_t) (is_memory & 1u)) << 11;
    packed |= ((uint32_t) (count & 0x1fu)) << 12;
    packed |= (flags ? 1u : 0u) << 17;

    return packed;
}

static inline uint8_t
new_dynarec_d0d3_rotate_compare_opcode(uint32_t packed)
{
    return (uint8_t) (packed & 0xffu);
}

static inline uint8_t
new_dynarec_d0d3_rotate_compare_subgroup(uint32_t packed)
{
    return (packed & (1u << 8)) ? 0x18u : 0x10u;
}

static inline uint8_t
new_dynarec_d0d3_rotate_compare_width(uint32_t packed)
{
    switch ((packed >> 9) & 0x3u) {
        case 0:
            return 8;
        case 1:
            return 16;
        default:
            return 32;
    }
}

static inline uint8_t
new_dynarec_d0d3_rotate_compare_is_memory(uint32_t packed)
{
    return (uint8_t) ((packed >> 11) & 1u);
}

static inline uint8_t
new_dynarec_d0d3_rotate_compare_count(uint32_t packed)
{
    return (uint8_t) ((packed >> 12) & 0x1fu);
}

static inline uint32_t
new_dynarec_d0d3_rotate_compare_flags(uint32_t packed)
{
    return (packed & (1u << 17)) ? 1u : 0u;
}

static inline void
new_dynarec_note_purgable_page_enqueued_write(void)
{
    new_dynarec_stats.purgable_page_enqueues_write++;
}

static inline void
new_dynarec_note_purgable_page_enqueued_codegen(void)
{
    new_dynarec_stats.purgable_page_enqueues_codegen++;
}

static inline void
new_dynarec_note_purgable_page_enqueued_bulk_dirty(void)
{
    new_dynarec_stats.purgable_page_enqueues_bulk_dirty++;
}

static inline void
new_dynarec_note_purgable_page_dequeued_stale(void)
{
    new_dynarec_stats.purgable_page_dequeues_stale++;
}

static inline void
new_dynarec_note_purgable_page_dequeued_flush(void)
{
    new_dynarec_stats.purgable_page_dequeues_flush++;
}

static inline void
new_dynarec_note_purgable_page_dequeued_no_blocks(void)
{
    new_dynarec_stats.purgable_page_dequeues_no_blocks++;
}
#endif

extern void codegen_init(void);
extern void codegen_flush(void);

/*Current physical page of block being recompiled. -1 if no recompilation taking place */
extern uint32_t recomp_page;
extern int      codegen_in_recompile;

#endif
