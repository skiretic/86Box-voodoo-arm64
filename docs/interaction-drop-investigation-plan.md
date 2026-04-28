# Windows 98 Guest UI Interaction Speed-Drop Investigation Plan

## Problem Statement

86Box shows sharp emulation-speed drops while interacting with a Windows 98 guest UI, especially during mouse and keyboard activity inside the guest. The issue is not a new branch-only regression: upstream/current 86Box behavior is reported to show the same class of slowdown, and the problem should be treated as a long-standing architectural or workload-interaction issue rather than as evidence of a recent branch delta.

This plan is for a later execution pass. It documents the current-code paths to inspect, ranked hypotheses, measurements to collect, and decision criteria. No VM/performance runs were performed for this planning pass.

## Known Facts And Constraints

- The slowdown happens during Windows 98 guest UI interaction.
- The issue is reported to exist in upstream/current 86Box as well as this working branch. Do not assume upstream is a known-good control.
- The issue has existed for a long time. Treat current architecture and workload behavior as the primary investigation target.
- Telemetry mode is not required to reproduce the issue. Telemetry and log parsing are not primary cause candidates.
- C8 changed sampling/reporting math only. Treat C8 as a reporting artifact control unless direct code evidence later contradicts this.
- Runtime-cost logging may still matter if logs are always active in the tested build, especially non-release builds where `pclog_ex()` flushes every log write.
- CPU emulation, dynarec, timer/PIC behavior, renderer presentation, blit synchronization, UI/input event handling, and host interaction side effects are all in scope.
- Future experiments must distinguish true emulation slowdown from delayed/biased speed reporting.
- This session performed code and history orientation only. It did not edit runtime code and did not run VM/performance tests.

## Orientation

- Working branch during this planning pass: `ndr-pacing-lab`.
- Remote orientation:
  - `origin`: `https://github.com/skiretic/86Box-voodoo-arm64.git`
  - `upstream`: `https://github.com/86Box/86Box.git`
- Current branch deltas are useful as measurement variants, but not as the primary explanatory frame because upstream/current behavior is also reported affected.
- The correct control model for later execution is:
  - compare multiple current builds/configurations for sensitivity;
  - compare current branch and upstream/current to find shared vs branch-specific amplification;
  - avoid treating any upstream build as known-good unless it is empirically shown to be good under the same workload.

## Code-Path Map

### High-Level Execution Loop

- `src/qt/qt_main.cpp:main_thread_fn()`
  - Runs the emulation loop on a high-priority Qt thread.
  - Tracks host-time debt using `elapsed_timer.nsecsElapsed()`.
  - Calls `pc_run()` when debt exceeds the 1 ms or 10 ms quantum.
  - Sleeps via `plat_delay_ms(1)` when no work is due.
  - In this branch, only one `pc_run()` is executed per scheduler pass. That can be a branch-specific pacing variant to measure, but not the assumed root cause of a long-standing upstream issue.

- `src/86box.c:pc_run()`
  - Updates CPU-independent timers with `rivatimer_update_all()`.
  - Calls `startblit()` before `cpu_exec()`.
  - Executes guest CPU for one quantum.
  - Processes untimed mouse input after CPU execution when `mouse_timed` is false.
  - Processes joystick input.
  - Calls `endblit()`.
  - Updates frame counters and, when `title_update` is set, computes and logs the title speed sample.

- `src/86box.c:pc_onesec()`
  - Runs from a Qt `QTimer` configured in `src/qt/qt_main.cpp`.
  - Samples `framecount` and stores elapsed host milliseconds for reporting.
  - Delayed Qt timer delivery can produce reporting artifacts, so future validation must collect independent timing around `pc_run()` and render/input paths.

### CPU Emulation, Dynarec, Timers, And Interrupts

- `src/cpu/386_dynarec.c:exec386_dynarec()`
  - Splits the requested CPU work into small periods using `cyc_period = cycs / (force_10ms ? 2000 : 200)`, approximately 5 microseconds of emulated time per inner period.
  - Chooses interpreter or dynarec via `cpu_force_interpreter`, `cpu_override_dynarec`, and cache status.
  - Checks NMI, SMI, and PIC pending interrupts.
  - Calls `picinterrupt()` when interrupts are enabled and `pic.int_pending` is set.
  - Advances `tsc` and calls `timer_process()` when `timer_target` has expired.

- `src/cpu/386_dynarec.c:exec386_dynarec_dyn()`
  - Looks up and validates dynarec code blocks.
  - Handles dirty-mask conflicts and codegen flush checks.
  - Periodically logs dynarec summary data in this branch. That logging is likely too sparse to explain interaction-only slowdown, but it should be checked in non-release/perf builds.

