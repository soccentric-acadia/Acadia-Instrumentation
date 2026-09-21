# Task 003 (Acadia-Instrumentation) — `/pst` partition validation

Parent task: `tasks/003-add-pst-partition/plan.md` (Acadia parent repository).
Status: draft — awaiting approval. Date: 2026-09-21. Owner: Sandesh Ghimire.
Classification: **validation-only** — no code change expected.

## Role in cross-repository validation

Acadia-OS gains a dedicated p4 `pst` partition (1 GB ext4, label `pst`)
mounted at `/pst` before `instr-istate` starts. This repository consumes the
contract; the implementation must keep working unchanged against a mounted
`/pst` instead of a rootfs directory.

## Affected interfaces (contract, not code)

- `PstStore::mount()` (`daemons/istate/src/pst.cpp`) — expects `/pst`
  writable by `instr-istate:instr` and creates
  `system/`, `state/`, `state/alarms/`, `state/spool/`, `state/ota/`,
  `backup/` on the mounted volume. No change required; verify it seeds
  `.schema-version` on the partition.
- `daemons/alarms` — `/pst/state/alarms/active.db` + persisted catalog must
  survive an OTA round-trip (the acceptance criterion).
- `daemons/logmanager` — `/pst/state/logs` collection must resume after an
  update with history intact.
- `daemons/backup` — `/pst/system` snapshots and `/pst/backup` archives must
  remain available across an update.

## Expected files

None (no source change). If validation reveals an ownership or path issue,
the deviation is recorded and escalated for a scope update before any change
is made.

## Local tests

- Existing googletest suites for istate/alarms/logmanager/backup keep passing
  unchanged (they exercise `/pst` paths against a simulated root, so no code
  change is needed for the partition itself).

## Cross-repository validation (with Acadia-OS)

- On-target (hardware access required):
  1. Fresh flash: `instr-istate` healthy, `systemctl status instr-istate`
     clean, `/pst/system/.schema-version` present on the p4 partition.
  2. Raise an alarm, collect logs, create a backup snapshot; perform an OTA
     self-update; after reboot confirm all three survived (p4 untouched by
     SWUpdate).
  3. OTA the new release onto a device flashed with the *old* image: istate
     behaves exactly as today (volatile `/pst` fallback) with no failures.
- Record results in this folder's `validation.md` with the OS task records
  referenced.