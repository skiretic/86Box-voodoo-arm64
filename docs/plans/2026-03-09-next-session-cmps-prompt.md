# Next Session Prompt: Pivot Back To Low-Risk CPU Dynarec Work

Use this prompt to start the next session:

```text
Continue on branch `cpu-optimizations` in `/Users/anthony/projects/code/86Box-voodoo-arm64`.

Follow `AGENTS.md` and bootstrap/skill workflow first.

Start from current head and read first:
- `/Users/anthony/projects/code/86Box-voodoo-arm64/README.md`
- `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/plans/2026-03-09-guest-regression-planning-reset.md`
- `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/plans/2026-03-09-i686-opportunity-review.md`
- `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/plans/new-dynarec-executive-summary.md`
- `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/plans/new-dynarec-changelog.md`
- `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/plans/new-dynarec-optimization-overview.md`
- `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/plans/2026-03-09-next-session-cmps-prompt.md`

Current required context:
- stable guest-facing state remains the current branch state with direct `BSF/BSR`, direct `0x0f 0xaf`, and direct `D0-D3` `RCL/RCR` disabled
- host-side semantics harness support for `0x0f 0xaf`, `BSF/BSR`, and rotate-through-carry remains in tree
- the narrow `D0-D3` compare/debug path is in tree and has already shown broad match-only evidence; do not spend the session extending that tooling unless a concrete blocker appears
- the practical conclusion from the recent `D0-D3` work is: likely not ordinary rotate math/flags, so stop digging there for now
- do not revert unrelated changes
- `Windows 98 SE` is the standing strict-i686 VM going forward:
  - `/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 SE`

Session goal:
Pivot back to low-risk CPU dynarec implementation work and land the next small table-hole family that is more likely to stick than the recent debug-first bailout families.

Recommended target:
- base `0xa6` / `0xa7` (`CMPSB` / `CMPSW` / `CMPSD`)

Why this target:
- it is currently still missing from direct base-opcode dispatch
- it reuses the existing string-op infrastructure already used for `MOVS` / `STOS` / `LODS`
- it is a cleaner implementation task than more `D0-D3` compare instrumentation
- it avoids reopening `0x0f 0xaf`, `BSF/BSR`, protected-mode/system opcodes, or port-I/O work

Hard constraints:
- CPU new dynarec scope only
- ARM hard constraint still applies: no ARM code above ARMv8.0-A
- do not guest-enable `D0-D3` `RCL/RCR`
- do not retry guest enablement for `0x0f 0xaf` or `BSF/BSR`
- do not choose port-I/O, interrupt, segment, or other system opcode families as side work
- keep the new work narrow and table-hole oriented

Required work:
1. Audit the existing string-op direct handlers and coverage-policy tests.
2. Implement the smallest coherent direct path for base `0xa6` / `0xa7`.
3. Add or update focused host-side tests first.
4. Update planning/docs/changelog to describe the pivot away from `D0-D3` debug work and the new low-risk target.
5. Verify with focused local tests/build.
6. Only do one narrow guest validation run if the local evidence is clean.

Desired outcome:
- the branch is back on low-risk implementation work instead of open-ended debug probing
- `0xa6` / `0xa7` direct coverage is in tree or clearly blocked by specific evidence
- docs reflect that `D0-D3` compare work is paused and why
```

Status update after this prompt was executed:

- base `0xa6` / `0xa7` direct coverage is now in tree through shared non-REP `CMPS` handlers
- focused local policy/build verification passed
- guest validation on `Windows 98 SE` is still pending
