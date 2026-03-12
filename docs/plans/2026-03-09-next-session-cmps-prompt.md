# Next Session Prompt: Continue After CMPS Validation

> Historical prompt note (updated 2026-03-12): this prompt was consumed later the same day. `SCAS` (`0xae` / `0xaf`) has since landed and guest-validated, the branch has also since landed and guest-validated `SAHF` / `LAHF` (`0x9e` / `0x9f`), and later work closed the large measured REP string subset on `Windows 98 SE`. Treat the body below as preserved session history, not the current next-step instruction. For current branch status, use [new-dynarec-executive-summary.md](./new-dynarec-executive-summary.md), [new-dynarec-changelog.md](./new-dynarec-changelog.md), and [new-dynarec-optimization-overview.md](./new-dynarec-optimization-overview.md).

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
Continue from the now-validated `CMPS` landing and take the next same-class low-risk CPU dynarec table-hole family.

Recommended target:
- base `0xae` / `0xaf` (`SCASB` / `SCASW` / `SCASD`)

Why this target:
- the previous low-risk target (`CMPS`, `0xa6` / `0xa7`) is now in tree and guest-validated on `Windows 98 SE`
- `SCAS` is the obvious sibling family and still appears in the latest shutdown log as plain table holes:
  - `CPU new dynarec base fallbacks [shutdown]: opcode=0xae helper_table_null=33 helper_bailout=0`
  - `CPU new dynarec base fallbacks [shutdown]: opcode=0xaf helper_table_null=16 helper_bailout=0`
- it reuses the same string-op infrastructure and the same lazy `SUB`-style flag surface as `CMPS`
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
2. Implement the smallest coherent direct path for base `0xae` / `0xaf`.
3. Add or update focused host-side tests first.
4. Update planning/docs/changelog to record that `CMPS` is now guest-validated on `Windows 98 SE` and that `SCAS` is the new low-risk target.
5. Verify with focused local tests/build.
6. Only do one narrow guest validation run if the local evidence is clean.

Desired outcome:
- the branch is back on low-risk implementation work instead of open-ended debug probing
- `0xae` / `0xaf` direct coverage is in tree or clearly blocked by specific evidence
- docs reflect that `D0-D3` compare work is paused and why
```

Status update after this prompt was executed:

- base `0xa6` / `0xa7` direct coverage is now in tree through shared non-REP `CMPS` handlers
- focused local policy/build verification passed
- guest validation on `Windows 98 SE` reached a normal shutdown with no `0xa6` / `0xa7` base-fallback entries
- the next recommended target is non-REP `SCAS` (`0xae` / `0xaf`)
- it is still plain table-hole traffic (`helper_table_null`), not bailout cleanup
- it should reuse the same string-op infrastructure and the same lazy `SUB`-style flag pattern
- it avoids reopening `0x0f 0xaf`, `BSF/BSR`, `D0-D3`, port-I/O, or protected/system opcode work

Hard constraints:
- CPU new dynarec scope only
- ARM hard constraint still applies: no ARM code above ARMv8.0-A
- do not guest-enable `D0-D3` `RCL/RCR`
- do not retry guest enablement for `0x0f 0xaf` or `BSF/BSR`
- do not choose port-I/O, interrupt, segment, or other system opcode families as side work
- keep the new work narrow and table-hole oriented

Required work:
1. Audit the existing non-REP string-op direct handlers and coverage-policy tests.
2. Implement the smallest coherent direct path for base `0xae` / `0xaf`.
3. Add or update focused host-side tests first.
4. Update planning/docs/changelog to record the validated `CMPS` result and the new `SCAS` target.
5. Verify with focused local tests/build.
6. Only do one narrow guest validation run on `Windows 98 SE` if the local evidence is clean.

Desired outcome:
- `0xae` / `0xaf` direct coverage is in tree or clearly blocked by specific evidence
- the next guest shutdown report no longer contains `0xae` / `0xaf` base-fallback entries
- docs stay aligned with the current branch state and standing VM choice
```
