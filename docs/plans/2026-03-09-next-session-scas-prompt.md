# Next Session Prompt: Re-rank The Next Low-Risk Target

> Historical prompt note (updated 2026-03-09): this prompt was also consumed later the same day. The recommended follow-up audit was completed, `SAHF` / `LAHF` (`0x9e` / `0x9f`) was chosen, and that pair has since landed and guest-validated on `Windows 98 SE`. Treat the body below as preserved planning history, not current execution guidance. For current branch status, use [new-dynarec-executive-summary.md](./new-dynarec-executive-summary.md), [new-dynarec-changelog.md](./new-dynarec-changelog.md), and [new-dynarec-optimization-overview.md](./new-dynarec-optimization-overview.md).

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
- `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/plans/2026-03-09-next-session-scas-prompt.md`

Current required context:
- stable guest-facing state remains the current branch state with direct `BSF/BSR`, direct `0x0f 0xaf`, and direct `D0-D3` `RCL/RCR` disabled
- host-side semantics harness support for `0x0f 0xaf`, `BSF/BSR`, and rotate-through-carry remains in tree
- the narrow `D0-D3` compare/debug path is in tree and has already shown broad match-only evidence; do not spend the session extending that tooling unless a concrete blocker appears
- `CMPS` (`0xa6` / `0xa7`) is now in tree and guest-validated on `Windows 98 SE`
- the latest logged `Windows 98 SE` shutdown report is `/tmp/windows98_se_cmps_validation.log`
- that shutdown report contains no base-fallback entries for `0xa6` / `0xa7`
- base `0xae` / `0xaf` (`SCASB` / `SCASW` / `SCASD`) direct coverage is now in tree locally
- focused local verification already passed:
  - `cc -std=c11 -DUSE_NEW_DYNAREC -Isrc/include -Isrc/cpu tests/codegen_new_opcode_coverage_policy_test.c src/codegen_new/codegen_observability.c -o /tmp/codegen_new_opcode_coverage_policy_test && /tmp/codegen_new_opcode_coverage_policy_test`
  - `cmake --build out/build/llvm-macos-aarch64.cmake --target 86Box -j4`
  - `codesign -s - --force --deep out/build/llvm-macos-aarch64.cmake/src/86Box.app`
- `/tmp/windows98_se_scas_validation.log` reached shutdown with:
  - `CPU new dynarec fallback families [shutdown]: base=20856 0f=5565 x87=4568 rep=9016 3dnow=0`
  - no shutdown base-fallback entries for `0xae` or `0xaf`
- repeated manual reruns were reported stable enough to treat the first blue screen as non-reproducible
- do not revert unrelated changes
- `Windows 98 SE` is the standing strict-i686 VM going forward:
  - `/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 SE`

Session goal:
Choose the next low-risk CPU dynarec target now that the non-REP compare-string sibling family is closed.

Primary task:
- refresh the measured opportunity ranking before implementing another opcode family

Why this target:
- the last same-class low-risk table-hole sibling is now landed and guest-validated
- the remaining hot families are more system-heavy or already known guest-risky
- the next safe move is to re-rank from current evidence instead of guessing

Hard constraints:
- CPU new dynarec scope only
- ARM hard constraint still applies: no ARM code above ARMv8.0-A
- do not guest-enable `D0-D3` `RCL/RCR`
- do not retry guest enablement for `0x0f 0xaf` or `BSF/BSR`
- do not choose port-I/O, interrupt, segment, or other system opcode families as side work
- keep the work narrow and evidence-led

Required work:
1. Start from current head and the maintained docs/logs, including `/tmp/windows98_se_scas_validation.log`.
2. Re-rank the next candidates from the latest strict-i686 shutdown evidence instead of the pre-`SCAS` ranking.
3. Prefer table-hole or other low-risk coverage work; avoid reopening the paused high-risk guest-regressed families.
4. Write down the narrowed recommendation before implementing anything.
5. If a new target is clearly low-risk, follow the same host-test-first workflow.

Desired outcome:
- the next target is chosen from current evidence rather than stale pre-`SCAS` assumptions
- docs stay aligned with the branch state where both `CMPS` and `SCAS` are guest-validated on `Windows 98 SE`
- docs stay aligned with the current branch state and standing VM choice
```
