# Task 002 (leaf) — Add PREEMPT_RT patch to the Linux kernel — Acadia-Instrumentation

Parent task: `Acadia/tasks/002-add-preempt-rt-kernel-patch/plan.md`
- **Task ID:** 002
- **Date:** 2026-09-21
- **Status:** approved 2026-09-21 (aarch64-only scope, D1 resolved) — build-side complete; on-target pending
- **Class:** validation-only

## Scope in this repository

None (no code change). The instrumentation daemons run inside the Acadia-OS
image. The PREEMPT_RT kernel changes kernel scheduling behavior, not any
interface instrumentation depends on (gRPC-over-UDS, schemas, sysfs/procfs
reads are stable across preempt models).

## Affected interfaces

- None changed. Runtime observation of monitord/istate on the RT image for
  regressions only.

## Expected validation role

1. Host build/test: unaffected (no source change; host tests are
   kernel-independent) — may be skipped with a recorded reason.
2. On-target smoke test against the RT image built by Acadia-OS (task 002):
   `istate` status via `_ctrl`, monitord sampling produces telemetry/faults
   records, no unexpected fault/alarm churn under RT scheduling.
3. Results recorded in this leaf's `validation.md` with the image revision
   tested.

## Risk to verify

- monitord/telemetry polling loops are userspace; RT threading should not
  affect them. Confirm no new timeout/missed-sample faults appear on target.