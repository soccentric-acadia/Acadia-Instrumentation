# Task 014 — Acadia-Instrumentation Phase 0 validation

| Check | Result |
| --- | --- |
| `cmake --build --preset host-debug --target hardware unit_hardware unit_platform -j"$(nproc)"` | Passed. |
| `ctest --test-dir build-host-debug --output-on-failure -R '^(Hardware\|Descriptors.AcadiaProductionCapabilityContractRejectsUnsupportedBoardAndRevision\|descriptor_compatibility)'` | Passed: 18/18. |
| `python3 tools/contracts/verify_production_contract.py` | Passed; generated operation catalog, committed descriptor, production lock, prior-descriptor compatibility, and regenerated descriptor agree. |
| `git diff --check` | Passed. |
| `ctest --test-dir build-host-debug --output-on-failure -R '^Hardware'` | Follow-up authorization validation passed: 15/15. |
| `ctest --test-dir build-host-debug --output-on-failure -R '^HardwareSchedule.DueChecksRejectReservedAndUnsupportedGpioBeforeToggling$'` | Scheduled authorization negative validation passed: 1/1. |

The hardware negative test proves unlisted devices are `NotFound`, reserved
console GPIO and peripherals absent from the validated manifest are
`NotSupported`, and none reaches the platform. The descriptor verifier
self-test exercises removed/type-changed fields and removed methods/services; CI runs both
compatibility checks through CTest and the production-contract gate.

The follow-up audit coverage also invokes both `check` and `check-all` for
reserved and unsupported GPIO. Each returns an explicit `authorization`
`unknown` result with a `not supported` detail, while the simulated GPIO lines
remain untouched.

Scheduled due-check coverage applies the same reserved and unsupported GPIO
policy and proves the due-check result is an authorization refusal before
either GPIO line can be toggled.

Skipped: full CTest, hardware-on-target validation, and cross-repository
consumer verification are outside these two Phase 0 fixes and were not run.

## Phase 1 P1-08 and P1-09

- `python3 tools/contracts/verify_production_contract.py` — passed; includes
  authorization-matrix terminal-state and destructive-confirmation assertions.
