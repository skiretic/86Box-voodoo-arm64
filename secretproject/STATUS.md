# SMP / Abit BP6 — Executive Summary

**Branch**: `smp-bp6`
**Started**: 2026-03-05
**Last Updated**: 2026-03-06

---

## Overall Progress

```
Phase 1: Local APIC           [##########] 100%  DONE
Phase 2: I/O APIC (82093AA)   [##########] 100%  DONE
Phase 3: Dual CPU State        [##########] 100%  DONE
Phase 4: SMP Execution & IPI   [########..]  80%  IN PROGRESS (blocked on SIPI timing)
Phase 5: BP6 Machine Def       [##########] 100%  DONE
Phase 6: Polish & Optimization [..........]   0%  NOT STARTED
```

**Overall**: Phases 1-3, 5 complete. Phase 4 ~80% done. APIC MMIO blocker RESOLVED. New blocker: cooperative scheduling timing.

---

## Phase Status Detail

### Phase 1: Local APIC — DONE

| Task | Status |
|------|--------|
| 1.1 New files (apic.c, apic.h) | DONE |
| 1.2 APIC register map (MMIO) | DONE |
| 1.3 Virtual wire mode (LINT0=ExtINT, LINT1=NMI) | DONE |
| 1.4 APIC timer (one-shot + periodic) | DONE |
| 1.5 MSR integration (IA32_APIC_BASE 0x1B) | DONE |
| 1.6 Conditional activation (P6+ CPUs only) | DONE |
| 1.7 Validation | DONE (Slot 1 VM boots normally, no regressions) |
| 1.8 Build integration (CMakeLists.txt) | DONE |

### Phase 2: I/O APIC (82093AA) — DONE

| Task | Status |
|------|--------|
| 2.1 Register map (IOREGSEL/IOWIN) | DONE |
| 2.2 Redirection table entries (24x 64-bit) | DONE |
| 2.3 Interrupt flow (device IRQ -> APIC -> CPU) | DONE |
| 2.4 Legacy PIC coexistence | DONE (dispatch layer in picint_common) |
| 2.5 Remove MP table patcher | DONE |
| 2.6 PIIX4E integration | DONE (transparent via PIC dispatch) |
| 2.7 Validation | DONE (regression test passed — Win2K single CPU) |

### Phase 3: Dual CPU State — DONE

| Task | Status |
|------|--------|
| 3.1 cpu_context_t structure | DONE |
| 3.2 Context switch (save/load/switch_to) | DONE |
| 3.3 Per-CPU globals audit + capture | DONE |
| 3.4 Dynarec considerations | DONE |
| 3.5 Machine flag (MACHINE_SMP) | DONE |
| 3.6 Validation | DONE (regression test passed — Win2K single CPU) |

### Phase 4: SMP Execution & IPI — IN PROGRESS (80%)

| Task | Status |
|------|--------|
| 4.1 Cooperative time-slicing execution loop | DONE (pc_run in 86box.c) |
| 4.2 CPU states during SMP boot | DONE (AP starts in wait_for_sipi) |
| 4.3 IPI delivery via ICR (INIT/SIPI/Fixed) | DONE (apic.c — full IPI engine) |
| 4.4 HLT handling | DONE (opHLT sets halted flag, scheduler skips) |
| 4.5 Shared memory | N/A (cooperative model — no races) |
| 4.6 TSC synchronization | DONE (sync after each CPU slice) |
| 4.7 Dynarec disable for SMP | DONE (force exec386 when num_cpus > 1) |
| 4.8 APIC MMIO accessibility | **DONE** (fixed mem_reset ordering + _mem_state) |
| 4.9 SIPI timing / AP startup | **BLOCKED** — see below |
| 4.10 Validation | BLOCKED on 4.9 |

**What works (as of 2026-03-06)**:
- APIC MMIO at 0xFEE00000 fully accessible (reads AND writes confirmed)
- BIOS programs APIC registers: SVR=0x10F (enabled), LINT0=ExtINT, LINT1=NMI
- BIOS sends SIPI to AP (ICR shorthand=3, vector=0x50)
- AP starts executing at correct SIPI vector (0x50000:0000)
- AP runs and converges to BIOS code (CS:IP = F000:7DA9)
- Single-CPU regression passes (Win2K boots to desktop)

**BLOCKER: Cooperative scheduling timing**:

Both CPUs end up stuck at linear 0xF7DA9 with **IF=0** (interrupts disabled) — this is a BIOS error halt. Root cause:

1. BSP sends SIPI during its 300K-cycle time slice
2. BSP immediately checks for AP response (reads shared memory flag)
3. AP hasn't run yet — it's waiting for its time slice
4. BSP concludes AP is dead → enters error halt with IF=0
5. AP finally runs next time slice, but BSP already gave up

This is a fundamental timing issue with cooperative scheduling and 300K-cycle time slices. On real hardware, the AP starts executing immediately after SIPI. In our model, the AP doesn't run until the BSP yields.

