#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <86box/86box.h>
#include <86box/mem.h>

page_t   test_pages[4];
page_t  *pages                   = test_pages;
uint32_t purgable_page_list_head = EVICT_LINK_NULL;
int      purgeable_page_count    = 0;

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
    }
    purgable_page_list_head = EVICT_LINK_NULL;
    purgeable_page_count    = 0;
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

    return 0;
}
