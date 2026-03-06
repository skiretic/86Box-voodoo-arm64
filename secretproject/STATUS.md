# SMP / Abit BP6 — Executive Summary

**Branch**: `smp-bp6`
**Started**: 2026-03-05
**Last Updated**: 2026-03-05

---

## Overall Progress

```
Phase 1: Local APIC           [##########] 100%  DONE
Phase 2: I/O APIC (82093AA)   [##########] 100%  DONE
Phase 3: Dual CPU State        [##########] 100%  DONE
Phase 4: SMP Execution & IPI   [######....]  60%  IN PROGRESS (blocked on APIC MMIO)
Phase 5: BP6 Machine Def       [##########] 100%  DONE
Phase 6: Polish & Optimization [..........]   0%  NOT STARTED
```

**Overall**: Phases 1-3, 5 complete. Phase 4 ~60% done, blocked on BIOS not detecting 2nd CPU.

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

**Key deliverables**: ~1100 lines new code. APIC registers, timer, virtual wire mode, MSR wiring, interrupt dispatch augmented in 386.c and 386_dynarec.c.

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

**Key deliverables**: ~500 lines new code replacing ~100-line stub. Full 82093AA with 24 redirection entries, edge/level trigger, all delivery modes. PIC coexistence via `picint_common()` dispatch. EOI feedback from Local APIC.

### Phase 3: Dual CPU State — DONE

| Task | Status |
|------|--------|
| 3.1 cpu_context_t structure | DONE |
| 3.2 Context switch (save/load/switch_to) | DONE |
| 3.3 Per-CPU globals audit + capture | DONE |
| 3.4 Dynarec considerations | DONE |
| 3.5 Machine flag (MACHINE_SMP) | DONE |
| 3.6 Validation | DONE (regression test passed — Win2K single CPU) |

### Phase 4: SMP Execution & IPI — IN PROGRESS (60%)

| Task | Status |
|------|--------|
| 4.1 Cooperative time-slicing execution loop | DONE (pc_run in 86box.c) |
| 4.2 CPU states during SMP boot | DONE (AP starts in wait_for_sipi) |
| 4.3 IPI delivery via ICR (INIT/SIPI/Fixed) | DONE (apic.c — full IPI engine) |
| 4.4 HLT handling | DONE (opHLT sets halted flag, scheduler skips) |
| 4.5 Shared memory | N/A (cooperative model — no races) |
| 4.6 TSC synchronization | DONE (sync after each CPU slice) |
| 4.7 Dynarec disable for SMP | DONE (force exec386 when num_cpus > 1) |
| 4.8 Validation | BLOCKED — see below |

**What works**:
- SMP execution loop with context switching — BSP boots BIOS, AP correctly waits
- Single-CPU regression passes (Win2K boots to desktop)
- BP6 machine boots to BIOS POST screen with SMP active

**BLOCKER: BIOS sees only 1 CPU**:
- The BP6 BIOS displays "1 CPU" at POST — it doesn't detect the AP
- Root cause: APIC MMIO handler at 0xFEE00000 receives ZERO reads/writes
- The BIOS never accesses the APIC registers, so it never sends INIT/SIPI to the AP
- Tried both `MEM_MAPPING_EXTERNAL` and `MEM_MAPPING_INTERNAL` — neither works
- **Leading theory**: The dynarec was still active (num_cpus=2 set AFTER cpu_set() ran). Last WIP commit moved num_cpus=2 before machine_at_common_init() but NOT YET TESTED
- **Also investigate**: 86Box memory system routing for address 0xFEE00000 — chipset may need to explicitly set `MEM_READ_EXTANY` for the APIC range, OR the APIC mapping may need special treatment since the real Local APIC intercepts accesses at the CPU level (not the memory bus)

**Key bugs found and fixed this session**:
1. `apic_switch_cpu()` was doing `mem_mapping_disable/enable` on every context switch, causing BSP to hang → Fixed: only update global `apic` pointer, no mapping swaps
2. AP APIC (`apics[1]`) was never initialized → Fixed: `cpu_smp_init()` now calls `apic_init_cpu(i)` for APs
3. Halved cycle budget (150K) caused BIOS to get stuck in I/O polling loops → Fixed: each CPU gets full cycle budget (`total_cycles`)
4. APIC MMIO handlers used `priv` pointer from mapping → Fixed: use global `apic` pointer for active CPU

