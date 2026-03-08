#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "codegen_public.h"

int
main(void)
{
    char                summary[2048];
    new_dynarec_stats_t snapshot;
    int                 deferrals = 0;

    assert(new_dynarec_classify_post_purge_state(1, BLOCK_INVALID) == NEW_DYNAREC_POST_PURGE_HAVE_FREE_BLOCK);
    assert(new_dynarec_classify_post_purge_state(0, 9) == NEW_DYNAREC_POST_PURGE_RETRY_DIRTY_LIST);
    assert(new_dynarec_classify_post_purge_state(0, BLOCK_INVALID) == NEW_DYNAREC_POST_PURGE_RANDOM_EVICTION);
    assert(new_dynarec_should_remove_aborted_mark_block(1, 1) == 1);
    assert(new_dynarec_should_remove_aborted_mark_block(1, 0) == 0);
    assert(new_dynarec_should_remove_aborted_mark_block(0, 0) == 0);

    new_dynarec_stats_reset();
    assert(!new_dynarec_should_defer_marking_new_block());
    new_dynarec_note_allocator_pressure_empty_purgable_list();
    assert(new_dynarec_should_defer_marking_new_block());

    new_dynarec_note_random_eviction(0x1100, 0x2100, 0x00);

    while (new_dynarec_should_defer_marking_new_block()) {
        new_dynarec_note_deferred_block_mark(0x1200 + (uint32_t) deferrals, 0x2200 + (uint32_t) deferrals);
        deferrals++;
    }

    assert(deferrals == NEW_DYNAREC_RANDOM_EVICTION_MARK_DEFERRAL_BUDGET);
    assert(!new_dynarec_should_defer_marking_new_block());

    memset(&snapshot, 0, sizeof(snapshot));
    new_dynarec_stats_snapshot(&snapshot);
    assert(snapshot.random_evictions == 1);
    assert(snapshot.deferred_block_marks == NEW_DYNAREC_RANDOM_EVICTION_MARK_DEFERRAL_BUDGET);

    assert(new_dynarec_format_stats_summary(summary, sizeof(summary), &snapshot) > 0);
    assert(strstr(summary, "deferred_block_marks=8"));

    return 0;
}
