# Task 014 — Acadia-Instrumentation scope

Parent plan: [Acadia Task 014](../../../../tasks/014-production-flow-operational-readiness/plan.md).

## Scope

Instrumentation is the source of truth for device protobuf and operation-catalog contracts and provides spoke state, authorization, data fanout/spooling, and lifecycle health. Execute P0-01, P0-02, P0-04, P1-08 through P2-13, P0-16, P1-20 through P2-22, P2-25, and P0-26 where they affect Instrumentation.

## Interfaces and expected work

- Deterministic descriptor/catalog generation and compatibility policy.
- `ConfigurationApi`, data stream, command authorization, operation result, spool, health, and upload interfaces.
- Explicitly gated `HqPeer` behavior and documented AgentLink transport boundary.

## Local and cross-repository validation

Run CTest and generated-contract checks; add negative authorization, reconnect, deduplication, spool/backpressure, and fault/telemetry tests. Participate in real-binary compose and supported-board HIL validation with Configuration and HQ.
