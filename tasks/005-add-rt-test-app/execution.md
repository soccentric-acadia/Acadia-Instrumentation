# Task 005 execution — Add `rt-test` standalone RT test application (Acadia-Instrumentation)

Parent task: `Acadia/tasks/005-add-rt-test-app/plan.md`
- **Approval date:** 2026-09-21
- **Approved scope:** standalone test app in `tools/rt-test/`; no daemon,
  no systemd, no proto/gRPC/istate involvement
- **Status:** implemented — host gate green; on-target validation pending

## Implementation checklist

- [x] `tools/rt-test/rttest.h` — core API (identity, sched probe, stats,
      verdict, report, suite)
- [x] `tools/rt-test/rttest.cpp` — implementation: uname/`/sys` identity
      parsing, SCHED_FIFO probe (rlimit + CapEff + throttling knobs),
      cyclictest-style 1 kHz wakeup-latency loops (idle + under CPU/memory/
      IO churn), bounded priority-inversion scenario (non-PI vs
      PTHREAD_PRIO_INHERIT), pipe ping-pong roundtrip, 10 kHz timer-jitter
      window, threaded-IRQ census, histogram/percentile stats, JSON report
      writer, verdict evaluation
- [x] `tools/rt-test/main.cpp` — CLI shell (`--out`, `--duration`, `--load`,
      `--prio`, `--threads`, `--json`, `--quiet`, `--help`), signal-safe
      partial report on SIGINT/SIGTERM
- [x] `tools/rt-test/test/test_rttest.cpp` — 16 unit tests (identity parsing,
      percentile/histogram math, JSON round-trip, verdict logic, degraded path)
- [x] CMake wiring: `tools/CMakeLists.txt` (`instr_rttest_lib` +
      `instr-rt-test`), `test/CMakeLists.txt` (`unit_rt_test`)
- [x] Host gate: `make build` / `make test` (343/343) / `check-format` /
      `run-clang-tidy` — all green; no tidy findings in `tools/rt-test/`

## Files changed

- `tools/rt-test/rttest.h`, `tools/rt-test/rttest.cpp`, `tools/rt-test/main.cpp`
- `tools/rt-test/test/test_rttest.cpp`
- `tools/CMakeLists.txt`, `test/CMakeLists.txt`

## Interface / schema / dependency changes

- None. No proto, no contract, no schema, no systemd, no socket. New binary
  `instr-rt-test` only. Contract tests untouched and passing.

## Deviations from the approved plan

- None material. Implementation notes:
  - `run_inversion` was promoted from the anonymous namespace to the public
    header API so the bounded scenario can be driven in isolation (it made
    debugging possible and keeps the scenario unit-testable).
  - Two defects found and fixed during bring-up (both would have hung or
    distorted the demo): the pipe ping-pong ponger blocked forever on a pipe
    with no remaining writer (fixed by closing the write end before joining),
    and the inversion cap joined the waiter before setting the stop flag
    (fixed ordering so the join is finite).

## Commands run and decisions

- Host run: `build-host-debug/tools/instr-rt-test --duration 2 --out /tmp/rt-test-demo.json`
  — full suite on the non-RT dev host (PREEMPT_DYNAMIC kernel), degraded
  SCHED_OTHER path exercised exactly as designed; valid JSON written;
  exit 0.
- Latency values measured on this shared host are recorded as demo output,
  never as acceptance evidence (plan: CI gates structure only).