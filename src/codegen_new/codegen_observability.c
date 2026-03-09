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
static uint32_t                   new_dynarec_mark_deferral_budget;
static uint64_t                   new_dynarec_3dnow_direct_hits[256];
static uint64_t                   new_dynarec_3dnow_helper_table_null_hits[256];
static uint64_t                   new_dynarec_3dnow_helper_bailout_hits[256];
static uint64_t                   new_dynarec_rep_gap_hits;
static uint64_t                   new_dynarec_softfloat_x87_gap_hits;
static uint64_t                   new_dynarec_fallback_family_hits[6];
static uint64_t                   new_dynarec_base_fallback_table_null_hits[256];
static uint64_t                   new_dynarec_base_fallback_bailout_hits[256];
static uint64_t                   new_dynarec_0f_fallback_table_null_hits[256];
static uint64_t                   new_dynarec_0f_fallback_bailout_hits[256];
typedef struct {
    uint32_t pc;
    uint32_t packed_compare;
    uint32_t original_operand;
    uint32_t direct_result;
    uint32_t direct_flag_mask;
    uint32_t helper_result;
    uint32_t helper_flag_mask;
} new_dynarec_d0d3_compare_sample_t;
typedef struct {
    uint32_t pc;
    uint32_t packed_compare;
    uint32_t reason;
} new_dynarec_d0d3_compare_bailout_sample_t;
static struct {
    new_dynarec_d0d3_compare_sample_t recent_samples[8];
    new_dynarec_d0d3_compare_bailout_sample_t recent_bailouts[8];
    uint64_t attempts;
    uint64_t matches;
    uint64_t mismatches;
    uint32_t site_count;
    uint32_t site_log_overflow;
    uint32_t last_pc;
    uint32_t recent_sample_count;
    uint32_t recent_sample_next;
    uint32_t recent_bailout_count;
    uint32_t recent_bailout_next;
    uint32_t packed_compare;
    uint32_t original_operand;
    uint32_t direct_result;
    uint32_t direct_flag_mask;
    uint32_t helper_result;
    uint32_t helper_flag_mask;
    uint32_t site_pcs[32];
    uint32_t site_packed_compare[32];
    uint64_t site_attempts[32];
    uint64_t site_mismatches[32];
    uint64_t site_no_block_ins_bailouts[32];
    uint64_t site_zero_count_bailouts[32];
    uint64_t site_count_zero[32];
    uint64_t site_count_one[32];
    uint64_t site_count_multi[32];
    uint64_t site_cf_set[32];
    uint64_t site_operand_zero[32];
    uint64_t site_result_changed[32];
    uint64_t site_flags_nonzero[32];
} new_dynarec_d0d3_compare_state;

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
    new_dynarec_mark_deferral_budget = 0;
    memset(new_dynarec_3dnow_direct_hits, 0, sizeof(new_dynarec_3dnow_direct_hits));
    memset(new_dynarec_3dnow_helper_table_null_hits, 0, sizeof(new_dynarec_3dnow_helper_table_null_hits));
    memset(new_dynarec_3dnow_helper_bailout_hits, 0, sizeof(new_dynarec_3dnow_helper_bailout_hits));
    new_dynarec_rep_gap_hits           = 0;
    new_dynarec_softfloat_x87_gap_hits = 0;
    memset(new_dynarec_fallback_family_hits, 0, sizeof(new_dynarec_fallback_family_hits));
    memset(new_dynarec_base_fallback_table_null_hits, 0, sizeof(new_dynarec_base_fallback_table_null_hits));
    memset(new_dynarec_base_fallback_bailout_hits, 0, sizeof(new_dynarec_base_fallback_bailout_hits));
    memset(new_dynarec_0f_fallback_table_null_hits, 0, sizeof(new_dynarec_0f_fallback_table_null_hits));
    memset(new_dynarec_0f_fallback_bailout_hits, 0, sizeof(new_dynarec_0f_fallback_bailout_hits));
    memset(&new_dynarec_d0d3_compare_state, 0, sizeof(new_dynarec_d0d3_compare_state));
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
new_dynarec_3dnow_hit_logging_enabled(void)
{
    static int enabled = -1;

    if (enabled == -1) {
        const char *value = getenv("86BOX_NEW_DYNAREC_LOG_3DNOW_HITS");

        enabled = (value && value[0] && strcmp(value, "0")) ? 1 : 0;
    }

    return enabled;
}

int
new_dynarec_gap_family_logging_enabled(void)
{
    static int enabled = -1;

    if (enabled == -1) {
        const char *value = getenv("86BOX_NEW_DYNAREC_LOG_GAP_FAMILIES");

        enabled = (value && value[0] && strcmp(value, "0")) ? 1 : 0;
    }

    return enabled;
}

