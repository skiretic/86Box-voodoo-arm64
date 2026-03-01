# VideoCommon Planning Documentation Validation Report

**Date**: 2026-02-26
**Reviewer**: vc-debug agent
**Documents reviewed**: 12 planning/research docs + 5 agent definitions (17 total)

---

## A. Cross-Document Consistency

### A.1 Push Constant Size (64 bytes)

| Document | Value | Verdict |
|----------|-------|---------|
| DESIGN.md (line 266) | 64 bytes | OK |
| push-constant-layout.md (throughout) | 64 bytes, 16 fields | OK |
| CHECKLIST.md (task 2.7) | 64-byte `vkCmdPushConstants` | OK |
| vc-shader.md (line 62) | 64 bytes | OK |
| vc-lead.md (line 55) | Push constant range: 64 bytes | OK |
| **vulkan-architecture.md (line 59)** | **"~80 bytes"** | **MISMATCH** |

**Finding**: vulkan-architecture.md line 59 says "Our 14 non-sampler uniforms fit in ~80 bytes." This was written before push-constant-layout.md finalized the 64-byte struct. The ~80 byte estimate is stale.

### A.2 Pipeline Strategy (Blend Baked, Depth Dynamic)

| Document | Strategy | Verdict |
|----------|----------|---------|
| DESIGN.md (lines 215-230) | Blend in VkPipeline, depth dynamic via VK_EXT_extended_dynamic_state | OK |
| push-constant-layout.md (batch key section) | 8-byte blend key for pipeline cache | OK |
| CHECKLIST.md (task 1.7, 6.3) | Pipeline cache by blend key, dynamic depth | OK |
| vc-shader.md (lines 57-60) | Blend NOT dynamic (MoltenVK limitation), depth dynamic | OK |
| vulkan-architecture.md (pipeline section) | Same strategy | OK |

**Finding**: Consistent across all documents. No issues.

### A.3 File and Module Names

| Module | DESIGN.md | CHECKLIST.md | cmake-integration.md | Verdict |
|--------|-----------|--------------|---------------------|---------|
| vc_core.c/h | Yes | Yes (task 1.4) | Yes | OK |
| vc_render_pass.c/h | Yes | Yes (task 1.5) | Yes | OK |
| vc_pipeline.c/h | Yes | Yes (task 1.7) | Yes | OK |
| vc_shader.c/h | Yes | Yes (task 1.6) | Yes | OK |
| vc_texture.c/h | Yes | Yes (task 3.1) | Yes | OK |
| vc_batch.c/h | Yes | Yes (task 1.8) | Yes | OK |
| vc_readback.c/h | Yes | Yes (task 1.9) | Yes | OK |
| vc_thread.c/h | Yes | Yes (task 1.10) | Yes | OK |
| vid_voodoo_vk.c/h | Yes | Yes (task 2.3) | Yes | OK |
| videocommon.h | Yes | Yes (task 1.11) | Yes | OK |

**Finding**: All module names are consistent. No discrepancies.

### A.4 Shader Path

| Document | Shader Path | Verdict |
|----------|-------------|---------|
| CHECKLIST.md (task 1.3) | `src/video/videocommon/shaders/voodoo_uber.{vert,frag}` | Reference |
| DESIGN.md (section on shaders) | `shaders/voodoo_uber.{vert,frag}` (relative) | OK |
| vc-debug.md (line 107) | `src/video/shaders/voodoo_uber.frag` | **MISMATCH** |
| vc-shader.md (line 34) | `shaders/voodoo_uber.{vert,frag}` | Ambiguous |

**Finding**: vc-debug.md references `src/video/shaders/` but CHECKLIST.md places shaders in `src/video/videocommon/shaders/`. The vc-debug agent definition has a stale path.

### A.5 Phase Count

All documents consistently reference 11 phases. No issues.

### A.6 Library Choices

