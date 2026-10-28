# Task 014 — Acadia-Instrumentation execution record

- **Approval date:** 2026-09-28
- **Approved scope:** Parent Task 014 Phase 0, P0-01, P0-02, and P0-04.

## Checklist

- [x] P0-01 Publish and verify the prior descriptor compatibility baseline.
- [ ] P0-02 Generate the canonical operation catalog.
- [x] P0-04 Enforce the supported production capability matrix for hardware operations.

## Implementation record

### P0-04 — 2026-09-28

- Extended the capability schema to require Hardware matrix version `1.0.0`,
  board revision `rpi5-cm5-bom-rev-c`, and an allowed board (`rpi-5` or
  `cm5`).
- Added `capability` and `reserved` policy to device descriptors and pass the
  validated board/revision manifest into Hardware's `DeviceManager`.
  Hardware now fails closed when the descriptor board does not match its
  manifest, a device capability is absent, or a descriptor marks a device
  reserved. The daemon refuses startup without both validated descriptors.
- Added focused negative coverage for an unlisted device, reserved debug
  console GPIO, and a GPIO peripheral missing from the board capability list.
- Closed the follow-up audit finding: `check`, `check-all`, and scheduled
  checks now authorize the device before running any health check. Rejected
  devices emit an explicit `authorization` result with `unknown` state and a
  `not supported` detail; no potentially destructive check reaches hardware.
- Added focused scheduled due-check coverage for reserved-console and
  capability-disabled GPIO, proving both emit authorization refusals without
  toggling either line.

### P0-01 — 2026-09-28

- Added `contracts/proto-schema-v3-compatibility-baseline.pb`, a checked-in
  prior descriptor baseline, and
  `tools/contracts/verify_descriptor_compatibility.py`.
- The verifier rejects removed or changed-number/type/label protobuf fields
  and removed or signature-changed services and methods. Its direct self-test
  covers removed/type-changed fields plus removed methods and services.
- Registered descriptor verification and self-test with CTest and extended
  the production-contract CI gate so the generated current descriptor is
  compared with the compatibility baseline.

Validation executed:

| Command | Result |
| --- | --- |
| `cmake --build --preset host-debug --target unit_hardware unit_platform -j"$(nproc)"` | Passed. |
| `ctest --test-dir build-host-debug --output-on-failure -R '^(Hardware\|Descriptors.AcadiaProductionCapabilityContractRejectsUnsupportedBoardAndRevision\|descriptor_compatibility)'` | Passed: 18/18. |
| `python3 tools/contracts/verify_production_contract.py` | Passed; operation catalog, baseline compatibility, committed descriptor, and regenerated descriptor match. |
| `ctest --test-dir build-host-debug --output-on-failure -R '^Hardware'` | Passed after the check authorization follow-up: 15/15. |
| `ctest --test-dir build-host-debug --output-on-failure -R '^HardwareSchedule.DueChecksRejectReservedAndUnsupportedGpioBeforeToggling$'` | Passed: 1/1. |

### P1-08 and P1-09 — 2026-09-28

- Published the canonical machine-testable command authorization and terminal
  state matrix. The contract verifier rejects missing shared command ID,
  incomplete terminal state transitions, or missing destructive confirmations.
- Current ControlRequest accepts only uint64 request IDs and has no cancel
  frame; Configuration therefore derives a stable correlation ID and HQ only
  cancels commands before delivery. Protocol expansion is required for exact
  string IDs and in-flight cancellation.
