# Voodoo ARM64 JIT Port — Agent Guide

## Available Agents

### Voodoo GPU JIT agents

| Agent | Color | Purpose | Tools |
|-------|-------|---------|-------|
| `voodoo-lead` | 🔴 red | Scaffolding, coordination, build/test, Phase 1+2 | Write, Edit, Bash, Serena (all) |
| `voodoo-texture` | 🔵 cyan | Texture fetch, LOD, bilinear, TMU combine (Phase 3) | Write, Edit, Bash, Serena (all) |
| `voodoo-color` | 🟢 green | Color/alpha combine pipeline (Phase 4) | Write, Edit, Bash, Serena (all) |
| `voodoo-effects` | 🟣 magenta | Fog, alpha test/blend, dither, framebuffer write, per-pixel increments (Phase 5+6) | Write, Edit, Bash, Serena (all) |
| `voodoo-optimizer` | ⚪ white | Peephole opts, redundant load/store elimination, NEON tuning, instruction selection | Write, Edit, Bash, Serena (all) |
| `voodoo-debug` | 🟡 yellow | Validation, encoding verification, build diagnostics — **read-only** | Bash, Serena (read-only) |
| `voodoo-arch` | 🔵 blue | Architecture research, spec validation against 3dfx docs — **read-only** | WebSearch, WebFetch, Serena (read-only) |

### CPU dynarec JIT agents

These agents work on the ARM64 CPU dynamic recompiler (`src/codegen_new/`), **not** the Voodoo GPU JIT.

| Agent | Color | Purpose | Tools |
|-------|-------|---------|-------|
| `cpu-jit-impl` | 🔴 red | ARM64 CPU dynarec implementation (PFRSQRT, BL calls, emitters, C interp opts) | Write, Edit, Bash, Serena (all) |
| `cpu-jit-debug` | 🟡 yellow | CPU dynarec debugging, encoding validation — **read-only** | Bash, Serena (read-only) |
| `cpu-jit-arch` | 🔵 blue | CPU dynarec architecture research (ARM64 ISA, 3DNow!, SSE2, NEON) — **read-only** | WebSearch, WebFetch, Serena (read-only), Context7 |

## When to Use Each Agent

### voodoo-lead (coordinator)
**Use for:**
- Phase 1 scaffolding (header creation, prologue/epilogue)
- Phase 2 pixel loop and depth test
- Build system integration
- Guard changes to existing files
- Coordinating work between other agents
- Testing and validation workflow

**Example:** "Implement Phase 1 scaffolding for ARM64 codegen"

### voodoo-texture (texture specialist)
**Use for:**
- Phase 3 texture fetch implementation
- Perspective-correct W division
- LOD calculation
- Point-sampled and bilinear filtering
- Mirror/clamp texture addressing
- Dual-TMU combine logic

**Example:** "Implement bilinear texture filtering with NEON"

### voodoo-color (color/alpha specialist)
**Use for:**
- Phase 4 color and alpha combine pipeline
- Color select (iter_rgb, tex, color1, lfb)
- Alpha select (iter_a, tex, color1)
- Chroma key test
- Alpha mask test
- Color/alpha multiply and blend operations
- Output clamping

**Example:** "Implement color combine with cc_mselect modes"

### voodoo-effects (effects specialist)
**Use for:**
- Phase 5: Fog (constant, add, mult), alpha test, alpha blend
- Phase 6: Dithering (4x4, 2x2), framebuffer write, depth write
- Per-pixel state increments
- RGB565 packing
- Tiled framebuffer support

**Example:** "Implement fog blend with FOG_MULT mode"

### voodoo-debug (validator)
**Use for:**
- Analyzing build errors or warnings
- Validating ARM64 instruction encodings
- Comparing codegen output to x86-64 reference
- Diagnosing runtime crashes or incorrect behavior
- Reading disassembly (objdump, otool)
- Verifying static assertions

**Example:** "Check the ARM64 encoding for SMULL instruction at line 2950"

### voodoo-optimizer (optimization specialist)
**Use for:**
- Peephole improvements to generated ARM64 code
- Redundant load/store elimination
- Better instruction selection (e.g. BIC+ASR clamp, TBZ for single-bit tests)
- NEON/ASIMD vectorization opportunities
- Prologue/epilogue register pinning
- Strictly ARMv8.0-A baseline — no v8.1+ extensions

**Example:** "Find and eliminate redundant loads in the alpha blend path"
**Example:** "Pin the rgb565 table pointer in a callee-saved register"