| Library | DESIGN.md | cmake-integration.md | Agent defs | Verdict |
|---------|-----------|---------------------|------------|---------|
| volk (dynamic loader) | Yes | Yes (vendored) | vc-lead: yes | OK |
| VMA (memory allocator) | Yes | Yes (vendored) | vc-lead: yes | OK |
| glslc (SPIR-V compiler) | Yes | Yes (FindProgram) | vc-shader: yes | OK |
| vk-bootstrap | **Resolved Q15: NOT adopted** | Not mentioned | **vc-arch.md line 97: listed** | **MISMATCH** |

**Finding**: DESIGN.md resolved question #15 explicitly says "Not adopted" for vk-bootstrap. However, vc-arch.md line 97 still lists it as a third-party library to research. This is misleading -- the agent might recommend something already rejected.

### A.7 Display Strategy

| Document | Strategy | Verdict |
|----------|----------|---------|
| DESIGN.md | Phase 1: readback to target_buffer; Phase 2: VkSurfaceKHR | OK |
| CHECKLIST.md | Phase 2.5: readback scanout; Phase 9: Qt VCRenderer | OK |
| vc-plumbing.md | Phase 1: readback path; Phase 2: VkSurfaceKHR | OK |

**Finding**: Consistent. The two-phase display strategy (readback first, zero-copy later) is aligned.

### A.8 Fog Table Format

| Document | Format | Verdict |
|----------|--------|---------|
| DESIGN.md (line 259) | 64x1 `sampler2D` (sampler1D not available in Vulkan SPIR-V) | Reference |
| push-constant-layout.md (line 230-232) | 64x1 VkImage, VK_FORMAT_R8G8_UNORM, `sampler2D` | OK |
| CHECKLIST.md (task 3.4) | 64x1 VkImage, VK_FORMAT_R8G8_UNORM, `sampler2D` with V=0.5 | OK |
| **uniform-mapping.md (line 355)** | **`sampler1D`** | **MISMATCH** |
| **uniform-mapping.md (line 537)** | **`uniform sampler1D u_fog_table`** | **MISMATCH** |

**Finding**: uniform-mapping.md still uses `sampler1D` for the fog table in two places: the sampler table (line 355) and the final recommended shader block (line 537). The Vulkan SPIR-V target does not support `sampler1D`. This must be corrected to `sampler2D` with a 64x1 image.

### A.9 Vulkan Loader Strategy

| Document | Approach | Verdict |
|----------|----------|---------|
| DESIGN.md | volk (dynamic loader, no link-time Vulkan dependency) | OK |
| cmake-integration.md | volk vendored, no find_package(Vulkan) | OK |
| **vc-lead.md (line 45)** | **"Add `find_package(Vulkan)` and linking"** | **MISMATCH** |

**Finding**: vc-lead.md responsibility #2 says to use `find_package(Vulkan)` but cmake-integration.md explicitly states volk replaces the need for find_package(Vulkan). The whole point of volk is to avoid link-time dependency on the Vulkan loader. vc-lead.md has a stale instruction.

---

## B. Checklist Completeness

### B.1 DESIGN.md Features vs CHECKLIST.md Tasks

I traced each feature described in DESIGN.md to specific checklist task(s):

