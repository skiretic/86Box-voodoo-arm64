# Voodoo4 Next-Session Prompt

Use Serena in this session.

Repo and branch:
- Repo: `/Users/anthony/projects/code/86Box-voodoo-arm64`
- Branch: `voodoo4-restart`

Required startup steps:
1. Activate Serena project: `/Users/anthony/projects/code/86Box-voodoo-arm64`
2. `Verified:` Serena onboarding for this repo was already completed in earlier sessions; do not spend time re-checking it unless Serena explicitly says project context is missing.
3. Read these source-of-truth docs first:
   - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/status/voodoo4-executive-summary.md`
   - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/status/voodoo4-tracker.md`
   - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/status/voodoo4-changelog.md`
   - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/research/voodoo4-index.md`
   - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/research/voodoo4-rom-analysis.md`
   - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/research/voodoo4-banshee-correlation.md`
   - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/research/voodoo4-open-questions.md`
   - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/plans/voodoo4-recovery-plan.md`
   - `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/plans/voodoo4-next-session-prompt.md`

Current verified state:
- `Verified:` the earlier ROM-dispatch / pre-ext boundary is solved.
- `Verified:` PCI identity is `121a:0009` and the ROM-backed subsystem tuple is `121a:0004`.
- `Verified:` Windows desktop boot works with the Voodoo4 driver active.
- `Verified:` common tested `16-bit` desktop modes work.
- `Verified:` the earlier tiled `32-bit` desktop renderer gap is solved for manually tested common modes `640x480`, `800x600`, `1024x768`, and `1280x1024`.
- `Verified:` Voodoo4 now exposes/defaults to `32 MB` backing memory.
- `Verified:` guest-visible Voodoo4 memory reporting now also shows `32 MB`; 3DMark99 reports about `31207 KB`.
- `Verified:` the working pre-`32 MB` Voodoo4 desktop baseline was effectively an `8 MB` path; there was no separately verified good `16 MB` Voodoo4 desktop path.
- `Verified:` the remaining live symptom is inconsistent desktop population after the guest starts using the `32 MB` path.
- `Verified:` bad mode traces still show `pixfmt=3`, `tile=1`, and `render=32bpp_tiled`.
- `Verified:` the bad path uses `desktopStart=0x00d00000`.
- `Verified:` CPU/LFB traces show guest traffic targeting `0x01d00000`, an exact `+0x01000000` (`16 MB`) split above desktop start.
- `Verified:` current screenshots show the failure is inconsistent rather than uniform: some windows, icons, and labels render correctly while other desktop regions remain black or missing.
- `Verified:` widening the traced V4/Banshee register block to `0x1e0..0x260` still did not show the idle desktop path programming classic `colBufferAddr` / `colBufferStride` / `auxBufferAddr` / `auxBufferStride` / `leftOverlayBuf` / `swapbufferCMD` state before the first high-base V4 LFB traffic.
- `Verified:` a first minimal V4-only CPU/LFB alias experiment engaged exactly on the traced split path (`effectiveTileBase=00d00000`, `aliasApplied=1`) while the guest still programmed raw `tileBase=01d00000`.
- `Verified:` that naive V4-only CPU/LFB fold did not visibly improve the idle desktop by itself.
- `Verified:` traced bad low-linear source families now include `0x00133000` (`0x001332d4` / `0x00133308`) and `0x002a0000` (`0x002a0084..0x002a0090`) in addition to earlier failing families.
- `Verified:` `bad3` (`0x002a0000`) is not uniformly zero; the traced `0x22000003` producer can leave row-head words zero while writing nonzero data farther into the same row.
- `Verified:` the paired `screen_to_screen` consumer from `srcBase=0x002a0084`, `srcFmt=0x00050080`, `srcStride=0x80`, `srcXY=0` faithfully copies that mixed row-head / row-tail state into the desktop.
- `Verified:` a larger V4-only producer-side experiment that force-normalized masked mono `0x22000003` uploads to a left-aligned row origin changed the traced `bad3` launch (`lastByte=4` instead of `12`) but did not improve the visible desktop.
- `Verified:` the same final rerun also reproduced the earlier `bad2` zero-source family at `0x001332d4` and `0x00133308`.

Key current evidence:
- Current log path: `/tmp/voodoo4-mem-trace.log`
- Older working comparison log: `/tmp/voodoo4-mode-boundary.log`
- Current high-value log anchors:
  - `lfbMemoryConfig[2] ... tileBase=01d00000 ... desktopStart=00d00000`
  - `V4 LFB trace[1] ... effectiveTileBase=00d00000 aliasApplied=1`
  - `V4 watch host stride[...] ... command=22000003 ... srcFmt=00000010 ... srcXY=0000f040`
  - `V4 watch linear result[26] ... dstAfter=002a0084@00000000 002a0088@00000000 002a008c@004f4f8f 002a0090@004f4f8f`
  - `V4 2D sample[42] ... src=002a0084@00000000 002a0088@00000000 002a008c@004f4f8f 002a0090@004f4f8f`
  - `V4 2D sample[71] ... src=001332d4@00000000 ...`
  - `V4 2D sample[73] ... src=00133308@00000000 ...`

