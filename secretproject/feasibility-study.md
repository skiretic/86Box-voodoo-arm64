# Abit BP6 Motherboard — Feasibility Study for 86Box

## Executive Summary

The Abit BP6 is a dual Socket 370 motherboard (1999) using the Intel 440BX chipset, famous for enabling cheap SMP with paired Celeron Mendocino CPUs. Implementing it in 86Box is **feasible but requires significant core infrastructure work** — specifically, 86Box currently has **zero multi-CPU support**. The board-level components (440BX, PIIX4E, W83977EF, W83782D) are all already implemented. The blocking work is SMP: Local APIC, I/O APIC (real implementation, not the current stub), dual CPU state management, and a multi-CPU execution model.

**Verdict**: The machine definition itself is trivial (< 1 day). SMP infrastructure is a major project (weeks to months). The HPT366 IDE controller is a nice-to-have but can be omitted initially.

---

## 1. Board Hardware Specification

| Component | Part | Status in 86Box |
|-----------|------|-----------------|
| Northbridge | Intel 82443BX (440BX) | Implemented (`src/chipset/intel_4x0.c`) |
| Southbridge | Intel 82371EB (PIIX4E) | Implemented (`src/chipset/intel_piix.c`) |
| I/O APIC | Intel 82093AA | **Stub only** (`src/ioapic.c` — patches out MP tables) |
| Super I/O | Winbond W83977EF | Implemented (`src/sio/sio_w83977.c`) |
| HW Monitor | Winbond W83782D | Implemented (`src/device/hwm_lm78.c`) |
| IDE (extra) | HighPoint HPT366 (UDMA/66) | **Not implemented** |
| CPU Socket | Dual Socket 370 (PPGA) | Single-CPU only today |
| Memory | 3x DIMM, PC100 SDRAM, 768MB max | Implemented (SPD + 440BX DRAM controller) |
| BIOS | Award PnP BIOS (ID: 2A69KA15) | ROMs available from archives |
| AGP | 1x AGP 2x (3.3V) | Implemented (440BX AGP bridge) |
| PCI | 5x PCI 2.1 | Implemented |
| ISA | 2x ISA 16-bit | Implemented |

### Notable Details

- **No onboard audio or network** — users add expansion cards
- **USB 1.1** (2 ports) from PIIX4E — already emulated
- **SoftMenu** (jumperless FSB/multiplier/voltage config) — lives in BIOS ROM, no emulator work needed
- **BIOS versions**: bp6lp (early), bp6nj, bp6qq, bp6ru (final, 2000-05-22). Multiple dumps available from TheRetroWeb, soggi.org, archived Abit FTP

---

## 2. What Already Exists in 86Box

### 2.1 Chipset (Complete)

The 440BX northbridge is fully implemented in `src/chipset/intel_4x0.c` as `i440bx_device` (type `INTEL_440BX = 11`). It handles:
- CPU-to-DRAM mapping (DRB registers, 8-byte units, up to 512MB)
- AGP 2x bridge + GART
- SMRAM
- PCI configuration
- Bus speed detection (66/100 MHz FSB)

The PIIX4E southbridge is in `src/chipset/intel_piix.c` as `piix4e_device`. It provides:
- 2x UDMA/33 IDE channels (SFF8038i bus masters)
- USB 1.1 host controller
- ACPI power management
- SMBus host controller
- RTC/CMOS NVRAM
- PCI-to-ISA bridge with I/O traps
- Port 92 (soft reset)

### 2.2 Existing Socket 370 + 440BX Machines (3 boards)

| Board | Internal Name | Super I/O | Notes |
|-------|---------------|-----------|-------|
| AEWIN AW-O671R | `awo671r` | W83977EF (×2) | Dual Super I/O |
| AmazePC AM-BX133 | `ambx133` | W83977EF | GL518SM HW monitor |
| ASUS CUBX | `cubx` | W83977EF | CMD648 IDE, ICS9250 clock, AS99127F HW monitor |