| DESIGN.md Feature | CHECKLIST.md Task(s) | Verdict |
|-------------------|---------------------|---------|
| Vulkan instance/device/queue | 1.4 | OK |
| volk loader | 1.2 (volk vendoring) | OK |
| VMA allocator | 1.2 (VMA vendoring), 1.4 (VMA setup) | OK |
| SPIR-V shader compilation | 1.3 (glslc pipeline) | OK |
| Render pass + framebuffer | 1.5 | OK |
| Pipeline + cache | 1.7 | OK |
| Push constant mapping | 2.7 | OK |
| Vertex reconstruction | 2.3 | OK |
| Triangle submission branch | 2.4 | OK |
| Scanout integration | 2.5 | OK |
| Texture upload/cache | 3.1, 3.2, 3.3 | OK |
| NCC palette invalidation | 3.6 | OK |
| Fog table texture | 3.4 | OK |
| Color combine | 4.1 | OK |
| Alpha combine | 4.2 | OK |
| Chroma key | 4.3 | OK |
| TMU1 + multi-texture | 5.1, 5.2 | OK |
| Detail texture + LOD fraction | 5.3 | OK |
| Trilinear | 5.4 | OK |
| Fog (table/alpha/Z/W modes) | 6.1 | OK |
| Alpha test | 6.2 | OK |
| Alpha blend (standard) | 6.3 | OK |
| Copy-on-blend (AFUNC_ACOLORBEFOREFOG) | 6.4 | OK |
| Stipple test | 7.1 | OK |
| Dither (three-tier) | 7.2 | OK |
| Fastfill/clear | 7.3 | OK |
| Alpha mask test | 7.4 | OK |
| W-buffer depth | 7.5 | OK |
| Depth bias + depth source | 7.6 | OK |
| LFB read (sync) | 8.1 | OK |
| LFB read (async double-buffered) | 8.2 | OK |
| LFB write | 8.3 | OK |
| Dirty tile tracking | 8.4 | OK |
| Qt VCRenderer | 9.1-9.3 | OK |
| Post-processing | 9.4 | OK |
| VGA passthrough (Banshee/V3) | 10.1 | OK |
| 2D blitter integration | 10.2 | OK |
| Dual-path validation | 11.1, 11.2 | OK |
| Pipeline cache persistence | 11.4 | OK |
| Cross-platform validation | 11.5 | OK |
| Extended dynamic state fallback | 11.6 | OK |

### B.2 Missing from CHECKLIST

| Feature in DESIGN.md | CHECKLIST Coverage | Verdict |
|----------------------|-------------------|---------|
| Validation layer debug callback | 1.4 (last sub-task) | OK |
| Color write mask (baked into pipeline) | Implied in 6.3 | **IMPLICIT** |
| Depth write enable (dynamic state) | Implied in 1.7 | **IMPLICIT** |
| Texture wrap/clamp/mirror modes | Covered in 3.1 (VkSampler) | OK |
| Alpha buffer mode (fbzMode bit 18) | Not explicitly covered | **MISSING** |
| Rotating stipple detail (per-scanline offset) | Mentioned in 7.1 but no separate acceptance criteria | **WEAK** |

**Finding**: Color write mask and depth write enable changes are not explicitly called out as testable items, though they are implicitly part of pipeline and dynamic state tasks. Alpha buffer mode (`FBZ_ALPHA_ENABLE`, fbzMode bit 18) is mentioned in uniform-mapping.md but has no dedicated checklist task. Rotating stipple mode is mentioned but not clearly acceptance-tested.

---

## C. Agent Coverage

### C.1 Task Distribution

| Agent | # Tasks | Assessment |
|-------|---------|------------|
| vc-lead | 38 | Heavy but appropriate -- coordination role |
| vc-shader | 28 | Core rendering work, well-scoped |
| vc-plumbing | 27 | Infrastructure and display, well-scoped |
| vc-debug | 12 | Validation checkpoints at each phase, reasonable |
| vc-arch | 2 | Very light -- only CHANGELOG and research. Consider more tasks. |

### C.2 Agent Definition Accuracy

| Agent | Issue | Severity |
|-------|-------|----------|
| vc-lead.md | Line 45: references `find_package(Vulkan)` -- should be volk, per cmake-integration.md | Medium |
| vc-debug.md | Line 107: shader path `src/video/shaders/voodoo_uber.frag` should be `src/video/videocommon/shaders/` | Low |
| vc-arch.md | Line 97: lists vk-bootstrap as a third-party library, but DESIGN.md Q15 says "Not adopted" | Low |
| vc-shader.md | No issues found | OK |
| vc-plumbing.md | No issues found | OK |

### C.3 Knowledge Gaps

- vc-debug.md lists `src/video/vid_voodoo_vk.c` as "Voodoo Vulkan bridge" but this file does not exist yet (to be created in Phase 2). This is expected and correct -- it references future work.
- vc-shader.md correctly references push-constant-layout.md for the 64-byte struct.
- All agents correctly reference DESIGN.md as the authoritative Vulkan 1.2 design doc.

---

## D. Stale OpenGL References

### D.1 Documents with GL References

