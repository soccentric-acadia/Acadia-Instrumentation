# Changelog

## 2026-09-24 — Integration-production pass (T-P25, T-P31, T-P32, T-P35)

- **Fix:** spokes could never register with istate on a device. With the default root "/", the dial target became `unix://run/...`, which gRPC rejects. `uds_target` now normalises it, and the topology tests use the production path shape (`3d03ba6`).
- T-P25: the operation catalog is generated from the tables and checked by ctest.
- T-P31: OpenSSL crypto, AEAD v2 with a legacy read path, versioned salts and argv-based exec.
- T-P32: Acadia rebrand; non-Acadia descriptors pruned; YAML factory identity.
- T-P00c: `disabled_spokes` (OTA is off on Acadia) and a polkit rule.
- INS-020: GitHub CI covering build, 366 ctests, format, catalog drift and ASan+UBSan. The sim-device image builds again.
- Validation: 366/366 ctest; clean ubuntu:24.04 CI reproduction; sim device runs with all 8 enabled spokes ready.

## 2026-09-21 — Cross-repo API surface audit: istate contract extensions (task 009)

### Added

- Extended `istate.proto` with the telemetry, fault, and log-manager
  contract extensions, spool support for log bundles, and the
  DataFanout / ControlAllowed northbound paths; refreshed contract
  golden hash and device/compatibility contract artifacts.

## 2026-09-21 — Heartbeat wiring, istate self-configuration and review repairs

### Fixed

- Repaired the tree at HEAD: the DefaultFileSet refactor left `core.cpp` and
  every istate test calling the old flat-map signatures; istate and all eight
  test targets now build, migrate fixtures included (schema-version constant
  replaces a hardcoded "1").
- Spoke heartbeats no longer starve under traffic: the transport writer sends
  a heartbeat on its own cadence every interval regardless of queued frames
  (§3.1), and a busy spoke can no longer look disconnected to GetPeerStatus.
- GetPeerStatus counts the last thing a peer said (heartbeat or any frame),
  derives its staleness window from `heartbeat_failure_timeout_s` instead of a
  hardcoded 30 s, and reports the HQ peer connected from the session state.
- `pet_watchdog` implements the documented contract: a down must-live daemon
  is petted through a grace window, after which petting stops so the hardware
  watchdog acts as the last resort (previously the watchdog was petted
  unconditionally, making the must-live gate toothless).
- Removed the mangled Doxygen block above `TypedHub` in hubs.cpp.

### Added

- istate applies its own configuration (§6): heartbeat cadence, warning and
  failure thresholds, watchdog timeout, HQ endpoint and the time policy are
  runtime-effective at boot, after every config write (write listener), and
  via deliver_config("istate"); the cadence is relayed to every spoke with a
  transport-level `set-heartbeat-interval` command, and late registerers
  receive the current cadence and time policy at registration.
- networkd `set-time-policy` command: applies istate's policy (source
  priority, NTP servers, PTP domain/constellations) to the plan and re-renders
  the time daemons through the platform (R8).
- logmanager enforces the operator-configured daemon-log rotation policy
  (`daemon_log_dir`, `daemon_log_rotate_size_bytes`, `daemon_log_retention_files`) —
  previously dead keys.
- Warning tier in the liveness sweep: a peer quiet past
  `heartbeat_warning_timeout_s` degrades before the hang threshold; the tier
  is configurable and defaults to 15 s (previously declared but unused).
- webconfigd (Acadia-Configuration) heartbeats istate every 5 s so the
  configuration peer reports connected (the RPC previously had no caller).
- Tests: heartbeat recording with staleness/peer-mismatch rejection, own-config
  application and write-listener chain, late-registrant policy delivery,
  watchdog grace policy, warning tier, heartbeat-under-traffic over the real
  channel, set-time-policy dispatch, and the daemon-log rotator (352 total).

## 2026-09-21 — Root-agent feature workflow guard (task 006)

