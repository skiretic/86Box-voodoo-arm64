#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

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
    assert(stats->purgable_page_missed_write_enqueue_overlap == 0);
    assert(stats->purgable_page_reenqueues_after_flush == 0);
    assert(stats->purgable_page_reenqueues_after_no_blocks == 0);
    assert(stats->purgable_page_dequeues_stale == 0);
    assert(stats->purgable_page_dequeues_flush == 0);
    assert(stats->purgable_page_dequeues_flush_write == 0);
    assert(stats->purgable_page_dequeues_flush_codegen == 0);
    assert(stats->purgable_page_dequeues_flush_bulk_dirty == 0);
    assert(stats->purgable_page_dequeues_no_blocks == 0);
    assert(stats->purgable_page_dequeues_no_blocks_write == 0);
    assert(stats->purgable_page_dequeues_no_blocks_codegen == 0);
    assert(stats->purgable_page_dequeues_no_blocks_bulk_dirty == 0);
    assert(stats->purgable_flush_attempts == 0);
    assert(stats->purgable_flush_successes == 0);
    assert(stats->purgable_flush_no_overlap == 0);
    assert(stats->purgable_flush_no_free_block == 0);
    assert(stats->purgable_flush_no_blocks == 0);
    assert(stats->purgable_flush_dirty_list_reuses == 0);
    assert(stats->random_evictions == 0);
}