All defined in `src/machine/m_at_socket370.c`. All are single-CPU.

### 2.3 Machine Init Pattern

Every 440BX Socket 370 board follows this template:

```c
int machine_at_bp6_init(const machine_t *model) {
    int ret = bios_load_linear("roms/machines/bp6/BIOS.bin",
                               0x000c0000, 262144, 0);
    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);
    // ... PCI/ISA slots ...

    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add_params(&w83977_device,
                      (void *)(W83977EF | W83977_AMI | W83977_NO_NVR));
    device_add(&sst_flash_39sf020_device);   // BIOS flash
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);  // 3 DIMMs
    device_add(&w83782d_device);             // HW monitor

    return ret;
}
```

### 2.4 Machine Registration

Machines are registered in `src/machine/machine_table.c` with:
- `.name` / `.internal_name` — display and config names
- `.type` = `MACHINE_TYPE_SOCKET370`
- `.chipset` = `MACHINE_CHIPSET_INTEL_440BX`
- `.init` = init function pointer
- `.cpu` = array of compatible CPU definitions
- Flags like `MACHINE_PCI`, `MACHINE_AGP`, `MACHINE_PIIX4`, etc.

---

## 3. What's Missing: The SMP Problem

This is the hard part. 86Box has **no multi-CPU support whatsoever**.

### 3.1 Current CPU Architecture (Single-CPU Only)

- **Single global state**: `cpu_state_t cpu_state;` in `src/cpu/cpu.c` — one CPU, one set of registers, one context
- **Single execution pointer**: `void (*cpu_exec)(int32_t cycs);` — one execution engine, called once per time slice
- **Single CPU selection**: `CPU *cpu_s;` and `cpu_family_t *cpu_f;` — one CPU model
- **Main loop** (`src/86box.c:2006`): `cpu_exec((int32_t) cpu_s->rspeed / 1000);` — no multi-CPU iteration
- **Thread model**: `__thread int is_cpu_thread = 0;` — one CPU thread

### 3.2 APIC Status: MSR Exists, Nothing Else

- MSR 0x1B (`IA32_APIC_BASE`) can be read/written in `src/cpu/cpu.c` (lines 3231–3235, 4036–4040)
- But the value is **never acted upon** — no MMIO region is mapped, no APIC registers exist
- Pentium Pro/II/III CPUs report APIC support via CPUID (bit 9) but it's cosmetic

### 3.3 I/O APIC Status: MP Table Patcher Only

`src/ioapic.c` contains a device whose **sole purpose** is to search the BIOS area (0xF0000–0xFFFFF) for the `_MP_` and `PCMP` signatures and **patch them out** (overwrite with 0xFF). This prevents Award BIOS SMP initialization code from running on single-CPU emulation. There is no actual I/O APIC register implementation.

A `piix3_ioapic_device` variant exists in `intel_piix.c` but serves the same patching purpose.

### 3.4 What SMP Requires (Ordered by Dependency)

#### Phase A: Local APIC (Per-CPU)

Each CPU needs a Local APIC with MMIO registers at the APIC base (default 0xFEE00000):

| Register | Offset | Purpose |
|----------|--------|---------|
| APIC ID | 0x020 | Unique CPU identifier |
| APIC Version | 0x030 | Feature detection |
| Task Priority (TPR) | 0x080 | Interrupt priority filtering |
| EOI | 0x0B0 | End of interrupt signal |
| Logical Destination | 0x0D0 | Logical addressing |
| Destination Format | 0x0E0 | Flat vs cluster mode |
| Spurious Int Vector | 0x0F0 | Enable/disable APIC |
| ISR/IRR/TMR | 0x100–0x190 | In-service, request, trigger mode |
| ICR (low + high) | 0x300–0x310 | Inter-Processor Interrupts |
| LVT Timer | 0x320 | Local timer interrupt |
| LVT Thermal/Perf/LINT0/LINT1/Error | 0x330–0x370 | Local interrupt sources |
| Timer Init Count | 0x380 | APIC timer |
| Timer Current Count | 0x390 | APIC timer |
| Timer Divide Config | 0x3E0 | APIC timer divisor |

