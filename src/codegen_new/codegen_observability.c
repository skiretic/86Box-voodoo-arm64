#include <string.h>

#include "codegen_public.h"

#ifdef USE_NEW_DYNAREC
new_dynarec_stats_t new_dynarec_stats;

static new_dynarec_trace_hook_t new_dynarec_trace_hook;
static void                    *new_dynarec_trace_hook_opaque;

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

void
new_dynarec_set_trace_hook(new_dynarec_trace_hook_t hook, void *opaque)
{
    new_dynarec_trace_hook        = hook;
    new_dynarec_trace_hook_opaque = opaque;
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
#endif
