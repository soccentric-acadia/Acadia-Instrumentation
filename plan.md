# Instrumentation Interface, Heartbeat, Logging, and Watchdog Plan

## 1. Purpose

This plan defines the work needed to make `istate`, the nine instrumentation
daemons, Configuration, and HQ operate through one consistent management
contract.

The design preserves the repository's existing hub-and-spoke rule:

* `istate` remains the only on-device hub and the only process that owns
  `/pst`.
* A spoke never connects directly to another spoke.
* Configuration and HQ use the same **logical** peer/control interface, while
  retaining their appropriate transports and authorization.
* A missing heartbeat is observable and diagnosable; it does not reboot the
  device.

## 2. Review of the current implementation

### 2.1 What already exists

The repository already provides useful foundations:

* `common.proto` has registration, acknowledgements, commands, and
  `LivenessPing`/`LivenessPong`.
* Spokes register with `istate` and maintain a reconnecting gRPC channel.
* `istate` records spoke liveness and performs periodic liveness sweeps.
* `ConfigurationApi` is served by `istate` over a Unix socket.
* HQ uses a bidirectional `HqPeer.Session` over the tunnel.
* `RelayCommand` and the shared command tables give `istate` a path to expose
  daemon operations without a direct spoke-to-spoke connection.
* Logging is structured and currently goes to stderr, which systemd/journald
  can collect.
* A platform hardware-watchdog abstraction exists and is currently petted by
  `istate`.

### 2.2 Gaps to close

1. **No single peer contract:** Configuration has a request/response API while
   HQ has a stream with different message shapes and operation naming.
2. **Heartbeat coverage is incomplete:** spoke liveness is primarily a hub
   ping/response mechanism; Configuration and HQ do not share a defined
   bidirectional heartbeat contract; heartbeat state is not exposed uniformly.
3. **Per-daemon files are not guaranteed:** daemon output is currently stderr
   and therefore normally lands in journald, not in a dedicated file.
4. **The requested watchdog semantics are unclear in the current model:** the
   existing platform watchdog can cause a hardware reset, whereas this
   requirement calls for a software monitor that logs an unhealthy `istate`
   and does not reboot.
5. **Control coverage is not explicitly exposed through `istate`:** relay
   support exists, but there is no canonical discovery, validation, audit, and
   result contract proving that every `_ctrl` operation is available through
   the hub.
6. **Operational policy is not centralized:** heartbeat intervals, timeout
   thresholds, log paths/retention, and watchdog behavior need configuration,
   schema validation, and documented defaults.

## 3. Terminology and non-negotiable rules

* **Peer:** any control-plane client of `istate`: a spoke, Configuration, or
  HQ.
* **Spoke:** one of `networkd`, `monitord`, `hardware`, `faults`, `alarms`,
  `logmanager`, `telemetry`, `ota`, or `backup`.
* **Heartbeat:** an application-level message containing identity, sequence,
  monotonic send time, readiness, and health summary. A transport connection
  being open is not itself a heartbeat.
* **Operational:** the peer has registered, is within the heartbeat deadline,
  and has not reported a fatal/degraded state that requires attention.
* **Control operation:** an operation from a daemon's command table, including
  operations currently exposed by that daemon's `_ctrl` tool.

The following rules apply to every phase:

1. No new direct socket, RPC, or file protocol between two spokes.
2. Every accepted command is authenticated by the transport, authorization
   checked by `istate`, assigned an ID, and written to the audit timeline.
3. Every failure returns a diagnosable error code and message; no timeout or
   invalid input is converted into a successful response.
4. Heartbeat timestamps use `CLOCK_MONOTONIC` plus boot ID. Wall time is
   informational only.
5. Heartbeat payloads contain no secrets, credentials, private keys, or full
   configuration values.

## 4. Target architecture

### 4.1 One logical peer interface

Add a shared, versioned peer contract in `proto/common.proto`:

* `PeerHello` / `PeerHelloReply`
* `Heartbeat`
* `HeartbeatAck`
* `PeerStatus`
* `ListOperationsRequest` / `OperationDescriptorList`
* `ControlRequest` / `ControlReply`
* `SubscribeEventsRequest` / `PeerEvent`