Estimated: ~1000–1500 lines of new code. New files: `src/cpu/apic.c`, `src/include/86box/apic.h`.

#### Phase B: I/O APIC (System-Wide)

Replace the stub in `src/ioapic.c` with a real 82093AA implementation:

| Register | Purpose |
|----------|---------|
| IOREGSEL (0xFEC00000) | Register select |
| IOWIN (0xFEC00010) | Register data window |
| ID, Version | Identification |
| Redirection Table (24 entries) | IRQ → APIC routing |

Each redirection entry maps an IRQ line to: destination APIC(s), vector, delivery mode, trigger mode, polarity, mask.

Estimated: ~500–800 lines. Expand existing `src/ioapic.c`.

#### Phase C: Dual CPU State

```c
// Current (single CPU):
cpu_state_t cpu_state;

// Needed (multi-CPU):
cpu_state_t cpu_states[MAX_CPUS];  // or dynamically allocated
int num_cpus;
int current_cpu;
```

This touches the deepest part of 86Box. `cpu_state` is referenced **everywhere** — every instruction decoder, every memory access, every interrupt handler. Options:

1. **Macro indirection**: `#define cpu_state cpu_states[current_cpu]` — minimally invasive but has performance implications
2. **Pointer swap**: `cpu_state_t *active_cpu = &cpu_states[current_cpu]` — requires changing all references from `cpu_state.` to `active_cpu->`
3. **Context switch on CPU swap**: Copy active state in/out of single `cpu_state` global — ugly but zero code changes to consumers

Option 3 is probably the most practical for 86Box's architecture.

#### Phase D: Multi-CPU Execution Model

Three possible approaches:

1. **Cooperative time-slicing** (simplest, recommended):
   ```c
   for (int i = 0; i < num_cpus; i++) {
       switch_to_cpu(i);        // swap cpu_state
       cpu_exec(cycles / num_cpus);
   }
   ```
   Each CPU gets half the cycle budget per time slice. Simple, deterministic, no threading issues. This is how most emulators (QEMU TCG, PCem) handle SMP.

2. **Threaded** (complex): One pthread per CPU with synchronization barriers. Better performance on multi-core hosts but enormously complex — every shared structure needs locking.

3. **Hybrid**: Time-sliced with per-CPU cycle budgets weighted by workload.

Cooperative time-slicing is strongly recommended for a first implementation.

#### Phase E: IPI (Inter-Processor Interrupts)

The APIC ICR (Interrupt Command Register) must support:
- **INIT**: Reset a CPU (used during SMP boot)
- **SIPI** (Startup IPI): Start a CPU at a specific address (used during SMP boot)
- **Fixed/Lowest Priority**: Route interrupts between CPUs
- **NMI/SMI**: System management

Without IPI, the second CPU can never be started. The OS (Windows NT/2000, Linux) sends INIT + SIPI to boot the Application Processor (AP).

#### Phase F: Memory Coherency

Both CPUs share system RAM. Minimum requirements:
- LOCK prefix on atomic instructions (XCHG, CMPXCHG, etc.) must be visible to both CPUs
- With cooperative time-slicing, this is mostly free (only one CPU runs at a time)
- TLB flushes via IPI (OS sends IPI to flush remote TLB)

#### Phase G: Per-CPU MSRs and TSC

- Each CPU needs its own MSR bank (APIC_BASE, MTRRs, etc.)
- TSC (Time Stamp Counter) should advance proportionally for each CPU
- MTRR writes on one CPU should be visible system-wide

### 3.5 SMP Boot Sequence (What Must Work)

