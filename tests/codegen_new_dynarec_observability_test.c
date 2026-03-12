#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "cpu.h"
#include "codegen_public.h"

typedef struct trace_capture_t {
    int                       event_count;
    new_dynarec_trace_event_t last_event;
} trace_capture_t;

static void
capture_trace(const new_dynarec_trace_event_t *event, void *opaque)
{
    trace_capture_t *capture = (trace_capture_t *) opaque;

    capture->event_count++;
    capture->last_event = *event;
}

static void
assert_zeroed(const new_dynarec_stats_t *stats)
{
    assert(stats->blocks_marked == 0);
    assert(stats->deferred_block_marks == 0);
    assert(stats->blocks_recompiled == 0);
    assert(stats->direct_recompiled_instructions == 0);
    assert(stats->helper_call_fallbacks == 0);
    assert(stats->direct_opcode_table_null_hits == 0);
    assert(stats->direct_handler_bailouts == 0);
    assert(stats->invalidations == 0);
    assert(stats->byte_mask_transitions == 0);
    assert(stats->no_immediates_transitions == 0);
    assert(stats->allocator_pressure_events == 0);
    assert(stats->allocator_pressure_empty_purgable_list == 0);
    assert(stats->purgable_page_enqueues == 0);
    assert(stats->purgable_page_enqueues_write == 0);
    assert(stats->purgable_page_enqueues_codegen == 0);
    assert(stats->purgable_page_enqueues_bulk_dirty == 0);
    assert(stats->purgable_page_dequeues_stale == 0);
    assert(stats->purgable_page_dequeues_flush == 0);
    assert(stats->purgable_page_dequeues_no_blocks == 0);
    assert(stats->purgable_flush_attempts == 0);
    assert(stats->purgable_flush_successes == 0);
    assert(stats->purgable_flush_no_overlap == 0);
    assert(stats->purgable_flush_no_free_block == 0);
    assert(stats->purgable_flush_no_blocks == 0);
    assert(stats->purgable_flush_dirty_list_reuses == 0);
    assert(stats->random_evictions == 0);
    assert(stats->verify_samples == 0);
    assert(stats->verify_direct_samples == 0);
    assert(stats->verify_helper_table_null_samples == 0);
    assert(stats->verify_helper_bailout_samples == 0);
}

static void
assert_no_3dnow_hit_summary(uint8_t opcode)
{
    char summary[256];

    assert(new_dynarec_format_3dnow_hit_summary(summary, sizeof(summary), opcode) == 0);
}

static void
assert_no_gap_family_summary(void)
{
    char summary[256];

    assert(new_dynarec_format_gap_family_summary(summary, sizeof(summary)) == 0);
}

static void
assert_no_fallback_family_summary(void)
{
    char summary[256];

    assert(new_dynarec_format_fallback_family_summary(summary, sizeof(summary)) == 0);
}

static void
assert_no_base_fallback_summary(uint8_t opcode)
{
    char summary[256];

    assert(new_dynarec_format_base_fallback_summary(summary, sizeof(summary), opcode) == 0);
}

static void
assert_no_0f_fallback_summary(uint8_t opcode)
{
    char summary[256];

    assert(new_dynarec_format_0f_fallback_summary(summary, sizeof(summary), opcode) == 0);
}

static void
assert_no_rep_fallback_summary(uint8_t opcode)
{
    char summary[256];

    assert(new_dynarec_format_rep_fallback_summary(summary, sizeof(summary), opcode) == 0);
}

static void
assert_no_rep_scas_debug_summary(void)
{
    char summary[256];

    assert(new_dynarec_format_rep_scas_debug_summary(summary, sizeof(summary)) == 0);
}

static void
assert_no_rep_scas_debug_site_summary(uint32_t index)
{
    char summary[256];

    assert(new_dynarec_format_rep_scas_debug_site_summary(summary, sizeof(summary), index) == 0);
}

