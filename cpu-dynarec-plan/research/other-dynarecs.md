# Other Dynarec Implementations: Research Audit

Comprehensive comparison of x86-to-ARM64 (and related) JIT emulators, evaluated against
86Box's "new dynarec" (`src/codegen_new/`).

**Date**: 2026-03-03
**Author**: cpu-arch research agent

---

## Table of Contents

1. [86Box Baseline (Current State)](#1-86box-baseline)
2. [box64](#2-box64)
3. [FEX-Emu](#3-fex-emu)
4. [QEMU TCG](#4-qemu-tcg)
5. [Rosetta 2](#5-rosetta-2)
6. [Dolphin (GameCube/Wii)](#6-dolphin)
7. [RPCS3 (PS3)](#7-rpcs3)
8. [Comparative Tables](#8-comparative-tables)
9. [Key Takeaways for 86Box](#9-key-takeaways-for-86box)
10. [Sources](#10-sources)

---

## 1. 86Box Baseline

Before comparing external emulators, here is a summary of 86Box's current dynarec
architecture so every comparison is grounded.

### 1.1 IR Design

86Box uses a UOP-based (micro-operation) intermediate representation. The pipeline has
three stages:

1. **x86 decode -> UOPs** (`codegen_ops_*.c`): Guest x86 instructions are decoded and
   translated into platform-independent micro-operations. Each `codegen_ops_*.c` file
   handles a family of x86 instructions.
2. **IR processing** (`codegen_ir.c`): Single-pass compilation. UOPs are stored in a
   linear array (`ir_data_t.uops[]`). Limited optimization: register renaming for MOV
   elimination, dead-value MOV_IMM elision (write-imm-to-memory shortcut), loop
   unrolling via `codegen_ir_set_unroll()`.
3. **Backend lowering** (`codegen_backend_arm64_uops.c` / `codegen_backend_x86-64_uops.c`):
   Each UOP type dispatches to a handler function that emits native instructions.

**Key characteristics**:
- Flat UOP array, no SSA form
- No multi-pass optimization (no constant propagation, no dead code elimination
  beyond the MOV_IMM special case)
- Register allocation is interleaved with code emission in a single pass through
  `codegen_reg.c`
- Jump targets resolved via linked lists within the UOP array
- UOP types encode metadata in flag bits (BARRIER, ORDER_BARRIER, JUMP, JUMP_DEST,
  PARAMS_REGS, PARAMS_IMM, PARAMS_POINTER)

### 1.2 Flag Handling

86Box uses a **deferred/lazy flag evaluation** scheme:
- Flag operands are stored in `cpu_state.flags_op`, `flags_res`, `flags_op1`, `flags_op2`
- These are mapped to IR registers: `IREG_flags_op`, `IREG_flags_res`, etc.
- Flags are only materialized when read (e.g., by a conditional branch or PUSHF)
- `codegen_accumulate.c` implements **cycle accumulation** (not flag accumulation
  despite the name) -- it batches cycle counter decrements into a single ADD at block end

**Limitations**: No backward dataflow analysis to determine which flags are actually
needed. Every flag-setting instruction updates the deferred-flag IR registers. The flag
materialization cost is paid at every conditional branch, even if only one flag is needed.

### 1.3 Self-Modifying Code Detection

86Box uses **page-level dirty tracking**:
- Each page has `code_present_mask`, `dirty_mask`, and finer `byte_dirty_mask[64]`
- When a write hits a page with compiled code, `codegen_check_flush()` invalidates
  all blocks whose page masks overlap with the dirty mask
- A "dirty list" of recently-evicted blocks (max 64) preserves some history for
  blocks that are frequently invalidated by SMC
- Block deletion removes from the hash table and page lists, frees the code buffer

### 1.4 Block Linking

- Blocks are stored in a hash table (`codeblock_hash[]`) indexed by physical address
- No direct block-to-block linking (no jump patching between compiled blocks)
- Every block exit returns to the dispatcher, which looks up the next block
- This is a significant performance gap versus other dynarecs

### 1.5 ARM64 Backend

- Uses callee-saved registers X19-X28 (10 GPRs) and V8-V15 (8 FP/NEON regs)
- Emits individual ARM64 instructions via `host_arm64_*()` helper functions
- Uses CSEL (CC, EQ, VS variants), CBNZ, shifted register operands (LSR/LSL in
  ADD/CMP), BFI for sub-register insertion
- NEON used for MMX/SSE emulation (CMGT, CMEQ, ADD_V, etc.)
- Memory access goes through helper routines (`codegen_mem_load_*` / `codegen_mem_store_*`)
  that handle TLB lookup and alignment
- No fastmem (signal-handler-based memory access optimization)
- No LDP/STP pairing optimization for register spills

---

## 2. box64

**Project**: [github.com/ptitSeb/box64](https://github.com/ptitSeb/box64)
**Type**: Linux userspace x86-64 emulator, ARM64/RISC-V/LoongArch
**Closest to 86Box's use case**: Yes (x86-64 -> ARM64 translation)

### 2.1 IR Design

box64 does **NOT** use a traditional IR. It is a "splatter JIT" -- x86 instructions are
translated directly to native ARM64 instructions without an intermediate representation.
The translation is organized into **4 passes** over the same x86 instruction stream,
each pass compiled with different C macro definitions:

- **Pass 0**: Count x86 instructions, set up per-instruction metadata (dynablock
  structure), first estimate of native instruction count, record jumps, flags set/used,
  barriers.
- **Pass 1** (flag propagation): Backward iteration using **Kildall's algorithm** to
  determine which flags are actually required by subsequent instructions. Also resolves
  jump targets (same-block vs cross-block linking).
- **Pass 2** (`native_pass2`): Final count of native ARM64 instructions to allocate the
  exact code buffer size. Also computes NEON register cache allocations.
- **Pass 3** (code emission): Actual ARM64 code generation with the final flag and
  register allocation information.

**Comparison with 86Box**: box64's approach avoids IR overhead entirely. The 4-pass
design is clever -- it reuses the same code path (decoder function) with different
compile-time macros for each pass. This is more complex to maintain but avoids the
memory and indirection costs of an IR. 86Box's single-pass IR approach is simpler but
misses optimization opportunities that box64's multi-pass design captures.

### 2.2 Optimization Passes

- **Kildall's algorithm** for backward flag propagation (Pass 1) -- this is the
  single biggest optimization in box64
- **Big block construction** (`BOX64_DYNAREC_BIGBLOCK`): Can build across basic block
  boundaries, creating larger translation units. Three modes: 0=small blocks,
  1=big blocks (default), 2=biggest possible (even across overlapping blocks)
- **No constant propagation or dead code elimination** beyond flag elimination
- The optimization strategy is pragmatic: fast compilation, good-enough code

### 2.3 Flag Handling

This is box64's most sophisticated optimization area:

- **Kildall's algorithm**: Backward dataflow analysis determines exactly which flags
  each instruction needs to compute. If SF is never read before being overwritten,
  the instruction that would set SF can use a non-flag-setting variant.
- **Native Flags** (`BOX64_DYNAREC_NATIVEFLAGS`, enabled by default since v0.3.2):
  ARM64's NZCV flags are used directly to represent x86 EFLAGS where possible.
  When an x86 CMP is followed by a Jcc, the ARM64 CMP + Bcc can execute without
  any flag materialization. Documented **30% speedup on 7zip benchmark**.
- **Partial flag handling**: x86 instructions that set only some flags (e.g., INC
  doesn't set CF) are handled by preserving the unaffected flag bits.
- **Undefined flag optimization**: Undefined flags are handled to match real CPU
  behavior for compatibility, but only when actually needed.

**Comparison with 86Box**: box64's backward flag analysis is a massive advantage. 86Box
always stores all four flag components (op, res, op1, op2) for every flag-setting
instruction. box64 determines which flags are dead and skips their computation entirely.
The "native flags" feature goes further by keeping flags in ARM64 NZCV register.

### 2.4 Self-Modifying Code Detection

- mprotect()-based page protection for detection
- JIT code and self-modifying code interaction is handled by invalidating dynablocks
  when writes to code pages are detected
- `BOX64_DYNAREC_CALLRET` optimization for call/ret prediction interacts with SMC
  handling (CALLRET=2 mode improves compatibility with SMC code)
- "Waiting slot" mechanism for code that frequently modifies itself

**Comparison with 86Box**: Similar page-level approach. 86Box's dirty-list mechanism
(64-block history) for frequently-invalidated blocks is a nice touch that box64 doesn't
specifically document.

### 2.5 Block Linking

- Direct block chaining via jump patching: when one block jumps to another, the
  jump instruction is patched to go directly to the target block
- Big-block mode can merge multiple basic blocks into a single translation unit
- Cross-block jumps that leave the current block require linking

**Comparison with 86Box**: box64 has block linking; 86Box does not. This is one of the
most impactful performance differences.

### 2.6 ARM64-Specific Optimizations

- **NEON register caching**: x86 XMM/YMM registers are cached in NEON registers
  across instructions within a block
- **Full AVX2 emulation** via NEON (no SVE required)
- **Strong memory model emulation** (`BOX64_DYNAREC_STRONGMEM`): Configurable levels
  of memory barriers for x86 TSO emulation (0=none, 1=store barriers, 2=SIMD barriers,
  3=aggressive, 4=QEMU-style full TSO)
- **Conditional execution**: ARM64 CSEL and conditional branches used where possible
- **Barrel shifter**: Used for EA calculations (shifted register operands in ADD)

### 2.7 Performance

- 5-10x faster than interpreter alone
- Native flags: **30% improvement** on CPU-intensive benchmarks (7zip)
- Competitive with FEX-Emu on most workloads
- Strongmem=0 (no barriers) is fastest but may cause issues with threaded code

---

## 3. FEX-Emu

**Project**: [github.com/FEX-Emu/FEX](https://github.com/FEX-Emu/FEX)
**Type**: Linux userspace x86/x86-64 emulator for ARM64
**Closest to 86Box's use case**: Yes

### 3.1 IR Design

FEX-Emu uses a **full SSA (Static Single Assignment) IR** with multiple optimization
passes. This is the most sophisticated IR among the emulators studied:

- x86 instructions are decoded into SSA IR nodes
- The IR is defined in `IR.json` (machine-readable specification)
- SSA form enables standard compiler optimization passes
- Multi-pass pipeline: decode -> optimize -> register allocate -> code emit
- IR supports 256-bit operations natively (for AVX/AVX2)

**Comparison with 86Box**: FEX's IR is dramatically more sophisticated. 86Box's UOP
array is a flat, non-SSA representation with no graph structure. FEX's SSA form
enables optimizations that are impossible in 86Box's current design.

### 3.2 Optimization Passes

FEX implements several IR optimization passes:

- **Dead Code Elimination (DCE)**: Removes IR nodes whose results are never used.
  The hashmap originally used for DCE was later removed for performance.
- **Constant Propagation/Simplification**: Constants are folded through arithmetic.
- **x87 Stack Optimization Pass**: A very extensive pass that removes x87 stack
  management overhead. This "causes significant performance improvements in x87
  heavy games because all the x87 stack management typically gets deleted."
- **Register Access Optimization**: Originally a pass that converted generic register
  accesses to static register allocator accesses. Later made direct at decode time,
  removing ~12% JIT compilation overhead.
- **Dead Flag Calculation Elimination**: x86 flag computations whose results are
  never consumed before being overwritten are removed from the IR.

**Comparison with 86Box**: FEX has a full compiler optimization pipeline. 86Box has
essentially no optimization passes (the MOV-elimination and MOV_IMM-to-memory shortcuts
in `codegen_ir_compile()` are peephole special cases, not general optimization). The
x87 stack optimization pass is particularly relevant since 86Box emulates x87 for the
same era of CPUs.

### 3.3 Flag Handling

- x86 flags are efficiently mapped to ARM64 NZCV where possible
- The goal is to keep flags in the host flag register as long as possible
- Dead flag calculation elimination pass removes flag computations that are never consumed
- SF, ZF, CF, OF map relatively cleanly to ARM64 N, Z, C, V
- PF (parity flag) and AF (auxiliary flag) require special handling as ARM64 has no
  equivalent -- these are computed in GPRs when needed
- Flags are represented as explicit SSA values in the IR, enabling precise liveness
  analysis

**Comparison with 86Box**: FEX's SSA-based flag handling is more precise than 86Box's
deferred evaluation. 86Box's approach stores all flag operands; FEX can eliminate dead
flag computations entirely. However, FEX's approach requires more JIT compilation time.

### 3.4 Self-Modifying Code Detection

- Page-level tracking integrated with the frontend decoder
- When memory syscalls modify mapped regions, the virtual memory tracking system
  detects this and invalidates affected code caches
- Frontend can detect code modifications during decode and discard partial compilations
- SMC support for specific games (Peggle Deluxe, Crysis 2 Maximum Edition) demonstrated
  the need for robust detection

**Comparison with 86Box**: Similar page-level approach. FEX's integration with the
frontend decoder for mid-decode invalidation is more aggressive. 86Box doesn't detect
modifications during decode.

### 3.5 Block Linking

- Not extensively documented, but FEX supports block chaining
- **Shared code buffers** (FEX-2506, mid-2025): All JIT code is placed in a shared
  buffer accessible by all threads. If any thread has JITed a code region, all other
  threads can use it. This gave a **25% reduction in JIT time**.
- **Code caching to filesystem**: Work-in-progress feature to persist JIT output across
  program executions (AOT-like caching)
- **Call-return stack optimization**: FEX takes advantage of ARM64's own call-return
  prediction hardware by translating x86 CALL/RET to ARM64 BL/RET.

**Comparison with 86Box**: FEX's shared code buffers and filesystem caching are far
beyond 86Box's scope (86Box is single-threaded CPU emulation). However, the call-return
prediction optimization is directly applicable.

### 3.6 ARM64-Specific Optimizations

- **Static register allocation**: x86 GPRs and XMM registers are statically mapped to
  ARM64 registers. The register order was later adjusted to match Arm64EC ABI for
  better interop (less shuffling when calling out to native code).
- **AVX/AVX2 via NEON**: All 256-bit operations implemented using 128-bit NEON with
  pairs of registers (or native SVE 256-bit on supported hardware).
- **SSE -> NEON mapping**: MMX and SSE1/SSE2 map relatively well to NEON. SSSE3+
  becomes expensive (e.g., `pmaddubsw` on XMM requires ~10 NEON opcodes).
- **memcpy/memset optimization**: Specialized implementations improved throughput from
  2-3 GB/s to 88 GB/s.

**Comparison with 86Box**: FEX's static register allocation is more direct than 86Box's
dynamic allocation. 86Box already uses NEON for MMX/SSE but doesn't document SSSE3+
overhead. The memcpy/memset optimization is irrelevant for 86Box (different use case).

### 3.7 Performance

- Competitive with box64 on most benchmarks
- x87 stack optimization: "significant performance improvements" in x87-heavy games
- Register access optimization: 12% JIT compilation time reduction
- Shared code buffers: 25% JIT time reduction
- Call-return prediction: "majority of performance uplift" came from this optimization

---

## 4. QEMU TCG

**Project**: [qemu.org](https://www.qemu.org/)
**Type**: Multi-architecture system/user emulator
**TCG**: Tiny Code Generator -- QEMU's JIT backend

### 4.1 IR Design

QEMU TCG uses a **micro-operation IR** (TCG ops) as an intermediate representation:

- **Two-stage translation**: Guest instructions -> TCG IR ops -> host instructions
- `gen_intermediate_code()` translates guest instructions to TCG ops (frontend)
- `tcg_gen_code()` translates TCG ops to host instructions (backend)
- TCG ops are a register-transfer-language (RTL)-like notation
- Uses "TCG temps" as virtual registers (similar to SSA values but not true SSA)
- IR is designed to be minimal and portable across all host architectures
- Separate frontends for each guest architecture, separate backends for each host

**Comparison with 86Box**: QEMU's IR is more principled (clean separation of
frontend/backend with a well-defined IR contract) but less optimized than FEX's SSA.
86Box's UOP IR is conceptually similar to TCG ops but less formalized.

### 4.2 Optimization Passes

TCG applies optimizations primarily at the basic-block level:

- **Liveness analysis**: Determines which TCG temps are live at each point. Dead values
  are eliminated.
- **Dead code elimination**: Instructions computing dead results are removed.
  "Especially useful for condition code optimization in QEMU."
- **Single-instruction simplification**: Algebraic simplification of individual ops
  (e.g., `x + 0 -> x`, `x & 0xffffffff -> x`)
- **Move coalescing**: Suppresses moves from a dead variable to another
- **`tcg_optimize()`**: The main optimization function that applies these passes
- **No multi-block optimization**: Optimizations are strictly intra-block

**Comparison with 86Box**: QEMU has more optimization than 86Box (liveness analysis,
DCE) but less than FEX (no SSA, no multi-block). 86Box should at minimum implement
basic liveness analysis and DCE.

### 4.3 Flag Handling

QEMU's flag handling strategy varies by guest architecture:

- **Lazy evaluation** for x86, m68k, CRIS, SPARC: Flag state is encoded as
  (operation, operand1, operand2, result) and only materialized on demand
- The lazy scheme is "important for CPUs where every instruction sets the condition
  codes" (x86)
- Flags are stored as TCG globals (not in host flags register)
- "Condition codes, delay slots on SPARC, conditional execution on Arm... are
  rarely accessed directly by the program and/or change very frequently" -- so
  they are kept in memory and not flushed until block boundaries

**Comparison with 86Box**: QEMU's lazy flag scheme is essentially identical to 86Box's
approach (store op + operands, compute on demand). Neither has backward dataflow
analysis to eliminate dead flag computations (which box64 and FEX both have). QEMU's
liveness analysis does catch some dead flag computations at the IR level.

### 4.4 Self-Modifying Code Detection

Two distinct approaches depending on emulation mode:

- **User mode**: Mark host pages as write-protected with `mprotect()`. SEGV signal
  handler fires on write, invalidates all TBs on the page, re-enables write access.
- **System mode (softmmu)**: Software TLB with dirty-page tracking. Each page
  maintains a linked list of all TBs it contains. Write access triggers TB
  invalidation via the linked list.
- **Block chaining undo**: When a TB is invalidated, all blocks that chain into it
  must have their jump patches reversed to return to the dispatcher.

**Comparison with 86Box**: Very similar to 86Box's approach. 86Box's system is
equivalent to QEMU's softmmu approach (dirty masks per page, linked lists of blocks).
QEMU's chain-undo mechanism is more complex because it has block linking (86Box doesn't).

### 4.5 Block Linking

QEMU has sophisticated block chaining:

- **Direct block chaining**: The end-of-block jump is patched to go directly to the
  next translated block, bypassing the dispatcher
- **Same-page restriction**: "Chaining is only performed when the destination of the
  jump shares a page with the basic block that is performing the jump"
- **Zero-overhead chaining on x86**: "On some host architectures (such as x86 or
  PowerPC), the JUMP opcode is directly patched so that the block chaining has no
  overhead"
- **`tcg_gen_lookup_and_goto_ptr()`**: For indirect jumps, emits code to look up and
  jump to the next TB without returning to the main loop
- **Unchaining for invalidation**: When a TB is invalidated, all blocks chaining to
  it are unpatched

**Comparison with 86Box**: QEMU's block chaining is a critical optimization that 86Box
completely lacks. The same-page restriction is a pragmatic safety measure. The
unchaining mechanism is the price paid for block linking when SMC invalidation occurs.

### 4.6 ARM64-Specific Optimizations

- TCG's ARM64 backend is relatively straightforward
- Uses ARM64 conditional branches (B.cond)
- No documented LDP/STP pairing optimization
- No signal-handler-based fastmem in TCG (uses softmmu helper calls)
- The backend is designed for correctness and portability, not maximum ARM64 performance

**Comparison with 86Box**: QEMU's ARM64 backend is similar in sophistication to 86Box's.
Neither takes full advantage of ARM64-specific features like LDP/STP pairing or
signal-handler fastmem.

### 4.7 Performance

- Correct but slow compared to specialized translators
- A 2025 paper demonstrated a direct-translation proof-of-concept that was **35x faster
  than QEMU TCG**, indicating substantial overhead from the IR model
- QEMU prioritizes correctness and architecture coverage over raw speed
- box64 and FEX are 5-10x faster than QEMU for x86-64 workloads

---

## 5. Rosetta 2

**Project**: Apple's proprietary x86-64 to ARM64 binary translator
**Type**: Hybrid AOT + JIT translator
**Relevance**: Shows what is possible with hardware support

### 5.1 Translation Design

Rosetta 2 uses a fundamentally different approach from all open-source emulators:

- **Ahead-of-Time (AOT) translation**: The entire text segment is translated from
  x86-64 to ARM64 when an application is first installed. AOT files are Mach-O
  binaries with a custom load command (`LC_AOT_METADATA`, command number 0xcacaca01).
- **JIT fallback**: For dynamically-generated code (e.g., JavaScript JITs, .NET CLR),
  Rosetta 2 has a JIT mode that translates x86-64 code pages on demand.
- **Kernel integration**: The kernel identifies x86-64 Mach-O files early in the exec
  path and transfers control to a Rosetta translation stub instead of dyld.

**Comparison with 86Box**: AOT translation is not applicable to 86Box (we're emulating
a full system with arbitrary code execution). The JIT fallback is conceptually similar
to any block-at-a-time JIT.

### 5.2 Optimization Techniques

Rosetta 2 implements several sophisticated optimizations:

- **Unused-flags optimization**: Backward analysis to identify x86 instructions whose
  flag results are never consumed. "The vast majority of flag-setting x86 instructions
  can be translated to non-flag-setting ARM instructions with no fix-up required."
- **Return-address prediction**: x86 CALL/RET are translated to ARM64 BL/RET to take
  advantage of the ARM CPU's hardware return-address stack. This is critical for
  branch prediction performance.
- **Function prologue/epilogue fusion**: Multiple PUSH instructions in a prologue are
  combined into a single STP sequence with a deferred stack pointer update. Similarly
  for POPs in epilogues.
- **Custom hardware support**: Apple M1+ SoCs have undocumented extensions:
  - **TSO mode**: A kernel-controlled configuration register switches the memory model
    to Total Store Ordering for Rosetta processes. The scheduler toggles this per
    timeslice. This eliminates ALL memory barrier overhead.
  - **Flag extensions**: ADDS, SUBS, CMP compute PF (parity) and AF (auxiliary) and
    store them as bits 26 and 27 of NZCV. Instructions SETF8, SETF16, AXFLAG, XAFLAG
    provide x86-compatible flag handling with zero performance penalty.

**Comparison with 86Box**: The unused-flags optimization is the same technique as box64's
Kildall's algorithm. The return-address prediction optimization is applicable to 86Box.
The hardware features (TSO, custom flags) are unique to Apple Silicon and give Rosetta 2
an unfair advantage. However, they demonstrate the importance of these optimizations --
Apple built dedicated hardware for them.

### 5.3 Flag Handling

The most advanced flag handling of any translator:

- **Hardware-assisted**: M1+ has custom instructions that compute x86-compatible PF and
  AF within the ARM64 NZCV update. This means `ADDS x0, x1, x2` also sets PF and AF.
- **Software fallback**: When hardware flags don't suffice (rare cases), Rosetta falls
  back to computing flags in GPRs.
- **Dead-flag elimination**: Backward analysis (same concept as box64's Kildall's)
  eliminates flag computations that are never consumed.

**Comparison with 86Box**: Rosetta's hardware flags are obviously not reproducible, but
the dead-flag elimination technique is. 86Box should implement backward flag analysis
as box64 and Rosetta do.

### 5.4 Self-Modifying Code

- AOT translation handles static code; JIT handles dynamic code
- JIT can detect when x86-64 code in heap memory is modified and retranslate
- No specific details are publicly documented about the invalidation mechanism

### 5.5 Performance

- Rosetta 2 achieves approximately **70-80% of native ARM64 performance** on many
  workloads (AnandTech benchmarks)
- The AOT approach eliminates JIT compilation overhead for static code
- Hardware TSO eliminates memory barrier overhead entirely
- Hardware flag support eliminates flag emulation overhead entirely
- "By being so much wider than comparable x86 CPUs, the M1 has a remarkable ability
  to avoid being throughput-bound, even with all the extra instructions Rosetta 2
  generates" (dougallj)

---

## 6. Dolphin (GameCube/Wii)

**Project**: [dolphin-emu.org](https://dolphin-emu.org/)
**Type**: PowerPC -> ARM64/x86-64 JIT
**Relevance**: Console emulator JIT with mature ARM64 backend

### 6.1 IR Design

Dolphin uses a **direct translation** approach (no IR):

- PowerPC instructions are decoded and translated directly to host instructions
- The JIT has separate backends: `Jit64` (x86-64) and `JitArm64` (ARM64)
- No intermediate representation between guest and host
- Each PowerPC instruction has handler functions that emit host instructions directly

**Comparison with 86Box**: Dolphin's direct approach is similar to box64's splatter JIT.
It trades optimization opportunities for lower JIT compilation latency.

### 6.2 Optimization Passes

- **No formal optimization passes** (direct translation)
- **Register caching**: PowerPC GPRs and FPRs are cached in host registers with a
  register cache mechanism that tracks dirty/clean state
- **Instruction-level optimizations**: Individual instruction handlers include
  pattern-specific optimizations (e.g., common instruction sequences)

### 6.3 Flag Handling (Condition Register)

PowerPC has a more complex condition register (CR) than x86:
- 8 CR fields, each with 4 bits (LT, GT, EQ, SO)
- Dolphin keeps CR fields in individual host registers
- **Lazy CR computation**: CR updates are deferred where possible
- **CR field fusion on ARM64**: GT bit handling requires careful management (bit 63
  convention to prevent accidental GT set)
- CR fields can be discarded without storing if analysis shows they won't be read

**Comparison with 86Box**: Different guest ISA, but the lazy evaluation and discard
optimizations are conceptually applicable to x86 EFLAGS.

### 6.4 Self-Modifying Code

- **Instruction cache (ICBI)**: PowerPC explicitly signals instruction cache invalidation,
  making SMC detection easier than x86
- For memory writes to code regions, Dolphin invalidates affected JIT blocks
- Block lookup via physical address hash table

### 6.5 Block Linking

- Direct block chaining with jump patching
- ARM64 branch range limitation: code memory allocated within +/- 128 MiB for
  direct B instruction encoding
- Exit stubs for blocks that haven't been compiled yet

### 6.6 ARM64-Specific Optimizations

- **Fastmem with signal handlers**: Memory accesses are emitted as direct loads/stores
  to a mapped memory region. Faults trigger a signal handler that **backpatches** the
  faulting instruction sequence to call a slow-path handler instead. This gives
  **5-15% speedup** over always-checking memory access.
  - "A fastmem load/store takes as little as 2 instructions, where the same access in
    slowmem can take up to 1000 instructions"
- **STP optimization**: PR #13144 added STP (store pair) optimization for ppcState
  register stores. However, CR field offsets are too large for STP encoding, limiting
  the benefit.
- **Register flush optimization**: Flushes multiple registers efficiently, minimizing
  pipeline-unfriendly long runs of stores
- **Conditional select**: CSEL used for branchless condition register manipulation

**Comparison with 86Box**: Dolphin's fastmem is highly relevant. 86Box's memory access
always goes through helper functions. Signal-handler fastmem could dramatically reduce
memory access overhead. The STP pairing for register spills is also applicable.

### 6.7 Performance

- Fastmem: **5-15% overall speedup**
- JIT is essential -- without it, "Dolphin runs at unplayably slow frame rates"
- ARM64 backend performance is competitive with x86-64 backend on Apple Silicon

---

## 7. RPCS3 (PS3)

**Project**: [rpcs3.net](https://rpcs3.net/)
**Type**: Cell (PPU + SPU) -> x86-64/ARM64 recompiler
**Relevance**: LLVM-based JIT approach, ARM64 port lessons

### 7.1 IR Design

RPCS3 uses **LLVM IR** as its intermediate representation:

- PPU (PowerPC) and SPU (Cell vector unit) instructions are translated to LLVM IR
- LLVM's full optimization pipeline is available (but adds compilation latency)
- The "ubertrampoline" generator handles dispatch between compiled blocks
- ARM64 support uses asmjit for the trampoline generator (not LLVM for this part)

**Comparison with 86Box**: LLVM IR is massively more sophisticated but has very high
compilation latency. Not suitable for 86Box's use case where compilation must be
near-instant.

### 7.2 Optimization Passes

- Full LLVM optimization pipeline (constant folding, loop optimization, vectorization,
  register allocation, instruction selection, etc.)
- SPU vectorization: SPU is a vector processor, so LLVM's auto-vectorization is
  particularly valuable
- FMA (fused multiply-add) enabled for ARM64 targets

### 7.3 ARM64 Port Challenges

The ARM64 port provides lessons on what is hard:
- Early ARM64 builds (2021) could not run anything; only interpreters worked
- LLVM modifications were needed for ARM64 JIT support (maintaining an LLVM fork)
- By late 2023, only non-LLVM builds worked on ARM64
- By mid-2024, commercial titles ran with LLVM PPU on ARM64
- SPU LLVM on ARM64 uses the "slow" LLVM recompiler (no fast asm version)

**Comparison with 86Box**: RPCS3's difficulties with ARM64 LLVM JIT illustrate why
simpler JIT approaches (like 86Box's) are more practical for ARM64 targets.

### 7.4 Performance

- ARM64 build is "a huge performance improvement compared to the x64 build running
  under Rosetta 2"
- MoltenVK provides good GPU performance on macOS
- SPU recompiler is the bottleneck on ARM64 (slow LLVM JIT)

---

## 8. Comparative Tables

### 8.1 IR Design Comparison

| Emulator | IR Type | Passes | SSA | Complexity |
|----------|---------|--------|-----|------------|
| **86Box** | UOP array | 1 (single-pass) | No | Low |
| **box64** | None (splatter JIT) | 4 macro passes | No | Medium |
| **FEX-Emu** | Full SSA IR | Multi-pass | Yes | High |
| **QEMU TCG** | TCG ops (RTL-like) | 2 (translate + optimize) | No | Medium |
| **Rosetta 2** | Unknown (proprietary) | Multi-pass + AOT | Unknown | Very High |
| **Dolphin** | None (direct) | 1 | No | Low |
| **RPCS3** | LLVM IR | Full LLVM pipeline | Yes | Very High |

### 8.2 Flag Handling Comparison

| Emulator | Strategy | Backward Analysis | Native Flags | Dead Flag Elim |
|----------|----------|-------------------|-------------|----------------|
| **86Box** | Deferred (op/res/op1/op2) | No | No | No |
| **box64** | Kildall's + Native Flags | Yes | Yes (ARM64 NZCV) | Yes |
| **FEX-Emu** | SSA-based + IR elim | Yes (via SSA liveness) | Partial (NZCV mapping) | Yes |
| **QEMU TCG** | Deferred (lazy) | No (but DCE catches some) | No | Partial |
| **Rosetta 2** | Hardware-assisted + dead elim | Yes | Yes (custom HW) | Yes |
| **Dolphin** | Lazy CR fields | Yes (CR discard) | Partial | Yes |
| **RPCS3** | LLVM-based | Yes (LLVM passes) | N/A | Yes |

### 8.3 Self-Modifying Code Detection

| Emulator | Detection Method | Granularity | History |
|----------|-----------------|-------------|---------|
| **86Box** | Page dirty masks | Byte-level dirty tracking | 64-block dirty list |
| **box64** | mprotect() / page-level | Page-level | SMC-aware optimizations |
| **FEX-Emu** | Memory syscall tracking | Page-level | Frontend decode detection |
| **QEMU TCG** | mprotect (user) / softmmu (system) | Page-level | TB linked lists |
| **Rosetta 2** | Proprietary | Unknown | AOT + JIT |
| **Dolphin** | ICBI instruction | Instruction-level (explicit) | Block invalidation |
| **RPCS3** | LLVM invalidation | Function-level | N/A |

### 8.4 Block Linking / Chaining

| Emulator | Block Linking | Direct Jump Patching | Unchaining |
|----------|---------------|---------------------|------------|
| **86Box** | **None** | **No** | N/A |
| **box64** | Yes | Yes | Yes |
| **FEX-Emu** | Yes | Yes (+ shared buffers) | Yes |
| **QEMU TCG** | Yes (same-page) | Yes (zero-overhead on x86) | Yes |
| **Rosetta 2** | Yes | Yes | Unknown |
| **Dolphin** | Yes | Yes | Yes |
| **RPCS3** | Yes (LLVM) | Yes | Yes |

### 8.5 ARM64-Specific Features Used

| Feature | 86Box | box64 | FEX-Emu | QEMU | Dolphin |
|---------|-------|-------|---------|------|---------|
| CSEL/CSINC/CSINV | CSEL (3 variants) | Yes | Yes | Limited | Yes |
| CBZ/CBNZ | CBNZ only | Yes | Yes | Yes | Yes |
| TBZ/TBNZ | No | Yes | Yes | Unknown | Yes |
| Barrel shifter (shifted reg) | ADD_REG with LSR/LSL | Yes | Yes | Yes | Yes |
| LDP/STP pairing | No | Partial | Partial | No | Partial (PR #13144) |
| NEON for SIMD | Yes (MMX/SSE) | Yes (full AVX2) | Yes (full AVX2) | Yes | Yes (paired singles) |
| Fastmem (signal handler) | No | N/A (userspace) | N/A (userspace) | No | Yes (5-15% gain) |
| TSO hardware (Apple Silicon) | No | BOX64_DYNAREC_STRONGMEM | No | No | N/A |
| BL/RET prediction | No | Yes | Yes | Unknown | Yes |

---

## 9. Key Takeaways for 86Box

### 9.1 Highest-Impact Improvements (ranked by estimated benefit)

#### 1. Block Linking / Direct Jump Patching
**Every other emulator has this. 86Box does not.**

Currently, every block exit in 86Box returns to the dispatcher, which performs a hash
lookup to find the next block. Direct block chaining patches the end-of-block jump to
go straight to the next block. This eliminates the dispatcher overhead for the common
case (sequential execution).

- **Estimated impact**: 15-30% overall speedup (based on Dolphin's "5-15% from fastmem
  alone" and QEMU's "zero-overhead chaining")
- **Complexity**: Medium. Need to add jump-patch infrastructure and unchaining for
  invalidation.
- **Risk**: Must handle invalidation correctly (reverse patches when blocks are deleted)

#### 2. Backward Flag Analysis (Kildall's Algorithm)
**Used by: box64, FEX-Emu, Rosetta 2**

86Box currently stores all 4 flag components (op, res, op1, op2) for every flag-setting
instruction. A backward pass over the instruction stream can determine which flags are
actually consumed before being overwritten. For x86 code, the vast majority of flag
updates are dead.

- **Estimated impact**: 10-30% (box64 measured 30% on 7zip with native flags;
  Rosetta 2 considers this their most important software optimization)
- **Complexity**: Medium. Requires a second pass over the UOP stream before code
  emission. Can be implemented as a pre-pass in `codegen_ir_compile()`.
- **Risk**: Low. Does not change code generation for instructions whose flags ARE read.

#### 3. Native Flag Mapping (ARM64 NZCV)
**Used by: box64, FEX-Emu (partial), Rosetta 2 (hardware)**

After dead-flag analysis eliminates most flag updates, the remaining flag-setting
instructions can often use ARM64's NZCV flags directly instead of storing to memory.
When CMP is followed by a conditional branch, this saves 4+ memory operations.

- **Estimated impact**: Additional 5-15% on top of dead-flag analysis
- **Complexity**: High. Requires tracking NZCV liveness across instructions, careful
  handling of partial flag updates, PF/AF still need GPR computation.
- **Risk**: Medium. ARM64 NZCV semantics don't perfectly match x86 EFLAGS for all
  operations (e.g., subtract carry is inverted).

#### 4. Call-Return Stack Optimization
**Used by: FEX-Emu, Rosetta 2, box64**

Translating x86 CALL/RET as ARM64 BL/RET (instead of indirect branches) enables the
ARM CPU's hardware return-address predictor. FEX-Emu says "the majority of performance
uplift" came from this optimization.

- **Estimated impact**: 5-10% (highly workload-dependent)
- **Complexity**: Low-Medium. Requires tracking call/return pairs and emitting BL+RET
  instead of generic branches.
- **Risk**: Low. Fallback to indirect branch when prediction fails.

#### 5. Register Spill Optimization (LDP/STP Pairing)
**Used by: Dolphin (partial)**

86Box's ARM64 backend stores/loads registers individually. Pairing adjacent
store/load operations into STP/LDP would halve the instruction count for register spills.

- **Estimated impact**: 3-5%
- **Complexity**: Low. Peephole pass over emitted ARM64 instructions to merge adjacent
  STR/LDR into STP/LDP.
- **Risk**: Low.

### 9.2 Medium-Impact Improvements

#### 6. Basic Liveness Analysis and DCE
- Add a pre-pass to mark dead IR registers (those written but never read)
- Eliminate UOPs that write only to dead registers
- **Estimated impact**: 5-10% (depends on how much dead code exists in translated blocks)

#### 7. TBZ/TBNZ for Bit Tests
- 86Box doesn't use TBZ/TBNZ (test bit and branch zero/nonzero)
- These are ideal for testing individual flag bits, status bits, etc.
- **Estimated impact**: 1-3%

#### 8. Improved x87 Handling
- FEX's x87 stack optimization pass shows massive gains for FPU-heavy code
- 86Box emulates x87 for Pentium-era code which uses x87 heavily
- Optimizing x87 stack management in the IR could give significant speedups

### 9.3 Long-Term / Aspirational

#### 9. SSA-based IR
- FEX demonstrates the power of SSA for optimization
- Would require a complete rewrite of the IR layer
- Probably not worth the effort unless 86Box's dynarec is fundamentally redesigned

#### 10. Fastmem (Signal-Handler Memory Access)
- Dolphin's fastmem gives 5-15% and is "2 instructions vs up to 1000"
- However, 86Box emulates a full system with segmentation, paging, and TLB
- Fastmem may be applicable for flat-model protected-mode code but not for real
  mode or segmented code
- Complex to implement correctly with 86Box's memory model

---

## 10. Sources

### box64
- [Revisiting the dynarec (July 2024)](https://box86.org/2024/07/revisiting-the-dynarec/)
- [Inner workings (July 2021)](https://box86.org/2021/07/inner-workings-a-high%E2%80%91level-view-of-box86-and-a-low%E2%80%91level-view-of-the-dynarec/)
- [Box64 v0.3.2 native flags (Phoronix)](https://www.phoronix.com/news/Box64-0.3.2-Released)
- [box64 GitHub](https://github.com/ptitSeb/box64)
- [box64 USAGE.md](https://github.com/ptitSeb/box64/blob/main/docs/USAGE.md)
- [Box86/Box64 vs QEMU vs FEX (vs Rosetta2) benchmarks](https://box86.org/2022/03/box86-box64-vs-qemu-vs-fex-vs-rosetta2/)

### FEX-Emu
- [FEX-Emu website](https://fex-emu.com/)
- [FEX GitHub](https://github.com/FEX-Emu/FEX)
- [FEX Register Allocator wiki](https://github.com/FEX-Emu/FEX/wiki/Register-Allocator)
- [FEX DeepWiki architecture](https://deepwiki.com/FEX-Emu/FEX)
- [FEX-2506 shared code buffers](https://fex-emu.com/FEX-2506/)
- [FEX IR definition (IR.json)](https://github.com/FEX-Emu/FEX/blob/FEX-2409/FEXCore/Source/Interface/IR/IR.json)
- [FEX x87 optimization pass](https://github.com/FEX-Emu/FEX/commit/a1378f94ce8e7843d5a3cd27bc72847973f8e7ec)

### QEMU TCG
- [QEMU TCG deep dive part 1 (Airbus)](https://airbus-seclab.github.io/qemu_blog/tcg_p1.html)
- [QEMU TCG deep dive part 2 (Airbus)](https://airbus-seclab.github.io/qemu_blog/tcg_p2.html)
- [QEMU Translator Internals](https://www.qemu.org/docs/master/devel/tcg.html)
- [QEMU TCG IR documentation](https://www.qemu.org/docs/master/devel/tcg-ops.html)
- [QEMU old tech docs (SMC, lazy flags)](https://qemu.weilnetz.de/doc/2.7/qemu-tech-20160903.html)
- [Boosting Cross-Architectural Emulation (arXiv:2501.03427)](https://arxiv.org/abs/2501.03427)

### Rosetta 2
- [Why is Rosetta 2 fast? (dougallj)](https://dougallj.wordpress.com/2022/11/09/why-is-rosetta-2-fast/)
- [Project Champollion: Reverse-engineering Rosetta 2 part 1](https://ffri.github.io/ProjectChampollion/part1/)
- [Project Champollion: Reverse-engineering Rosetta 2 part 2](https://ffri.github.io/ProjectChampollion/part2/)
- [Apple Developer: About the Rosetta translation environment](https://developer.apple.com/documentation/apple-silicon/about-the-rosetta-translation-environment)
- [AnandTech M1 Rosetta 2 benchmarks](https://www.anandtech.com/show/16252/mac-mini-apple-m1-tested/6)
- [Rosetta 2 Wikipedia](https://en.wikipedia.org/wiki/Rosetta_(software))
- [UTM issue #5460: Implementing Rosetta 2 techniques](https://github.com/utmapp/UTM/issues/5460)

### Dolphin
- [Dolphin JitArm64 source](https://github.com/dolphin-emu/dolphin/tree/master/Source/Core/Core/PowerPC/JitArm64)
- [Dolphin JitArm64 backpatch](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/PowerPC/JitArm64/JitArm64_BackPatch.cpp)
- [Dolphin STP optimization PR #13144](https://github.com/dolphin-emu/dolphin/pull/13144)
- [Dolphin Progress Report (2020)](https://dolphin-emu.org/blog/2020/10/05/dolphin-progress-report-july-and-august-2020/)
- [Dolphin fastmem bug #7013](https://bugs.dolphin-emu.org/issues/7013)

### RPCS3
- [Introducing RPCS3 for arm64 (December 2024)](https://blog.rpcs3.net/2024/12/09/introducing-rpcs3-for-arm64/)
- [RPCS3 PPU LLVM ARM64 PR #12115](https://github.com/RPCS3/rpcs3/pull/12115)
- [RPCS3 SPU LLVM ARM64 PR #12338](https://github.com/RPCS3/rpcs3/pull/12338)

### General
- [Lazy EFlags evaluation (Silvio Cesare)](https://silviocesare.wordpress.com/2009/03/08/lazy-eflags-evaluation-and-other-emulator-optimisations/)
- [NO EXECUTE: Flags in Bochs (emulators.com)](http://www.emulators.com/docs/nx11_flags.htm)
- [Box64 vs FEX on Cortex-A53](https://printserver.ink/blog/box64-vs-fex/)
