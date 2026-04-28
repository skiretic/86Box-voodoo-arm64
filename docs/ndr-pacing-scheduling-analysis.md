# Runtime Pacing and Scheduling Analysis

Branch: `ndr-pacing-lab`

Scope: Qt main loop pacing/cadence, debt and backlog handling, timer/wake/sleep cadence, scheduler interactions causing bursty work, and UI/main-thread contention paths tied to runtime pacing.

Primary objective: smoother feel and consistency around the `100%` target (frametime-style stability), not just higher average speed. Prefer runs that spend more time near `100%` with fewer low-tail dips and fewer abrupt sample-to-sample swings.

Secondary objective: higher host throughput for emulating faster-clocked guest CPUs is valuable, but treated here as a separate, larger refactor track after consistency fixes are stable.

Initial analysis constraints (historical, for first draft): analysis/report only; no code edits, patch application, commits, branch changes, or VM launch during the initial write-up pass.

## Current Branch Status (2026-04-27)

Consistency tuning on `ndr-pacing-lab` was paused after C7 A2, then briefly reopened for C8 telemetry-sampling correctness on 2026-04-27. C7 A2 remains landed by operator decision. C8 A1 is now landed as a measurement fix, and this branch stops without running a C8 re-lock.

## Defined Primary Workload Gate (Locked Baseline)

To preserve comparability with the existing lock, keep the original 3-run workflow unchanged:

1. Quake III timedemo (`demo four`)
2. 3DMark99
3. WL-05 microstress via `MRUNALL.BAT` (quick + normal + smc)

Run-shape requirements:

- Exactly 3 accepted runs count toward the gate, and each accepted run must be both clean and valid. Mark every attempt as `clean`, `noisy`, or `tainted`; noisy/tainted attempts do not count and must be replaced with the rejection reason recorded in the decision log.
- Phase-marker validity in each run: `start_seen=1`, `max_seq=3`, `valid_for_q3_3dmark_wl05=1`.
- `MRUNALL` must complete all three sub-modes with `MICRO_SUMMARY status=OK`.
- WL-05 totals must match locked canonical values for the fixed VM profile:
  - quick: `45db7b65`
  - normal: `2520dd5e`
  - smc: `b86f22a1`

Baseline references:

- `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/arm64-dynarec-phase-marker-baseline-protocol.md`
- active post-qt-pacing gate lock (current comparison baseline, fixed 266 MHz lane): `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/baseline-lock-2026-04-27-c3-reconfirm-3run.md`
- pre-qt-pacing anchor (historical reference before commit `ee4d5c5ae`, `qt: pace main loop with single-step pc_run`): `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec/baseline-lock-2026-04-25-3run.md`

## Artifact Location and Naming (Canonical)

Host-side run artifacts are stored under:

- `/Users/anthony/projects/code/86Box-voodoo-arm64/docs/perf-artifacts/arm64-dynarec`

Per-run directory shape (from telemetry launcher scripts):

- `${STAMP}-${VM_NAME}-${RUN_TAG}` (for example `2026-04-26_12-40-10-Windows 98 Gaming PC-<tag>`)
- required host files in each run dir:
  - `86box.log`
  - `run-metadata.txt`
  - `wl05-hashes.txt` (host-side transcription or copied guest output containing the quick/normal/smc totals for that run)

Guest-side WL-05 logs are written in VM at:

- `C:\PERF_LOG\wl05-micro-quick.log`
- `C:\PERF_LOG\wl05-micro-normal.log`
- `C:\PERF_LOG\wl05-micro-smc.log`
- `C:\PERF_LOG\wl05-micro-last.txt`

## Primary Gate Runbook (Operator)

1. Before launch, record host-noise notes in `run-metadata.txt` and label the attempt `clean`, `noisy`, or `tainted`. If known heavy background work is active, postpone the run instead of counting it.
   - minimum preflight host check commands:
     - `date`
     - `uptime`
     - `ps -axo pid,ppid,pcpu,pmem,comm | sort -k3 -nr | head -n 20`
     - `memory_pressure | sed -n '1,30p'`
     - `iostat -w 1 -c 2 | tail -n 10`
     - `pmset -g therm | sed -n '1,40p'`
   - preflight interpretation:
     - if thermal/performance warnings are present, mark run `noisy` and postpone.
     - if swap activity is non-zero or host load is clearly dominated by unrelated heavy work, postpone.
     - if unavoidable host activity exists but run proceeds, record it explicitly in `run-metadata.txt` and treat outliers as replaceable.
2. Launch telemetry run (host):
   - `env 86BOX_3DNOW_COV_STATS=1 ./scripts/dynarec/launch-vm-telemetry-run.sh <run_tag>`
   - capture `run_dir` from launcher output.
3. Execute locked workload in guest in order:
   - run Q3 demo four.
   - while switching from Q3 to 3DMark99 in the guest, press hotkey `1` to mark that transition.
   - run 3DMark99.
   - while switching from 3DMark99 to WL-05 in the guest, press hotkey `1` again to mark that transition.
   - run `D:\SCRIPTS\MRUNALL.BAT D:`.
   - after WL-05 completes, press hotkey `1` a third time while exiting/closing out the guest-side workload sequence to emit `seq=3`.
   - config lock: keep Q3 and 3DMark99 settings/profile identical to locked baseline runs (no settings changes during comparability runs).
4. Parse host log:
   - `./scripts/dynarec/analyze-emu-speed-log.sh <run_dir>/86box.log`
5. Validate per-run marker and correctness prerequisites:
   - `EMU_PHASE_MARKERS ... valid_for_q3_3dmark_wl05=1`
   - no correctness/hash anomalies in locked workload checks
   - if this attempt is noisy, tainted, or invalid, mark it `Rejected/Replaced` immediately and do not count it toward the 3-run aggregate
   - only evaluate aggregate thresholds after exactly 3 accepted clean valid runs exist