1. BIOS starts CPU 0 (BSP — Bootstrap Processor)
2. BIOS initializes hardware, builds MP table in memory
3. OS loads, reads MP table, discovers CPU 1 (AP — Application Processor)
4. OS sends INIT IPI to CPU 1 via Local APIC ICR
5. OS sends SIPI to CPU 1 with startup vector (real-mode entry point)
6. CPU 1 begins executing at the SIPI vector address
7. CPU 1 enters protected mode, joins the OS scheduler
8. Both CPUs now execute OS threads, interrupts are distributed via I/O APIC

Every step from 3 onward requires new infrastructure.

---

## 4. The HPT366 Question

The HighPoint HPT366 provides 2 additional UDMA/66 IDE channels. It's a PCI device (vendor 0x1103, device 0x0004).

**Can it be omitted?** Yes. The board still has 2 IDE channels from the PIIX4E (UDMA/33). For initial bring-up, this is sufficient. Hard drives and CD-ROMs work fine on UDMA/33.

**Implementation difficulty**: Medium. It's a PCI IDE controller with a known register interface. Linux has a well-documented driver (`drivers/ide/hpt366.c`). Estimated ~500–800 lines.

**Recommendation**: Omit for initial implementation. Add later as an enhancement.

---

## 5. Work Breakdown

### Tier 1: Machine Definition (Trivial — hours)

If SMP already existed, adding the BP6 would be straightforward:
- [ ] Add `machine_at_bp6_init()` to `src/machine/m_at_socket370.c`
- [ ] Add machine entry to `src/machine/machine_table.c`
- [ ] Place BIOS ROM in `roms/machines/bp6/`
- [ ] Wire up: i440bx + piix4e + w83977ef + w83782d + SST flash + SPD
- [ ] Configure PCI slot layout (5 PCI + 1 AGP + northbridge + southbridge)

### Tier 2: SMP Infrastructure (Major — weeks to months)

| Task | Est. Lines | Difficulty | Dependencies |
|------|-----------|------------|--------------|
| Local APIC implementation | 1000–1500 | High | None |
| I/O APIC implementation | 500–800 | Medium | Local APIC |
| Dual CPU state management | 200–500 | High (invasive) | None |
| Multi-CPU execution loop | 100–300 | Medium | CPU state mgmt |
| IPI delivery (INIT/SIPI/Fixed) | 300–500 | High | Local APIC + exec loop |
| Per-CPU MSR/TSC | 200–400 | Medium | CPU state mgmt |
| MP table handling (remove patcher) | 50–100 | Low | I/O APIC |

**Total estimated new code**: ~2500–4000 lines
**Total estimated modified code**: Extensive — `cpu_state` is referenced in hundreds of files

### Tier 3: HPT366 IDE Controller (Optional)

- [ ] PCI device skeleton
- [ ] IDE register interface (PIO + DMA)
- [ ] UDMA/66 timing
- Estimated: 500–800 lines

---

## 6. Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| `cpu_state` is a global used everywhere | High | Context-switch approach (copy in/out) avoids touching consumers |
| APIC timer accuracy affects OS scheduling | Medium | Start with simple countdown, refine later |
| Memory ordering bugs in SMP | Medium | Cooperative time-slicing eliminates most races |
| BIOS MP table incompatibility | Low | Multiple BIOS versions available; Award BIOSes are well-understood |
| Dynarec compatibility with CPU switching | High | May need to flush JIT cache on CPU switch, or use per-CPU code caches |
| Interrupt routing complexity | Medium | Start with simple fixed delivery, add lowest-priority later |
| Performance (two CPUs = half speed each) | Medium | Inherent to cooperative model; acceptable for period-correct workloads |

### Dynarec-Specific Concerns

The new dynarec (`src/codegen_new/`) maintains:
- A block cache keyed by physical address
- Register allocation state
- Dirty page tracking for self-modifying code detection

With dual CPUs:
- Block cache could be shared (both CPUs execute the same x86 code) or per-CPU
- On CPU switch, the dynarec's internal state (host register mappings, pending flags) must be saved/restored
- Block linking (Phase 4 of the dynarec improvements) assumes a single CPU — linked blocks would need to handle CPU switches at link points