I searched all planning documents for patterns: `GL_`, `glBlend`, `glDepth`, `glTex`, `glUniform`, `glRead`, `FBO`, `sampler1D`, `glOrtho`, `GLAD`, `glColor`, `glScissor`, `glCopy`, `PBO`, `UBO ref`, `GL calls`, `GL fixed`.

| Document | # GL References | Severity | Notes |
|----------|----------------|----------|-------|
| **uniform-mapping.md** | ~40+ | **HIGH** | Pervasive GL terminology: `glBlendFunc`, `glDepthFunc`, `GL_ZERO`, `GL_SCISSOR_TEST`, `sampler1D`, section titled "GL Fixed-Function State" |
| **texture-formats.md** | ~15 | **HIGH** | Upload section uses `glTexImage2D`, `GL_BGRA`, `GL_UNSIGNED_BYTE`, `GL_UNPACK_ROW_LENGTH` |
| **dither-blend-ordering.md** | ~20 | **HIGH** | References `glBlendFunc`, `glBlitFramebuffer`, `FBO`, GL blend factor names (`GL_SRC_ALPHA`, `GL_ONE`, etc.) |
| **emulator-survey.md** | ~15 | **MEDIUM** | References `glReadPixels`, `PBO`, `glUniform`, `glBlendFunc`, `glCopyTexSubImage2D` -- some are appropriate when discussing other emulators' GL backends |
| **perspective-correction.md** | ~10 | **MEDIUM** | References `glOrtho`, OpenGL 4.6 spec, GL interpolation concepts |
| **intercept-point.md** | ~5 | **MEDIUM** | Title says "GL Rendering", body references GL concepts |
| **testing-strategy.md** | 2 | **LOW** | Two "FBO" references (lines 464, 474) |
| **DESIGN.md** | ~15 | **LOW** | Complexity comparison table intentionally uses GL for comparison; one `sampler1D` mention in explanation (line 655) |
| **vulkan-architecture.md** | ~20 | **ACCEPTABLE** | GL-to-Vulkan mapping table intentionally references GL concepts for the purpose of translation guidance |
| push-constant-layout.md | 1 | **LOW** | One `sampler1D` in explanation text |
| cmake-integration.md | 0 | OK | Clean |
| CHECKLIST.md | 0 | OK | Clean |

### D.2 Critical GL References to Fix

1. **uniform-mapping.md**: The entire document is framed around GL concepts. Section "GL Fixed-Function State" (line 357) must be renamed to "Vulkan Pipeline State". All `glBlendFunc`/`glDepthFunc` references must become `VkBlendFactor`/`VkCompareOp`. The `sampler1D` fog table declaration (lines 355, 537) must become `sampler2D`.

2. **texture-formats.md**: The "Upload Parameters" section references `glTexImage2D`, `GL_BGRA`, `GL_UNSIGNED_BYTE`. Must be rewritten to reference `vkCmdCopyBufferToImage`, `VK_FORMAT_B8G8R8A8_UNORM`, staging buffer workflow.

3. **dither-blend-ordering.md**: All blend factor references (`GL_SRC_ALPHA`, `GL_ONE`, etc.) must become Vulkan equivalents (`VK_BLEND_FACTOR_SRC_ALPHA`, `VK_BLEND_FACTOR_ONE`). `FBO` references should become "VkFramebuffer" or "render target." `glBlitFramebuffer` should become `vkCmdBlitImage`.

4. **intercept-point.md**: Title "Where to Hook into the Voodoo Pipeline for GL Rendering" should say "Vulkan Rendering."

---

## E. Technical Contradictions

### E.1 Push Constant Size: ~80 bytes vs 64 bytes

- **vulkan-architecture.md line 59**: "Our 14 non-sampler uniforms fit in ~80 bytes"
- **push-constant-layout.md**: Exact 64-byte layout with 16 fields
- **DESIGN.md line 266**: "Push constant layout (64 bytes)"

**Resolution**: The ~80 byte estimate in vulkan-architecture.md is stale, written before the optimized packing in push-constant-layout.md. Update vulkan-architecture.md line 59 to say "64 bytes" and reference push-constant-layout.md.

