#include <stdint.h>

#include <86box/86box.h>
#include <86box/mem.h>

static inline uint32_t
page_index(page_t *page)
{
    return ((uintptr_t) page - (uintptr_t) pages) / sizeof(page_t);
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
    purgeable_page_count--;
}
