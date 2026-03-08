#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codegen_public.h"

#ifdef USE_NEW_DYNAREC
new_dynarec_stats_t new_dynarec_stats;

static new_dynarec_trace_hook_t new_dynarec_trace_hook;
static void                    *new_dynarec_trace_hook_opaque;
static new_dynarec_verify_config_t new_dynarec_verify_config;
static int                        new_dynarec_verify_config_initialized;

static void
new_dynarec_emit_trace(new_dynarec_trace_kind_t kind, uint32_t pc, uint32_t phys, uint32_t detail, uint16_t flags)
{
    new_dynarec_trace_event_t event;

    if (!new_dynarec_trace_hook)
        return;

    event.kind   = kind;
    event.pc     = pc;
    event.phys   = phys;
    event.detail = detail;
    event.flags  = flags;

    new_dynarec_trace_hook(&event, new_dynarec_trace_hook_opaque);
}

static uint32_t
new_dynarec_parse_u32_env(const char *name, int *present)
{
    const char    *value = getenv(name);
    char          *end   = NULL;
    unsigned long  parsed;

    if (present)
        *present = 0;

    if (!value || !value[0])
        return 0;

    parsed = strtoul(value, &end, 0);
    if (!end || end == value || *end)
        return 0;

    if (present)
        *present = 1;

    return (uint32_t) parsed;
}

static void
new_dynarec_verify_config_init_from_env(void)
{
    int present_pc     = 0;
    int present_opcode = 0;
    int present_budget = 0;

    if (new_dynarec_verify_config_initialized)
        return;

    new_dynarec_verify_config_initialized = 1;
    memset(&new_dynarec_verify_config, 0, sizeof(new_dynarec_verify_config));

    new_dynarec_verify_config.pc     = new_dynarec_parse_u32_env("86BOX_NEW_DYNAREC_VERIFY_PC", &present_pc);
    new_dynarec_verify_config.opcode = (uint16_t) new_dynarec_parse_u32_env("86BOX_NEW_DYNAREC_VERIFY_OPCODE", &present_opcode);
    new_dynarec_verify_config.budget = new_dynarec_parse_u32_env("86BOX_NEW_DYNAREC_VERIFY_BUDGET", &present_budget);
    new_dynarec_verify_config.match_pc     = present_pc ? 1 : 0;
    new_dynarec_verify_config.match_opcode = present_opcode ? 1 : 0;

    if (!present_budget && (present_pc || present_opcode))
        new_dynarec_verify_config.budget = 1;
}

void
new_dynarec_stats_reset(void)
{
    memset(&new_dynarec_stats, 0, sizeof(new_dynarec_stats));
}

void
new_dynarec_stats_snapshot(new_dynarec_stats_t *out)
{
    if (!out)
        return;

    *out = new_dynarec_stats;
}

int
new_dynarec_stats_logging_enabled(void)
{
    static int enabled = -1;

    if (enabled == -1) {
        const char *value = getenv("86BOX_NEW_DYNAREC_STATS");

        enabled = (value && value[0] && strcmp(value, "0")) ? 1 : 0;
    }

    return enabled;
}

int
new_dynarec_format_stats_summary(char *buffer, size_t size, const new_dynarec_stats_t *stats)
{
    new_dynarec_stats_t snapshot;

    if (!buffer || !size)
        return 0;

    if (stats)
        snapshot = *stats;
    else
        snapshot = new_dynarec_stats;

    return snprintf(buffer, size,
                    "blocks_marked=%" PRIu64
                    " blocks_recompiled=%" PRIu64
                    " direct_recompiled_instructions=%" PRIu64
                    " helper_call_fallbacks=%" PRIu64
                    " direct_opcode_table_null_hits=%" PRIu64
                    " direct_handler_bailouts=%" PRIu64
                    " invalidations=%" PRIu64
                    " byte_mask_transitions=%" PRIu64
                    " no_immediates_transitions=%" PRIu64
                    " allocator_pressure_events=%" PRIu64
                    " allocator_pressure_empty_purgable_list=%" PRIu64
                    " purgable_page_enqueues=%" PRIu64
                    " purgable_page_enqueues_write=%" PRIu64
                    " purgable_page_enqueues_codegen=%" PRIu64
                    " purgable_page_enqueues_bulk_dirty=%" PRIu64
                    " purgable_page_dequeues_stale=%" PRIu64
                    " purgable_page_dequeues_flush=%" PRIu64
                    " purgable_page_dequeues_no_blocks=%" PRIu64
                    " purgable_flush_attempts=%" PRIu64
                    " purgable_flush_successes=%" PRIu64
                    " purgable_flush_no_overlap=%" PRIu64
                    " purgable_flush_no_free_block=%" PRIu64
                    " purgable_flush_no_blocks=%" PRIu64
                    " purgable_flush_dirty_list_reuses=%" PRIu64
                    " random_evictions=%" PRIu64
                    " verify_samples=%" PRIu64
                    " verify_direct_samples=%" PRIu64
                    " verify_helper_table_null_samples=%" PRIu64
                    " verify_helper_bailout_samples=%" PRIu64,
                    snapshot.blocks_marked,
                    snapshot.blocks_recompiled,
                    snapshot.direct_recompiled_instructions,
                    snapshot.helper_call_fallbacks,
                    snapshot.direct_opcode_table_null_hits,
                    snapshot.direct_handler_bailouts,
                    snapshot.invalidations,
                    snapshot.byte_mask_transitions,
                    snapshot.no_immediates_transitions,
                    snapshot.allocator_pressure_events,
                    snapshot.allocator_pressure_empty_purgable_list,
                    snapshot.purgable_page_enqueues,
                    snapshot.purgable_page_enqueues_write,
                    snapshot.purgable_page_enqueues_codegen,
                    snapshot.purgable_page_enqueues_bulk_dirty,
                    snapshot.purgable_page_dequeues_stale,
                    snapshot.purgable_page_dequeues_flush,
                    snapshot.purgable_page_dequeues_no_blocks,
                    snapshot.purgable_flush_attempts,
                    snapshot.purgable_flush_successes,
                    snapshot.purgable_flush_no_overlap,
                    snapshot.purgable_flush_no_free_block,
                    snapshot.purgable_flush_no_blocks,
                    snapshot.purgable_flush_dirty_list_reuses,
                    snapshot.random_evictions,
                    snapshot.verify_samples,
                    snapshot.verify_direct_samples,
                    snapshot.verify_helper_table_null_samples,
                    snapshot.verify_helper_bailout_samples);
}

