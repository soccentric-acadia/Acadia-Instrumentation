# Task 003 (Acadia-Instrumentation) — validation record

Classification: validation-only — no code changes made to this repository.

## Contract verification (read-only inspection)

- `instr-istate.service`, `instr-alarms.service`, `instr-backup.service`,
  `instr-logmanager.service`, `instr-ota.service` already declare
  `RequiresMountsFor=/pst` and expect a `pst.mount` unit from the OS layer
  (Task.md D-04) — the Acadia-OS `acadia-pst-init` recipe now provides
  exactly that unit.
- `PstStore::mount()` layout (`system/`, `state/`, `state/alarms/`,
  `state/spool/`, `state/ota/`, `backup/`) is preserved; `/pst/system` and
  `/pst/state/spool` remain istate-created (0755, group read-only).
- Per-spoke store dirs (`state/alarms` → instr-alarms, `state/logs` →
  instr-logmanager, `state/ota` → instr-ota, `backup` → instr-backup) are
  provisioned by the OS init script with setgid group-write (2770), matching
  the units' `ReadWritePaths` contract.

## Local tests

- Existing daemon test suites unchanged and green (run via the Acadia-OS
  build; instrumentation code untouched by this task).

## Cross-repository validation (with Acadia-OS)

| Step | Result |
|------|--------|
| OS artifact carries the mount contract (pst.mount + units in OTA rootfs) | PASS (debugfs extraction, parent validation.md #10) |
| On-target: istate healthy with mounted /pst, OTA round-trip state survival, old-layout OTA fallback | PENDING hardware access — procedure in parent validation.md; owner: Acadia-OS/Instrumentation maintainers |

No deviations from the leaf plan (no code change was required).