6. Validate WL-05 correctness totals from guest output/logs and persist them host-side in `<run_dir>/wl05-hashes.txt` (or an equivalent file referenced from `run-metadata.txt`):
   - quick `45db7b65`
   - normal `2520dd5e`
   - smc `b86f22a1`
7. After exactly 3 accepted clean valid runs exist, evaluate the smoothness-first primary gate and secondary guardrails from this document against the current active lock.
8. Record run verdict and notes in decision log (below).

## Consistency Pass/Fail Thresholds (Primary Gate)

Evaluate on 3-run aggregate against the locked baseline using a smoothness-first gate.

Primary smoothness gate (must pass):

- center-band occupancy must not regress versus lock:
  - `% samples in 99-101`
  - `% samples in 98-102`
- low-tail behavior must not regress versus lock:
  - `% samples <95`
  - `% samples <90`
- burst/swing behavior must not regress versus lock:
  - `% samples >103` (and `>=105` when present)
  - adjacent-sample jump severity (`|delta|` mean and `% jumps >=3/5/8`)
- no correctness/hash anomalies in locked workload checks

Secondary guardrails (still required):

- whole-run `avg >= 99.3`
- whole-run `p99 <= 103`
- whole-run `dips<95 <= 24`
- whole-run `dips<90 <= 12`
- whole-run `crossings <=` locked baseline aggregate; if `crossings` worsens, the slice fails the gate and may be retained only as non-promotable exploratory evidence until a re-lock or explicit gate change is approved

Notes:

- The smoothness metrics above are derived from whole-run raw `EMU_SPEED_SAMPLE percent=<n>` series (include startup, same as lock style).
- Optional warmup-trimmed summaries may be recorded as secondary context only; they are not the promotion gate.

## Non-Goals (This Document)

- Do not change the locked primary gate workflow (`Q3 demo four -> 3DMark99 -> WL-05/MRUNALL`) during consistency-track slices.
- Do not add new mandatory workloads to the primary gate in this phase.
- Do not treat throughput-track refactors (C2-class `pc_run` restructuring and related pipeline work) as in-scope for consistency slices.

## Decision Log (Working)

Use this table as the active tracker while implementing. Record one row per attempt, not one row per candidate. A candidate that gets a rethink pass should therefore appear as `A1`, `A2`, and so on.

Iteration policy:

- No first-try abandonment: if a candidate attempt fails gate criteria, do one focused rethink pass (hypothesis update + revised patch plan) before marking it rejected/deferred.
- Strategy-level defer carve-out: a candidate may be marked `Deferred` before first implementation attempt when this document explicitly places it in the deferred track (for example C2 throughput track).
- Log both attempts in this table with evidence and reason for the revised approach.
- Only mark a candidate `Rejected` after at least one documented revision attempt, unless a hard correctness/safety blocker is discovered.
- Experimental cleanup is required: if an attempt is rejected, remove broken/abandoned code paths instead of leaving them deactivated behind flags, dead branches, or commented-out blocks.
- `Decision` values: `Keep`, `Reject`, `Defer`.
- `Status` values: `Planned`, `In progress`, `Gate-failed`, `Reverted`, `Landed`, `Deferred`.

