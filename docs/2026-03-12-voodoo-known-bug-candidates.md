# Voodoo Known Bug Candidates

Date: 2026-03-13
Branch: `voodoo-dev`
Status basis: current local branch state after Tasks 1 through 7 are implemented locally

## Purpose

This is a short working list of likely remaining bugs or correctness risks in the Voodoo renderer/JIT stack.

It is not a full architecture review. It is meant to answer:

- what bugs likely still exist
- which ones are most worth checking next
- which ones are historical weak spots rather than freshly proven defects

## Executive Summary

The most likely remaining correctness issues are no longer the ones already fixed in this session.

The best current bug candidates are:

1. newly widened output-alpha parity in both JITs is still under-validated in real runtime content
2. x86-64 live behavior of the new output-alpha path is still unverified in this workspace
3. rare blend-factor combinations remain lightly exercised in real games
4. historically fragile fog, transparency, TMU ordering, depth bias, and render-thread timing paths may still hide edge-case regressions

The broad smoke coverage from `3DMark99` and `3DMark2000` plus the signed-release sanity pass is reassuring, but it does not fully replace game-specific checks like `Extreme Assault` and `Lands of Lore III`.

## High-Confidence Remaining Candidates

### 1. JIT output-alpha parity is implemented but still under-validated

Confidence: High
Why it is still likely:

- the interpreter no longer uses the old `AONE`-only alpha writeback logic
- both JITs now have local parity patches, but those paths have only build-level verification in this session
- the historically fragile alpha-buffer path still needs runtime evidence in real games

What it could affect:

- alpha-plane scenes
- transparency/HUD behavior
- any workload that depends on non-`AFUNC_AONE` alpha-for-alpha factors

Best checks:

- `Lands of Lore III`
- `Extreme Assault`
- `Half-Life 1`

### 2. x86-64 live validation is still missing

Confidence: High
Why it is still likely:

- the x86-64 JIT parity change has only been syntax-checked locally
- x86-64 runtime testing remains unavailable in this workspace
- the x86-64 JIT is older and still a believable source of subtle live-only regressions

What it could affect:

- alpha-plane scenes on x86-64 hosts
- transparency/HUD behavior
- regressions that only appear with real cached x86-64 blocks at runtime

Best checks:

- `Lands of Lore III`
- `Extreme Assault`
- `Unreal Gold`
- `3DMark99`

### 3. Rare blend-factor combinations are still under-tested

Confidence: Medium-High
Why it is still likely:

- the interpreter and both JITs now support a broader factor vocabulary than before
- historical notes in this repo indicate the software path had mostly exercised `4 = ONE`
- build-level verification is good, but runtime evidence is still limited

What it could affect:

- uncommon alpha blend states
- alpha-plane aux writes through the LFB path
- rare scenes that synthetic benchmarks do not hit

Best checks:

- `Lands of Lore III`
- any title known to rely on alpha planes or mask behavior

## Historical Weak Spots Likely To Hide More Bugs

### 4. Fog-path corner cases

Confidence: Medium
Why:

- there are already historical fixes for wrong `FOG_Z` shifts and fog alpha handling
- those fixes suggest this area is easy to get subtly wrong

Best checks:

- `Unreal Gold`
- `3DMark99`
- `3DMark2000`
- `Need for Speed II: SE` or `III`

### 5. Transparency and HUD-specific behavior

Confidence: Medium
Why:

- `Extreme Assault` already needed a targeted transparency/HUD fix
- the alpha pipeline is one of the least robustly documented parts of the renderer

Best checks:

- `Extreme Assault`
- `Half-Life 1`

### 6. TMU ordering and dual-TMU interactions

Confidence: Medium
Why:

- ARM64 already required a fix for TMU1 negate ordering
- dual-TMU paths are state-heavy and easy to miscompile

Best checks:

- `Unreal Gold`
- `Turok demo`

### 7. Depth bias and aux/depth interactions

Confidence: Medium
Why:

- ARM64 already needed a depth-bias clamp correction
- aux/depth behavior is tightly coupled to the same state machinery involved in alpha planes and tiled-buffer modes

Best checks:

- `3DMark99`
- `3DMark2000`
- `Lands of Lore III`

### 8. Render-thread timing and contention behavior

Confidence: Medium
Why:

- ARM64 already needed contention-related fixes
- timing-sensitive problems may not appear in a short single run

Best checks:

- repeated demo loops in the same session
- longer `Unreal Gold` gameplay or attract/demo runs
- trying more than one render-thread setting when investigating odd behavior

## Structural Risk Areas

These are not proven bugs by themselves, but they make bugs more likely.

### 9. Interpreter/JIT contract is still too implicit

Confidence: Medium
Why it matters:

- JIT headers rely on outer-scope locals and macros from the interpreter context
- small changes can silently shift semantics between the interpreter and generated code

Practical takeaway:

- any future JIT change should be validated against the interpreter, not just against previous JIT behavior

### 10. x86-64 JIT internals are older and less evolved

Confidence: Medium
Why it matters:

- older cache structure
- more ad hoc layout than the ARM64 backend
- still treated as an important behavioral reference in places

Practical takeaway:

- small latent bugs in x86-64 remain believable even if ARM64 looks stable

## Bugs Already Addressed In This Session

These were strong candidates before this work and are now no longer the top unresolved items.

### Fixed: tiled cache-key aliasing between `col_tiled` and `aux_tiled`

Status: fixed in `cfdda4cae`

### Fixed locally, still awaiting broader validation: incomplete output-alpha blending

Status:

- interpreter and LFB path improved in `cf16e67c3`
- x86-64 JIT parity implemented locally and syntax-checked
- ARM64 JIT parity implemented locally and built on the active ARM64 environment

## Suggested Next Investigation Order

1. Run `Lands of Lore III` and `Extreme Assault` on the fresh ARM64 build.
2. Run `Unreal Gold`, `3DMark99`, and `3DMark2000`.
3. If those pass, treat x86-64 as build-verified but runtime-unverified until a suitable host is available.
4. Investigate only if a specific game or scene shows a real visual fault.

## Bottom Line

The most probable forgotten bugs are not random mysteries. They cluster around:

- JIT/interpreter parity for output alpha
- historically fragile fog/transparency/TMU/depth paths
- timing-sensitive rendering behavior under repeated state churn

If you want the shortest next bug-hunting path, use the fresh ARM64 build to cover `Lands of Lore III`, `Extreme Assault`, `Unreal Gold`, `3DMark99`, and `3DMark2000` before widening scope.