- Added a canonical instruction requiring leaf-started agents to refuse feature
  changes and direct the user to the Acadia parent root.

## 2026-09-21 — Version support (task 001)

### Added

- Added a Makefile `version` target (CONV-004) that prints the existing
  `VERSION` file, so Make and CMake share one source of truth and the parent
  `make versions` table covers this repository.

Task records: `tasks/001-add-version-support-all-repos/`.

## 2026-09-18 — Agent documentation and task workflow standardization

- Added canonical `AGENTS.md` and `CLAUDE.md` pointers plus a concise canonical
  agent document with CMake build and test instructions.
- Added ignored local `Plan.md` and `Task.md` workflow artifacts.
- Verified the required document shape and the declared `make build` and
  `make test` targets.

Maintained by the Loop Engine. One entry per loop that touched this repo,
newest first, appended on loop close (pass or fail) and committed on the loop's
own feature branch. See the master repo's `Arches-Loop-Orchestration-Spec-v2.md` §11.1.

Entries written by hand are fine; the engine inserts directly under this header.

## Build made arch-independent

The build no longer depends on where it runs. `find_package(Threads)` +
`Threads::Threads` now links pthreads portably (older toolchains — JetPack/L4T
aarch64, pre-2.34 glibc Yocto, Debian 10 — previously failed with undefined
`pthread_create` because the test build hardcoded `pthread` and the daemons
linked by luck on glibc 2.34+); gTest links via imported targets with a bare-name
fallback for pkg-config-only sysroots; `find_program(... REQUIRED)` calls (needs
CMake ≥ 3.18) replaced with manual checks so the declared CMake 3.16 baseline is
honoured; protoc resolves via target-or-PATH fallback; GNU-only warning flags are
compiler-guarded; deprecated `EXECUTABLE_OUTPUT_PATH`/`LIBRARY_OUTPUT_PATH` and
the source-tree `output_dir/` pollution are gone; one `isdigit()` now casts to
`unsigned char` (plain `char` is signed on x86, unsigned on ARM). Stale docs
(`REPO_MAP.md`, `.claude/validate.sh`, `.vscode/settings.json`) no longer describe
the removed Docker/aarch64-container workflow.

## Yocto recipe moved to meta-acadia

The in-repo `yocto/` layer is removed: the `instrumentation` bitbake recipe now lives in the
OS layer (`Acadia-OS/meta/meta-acadia/recipes-instrumentation/instrumentation/`), and
`acadia-image.bb` installs it. This repo is a plain CMake project again — no bitbake files.
Runtime fixes carried over to the meta-acadia recipe from the RPi 5 audit: scarthgap series
compat, Acadia SRC_URI/SRCREV, parse-time-valid PV, gpio group for the udev rule, and
CM5 machine mapping.

## 1.0.0

First tagged version of the suite. `VERSION` is one version for the whole
suite (REQ §2) — the ten daemons and ten `*_ctrl` tools are released together
and are not independently versioned.

## Yocto cross-build fixes

Three things that only a cross (bitbake) build exercises, all reproduced
against the Acadia-OS native + target recipes:

- `catalog/CMakeLists.txt`: when `CMAKE_CROSSCOMPILING`, the alarms header is
  generated by the **host** alarmgen, resolved with
  `find_program(NO_CMAKE_FIND_ROOT_PATH)`. A bare target-name `COMMAND` is
  rewritten by CMake to the target binary's own path, bypassing PATH — so
  staging the native alarmgen (`DEPENDS instrumentation-native`) was not
  enough and the cross build died with `Exec format error`.
- `gtest_discover_tests()` deferred to ctest time (`DISCOVERY_MODE PRE_TEST`)
  under cross-compiling: the default runs every test binary at build time on
  the build host.
- The test suite is now gated on `BUILD_TESTING` (via `include(CTest)`), so
  the recipe's `-DBUILD_TESTING=OFF` actually skips it instead of dragging a
  GTest dependency into every Yocto build.
