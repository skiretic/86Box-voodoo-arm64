# NV3 Riva 128 Implementation Changelog

## Phase 2: Core Subsystems

### 2026-02-23 -- Driver Init Hang RESOLVED

**Commits:** `0b1b54ebd`, `a48181e38`, `51a4c7644`

**Summary:**
Fixed the SVGA mode switch hang. The NVIDIA Windows 98 driver now completes
its full init sequence (PMC reset, PTIMER, PGRAPH, PFIFO enable, PIO channel
setup, object binding) and enters normal operation. Windows 98 boots through
the loading screen and the mode switch to 256 colors succeeds without hanging.
Display is blank after mode switch — SVGA scanout not yet implemented.

**Root causes found and fixed:**

1. **PFIFO CACHE1_STATUS (0x003214) returning 0** — This read-only register
   reports cache state: bit 4 = EMPTY (PUT == GET). On a freshly initialized
   cache, EMPTY must be set. The register bank returned 0 ("not empty, not
   full" — impossible state), so the driver polled forever. Fixed by computing
   EMPTY dynamically from PUT/GET pointers. Same fix applied to CACHE0_STATUS
   (0x003014).

2. **PFIFO RUNOUT_STATUS (0x002400) returning 0** — Same bit layout as cache
   STATUS. The driver checks this after CACHE1_STATUS during init. Fixed with
   the same dynamic EMPTY computation from RUNOUT_PUT/GET.

3. **USER space FifoFree (0x800010+) returning 0** — PIO channel submission
   interface. Each subchannel (8KB stride) has a FifoFree register at offset
   0x10 indicating how many 32-bit entries the FIFO can accept. Returning 0
   means "FIFO full" — driver waits forever. Fixed by returning 128 (plenty
   of free space). USER space writes silently accepted (full PFIFO dispatch
   deferred to Phase 3).

**PFIFO register defines added:**
- CACHE0: PUSH0, PUT, STATUS, PULL0, GET
- CACHE1: PUSH0, PUSH1, PUT, STATUS, PULL0, PULL1, GET
- RUNOUT: STATUS, PUT, GET
- RAMHT, RAMFC, RAMRO config registers
- CACHE_STATUS bit defines (RANOUT, EMPTY, FULL)
- USER channel stride (64KB) and FifoFree offset (0x10)

**Current state:** Driver init completes. VPLL writes 26.71 MHz (correct for
640x480). GENERAL_CTRL set to 0x00100710. PIO objects bound to subchannels.
Screen goes blank after Win98 loading screen — expected, needs SVGA scanout.

---

### 2026-02-22 -- MMIO Dispatch Bugfixes

**Commit:** `5566a86fe` -- NV3: Fix MMIO dispatch bugs causing driver init hang on SVGA mode switch

**Summary:**
Fixed five bugs in the NV3 MMIO dispatch that caused the Windows 98 NVIDIA
driver to hang when switching from VGA to 256-color SVGA mode. The driver
was getting stuck in infinite init loops due to corrupted PLL values and
false device detection at unmapped addresses.

**Fixes:**

1. **Unmapped MMIO returns 0** -- Removed mmio_fallback[] store/readback
   array. Reads from addresses outside any known subsystem now return
   0x00000000 instead of echoing back previously written values. The driver
   probed 0x030000-0x03FFFF in tight loops; the old behavior made it think
   a device existed there.

2. **PRAMDAC/PCRTC/PGRAPH/PFIFO register bank sync** -- Writes to dedicated
   fields (PLL coefficients, general_control, cursor_pos, interrupt registers)
   are now mirrored into the regs[] bank array. Previously the driver wrote
   via explicit switch cases but read back through the default bank path,
   seeing stale zeros. PRAMDAC init values also synced at startup.

3. **MMIO trace logging** -- Added ENABLE_NV3_MMIO_TRACE_LOG compile flag
   for detailed R/W trace of every MMIO access (address + value). Disabled
   by default; uncomment the #define to enable.

4. **PTIMER read side-effect fix** -- TIME_0/TIME_1 reads now compute into
   local variables instead of writing back to stored fields. The old
   write-back corrupted state during byte/word RMW sequences.

5. **W1C RMW documentation** -- Added comments documenting the known issue
   where byte/word writes to write-1-to-clear interrupt registers can
   spuriously clear bits in unwritten bytes due to the RMW path.

---

### 2026-02-22 -- Phase 2 Complete

**Commit:** `35688445a` -- NV3: Phase 2 core subsystems - PMC, PTIMER, PFB, PEXTDEV, PRAMDAC, PCRTC

**Summary:**
Implemented all Phase 2 core GPU subsystems. The card now has proper interrupt
routing, PLL clock programming, VBlank interrupts, and SVGA mode infrastructure.
The NVIDIA Windows driver should be able to program SVGA modes through the VPLL
register, and the recalctimings function correctly calculates the pixel clock.

**Subsystems implemented:**

- **PMC (0x000000)** -- Master control with read-only interrupt aggregation
  from all subsystems (PFIFO, PGRAPH, PTIMER, PCRTC, PBUS). INTR_EN_0
  implemented as 2-bit enum per envytools (disabled/hardware/software).
  PCI IRQ routing via pci_set_irq/pci_clear_irq.

- **PTIMER (0x009000)** -- NUMERATOR/DENOMINATOR registers, TIME_0/TIME_1
  64-bit counter, ALARM_0 with interrupt. Interrupt propagates through PMC
  to PCI. (Note: auto-incrementing counter deferred to rivatimer integration.)

- **PFB (0x100000)** -- BOOT_0 now properly read-only (hardware-determined).
  CONFIG_0/CONFIG_1 writable.

- **PEXTDEV (0x101000)** -- Crystal frequency now derived from straps bit 6
  and stored in nv3_t.crystal_freq for use by PLL calculations.

- **PRAMDAC (0x680000)** -- VPLL/NVPLL/MPLL coefficient registers with field
  extraction macros. PLL formula: Freq = (crystal * N) / (M * (1 << P)).
  GENERAL_CONTROL with DAC bit depth and cursor mode fields. CURSOR_START
  register. VPLL writes trigger svga_recalctimings. Default PLL values
  programmed at init for VGA-rate clocks.

- **PCRTC (0x600000)** -- VBlank interrupt via svga->vblank_start callback.
  INTR/INTR_EN registers with write-1-to-clear. CONFIG and START_ADDR.

**recalctimings overhaul:**
- Reads VPLL coefficient register and calculates pixel clock
- Uses standard 86Box formula: svga->clock = (cpuclock * 2^32) / freq
- Sets svga->render to svga_render_{8,16,32}bpp_highres
- Handles 8-bit vs 6-bit DAC mode via svga_set_ramdac_type
- Extended blanking mode for SVGA (vblankstart = dispend, split = 99999)
- Fixed dots_per_clock to 8/16 in extended modes

**Other improvements:**
- RMA (Real Mode Access) window now functional (was stubbed in Phase 1)
- Comprehensive MMIO catch-all logging by subsystem range
- PRAMIN/PFIFO/PGRAPH range-aware default handlers

**Register defines added:**
- PCRTC: INTR, INTR_EN, CONFIG, START_ADDR
- PRAMDAC: NVPLL_COEFF, MPLL_COEFF, VPLL_COEFF, GENERAL_CTRL, CURSOR_START
- PLL field macros: NV3_PLL_M(), NV3_PLL_N(), NV3_PLL_P()
- Crystal constants: NV3_CRYSTAL_FREQ_13500, NV3_CRYSTAL_FREQ_14318
- PMC INTR_EN enum: DISABLED/HARDWARE/SOFTWARE
- PRAMDAC general control bitfield masks
- PTIMER_INTR_ALARM, PCRTC_INTR_VBLANK

**Struct changes:**
- Added nv3_pcrtc_t (intr, intr_en, config, start_addr)
- Expanded nv3_pramdac_t (added cursor_start, renamed fields)
- Added crystal_freq to nv3_t
- Added timer.h include to vid_nv3.h

---

## Phase 1: Device Skeleton + VGA Boot

### 2026-02-22 -- Phase 1 Complete

**Commit:** `fcf945fc1` -- NV3: Phase 1 device skeleton - VGA boot working

**New files:**
- `src/video/nv/vid_nv3.c` -- Main device file (device_t, PCI config, VGA I/O, MMIO stubs)
- `src/video/nv/vid_nv3.h` -- Private header (nv3_t struct, subsystem state structs)
- `src/include/86box/nv/vid_nv3_regs.h` -- Register address defines (PMC, PFB, PEXTDEV, PTIMER, PRAMDAC, PCRTC, PFIFO, PGRAPH, RAMIN, USER)

**Modified files:**
- `src/video/CMakeLists.txt` -- Added `nv/vid_nv3.c` to vid library
- `src/video/vid_table.c` -- Registered Riva 128 PCI + Riva 128 ZX PCI devices
- `src/include/86box/video.h` -- Added extern declarations for nv3_device_pci, nv3t_device_pci

**What works:**
- PCI config space (vendor 0x12D2, device 0x0018/0x0019)
- BAR0 16MB MMIO mapping with stub handlers
- BAR1 16MB linear framebuffer mapping
- ROM/BIOS loading (ELSA Erazor for NV3, reference ROM for NV3T)
- PCI Expansion ROM BAR relocation
- SVGA core initialized with 4MB (NV3) or 8MB (NV3T) VRAM
- VGA I/O intercept with monochrome/color remap
- Extended CRTC registers: REPAINT0 (0x19), REPAINT1 (0x1A), read bank (0x1D), write bank (0x1E), FORMAT (0x25), PIXEL_MODE (0x28), HEB (0x2D)
- Sequencer lock/unlock (index 0x06, write 0x57 to unlock)
- recalctimings hook with extended start address, pixel mode, 10-bit vertical counters
- I2C/DDC GPIO via CRTC register 0x3E
- PMC boot register readback (revision A00/B00/C00)
- PFB boot register with memory size/width/bank config
- PEXTDEV straps (crystal freq, bus type, bus width)
- SVGA MMIO redirect (PRMVIO, PRMCIO, USER_DAC ranges)
- RMA (Real Mode Access) stub

**Test results:**
- DOS boots with VGA text mode (80x25)
- VGA BIOS POST completes successfully
- Windows 98 SE boots to desktop with Standard VGA adapter (640x480, 16 colors)

**Known limitations (expected for Phase 1):**
- No SVGA extended modes (no PLL clock programming yet)
- No hardware cursor
- No 2D or 3D acceleration
- MMIO subsystem registers are stubs (log unknown accesses)
- RMA window reads/writes are stubbed
