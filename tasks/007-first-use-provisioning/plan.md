# Task 007 (Acadia-Instrumentation) — First-use provisioning: complete `/pst` default set

Parent task: `tasks/007-first-use-provisioning/plan.md` (Acadia parent
repository). Status: draft — awaiting approval. Date: 2026-09-21.
Owner: Sandesh Ghimire.

## Scope (this repository only)

istate owns `/pst` (R4): all provisioning happens in istate's first-boot
path; the spokes gain read-fallbacks. No proto, contract, or packaging change
in this repository.

## Affected interfaces and components

1. `daemons/istate/include/instr/istate/pst.h`
   - `using DefaultFileSet = std::map<std::string, std::map<std::string, std::string>>;`
   - `seed_defaults(DefaultFileSet)`, `migrate(DefaultFileSet)`,
     `system_reset(DefaultFileSet)` signatures.
   - `kConfigSchemaVersion` "1" → "2" (same commit that widens the seeded set).
2. `daemons/istate/src/pst.cpp`
   - `seed_defaults`: write every default file (`write_protected` →
     checksum + `.bak`), stamp schema version on virgin.
   - `migrate`: config.yaml keeps `merge_release`; other files added only
     when missing, never overwritten.
   - `system_reset` reseeds the complete set.
3. `daemons/istate/include/instr/istate/core.h`, `src/core.cpp`, `src/main.cpp`
   - `boot(const DefaultFileSet&)`, `set_default_provider` signature.
   - `load_defaults()` walks every file under
     `/usr/share/instrumentation/defaults/<daemon>/` (root-aware).
4. `lib/common` — first-existing-path read helper + unit test
   (`test/unit/test_common.cpp`).
5. `daemons/faults/src/main.cpp` — rules from
   `pst/system/faults/fault-rules.yaml`, fallback
   `/etc/instrumentation/fault-rules.yaml`.
6. `daemons/hardware/src/main.cpp` — descriptor from
   `pst/system/hardware/devices.yaml`, fallback `/etc/instrumentation/devices.yaml`.
7. `daemons/alarms/src/main.cpp` — merge source from
   `pst/system/alarms/catalog.yaml`, fallback
   `/usr/share/instrumentation/catalog/alarms.yaml` (runtime catalog path
   `/pst/state/alarms/catalog.yaml` unchanged).
8. Tests: `test_pst.cpp` (full-set seeding, migration add/preserve,
   system_reset), `test_config.cpp`, `test_provisioning.cpp` (signatures),
   `test/integration/harness.cpp` (defaults include a non-config file),
   `test_common.cpp` (helper).

## Local tests

- Full ctest (`make test`), `make check-format`.
- New/extended cases listed above; integration boot/reset with widened set.

## Role in cross-repository validation

- Provides the validated instrumentation commit referenced by the Acadia-OS
  `INSTR_SRCREV` bump.
- Confirms config API behaviour unchanged for Acadia-Configuration
  (validation-only check in that repo, coordinated by the parent).