The contract must include:

* peer ID and peer kind (`spoke`, `configuration`, `hq`);
* build version and protocol schema version;
* capability names;
* heartbeat sequence and monotonic timestamp;
* readiness and health state;
* request ID, operation name, JSON argument object, actor, and deadline;
* structured result JSON, error code, and human-readable error;
* correlation IDs for audit and HQ relay.

The generated API should expose the same operation semantics to Configuration
and HQ. It is acceptable for Configuration to use gRPC-over-UDS and HQ to use
the existing authenticated gRPC-over-WireGuard session; the messages,
operation names, validation, result shape, and event semantics must be shared.

Do not give HQ unrestricted access merely because it uses the peer interface.
Authorization must classify operations as:

* read-only;
* runtime-safe control;
* configuration mutation;
* disruptive (`restart`, `reset`, factory reset, OTA);
* local-only or manufacturing-only.

The policy must be enforced in `istate` for both callers.

### 4.2 Heartbeat topology

Implement heartbeat in both directions:

* Every spoke sends `Heartbeat` to `istate` on its established channel.
* `istate` sends a heartbeat request or liveness challenge to every spoke and
  records the reply.
* Configuration sends heartbeats to `istate` on its peer channel.
* HQ and `istate` exchange heartbeats inside `HqPeer.Session`.
* `istate` publishes peer state through Configuration and HQ using the same
  `PeerStatus` shape.

Recommended initial policy, configurable through `istate` defaults:

| Setting | Default | Meaning |
|---|---:|---|
| heartbeat interval | 5 s | Normal send interval |
| warning deadline | 15 s | Three missed intervals: mark degraded and log |
| failure deadline | 30 s | Mark non-operational and raise a timeline/fault event |
| reconnect backoff | existing policy | Do not busy-loop a disconnected peer |
| sequence handling | strictly increasing per boot | Reject stale or duplicate health samples |

A heartbeat timeout must not by itself restart a spoke or reboot the device.
Existing liveness sweeps and systemd restart policy remain separate and are
reported as separate events.

### 4.3 Dedicated per-daemon logs

Interpret “each daemon has its own log in `/var/log/daemon.log`” as one log
file per daemon, not ten processes appending to one ambiguous file:

```text
/var/log/instrumentation/istate.log
/var/log/instrumentation/networkd.log
/var/log/instrumentation/monitord.log
/var/log/instrumentation/hardware.log
/var/log/instrumentation/faults.log
/var/log/instrumentation/alarms.log
/var/log/instrumentation/logmanager.log
/var/log/instrumentation/telemetry.log
/var/log/instrumentation/ota.log
/var/log/instrumentation/backup.log
```

If an external packaging requirement mandates the literal filename
`/var/log/daemon.log`, it must be clarified before implementation because a
single shared path cannot provide per-daemon isolation. The preferred naming
above can also be exposed through logmanager as source `daemon=<name>`.

Use a shared file sink with:

* atomic line writes and one structured record per line;
* the existing redaction logic before the sink;
* restrictive ownership/mode (`root:instr` or the chosen log-reader group);
* bounded rotation, retention, and disk-budget accounting;
* no credentials or private key material;
* continued stderr/journald output during migration or file-sink failure.

A file-sink failure must be logged to stderr/journald and must not stop a
daemon. Logmanager may collect the files, but daemons must not write another
daemon's file.

### 4.4 Software watchdog for `istate`

Add an independent software watchdog service/process, preferably
`instr-istate-watchdog`, supervised by systemd but independent of the `istate`
main loop.

The monitor should check all of the following before declaring `istate`
operational:

* the `istate` process/service is present;
* the health endpoint or heartbeat file has been refreshed within the failure
  deadline;
* the peer registry and event loop have advanced their monotonic sequence;
* the gRPC health/control endpoint answers a bounded request.

On failure it must:

1. write a clear warning/error to the watchdog log and journald;
2. include last-seen heartbeat age, boot ID, process ID, and failed check;
3. emit one edge-triggered event per outage and a recovery event;
4. optionally forward the event to the normal system event pipeline when
   available;
5. **never call reboot, `systemctl restart`, the hardware watchdog, or an OTA
   rollback.**