void
new_dynarec_set_trace_hook(new_dynarec_trace_hook_t hook, void *opaque)
{
    new_dynarec_trace_hook        = hook;
    new_dynarec_trace_hook_opaque = opaque;
}

void
new_dynarec_set_verify_config(const new_dynarec_verify_config_t *config)
{
    new_dynarec_verify_config_initialized = 1;
    memset(&new_dynarec_verify_config, 0, sizeof(new_dynarec_verify_config));
    if (config)
        new_dynarec_verify_config = *config;
}

void
new_dynarec_get_verify_config(new_dynarec_verify_config_t *out)
{
    if (!out)
        return;

    *out = new_dynarec_verify_config;
}

int
new_dynarec_note_verify_sample(uint32_t pc, uint16_t opcode, new_dynarec_verify_outcome_t outcome)
{
    new_dynarec_verify_config_init_from_env();

    if (!new_dynarec_verify_config.budget)
        return 0;

    if (new_dynarec_verify_config.match_pc && new_dynarec_verify_config.pc != pc)
        return 0;

    if (new_dynarec_verify_config.match_opcode && new_dynarec_verify_config.opcode != opcode)
        return 0;

    switch (outcome) {
        case NEW_DYNAREC_VERIFY_DIRECT:
            break;
        case NEW_DYNAREC_VERIFY_HELPER_TABLE_NULL:
            break;
        case NEW_DYNAREC_VERIFY_HELPER_BAILOUT:
            break;
        default:
            return 0;
    }

    new_dynarec_verify_config.budget--;
    new_dynarec_stats.verify_samples++;
    if (outcome == NEW_DYNAREC_VERIFY_DIRECT)
        new_dynarec_stats.verify_direct_samples++;
    else if (outcome == NEW_DYNAREC_VERIFY_HELPER_TABLE_NULL)
        new_dynarec_stats.verify_helper_table_null_samples++;
    else
        new_dynarec_stats.verify_helper_bailout_samples++;

    new_dynarec_emit_trace(NEW_DYNAREC_TRACE_VERIFY_SAMPLE, pc, 0, opcode, outcome);
    return 1;
}

void
new_dynarec_note_block_marked(uint32_t pc, uint32_t phys, uint32_t detail)
{
    new_dynarec_stats.blocks_marked++;
    new_dynarec_emit_trace(NEW_DYNAREC_TRACE_BLOCK_MARKED, pc, phys, detail, 0);
}

void
new_dynarec_note_block_recompiled(uint32_t pc, uint32_t phys, uint32_t detail)
{
    new_dynarec_stats.blocks_recompiled++;
    new_dynarec_emit_trace(NEW_DYNAREC_TRACE_BLOCK_RECOMPILED, pc, phys, detail, 0);
}

void
new_dynarec_note_block_invalidated(uint32_t pc, uint32_t phys, uint16_t flags)
{
    new_dynarec_stats.invalidations++;
    new_dynarec_emit_trace(NEW_DYNAREC_TRACE_BLOCK_INVALIDATED, pc, phys, 0, flags);
}

void
new_dynarec_note_block_became_byte_mask(uint32_t pc, uint32_t phys, uint16_t flags)
{
    new_dynarec_stats.byte_mask_transitions++;
    new_dynarec_emit_trace(NEW_DYNAREC_TRACE_BLOCK_BECAME_BYTE_MASK, pc, phys, 0, flags);
}

void
new_dynarec_note_block_became_no_immediates(uint32_t pc, uint32_t phys, uint16_t flags)
{
    new_dynarec_stats.no_immediates_transitions++;
    new_dynarec_emit_trace(NEW_DYNAREC_TRACE_BLOCK_BECAME_NO_IMMEDIATES, pc, phys, 0, flags);
}

void
new_dynarec_note_allocator_pressure(uint32_t detail)
{
    new_dynarec_stats.allocator_pressure_events++;
    new_dynarec_emit_trace(NEW_DYNAREC_TRACE_ALLOCATOR_PRESSURE, 0, 0, detail, 0);
}

void
new_dynarec_note_random_eviction(uint32_t pc, uint32_t phys, uint16_t flags)
{
    new_dynarec_stats.random_evictions++;
    new_dynarec_emit_trace(NEW_DYNAREC_TRACE_RANDOM_EVICTION, pc, phys, 0, flags);
}

new_dynarec_post_purge_state_t
new_dynarec_classify_post_purge_state(int free_list_nonempty, uint16_t dirty_list_tail)
{
    if (free_list_nonempty)
        return NEW_DYNAREC_POST_PURGE_HAVE_FREE_BLOCK;

    if (dirty_list_tail != BLOCK_INVALID)
        return NEW_DYNAREC_POST_PURGE_RETRY_DIRTY_LIST;

    return NEW_DYNAREC_POST_PURGE_RANDOM_EVICTION;
}
#endif
