# Task 005 validation — `rt-test` standalone RT test application

- **Date:** 2026-09-21
- **Status:** host-side validation complete; independent audit passed (85/100); on-target validation pending (rpi5 hardware, platform team)

## Evidence — Acadia-Instrumentation (host gate)

Repository: `Project/Acadia-Instrumentation`, branch `develop` (uncommitted worktree).

| Check | Command | Result |
|---|---|---|
| Full build | `cmake --build --preset host-debug -j` | green, no warnings from `tools/rt-test/` |
| Test suite | `ctest --preset host-debug` | **343/343 passed** (16 new `RtTest*` tests included) |
| Format gate | `cmake --build --preset host-debug --target check-format` | green (whole tree) |
| Static analysis | `cmake --build --preset host-debug --target run-clang-tidy` | green; zero findings in `tools/rt-test/` |

New unit tests (16, all passing): identity parsing (PREEMPT_RT / VOLUNTARY /
PREEMPT / DYNAMIC / NONE / unknown strings; `/sys/kernel/realtime` alone),
percentiles on a known 100-sample vector, histogram bucketing + overflow,
singleton/empty safety, verdict evaluation (within/over budget, non-RT,
missing load phase), JSON report round-trip (identity/sched/tests/verdict/
rt_improves/warnings), degraded SCHED_FIFO run recorded as a warning.

## Evidence — functional run on a non-RT host

`build-host-debug/tools/instr-rt-test --duration 2 --out /tmp/rt-test-demo.json`
— host: Ubuntu 24.04, 32 CPUs, kernel `7.0.0-31-generic PREEMPT_DYNAMIC`
(non-RT), unprivileged (SCHED_FIFO refused).

- Full suite completed, exit 0, table printed, valid JSON written
  (parsed with a JSON reader: all sections present, 64-bucket histograms).
- Degradation path exercised as designed: `SCHED_FIFO not granted
  (Operation not permitted)` recorded in warnings; loops ran under
  SCHED_OTHER; report remained complete.
- Demo signal visible on non-RT: priority-inversion non-PI block ≫ PI block
  (619 ms vs 56 ms on the run recorded here); under-load wakeup latency
  max ≈ 57 ms with seconds-scale outliers on shared hardware.
- Measured values are demo output only — acceptance evidence is the
  on-target RT run below.

## Pending on-target checks (owner: platform team, joint with Acadia-OS)

1. Image with `instr-rt-test` (requires Acadia-Instrumentation commit +
   `INSTR_SRCREV` bump — see Acadia-OS leaf execution.md).
2. RT image (6.6.78-rt51) run as root: `PREEMPT_RT` detected, SCHED_FIFO
   granted, wakeup-latency idle/load within the 500 µs budget, threaded-IRQ
   census > 0; cross-check vs `cyclictest -m -p80 -i1000 -h400`.
3. Paired non-RT image run (stock pre-task-002 image or fragment-less build)
   — the client-facing RT-vs-non-RT evidence; both result JSONs recorded
   here with their SHA-256s.

## Skipped checks (with reason)

- `make build rpi5` — pending SRCREV bump (commit-gated); recorded in the
  Acadia-OS leaf. The recipe edit is a single install line with the same
  pattern as the daemon install loop directly above it.
- On-target runs — rpi5 hardware not available in this session.

## Known limitations / follow-ups

- One JSON per run, written to the working directory by default; retention
  is the operator's concern (approved plan decision).
- On very busy shared hosts the non-PI inversion block can be large (that is
  the demonstration); the scenario is hard-capped at 3 s so the demo stays
  bounded either way.

## Independent implementation audit (2026-09-21)

Reviewer: independent audit pass (not the implementing session's code
author role). Compared: original request, approved plan (parent + leaf),
execution record, and validation evidence. Verification commands re-run
independently: full build (0 errors/warnings), `ctest` 343/343, functional
run `--duration 1` (exit 0) with the JSON re-parsed (sha256
`d3c64b52…`), recipe diff inspected (exactly one install line + comment).

**Findings (applied during this audit):**

- `main.cpp` relied on transitive includes for `std::exit`/`std::atoi`
  (`<cstdlib>`) and `std::min`/`std::max` (`<algorithm>`) — fixed with
  direct includes; gate re-run green.
- `report_json` omitted the plan-specified `"version"` field — added
  (`INSTR_VERSION` = 1.0.0); gate re-run green.
- Audit-session hygiene: a verification command truncated a previously
  generated result file (empty-file hash recorded); regenerated and
  re-hashed. Lesson recorded in the root `improvements.md`.

**Scoring criteria and deductions (out of 100):**

| Criterion | Max | Awarded | Notes |
|---|---:|---:|---|
| Requirements coverage & traceability | 25 | 25 | All five request elements map to implemented behaviour; revision (app, not daemon) traceable in plans |
| Plan adherence & scope control | 15 | 15 | Scope guards held: no proto/gRPC/istate/systemd; provider-first order documented |
| Code quality & test coverage | 25 | 24 | 16 unit tests on all pure logic; −1: timing loops exercised only functionally (by design, but a smoke-style host-run ctest target could lock this in) |
| Validation evidence | 20 | 15 | −5: on-target paired RT/non-RT runs and the OS image build are pending (hardware + commit-gated); host evidence complete and reproducible |
| Documentation & records | 15 | 11 | −4: CHANGELOG entries pending (commit-time by design); --help covers usage, but a standalone usage note was not added beyond it |

**Final score: 85/100.** Evidence-based; remaining deductions correspond to
explicitly documented pending items (on-target runs, image build, changelog
at commit time), not to unverified claims.

**Remaining risks:** on-target behaviour unverified until the rpi5 run;
`INSTR_SRCREV` bump must land together with the provider commit or the
image silently lacks the tool (asymmetry documented in the Acadia-OS leaf).