**Severity**: Medium (could confuse an agent into thinking there is more push constant budget).

### E.2 Fog Table: sampler1D vs sampler2D

- **uniform-mapping.md lines 355, 537**: `sampler1D u_fog_table`
- **DESIGN.md line 259**: `sampler2D` (explicit note: "sampler1D not available in Vulkan SPIR-V")
- **push-constant-layout.md line 230-232**: `sampler2D` (64x1 VkImage)
- **CHECKLIST.md task 3.4**: `sampler2D` with V=0.5

**Resolution**: uniform-mapping.md must be updated. `sampler1D` does not exist in Vulkan SPIR-V. The fog table is a 64x1 `sampler2D` with V coordinate fixed at 0.5.

**Severity**: High (will cause shader compilation failure if used verbatim).

### E.3 Perspective Correction: W=1 vs Encoded W

- **emulator-survey.md, section E.2 points 4-5**: "Set `gl_Position.w = 1.0`" and "Use `noperspective` for all varyings"
- **perspective-correction.md**: Recommends Approach C: encode W into `gl_Position.w`, use `smooth` for texture varyings, `noperspective` only for colors/depth
- **DESIGN.md lines 250-253**: Matches perspective-correction.md (hybrid approach, W encoding)
- **CHECKLIST.md task 2.6**: Explicit W encoding: `gl_Position = vec4(ndc*W, z*W, W)`

**Resolution**: emulator-survey.md section E.2 contains initial recommendations that were superseded by the deeper analysis in perspective-correction.md. The perspective-correction.md approach (Approach C) is the correct one, adopted by DESIGN.md and CHECKLIST.md. The emulator-survey.md recommendations should be annotated as "superseded by perspective-correction.md".

**Severity**: High (W=1 with noperspective for all would produce incorrect texture mapping).

### E.4 UBO vs Push Constants

- **emulator-survey.md, section D.4 point 1**: "Use a single UBO for all Voodoo pipeline state"
- **DESIGN.md, push-constant-layout.md**: Push constants (64 bytes), not UBO

**Resolution**: The emulator survey recommendation was an early suggestion. The final design chose push constants for lower-latency per-batch updates (no descriptor set update needed). emulator-survey.md should note this was superseded.

**Severity**: Low (emulator-survey is exploratory research, not prescriptive).

---

## F. Missing Coverage

### F.1 Voodoo Features Not Explicitly Covered

| Feature | Mentioned? | Checklist Task? | Assessment |
|---------|-----------|-----------------|------------|
| Alpha buffer mode (fbzMode bit 18) | uniform-mapping.md mentions it | No dedicated task | **GAP** -- rarely used but should be noted |
| Rotating stipple mode (scanline offset) | CHECKLIST 7.1 mentions it briefly | Acceptance criteria unclear | **WEAK** |
| YUV texture format decode | texture-formats.md documents it | Covered by CPU-side decode | OK (implicit) |
| NCC table decode | texture-formats.md, CHECKLIST 3.6 | Explicit task | OK |
| Chromarange (fbzMode bit 25+) | Not mentioned | No task | **GAP** -- Voodoo 2+ feature, rarely used |
| SLI compositing | DESIGN.md resolved Q11: "defer" | No task (intentional deferral) | OK |
| Texture rectangle clamp mode | Not explicitly mentioned | Implied in sampler management | **WEAK** |
| Color mask (fbzMode bit 10) | DESIGN.md mentions write masks | Implicit in pipeline state | OK |
| Y-origin (fbzMode bit 17) | Not explicitly mentioned | No task | **GAP** -- affects framebuffer Y direction |

### F.2 Risk Assessment for Gaps

- **Alpha buffer mode**: Extremely rare in games. Can defer.
- **Chromarange**: Voodoo 2+ only, rarely used by games. Can defer.
- **Y-origin**: Used by some games to flip rendering direction. Should be added to CHECKLIST Phase 7 as a minor task, as it affects vertex Y coordinate interpretation.
- **Rotating stipple**: The implementation note in task 7.1 mentions it but lacks specific acceptance criteria.

---