| Date | Candidate | Attempt | Decision | Status | Change/Notes | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| 2026-04-26 | C1 | A1 | Keep | Landed | Phase 1 landed in `src/qt/qt_main.cpp`: ns debt accounting + soft debt cap, single-run-per-pass preserved, no phase-2 bounded catch-up. Follow-on revisit retained: evaluate larger-cap/no-cap variants as non-default tuning after current consistency slices. | 3-run gate set: `c1-phase1-a1-r1b`, `c1-phase1-a1-r2c`, `c1-phase1-a1-r3`; markers valid in all runs; operator-confirmed WL-05 hashes matched lock. Aggregate vs active lock: avg `99.655` vs `99.638` (`+0.017`), p99 `102.000` vs `102.333` (`-0.333`), dips<95 `16.333` vs `16.667` (`-0.333`), dips<90 `2.667` vs `2.667` (`+0.000`), crossings `122.333` vs `127.333` (`-5.000`). |
| 2026-04-26 | C2 | A0 | Defer | Deferred | Throughput-track only; requires opcode stress + baseline signature before default-on. | Correctness gate + workload gate |
| 2026-04-26 | Q0 (carry-in) | A1 | Keep | Landed | Pre-C1 Qt pacing carry-in from prior branch state: `ee4d5c5ae` (`qt: pace main loop with single-step pc_run`) in `src/qt/qt_main.cpp`. This is the lane-defining post-qt-pacing base change that preceded C1 on `ndr-pacing-lab`. | Captured by post-qt-pacing lock artifact (`baseline-lock-2026-04-26-postqt-266-3run.md`) old-vs-new vs pre-qt-pacing lock: avg `+0.073667`, p99 `+0.333333`, dips<95 `-3.333333`, dips<90 `-5.333333`, crossings `+6.666667`. |
| 2026-04-27 | C3 | A1 | Keep | Landed | Phase 1 narrow patch in `src/qt/qt_platform.cpp` kept: replaced fixed contention sleep (`sleep_for(1 ms)`) with cooperative `std::this_thread::yield()` in `endblit()`. Lock-scope narrowing remains deferred. | 3-run gate set: `c3-a1-r1`, `c3-a1-r2`, `c3-a1-r3`; each run had valid markers (`start_seen=1`, `max_seq=3`, `valid_for_q3_3dmark_wl05=1`) and locked WL-05 hashes (`quick=45db7b65`, `normal=2520dd5e`, `smc=b86f22a1`, `status=OK`) recorded in per-run `wl05-hashes.txt`. Aggregate vs active lock: avg `99.661` vs `99.638` (`+0.023`), p99 `101.667` vs `102.333` (`-0.667`), dips<95 `15.667` vs `16.667` (`-1.000`), dips<90 `2.667` vs `2.667` (`+0.000`), crossings `118.000` vs `127.333` (`-9.333`). Aggregate vs previous landed code-change cluster (C1 A1): avg `+0.006`, p99 `-0.333`, dips<95 `-0.666`, dips<90 `+0.000`, crossings `-4.333`. |
| 2026-04-27 | C3 | A2 | Keep | Landed | Operator-directed baseline reconfirm on unchanged C3 code state (no additional code changes): fresh clean-host 3-run set captured to replace the active gate artifact for ongoing consistency work. | 3-run set: `c3-baseline-reconfirm-r1`, `c3-baseline-reconfirm-r2`, `c3-baseline-reconfirm-r3`; markers valid in all runs and WL-05 hashes locked in per-run `wl05-hashes.txt`. Aggregate vs superseded 2026-04-26 post-Qt lock: avg `99.567` vs `99.638` (`-0.071`), p99 `102.333` vs `102.333` (`+0.000`), dips<95 `19.333` vs `16.667` (`+2.667`), dips<90 `6.667` vs `2.667` (`+4.000`), crossings `97.333` vs `127.333` (`-30.000`). Frame-style note: lower crossing churn and no `>=105` overshoot in this set, with higher low-tail counts; promoted as new active lock by operator decision. |
| 2026-04-27 | C4 | A1 | Defer | Deferred | Voodoo/FIFO track intentionally deferred from current Qt pacing lane; do not execute in this phase. | Deferred by operator scope decision; exclude from current gate sequence. |
| 2026-04-27 | C5 | A1 | Keep | Gate-failed | Initial mailbox/coalescing patch in `src/qt/qt_rendererstack.cpp` used pending/current rectangle union when draining backlog. | Early run signal regressed jitter metrics: `c5-a1-r1` tainted outlier (`min=0`, replaced), accepted runs `c5-a1-r2b` + `c5-a1-r3b` still trended worse vs lock/C1 on `dips<90` and crossings (`r2b crossings=127`, `r3b crossings=165`). Focused rethink required before reject. |
| 2026-04-27 | C5 | A2 | Reject | Reverted | Focused rethink tested bounded latest-frame mailbox drain in `src/qt/qt_rendererstack.cpp` (no rectangle-union burst path), but consistency gate still regressed on jitter metrics. Cleanup completed: C5 mailbox/coalescing code paths removed and `qt_rendererstack` restored to pre-C5 behavior. | 3-run gate set: `c5-a2-r1`, `c5-a2-r2`, `c5-a2-r3`; markers valid in all runs and WL-05 hashes locked (`quick=45db7b65`, `normal=2520dd5e`, `smc=b86f22a1`, `status=OK`). Aggregate vs active lock: avg `99.553` vs `99.638` (`-0.085`), p99 `102.333` vs `102.333` (`+0.000`), dips<95 `19.333` vs `16.667` (`+2.667`), dips<90 `6.333` vs `2.667` (`+3.667`), crossings `132.000` vs `127.333` (`+4.667`, gate fail). |
| 2026-04-27 | C6 | A1 | Reject | Reverted | Operator-directed reject after smoothness-focused review; C6 render-timer cadence/coalescing patch removed and `src/qt/qt_openglrenderer.cpp` restored to pre-C6 behavior. | Early signal run `c6-a1-r1` stayed near-100 on average but worsened low-tail smoothness vs lock/C3 (notably `<90` events), so slice is non-promotable. |
| 2026-04-27 | C7 | A1 | Keep | Gate-failed | Qt/UI minimal-risk patch in `src/qt/qt_mainwindow.cpp`: made LED previous-state values persistent, reduced LED timer interval to 100 ms, and routed runtime-sensitive `processEvents()` callsites through pause/modal/settings-gated helper (`ExcludeUserInputEvents` + `ExcludeSocketNotifiers`). | 3-run gate set: `c7-a1-r1`, `c7-a1-r2`, `c7-a1-r3`; markers valid in all runs (`start_seen=1`, `max_seq=3`, `valid_for_q3_3dmark_wl05=1`) and WL-05 hashes locked (`quick=45db7b65`, `normal=2520dd5e`, `smc=b86f22a1`, `status=OK`) in per-run `wl05-hashes.txt`. Aggregate vs active lock (`c3-baseline-reconfirm`): avg `99.546` vs `99.567` (`-0.021`), p99 `102.333` vs `102.333` (`+0.000`), dips<95 `15.333` vs `19.333` (`-4.000`), dips<90 `8.667` vs `6.667` (`+2.000`), crossings `97.667` vs `97.333` (`+0.334`, gate fail). Smoothness read: mixed (`<95` and `>103` improved, but `<90`, `99..101` occupancy, and jump severity regressed). Detailed report: `docs/perf-artifacts/arm64-dynarec/c7-a1-2026-04-27-3run.md`. |
| 2026-04-27 | C7 | A2 | Keep | Landed | Focused rethink pass kept low-risk LED persistence/100 ms timer changes and trimmed runtime-adjacent `processEventsOnlyWhenPausedOrModal()` callsites in `src/qt/qt_mainwindow.cpp` (`closeEvent`, `showEvent`, monitor resize/renderer switch path). Guarded helper remains only for language-change UI path. | A2-consistent 3-run set: `c7-a1-retest-r2`, `c7-a1-retest-r3`, `c7-a1-retest-r4`; markers valid in all runs and WL-05 hashes locked (`quick=45db7b65`, `normal=2520dd5e`, `smc=b86f22a1`, `status=OK`). Aggregate vs active lock: avg `99.335` vs `99.567` (`-0.232`), p99 `102.000` vs `102.333` (`-0.333`), dips<95 `18.667` vs `19.333` (`-0.666`), dips<90 `3.333` vs `6.667` (`-3.334`), crossings `42.333` vs `97.333` (`-55.000`). Signal mixed but operator-directed keep; detailed report: `docs/perf-artifacts/arm64-dynarec/c7-a2-2026-04-27-3run.md`. |
| 2026-04-27 | C8 | A1 | Keep | Landed | Telemetry-sampling correctness landed in `src/86box.c`: speed percent now uses measured elapsed sample duration from `pc_onesec()` (`sample_ms`) instead of assuming exact 1-second callbacks; runtime behavior unchanged. | Validation run `c8-a1-r1`: markers valid (`start_seen=1`, `max_seq=3`, `valid_for_q3_3dmark_wl05=1`), WL-05 hashes operator-confirmed OK, sample timing stable (`sample_ms` avg `999.998`, min `997`, max `1002`). Same-run math comparison (`new` vs synthetic `old`): avg `+0.330`, p99 `+1`, dips<95 `-1`, dips<90 `+0`, crossings `+13`, consistent with metric-definition change (not runtime tuning). Operator scope decision: no C8 re-lock on this branch. |

