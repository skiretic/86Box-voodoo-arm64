# Branch Diary: ndr-pacing-lab

Last updated: 2026-04-28

## Why this exists

This is the "what actually happened" log for this line of work so cleanup and upstreaming can be done without re-reading the whole graph every time.

## PR intent summary

This branch line is focused on ARM64 dynarec improvement work with validated 3DNow bring-up and a scoped Qt pacing follow-up. The pacing lane reached a provisional stop after C7 A2, then was briefly reopened for C8 telemetry-sampling correctness (measurement fix only) before returning to cleanup/consolidation for upstreaming.

Planned delivery strategy:

- create a fresh integration branch from latest `upstream/master`
- rebase/cherry-pick only intended functional changes from this line of work
- verify behavior on the rebased branch (correctness + pacing/perf sanity)
- strip branch-local logging, telemetry noise, and non-essential cruft
- clean up commit history into one coherent PR-ready series, then present as one large PR

What this should communicate to reviewers:

- the work was phased, measured, and corrected in-place when experiments failed
- non-working trial paths were explicitly reverted instead of left dormant
- upstream submission will prioritize correctness and validated behavior over branch-local narrative docs/logging

## Scope and split points

Current branch: `ndr-pacing-lab`
Author scope for this diary: `skiretic` commits only (non-`skiretic` upstream commits in ancestry are intentionally excluded from the summaries below).

- `master` divergence base: `123c135cf5ce703e0a406e88f6f49677370e7cd7`
- `ndr-analysis` tip anchor: `7205f82e9c045ff335af2570880f8c2630f76a12`
- `ndr-3dnow-lab` tip anchor: `d891888615e0273a100169c38be123ab55d87334`
- `ndr-pacing-lab` created from `ndr-3dnow-lab` on 2026-04-26

Commit counts along this path:

- `master..ndr-analysis`: 52 (`skiretic` authored)
- `ndr-analysis..ndr-3dnow-lab`: 48 (`skiretic` authored)
- `ndr-3dnow-lab..ndr-pacing-lab`: 17 (`skiretic` authored)
- total from `master` base to current `ndr-pacing-lab`: 117 (`skiretic` authored)

## High-level timeline

### Phase 1: ARM64 dynarec NDR lanes (S-01/S-02/S-03 + A-013)

Date window: 2026-04-21 to 2026-04-22

What changed:

- built wave workflow and Win98 microstress harness
- landed ARM64 lowering fixes (`mov_imm`, direct imm-store hooks)
- added and tuned telemetry infrastructure (capture scripts + parsers)
- ran A-013 branch-shaping expansion (`BL/B`, `CBNZ`, `BEQ`, `BCOND`, `TBZ/TBNZ`)
- set 266 MHz baseline/gating protocol and closed A-013 lane

Key code commits:

- `d88433828` S-01 MMX branch patch fix
- `626abd938` S-02a direct imm-store hooks
- `1b2185e94` S-02b bounded `mov_imm` lowering
- `b9d3c2f48` S-03a churn telemetry counters
- `4fae4eab6` S-03b delayed no-immediates policy
- `0dbe439b5` A-013a+b branch classification + direct `BL/B`
- `c08cf225b` A-013 CBNZ path shaping
- `a89166718` A-013 BEQ + nonlocal-rel branches
- `07e0914e0` A-013 BCOND patch path
- `c3776d99c` A-013 TBZ/TBNZ guarded path
- `318688e58` S-03c retry-decay
- `5a167d5c7` S-03d threshold tuning
- `79580357a` S-03e burst-aware escalation

### Phase 2: ARM64 3DNow bring-up + validation

Date window: 2026-04-22 to 2026-04-26

What changed:

- created Win98 3DNow coverage harness and execution flow
- enabled phase-1 3DNow table + counters
- iterated on PFACC correctness/perf path
- brought up ext/base opcode lanes (`PFRSQRT`, `PMULHRW/PAVGUSB`, `PI2FW`, `PFNACC/PFPNACC`, `PSWAPD`, companion decode paths)
- explicit cleanup discipline: revert non-working bring-up attempts, keep validated paths
- late-phase pfrcp/pfrsqrt temp-pressure and recip-refine work

