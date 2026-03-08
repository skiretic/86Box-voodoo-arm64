#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <86box/86box.h>
#include <86box/mem.h>
#include "codegen_public.h"

page_t   test_pages[4];
page_t  *pages                   = test_pages;
uint32_t purgable_page_list_head = EVICT_LINK_NULL;
int      purgeable_page_count    = 0;
uint64_t byte_dirty_masks[4][64];
uint64_t byte_code_present_masks[4][64];
new_dynarec_stats_t new_dynarec_stats;

void
fatal(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    abort();
}

static void
reset_pages(void)
{
    for (size_t i = 0; i < 4; i++) {
        test_pages[i].evict_prev = EVICT_NOT_IN_LIST;
        test_pages[i].evict_next = EVICT_LINK_NULL;
        test_pages[i].evict_enqueue_source = PAGE_EVICT_ENQUEUE_SOURCE_NONE;
        test_pages[i].dirty_mask = 0;
        test_pages[i].code_present_mask = 0;
        test_pages[i].byte_dirty_mask = byte_dirty_masks[i];
        test_pages[i].byte_code_present_mask = byte_code_present_masks[i];
        for (size_t j = 0; j < 64; j++) {
            byte_dirty_masks[i][j] = 0;
            byte_code_present_masks[i][j] = 0;
        }
    }
    purgable_page_list_head = EVICT_LINK_NULL;
    purgeable_page_count    = 0;
    new_dynarec_stats       = (new_dynarec_stats_t) {0};
}

