# arm64-and-pacing Build Confirmation and Cleanup Plan

## Build Confirmation

- Date: 2026-04-28
- Branch: `arm64-and-pacing`
- Result: Clean configure/build/codesign completed successfully via:
  - `./scripts/setup-and-build.sh build`
- Artifact produced:
  - `build/src/86Box.app`

## Intent (Next Phase)

This branch currently includes extra telemetry/debug/reporting scaffolding that was useful during investigation but is not part of the core functional code path.

Planned next-phase objective:
- Strip non-essential telemetry/logging/counter/debug cruft and keep only the code required for actual emulator behavior.

Examples of likely cleanup targets (to be audited deeply later):
- ad-hoc telemetry counters and periodic reports
- temporary diagnostic logging and timing prints
- experiment-only instrumentation paths
- branch-specific debug toggles/markers that do not affect functional behavior

## Scope Note

- No deep removal pass is being done in this step.
- This document records build status and the agreed cleanup direction only.
- A full inventory-and-removal pass will be scheduled separately.