## G. Platform Gaps

### G.1 Platform Coverage Matrix

| Feature | macOS (MoltenVK) | Windows | Linux (Mesa) | Pi 5 (V3D) |
|---------|------------------|---------|--------------|-------------|
| Vulkan 1.2 baseline | DESIGN.md: yes | DESIGN.md: yes | DESIGN.md: yes | DESIGN.md: yes |
| volk loader | cmake-integration: yes | cmake-integration: yes | cmake-integration: yes | cmake-integration: yes |
| VK_EXT_extended_dynamic_state | vulkan-arch: detected at runtime | Yes | Yes | vulkan-arch: "likely" |
| MoltenVK blend limitation | DESIGN.md: documented | N/A | N/A | N/A |
| Pipeline cache path | CHECKLIST 11.4 | Implied | Implied | Implied |
| Validation layer testing | CHECKLIST 11.5 | CHECKLIST 11.5 | CHECKLIST 11.5 | CHECKLIST 11.5 |
| Qt VCRenderer surface | vc-plumbing: QVulkanInstance | Implied | Implied | Implied |

### G.2 Platform-Specific Issues

| Issue | Document | Assessment |
|-------|----------|------------|
| MoltenVK no extended_dynamic_state3 (blend) | DESIGN.md, vc-shader.md: documented | OK |
| MoltenVK Metal translation quirks | vc-debug.md: mentioned | OK |
| Pi 5 unified memory | vc-plumbing.md line 102: mentioned | OK |
| Pi 5 Vulkan 1.2 feature level | vulkan-architecture.md: documented | OK |
| Pi 5 VK_EXT_extended_dynamic_state support | vulkan-architecture.md: "likely supported" | **UNVERIFIED** |
| Windows VK_KHR_win32_surface | vc-arch.md: listed | OK |
| Linux Wayland vs Xlib surface | vc-arch.md: listed | OK (both mentioned) |
| macOS VK_MVK_macos_surface | vc-arch.md: listed | OK |
| cmake platform defines | cmake-integration.md: all 4 platforms | OK |

### G.3 Platform Gaps

1. **Pi 5 VK_EXT_extended_dynamic_state**: Listed as "likely supported" but not verified. Should be confirmed with `vulkaninfo` on a Pi 5 or from V3D driver docs. The fallback path (CHECKLIST 11.6) covers this, but verification would be useful.

2. **Windows build testing**: No explicit early-phase Windows build validation task. The cross-platform validation is deferred to Phase 11. Consider adding a Phase 1 note to verify Windows compilation.

3. **Qt Vulkan surface creation**: Only `QVulkanInstance::surfaceForWindow()` is mentioned. Need to verify this works across all Qt platform plugins (xcb, wayland, cocoa, windows).

---

## Summary of Findings

### Critical Issues (Must Fix Before Implementation)

| # | Issue | Document(s) | Fix Required |
|---|-------|-------------|-------------|
| C1 | Fog table `sampler1D` in uniform-mapping.md | uniform-mapping.md lines 355, 537 | Change to `sampler2D` (64x1), add note about Vulkan SPIR-V limitation |
| C2 | Perspective contradiction (W=1 vs W encoding) | emulator-survey.md section E.2 | Add note: "Superseded by perspective-correction.md Approach C" |
| C3 | Push constant size "~80 bytes" | vulkan-architecture.md line 59 | Update to "64 bytes (see push-constant-layout.md)" |

### Medium Issues (Should Fix)

| # | Issue | Document(s) | Fix Required |
|---|-------|-------------|-------------|
| M1 | vc-lead.md references `find_package(Vulkan)` | vc-lead.md line 45 | Change to "volk dynamic loading" per cmake-integration.md |
| M2 | uniform-mapping.md pervasive GL terminology | uniform-mapping.md (~40 refs) | Rewrite GL API calls to Vulkan equivalents |
| M3 | texture-formats.md GL upload section | texture-formats.md upload section | Rewrite to Vulkan staging buffer workflow |
| M4 | dither-blend-ordering.md GL blend factors | dither-blend-ordering.md (~20 refs) | Replace GL_* with VK_BLEND_FACTOR_* |
| M5 | intercept-point.md title says "GL Rendering" | intercept-point.md line 1 | Change to "Vulkan Rendering" |
| M6 | vc-debug.md shader path wrong | vc-debug.md line 107 | Fix to `src/video/videocommon/shaders/` |
| M7 | vc-arch.md lists vk-bootstrap | vc-arch.md line 97 | Remove or note "not adopted" |

