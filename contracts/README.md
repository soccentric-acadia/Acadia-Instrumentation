# Device contract artifacts

`device-contract-v1.json` is the versioned, device-facing metadata contract
owned by Instrumentation. It publishes the already-deployed wire contract; it
does not change any `.proto` field or handshake behavior. The adjacent
`proto-schema-v3.pb` is the deterministic descriptor set for the sources in
`../proto/`, and `compatibility-v1.json` states the safe and breaking change
rules consumers must apply.

Consumers should:

1. Read `device-contract-v1.json`.
2. Verify the SHA-256 of `proto-schema-v3.pb` and `../catalog/alarms.yaml`.
3. Require an exact `proto_schema_version` match before registration.
4. Treat catalog additions as compatible only when existing IDs and names are
   unchanged. Never infer compatibility from the suite version alone.

`production-contract-lock-v1.json` freezes the descriptor, the Configuration
proto subset, and the generated operation catalog together. CI runs
`tools/contracts/verify_production_contract.py`, which regenerates the
descriptor and catalog before accepting a change. Consumers vendor this lock
and verify the artifacts they compile or load against its digests.

`proto-schema-v3-compatibility-baseline.pb` is the checked-in prior descriptor
baseline. CI compares it to the current descriptor and rejects removed,
renumbered, or type-changed message fields, plus removed or signature-changed
services and methods. Additive fields, services, and methods remain allowed.

These artifacts are intentionally published in Instrumentation only. Updating
HQ or Configuration copies, generated clients, or deployment packaging
requires a coordinated cross-repository change.

## Operation catalog (T-P25)

`operation-catalog.json` lists every spoke control verb (name, description,
arguments) and is **generated** from the daemons' command-table
registrations by `tools/catalog/gen_operation_catalog.py`. The ctest
`operation_catalog_up_to_date` fails when a verb is added or renamed without
regenerating it. Acadia-Configuration and Acadia-HQ vendor this file instead
of keeping hand copies; after regenerating here, re-vendor it there.