Key code commits:

- `1144ccbea` add 3DNow coverage harness + telemetry + plan
- `29b35f4a7` enable phase-1 3DNow table with coverage counters
- `0af273502` fix `pfrsqrt` lowerer flow
- `190d47f23` enable PFACC dynarec path
- `3810154a6` PFACC lane math fix
- `96f339e65` PFACC src-dst alias fix
- `695464359` PFACC compose fix via ZIP1
- `27deec595` PFACC fix via FADDP pairwise add
- `6fc131450` enable/validate PMULHRW + PAVGUSB path
- `45d89f7d5` add `0f0d/0f0e` base companion paths
- `d27c076b6` fix PI2FW 3DNowExt lane insert
- `b55b1162c` enable PFNACC + PFPNACC ext paths
- `057ec68ae` enable/validate PSWAPD ext path
- `17fd90605` lock pfrcpit no-op elision
- `b5783aca5` trim pfrcp/pfrsqrt temp pressure
- `35e78b3f4` alias-safe pfrcp fix
- `4736f8724` recip-refine telemetry
- `378e85f27` one-shot low-return mem-path temp trims

Notable reverted experiments (already self-cleaned in history):

- `7b77f82b1` then reverted by `2f8f2c865` (PF2IW real path)
- `bde76361a` then reverted by `265e77bf5` (PSWAPD real path)

### Phase 3: Qt pacing lane (`ndr-pacing-lab` split)

Date window: 2026-04-26 to 2026-04-27

What changed:

- introduced single-step `pc_run` pacing in Qt main loop
- staged C1 and C3 pacing slices with gate evidence
- attempted C5 mailbox/coalescing handoff, then rejected/reverted after A2 gate fail
- moved gate emphasis to smoothness-first and clarified post-Qt baseline lineage
- completed C7 A2 retain/trim pass
- reopened C8 telemetry-sampling correction to normalize speed samples before final branch cleanup (no C8 re-lock run set executed in this branch scope)

Key commits:

- `ee4d5c5ae` pace main loop with single-step `pc_run`
- `b74a056cb` C1 phase-1 pacing slice
- `6ba0dcab9` C3 A1 pass, cooperative endblit contention yield
- `532de825b` C5 mailbox/coalescing handoff attempt
- `9f8058a4c` reject C5 and revert rendererstack mailbox path
- `87a24f060` C7 A1 working patch restoration before locked retest sequence
- `88e96fd1f` C7 A2 retain/trim landing with finalized retest docs
- `bac6b8d1d` C8 telemetry-sampling correctness landing (measurement fix only)

## Upstream cleanup map (suggested)

### Stack A: ARM64 dynarec telemetry and branch-shaping core

Target: keep code-path commits that changed runtime behavior and correctness.

Candidate keep set includes S-02/S-03/A-013 code commits and parser/launcher commits tied to repeatable validation.

### Stack B: ARM64 3DNow functional bring-up

Target: keep opcode enable/fix commits that ended in validated states.

Drop/avoid exporting dead trial points; their reverts already neutralize most of this.

### Stack C: Qt pacing changes

Target: keep `ee4d5c5ae`, C1/C3 validated behavior, C7 A2 final state, and C8 telemetry-sampling correction; exclude C5/C6 rejected paths. C8 is retained as a measurement-definition update without branch-local re-lock expansion.

### Stack D: Tooling/harnesses

Target: optionally upstream helper tooling if maintainers want it.

- `tools/win98-microstress/*`
- `tools/win98-3dnowcov/*`
- `scripts/dynarec/*`

### Stack E: Docs

Target: do **not** upstream most branch-local execution diaries as-is.

Convert to short upstream PR notes/changelogs; keep the deep logs in fork docs/archive.

## Quick triage tags for cleanup session

Use these tags when rewriting history into upstream PR stacks:

