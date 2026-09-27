# Task 010 — On-target validation suite (leaf record: Acadia-Instrumentation)

Parent task: `tasks/010-on-target-validation-suite/plan.md` (Acadia root).

| Field | Value |
|---|---|
| Class | validation-only (no source changes in this repo) |
| Track | Production |
| Date | 2026-09-26 |

## Scope in this repository

The Acadia root gains `validation/` — a Python suite executed from an x86 host
that validates the apps built from **this repository** as deployed on a flashed
Raspberry Pi (Acadia-OS image):

- systemd units `instr-istate.service`, `instr-istate-watchdog.service`,
  `instr-{networkd,monitord,hardware,faults,alarms,logmanager,telemetry,backup,ota}.service`
  (existence/enablement per T-P00c, active state, sandbox keys);
- binaries `/usr/bin/instr-<daemon>`, `/usr/bin/<name>_ctrl`, watchdog, rt-test;
- sockets `/run/instrumentation/<daemon>.sock` (0660 instr-<daemon>:instr),
  `<daemon>-ctrl.sock` (0600 root-only), `application.sock`
  (0660 instr-istate:instr-apps);
- packaged descriptors `/etc/instrumentation/{devices,capabilities,resource-profile}.yaml`,
  defaults `/usr/share/instrumentation/defaults/`, catalog, schemas, proto,
  `contracts/device-contract-v1.json` (proto schema v3);
- functional coverage of every `_ctrl` verb family (istate, alarms, monitord,
  hardware, faults, logmanager, telemetry, networkd, ota, backup) as listed in
  the repo README "Operate" section, plus integration checks (alarm→timeline,
  fault→timeline, sample→timeline, app.log ingestion, health score);
- resilience rules R1/R10 (spoke restart re-registration; istate restart →
  spokes survive and re-attach) behind `--resilience`.

## Affected interfaces

None changed. The suite is a consumer of:
- the ctrl wire envelope `{"ok":..,"data":..}` (`lib/common/src/command_table.cpp`);
- the ctrl arg syntax `op key=value --json` (`lib/ctrl/src/ctrl.cpp tool_main`);
- the deployed file layout from
  `Acadia-OS/meta/meta-acadia/recipes-instrumentation/instrumentation/instrumentation.bb`.

## Local tests

None added (validation-only). The suite's host-side verification lives at the
parent task (`--self-test`, `--plan`).

## Role in cross-repository validation

Produces the HIL evidence bundle (JSON + Markdown + raw command outputs) that
integration-production §6 requires before any "fails on target" verdict can be
closed.