**Fix needed**: Force BSP to yield remaining cycles after SIPI delivery, so the AP gets to run before the BSP continues checking. Options:
- Set `cycles = 0` in the IPI handler after SIPI delivery to force `cpu_exec()` return
- Add `CPU_BLOCK_END()` equivalent in SIPI handler
- Reduce time slice to much smaller value (e.g. 10K cycles like QEMU TCG)

### Phase 5: BP6 Machine Definition — DONE

| Task | Status |
|------|--------|
| 5.1 machine_at_bp6_init() | DONE |
| 5.2 Machine table entry | DONE |
| 5.3 PCI slot layout | DONE (5 PCI + AGP) |
| 5.4 BIOS ROM (bp6ru.bin) | DONE |
| 5.5 PCI slot verification | PENDING |
| 5.6 Validation | PARTIAL (boots to BIOS, SMP detection blocked on timing) |

### Phase 6: Polish & Optimization — NOT STARTED

---

## Bugs Fixed This Session (2026-03-06)

### BUG 5: APIC MMIO mapping destroyed by mem_reset() — FIXED
**Root cause**: Two independent bugs:
1. `machine_init_ex()` calls `cpu_set()` (which creates APIC mapping) BEFORE `mem_reset()` (which zeroes all mappings). The mapping was orphaned from the linked list.
2. `_mem_state[]` was never set to INTERNAL for 0xFEE00000, so even if the mapping survived, `mem_mapping_access_allowed()` would reject INTERNAL mappings when state is zero.

**Fix**: New `apic_reset_mapping()` function (apic.c) that:
- Calls `mem_set_mem_state_both(0xFEE00000, 0x1000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL)`
- Calls `mem_mapping_add()` to re-add BSP's APIC mapping
- Called from `cpu_smp_init()` which runs during machine init (after mem_reset)

**Commit**: c534a3593

---

## Previous Bugs Fixed (2026-03-05)

1. `apic_switch_cpu()` mem_mapping_disable/enable cycle → only swap global `apic` pointer
2. AP APIC never initialized → `cpu_smp_init()` calls `apic_init_cpu(i)` for APs
3. Halved cycle budget → each CPU gets full `total_cycles`
4. APIC MMIO handlers used priv pointer → use global `apic` pointer

---

## Risk Tracker

| # | Risk | Status | Notes |
|---|------|--------|-------|
| R1 | cpu_state global refactor breaks everything | MITIGATED | Copy-in/out works, regression passes |
| R2 | BIOS SMP boot code undocumented requirements | MITIGATED | BIOS programs APIC and sends SIPI correctly |
| R3 | I/O APIC breaks existing interrupt routing | MITIGATED | Transparent dispatch — regression passes |
| R4 | Dynarec incompatible with CPU switching | MITIGATED | Disabled for SMP |
| R5 | TSC desync causes OS scheduler issues | OPEN | TSC sync implemented but untested |
| R6 | Performance unacceptable | OPEN | Full cycle budget per CPU means 2x cycles per timeslice |
| R7 | Wrong PCI slot layout prevents IRQ routing | OPEN | Verify against $PIR in BIOS |
| R8 | Win2000 SMP HAL specific APIC requirements | OPEN | |
| R9 | APIC MMIO mapping not reachable | **RESOLVED** | Fixed: re-add mapping after mem_reset + set _mem_state |
| R10 | Cooperative scheduling timing vs SIPI | **ACTIVE** | BSP checks AP response before AP runs |

---

## BIOS APIC Sequence (observed from logs)

```
1. BIOS enables APIC:   SVR [0F0] = 0x0000010F  (bit 8 = enabled, vector 0x0F)
2. LINT0 = ExtINT:      [350] = 0x00005700       (DM=7=ExtINT, not masked)
3. LINT1 = NMI:         [360] = 0x00005400       (DM=4=NMI, not masked)
4. Set APIC ID:         [020] = 0x00000000       (BSP = ID 0)
5. Clear ESR:           [280] = 0x00000000
6. Send SIPI:           [300] = 0x000CC650       (delmod=6=SIPI, shorthand=3=AllExSelf, vec=0x50)
7. AP starts at 5000:0000 → runs to F000:7DA9
8. BSP also ends at F000:7DA9 with IF=0 (error halt)
```

---

## Commits

| Hash | Description |
|------|-------------|
| ca68ebf74 | Phase 4: IPI delivery in Local APIC (INIT, SIPI, Fixed, NMI) |
| aba0909d7 | Phase 4: SMP execution loop, HLT handling, TSC sync, dynarec disable |
| 09be6b553 | Fix SMP context switch: APIC MMIO mapping and cycle budget |
| a4977bb63 | Phase 5: BP6 machine definition |
| f7aef641d | WIP: APIC MMIO mapping debug + dynarec ordering fix |
| d0f9376f1 | Update STATUS.md with Phase 4/5 progress and APIC MMIO blocker |
| c534a3593 | Fix APIC MMIO mapping destroyed by mem_reset() |
| ab1cfc1f7 | Add diagnostic logging for APIC MMIO and SIPI debugging |
