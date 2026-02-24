# NV3 Resume Prompt

Paste this to pick up where we left off:

---

Continue NV3 Riva 128 SVGA scanout work. Read `riva128-nv3-plan/STATUS.md` and `riva128-nv3-plan/CHANGELOG.md` for full context.

**Current blocker:** VPLL pixel clock is 5.97 MHz instead of ~25 MHz, causing 14 Hz refresh and distorted display. The BIOS writes VPLL=0x050C (M=12 N=5 P=0) during POST and the Win98 driver never reprograms it. Register offsets are verified correct (VPLL at 0x680508). Framebuffer data IS visible — just wrong timing.

**Investigate:**
1. Why doesn't the BIOS program a correct VPLL frequency? Enable MMIO trace (`ENABLE_NV3_TRACE 1` in vid_nv3.c) and check what addresses the BIOS writes during POST for PLL programming. Maybe it writes to VPLL via byte writes that corrupt the RMW path.
2. Check PLL_COEFF_SELECT (0x68050C) — envytools says bits 25:24 select PCLK_SOURCE (0=VPLL, 1=VIP, 2=XTAL). The driver may expect the crystal oscillator to be the clock source, not VPLL. If so, recalctimings should use crystal_freq directly when PCLK_SOURCE != VPLL.
3. Check if the BIOS writes the real VPLL value to a different address that we're not catching (e.g. through the register bank default path instead of the dedicated VPLL case).

**Key diagnostic data (from /tmp/nv3_log.txt):**
- vtotal=525, htotal=100, dispend=480, hdisp=640, rowoffset=80 (all correct)
- vpll=0x0000050C, crystal=14318180, cpuclock=266666667
- gen_ctrl=0x00000710 (VGA_STATE_SEL=1)
- seqregs[1]=0x01, fmt=0x00, char_width=8

Work directly — do NOT spawn nv3-lead agent (it loops).