### voodoo-arch (architecture expert)
**Use for:**
- Validating implementation against official 3dfx specs
- Researching Voodoo hardware behavior
- Finding authoritative documentation (Glide SDK, SST-1 programmer's guide)
- Understanding register bit layouts (fbzMode, fbzColorPath, alphaMode)
- Comparing our logic to real hardware
- Answering questions about Voodoo 1/2/Banshee/3 differences

**Example:** "Does our color combine implementation match the Voodoo spec?"
**Example:** "What's the exact fog blend formula for FOG_MULT mode?"
**Example:** "Find the official 3dfx docs on alpha blend factors"

### cpu-jit-impl (CPU dynarec implementer)
**Use for:**
- ARM64 CPU JIT backend fixes and new emitters (`src/codegen_new/`)
- PFRSQRT instruction fix
- BL intra-pool call optimization
- LOAD_FUNC_ARG width fix
- C-level interpreter optimizations
- Builds and commits on success

**Example:** "Fix the PFRSQRT reciprocal square root approximation in the ARM64 backend"

### cpu-jit-debug (CPU dynarec debugger)
**Use for:**
- Validating ARM64 instruction encodings in the CPU dynarec
- Diagnosing build errors in `src/codegen_new/`
- Comparing ARM64 codegen to x86-64 reference behavior
- Analyzing NEON/SIMD correctness

**Example:** "Verify the ARM64 encoding of our SMULL emitter"

### cpu-jit-arch (CPU dynarec architecture expert)
**Use for:**
- ARM64/AArch64 ISA research for CPU dynarec work
- AMD 3DNow!, SSE2, MMX behavior lookup
- NEON/ASIMD instruction mapping
- JIT compilation technique research

**Example:** "What's the correct ARM64 equivalent of PFRSQRT?"

---

## Usage Pattern

### For implementation work:
1. Spawn the appropriate implementation agent (lead/texture/color/effects)
2. Agent implements the feature
3. Agent builds with `./scripts/build-and-sign.sh`
4. Agent commits on success
5. If build fails → spawn `voodoo-debug` to diagnose
6. If behavior is unclear → spawn `voodoo-arch` to research specs

### For validation:
1. Spawn `voodoo-debug` to analyze code/logs/disassembly
2. If spec clarification needed → spawn `voodoo-arch` with specific question
3. Debug agent or arch agent reports findings
4. Implementation agent applies fixes if needed

### For spec questions:
1. Spawn `voodoo-arch` with your question
2. Agent searches authoritative sources (WebSearch/WebFetch)
3. Agent compares specs to our implementation
4. Agent reports: ✅ matches, ⚠️ discrepancy, or ❌ bug

## Example Workflows

### Workflow 1: Phase Implementation
```
User: "Implement Phase 5"
→ Spawn voodoo-effects agent
  → Agent implements fog/alpha test/blend
  → Agent builds → SUCCESS
  → Agent commits
→ User tests
→ If rendering wrong:
  → Spawn voodoo-arch: "Validate fog blend formula"
  → Arch agent finds spec
  → Arch agent reports discrepancy
  → Spawn voodoo-effects to fix
```

### Workflow 2: Build Error
```
User: "Build is failing with 'unknown type voodoo_state_t'"
→ Spawn voodoo-debug
  → Debug agent diagnoses: missing include or guard
  → Debug agent reports fix
→ User applies fix or has implementation agent do it
```

### Workflow 3: Spec Validation
```
User: "Is our bilinear filtering correct?"
→ Spawn voodoo-arch
  → Agent searches for Glide SDK bilinear docs
  → Agent reads our codegen_texture_fetch implementation
  → Agent compares weight calculations
  → Agent reports: ✅ matches spec (4-tap blend with >>8 shift)
```

### Workflow 4: Rendering Bug
```
User: "Textures look wrong in Quake"
→ Spawn voodoo-debug to check JIT logs
  → Debug agent sees textureMode values
→ Spawn voodoo-arch: "What does textureMode bit 17 control?"
  → Arch agent: "Bit 17 = minification filter (0=point, 1=bilinear)"
→ Spawn voodoo-texture to verify minification logic
  → Texture agent finds bug in LOD selection
  → Texture agent fixes and commits
```

## Agent Communication

Agents can work together:
- Implementation agents (lead/texture/color/effects) **write code**
- Debug agent **analyzes code** and reports issues (read-only)
- Arch agent **researches specs** and validates correctness (read-only)

When an implementation agent needs validation:
1. User spawns debug or arch agent separately
2. Validation agent reports findings
3. User spawns implementation agent to apply fixes

## Quick Reference

**I want to...** | **Spawn this agent**
---|---
Implement a new Voodoo JIT phase | lead, texture, color, or effects (depending on phase)
Optimize Voodoo JIT codegen performance | voodoo-optimizer
Fix a Voodoo JIT build error | voodoo-debug
Understand what the Voodoo hardware does | voodoo-arch
Validate Voodoo JIT implementation | voodoo-arch
Debug wrong rendering output | voodoo-debug first, then voodoo-arch if needed
Check ARM64 instruction encoding (Voodoo) | voodoo-debug
Find official 3dfx documentation | voodoo-arch
Compare our Voodoo code to x86-64 reference | voodoo-debug
Research Voodoo register layouts | voodoo-arch
Implement a CPU dynarec fix or emitter | cpu-jit-impl
Debug CPU dynarec build or encoding error | cpu-jit-debug
Research ARM64 ISA for CPU dynarec | cpu-jit-arch

## Color Coding (for easy identification in logs)

### Voodoo GPU JIT
- 🔴 **Red** = `voodoo-lead` (coordinator)
- 🔵 **Cyan** = `voodoo-texture` (texture specialist)
- 🟢 **Green** = `voodoo-color` (color/alpha specialist)
- 🟣 **Magenta** = `voodoo-effects` (fog/blend/dither/write)
- ⚪ **White** = `voodoo-optimizer` (peephole/NEON optimizer)
- 🟡 **Yellow** = `voodoo-debug` (validator, read-only)
- 🔵 **Blue** = `voodoo-arch` (3dfx spec research, read-only)

### CPU Dynarec JIT
- 🔴 **Red** = `cpu-jit-impl` (implementer)
- 🟡 **Yellow** = `cpu-jit-debug` (validator, read-only)
- 🔵 **Blue** = `cpu-jit-arch` (ISA research, read-only)
