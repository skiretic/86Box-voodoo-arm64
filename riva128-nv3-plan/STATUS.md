# NV3 Riva 128 -- Executive Summary

**Project:** NVIDIA Riva 128 (NV3) GPU emulation for 86Box
**Branch:** `riva128`
**Author:** skiretic
**Started:** 2026-02-22

---

## Current Status

| | |
|---|---|
| **Current Phase** | Phase 2 — SVGA scanout needed |
| **Build** | Compiles clean (zero NV3 warnings) |
| **Boot** | Win98 boots, driver init completes, mode switch succeeds |
| **Display** | VGA working; SVGA mode switch works but screen blank (no scanout) |
| **Acceleration** | None (stubs only) |
| **Blockers** | SVGA display blank after mode switch — needs PCRTC/PRAMDAC scanout |

---

## Phase Progress

| Phase | Description | Status | Key Milestone |
|-------|-------------|--------|---------------|
| 1 | Device Skeleton + VGA Boot | **Complete** | Win98 SE boots with VGA |
| 2 | Core Subsystems (PMC, PTIMER, PFB, PRAMDAC, PCRTC) | **Complete** | PLL clock + VBlank interrupt + IRQ routing |
| 3 | PFIFO + RAMIN | Not started | Command submission works |
| 4 | 2D Acceleration | Not started | Accelerated Windows desktop |
| 5 | 3D Interpreter | Not started | D3D5 games render |
| 6 | ARM64 JIT Codegen | Not started | Fast 3D on Apple Silicon |
| 7 | x86-64 JIT Codegen | Not started | Fast 3D on Intel/AMD |
| 8 | Variants + Polish | Not started | ZX variant, overlay, DMA |

---

## What Works Today

- VGA BIOS POST completes (NVIDIA logo visible)
- DOS boots with 80x25 text mode
- Windows 98 SE installs Standard VGA adapter and boots to desktop
- PCI config space reads back correct vendor/device IDs
- BAR0 (16MB MMIO) and BAR1 (16MB framebuffer) mapped
- ROM loading from `roms/video/nvidia/nv3/`
- Two card variants selectable in 86Box GUI:
  - NVIDIA Riva 128 PCI (4MB, ELSA Erazor BIOS)
  - NVIDIA Riva 128 ZX PCI (8MB, reference BIOS)
- **PMC interrupt aggregation** from all subsystems
- **PCI IRQ routing** (assert/deassert INTA based on INTR_0 & INTR_EN_0)
- **PRAMDAC PLL registers** (VPLL, NVPLL, MPLL) with clock calculation
- **PLL formula**: Freq = (crystal * N) / (M * (1 << P))
- **PCRTC VBlank interrupt** fires at correct rate
- **recalctimings** reads VPLL and programs pixel clock for SVGA modes
- **SVGA rendering** (8bpp, 16bpp, 32bpp) via standard svga_render_*bpp_highres
- **DAC bit depth** (6-bit/8-bit) via PRAMDAC GENERAL_CONTROL
- **RMA (Real Mode Access)** window functional for MMIO reads/writes
- **Crystal frequency** derived from PEXTDEV straps

## What Doesn't Work Yet

- **SVGA scanout after mode switch** -- Driver init completes and writes VPLL=26.71 MHz, GENERAL_CTRL=0x00100710, but screen goes blank. Need recalctimings to pick up NV3 extended mode and render from framebuffer (Phase 2 remaining)
- **Hardware cursor rendering** -- CURSOR_START register exists but no draw callback (Phase 2 remaining)
- **PTIMER time counter increment** -- registers exist but counter is not auto-advancing (needs rivatimer integration)
- **2D acceleration** -- blit, rectangle, GDI text (Phase 4)
- **3D rendering** -- D3D5 textured triangles (Phase 5)

---

## Files

### Source Files
| File | Lines | Purpose |
|------|-------|---------|
| `src/video/nv/vid_nv3.c` | ~1100 | Main device (PCI, VGA I/O, MMIO, subsystems) |
| `src/video/nv/vid_nv3.h` | ~210 | Private header (nv3_t, subsystem structs) |
| `src/include/86box/nv/vid_nv3_regs.h` | ~310 | Register defines (PMC, PFB, PEXTDEV, PTIMER, PRAMDAC, PCRTC) |

### Modified Files
| File | Change |
|------|--------|
| `src/video/CMakeLists.txt` | Added `nv/vid_nv3.c` to build |
| `src/video/vid_table.c` | Registered 2 device_t entries |
| `src/include/86box/video.h` | Added extern declarations |

---

## Architecture Decisions

1. **Single-file monolith** -- All NV3 code in `vid_nv3.c` with subsystem state in `nv3_t`. May split into per-subsystem files if it grows too large.
2. **`svga_t` as first member** -- Standard 86Box pattern for VGA compatibility. Allows `(nv3_t *)svga == nv3`.
3. **Instance-based state** -- No global pointers. All state passed via `void *priv`.
4. **Reference, not fork** -- 86Box-nv used as behavioral reference only. All code written from scratch.
5. **Interpreter first** -- Pure portable C for Phases 1-5. JIT codegen (ARM64/x86-64) layered on top in Phases 6-7.
6. **PMC INTR_0 is read-only** -- On NV3, PMC_INTR_0 is an aggregation of subsystem interrupts, recomputed on every read. Drivers clear interrupts via individual subsystem INTR_0 registers.
7. **PMC INTR_EN_0 is an enum** -- Not a bitmask. Values: 0=disabled, 1=hardware (PCI INTA), 2=software (bit 31).
8. **SVGA override = 0** -- We use the standard SVGA rendering path (svga_render_*bpp_highres) for extended modes, not our own scanline renderer. This matches the Banshee pattern.

---

## Test Environment

- **VM:** Windows 98 SE (existing 86Box VM)
- **ROM (NV3):** `roms/video/nvidia/nv3/VCERAZOR.BIN` (ELSA Erazor, 32KB)
- **ROM (NV3T):** `roms/video/nvidia/nv3/vgasgram.rom` (reference, 32KB)
- **Build:** macOS ARM64, cmake regular preset, NEW_DYNAREC=ON, QT=ON

---

*Last updated: 2026-02-23 -- Driver init hang resolved, SVGA scanout next*
