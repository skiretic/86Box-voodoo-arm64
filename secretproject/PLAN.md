# Abit BP6 Implementation Plan

## Overview

This plan covers implementing the Abit BP6 dual Socket 370 motherboard in 86Box, which requires building SMP (Symmetric Multiprocessing) infrastructure from scratch. The work is organized into 6 phases, each independently testable, with clear deliverables and validation criteria.

**Branch**: `smp-bp6` (off `master`)

**Key constraint**: 86Box currently has zero multi-CPU support. The SMP infrastructure (Phases 1–4) is the bulk of the work; the BP6 board definition itself (Phase 5) is trivial once SMP works.

---

## Phase 1: Local APIC — DONE

**Status**: COMPLETE (2026-03-05, commit 13bc0c275)

**Goal**: Implement the Local APIC (per-CPU interrupt controller) as an MMIO device. Test with single-CPU boards first — several existing BIOSes probe the APIC.

**Why first**: Everything else (I/O APIC routing, IPI, SMP boot) depends on having a functional Local APIC. This phase is self-contained and testable on existing single-CPU machines.

### 1.1 New Files

- `src/cpu/apic.c` — Local APIC implementation
- `src/include/86box/apic.h` — Public header

### 1.2 APIC Register Map (MMIO at APIC base, default 0xFEE00000)

Implement the full register set that P6-class CPUs expose:

| Offset | Register | R/W | Priority |
|--------|----------|-----|----------|
| 0x020 | APIC ID | R/W | Must-have |
| 0x030 | APIC Version | R | Must-have |
| 0x080 | Task Priority Register (TPR) | R/W | Must-have |
| 0x090 | Arbitration Priority (APR) | R | Stub (return 0) |
| 0x0A0 | Processor Priority (PPR) | R | Must-have |
| 0x0B0 | End of Interrupt (EOI) | W | Must-have |
| 0x0D0 | Logical Destination (LDR) | R/W | Must-have |
| 0x0E0 | Destination Format (DFR) | R/W | Must-have |
| 0x0F0 | Spurious Interrupt Vector (SVR) | R/W | Must-have (bit 8 = APIC enable) |
| 0x100–0x170 | In-Service Register (ISR) | R | Must-have (256-bit vector) |
| 0x180–0x1F0 | Trigger Mode Register (TMR) | R | Must-have |
| 0x200–0x270 | Interrupt Request Register (IRR) | R | Must-have |
| 0x280 | Error Status Register (ESR) | R | Stub |
| 0x300 | ICR Low | R/W | Must-have (IPI — Phase 4) |
| 0x310 | ICR High | R/W | Must-have (IPI — Phase 4) |
| 0x320 | LVT Timer | R/W | Must-have |
| 0x330 | LVT Thermal | R/W | Stub |
| 0x340 | LVT Performance | R/W | Stub |
| 0x350 | LVT LINT0 | R/W | Must-have (virtual wire mode) |
| 0x360 | LVT LINT1 | R/W | Must-have |
| 0x370 | LVT Error | R/W | Stub |
| 0x380 | Timer Initial Count | R/W | Must-have |
| 0x390 | Timer Current Count | R | Must-have |
| 0x3E0 | Timer Divide Configuration | R/W | Must-have |

### 1.3 Virtual Wire Mode

For backward compatibility with single-CPU systems, the APIC must support "virtual wire" mode:
- LINT0 configured as ExtINT (passes through 8259 PIC interrupts)
- LINT1 configured as NMI
- This is how the BIOS starts before switching to symmetric I/O mode

The existing `picinterrupt()` call in the execution loop (`src/cpu/386_dynarec.c:857`, `src/cpu/386.c:384`) must be augmented:
- If APIC is enabled and SVR bit 8 is set: route through APIC
- If APIC is disabled: use legacy PIC path (current behavior)

### 1.4 APIC Timer