The existing hardware watchdog abstraction must be explicitly separated from
this requirement. If hardware watchdog petting remains enabled for a product
profile, it needs its own documented policy and tests; it must not be the
implementation of the no-reboot software watchdog.

## 5. Detailed implementation phases

### Phase 0 — Contract and policy freeze

1. Confirm peer kinds, authorization classes, heartbeat defaults, log path,
   ownership, rotation limits, and whether HQ may issue disruptive commands.
2. Add a protocol version and compatibility rules. Older peers must receive a
   diagnosable schema mismatch rather than silently losing heartbeat/control.
3. Add JSON/YAML schema entries for:
   * heartbeat policy;
   * software-watchdog policy;
   * per-daemon logging and retention policy;
   * control authorization policy.
4. Update `docs/interface.md` generation inputs and add a compatibility section.

### Phase 1 — Shared peer and heartbeat protocol

1. Extend `proto/common.proto` with the shared messages described above.
2. Extend `proto/istate.proto` so `ConfigurationApi` exposes:
   * peer registration/heartbeat;
   * peer status;
   * operation discovery;
   * generic control execution;
   * event streaming for heartbeat loss/recovery and command results.
3. Update `HqPeer.Session` to carry the same messages, or add a thin adapter
   that maps the session frames to the shared contract without duplicating
   validation.
4. Add a spoke-side heartbeat scheduler to the shared transport layer.
5. Add `istate` registry fields for last received/sent sequence, last receive
   monotonic time, health, readiness, protocol version, and timeout state.
6. Define duplicate, stale, malformed, and clock/boot-ID handling.
7. Add metrics and audit/timeline events for heartbeat sent, received, missed,
   recovered, rejected, and peer schema mismatch.

### Phase 2 — Uniform control through `istate`

1. Make every daemon publish its command-table descriptors through the hub.
2. Implement `ListOperations` in `istate` with operation name, description,
   argument schema, required authorization, and whether the operation streams.
3. Implement `Control` using the existing command table as the source of
   truth, so `_ctrl` and peer control cannot drift.
4. Map all existing `_ctrl` operations, including:
   * `istate_ctrl` state/config/status operations;
   * `networkd` network, tunnel, interface, and connectivity operations;
   * hardware device controls;
   * fault, alarm, log, telemetry, OTA, backup, and monitor operations.
5. Return the daemon's exact structured result and error through
   `ControlReply`; do not reduce it to a boolean.
6. Add request deadlines, cancellation, idempotency handling, and audit actor
   attribution (`configuration`, `hq`, `local`, or `istate`).
7. Make Configuration and HQ clients consume this one operation catalog and
   display/relay the same status and error codes.

### Phase 3 — Logging

1. Add a reusable `FileSink` in `lib/common`, sharing formatting and
   redaction with `StderrSink`.
2. Configure each daemon with its own source name and file path.
3. Add `system/tmpfiles.d` entries and package/install rules for the log
   directory and permissions.
4. Add rotation and retention through the existing logmanager/budget model.
5. Ensure startup, registration, heartbeat timeout, control failure, watchdog
   warning, recovery, and shutdown records are present in the owning daemon's
   file.
6. Keep journald/stderr as a fallback and test behavior when the log
   directory is read-only or full.

### Phase 4 — Software watchdog

1. Add a small watchdog health contract (UDS endpoint or atomically replaced
   heartbeat/status file) that `istate` updates only after its main loop and
   control/event processing have made progress.
2. Implement `instr-istate-watchdog` as a separate executable or service.
3. Add a systemd unit with:
   * no `CAP_SYS_BOOT`;
   * no `/dev/watchdog` access;
   * no restart/reboot action;
   * bounded restart policy for the monitor itself;
   * explicit `After=` ordering on the istate unit.
4. Add edge-triggered outage/recovery logging and rate limiting so a stuck
   device does not fill storage.
5. Expose watchdog state in `SystemInfo`, `PeerStatus`, and the health score.

### Phase 5 — Integration and rollout