## Delta Reference (266 MHz)

Reference basis:

- pre-qt-pacing baseline lock: `baseline-lock-2026-04-25-3run.md` (accepted runs at commit `c9b56425...`)
- C1 aggregate: `c1-phase1-a1-r1b`, `c1-phase1-a1-r2c`, `c1-phase1-a1-r3`
- C3 aggregate: `c3-a1-r1`, `c3-a1-r2`, `c3-a1-r3`
- C3 reconfirm aggregate (new active lock): `c3-baseline-reconfirm-r1`, `c3-baseline-reconfirm-r2`, `c3-baseline-reconfirm-r3`

Before/After Delta Summary:

- pre-qt-pacing -> C1
  - avg: `+0.09%`
  - p99: `-0.33%`
  - dips<100: `-1.21%`
  - dips<95: `-18.33%`
  - dips<90: `-66.67%`
- C1 -> C3
  - avg: `+0.01%`
  - p99: `-0.33%`
  - dips<100: `+0.00%`
  - dips<95: `-4.08%`
  - dips<90: `+0.00%`
- pre-qt-pacing -> C3
  - avg: `+0.10%`
  - p99: `-0.33%`
  - dips<100: `-1.21%`
  - dips<95: `-21.67%`
  - dips<90: `-66.67%`
- pre-qt-pacing -> C3 reconfirm lock (2026-04-27)
  - avg: `+0.00%`
  - p99: `+0.33%`
  - dips<100: `-0.81%`
  - dips<95: `-3.33%`
  - dips<90: `-16.67%`

Interpretation note:

- `C1 -> C3` is a consistency refinement, not a large throughput jump. Some metrics are intentionally flat because `C1` had already reduced them near floor (for example `dips<90` remained `2.667` and `dips<100` remained `81.667` on 3-run mean).
- The improvement signal for `C3` is in jitter/churn control: lower `p99`, fewer `dips<95`, and fewer crossings versus `C1`.
- Read flat low-tail deltas in `C1 -> C3` as "no regression at already-low levels," not as evidence that `C3` failed to improve consistency.
- The 2026-04-27 C3 reconfirm lock is the active baseline by operator decision; versus the superseded 2026-04-26 lock it shows lower crossing churn and no `>=105` overshoots in the accepted set, with weaker low-tail counts. Treat this as the authoritative reference point for this branch's completed C7 work.

## Track-Oriented Candidate Map

Consistency Track (default, near-term priority):

1. C1. Main Qt pacing loop uses millisecond debt, abrupt debt reset, and no deadline model
2. C3. Blit mutex contention injects explicit 1 ms sleeps and covers too much runtime work
3. C4. GPU FIFO wake/wait paths create bursty producer-consumer scheduling under heavy 3D load
4. C7. UI timers and `processEvents()` paths add recurring main-thread contention
5. C8. Speed sampling uses a nominal 1 second interval and can over-report dip/rebound events

Historical rejected slices (not in forward queue):

- C5 (A2 rejected, reverted)
- C6 (A1 rejected, reverted)

Throughput Track (deferred, larger refactor):

1. C2. `pc_run()` uses coarse 1 ms / 10 ms work packets while holding the blit mutex

## C1 (Consistency): Main Qt Pacing Loop Debt/Clamp

File/function/lines: `/Users/anthony/projects/code/86Box-voodoo-arm64/src/qt/qt_main.cpp`, `main_thread_fn`, lines 437, 450, 456, 458, 485, 486, 501.

Why this can cause wobble/smoothness loss: `drawits` accumulates elapsed milliseconds, runs one `pc_run()` per loop, then subtracts 1 or 10. Under heavy load, `pc_run()` can take longer than the quantum, so debt grows. When load eases, positive debt drives continuous work until drained, or the `drawits > 50` clamp abruptly erases backlog. That can produce dip, catch-up, rebound, and crossings.

Confidence: high

Change risk: medium

Machine-agnostic fix proposal: replace integer millisecond debt with a monotonic nanosecond deadline accumulator, clamp debt softly, and remove abrupt reset while preserving one `pc_run()` per scheduler pass in v1. Keep behavior deterministic by basing guest work on elapsed host time, not host-specific tuning. Treat bounded multi-run catch-up as a separate phase-2 experiment behind a flag.