The APIC has a local timer that generates interrupts:
- Countdown from Initial Count at a rate determined by Divide Configuration
- One-shot or periodic mode (bit 17 of LVT Timer)
- Clocked from the CPU bus clock
- Hook into 86Box's existing timer subsystem (`timer_add()`)

### 1.5 MSR Integration

`IA32_APIC_BASE` (MSR 0x1B) already exists in `src/cpu/cpu.c` but is a no-op. Wire it up:
- Bits [35:12] = APIC base address (physical)
- Bit 11 = APIC global enable
- Bit 8 = BSP flag (read-only on AP, set on BSP)
- On write: remap MMIO region to new base address

### 1.6 Conditional Activation

Only enable the APIC for CPU models that have it:
- Pentium Pro and later Intel (already gated by `is_p6`)
- Celeron Mendocino (the BP6's target CPU — critical to verify CPUID reports APIC)
- NOT on earlier Pentiums, Celerons without APIC, or non-Intel CPUs

### 1.7 Validation

- **Test 1**: Boot existing Slot 1 / Socket 370 machines with Award BIOS. BIOS should detect APIC via CPUID and MSR, configure virtual wire mode, and boot normally.
- **Test 2**: Run Windows 98/2000 on single-CPU APIC-enabled machine. OS should see APIC in ACPI tables but behave identically to PIC mode.
- **Test 3**: Verify APIC timer fires interrupts at correct rate.

### 1.8 Estimated Scope

- ~1000–1500 lines new code
- ~50–100 lines modified (MSR wiring, interrupt dispatch)
- Key reference: Intel SDM Vol 3A, Chapter 10 (APIC)

---

## Phase 2: I/O APIC (82093AA) — DONE

**Status**: COMPLETE (2026-03-05, commits 2cc2c31ea, 33aa4fee7)

**Goal**: Replace the stub `src/ioapic.c` with a real Intel 82093AA I/O APIC implementation. This provides system-wide interrupt routing from PCI/ISA devices to Local APICs.

**Why second**: The I/O APIC receives device interrupts and routes them to CPUs via Local APICs. It's the bridge between hardware interrupts and the APIC system. Without it, you can't do APIC-mode interrupts at all — only virtual wire mode.

### 2.1 Register Map (MMIO at 0xFEC00000)

The 82093AA has an indirect register access model:

| Address | Register | Purpose |
|---------|----------|---------|
| 0xFEC00000 | IOREGSEL | Index register (selects which internal register to access) |
| 0xFEC00010 | IOWIN | Data window (reads/writes the selected register) |

Internal registers accessed via IOREGSEL/IOWIN:

| Index | Register | Purpose |
|-------|----------|---------|
| 0x00 | IOAPICID | I/O APIC ID (4-bit, for APIC bus arbitration) |
| 0x01 | IOAPICVER | Version + max redirection entry count |
| 0x02 | IOAPICARB | Arbitration ID |
| 0x10–0x3F | IOREDTBL[0–23] | 24 redirection table entries (64-bit each) |

### 2.2 Redirection Table Entries

Each 64-bit entry controls one IRQ line:

| Bits | Field | Description |
|------|-------|-------------|
| 7:0 | Vector | Interrupt vector (0x10–0xFE) |
| 10:8 | Delivery Mode | Fixed, Lowest Priority, SMI, NMI, INIT, ExtINT |
| 11 | Dest Mode | Physical (0) or Logical (1) |
| 12 | Delivery Status | Idle/Send Pending (read-only) |
| 13 | Pin Polarity | Active High (0) or Active Low (1) |
| 14 | Remote IRR | For level-triggered (read-only) |
| 15 | Trigger Mode | Edge (0) or Level (1) |
| 16 | Mask | 0 = enabled, 1 = masked |
| 63:56 | Destination | Target APIC ID(s) |

### 2.3 Interrupt Flow

```
Device asserts IRQ line (e.g., PCI INTA#)
  → I/O APIC redirection table lookup
    → Delivery mode + destination determines target Local APIC
      → Local APIC sets bit in IRR
        → If priority allows, CPU takes interrupt
```

### 2.4 Legacy PIC Coexistence

The 8259 PIC and I/O APIC must coexist:
- At boot, BIOS uses PIC (virtual wire mode through LINT0)
- OS switches to symmetric I/O mode (I/O APIC routes all interrupts)
- The `picint()` / `picintlevel()` calls throughout 86Box must be augmented with an I/O APIC path
- Create a dispatch layer: if I/O APIC is active for this IRQ, route through APIC; otherwise use PIC

### 2.5 Remove MP Table Patcher

The current `ioapic_write()` patches `_MP_` and `PCMP` table signatures to prevent SMP boot. Once the I/O APIC is real:
- Remove the POST-80h write hook
- Remove the signature patching code
- The BIOS MP tables will remain intact and be read by the OS

### 2.6 PIIX4E Integration

The PIIX4E southbridge routes PCI interrupts (PIRQ A–D) and ISA interrupts through its interrupt steering registers. The I/O APIC sits between the PIIX4E and the Local APICs:
- ISA IRQs 0–15 → I/O APIC inputs 0–15
- PCI PIRQ A–D → I/O APIC inputs 16–19 (typical mapping)
- Wire the existing PIIX4E IRQ routing into the I/O APIC

### 2.7 Validation

- **Test 1**: Boot existing single-CPU boards with I/O APIC enabled. BIOS should configure redirection table. OS should receive interrupts via APIC path.
- **Test 2**: Windows 2000 on single-CPU Slot 1 board — should detect APIC in ACPI/MP table and use symmetric I/O mode.
- **Test 3**: Verify all IRQs still work: keyboard, mouse, disk, timer (IRQ0), RTC (IRQ8).

### 2.8 Estimated Scope

- ~500–800 lines new code (replacing ~100 lines of stub)
- ~200–300 lines modified (interrupt dispatch layer, PIIX4E wiring)
- Key reference: Intel 82093AA I/O APIC datasheet

---

## Phase 3: Dual CPU State Management — DONE

**Status**: COMPLETE (2026-03-05, commit b43521a45)

**Goal**: Allow 86Box to maintain state for two CPUs simultaneously. No second CPU executes yet — this phase is purely structural, ensuring the first CPU continues to work identically.

**Why third**: This is the most invasive change (touches the deepest core structures) but has zero functional change when `num_cpus == 1`. Doing it before execution changes makes regressions easy to isolate.

### 3.1 The cpu_state Problem

`cpu_state` is a single global (`cpu_state_t cpu_state;` in `src/cpu/cpu.c`) referenced by ~30 macros:

```c
#define EAX  cpu_state.regs[0].l
#define cs   cpu_state.seg_cs.base
#define cr0  cpu_state.CR0.l
// ... etc
```

The dynarec generates native code with hardcoded offsets via `cpu_state_offset()`. Changing these macros to pointer indirection would break the dynarec and impose a performance penalty on every instruction.

### 3.2 Approach: Context Switch (Copy In/Out)

**Do NOT change `cpu_state` macros or the dynarec's offset model.**

Instead, maintain a per-CPU state array and copy the active CPU's state into/out of the global `cpu_state`:

```c
// New structures:
typedef struct {
    cpu_state_t  cpu_state;
    fpu_state_t  fpu_state;
    msr_t        msr;
    uint64_t     tsc;
    uint32_t     cr2, cr3, cr4;
    uint32_t     dr[8];
    x86seg       gdt, ldt, idt, tr;
    // ... all per-CPU globals
    apic_state_t apic;          // From Phase 1
    int          halted;        // HLT state
    int          wait_for_sipi; // AP waiting for Startup IPI
} cpu_context_t;

cpu_context_t cpu_contexts[MAX_CPUS];  // MAX_CPUS = 2 (or small constant)
int           num_cpus = 1;
int           active_cpu = 0;
```

Context switch functions:

```c
void cpu_save_context(int cpu_id);   // cpu_state → cpu_contexts[cpu_id]
void cpu_load_context(int cpu_id);   // cpu_contexts[cpu_id] → cpu_state
void cpu_switch_to(int cpu_id);      // save current, load new
```

### 3.3 Per-CPU Globals to Capture

The following globals are per-CPU state and must be saved/restored on context switch:

| Global | Declared In | Notes |
|--------|-------------|-------|
| `cpu_state` | cpu.c | The big one (128 bytes) |
| `fpu_state` | cpu.c | FPU register stack |
| `msr` | cpu.c | Model-specific registers |
| `tsc` | cpu.c | Time stamp counter |
| `cr2`, `cr3`, `cr4` | cpu.c | Control registers |
| `dr[8]` | cpu.c | Debug registers |
| `gdt`, `ldt`, `idt`, `tr` | cpu.c | Descriptor tables |
| `cpu_cur_status` | cpu.c | Dynarec block status flags |
| `pccache`, `pccache2` | cpu.c | Prefetch cache |
| `oldds`, `oldss`, `olddslimit`, `oldsslimit` | cpu.c | Segment cache |
| `opcode` | cpu.c | Current opcode |
| `smi_latched`, `smm_in_hlt`, `smi_block` | cpu.c | SMM state |
| `cpu_end_block_after_ins` | cpu.c | Dynarec control |
| `_cache[2048]` | cpu.c | Internal CPU cache |

### 3.4 Dynarec Considerations

On CPU switch:
- **Flush the block cache** — simplest approach for Phase 3. Both CPUs generate into the same code buffer but with different contexts. Flushing ensures no stale JIT code runs.
- Future optimization: per-CPU block caches (deferred to Phase 6).
- The `cpu_state_offset()` macro remains unchanged — it always refers to the single global `cpu_state`.
- Alternative: disable dynarec for multi-CPU machines initially (use interpreter `exec386()`). This simplifies debugging enormously.

### 3.5 Machine Flag

Add a machine capability flag:

```c
#define MACHINE_SMP  (1 << ...)  // Machine supports multiple CPUs
```

And a runtime variable:

```c
extern int num_cpus;  // 1 for single-CPU, 2 for dual-CPU (set by machine init)
```

### 3.6 Validation

- **Test 1**: All existing single-CPU machines boot and run identically (regression test).
- **Test 2**: `cpu_save_context()` / `cpu_load_context()` round-trip preserves all state — write a diagnostic that saves, loads, and compares.
- **Test 3**: `num_cpus == 1` path has zero performance regression (no context switch calls).

### 3.7 Estimated Scope

- ~300–500 lines new code (`cpu_context_t`, save/load functions)
- ~100–200 lines modified (initialization, machine flag)
- This phase is high-risk due to the number of globals — careful audit required

---

## Phase 4: SMP Execution & IPI

**Goal**: Actually run two CPUs. Implement cooperative time-slicing in the main execution loop and IPI delivery (INIT, SIPI, Fixed) so the OS can boot the second CPU.

**Why fourth**: This is where SMP becomes real. Phases 1–3 provide all the building blocks; this phase wires them together.

### 4.1 Execution Model: Cooperative Time-Slicing

Modify the main loop in `src/86box.c` to alternate between CPUs:

```c
// Current (single CPU):
cpu_exec((int32_t) cpu_s->rspeed / 1000);

// New (multi-CPU):
int cycles_per_cpu = (int32_t) cpu_s->rspeed / 1000;
if (num_cpus > 1)
    cycles_per_cpu /= num_cpus;

for (int i = 0; i < num_cpus; i++) {
    cpu_switch_to(i);

    if (cpu_contexts[i].wait_for_sipi)
        continue;  // AP hasn't received SIPI yet
    if (cpu_contexts[i].halted && !/* pending interrupt */)
        continue;  // CPU in HLT, no interrupt to wake it

    cpu_exec(cycles_per_cpu);
}
```

This gives each CPU an equal share of the time budget. Only one CPU runs at a time — no threading, no races.

### 4.2 CPU States During SMP Boot

| State | BSP (CPU 0) | AP (CPU 1) |
|-------|-------------|------------|
| Power-on | Executes BIOS | Wait-for-SIPI (halted) |
| After BIOS | Running OS | Still wait-for-SIPI |
| After OS sends INIT IPI | Running | Reset, enter wait-for-SIPI |
| After OS sends SIPI | Running | Start executing at SIPI vector |
| Steady state | Running threads | Running threads |

### 4.3 IPI Delivery via ICR

When CPU 0 writes to the ICR (Local APIC offset 0x300/0x310):

```
ICR[10:8] = Delivery Mode:
  000 = Fixed       → deliver interrupt vector to target
  010 = SMI         → deliver SMI to target
  100 = NMI         → deliver NMI to target
  101 = INIT        → reset target CPU, enter wait-for-SIPI
  110 = Startup IPI → target begins execution at (vector × 0x1000) in real mode

ICR[63:56] = Destination APIC ID (physical mode)
ICR[11]    = Destination mode (0 = physical, 1 = logical)
ICR[18]    = Destination shorthand:
  00 = No shorthand (use destination field)
  01 = Self
  10 = All including self
  11 = All excluding self
```

Implementation:

```c
void apic_write_icr(uint32_t icr_low, uint32_t icr_high) {
    int delivery_mode = (icr_low >> 8) & 7;
    int dest_cpu = find_target_cpu(icr_high >> 24, icr_low);

    switch (delivery_mode) {
        case 5: // INIT
            cpu_contexts[dest_cpu].wait_for_sipi = 0;
            cpu_reset_context(dest_cpu); // reset to power-on state
            cpu_contexts[dest_cpu].wait_for_sipi = 1;
            break;
        case 6: // Startup IPI
            if (cpu_contexts[dest_cpu].wait_for_sipi) {
                cpu_contexts[dest_cpu].wait_for_sipi = 0;
                // Set CS:IP to vector * 0x1000 in real mode
                cpu_set_sipi_vector(dest_cpu, icr_low & 0xFF);
            }
            break;
        case 0: // Fixed
            apic_deliver_interrupt(dest_cpu, icr_low & 0xFF);
            break;
        // ... NMI, SMI
    }
}
```

### 4.4 HLT Handling

The `HLT` instruction halts the CPU until an interrupt arrives:
- Set `cpu_contexts[cpu_id].halted = 1`
- In the execution loop, skip halted CPUs unless they have a pending interrupt
- On interrupt delivery to a halted CPU, clear the halted flag

### 4.5 Shared Memory

Both CPUs access the same physical memory (86Box's `ram[]` array). With cooperative time-slicing:
- No simultaneous access — only one CPU runs at a time
- LOCK prefix: already handled by the instruction decoder (ensures atomicity within a single instruction). No additional work needed for cooperative model.
- TLB flush IPI: when one CPU changes page tables, the OS sends an IPI to the other CPU to flush its TLB. Handle in the Fixed IPI delivery path.

### 4.6 TSC Synchronization

Both CPUs should have roughly synchronized TSCs:
- Advance both CPUs' TSC by the cycle count each time either CPU runs
- Or: use a global clock and derive TSC from it

### 4.7 Dynarec Decision

For Phase 4, **disable the dynarec for dual-CPU machines** (force interpreter mode). The dynarec has global state (block cache, register allocator state) that isn't per-CPU-safe yet:

```c
if (num_cpus > 1)
    cpu_exec = exec386;  // interpreter, not exec386_dynarec
```

Dynarec SMP support is deferred to Phase 6.

### 4.8 Validation

- **Test 1**: Single-CPU regression — everything still works.
- **Test 2**: Boot a dual-CPU machine with a BIOS that has MP tables. Observe (via logging):
  - BIOS detects two CPUs via APIC ID
  - OS reads MP table, sends INIT + SIPI to CPU 1
  - CPU 1 starts executing at SIPI vector
- **Test 3**: Windows 2000 or Linux SMP kernel — both CPUs appear in Task Manager / `/proc/cpuinfo`.
- **Test 4**: Run a multi-threaded workload — both CPUs show utilization.

### 4.8 Estimated Scope

- ~500–800 lines new code (execution loop changes, IPI, SIPI, HLT)
- ~100–200 lines modified (main loop, cpu_exec integration)
- High complexity — the SMP boot sequence is finicky and BIOS/OS-specific

---

## Phase 5: BP6 Machine Definition

**Goal**: Add the Abit BP6 as a machine in 86Box with all its hardware components.

**Why fifth**: This is the payoff phase. All SMP infrastructure is in place; we just wire up the board.

### 5.1 New/Modified Files

- `src/machine/m_at_socket370.c` — add `machine_at_bp6_init()`
- `src/machine/machine_table.c` — add machine table entry
- `roms/machines/bp6/` — BIOS ROM file(s)

### 5.2 Machine Init Function

```c
int
machine_at_bp6_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/bp6/bp6ru.bin",
                           0x000c0000, 262144, 0);
    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);

    /* PCI slot layout (from BP6 manual / BIOS PCI listing) */
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);  /* 440BX */
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);  /* PIIX4E */
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);  /* AGP */
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);  /* PCI 1 */
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);  /* PCI 2 */
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);  /* PCI 3 */
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);  /* PCI 4 */
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);  /* PCI 5 */

    device_add(&i440bx_device);                                   /* Northbridge */
    device_add(&piix4e_device);                                   /* Southbridge */
    device_add(&ioapic_device);                                   /* I/O APIC */
    device_add_params(&w83977_device,
                      (void *) (W83977EF | W83977_AMI | W83977_NO_NVR));
    device_add(&sst_flash_39sf020_device);                        /* BIOS flash */
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);                      /* 3 DIMMs */
    device_add(&w83782d_device);                                  /* HW monitor */

    /* SMP: configure 2 CPUs */
    smp_set_num_cpus(2);

    return ret;
}
```

### 5.3 Machine Table Entry

```c
{
    .name          = "[i440BX] Abit BP6",
    .internal_name = "bp6",
    .type          = MACHINE_TYPE_SOCKET370,
    .chipset       = MACHINE_CHIPSET_INTEL_440BX,
    .init          = machine_at_bp6_init,
    .flags         = MACHINE_AGP | MACHINE_PS2 | MACHINE_PIIX4 | MACHINE_SMP,
    // ... CPU family list (Celeron Mendocino, Pentium III Coppermine)
}
```

### 5.4 BIOS ROM

- Source: TheRetroWeb, soggi.org, or archived Abit FTP
- Filename: `bp6ru.bin` (final official BIOS, 2000-05-22)
- Size: 256KB (262144 bytes)
- Place in `roms/machines/bp6/`

### 5.5 PCI Slot Layout Verification

The exact PCI device numbers need to be verified against the real BP6 BIOS. The values above are estimates based on similar Abit 440BX boards. Verification methods:
- Read the BP6 BIOS binary for PCI IRQ routing table ($PIR)
- Compare with Abit BX6/BX133 layouts in 86Box
- Test: wrong slot numbers cause PCI devices to not get IRQs

### 5.6 Validation

- **Test 1**: BP6 appears in machine selection UI under Socket 370.
- **Test 2**: BIOS boots to POST screen, detects both CPUs.
- **Test 3**: Windows 2000 SMP install — detects 2 processors, boots to desktop.
- **Test 4**: Linux SMP kernel — `cat /proc/cpuinfo` shows 2 CPUs.
- **Test 5**: Run a multi-threaded benchmark — both CPUs show load.

### 5.7 Estimated Scope

- ~80–120 lines new code
- ~10–20 lines modified (machine table entry)
- Low complexity — follows established patterns exactly

---

## Phase 6: Polish & Optimization

**Goal**: Address known limitations, add optional hardware, and optimize SMP performance.

### 6.1 Dynarec SMP Support

Enable the JIT compiler for dual-CPU machines:
- Per-CPU block caches (or shared cache with CPU-ID tagging)
- Save/restore dynarec-specific state on CPU switch (block pointers, code buffer position)
- Flush block cache on CPU switch (initial safe approach)
- Investigate: can both CPUs share the same translated blocks? (Yes, if blocks are pure functions of x86 state with no side effects on JIT-internal state)

### 6.2 HPT366 IDE Controller (Optional)

Implement the HighPoint HPT366 as a PCI device:
- PCI vendor 0x1103, device 0x0004
- 2x UDMA/66 IDE channels
- Reference: Linux `drivers/ide/hpt366.c`
- ~500–800 lines
- Add to BP6 init: `device_add(&hpt366_device);`

### 6.3 Additional Dual-CPU Machines

The SMP infrastructure enables other boards:
- **Tyan Tiger 100** (S1836DLUAN) — Dual Slot 1, 440BX
- **ASUS P2B-D** — Dual Slot 1, 440BX
- **Supermicro P6DGE** — Dual Slot 1, 440GX
- **Tyan Thunder 100** — Dual Slot 2, 440GX (Xeon)

Each is a trivial machine definition once SMP works.

### 6.4 Performance Optimization

- Profile the context-switch overhead. If significant, consider:
  - Lazy FPU context switching (only save/restore FPU state on actual FPU use)
  - Skip context switch if only one CPU is active (AP still in HLT)
  - Per-CPU TLB caches

### 6.5 APIC Refinements

- Lowest-priority delivery mode (OS load balancing)
- APIC error handling
- Remote read (rarely used but some OSes probe it)
- NMI delivery and masking

---

## Dependency Graph

```
Phase 1: Local APIC ──────────────┐
                                   ├── Phase 4: SMP Execution & IPI ── Phase 5: BP6
Phase 2: I/O APIC ───────────────┤                                        │
                                   │                                        │
Phase 3: Dual CPU State ──────────┘                                  Phase 6: Polish
```

Phases 1, 2, and 3 can be developed in parallel (independent code). Phase 4 requires all three. Phase 5 requires Phase 4. Phase 6 follows Phase 5.

---

## Risk Registry

| # | Risk | Impact | Likelihood | Mitigation |
|---|------|--------|------------|------------|
| R1 | cpu_state global refactor breaks everything | Critical | Medium | Context-switch approach avoids touching macros |
| R2 | BIOS SMP boot code has undocumented requirements | High | Medium | Test with multiple BIOS versions; add diagnostic logging |
| R3 | I/O APIC breaks existing interrupt routing | High | Medium | Implement as opt-in per machine; legacy PIC path unchanged |
| R4 | Dynarec incompatible with CPU switching | Medium | Low | Disable dynarec for SMP initially (Phase 4.7) |
| R5 | TSC desync causes OS scheduler issues | Medium | Medium | Advance both TSCs proportionally |
| R6 | Performance unacceptable (half speed per CPU) | Medium | Low | Cooperative model is inherently limited; acceptable for era |
| R7 | Wrong PCI slot layout prevents IRQ routing | Low | Medium | Verify against $PIR table in BIOS binary |
| R8 | Windows 2000 SMP HAL has specific APIC requirements | High | Medium | Use MPS 1.1 mode; test with multiple HALs |

---

## Milestone Summary

| Phase | Milestone | Key Deliverable |
|-------|-----------|-----------------|
| 1 | Local APIC works on single-CPU | APIC registers, timer, virtual wire mode |
| 2 | I/O APIC routes interrupts | 82093AA implementation, PIC coexistence |
| 3 | Dual CPU state compiles and runs | cpu_context_t, save/load, zero regression |
| 4 | Second CPU boots via SIPI | Time-slicing, IPI delivery, SMP boot |
| 5 | BP6 boots Windows 2000 SMP | Machine definition, BIOS, 2 CPUs in Task Manager |
| 6 | Dynarec + HPT366 + more boards | Performance, completeness |