**Recommendation**: Initially disable the dynarec for dual-CPU machines (use interpreter). Add dynarec SMP support as a follow-up optimization.

---

## 7. Comparable Emulator Approaches

### QEMU (TCG mode)
- Cooperative round-robin: each vCPU gets a time slice, single-threaded
- Separate `CPUState` per vCPU
- Shared translation cache with per-vCPU dirty tracking
- APIC and I/O APIC fully implemented

### PCem
- Does not support SMP at all (same situation as 86Box)
- Single CPU state, single execution loop

### DOSBox-X
- Limited SMP awareness (some APIC MSR handling) but no actual dual-CPU execution

### 86Box would be closer to QEMU's TCG approach if SMP were implemented.

---

## 8. Recommended Implementation Order

1. **Phase 1 — Local APIC**: Implement APIC registers, MMIO mapping, basic timer. Test with single-CPU boards that have APIC-aware BIOSes (existing Slot 1 boards with Award BIOS).

2. **Phase 2 — I/O APIC**: Replace the MP table patcher with a real 82093AA. Wire interrupt routing from PCI/ISA through I/O APIC to Local APIC.

3. **Phase 3 — Dual CPU state**: Add `cpu_states[]` array, context switch logic, per-CPU MSRs. Test with single CPU first (regression check).

4. **Phase 4 — Execution loop**: Add cooperative time-slicing. Second CPU starts in HALT (wait-for-SIPI state).

5. **Phase 5 — IPI**: Implement INIT/SIPI/Fixed delivery in ICR. This is the moment SMP actually boots.

6. **Phase 6 — BP6 machine definition**: Add the board, wire the components, test with real BIOS ROM.

7. **Phase 7 — HPT366** (optional): Add the extra IDE controller.

---

## 9. Historical Significance

The Abit BP6 (1999) was the first motherboard to support dual Celeron SMP without CPU modification. It exploited the fact that Intel's Mendocino Celeron had a functional Local APIC that Intel hadn't disabled (later steppings removed this). A pair of Celeron 300A CPUs overclocked to 450 MHz on a $130 motherboard could match or beat a $500+ Pentium II system. It became iconic in the Linux and overclocking communities and spawned a dedicated community (bp6.com).

Adding it to 86Box would be a significant milestone — it would be the first multi-CPU machine in the emulator.

---

## 10. BIOS Availability

| Version | Date | Notes |
|---------|------|-------|
| bp6lp | Early | Initial release |
| bp6nj | — | — |
| bp6qq | — | — |
| bp6ru | 2000-05-22 | Final official. Fixes Win2K CPU usage bug |

Sources: TheRetroWeb (6 Award BIOS files), soggi.org, archived Abit FTP (`fae.abit.com.tw`).

**MPS quirk**: BIOS offers MPS 1.1 vs 1.4 selection. MPS 1.4 mode causes Windows 2000 to not see the second CPU — must use MPS 1.1. This is a BIOS-level issue (MP table layout), not an emulator concern.

---

## 11. Conclusion

| Aspect | Assessment |
|--------|-----------|
| Board definition | **Trivial** — all peripherals exist, just wire them up |
| BIOS ROM | **Available** — multiple versions dumped |
| SMP infrastructure | **Major project** — ~2500–4000 new lines, deep core changes |
| Blocking dependency | **Local APIC + I/O APIC + dual CPU state + IPI** |
| HPT366 | **Optional** — board works without it |
| Dynarec impact | **Disable for SMP initially**, add support later |
| Historical value | **High** — first SMP machine in 86Box |

The BP6 itself is easy. SMP is the mountain. But the SMP infrastructure would unlock not just the BP6, but every dual-CPU board from the Pentium Pro through Pentium III era (dual Slot 1 boards, Tyan Tiger, etc.), making it a high-value investment.