- `src/cpu/386.c:exec386_2386()`
  - Interpreter path also handles PIC interrupts and calls `timer_process()` when the timer target expires.
  - CPU-mode A/B must include interpreter vs dynarec to see whether the drop is dynarec-specific or timer/interrupt/render-coupling common.

- `src/timer.c:timer_process()`
  - Processes expired emulated timers in a loop until the head timer is in the future.
  - Calls timer callbacks directly on the CPU execution thread.
  - Input, KBC, PIT, SVGA, Voodoo, and other device timers can therefore add work inside CPU execution.

- `src/timer.c:timer_on_auto()` and `src/timer.c:timer_add()`
  - Timers can be rearmed from callbacks, including high-frequency callbacks such as mouse and KBC polling.

- `src/pic.c:picint_common()`
  - Raises/lowers PIC interrupt request lines and calls `update_pending()`.
  - Keyboard IRQ 1 and mouse IRQ 12 are central to the interaction workload.

- `src/pic.c:picinterrupt()`
  - Acknowledges pending interrupts and returns vectors to the CPU core.
  - Interrupt burst rate during mouse movement should be measured.

### Mouse Input And PS/2/KBC Path

- `src/qt/qt_winrawinputfilter.cpp:nativeEventFilter()`
  - On Windows raw input, updates button state, wheel deltas, and relative or absolute motion.
  - Calls `mouse_set_buttons_ex()`, `mouse_set_z()`, `mouse_set_w()`, and `mouse_scale()`.

- `src/qt/macos_event_filter.mm:CocoaEventFilter::nativeEventFilter()`
  - On macOS captured mouse input, consumes native mouse move/drag/scroll/button events and updates mouse state through `mouse_scalef()`, `mouse_set_z()`, `mouse_set_w()`, and `mouse_set_buttons_ex()`.

- `src/qt/qt_rendererstack.cpp:RendererStack::mousePressEvent()` and `RendererStack::mouseReleaseEvent()`
  - Update guest-visible button state and manage capture transitions.

- `src/qt/qt_rendererstack.cpp:RendererStack::mouseMoveEvent()`
  - For captured mouse on non-Windows/non-macOS paths, scales relative movement and warps cursor to the window center.
  - On Windows/macOS, native raw/cocoa filters appear to handle captured relative movement.

- `src/qt/qt_rendererstack.cpp:RendererStack::event()`
  - Normalizes absolute/tablet coordinates on every mouse/touch event.
  - Uses renderer destination rectangles, width/height, and clamping. This is host-side event-loop work before guest delivery.

- `src/device/mouse.c:mouse_timer_poll()`
  - Rearms the mouse timer at `1000000.0 / sample_rate` microseconds.
  - Calls `mouse_process()` when `mouse_timed` is true.

- `src/device/mouse.c:mouse_set_sample_rate()`
  - Enables/disables timed mouse polling based on device sample rate and `force_constant_mouse`.
  - PS/2 default is 100 Hz, but Windows drivers can command other sample rates.

- `src/device/mouse.c:mouse_process()`
  - Dispatches to absolute/tablet poll callback or the active mouse device poll callback.
  - For timed devices, this work happens through the emulated timer system on the CPU thread.

- `src/device/mouse_ps2.c:ps2_report_coordinates()`
  - Consumes accumulated host mouse deltas using `mouse_subtract_coords()`, wheel deltas, and button state.
  - Queues 3 or 4 PS/2 bytes per report through `kbc_at_dev_queue_add()`.
  - This is a high-probability interaction hot path: dense host movement can become repeated KBC queue writes and IRQ delivery.

- `src/device/mouse_ps2.c:ps2_write()`
  - Handles PS/2 commands including sample-rate changes (`0xf3`), stream/remote modes, enable/disable, and status requests.
  - Future instrumentation should record actual guest-selected sample rate and stream mode during Windows 98 interaction.

### Keyboard Input And KBC Path

- `src/qt/qt_mainwindow.cpp:processKeyboardInput()`
  - Converts host scan codes to guest scan codes.
  - Handles special cases such as Print Screen and Pause.
  - Calls `keyboard_input()`.

- `src/qt/qt_mainwindow.cpp:MainWindow::keyPressEvent()` and `MainWindow::keyReleaseEvent()`
  - Forward Qt key events into the keyboard input path when keyboard delivery is enabled.

- `src/qt/qt_mainwindow.cpp:MainWindow::eventFilter()`
  - Handles shortcuts and fullscreen UI cases.
  - Can consume key events depending on capture/fullscreen state.
  - Should be counted during future interaction runs to separate guest input from host shortcut/menu processing.

