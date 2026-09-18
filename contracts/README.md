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

These artifacts are intentionally published in Instrumentation only. Updating
HQ or Configuration copies, generated clients, or deployment packaging
requires a coordinated cross-repository change.
