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
- `Verified:` the Voodoo4 path now exposes/defaults to `32 MB`, and guest-visible tools now also report about `31207 KB`.
- `Verified:` the working pre-`32 MB` desktop baseline was effectively an `8 MB` path, not a separately verified good `16 MB` Voodoo4 path.
- `Verified:` that `32 MB` change exposes a fresh desktop-only symptom: the tiled `32-bit` desktop surface becomes inconsistently populated once the driver moves it into higher VRAM.
- `Verified:` fresh high-base `2D` traces show the bad path launching `rectfill`, `host_to_screen`, and `screen_to_screen` against desktop base `0x00d00000`.
- `Verified:` sampled `screen_to_screen` copies are internally consistent, including later copies that faithfully move zero-filled linear source data into visible desktop tiles.
- `Verified:` fresh linear/LFB traces show writes landing at tiled base `0x01d00000` while desktop scanout and traced `2D` destinations remain at `0x00d00000`.
- `Inferred:` the active milestone is no longer “capture the first bad `32-bit` desktop path”; it is now “localize the higher-half desktop-surface population/address-translation mismatch without reopening solved ROM or common tiled-renderer boundaries.”

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

1. `Verified:` the earlier common tiled `32-bit` desktop distortion boundary is now closed for the manually tested `640x480`, `800x600`, `1024x768`, and `1280x1024` modes.
2. `Verified:` the traced good `16-bit` desktop path uses tiled scanout.
3. `Verified:` the traced bad `32-bit` desktop path also used tiled scanout, but initially fell through to the linear `32bpp` renderer.
4. `Verified:` adding a tiled `32-bit` desktop renderer fixed the manually tested `640x480`, `800x600`, `1024x768`, and `1280x1024` `32-bit` desktop modes.
5. `Verified:` guest-visible VRAM sizing is now corrected to `32 MB`.
6. `Verified:` the remaining desktop failure still lands on `32bpp_tiled`, so the active bug is not the earlier missing tiled renderer.
7. `Verified:` fresh high-base `2D` traces show the visible desktop work targeting `0x00d00000`.
8. `Verified:` sampled `screen_to_screen` copies behave consistently with their sources, including copies of already-zero linear source regions into visible desktop tiles.
9. `Verified:` fresh linear/LFB traces simultaneously show writes targeting `0x01d00000`, exactly `16 MB` above the visible desktop base.
10. `Unknown:` is the real mismatch in Voodoo4 linear/LFB address translation, in upstream source-surface population, or in desktop scanout interpretation?
11. `Unknown:` do Voodoo 4 display or memory-related registers require different reset values than Banshee/Voodoo3 beyond the now-working common desktop scanout baseline?
12. `Inferred:` Voodoo4/Voodoo5 differences that still matter are now more likely to sit in richer driver-visible desktop/2D behavior than in basic VGA/VBE bring-up.

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
3. `Verified:` temporary targeted mode-state tracing has been re-enabled because the `32 MB` work exposed a fresh post-desktop symptom.
4. `Verified:` two tiny runtime experiments against that fresh symptom were already tried and reverted: a desktop-base alias hack and a zero-`lfbMemoryConfig` LFB guard.
5. `Verified:` temporary high-base `2D` and linear/LFB tracing is now in place in `vid_voodoo_banshee_blitter.c` and `vid_voodoo_banshee.c`.
6. `Inferred:` the next smallest code delta should target the Voodoo4 linear/LFB or source-surface population path, not the already-solved guest-visible memory report or the already-closed common tiled `32-bit` renderer gap.
7. `Inferred:` only if that next boundary points back to reset/default assumptions should the next code delta retarget SDRAM sizing, `Init_dramInit1`, `DACMODE`, stride, or `Init_strapInfo`.

## Supporting Documents

- [Research index](../research/voodoo4-index.md)
- [ROM analysis](../research/voodoo4-rom-analysis.md)
- [Register and code correlation](../research/voodoo4-banshee-correlation.md)
- [Open questions and risks](../research/voodoo4-open-questions.md)