- `src/device/keyboard.c:keyboard_input()`
  - Normalizes extended scan codes, updates UI and guest key state arrays, and calls `key_process()` when capture rules permit.

- `src/device/keyboard.c:key_process()`
  - Applies scan-code translation, fake-shift handling, and break/make sequence generation.
  - Calls `keyboard_send()` for each byte.

- `src/device/keyboard_at.c:add_data_vals()`, `add_data_kbd_84()`, and `add_data_kbd()`
  - Queue keyboard data bytes through `kbc_at_dev_queue_add()`.

- `src/device/kbc_at_dev.c:kbc_at_dev_queue_add()`
  - Adds bytes to a main data FIFO or command-response FIFO.
  - There is no visible overflow/backpressure handling in this helper; future instrumentation should track occupancy and overwrites/wrap pressure.

- `src/device/kbc_at_dev.c:kbc_at_dev_poll()`
  - Moves device queue bytes to the KBC port's `out_new` field when the controller is ready.

- `src/device/kbc_at.c:kbc_at_poll()`
  - Runs every 100 microseconds of emulated time via `timer_advance_u64(&dev->kbc_poll_timer, 100ULL * TIMER_USEC)`.
  - Drives the controller state machine.

- `src/device/kbc_at.c:kbc_at_dev_poll()`
  - Also runs every 100 microseconds of emulated time and polls attached keyboard and auxiliary devices.

- `src/device/kbc_at.c:kbc_send_to_ob()`
  - Moves bytes to the output buffer, sets status bits, and triggers IRQ behavior.

- `src/device/kbc_at.c:kbc_do_irq()`
  - Pulses keyboard/mouse interrupts by calling `picint_common()` for IRQ 1 or IRQ 12.

### Renderer, Blit, Present, And Synchronization

- `src/qt/qt_platform.cpp:startblit()` and `endblit()`
  - Use a global recursive mutex (`blitmx`) and an atomic contention counter.
  - `pc_run()` holds this lock across `cpu_exec()`, mouse processing, joystick processing, and `endblit()`.
  - This is a key coupling point between CPU execution and rendering/blit paths. Any UI/render path that contends on this lock can directly affect emulation cadence.

- `src/video/video.c:video_blit_memtoscreen_monitor()`
  - Waits for previous blit completion with `video_wait_for_blit_monitor()`.
  - Marks the target buffer busy/in-use, records blit rectangle, increments rendered frame count, and wakes the blit thread.

- `src/video/video.c:video_wait_for_blit_monitor()`
  - Blocks while the blit thread is busy.

- `src/video/video.c:video_wait_for_buffer_monitor()`
  - Blocks while the renderer still owns the buffer.

- `src/video/video.c:blit_thread()`
  - Waits for blit work, calls the active platform blit function, clears `busy`, and signals completion.

- `src/qt/qt_rendererstack.cpp:RendererStack::blit()`
  - Runs on the blitter thread.
  - Copies the emulator target buffer into a renderer buffer.
  - Uses an atomic flag per renderer buffer. If buffers are busy, it completes the monitor blit early and drops/defers rendering work.
  - Emits `blitToRenderer` to the Qt renderer.

- `src/qt/qt_openglrenderer.cpp:OpenGLRenderer::onBlit()`
  - Runs on the renderer/Qt side.
  - Calls `context->makeCurrent()`.
  - Uploads texture data via `glTexSubImage2D()`.
  - Clears buffer usage, updates destination geometry, and renders immediately when `video_framerate == -1`.

- `src/qt/qt_openglrenderer.cpp:OpenGLRenderer::render()`
  - Performs GL rendering and calls `context->swapBuffers(this)`.
  - Swap/present blocking can backpressure queued blits and Qt event handling.

- `src/qt/qt_renderercommon.cpp:RendererCommon::eventDelegate()`
  - Forwards key events synchronously to `main_window` with `QApplication::sendEvent()`.
  - Forwards mouse/touch/wheel/enter/leave events synchronously to the parent widget.
  - Synchronous sendEvent forwarding should be measured under event flood.

- `src/qt/qt_mainwindow.cpp:MainWindow::blitToWidget()`
  - Routes primary and secondary monitor blits to the active renderer or completes blit immediately when invisible.

### SVGA/Voodoo Display Workloads

- `src/video/vid_svga.c:svga_poll()` and related display timer paths
  - Advance the SVGA display timer using `timer_advance_u64()` for on/off display time.
  - Render scanlines through `svga_do_render()`.
  - Call `video_wait_for_buffer_monitor()` when beginning a frame's visible output.
  - Call `video_blit_memtoscreen_monitor()` in `svga_doblit()`.