Current temporary instrumentation:
- `src/video/vid_voodoo_banshee_blitter.c`
  - high-base `2D` launch tracing for `rectfill`, `host_to_screen`, and `screen_to_screen`
  - sampled `screen_to_screen` pre/post pixel logging
  - direct watch pages for `good3` / `bad2` / `bad3`
  - `bad3` host-stride tracing and `dstAfterPlus16` row-tail dumps
  - current larger producer-side experiment that force-normalizes masked mono `0x22000003` uploads to a left-aligned row origin
- `src/video/vid_voodoo_banshee.c`
  - high-base linear/LFB write tracing in `banshee_write_linear_l()`
  - trace-only desktop-span / `tileMarkGuess` / `aaMarkGuess` logging
  - current minimal V4-only CPU/LFB alias experiment logs `effectiveTileBase` and `aliasApplied`
- `src/video/vid_voodoo_reg.c`
  - widened V4/Banshee register-block tracing across `0x1e0..0x260`
- `Verified:` a behavioral V4-only alias/fold experiment is now present in the branch, but it has only been verified as “engages” and “does not obviously fix the idle desktop”

Important negative results already tried:
- `Verified:` a V4-only desktop-base alias/mask experiment made the desktop worse and was reverted.
- `Verified:` a V4-only zero-`lfbMemoryConfig` LFB guard did not fix the distortion and was reverted.
- `Verified:` driver reinstall did not change the guest-visible `8 MB` report before the guest-visible memory fixes.
- `Verified:` a first minimal V4-only CPU/LFB fold that aliases the exact traced `+16 MB` split engaged successfully but did not visibly improve the idle desktop.
- `Verified:` a narrower stride-packed host-row sizing correction fixed the absurd `bad3` row completion calculation but did not improve the desktop.
- `Verified:` a larger producer-side left-alignment experiment for masked mono `0x22000003` uploads changed the traced `bad3` launch shape but still did not improve the desktop.

Working theory:
- `Verified:` `32 MB` detection itself is fixed.
- `Verified:` some visible desktop operations are correct, so the remaining bug is not a uniform scanout failure.
- `Verified:` some sampled `screen_to_screen` copies are faithfully copying zeros from a linear source region into the visible tiled desktop.
- `Verified:` the paired `bad3` consumer copy is behaving faithfully; it is copying the mixed zero/nonzero row head/tail state that the staging surface already contains.
- `Inferred:` the strongest current lead is now a V4 staging-surface population/layout mismatch, not a generic tiled scanout failure or a consumer-side readback bug.
- `Inferred:` the `0x01d00000` versus `0x00d00000` split remains an important address-level clue, but the producer-side row-origin normalization experiment was not sufficient.
- `Unknown:` whether the real fix is a missing seed/population pass for these low-linear staging pages, a different V4-only rule for masked mono `0x22000003` producer semantics, or another richer VSA-only address/layout rule.

Guardrails:
- Label major conclusions as `Verified` / `Inferred` / `Unknown`.
- Keep changes minimal and evidence-backed.
- Update living docs as you work.
- Do not claim anything beyond what is freshly verified.
- Prefer reuse-first over a speculative standalone Voodoo4 rewrite.
- Do not reopen the solved ROM-dispatch theory.
- Do not remove the current temporary tracing until the higher-VRAM mismatch is localized.

Suggested next debugging focus:
1. Start from the final verified state, not the earlier mid-session theory: the `bad3` consumer is faithful, and the larger producer-side left-alignment experiment is already a negative result.
2. Compare the traced masked `0x22000003` `bad3` producer against whatever earlier pass should have seeded `0x002a0000` and `0x00133000` before those later masked overlays or zero-source copies.
3. Revisit whether a subtler V4-only CPU/LFB or staging-layout rule still matters, but do not spend another session repeating the simple `+16 MB` fold or the already-failed producer-side left-alignment experiment.
4. Keep the widened `0x1e0..0x260` register trace and current low-linear watch instrumentation available, but prefer the next concrete producer/seed/layout hypothesis over another trace-only session.

Useful commands:
- `cmake --build build -j8`
- `bash scripts/test-voodoo4-blank-boundary.sh`
- launch with logging:
  - `./build/src/86Box.app/Contents/MacOS/86Box -P "$HOME/Library/Application Support/86Box/Virtual Machines/v4" --logfile /tmp/voodoo4-mem-trace.log`