### Phase 5: BP6 Machine Definition — DONE

| Task | Status |
|------|--------|
| 5.1 machine_at_bp6_init() | DONE |
| 5.2 Machine table entry | DONE |
| 5.3 PCI slot layout | DONE (5 PCI + AGP) |
| 5.4 BIOS ROM (bp6ru.bin) | DONE |
| 5.5 PCI slot verification | PENDING |
| 5.6 Validation | PARTIAL (boots to BIOS, SMP detection blocked) |

**Key deliverables**: Full BP6 machine definition in m_at_socket370.c, machine table entry, forward declaration in machine.h. Uses i440BX, PIIX4E, I/O APIC, W83977EF, W83782D HW monitor, SST flash.

### Phase 6: Polish & Optimization — NOT STARTED

---

## Dependency Graph

```
Phase 1: Local APIC [DONE] ----+
                                |
Phase 2: I/O APIC [DONE] ------+--> Phase 4: SMP Exec [BLOCKED] --> Phase 6: Polish
                                |         |
Phase 3: Dual CPU State [DONE] -+    Phase 5: BP6 [DONE]
```

Phase 4 validation blocked on APIC MMIO accessibility.

---

## Key Files

| File | Purpose | Phase |
|------|---------|-------|
| `src/cpu/apic.c` | Local APIC + IPI delivery | 1, 4 |
| `src/include/86box/apic.h` | APIC public header | 1 |
| `src/ioapic.c` | I/O APIC (82093AA) | 2 |
| `src/include/86box/ioapic.h` | I/O APIC public header | 2 |
| `src/pic.c` | PIC with I/O APIC dispatch layer | 2 |
| `src/cpu/cpu.c` | CPU state, MSR, context switch, SMP init | 1, 3, 4 |
| `src/cpu/cpu.h` | cpu_context_t, cpu_has_pending_interrupt | 3, 4 |
| `src/cpu/x86_ops_misc.h` | HLT sets halted flag for SMP | 4 |
| `src/86box.c` | Main execution loop with SMP time-slicing | 4 |
| `src/machine/m_at_socket370.c` | BP6 machine definition | 5 |
| `src/machine/machine_table.c` | BP6 machine registration | 5 |
| `src/include/86box/machine.h` | MACHINE_SMP flag, BP6 decl | 3, 5 |

---

## Risk Tracker

| # | Risk | Status | Notes |
|---|------|--------|-------|
| R1 | cpu_state global refactor breaks everything | MITIGATED | Copy-in/out works, regression passes |
| R2 | BIOS SMP boot code undocumented requirements | **ACTIVE** | BIOS doesn't detect 2nd CPU — APIC MMIO not reachable |
| R3 | I/O APIC breaks existing interrupt routing | MITIGATED | Transparent dispatch — regression passes |
| R4 | Dynarec incompatible with CPU switching | MITIGATED | Disabled for SMP, but ordering bug existed (now WIP fix) |
| R5 | TSC desync causes OS scheduler issues | OPEN | TSC sync implemented but untested |
| R6 | Performance unacceptable | OPEN | Full cycle budget per CPU means 2x cycles per timeslice |
| R7 | Wrong PCI slot layout prevents IRQ routing | OPEN | Verify against $PIR in BIOS |
| R8 | Win2000 SMP HAL specific APIC requirements | OPEN | |
| R9 | APIC MMIO mapping not reachable by CPU | **ACTIVE** | Neither INTERNAL nor EXTERNAL works — need to investigate 86Box mem system |

---

## Commits This Session

| Hash | Description |
|------|-------------|
| ca68ebf74 | Phase 4: IPI delivery in Local APIC (INIT, SIPI, Fixed, NMI) |
| aba0909d7 | Phase 4: SMP execution loop, HLT handling, TSC sync, dynarec disable |
| 09be6b553 | Fix SMP context switch: APIC MMIO mapping and cycle budget |
| a4977bb63 | Phase 5: BP6 machine definition |
| f7aef641d | WIP: APIC MMIO mapping debug + dynarec ordering fix |