### Low Issues (Nice to Fix)

| # | Issue | Document(s) | Fix Required |
|---|-------|-------------|-------------|
| L1 | testing-strategy.md "FBO" references | testing-strategy.md lines 464, 474 | Replace with "VkFramebuffer" |
| L2 | perspective-correction.md GL spec refs | perspective-correction.md | Add note: concepts apply to Vulkan too |
| L3 | emulator-survey.md UBO recommendation | emulator-survey.md section D.4 | Note: "Superseded -- using push constants" |
| L4 | DESIGN.md GL comparison table | DESIGN.md lines 638-658 | Acceptable (intentional comparison) |
| L5 | Y-origin (fbzMode bit 17) not covered | CHECKLIST.md | Add to Phase 7 |
| L6 | Alpha buffer mode not covered | CHECKLIST.md | Add note to Phase 7 (defer) |
| L7 | Chromarange not covered | CHECKLIST.md | Add note to Phase 7+ (defer) |

---

## Verdict: READY

> **Update 2026-02-26**: All 3 critical and 7 medium issues have been resolved.
> A full GL→Vulkan terminology pass was performed on uniform-mapping.md,
> texture-formats.md, and dither-blend-ordering.md. See fixes below.

The planning documentation is architecturally sound and thoroughly updated for
Vulkan 1.2. The 11-phase plan, 107-task checklist, push constant layout, and all
research documents now consistently use Vulkan terminology. Implementation can begin.

### Resolved Critical Issues

| # | Issue | Resolution |
|---|-------|------------|
| C1 | Fog table `sampler1D` in uniform-mapping.md | Fixed → `sampler2D` (64x1 VkImage) at all 3 locations |
| C2 | Perspective contradiction (W=1 vs W encoding) | Fixed → emulator-survey.md E.2 + C.5 rewritten to match perspective-correction.md Approach C |
| C3 | Push constant size "~80 bytes" | Fixed → "64 bytes (see push-constant-layout.md)" |

### Resolved Medium Issues

| # | Issue | Resolution |
|---|-------|------------|
| M1 | vc-lead.md `find_package(Vulkan)` | Fixed → "volk dynamic loading" |
| M2 | uniform-mapping.md GL terminology (~40 refs) | Fixed → Full Vulkan pass: VkBlendFactor, VkCompareOp, VkSampler, push constants, etc. |
| M3 | texture-formats.md GL upload section | Fixed → Vulkan staging buffer + vkCmdCopyBufferToImage workflow |
| M4 | dither-blend-ordering.md GL blend factors (~20 refs) | Fixed → VK_BLEND_FACTOR_*, VkPipeline, vkCmdCopyImage, etc. |
| M5 | intercept-point.md title says "GL Rendering" | Fixed → "Vulkan Rendering" |
| M6 | vc-debug.md shader path wrong | Fixed → `src/video/videocommon/shaders/` |
| M7 | vc-arch.md lists vk-bootstrap | Fixed → strikethrough with "Not adopted (DESIGN.md Q15)" |

### Remaining Low Issues (deferred, non-blocking)

| # | Issue | Status |
|---|-------|--------|
| L1 | testing-strategy.md "FBO" references (2 instances) | Deferred — minor |
| L2 | perspective-correction.md GL spec refs | Acceptable — concepts transfer to Vulkan |
| L3 | emulator-survey.md UBO recommendation | Acceptable — exploratory context |
| L5 | Y-origin (fbzMode bit 17) not in CHECKLIST | Add to Phase 7 when starting |
| L6 | Alpha buffer mode not covered | Add to Phase 7 when starting |
| L7 | Chromarange not covered | Add to Phase 7+ when starting |
