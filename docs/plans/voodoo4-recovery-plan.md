# Voodoo 4 Recovery Plan

Date: 2026-03-11

Scope: research-backed restart plan for Voodoo 4 4500 / VSA-100 on branch `voodoo4-restart`.

This plan intentionally replaces assumption-driven implementation planning with an evidence-first sequence.

## Starting Point

- `Verified:` the local ROM is a conventional x86 VGA option ROM with VBE services and PCI ID `121a:0009`.
- `Verified:` the ROM touches many offsets already modeled by 86Box’s Banshee/Voodoo3 path.
- `Verified:` the ROM does not justify a clean-sheet VGA bring-up model.
- `Inferred:` the first milestone should be “prove the minimum Banshee/Voodoo3 reuse surface for POST and VGA/VBE,” not “create a standalone Voodoo 4 architecture.”

## Non-Goals for the First Recovery Pass

- `Verified:` no 3D implementation work
- `Verified:` no Voodoo5 multi-chip work
- `Verified:` no speculative scanout or framebuffer hacks
- `Verified:` no forced adoption of a standalone `vid_voodoo4.c` unless reuse experiments fail with evidence

## Phase 1: Confirm the Reuse Baseline

### Goal

Prove how much of the current Banshee/Voodoo3 device shape already matches what the Voodoo 4 ROM expects.

### Required checks

1. `Verified:` audit current 86Box PCI identity, BAR exposure, ROM mapping, and ext-register coverage against the ROM findings.
2. `Verified:` enumerate every ROM-touched offset and classify it as:
   - already modeled correctly
   - modeled but likely with wrong defaults or semantics
   - missing
3. `Verified:` pay special attention to:
   - PCI device ID `0x0009`
   - ROM enable path
   - ext offsets `0x1c`, `0x28`, `0x2c`, `0x40`, `0x4c`, `0x5c`, `0x70`, `0x98`, `0xe4`, `0xe8`

### Exit criteria

- `Verified:` we have a written gap list grounded in the ROM, not in earlier branch assumptions.

## Phase 2: Implement the Smallest Proven Delta Set

### Goal

Reach ROM POST and VGA/VBE behavior by changing only what the evidence requires.

### Priority order

1. `Verified:` PCI identity and ROM exposure
2. `Verified:` ext-register coverage required by ROM POST
3. `Inferred:` memory-sizing or strap defaults only if ROM behavior proves current values are wrong
4. `Unknown:` any deeper shared-core change only if the earlier steps still fail

### Guardrails

- `Verified:` every change must trace back to either ROM-observed behavior or a source-backed register expectation.
- `Verified:` shared Banshee/Voodoo3 paths should be reused first.
- `Inferred:` if a new Voodoo 4-specific file becomes necessary, it should begin as an identity/delta layer, not a fork of the whole Banshee implementation.

## Phase 3: Validate What Is Actually Different

### Goal

Separate true VSA-100 deltas from accidental emulator gaps.

### Questions to answer

1. `Unknown:` does the ROM require VSA-100-specific config-space behavior beyond ID/BAR basics?
2. `Unknown:` does offset `0x70` block POST or only later features?
3. `Unknown:` do Voodoo 4 display registers require different reset values than Banshee/Voodoo3?
4. `Inferred:` are Voodoo4/Voodoo5 differences mostly outside milestone-one VGA/VBE bring-up?

### Exit criteria

- `Verified:` each unresolved difference is documented as either proven delta or remaining unknown.

## Phase 4: Audit Earlier Voodoo 4 Work Against Evidence

### Goal

Use old research only after the fresh baseline exists.

### Method

1. `Verified:` compare old branch claims against the ROM analysis and correlation docs.
2. `Verified:` reject any claim that lacks ROM support, source support, or a reproducible runtime trace.
3. `Inferred:` salvage only items that line up with the fresh evidence.

## Immediate Next Actions

1. `Verified:` preserve the new docs in this repo as the current source of truth.
2. `Verified:` use the ROM correlation list as the checklist for the first coding session.
3. `Inferred:` start implementation later from the smallest reuse-first delta set, not from an architectural rewrite.

## Supporting Documents

- [Research index](../research/voodoo4-index.md)
- [ROM analysis](../research/voodoo4-rom-analysis.md)
- [Register and code correlation](../research/voodoo4-banshee-correlation.md)
- [Open questions and risks](../research/voodoo4-open-questions.md)