int
new_dynarec_fallback_family_logging_enabled(void)
{
    static int enabled = -1;

    if (enabled == -1) {
        const char *value = getenv("86BOX_NEW_DYNAREC_LOG_FALLBACK_FAMILIES");

        enabled = (value && value[0] && strcmp(value, "0")) ? 1 : 0;
    }

    return enabled;
}

int
new_dynarec_base_fallback_logging_enabled(void)
{
    static int enabled = -1;

    if (enabled == -1) {
        const char *value = getenv("86BOX_NEW_DYNAREC_LOG_BASE_FALLBACKS");

        enabled = (value && value[0] && strcmp(value, "0")) ? 1 : 0;
    }

    return enabled;
}

int
new_dynarec_0f_fallback_logging_enabled(void)
{
    static int enabled = -1;

    if (enabled == -1) {
        const char *value = getenv("86BOX_NEW_DYNAREC_LOG_0F_FALLBACKS");

        enabled = (value && value[0] && strcmp(value, "0")) ? 1 : 0;
    }

    return enabled;
}

int
new_dynarec_d0d3_compare_logging_enabled(void)
{
    static int enabled = -1;

    if (enabled == -1) {
        const char *value = getenv("86BOX_NEW_DYNAREC_LOG_D0D3_COMPARE");

        enabled = (value && value[0] && strcmp(value, "0")) ? 1 : 0;
    }

    return enabled;
}

