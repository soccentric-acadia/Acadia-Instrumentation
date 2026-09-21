# Task 005 (leaf) — Add `rt-test` test application — Acadia-Instrumentation

Parent task: `Acadia/tasks/005-add-rt-test-app/plan.md`
- **Task ID:** 005
- **Date:** 2026-09-21
- **Status:** planned — awaiting approval (revised: simple test app, not a daemon)
- **Class:** affected (new standalone tool)

## Scope in this repository

Add a standalone test application `rt-test` that one-shot runs a PREEMPT_RT
feature demonstration suite on RT and non-RT kernels, prints a
human-readable comparison, and stores a JSON result file. It is **not a
daemon** (parent decisions D1/D2): no proto, no gRPC service, no `_ctrl`
tool, no istate registration, **no systemd unit, no service user, no
sysusers/tmpfiles entries**. istate, transport, contracts, and all consumers
are out of scope.

### New files

```text
tools/rt-test/
  rttest.h      # core API: suite runner, stats, report types
  rttest.cpp    # test implementations, stats, JSON report writer
  main.cpp      # thin CLI shell (flags, run, print, exit)
  test/test_rttest.cpp
```

Edits: `tools/CMakeLists.txt` (library `instr_rttest_lib` +
executable `instr-rt-test`), `test/CMakeLists.txt` (unit test target).

This follows the standalone-tool precedent of `tools/istate_watchdog.cpp`
(single-file tool linking only `instr_common`), with a small lib/main split
so the core is unit-testable.

## Test suite (parent table applies verbatim)

`kernel-identity`, `rt-sched-access`, `wakeup-latency` (idle + under load),
`prio-inversion` (non-PI vs PI), `wake-latency-ipc`, `timer-jitter`,
`irq-threading`; verdict against the task 002 budget (≤500 µs under load).

## Affected interfaces

- **None changed.** rt-test opens no instrumentation socket, consumes no
  daemon interface, and reads only `/proc` and `/sys`. The contract tests and
  docgen surface stay untouched (no proto exists — intentional; the generated
  `docs/interface.md` will not list rt-test).

## Expected files / components (exact)

- `tools/rt-test/rttest.cpp` — identity parser (`uname -v`,
  `/sys/kernel/realtime`, preemption model), RT scheduling access probe
  (SCHED_FIFO set + readback, `RLIMIT_RTPRIO`, `sched_rt_runtime_us`),
  cyclictest-style latency loop (`clock_nanosleep(TIMER_ABSTIME)`, per-CPU
  SCHED_FIFO prio-80 threads, 1 kHz), synthetic load generator (CPU/memory/
  I/O), priority-inversion scenario (PI + non-PI), pipe ping-pong latency,
  timer-jitter window, `/proc/interrupts` threaded-IRQ census,
  histogram/percentile stats, JSON report writer, verdict evaluator.
- `tools/rt-test/main.cpp` — CLI shell: flags (`--run`, `--out PATH`,
  `--duration S`, `--load on|off`, `--prio N`), `--help`, tee log sink,
  graceful SIGINT/SIGTERM, exit 0. Default result path: cwd
  `rt-test-results-<UTC timestamp>.json`.
- CMake: library `instr_rttest_lib` (links only `instr_common`); executable
  target named `instr-rt-test` (matches the `instr-istate-watchdog` naming
  style, so the Yocto recipe installs it verbatim).
- No daemon-side wiring: no `add_subdirectory(daemons/...)`, no systemd,
  no sysusers/tmpfiles, no proto, no ctrl.

## Local tests

1. **Unit (BUILD_TESTING):** identity parsing against fixture `uname -v` /
   `/sys/kernel/realtime` samples (RT and non-RT strings); percentile +
   histogram math on synthetic latency vectors; JSON report round-trip
   (write → parse → equal); verdict/threshold evaluation; graceful-degradation
   branch (no RT capability → `unavailable` recorded, SCHED_OTHER path, not a
   failure).
2. **Host functional:** run the built binary `--run` on the non-RT dev host;
   assert exit 0, table on stdout, valid JSON in the working directory.
3. Timing-sensitive measurements are never asserted on absolute values in CI
   (shared runners); only structure and math are gated.

## Role in cross-repo validation

- Provider for the Acadia-OS recipe change (binary must exist before the
  image build consumes it).
- Supplies the on-target RT evidence and the paired RT/non-RT result JSONs
  recorded in the parent `validation.md`; numbers cross-checked against
  `cyclictest` (task 002 D2).

## Risk to verify

- SCHED_FIFO without `CAP_SYS_NICE`/`RLIMIT_RTPRIO` must degrade to a
  recorded `unavailable` + SCHED_OTHER run, never a hard failure.
- Thread count clamped to available CPUs (1-core boards must work).
- Bounded total runtime (D6) so a demo finishes on schedule.