Expected metric direction: center-band occupancy (`99-101`, `98-102`) up, low-tail (`<95`, `<90`) down, jump severity down; with legacy summaries trending `p99` down, `dips<95` down, `dips<90` down, `crossings` down, and `avg` neutral to slight up.

Patch Sketch:

```diff
# /Users/anthony/projects/code/86Box-voodoo-arm64/src/qt/qt_main.cpp:445
- uint64_t old_time = elapsed_timer.elapsed();
- int      drawits = frames = 0;
+ qint64 old_ns = elapsed_timer.nsecsElapsed();
+ qint64 debt_ns = 0;
+ const qint64 quantum_ns = force_10ms ? 10'000'000 : 1'000'000;
+ const qint64 max_debt_ns = 50'000'000;

# /Users/anthony/projects/code/86Box-voodoo-arm64/src/qt/qt_main.cpp:450
- const uint64_t new_time = elapsed_timer.elapsed();
- drawits += static_cast<int>(new_time - old_time);
- old_time = new_time;
- if ((drawits > 0 || fast_forward) && !dopause) {
+ const qint64 new_ns = elapsed_timer.nsecsElapsed();
+ debt_ns += new_ns - old_ns;
+ old_ns = new_ns;
+ debt_ns = std::min(debt_ns, max_debt_ns);
+ if (((debt_ns >= quantum_ns) || fast_forward) && !dopause) {

# /Users/anthony/projects/code/86Box-voodoo-arm64/src/qt/qt_main.cpp:485
- drawits -= force_10ms ? 10 : 1;
- if (drawits > 50 || fast_forward)
-     drawits = 0;
+ debt_ns -= quantum_ns;
+ if (fast_forward)
+     debt_ns = 0;

# /Users/anthony/projects/code/86Box-voodoo-arm64/src/qt/qt_main.cpp:488
- } else {
+ }
+ if ((debt_ns < quantum_ns) && !fast_forward) {
```

Phase sequencing:

1. Phase 1 (default): debt accounting/smoothing only, with single-run-per-pass semantics preserved.
2. Phase 2 (optional experiment): bounded catch-up (for example max 2 runs/pass) behind compile/runtime guard.

Follow-on revisit (deferred, non-blocking):

- Revisit C1 debt cap tuning after current consistency-track slices:
  - larger soft cap variant (for example 75-100 ms)
  - no-cap variant (for controlled comparability test only)
- Keep any such variant out of default path until it passes the same locked workload gate and correctness checks.

## C2 (Throughput, Deferred): Coarse `pc_run()` Work Packets

File/function/lines: `/Users/anthony/projects/code/86Box-voodoo-arm64/src/86box.c`, `pc_run`, lines 2094, 2110, 2111, 2122.

Why this is in the throughput track: each call executes `cpu_s->rspeed / 1000` cycles, or `/100` when `force_10ms` is enabled. That makes runtime work arrive in coarse packets and can limit scalability at higher guest clocks. Splitting this path may improve throughput, but it also changes timing cadence and can reduce consistency if done prematurely.

Confidence: medium for consistency gains, high for throughput impact

Change risk: high

Machine-agnostic fix proposal: if/when pursued, split emulation into smaller internal quanta while preserving total requested cycles per host quantum, but gate behind a compile/runtime flag and validate timing-sensitive correctness before defaulting on. This is deferred until consistency-first pacing/render fixes are landed and measured.

Expected metric direction: center-band/tail/jump behavior mixed/uncertain until validated; throughput may rise but consistency signals (`p99`, tails, crossings) are not assumed improved.

Validation requirement before default-on: pass a guest opcode stress harness with stable baseline hash/signature, using a methodology similar to existing guest-tool validation flows (for example `3DNOWCOV`).

Patch Sketch:

```diff
# /Users/anthony/projects/code/86Box-voodoo-arm64/src/86box.c:2110
- startblit();
- cpu_exec((int32_t) cpu_s->rspeed / (force_10ms ? 100 : 1000));
+ int32_t total_cycles = (int32_t) cpu_s->rspeed / (force_10ms ? 100 : 1000);
+ int32_t slice_cycles = (int32_t) cpu_s->rspeed / 4000; /* 0.25 ms guest chunks */
+ if (slice_cycles <= 0)
+     slice_cycles = total_cycles;
+ while (total_cycles > 0) {
+     int32_t run_cycles = MIN(total_cycles, slice_cycles);
+     cpu_exec(run_cycles);
+     total_cycles -= run_cycles;
+     if (dopause || is_quit || hard_reset_pending)
+         break;
+ }
```

## C3 (Consistency): Blit Mutex Contention Sleep

File/function/lines: `/Users/anthony/projects/code/86Box-voodoo-arm64/src/qt/qt_platform.cpp`, `startblit`, lines 764, 766, 771; `endblit`, lines 775, 777, 779, 783. Related caller: `/Users/anthony/projects/code/86Box-voodoo-arm64/src/86box.c`, `pc_run`, lines 2110 and 2122.

Why this can cause wobble/smoothness loss: `endblit()` sleeps for 1 ms whenever contention is detected. Under UI/render contention, that inserts artificial stalls into the emulation cadence. Also, `pc_run()` holds the blit mutex across CPU execution, mouse, joystick, and timer work, so UI operations can block or be blocked by unrelated runtime work.

Confidence: high

Change risk: medium

Machine-agnostic fix proposal: narrow the mutex scope to shared video state only, remove fixed sleep, and use a fair handoff or queued renderer synchronization. Avoid sleeping as a contention policy.

Expected metric direction: center-band occupancy up, low-tail and jump severity down; legacy summaries should trend `p99` down, `dips<95` down, `dips<90` down, `crossings` down, with `avg` slight up.

Patch Sketch:

```diff
# /Users/anthony/projects/code/86Box-voodoo-arm64/src/86box.c:2110
- startblit();
  cpu_exec(...);
  ack_pause();
  ...
  joystick_process(0);
- endblit();

# Later, only around operations that actually touch shared blit/video state:
+ startblit();
+ mouse_process();
+ joystick_process(0);
+ endblit();

# /Users/anthony/projects/code/86Box-voodoo-arm64/src/qt/qt_platform.cpp:779
- if (blitmx_contention > 0) {
-     std::this_thread::sleep_for(std::chrono::milliseconds(1));
- }
+ if (blitmx_contention > 0) {
+     std::this_thread::yield();
+ }
```

Lock-scope audit checklist before landing:

1. Enumerate every operation currently inside `startblit()`/`endblit()` and classify as:
   - must hold blit mutex (shared video state)
   - safe outside mutex
2. Verify `mouse_process()` / `joystick_process()` call paths do not rely on implicit serialization from blit mutex.
3. Verify monitor screenshot / blit-complete paths remain race-free under renderer switching.
4. Re-run renderer-toggle and shutdown paths that previously motivated contention sleep (`video_toggle_option` path).

## C4 (Consistency): GPU FIFO Burst Wake/Wait

File/function/lines: `/Users/anthony/projects/code/86Box-voodoo-arm64/src/video/vid_voodoo_fifo.c`, `voodoo_queue_command`, lines 183, 190, 201, 203, 243, 244. Related: `/Users/anthony/projects/code/86Box-voodoo-arm64/src/video/vid_voodoo_fifo.c`, `voodoo_fifo_thread`, lines 381, 386, 388, 391, 1065. Similar patterns: `/Users/anthony/projects/code/86Box-voodoo-arm64/src/video/vid_s3.c`, `s3_wait_fifo_idle`, lines 516, 520, `s3_queue`, line 541, and `fifo_thread`, lines 11184 and 11189.

Why this can cause wobble/smoothness loss: the producer wakes the FIFO thread only at high occupancy, then the consumer drains the FIFO in one burst. When full, producer paths use 1 ms timed waits. This can alternate between CPU-side stalls and large GPU-side work bursts, especially under Voodoo/S3 heavy load.

Confidence: medium-high

Change risk: medium

Machine-agnostic fix proposal: wake at lower watermarks, use hysteresis, and bound FIFO drain batches by elapsed emulated or host-neutral work budget. Replace repeated 1 ms timeout polling with event-driven backpressure where possible.

Expected metric direction: center-band occupancy up, low-tail and jump severity down; legacy summaries should trend `p99` down, `dips<95` down, `dips<90` down, `crossings` down, with `avg` neutral to slight up.

Patch Sketch:

```diff
# /Users/anthony/projects/code/86Box-voodoo-arm64/src/video/vid_voodoo_fifo.c:243
- if (FIFO_ENTRIES > 0xe000)
+ if (FIFO_ENTRIES > FIFO_WAKE_HIGH_WATERMARK)
      voodoo_wake_fifo_thread(voodoo);

# /Users/anthony/projects/code/86Box-voodoo-arm64/src/video/vid_voodoo_fifo.c:391
- while (!FIFO_EMPTY) {
+ int batch = 0;
+ while (!FIFO_EMPTY && batch++ < FIFO_MAX_BATCH) {
      ...
  }
+ if (!FIFO_EMPTY)
+     voodoo_wake_fifo_thread(voodoo);

# /Users/anthony/projects/code/86Box-voodoo-arm64/src/video/vid_voodoo_fifo.c:201
- thread_wait_event(voodoo->fifo_not_full_event, 1);
+ thread_wait_event(voodoo->fifo_not_full_event, FIFO_WAIT_SHORT);
```

## C5 (Consistency, Historical Rejected): Renderer Buffer Handoff Drops Frames

File/function/lines: `/Users/anthony/projects/code/86Box-voodoo-arm64/src/qt/qt_rendererstack.cpp`, `RendererStack::blit`, lines 507, 509, 510, 526, 528, 529. Related: `/Users/anthony/projects/code/86Box-voodoo-arm64/src/qt/qt_softwarerenderer.cpp`, `SoftwareRenderer::onBlit`, lines 82, 90, 91, 98. Related: `/Users/anthony/projects/code/86Box-voodoo-arm64/src/qt/qt_openglrenderer.cpp`, `OpenGLRenderer::onBlit`, lines 1155, 1176, 1189.

Status note: attempted through `C5 A2`, then rejected and reverted.

Why this can cause wobble/smoothness loss: if the current render buffer is still marked busy, the blit path immediately completes and returns without handing a frame to Qt. That protects emulation throughput but creates uneven visual cadence. Under load, frame drops can cluster, then rebound when the Qt side catches up.

Confidence: medium-high

Change risk: medium

Machine-agnostic fix proposal: use a latest-frame mailbox or bounded wait. Coalesce frames explicitly instead of opportunistically dropping based on whichever buffer index is current.

Expected metric direction: center-band occupancy slight up and rendered-cadence jump severity down, with low-tail slight down; legacy summaries expected near-neutral `avg`, lower visual `p99`, and lower crossings.

Patch Sketch:

```diff
# /Users/anthony/projects/code/86Box-voodoo-arm64/src/qt/qt_rendererstack.cpp:509
- if (... || std::get<std::atomic_flag *>(imagebufs[currentBuf])->test_and_set()) {
-     video_blit_complete_monitor(m_monitor_index);
-     return;
- }
+ int selected = find_free_or_latest_buffer();
+ if (selected < 0) {
+     mark_pending_latest_blit(x, y, w, h);
+     video_blit_complete_monitor(m_monitor_index);
+     return;
+ }
+ currentBuf = selected;
+ std::get<std::atomic_flag *>(imagebufs[currentBuf])->test_and_set();

# /Users/anthony/projects/code/86Box-voodoo-arm64/src/qt/qt_rendererstack.cpp:528
  emit blitToRenderer(currentBuf, sx, sy, sw, sh);
+ drain_pending_latest_blit_if_any();
```

