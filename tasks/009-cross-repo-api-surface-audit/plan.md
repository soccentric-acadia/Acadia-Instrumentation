# Task 009 — Acadia-Instrumentation scope (leaf plan)
- **Status:** IMPLEMENTED — validated (see parent task validation.md)

Parent task: [`tasks/009-cross-repo-api-surface-audit/`](../../../../../tasks/009-cross-repo-api-surface-audit/plan.md)
in the Acadia parent repository. This leaf records only Instrumentation's part.

## Role

Instrumentation is the proto publisher and istate is the server of the
`ConfigurationApi` + the client of the (gated) `HqPeer` session.

## Changes implemented

- `proto/istate.proto`: added `DataStreamSpec` + `rpc StreamDeviceData`
  (server-stream `common.RelayedData`) to `ConfigurationApi` — the missing
  subscription API for spooled northbound data (finding H6).
- `daemons/istate/src/hubs.cpp`:
  - `StreamDeviceData` implementation (replay from `NorthboundSpool::peek` +
    live `DataFanout`, gap-free/deduped boundary);
  - telemetry `to_generic` now carries the full record set (not just a
    summary count) so device→HQ telemetry carries real samples;
  - `control_allowed` alias-normalized deny list (finding H4) and moved to
    the header for tests;
  - `HqFrame.command` target resolution fixed (finding H13).
- `daemons/istate/src/core.cpp` + `northbound.h/.cpp`: `DataFanout` +
  `spool_and_fanout` so every spooled record also reaches live subscribers.
- `daemons/istate/src/main.cpp`: production gate for the `HqClient` dial
  (finding H11 — decision record in the parent task).
- `docs/interface.md`: regenerated.
- Tests: `DataFanout` (2), `ControlAllowed` (1) in
  `daemons/istate/test/test_northbound.cpp`.

## Validation

352/352 ctest (host-debug preset) — see parent `validation.md`.