int
new_dynarec_d0d3_compare_site_logging_enabled(void)
{
    static int enabled = -1;

    if (enabled == -1) {
        const char *value = getenv("86BOX_NEW_DYNAREC_LOG_D0D3_COMPARE_SITES");

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
                    " deferred_block_marks=%" PRIu64
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
                    snapshot.deferred_block_marks,
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

int
new_dynarec_format_3dnow_hit_summary(char *buffer, size_t size, uint8_t opcode)
{
    uint64_t direct_hits;
    uint64_t helper_table_null_hits;
    uint64_t helper_bailout_hits;

    if (!buffer || !size)
        return 0;

    direct_hits            = new_dynarec_3dnow_direct_hits[opcode];
    helper_table_null_hits = new_dynarec_3dnow_helper_table_null_hits[opcode];
    helper_bailout_hits    = new_dynarec_3dnow_helper_bailout_hits[opcode];

    if (!(direct_hits || helper_table_null_hits || helper_bailout_hits))
        return 0;

    return snprintf(buffer, size,
                    "opcode=0x%02x direct=%" PRIu64 " helper_table_null=%" PRIu64 " helper_bailout=%" PRIu64,
                    opcode, direct_hits, helper_table_null_hits, helper_bailout_hits);
}

int
new_dynarec_format_gap_family_summary(char *buffer, size_t size)
{
    if (!buffer || !size)
        return 0;

    if (!(new_dynarec_rep_gap_hits || new_dynarec_softfloat_x87_gap_hits))
        return 0;

    return snprintf(buffer, size,
                    "rep=%" PRIu64 " softfloat_x87=%" PRIu64,
                    new_dynarec_rep_gap_hits, new_dynarec_softfloat_x87_gap_hits);
}

int
new_dynarec_format_fallback_family_summary(char *buffer, size_t size)
{
    if (!buffer || !size)
        return 0;

    if (!(new_dynarec_fallback_family_hits[NEW_DYNAREC_FALLBACK_FAMILY_BASE]
          || new_dynarec_fallback_family_hits[NEW_DYNAREC_FALLBACK_FAMILY_0F]
          || new_dynarec_fallback_family_hits[NEW_DYNAREC_FALLBACK_FAMILY_X87]
          || new_dynarec_fallback_family_hits[NEW_DYNAREC_FALLBACK_FAMILY_REP]
          || new_dynarec_fallback_family_hits[NEW_DYNAREC_FALLBACK_FAMILY_3DNOW]))
        return 0;

    return snprintf(buffer, size,
                    "base=%" PRIu64 " 0f=%" PRIu64 " x87=%" PRIu64 " rep=%" PRIu64 " 3dnow=%" PRIu64,
                    new_dynarec_fallback_family_hits[NEW_DYNAREC_FALLBACK_FAMILY_BASE],
                    new_dynarec_fallback_family_hits[NEW_DYNAREC_FALLBACK_FAMILY_0F],
                    new_dynarec_fallback_family_hits[NEW_DYNAREC_FALLBACK_FAMILY_X87],
                    new_dynarec_fallback_family_hits[NEW_DYNAREC_FALLBACK_FAMILY_REP],
                    new_dynarec_fallback_family_hits[NEW_DYNAREC_FALLBACK_FAMILY_3DNOW]);
}

int
new_dynarec_format_base_fallback_summary(char *buffer, size_t size, uint8_t opcode)
{
    uint64_t helper_table_null_hits;
    uint64_t helper_bailout_hits;

    if (!buffer || !size)
        return 0;

    helper_table_null_hits = new_dynarec_base_fallback_table_null_hits[opcode];
    helper_bailout_hits    = new_dynarec_base_fallback_bailout_hits[opcode];

    if (!(helper_table_null_hits || helper_bailout_hits))
        return 0;

    return snprintf(buffer, size,
                    "opcode=0x%02x helper_table_null=%" PRIu64 " helper_bailout=%" PRIu64,
                    opcode, helper_table_null_hits, helper_bailout_hits);
}

int
new_dynarec_format_0f_fallback_summary(char *buffer, size_t size, uint8_t opcode)
{
    uint64_t helper_table_null_hits;
    uint64_t helper_bailout_hits;

    if (!buffer || !size)
        return 0;

    helper_table_null_hits = new_dynarec_0f_fallback_table_null_hits[opcode];
    helper_bailout_hits    = new_dynarec_0f_fallback_bailout_hits[opcode];

    if (!(helper_table_null_hits || helper_bailout_hits))
        return 0;

    return snprintf(buffer, size,
                    "opcode=0x%02x helper_table_null=%" PRIu64 " helper_bailout=%" PRIu64,
                    opcode, helper_table_null_hits, helper_bailout_hits);
}

int
new_dynarec_format_d0d3_compare_summary(char *buffer, size_t size)
{
    const char *form;

    if (!buffer || !size)
        return 0;

    if (!(new_dynarec_d0d3_compare_state.attempts || new_dynarec_d0d3_compare_state.site_count))
        return 0;

    form = new_dynarec_d0d3_rotate_compare_is_memory(new_dynarec_d0d3_compare_state.packed_compare) ? "mem" : "reg";

    return snprintf(buffer, size,
                    "attempts=%" PRIu64
                    " matches=%" PRIu64
                    " mismatches=%" PRIu64
                    " sites=%u"
                    " site_log_overflow=%u"
                    " last_pc=0x%08x"
                    " last_opcode=0x%02x"
                    " last_subgroup=0x%02x"
                    " last_width=%u"
                    " last_form=%s"
                    " last_operand=0x%08x"
                    " last_count=%u"
                    " last_cf=%u"
                    " last_direct_result=0x%08x"
                    " last_direct_flags=0x%08x"
                    " last_helper_result=0x%08x"
                    " last_helper_flags=0x%08x",
                    new_dynarec_d0d3_compare_state.attempts,
                    new_dynarec_d0d3_compare_state.matches,
                    new_dynarec_d0d3_compare_state.mismatches,
                    new_dynarec_d0d3_compare_state.site_count,
                    new_dynarec_d0d3_compare_state.site_log_overflow,
                    new_dynarec_d0d3_compare_state.last_pc,
                    new_dynarec_d0d3_rotate_compare_opcode(new_dynarec_d0d3_compare_state.packed_compare),
                    new_dynarec_d0d3_rotate_compare_subgroup(new_dynarec_d0d3_compare_state.packed_compare),
                    new_dynarec_d0d3_rotate_compare_width(new_dynarec_d0d3_compare_state.packed_compare),
                    form,
                    new_dynarec_d0d3_compare_state.original_operand,
                    new_dynarec_d0d3_rotate_compare_count(new_dynarec_d0d3_compare_state.packed_compare),
                    new_dynarec_d0d3_rotate_compare_flags(new_dynarec_d0d3_compare_state.packed_compare) ? 1u : 0u,
                    new_dynarec_d0d3_compare_state.direct_result,
                    new_dynarec_d0d3_compare_state.direct_flag_mask,
                    new_dynarec_d0d3_compare_state.helper_result,
                    new_dynarec_d0d3_compare_state.helper_flag_mask);
}

int
new_dynarec_format_d0d3_compare_site_summary(char *buffer, size_t size, uint32_t index)
{
    uint32_t    pc;
    uint32_t    packed_compare;
    const char *form;

    if (!buffer || !size)
        return 0;

    if (index >= new_dynarec_d0d3_compare_state.site_count)
        return 0;

    pc             = new_dynarec_d0d3_compare_state.site_pcs[index];
    packed_compare = new_dynarec_d0d3_compare_state.site_packed_compare[index];
    form           = new_dynarec_d0d3_rotate_compare_is_memory(packed_compare) ? "mem" : "reg";

    return snprintf(buffer, size,
                    "index=%u pc=0x%08x opcode=0x%02x subgroup=0x%02x width=%u form=%s attempts=%" PRIu64 " mismatches=%" PRIu64 " no_block_ins_bailouts=%" PRIu64 " zero_count_bailouts=%" PRIu64 " count_zero=%" PRIu64 " count_one=%" PRIu64 " count_multi=%" PRIu64 " cf_set=%" PRIu64 " operand_zero=%" PRIu64 " result_changed=%" PRIu64 " flags_nonzero=%" PRIu64,
                    index,
                    pc,
                    new_dynarec_d0d3_rotate_compare_opcode(packed_compare),
                    new_dynarec_d0d3_rotate_compare_subgroup(packed_compare),
                    new_dynarec_d0d3_rotate_compare_width(packed_compare),
                    form,
                    new_dynarec_d0d3_compare_state.site_attempts[index],
                    new_dynarec_d0d3_compare_state.site_mismatches[index],
                    new_dynarec_d0d3_compare_state.site_no_block_ins_bailouts[index],
                    new_dynarec_d0d3_compare_state.site_zero_count_bailouts[index],
                    new_dynarec_d0d3_compare_state.site_count_zero[index],
                    new_dynarec_d0d3_compare_state.site_count_one[index],
                    new_dynarec_d0d3_compare_state.site_count_multi[index],
                    new_dynarec_d0d3_compare_state.site_cf_set[index],
                    new_dynarec_d0d3_compare_state.site_operand_zero[index],
                    new_dynarec_d0d3_compare_state.site_result_changed[index],
                    new_dynarec_d0d3_compare_state.site_flags_nonzero[index]);
}

int
new_dynarec_format_d0d3_compare_sample_summary(char *buffer, size_t size, uint32_t index)
{
    uint32_t absolute_index;
    uint32_t ring_index;
    const char *form;
    const new_dynarec_d0d3_compare_sample_t *sample;

    if (!buffer || !size)
        return 0;

    if (index >= new_dynarec_d0d3_compare_state.recent_sample_count)
        return 0;

    absolute_index = (new_dynarec_d0d3_compare_state.recent_sample_next
                      + (sizeof(new_dynarec_d0d3_compare_state.recent_samples) / sizeof(new_dynarec_d0d3_compare_state.recent_samples[0]))
                      - new_dynarec_d0d3_compare_state.recent_sample_count + index)
                     % (sizeof(new_dynarec_d0d3_compare_state.recent_samples) / sizeof(new_dynarec_d0d3_compare_state.recent_samples[0]));
    ring_index = absolute_index;
    sample = &new_dynarec_d0d3_compare_state.recent_samples[ring_index];
    form = new_dynarec_d0d3_rotate_compare_is_memory(sample->packed_compare) ? "mem" : "reg";

    return snprintf(buffer, size,
                    "index=%u pc=0x%08x opcode=0x%02x subgroup=0x%02x width=%u form=%s operand=0x%08x count=%u cf=%u direct=0x%08x/0x%08x helper=0x%08x/0x%08x",
                    index,
                    sample->pc,
                    new_dynarec_d0d3_rotate_compare_opcode(sample->packed_compare),
                    new_dynarec_d0d3_rotate_compare_subgroup(sample->packed_compare),
                    new_dynarec_d0d3_rotate_compare_width(sample->packed_compare),
                    form,
                    sample->original_operand,
                    new_dynarec_d0d3_rotate_compare_count(sample->packed_compare),
                    new_dynarec_d0d3_rotate_compare_flags(sample->packed_compare) ? 1u : 0u,
                    sample->direct_result, sample->direct_flag_mask,
                    sample->helper_result, sample->helper_flag_mask);
}

int
new_dynarec_format_d0d3_compare_bailout_summary(char *buffer, size_t size, uint32_t index)
{
    uint32_t absolute_index;
    uint32_t ring_index;
    const char *form;
    const char *reason;
    const new_dynarec_d0d3_compare_bailout_sample_t *sample;

    if (!buffer || !size)
        return 0;

    if (index >= new_dynarec_d0d3_compare_state.recent_bailout_count)
        return 0;

    absolute_index = (new_dynarec_d0d3_compare_state.recent_bailout_next
                      + (sizeof(new_dynarec_d0d3_compare_state.recent_bailouts) / sizeof(new_dynarec_d0d3_compare_state.recent_bailouts[0]))
                      - new_dynarec_d0d3_compare_state.recent_bailout_count + index)
                     % (sizeof(new_dynarec_d0d3_compare_state.recent_bailouts) / sizeof(new_dynarec_d0d3_compare_state.recent_bailouts[0]));
    ring_index = absolute_index;
    sample = &new_dynarec_d0d3_compare_state.recent_bailouts[ring_index];
    form = new_dynarec_d0d3_rotate_compare_is_memory(sample->packed_compare) ? "mem" : "reg";
    if (sample->reason == NEW_DYNAREC_D0D3_COMPARE_BAILOUT_NO_BLOCK_INS)
        reason = "no_block_ins";
    else if (sample->reason == NEW_DYNAREC_D0D3_COMPARE_BAILOUT_ZERO_COUNT)
        reason = "zero_count";
    else
        reason = "unknown";

    return snprintf(buffer, size,
                    "index=%u pc=0x%08x opcode=0x%02x subgroup=0x%02x width=%u form=%s reason=%s count=%u cf=%u",
                    index,
                    sample->pc,
                    new_dynarec_d0d3_rotate_compare_opcode(sample->packed_compare),
                    new_dynarec_d0d3_rotate_compare_subgroup(sample->packed_compare),
                    new_dynarec_d0d3_rotate_compare_width(sample->packed_compare),
                    form,
                    reason,
                    new_dynarec_d0d3_rotate_compare_count(sample->packed_compare),
                    new_dynarec_d0d3_rotate_compare_flags(sample->packed_compare) ? 1u : 0u);
}

static uint32_t
new_dynarec_find_d0d3_compare_site_index(uint32_t pc)
{
    uint32_t index;

    for (index = 0; index < new_dynarec_d0d3_compare_state.site_count; index++) {
        if (new_dynarec_d0d3_compare_state.site_pcs[index] == pc)
            return index;
    }

    return UINT32_MAX;
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
new_dynarec_d0d3_compare_enabled_for_site(uint32_t pc, uint8_t opcode)
{
    static int enabled = -1;

    if (enabled == -1) {
        const char *value = getenv("86BOX_NEW_DYNAREC_DEBUG_D0D3_RCLRCR");

        enabled = (value && value[0] && strcmp(value, "0")) ? 1 : 0;
    }

    if (!enabled)
        return 0;

    new_dynarec_verify_config_init_from_env();

    if (!new_dynarec_verify_config.budget)
        return 0;

    if (new_dynarec_verify_config.match_pc && new_dynarec_verify_config.pc != pc)
        return 0;

    if (new_dynarec_verify_config.match_opcode && new_dynarec_verify_config.opcode != opcode)
        return 0;

    return 1;
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
new_dynarec_note_3dnow_opcode_hit(uint8_t opcode, new_dynarec_verify_outcome_t outcome)
{
    switch (outcome) {
        case NEW_DYNAREC_VERIFY_DIRECT:
            new_dynarec_3dnow_direct_hits[opcode]++;
            break;
        case NEW_DYNAREC_VERIFY_HELPER_TABLE_NULL:
            new_dynarec_3dnow_helper_table_null_hits[opcode]++;
            break;
        case NEW_DYNAREC_VERIFY_HELPER_BAILOUT:
            new_dynarec_3dnow_helper_bailout_hits[opcode]++;
            break;
        default:
            break;
    }
}

void
new_dynarec_note_gap_family_hit(new_dynarec_gap_family_t family)
{
    switch (family) {
        case NEW_DYNAREC_GAP_FAMILY_REP:
            new_dynarec_rep_gap_hits++;
            break;
        case NEW_DYNAREC_GAP_FAMILY_SOFTFLOAT_X87:
            new_dynarec_softfloat_x87_gap_hits++;
            break;
        default:
            break;
    }
}

void
new_dynarec_note_fallback_family_hit(new_dynarec_fallback_family_t family)
{
    if (family <= NEW_DYNAREC_FALLBACK_FAMILY_NONE || family > NEW_DYNAREC_FALLBACK_FAMILY_3DNOW)
        return;

    new_dynarec_fallback_family_hits[family]++;
}

void
new_dynarec_note_base_fallback_opcode_hit(uint8_t opcode, new_dynarec_verify_outcome_t outcome)
{
    if (outcome == NEW_DYNAREC_VERIFY_HELPER_TABLE_NULL)
        new_dynarec_base_fallback_table_null_hits[opcode]++;
    else if (outcome == NEW_DYNAREC_VERIFY_HELPER_BAILOUT)
        new_dynarec_base_fallback_bailout_hits[opcode]++;
}

void
new_dynarec_note_0f_fallback_opcode_hit(uint8_t opcode, new_dynarec_verify_outcome_t outcome)
{
    if (outcome == NEW_DYNAREC_VERIFY_HELPER_TABLE_NULL)
        new_dynarec_0f_fallback_table_null_hits[opcode]++;
    else if (outcome == NEW_DYNAREC_VERIFY_HELPER_BAILOUT)
        new_dynarec_0f_fallback_bailout_hits[opcode]++;
}

void
new_dynarec_note_d0d3_compare_attempt(uint32_t packed_compare, uint32_t original_operand,
                                      uint32_t direct_result, uint32_t direct_flag_mask,
                                      uint32_t helper_result, uint32_t helper_flag_mask)
{
    const int mismatch = (direct_result != helper_result) || (direct_flag_mask != helper_flag_mask);

    new_dynarec_d0d3_compare_state.attempts++;
    if (mismatch)
        new_dynarec_d0d3_compare_state.mismatches++;
    else
        new_dynarec_d0d3_compare_state.matches++;

    new_dynarec_d0d3_compare_state.packed_compare   = packed_compare;
    new_dynarec_d0d3_compare_state.original_operand = original_operand;
    new_dynarec_d0d3_compare_state.direct_result    = direct_result;
    new_dynarec_d0d3_compare_state.direct_flag_mask = direct_flag_mask;
    new_dynarec_d0d3_compare_state.helper_result    = helper_result;
    new_dynarec_d0d3_compare_state.helper_flag_mask = helper_flag_mask;
    new_dynarec_d0d3_compare_state.recent_samples[new_dynarec_d0d3_compare_state.recent_sample_next].pc               = new_dynarec_d0d3_compare_state.last_pc;
    new_dynarec_d0d3_compare_state.recent_samples[new_dynarec_d0d3_compare_state.recent_sample_next].packed_compare   = packed_compare;
    new_dynarec_d0d3_compare_state.recent_samples[new_dynarec_d0d3_compare_state.recent_sample_next].original_operand = original_operand;
    new_dynarec_d0d3_compare_state.recent_samples[new_dynarec_d0d3_compare_state.recent_sample_next].direct_result    = direct_result;
    new_dynarec_d0d3_compare_state.recent_samples[new_dynarec_d0d3_compare_state.recent_sample_next].direct_flag_mask = direct_flag_mask;
    new_dynarec_d0d3_compare_state.recent_samples[new_dynarec_d0d3_compare_state.recent_sample_next].helper_result    = helper_result;
    new_dynarec_d0d3_compare_state.recent_samples[new_dynarec_d0d3_compare_state.recent_sample_next].helper_flag_mask = helper_flag_mask;
    new_dynarec_d0d3_compare_state.recent_sample_next =
        (new_dynarec_d0d3_compare_state.recent_sample_next + 1u)
        % (sizeof(new_dynarec_d0d3_compare_state.recent_samples) / sizeof(new_dynarec_d0d3_compare_state.recent_samples[0]));
    if (new_dynarec_d0d3_compare_state.recent_sample_count
        < (sizeof(new_dynarec_d0d3_compare_state.recent_samples) / sizeof(new_dynarec_d0d3_compare_state.recent_samples[0])))
        new_dynarec_d0d3_compare_state.recent_sample_count++;

    if (new_dynarec_d0d3_compare_state.last_pc != 0) {
        const uint32_t site_index = new_dynarec_find_d0d3_compare_site_index(new_dynarec_d0d3_compare_state.last_pc);

        if (site_index != UINT32_MAX) {
            const uint32_t count = new_dynarec_d0d3_rotate_compare_count(packed_compare);

            new_dynarec_d0d3_compare_state.site_attempts[site_index]++;
            if (mismatch)
                new_dynarec_d0d3_compare_state.site_mismatches[site_index]++;
            if (count == 0)
                new_dynarec_d0d3_compare_state.site_count_zero[site_index]++;
            else if (count == 1)
                new_dynarec_d0d3_compare_state.site_count_one[site_index]++;
            else
                new_dynarec_d0d3_compare_state.site_count_multi[site_index]++;
            if (new_dynarec_d0d3_rotate_compare_flags(packed_compare))
                new_dynarec_d0d3_compare_state.site_cf_set[site_index]++;
            if (original_operand == 0)
                new_dynarec_d0d3_compare_state.site_operand_zero[site_index]++;
            if (direct_result != original_operand)
                new_dynarec_d0d3_compare_state.site_result_changed[site_index]++;
            if (direct_flag_mask != 0)
                new_dynarec_d0d3_compare_state.site_flags_nonzero[site_index]++;
        }
    }

    if (mismatch && new_dynarec_d0d3_compare_logging_enabled()) {
        fprintf(stderr,
                "CPU new dynarec D0-D3 compare mismatch: opcode=0x%02x subgroup=0x%02x width=%u form=%s operand=0x%08x count=%u cf=%u direct=0x%08x/0x%08x helper=0x%08x/0x%08x\n",
                new_dynarec_d0d3_rotate_compare_opcode(packed_compare),
                new_dynarec_d0d3_rotate_compare_subgroup(packed_compare),
                new_dynarec_d0d3_rotate_compare_width(packed_compare),
                new_dynarec_d0d3_rotate_compare_is_memory(packed_compare) ? "mem" : "reg",
                original_operand,
                new_dynarec_d0d3_rotate_compare_count(packed_compare),
                new_dynarec_d0d3_rotate_compare_flags(packed_compare) ? 1u : 0u,
                direct_result, direct_flag_mask, helper_result, helper_flag_mask);
    }
}

void
new_dynarec_note_d0d3_compare_site(uint32_t pc, uint32_t packed_compare)
{
    uint32_t index;

    new_dynarec_d0d3_compare_state.last_pc         = pc;
    new_dynarec_d0d3_compare_state.packed_compare  = packed_compare;

    for (index = 0; index < new_dynarec_d0d3_compare_state.site_count; index++) {
        if (new_dynarec_d0d3_compare_state.site_pcs[index] == pc)
            return;
    }

    if (new_dynarec_d0d3_compare_state.site_count >= (sizeof(new_dynarec_d0d3_compare_state.site_pcs) / sizeof(new_dynarec_d0d3_compare_state.site_pcs[0]))) {
        new_dynarec_d0d3_compare_state.site_log_overflow = 1;
        return;
    }

    new_dynarec_d0d3_compare_state.site_pcs[new_dynarec_d0d3_compare_state.site_count]           = pc;
    new_dynarec_d0d3_compare_state.site_packed_compare[new_dynarec_d0d3_compare_state.site_count] = packed_compare;
    new_dynarec_d0d3_compare_state.site_count++;

    if (new_dynarec_d0d3_compare_site_logging_enabled()) {
        fprintf(stderr,
                "CPU new dynarec D0-D3 compare site: pc=0x%08x opcode=0x%02x subgroup=0x%02x width=%u form=%s\n",
                pc,
                new_dynarec_d0d3_rotate_compare_opcode(packed_compare),
                new_dynarec_d0d3_rotate_compare_subgroup(packed_compare),
                new_dynarec_d0d3_rotate_compare_width(packed_compare),
                new_dynarec_d0d3_rotate_compare_is_memory(packed_compare) ? "mem" : "reg");
    }
}

void
new_dynarec_note_d0d3_compare_site_bailout(uint32_t pc, uint32_t packed_compare,
                                           new_dynarec_d0d3_compare_bailout_reason_t reason)
{
    uint32_t site_index;

    new_dynarec_note_d0d3_compare_site(pc, packed_compare);
    site_index = new_dynarec_find_d0d3_compare_site_index(pc);
    if (site_index == UINT32_MAX)
        return;

    if (reason == NEW_DYNAREC_D0D3_COMPARE_BAILOUT_NO_BLOCK_INS)
        new_dynarec_d0d3_compare_state.site_no_block_ins_bailouts[site_index]++;
    else if (reason == NEW_DYNAREC_D0D3_COMPARE_BAILOUT_ZERO_COUNT)
        new_dynarec_d0d3_compare_state.site_zero_count_bailouts[site_index]++;

    new_dynarec_d0d3_compare_state.recent_bailouts[new_dynarec_d0d3_compare_state.recent_bailout_next].pc = pc;
    new_dynarec_d0d3_compare_state.recent_bailouts[new_dynarec_d0d3_compare_state.recent_bailout_next].packed_compare = packed_compare;
    new_dynarec_d0d3_compare_state.recent_bailouts[new_dynarec_d0d3_compare_state.recent_bailout_next].reason = (uint32_t) reason;
    new_dynarec_d0d3_compare_state.recent_bailout_next =
        (new_dynarec_d0d3_compare_state.recent_bailout_next + 1u)
        % (sizeof(new_dynarec_d0d3_compare_state.recent_bailouts) / sizeof(new_dynarec_d0d3_compare_state.recent_bailouts[0]));
    if (new_dynarec_d0d3_compare_state.recent_bailout_count
        < (sizeof(new_dynarec_d0d3_compare_state.recent_bailouts) / sizeof(new_dynarec_d0d3_compare_state.recent_bailouts[0])))
        new_dynarec_d0d3_compare_state.recent_bailout_count++;
}

void
new_dynarec_note_d0d3_compare_site_zero_count_bailout(uint32_t pc, uint32_t packed_compare)
{
    new_dynarec_note_d0d3_compare_site_bailout(pc, packed_compare,
                                               NEW_DYNAREC_D0D3_COMPARE_BAILOUT_ZERO_COUNT);
}

int
new_dynarec_should_defer_marking_new_block(void)
{
    if (!new_dynarec_mark_deferral_budget)
        return 0;

    new_dynarec_mark_deferral_budget--;
    return 1;
}

int
new_dynarec_should_remove_aborted_mark_block(int mark_block_initialized, int unexpected_abrt)
{
    return mark_block_initialized && unexpected_abrt;
}

int
new_dynarec_has_direct_pmaddwd_recompile(void)
{
    return 1;
}

int
new_dynarec_has_direct_3dnow_recompile(void)
{
    return 1;
}

int
new_dynarec_has_direct_3dnow_opcode_recompile(uint8_t opcode)
{
    switch (opcode) {
        case 0x0c:
        case 0x0d:
        case 0x1c:
        case 0x1d:
        case 0x8a:
        case 0x8e:
        case 0x90:
        case 0x94:
        case 0x96:
        case 0x97:
        case 0x9a:
        case 0x9e:
        case 0xa0:
        case 0xa4:
        case 0xa6:
        case 0xa7:
        case 0xaa:
        case 0xae:
        case 0xb0:
        case 0xb4:
        case 0xb6:
        case 0xb7:
        case 0xbb:
        case 0xbf:
            return 1;
        default:
            return 0;
    }
}

int
new_dynarec_direct_3dnow_opcode_count(void)
{
    return 24;
}

int
new_dynarec_has_direct_0f_opcode_recompile(uint8_t opcode)
{
    switch (opcode) {
        case 0x90:
        case 0x91:
        case 0x92:
        case 0x93:
        case 0x94:
        case 0x95:
        case 0x96:
        case 0x97:
        case 0x98:
        case 0x99:
        case 0x9a:
        case 0x9b:
        case 0x9c:
        case 0x9d:
        case 0x9e:
        case 0x9f:
        case 0xc8:
        case 0xc9:
        case 0xca:
        case 0xcb:
        case 0xcc:
        case 0xcd:
        case 0xce:
        case 0xcf:
        case 0xbc:
        case 0xbd:
            return 1;
        default:
            return 0;
    }
}

int
new_dynarec_direct_0f_setcc_opcode_count(void)
{
    return 16;
}

int
new_dynarec_direct_0f_bswap_opcode_count(void)
{
    return 8;
}

int
new_dynarec_direct_0f_bitscan_opcode_count(void)
{
    return 2;
}

int
new_dynarec_has_direct_base_opcode_recompile(uint8_t opcode)
{
    switch (opcode) {
        case 0xf6:
        case 0x9a:
        case 0xf7:
        case 0xff:
        case 0xca:
        case 0xcb:
        case 0x69:
        case 0x6b:
        case 0xa4:
        case 0xa5:
        case 0xa6:
        case 0xa7:
        case 0xae:
        case 0xaf:
        case 0x9e:
        case 0x9f:
        case 0x9d:
        case 0xc8:
        case 0xaa:
        case 0xab:
        case 0xac:
        case 0xad:
            return 1;
        default:
            return 0;
    }
}

int
new_dynarec_direct_base_string_opcode_count(void)
{
    return 10;
}

void
new_dynarec_note_allocator_pressure_empty_purgable_list(void)
{
    new_dynarec_stats.allocator_pressure_empty_purgable_list++;
    if (!new_dynarec_mark_deferral_budget)
        new_dynarec_mark_deferral_budget = NEW_DYNAREC_RANDOM_EVICTION_MARK_DEFERRAL_BUDGET;
}

void
new_dynarec_note_block_marked(uint32_t pc, uint32_t phys, uint32_t detail)
{
    new_dynarec_stats.blocks_marked++;
    new_dynarec_emit_trace(NEW_DYNAREC_TRACE_BLOCK_MARKED, pc, phys, detail, 0);
}

void
new_dynarec_note_deferred_block_mark(uint32_t pc, uint32_t phys)
{
    new_dynarec_stats.deferred_block_marks++;
    new_dynarec_emit_trace(NEW_DYNAREC_TRACE_BLOCK_MARK_DEFERRED, pc, phys, 0, 0);
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
    new_dynarec_mark_deferral_budget = NEW_DYNAREC_RANDOM_EVICTION_MARK_DEFERRAL_BUDGET;
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
