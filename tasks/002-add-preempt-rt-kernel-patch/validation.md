# Task 002 validation (validation-only leaf)

- **Date:** 2026-09-21
- **Status:** pending — awaiting the on-target validation run against the RT image

## Evidence so far

- No source change in this repository; no host-side test was executed for
  task 002 (kernel-only change; host tests are kernel-independent and
  unaffected).

## Pending on-target checks (image: rpi5_20260921_111255, 6.6.78-rt51 SMP PREEMPT_RT)

Owner: platform team (joint run with Acadia-OS on-target validation).

- (HAL) Device tests for CAN / 1-Wire / PWM / RTC against the RT image.
- (Hardware) Pin-map re-check + measured timing for CAN, 1-Wire, PWM.
- (Instrumentation) istate/monitord smoke test; no new timeout/missed-sample
  faults under RT scheduling.

Results to be appended here when hardware is available. Skipped-with-reason
recorded in the parent `tasks/002-add-preempt-rt-kernel-patch/validation.md`.