- `src/video/vid_svga.c:svga_doblit()`
  - Performs overscan/border work and submits the final blit through `video_blit_memtoscreen_monitor()`.
  - Windows 98 UI interaction may increase cursor invalidation, changed VRAM, and blit frequency/area.

- `src/video/vid_voodoo_display.c:voodoo_display_timer()`
  - Handles display line timing, swap-pending checks, dirty-line tracking, and calls `svga_doblit()` for VGA pass-through.
  - Uses `swap_mutex` and wakes FIFO threads on swap completion.

- `src/video/vid_voodoo_fifo.c:voodoo_wait_for_swap_complete()`
  - Waits while `swap_pending` is true.
  - Uses `plat_delay_ms(1)` on non-Windows hosts and `plat_delay_ms(0)` on Windows.
  - This can become relevant if the interaction workload uses Voodoo/3D paths or mixed VGA pass-through.

### Logging And Reporting Paths

- `src/86box.c:pclog_ex()`
  - In non-release builds, formats each message, writes to the log, and calls `fflush(stdlog)` on every log entry.
  - This is potentially expensive if any always-active logging fires during interaction.

- `src/86box.c:pc_run()`
  - Logs `EMU_SPEED_SAMPLE` once per title update in this branch.
  - This should be treated as a reporting path, not a primary runtime cause, unless logging volume or flush cost is shown to correlate with the slowdown.

- Many device logs are gated by `ENABLE_*_LOG` macros and runtime flags. Future runs must record build type and active logging macros.

## Ranked Hypotheses

### H1 - CPU/Render Coupling Through Global Blit Synchronization

Likelihood: High  
Effort: Medium  
Risk: Medium

Rationale:

- `pc_run()` holds the global blit mutex across CPU execution and post-CPU input processing.
- SVGA/Voodoo display timers can call blit/wait paths from inside `timer_process()`, which is itself called from the CPU core.
- The blit pipeline has blocking points: `video_wait_for_blit_monitor()`, `video_wait_for_buffer_monitor()`, renderer buffer flags, GL upload, and `swapBuffers()`.
- UI interaction can increase guest-side dirty regions and host event-loop activity at the same time, creating contention between CPU, blitter, renderer, and Qt event processing.

Evidence to seek:

- `pc_run()` wall time rises during interaction.
- Time inside `startblit()` wait, `cpu_exec()`, `video_wait_for_blit_monitor()`, `video_wait_for_buffer_monitor()`, `RendererStack::blit()`, `OpenGLRenderer::onBlit()`, or `swapBuffers()` rises during interaction.
- Renderer queue depth or dropped/busy renderer buffers increases during interaction.

Reject if:

- Interaction drops occur without increased blit/present wait time, without increased lock contention, and with unchanged render upload/swap timing.

### H2 - PS/2 Mouse/KBC/PIC Interrupt Burst Cost Under Mouse Movement

Likelihood: High  
Effort: Medium  
Risk: Low

Rationale:

- Host mouse movement accumulates deltas in atomics, then PS/2 polling converts deltas to 3 or 4 byte packets.
- KBC device/controller timers run every 100 microseconds of emulated time.
- Each packet byte can move through device FIFOs, KBC output buffer state, and IRQ 12/PIC paths.
- Windows 98 mouse interaction is likely to drive a continuous stream of PS/2 packets and guest interrupt handling.

Evidence to seek:

- Mouse packet generation rate, KBC FIFO occupancy, IRQ 12 rate, and guest interrupt time rise sharply during host mouse movement.
- Lowering guest mouse sample rate or switching mouse device/input mode reduces the speed drop.
- Keyboard-only interaction has a different, likely smaller signature than captured mouse movement.

Reject if:

- Mouse interaction produces minimal KBC/PIC activity or the drop persists unchanged when mouse packet generation is suppressed or substantially reduced.

### H3 - Guest UI Workload Causes Real CPU/Dynarec Slowdown Through Redraw, Driver, And Timer Pressure

Likelihood: High  
Effort: Medium to High  
Risk: Medium

Rationale:

- The Windows 98 UI can generate large redraw/cursor/driver activity during pointer movement even before considering host rendering costs.
- Dynarec execution checks interrupts, timers, dirty code masks, and block validity frequently.
- Interpreter and dynarec may respond differently to high interrupt density and video memory churn.
- A long-standing upstream issue is compatible with a genuine workload cost inside emulated CPU/device paths rather than branch-specific pacing.

Evidence to seek:

- `cpu_exec()` wall time per emulated quantum increases during interaction even when render wait time is separated out.
- PIC interrupt count, timer callback count, SVGA render line count, and dynarec invalidation/dirty-mask counters rise during interaction.
- Dynarec vs interpreter changes the magnitude or shape of the drop.

Reject if:

- CPU execution time and timer/interrupt counts remain flat while render/UI wait time alone explains the drop.

### H4 - Qt Event Loop And Synchronous Event Forwarding Starve Render Or Reporting

Likelihood: Medium-High  
Effort: Medium  
Risk: Low

Rationale:

- Renderer event delegation uses synchronous `QApplication::sendEvent()` for key and mouse events.
- Mouse move floods can make the Qt thread spend more time dispatching input and less time handling queued render/blit work and the one-second reporting timer.
- Delayed Qt `QTimer` delivery can make title speed reporting look worse or shifted relative to true CPU progress.

Evidence to seek:

- Qt input event rate spikes during interaction.
- Queued `blitToRenderer` delivery latency increases.
- `pc_onesec()` delivery jitter increases, but independent CPU progress counters do not drop as much as the title speed indicates.

Reject if:

- Qt event counts and queued signal latency stay flat, and independent timing matches title-reported slowdown.

### H5 - GL Upload/Present Backpressure During Interaction

Likelihood: Medium  
Effort: Medium  
Risk: Medium

Rationale:

- `OpenGLRenderer::onBlit()` calls `makeCurrent()`, texture upload, resize math, and sometimes immediate render.
- `OpenGLRenderer::render()` calls `swapBuffers()`.
- Host window focus/cursor/input activity can affect compositor scheduling and vsync behavior.
- If UI interaction increases blit rate or damage area, GL upload/present cost can rise.

Evidence to seek:

- `glTexSubImage2D()` or `swapBuffers()` wall time increases during interaction.
- Software renderer vs OpenGL renderer changes the drop magnitude.
- Windowed vs fullscreen or vsync/framerate settings change the drop magnitude.

Reject if:

- Renderer backend and present timing changes have no effect, and GL/present timings remain flat during interaction.

### H6 - Host Capture/Pointer Side Effects

Likelihood: Medium  
Effort: Low to Medium  
Risk: Low

Rationale:

- macOS captured mouse path uses `CGAssociateMouseAndMouseCursorPosition(false)` and native NSEvent filtering.
- Windows uses raw input paths.
- Non-Windows/non-macOS captured mouse paths may warp cursor position, creating additional events.
- Host event behavior may differ between captured vs uncaptured, raw vs Qt events, and relative vs absolute input.

Evidence to seek:

- Captured relative movement drops more than uncaptured hover or absolute/tablet input.
- Event rate or duplicate motion events differ dramatically by host/capture mode.

Reject if:

- Same slowdown occurs with no capture, no relative movement, and equivalent guest UI workload generated through keyboard-only or scripted guest actions.

### H7 - Always-On Logging Or Non-Release Flush Cost Amplifies Drops

Likelihood: Low to Medium  
Effort: Low  
Risk: Low

Rationale:

- `pclog_ex()` flushes on every log entry in non-release builds.
- Most device logs are compile-gated, but branch/perf builds may enable runtime logs or periodic summaries.
- Logging is not expected to be the root of an upstream-longstanding issue unless the tested build emits interaction-correlated logs.

Evidence to seek:

- Log write count or `fflush()` time rises during interaction.
- Release/no-log build materially reduces the drop while code-path counters remain similar.

Reject if:

- No interaction-correlated logging occurs, or disabling logging has no measurable impact.

### H8 - Reporting Artifact From `pc_onesec()`/Title Sampling

Likelihood: Medium as an artifact, Low as sole cause  
Effort: Low  
Risk: Low

Rationale:

- The speed percent is sampled by a Qt one-second timer and displayed/logged later from `pc_run()`.
- Qt event-loop load can delay the timer and skew title samples.
- User-visible slowdown may still be real, so reporting artifacts must be separated from runtime stalls rather than assumed.

Evidence to seek:

- Title speed dips occur without corresponding independent CPU progress, frame, or wall-time stalls.
- `pc_onesec()` jitter correlates with input event flood.

Reject if:

- Independent counters confirm reduced emulated progress per host second during the same intervals.

## Detailed Experiment Plan For Later Execution

### Experiment 1 - Reconfirm And Baseline With Independent Timing

Purpose: Establish a clean reference signature without relying only on title speed.

Steps:

1. Build or select a known configuration with debug symbols and minimal unrelated logging.
2. Run the same Windows 98 VM and the same desktop/UI scenario.
3. Record idle/no-input baseline for a fixed interval.
4. Record controlled interaction intervals: captured mouse move, mouse drag, keyboard-only navigation, and no-input again.
5. Collect independent counters around `pc_run()` iterations, host wall time, emulated time, framecount, KBC/PIC/timer counts, and render/blit times.
6. Compare title speed to independent emulation progress.

Expected outcomes:

- If the drop is real, independent emulated progress per host second falls during interaction.
- If mostly reporting artifact, title speed dips while independent progress is stable.

Disproof criteria:

- No reproducible delta between idle and interaction under controlled conditions.

### Experiment 2 - Input Modality Matrix

Purpose: Determine whether the trigger is mouse-specific, keyboard-specific, capture-specific, or general guest UI activity.

Matrix:

- No input, pointer outside VM window.
- Pointer over VM window, no capture, no guest delivery.
- Captured relative mouse movement.
- Mouse button press/release only.
- Click-and-drag movement.
- Wheel scrolling.
- Keyboard-only navigation.
- Absolute/tablet input if available.
- Different PS/2 sample rates if configurable or guest-selectable.
- Mouse disabled or alternative mouse device, where practical.

Metrics:

- Host input event count by type.
- Calls to `mouse_scale()`, `mouse_scalef()`, `mouse_set_buttons_ex()`, `mouse_set_z()`, `mouse_set_w()`.
- Calls to `mouse_process()` and `ps2_report_coordinates()`.
- PS/2 packet bytes queued.
- KBC queue occupancy and dropped/wrapped writes.
- IRQ 1 and IRQ 12 rates.
- `timer_process()` callback counts and wall time.

Confirming outcomes:

- Mouse-only spike supports H2/H6.
- Keyboard-only spike supports KBC/PIC or general guest workload, but less specifically mouse packet cost.
- Similar drops across all interaction types point toward Qt/render scheduling or guest redraw load.

### Experiment 3 - Blit/Renderer Synchronization Timing

Purpose: Measure whether render backpressure or global blit synchronization explains the drop.

Instrumentation points:

- `startblit()` wait time and contention count.
- `endblit()` handoff count.
- `video_wait_for_blit_monitor()` wait time.
- `video_wait_for_buffer_monitor()` wait time.
- `video_blit_memtoscreen_monitor()` call count, rectangle area, and monitor index.
- `RendererStack::blit()` copy time and busy-buffer early-complete count.
- `OpenGLRenderer::onBlit()` time, upload dimensions, and immediate-render count.
- `OpenGLRenderer::render()` and `swapBuffers()` time.
- Queued signal latency from `RendererStack::blit()` emit to renderer `onBlit()` handling.

A/B controls:

- OpenGL renderer vs software renderer, if practical.
- Windowed vs fullscreen.
- `video_framerate` capped vs uncapped, if available.
- Primary monitor only vs secondary monitor visible/hidden, if relevant.

Confirming outcomes:

- Interaction increases lock waits, blit waits, renderer buffer pressure, GL upload time, or swap time.

Rejecting outcomes:

- Blit/render metrics stay flat while CPU/timer/input metrics explain the drop.

### Experiment 4 - CPU/Dynarec/Timer Interaction

Purpose: Separate guest CPU work from host render/UI blocking.

A/B controls:

- Dynarec enabled vs interpreter/forced interpreter.
- Different emulated CPU speeds if practical.
- `force_10ms` mode vs default if the build supports it.
- Same renderer and input workload across CPU modes.

Metrics:

- `cpu_exec()` wall time per quantum.
- Instructions or cycles retired proxy, if available.
- `timer_process()` calls and total callback time.
- Timer callback breakdown: KBC, mouse, PIT, SVGA, Voodoo, other top callbacks.
- `picinterrupt()` calls and vectors, especially IRQ 1 and IRQ 12.
- Dynarec block lookup, compile, dirty-mask conflict, and flush counters.
- Guest-visible frame/blit rate.

Confirming outcomes:

- Dynarec-only change implicates dynarec/block/timer interaction.
- Both dynarec and interpreter showing similar drops implicates device/timer/render workload rather than dynarec-specific behavior.
- Reduced CPU speed changing the severity helps identify timer/interrupt density sensitivity.

### Experiment 5 - Guest Video Workload Sensitivity

Purpose: Determine whether Windows 98 UI redraw/cursor behavior drives SVGA/Voodoo work.

A/B controls:

- Different Windows 98 display settings: color depth, resolution, hardware acceleration slider if available.
- Different emulated video adapters, where practical.
- Voodoo present vs absent for non-3D desktop workload.
- Mouse trails/cursor shadow/theme effects if relevant.

