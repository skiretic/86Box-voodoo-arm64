# SMP / Abit BP6 — Changelog

All notable changes to the SMP implementation are documented here.
Entries are in reverse chronological order.

---

## 2026-03-05

### Phase 3: Dual CPU State — COMPLETE

**Commit**: `b43521a45` — Phase 3: Dual CPU state management (cpu_context_t + context switching)

New files:
- `src/cpu/smp.h` — Standalone cpu_context_t struct definition

Features implemented:
- cpu_context_t with comprehensive per-CPU state: cpu_state, fpu_state, x87 pointers, MSR, TSC, CR2/CR3/CR4, DR[8], GDT/LDT/IDT/TR, segment cache, dynarec status, SMM state, Cyrix CCRs, cache, APIC pointer, HLT/SIPI flags
- cpu_save_context() / cpu_load_context() / cpu_switch_to() functions
- cpu_smp_init() / cpu_smp_close() for lifecycle management
- num_cpus, active_cpu, cpu_contexts[MAX_CPUS] globals
- MACHINE_SMP flag (0x100000000ULL) in machine.h

Modified files:
- `src/cpu/cpu.c` — Context switch functions, SMP globals
- `src/cpu/cpu.h` — cpu_context_t typedef, MAX_CPUS, extern declarations
- `src/include/86box/machine.h` — MACHINE_SMP flag

### Phase 2: I/O APIC (82093AA) — COMPLETE

**Commits**: `2cc2c31ea` — Phase 2: Implement I/O APIC (Intel 82093AA)
`33aa4fee7` — Fix fpu_state_t build error: include x87_sf.h in cpu.h

New files:
- `src/include/86box/ioapic.h` — I/O APIC public header with register definitions

Features implemented:
- Full 82093AA replacing stub: indirect register access (IOREGSEL/IOWIN)
- 24 redirection table entries (64-bit each) with proper bit masking
- All delivery modes: Fixed, Lowest Priority, SMI, NMI, INIT, ExtINT
- Edge-triggered and level-triggered support with Remote IRR tracking
- EOI feedback: ioapic_eoi() clears Remote IRR, re-delivers if line asserted
- PIC coexistence: dispatch layer in picint_common() transparently routes IRQs
- MP table patcher completely removed

Modified files:
- `src/ioapic.c` — Complete replacement of stub with full implementation
- `src/pic.c` — I/O APIC dispatch layer in picint_common()
- `src/cpu/apic.c` — EOI handler calls ioapic_eoi() for level-triggered

### Phase 1: Local APIC — COMPLETE

**Commit**: `13bc0c275` — Phase 1: Implement Local APIC (per-CPU interrupt controller)

New files:
- `src/cpu/apic.c` (~1050 lines) — Full Local APIC implementation
- `src/include/86box/apic.h` (~50 lines) — Public APIC header

Features implemented:
- APIC register map: ID, Version, TPR, PPR, EOI, SVR, LDR, DFR
- 256-bit interrupt vectors: ISR, TMR, IRR with priority evaluation
- LVT entries: Timer, LINT0, LINT1, Error (Thermal/Perf stubbed)
- Virtual wire mode: LINT0 as ExtINT (PIC passthrough), LINT1 as NMI
- APIC timer: one-shot and periodic modes via 86Box timer subsystem
- Timer divide configuration (1/2/4/8/16/32/64/128)
- ICR Low/High registers stubbed (full IPI in Phase 4)
- EOI handling: clears highest-priority ISR bit
- SVR bit 8: global APIC enable/disable

Modified files:
- `src/cpu/cpu.c` — MSR 0x1B (IA32_APIC_BASE) wired: base address remapping, global enable, BSP flag. CPUID leaf 1 EDX bit 9 reports APIC presence.
- `src/cpu/386.c` — Interrupt dispatch augmented: APIC path when enabled, legacy PIC fallback
- `src/cpu/386_dynarec.c` — Same interrupt dispatch augmentation for dynarec path
- `src/cpu/CMakeLists.txt` — Added `apic.c` to build

Validation:
- Slot 1 VM tested: boots normally, no regressions with APIC active in virtual wire mode

Infrastructure:
- BIOS ROM `bp6ru.bin` copied to `roms/machines/bp6/` (gitignored, not tracked)
- `.gitignore` updated for `roms/machines/bp6/`

### Branch Created

**Commit**: `a080d1fb1` (initial, on top of master)

- Branch `smp-bp6` created off `master`
- Pushed to origin with tracking