1. Run the simulated device with all spokes, Configuration, and a fake HQ.
2. Roll out heartbeat/control protocol changes with compatibility gates.
3. Verify permissions and service sandboxing on the target image.
4. Enable per-daemon files first in diagnostics builds, then production
   profiles after rotation and storage budgets are validated.
5. Enable software watchdog logging in observe-only mode before field release.

## 6. Files and surfaces expected to change

These are expected touch points; exact names may change during implementation:

* `proto/common.proto`, `proto/istate.proto`, and generated contract hashes.
* `lib/transport/*` for shared heartbeat and peer-channel behavior.
* `daemons/istate/*` for peer registry, authorization, control dispatch,
  heartbeat policy, health endpoint, and event/audit records.
* `lib/common/*` for the file sink, log rotation policy, and watchdog status
  record helpers.
* Each daemon's transport/main wiring for heartbeat scheduling and log setup.
* `system/systemd/*` for the watchdog unit and service hardening.
* `system/tmpfiles.d/instrumentation.conf`,
  `system/sysusers.d/instrumentation.conf`, and image packaging rules.
* `schemas/*` and `system/defaults/istate/config.yaml`.
* `docs/interface.md`, generated documentation inputs, and operational docs.
* Unit, contract, integration, fault-injection, and system-file tests.

Do not edit generated protobuf sources or generated interface documentation by
hand; update their inputs and regenerate them using the repository's normal
build/documentation flow.

## 7. Test and acceptance plan

### Protocol and heartbeat tests

* Registration succeeds for every spoke, Configuration, and HQ with matching
  versions.
* A schema mismatch is refused with a visible reason and audit/timeline event.
* Each peer sends heartbeats at the configured interval with increasing
  sequence numbers and the current boot ID.
* Missing one, two, and three intervals produce the documented state
  transitions; recovery clears the degraded state exactly once.
* Stale, duplicate, malformed, and wrong-peer heartbeats are rejected.
* HQ disconnect/reconnect does not block local Configuration or spoke control.
* Heartbeat traffic does not starve alarms, faults, or control responses.

### Control coverage tests

* Every command-table operation is present in the `istate` operation catalog.
* For every daemon, invoking an operation through `_ctrl` and through
  `istate` produces equivalent validation and result semantics.
* Unauthorized Configuration/HQ calls fail; authorized calls carry the right
  actor in the audit trail.
* Timeouts, cancellation, malformed JSON, unknown operation, and daemon
  disconnect all return explicit errors.
* Factory reset, OTA, restart, and other disruptive operations require their
  existing confirmations and policy.

### Logging tests

* Each daemon writes only to its own file.
* Records contain daemon source, level, monotonic timestamp, boot ID, and
  redacted fields.
* Rotation, retention, full filesystem, missing directory, and permission
  failures are safe and diagnosable.
* No secret-leak test can find credentials in files, fallback output, or
  heartbeat/control payloads.

### Software-watchdog tests

* Healthy `istate` refreshes its health signal and produces no warning.
* A stalled event loop is detected after the configured deadline.
* A dead process is detected and logged.
* Recovery is logged once after progress resumes.
* The watchdog never opens `/dev/watchdog`, invokes reboot, or restarts
  `istate`.
* Watchdog and log-manager failures do not create an infinite logging loop.

### System and regression gates

Run the repository's normal validation:

```sh
./.claude/validate.sh quick
./.claude/validate.sh full
```

The implementation is complete only when the targeted tests, contract hash,
system-file checks, sanitizers, and the full validation gate pass. A validation
exit code of `77` remains “not run,” not success.

## 8. Definition of done

The feature set is ready for release when:

1. Configuration and HQ use the same versioned peer/control semantics.
2. `istate` receives and reports periodic heartbeats from every daemon,
   Configuration, and HQ, while also checking peer responsiveness.
3. Heartbeat loss produces bounded, actionable logs and state transitions
   without an automatic reboot.
4. Every daemon has an isolated, redacted, rotated log file under the agreed
   `/var/log` layout.
5. The independent software watchdog logs an unhealthy `istate` and has no
   reboot/restart path.
6. Every daemon `_ctrl` operation is discoverable and executable through
   `istate` subject to authorization.
7. The generated interface documentation, schemas, service files, tests, and
   deployment packaging all describe the same behavior.