Metrics:

- SVGA timer callback count and wall time.
- `svga_do_render()` line count/time.
- `svga_doblit()` count, dimensions, and area.
- Changed VRAM/dirty-line indicators where available.
- Voodoo swap wait/fifo wait counts if Voodoo is active.

Confirming outcomes:

- Display setting changes strongly affect the drop while input event counts remain similar.

Rejecting outcomes:

- Display workload metrics do not change during interaction, or video adapter/settings do not change severity.

### Experiment 6 - Logging And Build-Type Control

Purpose: Rule in/out runtime logging amplification.

Steps:

1. Record build type and relevant compile-time logging macros.
2. Measure log lines per second and flush count during idle and interaction.
3. Compare release/no-log build against non-release/perf build with the same workload.
4. If instrumentation logging is added, use buffered counters and flush only at interval boundaries.

Confirming outcomes:

- Interaction-correlated log volume and flush time explain a significant share of wall-time increase.

Rejecting outcomes:

- Log volume stays flat or no-log builds reproduce the same slowdown.

### Experiment 7 - Upstream/Branch Shared Signature Check

Purpose: Use upstream/current as an affected peer, not a known-good baseline.

Steps:

1. Run the same minimal instrumentation on upstream/current and this branch.
2. Compare signatures, not just aggregate speed.
3. Identify shared hot zones first.
4. Only after shared causes are understood, inspect branch-specific amplification.

Expected outcomes:

- Shared spike in the same metrics supports long-standing architecture issue.
- Different signatures suggest branch-specific amplification layered over an upstream issue.

## Metrics To Collect And Comparison Method

### Core Metrics

- Emulated progress per host second independent of title sampling.
- `pc_run()` count and wall time distribution.
- `cpu_exec()` wall time distribution.
- `timer_process()` count, callback count, and callback wall time.
- `picinterrupt()` count by vector/IRQ source.
- Host input event count by type.
- Mouse packet count, bytes queued, and PS/2 sample rate.
- KBC queue occupancy and output-buffer stalls.
- Blit submit count, blit rectangle area, wait time, and buffer wait time.
- Renderer queue latency, copy time, upload time, render time, and swap time.
- Qt event-loop timer jitter for `pc_onesec()`.
- Log line count and flush time in non-release builds.

### Comparison Method

- Use fixed-duration windows: pre-interaction idle, interaction, post-interaction recovery.
- Report median, p95, p99, and max wall-time for critical sections.
- Normalize counts per host second and per emulated second.
- Compare ratios: interaction/idle for each metric.
- Prefer direct counters over title speed for cause analysis.
- Treat title speed as a user-visible symptom and possible artifact, not the primary metric.

## Minimal Instrumentation Plan

Instrumentation should be targeted, reversible, and low overhead. Prefer per-thread counters and ring buffers over per-event synchronous logging.

Recommended counters:

- `pc_run()` start/end timestamps and over-budget count.
- `cpu_exec()` wall time.
- `startblit()` wait time and contention count.
- `video_wait_for_blit_monitor()` and `video_wait_for_buffer_monitor()` wait time.
- `video_blit_memtoscreen_monitor()` count and area.
- `RendererStack::blit()` copy time and busy-buffer returns.
- `OpenGLRenderer::onBlit()` upload time and size.
- `OpenGLRenderer::render()` and `swapBuffers()` time.
- `mouse_process()` and `ps2_report_coordinates()` counts.
- `kbc_at_dev_queue_add()` count/occupancy for keyboard and aux devices.
- `kbc_send_to_ob()` count by channel.
- `kbc_do_irq()` count by IRQ/channel.
- `picint_common()` and `picinterrupt()` count by IRQ/vector.
- `timer_process()` total time and callback identities for top callbacks.
- Qt input event counts and queued renderer signal latency.
- `pc_onesec()` delivery jitter.
- `pclog_ex()` count and total flush/write time in non-release builds.

Implementation guidance for later sessions:

- Use compile-time or environment-gated instrumentation.
- Do not log per event directly to disk during hot paths.
- Aggregate into counters and emit periodic summaries only.
- Add one instrumentation group at a time to avoid probe effect.
- Keep patches small and easy to revert.

## Host-Noise Control Protocol

Before each future run:

