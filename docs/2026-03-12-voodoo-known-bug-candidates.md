# Voodoo Known Bug Candidates

Date: 2026-03-12
Branch: `voodoo-dev`
Status basis: current local branch state after Tasks 1 through 4 of the gap-closure plan

## Purpose

This is a short working list of likely remaining bugs or correctness risks in the Voodoo renderer/JIT stack.

It is not a full architecture review. It is meant to answer:

- what bugs likely still exist
- which ones are most worth checking next
- which ones are historical weak spots rather than freshly proven defects

## Executive Summary

The most likely remaining correctness issues are no longer the ones already fixed in this session.

The best current bug candidates are:

1. output-alpha blending in the x86-64 JIT still lags the interpreter
2. output-alpha blending in the ARM64 JIT still lags the interpreter
3. rare blend-factor combinations in the interpreter are broader now, but still under-validated in real content
4. historically fragile fog, transparency, TMU ordering, depth bias, and render-thread timing paths may still hide edge-case regressions

The broad smoke coverage from `3DMark99` and `3DMark2000` is reassuring, but it does not fully replace game-specific checks like `Extreme Assault` and `Lands of Lore III`.

## High-Confidence Remaining Candidates

### 1. x86-64 JIT output-alpha parity is still incomplete

Confidence: High
Why it is still likely:

- the interpreter no longer uses the old `AONE`-only alpha writeback logic
- the x86-64 JIT still does
- this is an explicit plan item that has not yet been implemented

What it could affect:

- alpha-plane scenes
- transparency/HUD behavior
- any workload that depends on non-`AFUNC_AONE` alpha-for-alpha factors

Best checks:

- `Lands of Lore III`
- `Extreme Assault`
- `Half-Life 1`

### 2. ARM64 JIT output-alpha parity is still incomplete

Confidence: High
Why it is still likely:

- same gap as x86-64, but in the active Apple Silicon backend
- the ARM64 JIT currently mirrors the old reduced output-alpha handling
- recent ARM64 JIT history already shows multiple small correctness fixes in nearby pipeline logic

What it could affect:

- alpha-plane writes
- transparency/fog interactions
- ARM64-only corruption not visible in interpreter mode

Best checks:

- `Lands of Lore III`
- `Extreme Assault`
- `Unreal Gold`
- `3DMark99`
- `3DMark2000`

### 3. Newly broadened interpreter output-alpha coverage is still under-tested

Confidence: Medium-High
Why it is still likely:

- the interpreter now supports a broader factor vocabulary than before
- historical notes in this repo indicate the software path had mostly exercised `4 = ONE`
- the implementation is plausible and build-clean, but runtime evidence is still limited

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

### Partially fixed: incomplete output-alpha blending

Status:

- interpreter and LFB path improved in `cf16e67c3`
- x86-64 JIT still pending
- ARM64 JIT still pending

## Suggested Next Investigation Order

1. Finish x86-64 JIT output-alpha parity.
2. Finish ARM64 JIT output-alpha parity.
3. Run `Lands of Lore III` and `Extreme Assault`.
4. If those pass, investigate only if a specific game or scene shows a real visual fault.

## Bottom Line

The most probable forgotten bugs are not random mysteries. They cluster around:

- JIT/interpreter parity for output alpha
- historically fragile fog/transparency/TMU/depth paths
- timing-sensitive rendering behavior under repeated state churn

If you want the shortest next bug-hunting path, focus on the two JIT output-alpha tasks and use `Lands of Lore III` plus `Extreme Assault` as the first real-game sanity checks.