## C6 (Consistency, Historical Rejected): OpenGL Render Timer Cadence

File/function/lines: `/Users/anthony/projects/code/86Box-voodoo-arm64/src/qt/qt_openglrenderer.cpp`, `OpenGLRenderer::OpenGLRenderer`, lines 819, 821, 823. `/Users/anthony/projects/code/86Box-voodoo-arm64/src/qt/qt_openglrenderer.cpp`, `OpenGLRenderer::initialize`, lines 858, 842, 906, 908. `/Users/anthony/projects/code/86Box-voodoo-arm64/src/qt/qt_openglrenderer.cpp`, `OpenGLRenderer::onBlit`, lines 1155, 1189, 1190.

Status note: attempted as `C6 A1`, then rejected and reverted after smoothness-focused review.

Why this can cause wobble/smoothness loss: render timing is either a default Qt timer with `ceil(1000 / video_framerate)` or immediate render on each blit. The timer does not set `Qt::PreciseTimer`, and when vsync is enabled the render timer can fight swap pacing. Immediate rendering can also make queued blit processing expensive.

Confidence: medium

Change risk: low-medium

Machine-agnostic fix proposal: set precise timer type, use deadline-based frame scheduling, and coalesce render requests. When vsync is enabled, avoid an independent coarse frame timer unless explicitly needed.

Expected metric direction (hypothesis, not accepted): center-band occupancy slight up and jump severity slight down, with low-tail slight down; legacy summaries expected near-neutral `avg`, lower visual `p99`, and lower crossings.

Patch Sketch:

```diff
# /Users/anthony/projects/code/86Box-voodoo-arm64/src/qt/qt_openglrenderer.cpp:821
  , renderTimer(new QTimer(this))
  {
+     renderTimer->setTimerType(Qt::PreciseTimer);
      connect(renderTimer, &QTimer::timeout, this, [this]() { this->render(); });

# /Users/anthony/projects/code/86Box-voodoo-arm64/src/qt/qt_openglrenderer.cpp:906
- if (video_framerate != -1) {
-     renderTimer->start(ceilf(1000.f / (float) video_framerate));
- }
+ if (video_framerate != -1 && !video_vsync) {
+     scheduleNextRenderDeadline();
+ }

# /Users/anthony/projects/code/86Box-voodoo-arm64/src/qt/qt_openglrenderer.cpp:1189
- if (video_framerate == -1)
-     render();
+ requestRenderCoalesced();
```

## C7 (Consistency): UI Timer Contention

File/function/lines: `/Users/anthony/projects/code/86Box-voodoo-arm64/src/qt/qt_mainwindow.cpp`, frame-rate timer, lines 198, 199, 202, 207, 210. LED keyboard timer, lines 241, 242, 243, 244, 245. Other contention paths: lines 934, 936, 941, 1150, 1799, 2329, 2331.

Why this can cause wobble/smoothness loss: the Qt main thread receives recurring UI timers while also processing queued blits. The LED timer runs every 20 ms and resets `prev_*` variables inside the lambda, so it likely updates icons every tick instead of only on change. Manual `QApplication::processEvents()` can also inject reentrant UI work into sensitive paths.

Confidence: medium

Change risk: low

Machine-agnostic fix proposal: make the LED previous-state variables persistent, reduce timer churn, and avoid `processEvents()` in runtime-visible paths unless guarded by pause/settings state.

Expected metric direction: center-band occupancy slight up, jump severity slight down, and low-tail neutral to slight down; legacy summaries should show near-neutral `avg`, slightly lower `p99`, and slightly lower crossings.

Patch Sketch:

```diff
# /Users/anthony/projects/code/86Box-voodoo-arm64/src/qt/qt_mainwindow.cpp:244
  connect(ledKeyboardTimer, &QTimer::timeout, this, [this]() {
-     uint8_t prev_caps = 255, prev_num = 255, prev_scroll = 255, prev_kana = 255;
+     static uint8_t prev_caps = 255, prev_num = 255, prev_scroll = 255, prev_kana = 255;
      uint8_t caps, num, scroll, kana;
      keyboard_get_states(&caps, &num, &scroll, &kana);
      ...
+     prev_caps = caps;
+     prev_num = num;
+     prev_scroll = scroll;
+     prev_kana = kana;
  });

# /Users/anthony/projects/code/86Box-voodoo-arm64/src/qt/qt_mainwindow.cpp:934
- QApplication::processEvents();
+ processEventsOnlyWhenPausedOrModal();
```

## C8 (Consistency Telemetry): Speed Sampling Uses Nominal 1 Second

File/function/lines: `/Users/anthony/projects/code/86Box-voodoo-arm64/src/qt/qt_main.cpp`, one-second timer, lines 869, 870, 873, 874. `/Users/anthony/projects/code/86Box-voodoo-arm64/src/86box.c`, `pc_onesec`, lines 2170, 2172, 2173, 2175. `/Users/anthony/projects/code/86Box-voodoo-arm64/src/86box.c`, title update, lines 2131, 2135, 2162.

Why this can cause wobble/smoothness loss in reported metrics: `pc_onesec()` assumes each timer callback represents exactly one second. Even with `onesec` set to `Qt::PreciseTimer`, callback dispatch can still be delayed when the Qt main thread is busy. In those cases `fps` is sampled over a longer interval but reported as a fixed one-second percentage, creating apparent dip/rebound noise even when true cadence is steadier.

Confidence: medium

Change risk: low

Machine-agnostic fix proposal: measure actual elapsed time between samples with a monotonic clock and compute speed percentage from real sample duration.