int
main(void)
{
    char                summary[2048];
    new_dynarec_stats_t snapshot;
    trace_capture_t     trace_capture;

    memset(&trace_capture, 0, sizeof(trace_capture));

    new_dynarec_stats_reset();
    new_dynarec_stats_snapshot(&snapshot);
    assert_zeroed(&snapshot);

    new_dynarec_set_trace_hook(capture_trace, &trace_capture);

    new_dynarec_note_block_marked(0x1000, 0x2000, 7);
    assert(trace_capture.event_count == 1);
    assert(trace_capture.last_event.kind == NEW_DYNAREC_TRACE_BLOCK_MARKED);
    assert(trace_capture.last_event.pc == 0x1000);
    assert(trace_capture.last_event.phys == 0x2000);
    assert(trace_capture.last_event.detail == 7);

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
    new_dynarec_note_purgable_page_missed_write_enqueue_overlap();
    new_dynarec_note_purgable_page_reenqueued_after_flush();
    new_dynarec_note_purgable_page_reenqueued_after_no_blocks();
    new_dynarec_note_purgable_page_dequeued_stale();
    new_dynarec_note_purgable_page_dequeued_flush();
    new_dynarec_note_purgable_page_dequeued_flush_write();
    new_dynarec_note_purgable_page_dequeued_flush_codegen();
    new_dynarec_note_purgable_page_dequeued_flush_bulk_dirty();
    new_dynarec_note_purgable_page_dequeued_no_blocks();
    new_dynarec_note_purgable_page_dequeued_no_blocks_write();
    new_dynarec_note_purgable_page_dequeued_no_blocks_codegen();
    new_dynarec_note_purgable_page_dequeued_no_blocks_bulk_dirty();
    new_dynarec_note_purgable_flush_attempt();
    new_dynarec_note_purgable_flush_success();
    new_dynarec_note_purgable_flush_no_overlap();
    new_dynarec_note_purgable_flush_no_free_block();
    new_dynarec_note_purgable_flush_no_blocks();
    new_dynarec_note_purgable_flush_dirty_list_reuse();
    new_dynarec_note_random_eviction(0x1050, 0x2050, 0x08);
    assert(new_dynarec_classify_post_purge_state(1, BLOCK_INVALID) == NEW_DYNAREC_POST_PURGE_HAVE_FREE_BLOCK);
    assert(new_dynarec_classify_post_purge_state(0, 7) == NEW_DYNAREC_POST_PURGE_RETRY_DIRTY_LIST);
    assert(new_dynarec_classify_post_purge_state(0, BLOCK_INVALID) == NEW_DYNAREC_POST_PURGE_RANDOM_EVICTION);

    new_dynarec_stats_snapshot(&snapshot);
    assert(snapshot.blocks_marked == 1);
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
    assert(snapshot.purgable_page_missed_write_enqueue_overlap == 1);
    assert(snapshot.purgable_page_reenqueues_after_flush == 1);
    assert(snapshot.purgable_page_reenqueues_after_no_blocks == 1);
    assert(snapshot.purgable_page_dequeues_stale == 1);
    assert(snapshot.purgable_page_dequeues_flush == 1);
    assert(snapshot.purgable_page_dequeues_flush_write == 1);
    assert(snapshot.purgable_page_dequeues_flush_codegen == 1);
    assert(snapshot.purgable_page_dequeues_flush_bulk_dirty == 1);
    assert(snapshot.purgable_page_dequeues_no_blocks == 1);
    assert(snapshot.purgable_page_dequeues_no_blocks_write == 1);
    assert(snapshot.purgable_page_dequeues_no_blocks_codegen == 1);
    assert(snapshot.purgable_page_dequeues_no_blocks_bulk_dirty == 1);
    assert(snapshot.purgable_flush_attempts == 1);
    assert(snapshot.purgable_flush_successes == 1);
    assert(snapshot.purgable_flush_no_overlap == 1);
    assert(snapshot.purgable_flush_no_free_block == 1);
    assert(snapshot.purgable_flush_no_blocks == 1);
    assert(snapshot.purgable_flush_dirty_list_reuses == 1);
    assert(snapshot.random_evictions == 1);

    assert(new_dynarec_format_stats_summary(summary, sizeof(summary), &snapshot) > 0);
    assert(strstr(summary, "blocks_marked=1"));
    assert(strstr(summary, "blocks_recompiled=1"));
    assert(strstr(summary, "allocator_pressure_empty_purgable_list=1"));
    assert(strstr(summary, "purgable_page_enqueues=1"));
    assert(strstr(summary, "purgable_page_enqueues_write=1"));
    assert(strstr(summary, "purgable_page_enqueues_codegen=1"));
    assert(strstr(summary, "purgable_page_enqueues_bulk_dirty=1"));
    assert(strstr(summary, "purgable_page_missed_write_enqueue_overlap=1"));
    assert(strstr(summary, "purgable_page_reenqueues_after_flush=1"));
    assert(strstr(summary, "purgable_page_reenqueues_after_no_blocks=1"));
    assert(strstr(summary, "purgable_page_dequeues_stale=1"));
    assert(strstr(summary, "purgable_page_dequeues_flush=1"));
    assert(strstr(summary, "purgable_page_dequeues_flush_write=1"));
    assert(strstr(summary, "purgable_page_dequeues_flush_codegen=1"));
    assert(strstr(summary, "purgable_page_dequeues_flush_bulk_dirty=1"));
    assert(strstr(summary, "purgable_page_dequeues_no_blocks=1"));
    assert(strstr(summary, "purgable_page_dequeues_no_blocks_write=1"));
    assert(strstr(summary, "purgable_page_dequeues_no_blocks_codegen=1"));
    assert(strstr(summary, "purgable_page_dequeues_no_blocks_bulk_dirty=1"));
    assert(strstr(summary, "purgable_flush_attempts=1"));
    assert(strstr(summary, "purgable_flush_no_overlap=1"));
    assert(strstr(summary, "purgable_flush_no_free_block=1"));
    assert(strstr(summary, "purgable_flush_no_blocks=1"));
    assert(strstr(summary, "purgable_flush_dirty_list_reuses=1"));
    assert(strstr(summary, "random_evictions=1"));

    new_dynarec_note_block_became_no_immediates(0x1040, 0x2040, 0xa0);
    new_dynarec_stats_snapshot(&snapshot);
    assert(snapshot.no_immediates_transitions == 2);

    new_dynarec_set_trace_hook(NULL, NULL);

    return 0;
}