int
main(void)
{
    reset_pages();

    page_add_to_evict_list(&test_pages[0]);
    assert(page_in_evict_list(&test_pages[0]));
    assert(purgable_page_list_head == 0);
    assert(test_pages[0].evict_prev == EVICT_LINK_NULL);
    assert(test_pages[0].evict_next == EVICT_LINK_NULL);
    assert(purgeable_page_count == 1);
    assert(new_dynarec_stats.purgable_page_enqueues == 1);

    page_add_to_evict_list(&test_pages[2]);
    assert(purgable_page_list_head == 2);
    assert(test_pages[2].evict_prev == EVICT_LINK_NULL);
    assert(test_pages[2].evict_next == 0);
    assert(test_pages[0].evict_prev == 2);
    assert(purgeable_page_count == 2);

    page_remove_from_evict_list(&test_pages[2]);
    assert(purgable_page_list_head == 0);
    assert(test_pages[0].evict_prev == EVICT_LINK_NULL);
    assert(!page_in_evict_list(&test_pages[2]));
    assert(test_pages[2].evict_next == EVICT_LINK_NULL);
    assert(purgeable_page_count == 1);

    page_remove_from_evict_list(&test_pages[0]);
    assert(purgable_page_list_head == EVICT_LINK_NULL);
    assert(!page_in_evict_list(&test_pages[0]));
    assert(test_pages[0].evict_next == EVICT_LINK_NULL);
    assert(purgeable_page_count == 0);

    test_pages[1].byte_dirty_mask[7] = 0x4;
    test_pages[1].byte_code_present_mask[7] = 0x4;
    test_pages[1].evict_enqueue_source = PAGE_EVICT_ENQUEUE_SOURCE_WRITE;
    assert(page_has_dirty_code_overlap(&test_pages[1]));
    page_add_to_evict_list(&test_pages[1]);
    page_complete_dirty_code_flush(&test_pages[1]);
    assert(!page_has_dirty_code_overlap(&test_pages[1]));
    assert(!page_in_evict_list(&test_pages[1]));
    assert(test_pages[1].byte_dirty_mask[7] == 0);
    assert(test_pages[1].byte_code_present_mask[7] == 0);
    assert(purgeable_page_count == 0);
    assert(new_dynarec_stats.purgable_page_dequeues_flush == 1);

    test_pages[3].dirty_mask = 0x20;
    test_pages[3].code_present_mask = 0x20;
    assert(page_has_dirty_code_overlap(&test_pages[3]));
    page_add_to_evict_list(&test_pages[3]);
    page_complete_dirty_code_flush(&test_pages[3]);
    assert(!page_has_dirty_code_overlap(&test_pages[3]));
    assert(!page_in_evict_list(&test_pages[3]));
    assert(test_pages[3].dirty_mask == 0);
    assert(test_pages[3].code_present_mask == 0);
    assert(purgeable_page_count == 0);

    test_pages[1].dirty_mask = 0x8;
    test_pages[1].code_present_mask = 0x8;
    page_add_to_evict_list(&test_pages[1]);
    page_add_to_evict_list(&test_pages[0]);
    assert(purgable_page_list_head == 0);
    assert(page_get_next_reclaimable_evict_page() == &test_pages[1]);
    assert(purgable_page_list_head == 1);
    assert(!page_in_evict_list(&test_pages[0]));
    assert(page_in_evict_list(&test_pages[1]));
    assert(purgeable_page_count == 1);
    assert(new_dynarec_stats.purgable_page_dequeues_stale == 1);

    test_pages[1].code_present_mask = 0;
    assert(page_get_next_reclaimable_evict_page() == NULL);
    assert(purgable_page_list_head == EVICT_LINK_NULL);
    assert(!page_in_evict_list(&test_pages[1]));
    assert(purgeable_page_count == 0);

    test_pages[2].dirty_mask = 0x10;
    test_pages[2].code_present_mask = 0x10;
    test_pages[2].byte_dirty_mask[4] = 0x2;
    test_pages[2].byte_code_present_mask[4] = 0x2;
    test_pages[2].evict_enqueue_source = PAGE_EVICT_ENQUEUE_SOURCE_CODEGEN;
    page_add_to_evict_list(&test_pages[2]);
    page_clear_code_presence_if_no_blocks(&test_pages[2]);
    assert(test_pages[2].code_present_mask == 0);
    assert(test_pages[2].byte_code_present_mask[4] == 0);
    assert(!page_in_evict_list(&test_pages[2]));
    assert(new_dynarec_stats.purgable_page_dequeues_no_blocks == 1);

    test_pages[2].code_present_mask = 0x10;
    test_pages[2].byte_code_present_mask[4] = 0x2;
    test_pages[2].block = 7;
    page_clear_code_presence_if_no_blocks(&test_pages[2]);
    assert(test_pages[2].code_present_mask == 0x10);
    assert(test_pages[2].byte_code_present_mask[4] == 0x2);

    reset_pages();
    test_pages[0].dirty_mask = 0xffffffffffffffffULL;
    assert(!page_enqueue_if_reclaimable(&test_pages[0]));
    assert(!page_in_evict_list(&test_pages[0]));
    assert(new_dynarec_stats.purgable_page_enqueues == 0);
    assert(test_pages[0].evict_enqueue_source == PAGE_EVICT_ENQUEUE_SOURCE_NONE);

    test_pages[0].code_present_mask = 0x4;
    assert(page_enqueue_if_reclaimable(&test_pages[0]));
    assert(page_in_evict_list(&test_pages[0]));
    assert(new_dynarec_stats.purgable_page_enqueues == 1);

    reset_pages();
    test_pages[0].dirty_mask = 0xffffffffffffffffULL;
    assert(!page_enqueue_if_reclaimable_with_source(&test_pages[0], PAGE_EVICT_ENQUEUE_SOURCE_BULK_DIRTY));
    assert(test_pages[0].evict_enqueue_source == PAGE_EVICT_ENQUEUE_SOURCE_NONE);

    test_pages[0].code_present_mask = 0x4;
    assert(page_enqueue_if_reclaimable_with_source(&test_pages[0], PAGE_EVICT_ENQUEUE_SOURCE_BULK_DIRTY));
    assert(page_in_evict_list(&test_pages[0]));
    assert(test_pages[0].evict_enqueue_source == PAGE_EVICT_ENQUEUE_SOURCE_BULK_DIRTY);

    return 0;
}