Expected metric direction: center-band/tail/jump metrics become more faithful (fewer false positives), with runtime itself unchanged; legacy summaries should read cleaner (`dips`/crossings less noisy) while true runtime behavior remains comparable.

Patch Sketch:

```diff
# /Users/anthony/projects/code/86Box-voodoo-arm64/src/86box.c:2170
  void pc_onesec(void)
  {
+     static uint32_t last_ms;
+     uint32_t now_ms = plat_get_ticks();
+     uint32_t elapsed_ms = last_ms ? now_ms - last_ms : 1000;
+     last_ms = now_ms;
      fps = framecount;
      framecount = 0;
+     fps_sample_elapsed_ms = elapsed_ms;
      title_update = 1;
  }

# /Users/anthony/projects/code/86Box-voodoo-arm64/src/86box.c:2135
- speed_percent = fps / (force_10ms ? 1 : 10);
+ speed_percent = compute_speed_percent(fps, fps_sample_elapsed_ms, force_10ms);
```

## Consistency-First Rollout Plan

Rollback handling rule: a rollback is not by itself a rejection. If rollback criteria trigger and there is no hard correctness/safety blocker, revert and clean up the attempt, log it as `Gate-failed` or `Reverted`, update the hypothesis, and execute one revised attempt before marking the candidate `Rejected`. Do not use subjective "felt worse" assessments alone; attach either primary-gate deltas or a reproducible regression symptom.

1. C1 phase 1 only: high-resolution debt accounting while preserving single-run-per-pass semantics.

Rollback criteria: revert if guest time visibly runs slow or `dips<95`/`dips<90` worsen in the same workload.

2. C3: narrow/remove blit-mutex contention sleep and audit lock scope.

Rollback criteria: revert if renderer races, blit corruption, or renderer-switch instability appears.

3. C4: smooth FIFO wake/wait and bounded drain behavior.

Rollback criteria: revert if Voodoo/S3 correctness regresses, deadlocks appear, or `p99`/crossings do not improve.

4. C7: reduce UI timer churn and avoid reentrant `processEvents()` in runtime-sensitive paths.

Rollback criteria: revert if UI responsiveness or shutdown/settings flows regress.

5. C8: correct telemetry sampling interval so dip metrics reflect real behavior.

Promotion rule: treat C8 as a measurement-system change, not as a direct gate-improvement slice. Do not claim pass/fail improvement against the pre-C8 lock from post-C8 metrics alone. After C8 lands, immediately run the re-lock procedure and establish a new active 3-run baseline before evaluating subsequent consistency slices against aggregate speed thresholds.

Rollback criteria: revert if the updated sampler cannot produce stable, internally consistent summaries on the locked workload or if the follow-on re-lock cannot be completed cleanly.

## Promotion Gate To Throughput Track

Proceed to throughput-oriented refactors only after the consistency track passes the current active 3-run lock (original lock before any metric-definition change; re-locked baseline after C8 or any other measurement change) and shows net improvement in smoothness-first metrics (center-band occupancy, low-tail rates, overshoot/jump severity), while legacy guardrails (`p99`, `dips<95`, `dips<90`, `crossings`) and correctness remain clean.

## Correctness Gate (Guest Opcode Stress)

Goal: prevent timing/behavior regressions from being masked by average-speed improvements.

1. Keep a dedicated guest opcode stress tool for throughput-track changes, following the existing guest validation pattern (`START`/per-op `PASS|FAIL`/`DONE` markers plus deterministic hash/checksum output).
2. Cover semantic behavior, not just raw encodings: include touched opcode families, operand forms (reg-reg/reg-mem/mem-reg where applicable), prefix/mode variants, and flag/exception-sensitive paths.
3. Record and version a known-good baseline hash/signature for the selected guest CPU profile(s).
4. Promotion criterion for throughput-track patches: defined workload passes and opcode harness baseline matches (or approved/understood deltas only).
5. Keep throughput-track features behind a guard until both workload and opcode-gate criteria are satisfied.

Scope baseline for C2-style work:

- Keep current locked gates (`Q3 demo four`, `3DMark99`, `WL-05/MRUNALL`) mandatory.
- Keep existing deterministic guest correctness suites (for example `3DNOWCOV`) mandatory where relevant.
- Add/maintain opcode stress coverage for instruction families touched by the change (integer ALU, branches/control transfer, memory addressing/forms, flags/exception behavior, FPU/MMX/3DNow when touched).
- Treat throughput patches as non-promotable until opcode stress signatures are stable on chosen CPU profile(s).

## Deferred Throughput Track

C2 and related deeper CPU/device pipeline work target higher-clock guest feasibility and may improve average throughput, but should remain behind a guard until timing-sensitive correctness and consistency impact are validated.

## Optional Secondary Workloads (Non-Blocking For Primary Gate)

- 3DMark2000 full run: recommended as secondary cross-check because it is controllable and repeatable in this environment.
- UT99 timedemo loop: useful exploratory signal, but not primary-gate material until a deterministic stop/exit harness is in place.

## Re-Lock Procedure (When Thresholds/Workload Need To Change)

Only re-lock when changes are intentional and documented (for example workload-definition updates, gate-math retuning, or telemetry/measurement changes that alter reported gate metrics).

Required steps:

1. Capture exactly 3 clean valid runs using the updated intended gate definition.
2. Preserve marker validity requirements and correctness/hash checks in each run.
3. Publish a new lock artifact (same style as baseline lock docs) with:
   - accepted run IDs
   - rejected/replaced run IDs and reasons
   - aggregate metrics and thresholds
4. Include a concise old-vs-new comparison and rationale in the lock artifact.
5. Update this document’s gate section and baseline reference in the same change.
