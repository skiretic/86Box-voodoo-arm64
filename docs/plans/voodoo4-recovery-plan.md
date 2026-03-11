# Voodoo 4 Recovery Plan

Date: 2026-03-11

Scope: research-backed restart plan for Voodoo 4 4500 / VSA-100 on branch `voodoo4-restart`.

This plan intentionally replaces assumption-driven implementation planning with an evidence-first sequence.

## Starting Point

- `Verified:` the local ROM is a conventional x86 VGA option ROM with VBE services and PCI ID `121a:0009`.
- `Verified:` the tested ROM also validates PCI subsystem tuple `121a:0004` at `0x2c-0x2f`, and matching that value clears the earlier pre-ext gate.
- `Verified:` the ROM touches many offsets already modeled by 86Box’s Banshee/Voodoo3 path.
- `Verified:` the ROM does not justify a clean-sheet VGA bring-up model.
- `Verified:` the current reuse-first path now reaches ROM POST plus manual Windows desktop bring-up through at least `1024x768` `16-bit`, with the Voodoo4 driver installed.
- `Verified:` targeted mode-state tracing now shows the good live V4 Windows desktop path programming tiled `16-bit` scanout.
- `Verified:` the first reproduced `32-bit` desktop failure was tiled scanout landing on the wrong renderer.
- `Verified:` the smallest matching shared-path fix, a tiled `32-bit` desktop renderer, now restores the manually tested `640x480`, `800x600`, `1024x768`, and `1280x1024` `32-bit` desktop modes.
- `Inferred:` the active milestone is no longer “capture the first bad `32-bit` desktop path”; it is now “use the stronger desktop baseline to evaluate the next real Voodoo4-specific unknown only when a fresh symptom points there.”

## Non-Goals for the First Recovery Pass

- `Verified:` no 3D implementation work
- `Verified:` no Voodoo5 multi-chip work
- `Verified:` no speculative scanout or framebuffer hacks
- `Verified:` no forced adoption of a standalone `vid_voodoo4.c` unless reuse experiments fail with evidence

## Phase 1: Confirm the Reuse Baseline

Status: `Verified complete`

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

Status: `Verified complete for baseline desktop bring-up`

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

Status: `Active`

### Goal

Separate true VSA-100 deltas from accidental emulator gaps.

### Questions to answer

1. `Verified:` the first concrete failure beyond the earlier desktop milestone was distortion in tiled `32-bit` color with the Voodoo4 driver installed.
2. `Verified:` the traced good `16-bit` desktop path uses tiled scanout.
3. `Verified:` the traced bad `32-bit` desktop path also used tiled scanout, but initially fell through to the linear `32bpp` renderer.
4. `Verified:` adding a tiled `32-bit` desktop renderer fixed the manually tested `640x480`, `800x600`, `1024x768`, and `1280x1024` `32-bit` desktop modes.
5. `Unknown:` do Voodoo 4 display or memory-related registers require different reset values than Banshee/Voodoo3 beyond the now-working desktop scanout baseline?
6. `Unknown:` do shared assumptions such as SDRAM sizing or `Init_strapInfo` become visible on a later failing path?
7. `Inferred:` Voodoo4/Voodoo5 differences that still matter are now more likely to sit in richer driver-visible behavior than in basic VGA/VBE bring-up or common desktop scanout.

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
2. `Verified:` preserve the current functional fix in `vid_voodoo_banshee.c`: PCI device ID `121a:0009`, ROM-backed subsystem tuple `121a:0004`, and the existing ext offset `0x70` coverage.
3. `Verified:` the temporary targeted mode-state tracing has been removed now that the bad `32-bit` mode was captured and resolved.
4. `Inferred:` only revisit fresh runtime tracing if a new display or driver-visible symptom appears.
5. `Inferred:` the next smallest code delta should now be driven by a new reproduced post-desktop symptom, not by the solved tiled `32-bit` desktop boundary.
6. `Inferred:` only if that next boundary points back to reset/default assumptions should the next code delta target SDRAM sizing, `Init_dramInit1`, `DACMODE`, stride, or `Init_strapInfo`.

## Supporting Documents

- [Research index](../research/voodoo4-index.md)
- [ROM analysis](../research/voodoo4-rom-analysis.md)
- [Register and code correlation](../research/voodoo4-banshee-correlation.md)
- [Open questions and risks](../research/voodoo4-open-questions.md)