- Record host OS version, CPU model, power mode, display refresh rate, renderer backend, window/fullscreen state, and build type.
- Close high-CPU background applications.
- Disable or record screen recording, live preview, backup, indexing, and cloud-sync activity.
- Keep the VM window size, monitor, and display scaling fixed.
- Keep the same VM configuration and guest state snapshot.
- Warm up the VM before collecting data.
- Run idle, interaction, and recovery windows in the same order for each A/B variant.
- Repeat each variant enough times to detect run-to-run noise.
- Record host CPU utilization, thermal throttling/power pressure if available, and process thread samples if a run is anomalous.

Acceptance criteria for a run:

- Idle baseline is stable before interaction.
- No unrelated host process dominates CPU or GPU during the measured interval.
- The interaction script/manual pattern is consistent across variants.
- Instrumentation overhead remains small relative to the measured effect.

Rejection criteria for a run:

- Host thermal/power state changes materially mid-run.
- Background workload or logging storm appears unrelated to the VM.
- Interaction timing is inconsistent enough to invalidate A/B comparison.
- Instrumentation itself changes the symptom shape.

## Decision Criteria By Hypothesis

### H1 Confirm/Reject

Confirm if blit lock/wait/render queue metrics rise during interaction and explain a large share of lost host time.  
Reject if those metrics remain flat and CPU/input/timer counters explain the drop.

### H2 Confirm/Reject

Confirm if mouse movement causes PS/2 packet, KBC queue, IRQ 12, and timer callback rates to spike, and reducing mouse packet generation reduces slowdown.  
Reject if mouse activity counters are low or decoupling mouse delivery does not change the drop.

### H3 Confirm/Reject

Confirm if `cpu_exec()` wall time, guest interrupt density, timer callback cost, SVGA render work, or dynarec dirty/block counters rise during interaction independent of host render waits.  
Reject if CPU/timer work is flat and renderer/UI blocking dominates.

### H4 Confirm/Reject

Confirm if Qt event flood delays renderer signal handling or `pc_onesec()` delivery while independent CPU progress diverges from title speed.  
Reject if Qt event latency stays flat and independent metrics match the title dip.

### H5 Confirm/Reject

Confirm if GL upload/present costs rise during interaction or renderer backend/framerate changes alter the drop.  
Reject if renderer timing and backend changes have little effect.

### H6 Confirm/Reject

Confirm if capture/raw-input/native-pointer mode changes the drop more than guest workload changes do.  
Reject if equivalent guest workload causes the same slowdown across capture modes.

### H7 Confirm/Reject

Confirm if interaction-correlated log volume or flush time is significant and no-log/release builds reduce the drop.  
Reject if logs are quiet or no-log builds reproduce the issue.

### H8 Confirm/Reject

Confirm as artifact if title speed dips without independent emulated-progress loss and with `pc_onesec()` jitter.  
Reject as sole cause if independent emulated progress also drops.

## Risk And Effort Estimate

- H1 blit/render synchronization: Medium effort, high value, moderate probe-effect risk.
- H2 mouse/KBC/PIC path: Medium effort, high value, low risk.
- H3 CPU/dynarec/timer path: Medium to high effort, high value, moderate risk.
- H4 Qt event-loop/reporting: Medium effort, medium-high value, low risk.
- H5 GL/present backpressure: Medium effort, medium value, moderate platform variance.
- H6 host capture side effects: Low to medium effort, medium value, low risk.
- H7 logging overhead: Low effort, medium value as a control, low risk.
- H8 reporting artifact: Low effort, required control, low risk.

## Recommended Execution Order

1. Add minimal independent timing and counters for `pc_run()`, `cpu_exec()`, title timer jitter, and logging count. This separates true slowdown from reporting artifacts.
2. Add input/KBC/PIC counters for mouse packets, KBC occupancy, IRQ 1/12, and timer callbacks. This directly tests the most interaction-specific path.
3. Add blit/render synchronization timing for `startblit()`, video waits, renderer upload, and swap. This tests the main CPU/render coupling risk.
4. Run the input modality matrix with the same build and VM state.
5. Run renderer backend/framerate/windowing A/B tests.
6. Run CPU mode A/B tests: dynarec vs interpreter, plus CPU speed or timing mode sensitivity if practical.
7. Run video workload sensitivity tests inside Windows 98.
8. Compare upstream/current and this branch using the same minimal probes, treating both as affected unless proven otherwise.
9. Only after the shared signature is known, inspect branch-local changes as possible amplifiers.
10. Convert the strongest confirmed hypothesis into a small reversible mitigation experiment.

## Notes From This Planning Pass

The deepest current-code evidence points away from telemetry/log parsing as the central explanation and toward interaction between guest input delivery, high-frequency emulated KBC/timer/PIC work, CPU execution cadence, and renderer/blit synchronization. The first future runs should therefore collect independent runtime timing and event/counter signatures before attempting fixes.