int
main(void)
{
    char                summary[2048];
    new_dynarec_stats_t snapshot;
    trace_capture_t     trace_capture;
    new_dynarec_verify_config_t verify_config;

    memset(&trace_capture, 0, sizeof(trace_capture));

    new_dynarec_stats_reset();
    new_dynarec_stats_snapshot(&snapshot);
    assert_zeroed(&snapshot);
    assert_no_3dnow_hit_summary(0xae);
    assert_no_3dnow_hit_summary(0xbf);
    assert_no_gap_family_summary();
    assert_no_fallback_family_summary();
    assert_no_base_fallback_summary(0x90);
    assert_no_base_fallback_summary(0xec);
    assert_no_0f_fallback_summary(0x6f);
    assert_no_0f_fallback_summary(0xef);
    assert_no_rep_fallback_summary(0xa4);
    assert_no_rep_fallback_summary(0xaf);
    assert(new_dynarec_format_d0d3_compare_summary(summary, sizeof(summary)) == 0);
    assert_no_rep_scas_debug_summary();
    assert_no_rep_scas_debug_site_summary(0);
    setenv("86BOX_NEW_DYNAREC_DEBUG_REP_SCAS", "1", 1);
    setenv("86BOX_NEW_DYNAREC_DEBUG_REP_SCAS_BUDGET", "2", 1);
#ifdef NEW_DYNAREC_DEVTOOLS
    assert(new_dynarec_rep_scas_debug_enabled_for_site(0x405000u, 0xaeu) == 1);
    assert(new_dynarec_rep_scas_debug_enabled_for_site(0x406000u, 0xafu) == 1);
    assert(new_dynarec_rep_scas_debug_enabled_for_site(0x407000u, 0xaeu) == 0);
#else
    assert(new_dynarec_rep_scas_debug_enabled_for_site(0x405000u, 0xaeu) == 0);
    assert(new_dynarec_rep_scas_debug_enabled_for_site(0x406000u, 0xafu) == 0);
    assert(new_dynarec_rep_scas_debug_enabled_for_site(0x407000u, 0xaeu) == 0);
#endif
    unsetenv("86BOX_NEW_DYNAREC_DEBUG_REP_SCAS");
    unsetenv("86BOX_NEW_DYNAREC_DEBUG_REP_SCAS_BUDGET");

    new_dynarec_set_trace_hook(capture_trace, &trace_capture);

    new_dynarec_note_block_marked(0x1000, 0x2000, 7);
#ifdef NEW_DYNAREC_DEVTOOLS
    assert(trace_capture.event_count == 1);
    assert(trace_capture.last_event.kind == NEW_DYNAREC_TRACE_BLOCK_MARKED);
    assert(trace_capture.last_event.pc == 0x1000);
    assert(trace_capture.last_event.phys == 0x2000);
    assert(trace_capture.last_event.detail == 7);
#else
    assert(trace_capture.event_count == 0);
#endif

    assert(!new_dynarec_should_defer_marking_new_block());
    new_dynarec_note_block_recompiled(0x1010, 0x2010, 5);
    new_dynarec_note_direct_recompiled_instruction();
    new_dynarec_note_direct_recompiled_instruction();
    new_dynarec_note_helper_call_fallback(NEW_DYNAREC_HELPER_FALLBACK_DIRECT_TABLE_NULL);
    new_dynarec_note_helper_call_fallback(NEW_DYNAREC_HELPER_FALLBACK_DIRECT_HANDLER_BAILOUT);
    new_dynarec_note_block_invalidated(0x1020, 0x2020, 0x44);
    new_dynarec_note_block_became_byte_mask(0x1030, 0x2030, 0x20);
    new_dynarec_note_block_became_no_immediates(0x1040, 0x2040, 0xa0);
    new_dynarec_note_allocator_pressure(0x55);
    new_dynarec_note_allocator_pressure_empty_purgable_list();
    new_dynarec_note_purgable_page_enqueued();
    new_dynarec_note_purgable_page_enqueued_write();
    new_dynarec_note_purgable_page_enqueued_codegen();
    new_dynarec_note_purgable_page_enqueued_bulk_dirty();
    new_dynarec_note_purgable_page_dequeued_stale();
    new_dynarec_note_purgable_page_dequeued_flush();
    new_dynarec_note_purgable_page_dequeued_no_blocks();
    new_dynarec_note_purgable_flush_attempt();
    new_dynarec_note_purgable_flush_success();
    new_dynarec_note_purgable_flush_no_overlap();
    new_dynarec_note_purgable_flush_no_free_block();
    new_dynarec_note_purgable_flush_no_blocks();
    new_dynarec_note_purgable_flush_dirty_list_reuse();
    new_dynarec_note_random_eviction(0x1050, 0x2050, 0x08);
    assert(new_dynarec_should_defer_marking_new_block());
    new_dynarec_note_deferred_block_mark(0x1054, 0x2054);
#ifdef NEW_DYNAREC_DEVTOOLS
    assert(trace_capture.last_event.kind == NEW_DYNAREC_TRACE_BLOCK_MARK_DEFERRED);
    assert(trace_capture.last_event.pc == 0x1054);
    assert(trace_capture.last_event.phys == 0x2054);
#else
    assert(trace_capture.event_count == 0);
#endif
    memset(&verify_config, 0, sizeof(verify_config));
    verify_config.match_pc     = 1;
    verify_config.match_opcode = 1;
    verify_config.pc           = 0x1060;
    verify_config.opcode       = 0x90;
    verify_config.budget       = 2;
    new_dynarec_set_verify_config(&verify_config);
    memset(&snapshot, 0, sizeof(snapshot));
    new_dynarec_get_verify_config(&verify_config);
#ifdef NEW_DYNAREC_DEVTOOLS
    assert(verify_config.match_pc == 1);
    assert(verify_config.match_opcode == 1);
    assert(verify_config.pc == 0x1060);
    assert(verify_config.opcode == 0x90);
    assert(verify_config.budget == 2);
#else
    assert(verify_config.match_pc == 0);
    assert(verify_config.match_opcode == 0);
    assert(verify_config.pc == 0);
    assert(verify_config.opcode == 0);
    assert(verify_config.budget == 0);
#endif
    const int trace_count_before_verify = trace_capture.event_count;
#ifdef NEW_DYNAREC_DEVTOOLS
    assert(!new_dynarec_note_verify_sample(0x1060, 0x91, NEW_DYNAREC_VERIFY_DIRECT));
    assert(new_dynarec_note_verify_sample(0x1060, 0x90, NEW_DYNAREC_VERIFY_DIRECT));
    assert(trace_capture.event_count == trace_count_before_verify + 1);
    assert(trace_capture.last_event.kind == NEW_DYNAREC_TRACE_VERIFY_SAMPLE);
    assert(trace_capture.last_event.pc == 0x1060);
    assert(trace_capture.last_event.detail == 0x90);
    assert(trace_capture.last_event.flags == NEW_DYNAREC_VERIFY_DIRECT);
    assert(new_dynarec_note_verify_sample(0x1060, 0x90, NEW_DYNAREC_VERIFY_HELPER_BAILOUT));
    assert(!new_dynarec_note_verify_sample(0x1060, 0x90, NEW_DYNAREC_VERIFY_HELPER_TABLE_NULL));
#else
    assert(!new_dynarec_note_verify_sample(0x1060, 0x91, NEW_DYNAREC_VERIFY_DIRECT));
    assert(!new_dynarec_note_verify_sample(0x1060, 0x90, NEW_DYNAREC_VERIFY_DIRECT));
    assert(trace_capture.event_count == trace_count_before_verify);
    assert(!new_dynarec_note_verify_sample(0x1060, 0x90, NEW_DYNAREC_VERIFY_HELPER_BAILOUT));
    assert(!new_dynarec_note_verify_sample(0x1060, 0x90, NEW_DYNAREC_VERIFY_HELPER_TABLE_NULL));
#endif
    new_dynarec_note_3dnow_opcode_hit(0xae, NEW_DYNAREC_VERIFY_DIRECT);
    new_dynarec_note_3dnow_opcode_hit(0xae, NEW_DYNAREC_VERIFY_DIRECT);
    new_dynarec_note_3dnow_opcode_hit(0xae, NEW_DYNAREC_VERIFY_HELPER_BAILOUT);
    new_dynarec_note_3dnow_opcode_hit(0xbf, NEW_DYNAREC_VERIFY_HELPER_TABLE_NULL);
    new_dynarec_note_gap_family_hit(NEW_DYNAREC_GAP_FAMILY_REP);
    new_dynarec_note_gap_family_hit(NEW_DYNAREC_GAP_FAMILY_REP);
    new_dynarec_note_gap_family_hit(NEW_DYNAREC_GAP_FAMILY_SOFTFLOAT_X87);
    new_dynarec_note_fallback_family_hit(NEW_DYNAREC_FALLBACK_FAMILY_BASE);
    new_dynarec_note_fallback_family_hit(NEW_DYNAREC_FALLBACK_FAMILY_0F);
    new_dynarec_note_fallback_family_hit(NEW_DYNAREC_FALLBACK_FAMILY_X87);
    new_dynarec_note_fallback_family_hit(NEW_DYNAREC_FALLBACK_FAMILY_REP);
    new_dynarec_note_fallback_family_hit(NEW_DYNAREC_FALLBACK_FAMILY_3DNOW);
    new_dynarec_note_fallback_family_hit(NEW_DYNAREC_FALLBACK_FAMILY_3DNOW);
    new_dynarec_note_base_fallback_opcode_hit(0x90, NEW_DYNAREC_VERIFY_HELPER_TABLE_NULL);
    new_dynarec_note_base_fallback_opcode_hit(0x90, NEW_DYNAREC_VERIFY_HELPER_BAILOUT);
    new_dynarec_note_base_fallback_opcode_hit(0xec, NEW_DYNAREC_VERIFY_HELPER_TABLE_NULL);
    new_dynarec_note_0f_fallback_opcode_hit(0x6f, NEW_DYNAREC_VERIFY_HELPER_TABLE_NULL);
    new_dynarec_note_0f_fallback_opcode_hit(0x6f, NEW_DYNAREC_VERIFY_HELPER_BAILOUT);
    new_dynarec_note_0f_fallback_opcode_hit(0xef, NEW_DYNAREC_VERIFY_HELPER_TABLE_NULL);
    new_dynarec_note_rep_fallback_opcode_hit(0xa4, NEW_DYNAREC_VERIFY_HELPER_TABLE_NULL);
    new_dynarec_note_rep_fallback_opcode_hit(0xa4, NEW_DYNAREC_VERIFY_HELPER_BAILOUT);
    new_dynarec_note_rep_fallback_opcode_hit(0xaf, NEW_DYNAREC_VERIFY_HELPER_TABLE_NULL);
    assert(new_dynarec_classify_post_purge_state(1, BLOCK_INVALID) == NEW_DYNAREC_POST_PURGE_HAVE_FREE_BLOCK);
    assert(new_dynarec_classify_post_purge_state(0, 7) == NEW_DYNAREC_POST_PURGE_RETRY_DIRTY_LIST);
    assert(new_dynarec_classify_post_purge_state(0, BLOCK_INVALID) == NEW_DYNAREC_POST_PURGE_RANDOM_EVICTION);

    new_dynarec_stats_snapshot(&snapshot);
    assert(snapshot.blocks_marked == 1);
    assert(snapshot.deferred_block_marks == 1);
    assert(snapshot.blocks_recompiled == 1);
    assert(snapshot.direct_recompiled_instructions == 2);
    assert(snapshot.helper_call_fallbacks == 2);
    assert(snapshot.direct_opcode_table_null_hits == 1);
    assert(snapshot.direct_handler_bailouts == 1);
    assert(snapshot.invalidations == 1);
    assert(snapshot.byte_mask_transitions == 1);
    assert(snapshot.no_immediates_transitions == 1);
    assert(snapshot.allocator_pressure_events == 1);
    assert(snapshot.allocator_pressure_empty_purgable_list == 1);
    assert(snapshot.purgable_page_enqueues == 1);
    assert(snapshot.purgable_page_enqueues_write == 1);
    assert(snapshot.purgable_page_enqueues_codegen == 1);
    assert(snapshot.purgable_page_enqueues_bulk_dirty == 1);
    assert(snapshot.purgable_page_dequeues_stale == 1);
    assert(snapshot.purgable_page_dequeues_flush == 1);
    assert(snapshot.purgable_page_dequeues_no_blocks == 1);
    assert(snapshot.purgable_flush_attempts == 1);
    assert(snapshot.purgable_flush_successes == 1);
    assert(snapshot.purgable_flush_no_overlap == 1);
    assert(snapshot.purgable_flush_no_free_block == 1);
    assert(snapshot.purgable_flush_no_blocks == 1);
    assert(snapshot.purgable_flush_dirty_list_reuses == 1);
    assert(snapshot.random_evictions == 1);
#ifdef NEW_DYNAREC_DEVTOOLS
    assert(snapshot.verify_samples == 2);
    assert(snapshot.verify_direct_samples == 1);
    assert(snapshot.verify_helper_table_null_samples == 0);
    assert(snapshot.verify_helper_bailout_samples == 1);
#else
    assert(snapshot.verify_samples == 0);
    assert(snapshot.verify_direct_samples == 0);
    assert(snapshot.verify_helper_table_null_samples == 0);
    assert(snapshot.verify_helper_bailout_samples == 0);
#endif

    assert(new_dynarec_format_stats_summary(summary, sizeof(summary), &snapshot) > 0);
    assert(strstr(summary, "blocks_marked=1"));
    assert(strstr(summary, "deferred_block_marks=1"));
    assert(strstr(summary, "blocks_recompiled=1"));
    assert(strstr(summary, "allocator_pressure_empty_purgable_list=1"));
    assert(strstr(summary, "purgable_page_enqueues=1"));
    assert(strstr(summary, "purgable_page_enqueues_write=1"));
    assert(strstr(summary, "purgable_page_enqueues_codegen=1"));
    assert(strstr(summary, "purgable_page_enqueues_bulk_dirty=1"));
    assert(strstr(summary, "purgable_page_dequeues_stale=1"));
    assert(strstr(summary, "purgable_page_dequeues_flush=1"));
    assert(strstr(summary, "purgable_page_dequeues_no_blocks=1"));
    assert(strstr(summary, "purgable_flush_attempts=1"));
    assert(strstr(summary, "purgable_flush_no_overlap=1"));
    assert(strstr(summary, "purgable_flush_no_free_block=1"));
    assert(strstr(summary, "purgable_flush_no_blocks=1"));
    assert(strstr(summary, "purgable_flush_dirty_list_reuses=1"));
    assert(strstr(summary, "random_evictions=1"));
#ifdef NEW_DYNAREC_DEVTOOLS
    assert(strstr(summary, "verify_samples=2"));
    assert(strstr(summary, "verify_direct_samples=1"));
    assert(strstr(summary, "verify_helper_bailout_samples=1"));
#else
    assert(strstr(summary, "verify_samples=0"));
    assert(strstr(summary, "verify_direct_samples=0"));
    assert(strstr(summary, "verify_helper_bailout_samples=0"));
#endif

    memset(summary, 0, sizeof(summary));
#ifdef NEW_DYNAREC_DEVTOOLS
    assert(new_dynarec_format_3dnow_hit_summary(summary, sizeof(summary), 0xae) > 0);
    assert(strstr(summary, "opcode=0xae"));
    assert(strstr(summary, "direct=2"));
    assert(strstr(summary, "helper_table_null=0"));
    assert(strstr(summary, "helper_bailout=1"));

    memset(summary, 0, sizeof(summary));
    assert(new_dynarec_format_3dnow_hit_summary(summary, sizeof(summary), 0xbf) > 0);
    assert(strstr(summary, "opcode=0xbf"));
    assert(strstr(summary, "direct=0"));
    assert(strstr(summary, "helper_table_null=1"));
    assert(strstr(summary, "helper_bailout=0"));
    assert_no_3dnow_hit_summary(0xb4);

    memset(summary, 0, sizeof(summary));
    assert(new_dynarec_format_gap_family_summary(summary, sizeof(summary)) > 0);
    assert(strstr(summary, "rep=2"));
    assert(strstr(summary, "softfloat_x87=1"));

    memset(summary, 0, sizeof(summary));
    assert(new_dynarec_format_fallback_family_summary(summary, sizeof(summary)) > 0);
    assert(strstr(summary, "base=1"));
    assert(strstr(summary, "0f=1"));
    assert(strstr(summary, "x87=1"));
    assert(strstr(summary, "rep=1"));
    assert(strstr(summary, "3dnow=2"));

    memset(summary, 0, sizeof(summary));
    assert(new_dynarec_format_base_fallback_summary(summary, sizeof(summary), 0x90) > 0);
    assert(strstr(summary, "opcode=0x90"));
    assert(strstr(summary, "helper_table_null=1"));
    assert(strstr(summary, "helper_bailout=1"));

    memset(summary, 0, sizeof(summary));
    assert(new_dynarec_format_base_fallback_summary(summary, sizeof(summary), 0xec) > 0);
    assert(strstr(summary, "opcode=0xec"));
    assert(strstr(summary, "helper_table_null=1"));
    assert(strstr(summary, "helper_bailout=0"));
    assert_no_base_fallback_summary(0x91);

    memset(summary, 0, sizeof(summary));
    assert(new_dynarec_format_0f_fallback_summary(summary, sizeof(summary), 0x6f) > 0);
    assert(strstr(summary, "opcode=0x6f"));
    assert(strstr(summary, "helper_table_null=1"));
    assert(strstr(summary, "helper_bailout=1"));

    memset(summary, 0, sizeof(summary));
    assert(new_dynarec_format_0f_fallback_summary(summary, sizeof(summary), 0xef) > 0);
    assert(strstr(summary, "opcode=0xef"));
    assert(strstr(summary, "helper_table_null=1"));
    assert(strstr(summary, "helper_bailout=0"));
    assert_no_0f_fallback_summary(0x70);

    memset(summary, 0, sizeof(summary));
    assert(new_dynarec_format_rep_fallback_summary(summary, sizeof(summary), 0xa4) > 0);
    assert(strstr(summary, "opcode=0xa4"));
    assert(strstr(summary, "helper_table_null=1"));
    assert(strstr(summary, "helper_bailout=1"));

    memset(summary, 0, sizeof(summary));
    assert(new_dynarec_format_rep_fallback_summary(summary, sizeof(summary), 0xaf) > 0);
    assert(strstr(summary, "opcode=0xaf"));
    assert(strstr(summary, "helper_table_null=1"));
    assert(strstr(summary, "helper_bailout=0"));
    assert_no_rep_fallback_summary(0xa5);

    memset(summary, 0, sizeof(summary));
    new_dynarec_note_rep_scas_debug_site(0x403000u, 0xaeu);
    assert(new_dynarec_format_rep_scas_debug_summary(summary, sizeof(summary)) > 0);
    assert(strstr(summary, "attempts=1"));
    assert(strstr(summary, "sites=1"));
    assert(strstr(summary, "site_log_overflow=0"));
    assert(strstr(summary, "last_pc=0x00403000"));
    assert(strstr(summary, "last_opcode=0xae"));
    memset(summary, 0, sizeof(summary));
    assert(new_dynarec_format_rep_scas_debug_site_summary(summary, sizeof(summary), 0) > 0);
    assert(strstr(summary, "index=0"));
    assert(strstr(summary, "pc=0x00403000"));
    assert(strstr(summary, "opcode=0xae"));
    assert(strstr(summary, "attempts=1"));
    assert_no_rep_scas_debug_site_summary(1);

    memset(summary, 0, sizeof(summary));
    new_dynarec_note_rep_scas_debug_site(0x403000u, 0xaeu);
    new_dynarec_note_rep_scas_debug_site(0x404000u, 0xafu);
    assert(new_dynarec_format_rep_scas_debug_summary(summary, sizeof(summary)) > 0);
    assert(strstr(summary, "attempts=3"));
    assert(strstr(summary, "sites=2"));
    assert(strstr(summary, "last_pc=0x00404000"));
    assert(strstr(summary, "last_opcode=0xaf"));
    memset(summary, 0, sizeof(summary));
    assert(new_dynarec_format_rep_scas_debug_site_summary(summary, sizeof(summary), 0) > 0);
    assert(strstr(summary, "pc=0x00403000"));
    assert(strstr(summary, "opcode=0xae"));
    assert(strstr(summary, "attempts=2"));
    memset(summary, 0, sizeof(summary));
    assert(new_dynarec_format_rep_scas_debug_site_summary(summary, sizeof(summary), 1) > 0);
    assert(strstr(summary, "pc=0x00404000"));
    assert(strstr(summary, "opcode=0xaf"));
    assert(strstr(summary, "attempts=1"));

    memset(summary, 0, sizeof(summary));
    new_dynarec_note_d0d3_compare_site(0x401000u, new_dynarec_pack_d0d3_rotate_compare(0xd0u, 0x10u, 8u, 0u, 1u, C_FLAG));
    assert(new_dynarec_format_d0d3_compare_summary(summary, sizeof(summary)) > 0);
    assert(strstr(summary, "attempts=0"));
    assert(strstr(summary, "sites=1"));
    assert(strstr(summary, "site_log_overflow=0"));
    assert(strstr(summary, "last_pc=0x00401000"));
    assert(strstr(summary, "last_opcode=0xd0"));
    assert(strstr(summary, "last_form=reg"));
    assert(new_dynarec_format_d0d3_compare_sample_summary(summary, sizeof(summary), 0) == 0);
    assert(new_dynarec_format_d0d3_compare_bailout_summary(summary, sizeof(summary), 0) == 0);

    memset(summary, 0, sizeof(summary));
    new_dynarec_note_d0d3_compare_site(0x401000u, new_dynarec_pack_d0d3_rotate_compare(0xd0u, 0x10u, 8u, 0u, 1u, C_FLAG));
    assert(new_dynarec_format_d0d3_compare_summary(summary, sizeof(summary)) > 0);
    assert(strstr(summary, "sites=1"));
    memset(summary, 0, sizeof(summary));
    assert(new_dynarec_format_d0d3_compare_site_summary(summary, sizeof(summary), 0) > 0);
    assert(strstr(summary, "index=0"));
    assert(strstr(summary, "pc=0x00401000"));
    assert(strstr(summary, "opcode=0xd0"));
    assert(strstr(summary, "subgroup=0x10"));
    assert(strstr(summary, "width=8"));
    assert(strstr(summary, "form=reg"));
    assert(strstr(summary, "attempts=0"));
    assert(strstr(summary, "mismatches=0"));
    assert(strstr(summary, "no_block_ins_bailouts=0"));
    assert(strstr(summary, "zero_count_bailouts=0"));
    assert(strstr(summary, "count_zero=0"));
    assert(strstr(summary, "count_one=0"));
    assert(strstr(summary, "count_multi=0"));
    assert(strstr(summary, "cf_set=0"));
    assert(strstr(summary, "operand_zero=0"));
    assert(strstr(summary, "result_changed=0"));
    assert(strstr(summary, "flags_nonzero=0"));
    assert(new_dynarec_format_d0d3_compare_site_summary(summary, sizeof(summary), 1) == 0);

    memset(summary, 0, sizeof(summary));
    new_dynarec_note_d0d3_compare_site(0x402000u, new_dynarec_pack_d0d3_rotate_compare(0xd3u, 0x18u, 32u, 1u, 4u, 0u));
    assert(new_dynarec_format_d0d3_compare_site_summary(summary, sizeof(summary), 1) > 0);
    assert(strstr(summary, "index=1"));
    assert(strstr(summary, "pc=0x00402000"));
    assert(strstr(summary, "opcode=0xd3"));
    assert(strstr(summary, "subgroup=0x18"));
    assert(strstr(summary, "width=32"));
    assert(strstr(summary, "form=mem"));
    assert(strstr(summary, "attempts=0"));
    assert(strstr(summary, "mismatches=0"));
    assert(strstr(summary, "no_block_ins_bailouts=0"));
    assert(strstr(summary, "zero_count_bailouts=0"));
    assert(strstr(summary, "count_zero=0"));
    assert(strstr(summary, "count_one=0"));
    assert(strstr(summary, "count_multi=0"));
    assert(strstr(summary, "cf_set=0"));
    assert(strstr(summary, "operand_zero=0"));
    assert(strstr(summary, "result_changed=0"));
    assert(strstr(summary, "flags_nonzero=0"));

    new_dynarec_note_d0d3_compare_site_bailout(0x402000u,
                                               new_dynarec_pack_d0d3_rotate_compare(0xd3u, 0x18u, 32u, 1u, 4u, 0u),
                                               NEW_DYNAREC_D0D3_COMPARE_BAILOUT_NO_BLOCK_INS);
    memset(summary, 0, sizeof(summary));
    assert(new_dynarec_format_d0d3_compare_site_summary(summary, sizeof(summary), 1) > 0);
    assert(strstr(summary, "attempts=0"));
    assert(strstr(summary, "no_block_ins_bailouts=1"));
    assert(strstr(summary, "zero_count_bailouts=0"));
    memset(summary, 0, sizeof(summary));
    assert(new_dynarec_format_d0d3_compare_bailout_summary(summary, sizeof(summary), 0) > 0);
    assert(strstr(summary, "index=0"));
    assert(strstr(summary, "pc=0x00402000"));
    assert(strstr(summary, "reason=no_block_ins"));
    assert(strstr(summary, "count=4"));
    assert(strstr(summary, "cf=0"));

    new_dynarec_note_d0d3_compare_site_zero_count_bailout(0x402000u,
                                                          new_dynarec_pack_d0d3_rotate_compare(0xd3u, 0x18u, 32u, 1u, 4u, 0u));
    memset(summary, 0, sizeof(summary));
    assert(new_dynarec_format_d0d3_compare_site_summary(summary, sizeof(summary), 1) > 0);
    assert(strstr(summary, "attempts=0"));
    assert(strstr(summary, "no_block_ins_bailouts=1"));
    assert(strstr(summary, "zero_count_bailouts=1"));
    memset(summary, 0, sizeof(summary));
    assert(new_dynarec_format_d0d3_compare_bailout_summary(summary, sizeof(summary), 1) > 0);
    assert(strstr(summary, "index=1"));
    assert(strstr(summary, "pc=0x00402000"));
    assert(strstr(summary, "reason=zero_count"));
    assert(strstr(summary, "count=4"));
    assert(strstr(summary, "cf=0"));

    memset(summary, 0, sizeof(summary));
    new_dynarec_note_d0d3_compare_site(0x401000u, new_dynarec_pack_d0d3_rotate_compare(0xd0u, 0x10u, 8u, 0u, 1u, C_FLAG));
    new_dynarec_note_d0d3_compare_attempt(new_dynarec_pack_d0d3_rotate_compare(0xd0u, 0x10u, 8u, 0u, 1u, C_FLAG),
                                          0x80u, 0x01u, 0u, 0x01u, 0u);
    assert(new_dynarec_format_d0d3_compare_summary(summary, sizeof(summary)) > 0);
    assert(strstr(summary, "attempts=1"));
    assert(strstr(summary, "matches=1"));
    assert(strstr(summary, "mismatches=0"));
    assert(strstr(summary, "sites=2"));
    assert(strstr(summary, "last_pc=0x00401000"));
    assert(strstr(summary, "last_opcode=0xd0"));
    assert(strstr(summary, "last_width=8"));
    assert(strstr(summary, "last_form=reg"));
    memset(summary, 0, sizeof(summary));
    assert(new_dynarec_format_d0d3_compare_sample_summary(summary, sizeof(summary), 0) > 0);
    assert(strstr(summary, "index=0"));
    assert(strstr(summary, "pc=0x00401000"));
    assert(strstr(summary, "opcode=0xd0"));
    assert(strstr(summary, "operand=0x00000080"));
    assert(strstr(summary, "count=1"));
    assert(strstr(summary, "cf=1"));
    assert(strstr(summary, "direct=0x00000001/0x00000000"));
    assert(strstr(summary, "helper=0x00000001/0x00000000"));
    memset(summary, 0, sizeof(summary));
    assert(new_dynarec_format_d0d3_compare_site_summary(summary, sizeof(summary), 0) > 0);
    assert(strstr(summary, "attempts=1"));
    assert(strstr(summary, "mismatches=0"));
    assert(strstr(summary, "no_block_ins_bailouts=0"));
    assert(strstr(summary, "zero_count_bailouts=0"));
    assert(strstr(summary, "count_one=1"));
    assert(strstr(summary, "count_zero=0"));
    assert(strstr(summary, "count_multi=0"));
    assert(strstr(summary, "cf_set=1"));
    assert(strstr(summary, "operand_zero=0"));
    assert(strstr(summary, "result_changed=1"));
    assert(strstr(summary, "flags_nonzero=0"));
    memset(summary, 0, sizeof(summary));
    assert(new_dynarec_format_d0d3_compare_site_summary(summary, sizeof(summary), 1) > 0);
    assert(strstr(summary, "attempts=0"));
    assert(strstr(summary, "mismatches=0"));
    assert(strstr(summary, "no_block_ins_bailouts=1"));
    assert(strstr(summary, "zero_count_bailouts=1"));
    assert(strstr(summary, "count_zero=0"));
    assert(strstr(summary, "count_one=0"));
    assert(strstr(summary, "count_multi=0"));
    assert(strstr(summary, "cf_set=0"));
    assert(strstr(summary, "operand_zero=0"));
    assert(strstr(summary, "result_changed=0"));
    assert(strstr(summary, "flags_nonzero=0"));

    memset(summary, 0, sizeof(summary));
    new_dynarec_note_d0d3_compare_site(0x402000u, new_dynarec_pack_d0d3_rotate_compare(0xd3u, 0x18u, 32u, 1u, 4u, 0u));
    new_dynarec_note_d0d3_compare_attempt(new_dynarec_pack_d0d3_rotate_compare(0xd3u, 0x18u, 32u, 1u, 4u, 0u),
                                          0x80000001u, 0x18000000u, (C_FLAG | V_FLAG), 0x10000000u, C_FLAG);
    assert(new_dynarec_format_d0d3_compare_summary(summary, sizeof(summary)) > 0);
    assert(strstr(summary, "attempts=2"));
    assert(strstr(summary, "matches=1"));
    assert(strstr(summary, "mismatches=1"));
    assert(strstr(summary, "last_opcode=0xd3"));
    assert(strstr(summary, "last_width=32"));
    assert(strstr(summary, "last_form=mem"));
    assert(strstr(summary, "last_count=4"));
    memset(summary, 0, sizeof(summary));
    assert(new_dynarec_format_d0d3_compare_site_summary(summary, sizeof(summary), 1) > 0);
    assert(strstr(summary, "attempts=1"));
    assert(strstr(summary, "mismatches=1"));
    assert(strstr(summary, "no_block_ins_bailouts=1"));
    assert(strstr(summary, "zero_count_bailouts=1"));
    assert(strstr(summary, "count_zero=0"));
    assert(strstr(summary, "count_one=0"));
    assert(strstr(summary, "count_multi=1"));
    assert(strstr(summary, "cf_set=0"));
    assert(strstr(summary, "operand_zero=0"));
    assert(strstr(summary, "result_changed=1"));
    assert(strstr(summary, "flags_nonzero=1"));
    memset(summary, 0, sizeof(summary));
    assert(new_dynarec_format_d0d3_compare_sample_summary(summary, sizeof(summary), 1) > 0);
    assert(strstr(summary, "index=1"));
    assert(strstr(summary, "pc=0x00402000"));
    assert(strstr(summary, "opcode=0xd3"));
    assert(strstr(summary, "subgroup=0x18"));
    assert(strstr(summary, "operand=0x80000001"));
    assert(strstr(summary, "count=4"));
    assert(strstr(summary, "cf=0"));
    assert(strstr(summary, "direct=0x18000000/0x00000801"));
    assert(strstr(summary, "helper=0x10000000/0x00000001"));
#else
    assert_no_3dnow_hit_summary(0xae);
    assert_no_3dnow_hit_summary(0xbf);
    assert_no_gap_family_summary();
    assert_no_fallback_family_summary();
    assert_no_base_fallback_summary(0x90);
    assert_no_base_fallback_summary(0xec);
    assert_no_0f_fallback_summary(0x6f);
    assert_no_0f_fallback_summary(0xef);
    assert_no_rep_fallback_summary(0xa4);
    assert_no_rep_fallback_summary(0xaf);
    new_dynarec_note_rep_scas_debug_site(0x403000u, 0xaeu);
    assert_no_rep_scas_debug_summary();
    assert_no_rep_scas_debug_site_summary(0);
    assert(new_dynarec_format_d0d3_compare_summary(summary, sizeof(summary)) == 0);
    assert(new_dynarec_format_d0d3_compare_site_summary(summary, sizeof(summary), 0) == 0);
    assert(new_dynarec_format_d0d3_compare_sample_summary(summary, sizeof(summary), 0) == 0);
    assert(new_dynarec_format_d0d3_compare_bailout_summary(summary, sizeof(summary), 0) == 0);
#endif

    new_dynarec_note_block_became_no_immediates(0x1040, 0x2040, 0xa0);
    new_dynarec_stats_snapshot(&snapshot);
    assert(snapshot.no_immediates_transitions == 2);

    new_dynarec_set_verify_config(NULL);
    new_dynarec_set_trace_hook(NULL, NULL);

    return 0;
}
