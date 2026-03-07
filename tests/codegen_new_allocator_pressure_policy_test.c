#include <assert.h>
#include <stdint.h>

#include "codegen_public.h"

int
main(void)
{
    assert(new_dynarec_classify_post_purge_state(1, BLOCK_INVALID) == NEW_DYNAREC_POST_PURGE_HAVE_FREE_BLOCK);
    assert(new_dynarec_classify_post_purge_state(0, 9) == NEW_DYNAREC_POST_PURGE_RETRY_DIRTY_LIST);
    assert(new_dynarec_classify_post_purge_state(0, BLOCK_INVALID) == NEW_DYNAREC_POST_PURGE_RANDOM_EVICTION);

    return 0;
}