- `KEEP-CODE`: validated behavioral/correctness commit
- `KEEP-TOOL`: helper scripts/harness (optional upstream)
- `SQUASH-DOC`: doc-only commit that should be collapsed or left fork-local
- `DROP-EXP`: experimental attempt superseded/reverted

## Commands to refresh this diary later

```bash
base=$(git merge-base ndr-pacing-lab master)
a=7205f82e9c045ff335af2570880f8c2630f76a12
b=d891888615e0273a100169c38be123ab55d87334

# phase logs
 git log --reverse --date=short --format='%h %ad %s' --no-merges --author='skiretic' $base..$a
 git log --reverse --date=short --format='%h %ad %s' --no-merges --author='skiretic' $a..$b
 git log --reverse --date=short --format='%h %ad %s' --no-merges --author='skiretic' $b..ndr-pacing-lab

# code-heavy view (exclude docs)
 git log --reverse --oneline --no-merges --author='skiretic' $base..ndr-pacing-lab -- . ':(exclude)docs/**'
```

## Notes

- working tree state changes frequently; use `git status --short` for current local edits.
- this diary is descriptive; it does not change commit history.

## Post-upstream-sync sanity check (2026-04-29)

Purpose: validate that logging and pacing behavior still look healthy after pulling upstream commits into `ndr-pacing-lab` while preserving local instrumentation files.

Run:
- tag: `pacing-validate-r1`
- workload order: `Q3 demo four -> 3DMark99 full loop -> MRUNALL`
- markers: `seq=0..3` valid
- WL-05 hashes: quick `45db7b65`, normal `2520dd5e`, smc `b86f22a1` (`status=OK`)

Host-log sanity:
- `DYNAREC_S03A_SUMMARY` streaming and advancing
- `DYNAREC_3DNOW_SUMMARY tag=final total=3581 recompiled=3581 fallback=0`
- `DYNAREC_3DNOW_OPSUMMARY` present
- `EMU_SPEED_SAMPLE` present with `sample_ms` ~`999-1001`
- no `ERROR/assert/panic` hits

Smoothness snapshot (`r1`):
- samples `537`
- avg `99.706`, p99 `102`, min `86`, max `104`
- `<95 = 15` (`2.79%`)
- `<90 = 1` (`0.19%`)
- `>103 = 2` (`0.37%`)
- `>=105 = 0`
- crossings `102`

Comparison to active pre-upstream lock (`baseline-lock-2026-04-27-c3-reconfirm-3run` aggregate):
- low-tail and average are within expected single-run noise; no obvious regression signal
- crossings slightly higher than lock mean (`102` vs `97.33`) but not actionable from one run
- interpretation: logging/pacing path appears healthy after upstream sync; use standard 3-run aggregate for any formal re-lock decision

## Post-upstream-sync telemetry smoke (2026-05-10, `master-new`)

Purpose: verify that the `master-new <- upstream/master` merge preserved expected telemetry/logging behavior before push.

Merge context:
- `master-new` was synced from `upstream/master` with merge commit `d00b9e4e6`
- backup anchor created before merge: `backup/master-new-pre-sync-20260510-142433`

Run:
- launcher: `./scripts/dynarec/launch-vm-telemetry-run.sh s03a`
- run dir: `docs/perf-artifacts/arm64-dynarec/2026-05-10_14-26-33-Windows 98 Gaming PC-s03a`
- launch status: `launch_failed=0`

Host-log sanity:
- `DYNAREC_S03A_SUMMARY` streaming and advancing
- `EMU_SPEED_SAMPLE` present and continuing during run
- no `ERROR/fatal/assert/segfault/abort/panic` hits in `86box.log`

Sampling snapshot:
- `EMU_SPEED_SAMPLE` count `582`
- `sample_ms` min `426`, max `1004`, avg `999.014`
- dominant steady-state cadence remains ~`1s` (`998-1001` seen repeatedly near tail)

Interpretation:
- telemetry pipeline and core logging behavior remained intact after sync
- no immediate post-merge runtime/logging regression signal observed in this smoke run
