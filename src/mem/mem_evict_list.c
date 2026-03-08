#include <stdint.h>

#include <86box/86box.h>
#include <86box/mem.h>
#include "codegen_public.h"

static inline uint32_t
page_index(page_t *page)
{
    return ((uintptr_t) page - (uintptr_t) pages) / sizeof(page_t);
}

int
page_has_dirty_code_overlap(const page_t *page)
{
    if (page->code_present_mask & page->dirty_mask)
        return 1;

    for (uint8_t c = 0; c < 64; c++) {
        if (page->byte_code_present_mask[c] & page->byte_dirty_mask[c])
            return 1;
    }

    return 0;
}

page_t *
page_get_next_reclaimable_evict_page(void)
{
    while (!purgable_page_list_is_empty()) {
        page_t *page = &pages[purgable_page_list_head];

        if (page_has_dirty_code_overlap(page))
            return page;

        new_dynarec_note_purgable_page_dequeued_stale();
        page_remove_from_evict_list(page);
    }

    return NULL;
}

int
page_enqueue_if_reclaimable(page_t *page)
{
    if (page_in_evict_list(page) || !page_has_dirty_code_overlap(page))
        return 0;
    page_add_to_evict_list(page);
    return 1;
}

int
page_enqueue_if_reclaimable_with_source(page_t *page, page_evict_enqueue_source_t source)
{
    if (!page_enqueue_if_reclaimable(page))
        return 0;

    page_set_evict_enqueue_source(page, source);
    return 1;
}

void
page_set_evict_enqueue_source(page_t *page, page_evict_enqueue_source_t source)
{
    page->evict_enqueue_source = source;
}

void
page_complete_dirty_code_flush(page_t *page)
{
    page->code_present_mask &= ~page->dirty_mask;
    page->dirty_mask = 0;

    for (uint8_t c = 0; c < 64; c++) {
        page->byte_code_present_mask[c] &= ~page->byte_dirty_mask[c];
        page->byte_dirty_mask[c] = 0;
    }

    if (page_in_evict_list(page))
        new_dynarec_note_purgable_page_dequeued_flush();
    if (page_in_evict_list(page))
        page_remove_from_evict_list(page);
}

void
page_clear_code_presence_if_no_blocks(page_t *page)
{
    if (page->block || page->block_2)
        return;

    page->code_present_mask = 0;
    for (uint8_t c = 0; c < 64; c++)
        page->byte_code_present_mask[c] = 0;

    if (page_in_evict_list(page) && !page_has_dirty_code_overlap(page))
        new_dynarec_note_purgable_page_dequeued_no_blocks();
    if (page_in_evict_list(page) && !page_has_dirty_code_overlap(page))
        page_remove_from_evict_list(page);
}

void
page_add_to_evict_list(page_t *page)
{
    uint32_t page_nr = page_index(page);

    if (page_in_evict_list(page))
        fatal("page_add_to_evict_list: already in evict list!\n");

    page->evict_prev = EVICT_LINK_NULL;
    page->evict_next = purgable_page_list_head;

    if (!purgable_page_list_is_empty())
        pages[purgable_page_list_head].evict_prev = page_nr;

    purgable_page_list_head = page_nr;
    purgeable_page_count++;
    new_dynarec_note_purgable_page_enqueued();
}

void
page_remove_from_evict_list(page_t *page)
{
    if (!page_in_evict_list(page))
        fatal("page_remove_from_evict_list: not in evict list!\n");

    if (page->evict_prev != EVICT_LINK_NULL)
        pages[page->evict_prev].evict_next = page->evict_next;
    else
        purgable_page_list_head = page->evict_next;

    if (page->evict_next != EVICT_LINK_NULL)
        pages[page->evict_next].evict_prev = page->evict_prev;

    page->evict_prev = EVICT_NOT_IN_LIST;
    page->evict_next = EVICT_LINK_NULL;
    page->evict_enqueue_source = PAGE_EVICT_ENQUEUE_SOURCE_NONE;
    purgeable_page_count--;
}
