# Voodoo4 Next-Session Prompt

Use Serena in this session.

Repo and branch:
- Repo: `/Users/anthony/projects/code/86Box-voodoo-arm64`
- Branch: `voodoo4-restart`

Required startup steps:
1. Activate Serena project: `/Users/anthony/projects/code/86Box-voodoo-arm64`
2. Check onboarding
3. If needed, run onboarding
4. Read these source-of-truth docs first:
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
- `Verified:` fresh mode-state tracing still shows the bad mode using `pixfmt=3`, `tile=1`, and `render=32bpp_tiled`.
- `Verified:` compared with the older working pre-`32 MB` trace, the bad `32 MB` path moves the desktop start address upward into higher VRAM.
- `Verified:` current screenshots show the failure is inconsistent rather than uniform: some windows, icons, and labels render correctly while other desktop regions remain black or missing.

Key current evidence:
- Current log path: `/tmp/voodoo4-mem-trace.log`
- Older working comparison log: `/tmp/voodoo4-mode-boundary.log`
- Fresh bad `32 MB` trace examples:
  - `first ext read 0038 = 00000448`
  - `first ext read 0018 = 18000000`
  - `mode[...] ... pixfmt=3 tile=1 ... render=32bpp_tiled`
  - bad path desktop start example: `start=00d00000`
- Older working pre-`32 MB` comparison trace used a lower desktop start such as `start=005f8000`
- Fresh high-base `2D` trace examples:
  - `V4 2D trace[...] ... cmd=rectfill ... dstBase=00d00000 ...`
  - `V4 2D trace[...] ... cmd=host_to_screen ... dstBase=00d00000 ...`
  - `V4 2D trace[...] ... cmd=screen_to_screen ... dstBase=00d00000 ...`
- Fresh sampled `screen_to_screen` evidence:
  - some samples show internally consistent copies from linear or tiled sources into the visible tiled desktop
  - later samples also show zero-filled linear source regions being copied into visible desktop tiles, which matches the black/missing regions
  - examples:
    - `sample[1]` copies `00c0c0c0` values consistently
    - `sample[23]` and `sample[24]` copy zeros from linear source `002de9b0...` into previously nonzero visible desktop tiles
- Fresh linear/LFB evidence:
  - `V4 LFB trace[1]: orig=13d00000 decoded=01d00000 mapped=01d00000 ... tileBase=01d00000 ... desktop=00d00000`
  - `V4 LFB trace[33]: orig=13d00080 decoded=01d00080 mapped=01d01000 ... tileBase=01d00000 ... desktop=00d00000`
  - `Verified:` current traced linear/LFB path targets `0x01d00000` while desktop scanout and traced `2D` destinations target `0x00d00000`
  - `Verified:` that is an exact `0x01000000` (`16 MB`) split

Current temporary instrumentation:
- `src/video/vid_voodoo_banshee_blitter.c`
  - high-base `2D` launch tracing for `rectfill`, `host_to_screen`, and `screen_to_screen`
  - sampled `screen_to_screen` pre/post pixel logging
- `src/video/vid_voodoo_banshee.c`
  - high-base linear/LFB write tracing in `banshee_write_linear_l()`
- `Verified:` no behavioral LFB alias/fold fix has been applied yet

Important negative results already tried:
- `Verified:` a V4-only desktop-base alias/mask experiment made the desktop worse and was reverted.
- `Verified:` a V4-only zero-`lfbMemoryConfig` LFB guard did not fix the distortion and was reverted.
- `Verified:` driver reinstall did not change the guest-visible `8 MB` report before the guest-visible memory fixes.

Working theory:
- `Verified:` `32 MB` detection itself is fixed.
- `Verified:` some visible desktop operations are correct, so the remaining bug is not a uniform scanout failure.
- `Verified:` some sampled `screen_to_screen` copies are faithfully copying zeros from a linear source region into the visible tiled desktop.
- `Inferred:` the strongest current lead is a V4-specific higher-half linear/LFB or source-surface population mismatch.
- `Inferred:` the `0x01d00000` versus `0x00d00000` split is now the best concrete address-level clue.
- `Unknown:` whether the real fix is a V4-only linear/LFB alias/fold, another register/path that should reconcile those addresses, or an upstream surface-population bug that only becomes visible on the `32 MB` path.

Guardrails:
- Label major conclusions as `Verified` / `Inferred` / `Unknown`.
- Keep changes minimal and evidence-backed.
- Update living docs as you work.
- Do not claim anything beyond what is freshly verified.
- Prefer reuse-first over a speculative standalone Voodoo4 rewrite.
- Do not reopen the solved ROM-dispatch theory.
- Do not remove the current temporary tracing until the higher-VRAM mismatch is localized.

Suggested next debugging focus:
1. Trace what populates the zero-filled linear source region seen in `screen_to_screen sample[23]` and `sample[24]`.
2. Compare that source-population path with the separate linear/LFB path writing to `0x01d00000`.
3. Decide whether Voodoo4 linear/LFB translation should fold `0x01d00000` back onto the visible `0x00d00000` tiled desktop base before trying any behavioral fix.
4. Only after that, test the smallest possible V4-only hypothesis change.

Useful commands:
- `cmake --build build -j8`
- `bash scripts/test-voodoo4-blank-boundary.sh`
- launch with logging:
  - `./build/src/86Box.app/Contents/MacOS/86Box -P "$HOME/Library/Application Support/86Box/Virtual Machines/v4" --logfile /tmp/voodoo4-mem-trace.log`
