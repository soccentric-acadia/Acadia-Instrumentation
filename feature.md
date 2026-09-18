# Acadia fleet-management feature matrix

This matrix is the working contract for the three repositories:

* **HQ** is the multi-tenant fleet authority and command queue.
* **Configuration** is the local WebConfig daemon and device-initiated HQ
  session owner.
* **Instrumentation** is the on-device `istate` hub and the only owner of
  daemon control, `/pst`, peer health, and device audit state.

`Implemented` means there is production code and a passing repository gate.
`Partial` means code exists but the end-to-end path, policy, or test is
incomplete. `Missing` means the feature is specified or needed by the
architecture but is not implemented. `Blocked` means it requires a product or
deployment decision before code can safely land.

| # | Feature | HQ | Configuration | Instrumentation | Owner / application | Status |
|---:|---|---|---|---|---|---|
| 1 | Tenant isolation | Implemented | N/A | N/A | HQ API | Implemented |
| 2 | Viewer/operator/admin roles | Implemented | Local RBAC | N/A | HQ + Configuration | Implemented |
| 3 | Device records | Implemented | Device identity read-only | Identity source | Fleet inventory | Partial |
| 4 | Device creation | Implemented | Pairing token consumer | Enrollment peer | Provisioning | Implemented |
| 5 | Single-use pairing token | Implemented | Durable pairing agent | Enrollment support | Enrollment | Implemented |
| 6 | Pairing token expiry | Implemented | Local expiry handling | N/A | Enrollment | Implemented |
| 7 | Long-lived device credential | Credential database | Durable credential store | N/A | Enrollment | Implemented |
| 8 | Credential hashing at rest | Implemented | Credential protected | N/A | Security | Implemented |
| 9 | Credential revocation | Gateway enforcement | Revoked state | Peer disconnect support | Security | Partial |
| 10 | Credential rotation | Implemented | Re-enrollment path | Key rotation RPC | Security | Partial |
| 11 | Device-initiated connection | AgentLink server | AgentLink client | HqPeer client | Connectivity | Partial |
| 12 | Shared device protocol source | HQ proto copy | Configuration proto copy | Separate istate proto | Contract owner | Missing |
| 13 | Protocol version negotiation | Implemented | Implemented | Registration version | Compatibility | Partial |
| 14 | Descriptor/hash negotiation | Missing | Missing | Descriptor artifacts | Compatibility | Missing |
| 15 | Capability negotiation | Device model partial | Board manifest | Peer/catalog capabilities | Compatibility | Partial |
| 16 | Device boot ID | Implemented | Implemented | Heartbeat support | Liveness | Partial |
| 17 | Monotonic heartbeat sequence | Implemented | Implemented | Instrumentation heartbeat | Liveness | Partial |
| 18 | HQ heartbeat acknowledgement | Implemented | Implemented | Separate HqPeer shape | Liveness | Partial |
| 19 | Configuration heartbeat to istate | No consumer | Missing client | RPC exists | Liveness | Missing |
| 20 | Spoke heartbeat to istate | N/A | N/A | Implemented | Device health | Implemented |
| 21 | Peer status aggregation | Device online flag | Missing | Peer registry | Device health | Missing |
| 22 | HQ reachability status | Online flag | Pairing state only | HqPeer status | Device health | Partial |
| 23 | istate reachability status | Missing | Missing | Local state | Device health | Missing |
| 24 | Daemon health summary | Missing | Missing | Health score exists | Device health | Partial |
| 25 | Degraded state reporting | Stale sweep | Missing | Peer state | Device health | Partial |
| 26 | Offline transition audit | Partial | Missing | Timeline support | Audit | Missing |
| 27 | Reconnect backoff | Gateway/session | Agent backoff | Spoke backoff | Resilience | Implemented |
| 28 | Bounded reconnect jitter | N/A | Implemented | Existing policy | Resilience | Implemented |
| 29 | Network partition recovery | Gateway sweep | Agent reconnect | Peer recovery | Resilience | Partial |
| 30 | Process restart recovery | Pending rows | Durable pairing | Durable `/pst` | Resilience | Partial |
| 31 | Device reboot recovery | Presence reset | Credential reconnect | Boot-aware state | Resilience | Partial |
| 32 | Pending command persistence | Implemented | In-memory delivery only | Relay support | Commands | Partial |
| 33 | Pending command leasing | Missing | Missing | N/A | Commands | Missing |
| 34 | Pending command retry policy | Missing | Missing | N/A | Commands | Missing |
| 35 | Command delivery acknowledgement | Implemented | Implemented | ControlReply exists | Commands | Partial |
| 36 | Command correlation ID | Newly persisted | Newly sent | Request IDs exist | Commands | Partial |
| 37 | Idempotency key | Schema fields | Bounded cache | Control request ID | Commands | Partial |
| 38 | At-most-once destructive execution | Missing | Partial wrapper | Policy support | Safety | Missing |
| 39 | Command deadline propagation | Missing | Field exists | Control deadline | Safety | Partial |
| 40 | Duplicate command response | Missing | Completed cache | Audit query missing | Safety | Partial |
| 41 | Lost acknowledgement recovery | Missing | Missing | Status query support | Safety | Missing |
| 42 | Command state machine | Pending only | No durable state | Audit events | Commands | Missing |
| 43 | Accepted state | Missing | Missing | N/A | Commands | Missing |
| 44 | Running state | Missing | Missing | N/A | Commands | Missing |
| 45 | Succeeded state | Rollout-specific | Ack only | ControlReply | Commands | Partial |
| 46 | Failed state | Rollout-specific | Ack only | ControlReply | Commands | Partial |
| 47 | Expired state | Implemented | Missing | Deadline support | Commands | Partial |
| 48 | Operation discovery | Missing | Missing | Catalog APIs | Commands | Missing |
| 49 | Catalog revision | Missing | Missing | Catalog source | Compatibility | Missing |
| 50 | Catalog hash | Missing | Missing | Descriptor support | Compatibility | Missing |
| 51 | Generic daemon operation | Typed HQ enum | Typed agent enum | Generic relay | Commands | Missing |
| 52 | Backup operation | Queued | Unsupported handler | Backup spoke | Backup | Partial |
| 53 | Restore operation | Queued | Unsupported handler | Backup spoke | Backup | Partial |
| 54 | Factory reset | API/policy partial | Unsupported handler | Reset RPC | Safety | Partial |
| 55 | OTA update | Rollout persistence | Unsupported handler | OTA spoke | OTA | Partial |
| 56 | Restart daemon | Missing | Missing | SpokeControl RPC | Operations | Missing |
| 57 | Network mutation | Missing | Local route exists | Network spoke | Operations | Partial |
| 58 | Firewall mutation | Missing | Local route partial | Network spoke | Operations | Partial |
| 59 | Log collection command | Missing | Local logs | Logmanager | Operations | Partial |
| 60 | Alarm acknowledgement | HQ alarms | Local alarm API | Alarm ownership | Alarms | Partial |
| 61 | Alarm raise | Ingest only | Missing | ApplicationApi | Alarms | Partial |
| 62 | Alarm clear | Ingest only | Missing | ApplicationApi | Alarms | Partial |
| 63 | Fault ingest | Implemented | Missing | Faults spoke | Faults | Partial |
| 64 | Fault clear | Implemented | Missing | Faults spoke | Faults | Partial |
| 65 | Telemetry ingest | Implemented | Queue exists | Telemetry spoke | Telemetry | Partial |
| 66 | Telemetry backpressure | Missing | Queue support | Spool policy | Telemetry | Partial |
| 67 | Telemetry offline spool | Missing | Queue exists | Spool classes | Telemetry | Partial |
| 68 | Location telemetry | Implemented | Missing | Telemetry source | Telemetry | Partial |
| 69 | Log upload URL | Implemented | Request support | Bundle support | Logs | Partial |
| 70 | Log upload completion | Missing | Missing | Bundle metadata | Logs | Missing |
| 71 | Per-daemon log files | Missing | Missing | Logmanager store | Logs | Partial |
| 72 | Journal collection | Missing | Missing | Platform API exists | Logs | Partial |
| 73 | Application log sink | Missing | Missing | Application API | Logs | Partial |
| 74 | Log rotation | N/A | N/A | Store rotation | Logs | Implemented |
| 75 | Log retention budget | Missing | Missing | Budget RPC | Logs | Partial |
| 76 | Secret redaction | Partial | Partial | Redaction helpers | Security | Partial |
| 77 | Secret-free heartbeat | Partial | Partial | Contract comments | Security | Partial |
| 78 | Secret-free audit | Partial | Audit log | Audit policy | Security | Partial |
| 79 | TLS/channel authentication | Gateway deployment | Agent currently plaintext | WireGuard/HQ peer | Security | Partial |
| 80 | Local UDS authentication | N/A | Missing client | Socket permissions | Security | Partial |
| 81 | Application UDS group policy | N/A | N/A | sysusers/socket policy | Security | Partial |
| 82 | Tenant/device authorization | Implemented | Local authorization | Final device policy | Security | Partial |
| 83 | Operation classification | Missing | Missing | Partial policy | Safety | Missing |
| 84 | Local-only operation denial | Missing | Missing | Policy hooks | Safety | Missing |
| 85 | Manufacturing-only denial | Missing | Missing | Reset gate | Safety | Partial |
| 86 | Confirmation enforcement | API partial | UI confirmation | Reset confirmation | Safety | Partial |
| 87 | JSON argument validation | Missing | Missing | Control validation | Safety | Missing |
| 88 | Argument size limits | Missing | Missing | Transport limits | Safety | Missing |
| 89 | Result payload size limits | Missing | Missing | Control reply | Safety | Missing |
| 90 | Stable machine error codes | Partial | Missing | Partial | Errors | Missing |
| 91 | UNAUTHENTICATED mapping | Gateway | Agent error | Peer error | Errors | Partial |
| 92 | PERMISSION_DENIED mapping | API auth | Missing | Policy | Errors | Missing |
| 93 | INVALID_ARGUMENT mapping | API validation | Missing | Validation | Errors | Missing |
| 94 | NOT_FOUND mapping | API | Missing | Catalog | Errors | Partial |
| 95 | UNAVAILABLE mapping | Gateway | Missing | Peer | Errors | Missing |
| 96 | DEADLINE_EXCEEDED mapping | Implemented | Missing | Control deadline | Errors | Partial |
| 97 | ALREADY_EXISTS mapping | Missing | Cache only | Request IDs | Errors | Missing |
| 98 | FAILED_PRECONDITION mapping | Missing | Missing | Policy/state | Errors | Missing |
| 99 | ABORTED mapping | Missing | Missing | Cancellation | Errors | Missing |
| 100 | INTERNAL mapping | Partial | Partial | Partial | Errors | Partial |
| 101 | Local configuration read | N/A | Existing engine | GetConfig RPC | Configuration | Partial |
| 102 | Local configuration write | N/A | Existing engine | SetConfig RPC | Configuration | Partial |
| 103 | HQ configuration read | Missing | Missing | GetConfig RPC | Configuration | Missing |
| 104 | HQ configuration write | Missing | Missing | Signed config path | Configuration | Missing |
| 105 | Configuration origin tracking | N/A | Engine support | `/pst` owner | Configuration | Partial |
| 106 | HQ signed configuration | Missing | Missing | Signature support | Configuration | Missing |
| 107 | Local-over-HQ precedence | Missing | Engine policy | Origin policy | Configuration | Partial |
| 108 | Configuration schema negotiation | Missing | Schema exists | Schema version | Compatibility | Partial |
| 109 | Configuration audit timeline | Missing | Audit log | Timeline | Audit | Partial |
| 110 | HQ audit correlation | API audit | Local audit | Device audit | Audit | Missing |
| 111 | Device status REST API | Implemented | Missing | Status source | Fleet UI | Partial |
| 112 | Device health REST API | Missing | Missing | Health score | Fleet UI | Missing |
| 113 | Command status REST API | Implemented | Missing | Result source | Fleet UI | Partial |
| 114 | Operation catalog REST API | Missing | Missing | Catalog source | Fleet UI | Missing |
| 115 | Live device status UI | Partial | Local UI partial | N/A | Fleet UI | Partial |
| 116 | Local HQ/istate status UI | N/A | Missing | Status source | Local UI | Missing |
| 117 | WebSocket status fanout | Partial | SSE/local events | Timeline | Fleet UI | Partial |
| 118 | Health score factors | Missing | Missing | Implemented | Fleet UI | Partial |
| 119 | Stale session sweeper | Implemented | Reconnect | Peer sweep | Presence | Partial |
| 120 | Online transition event | Device flag | Missing | Timeline support | Presence | Missing |
| 121 | Offline transition event | Device flag | Missing | Timeline support | Presence | Missing |
| 122 | Revocation terminates stream | Implemented | Revoked state | HqPeer support | Security | Partial |
| 123 | Reissued credential reconnect | Implemented | Pairing flow | Enrollment support | Security | Partial |
| 124 | Watchdog observe-only mode | N/A | N/A | Service/tool exists | Reliability | Partial |
| 125 | Watchdog stalled-istate detection | N/A | Missing status consumer | Watchdog | Reliability | Partial |
| 126 | Watchdog never reboots | N/A | N/A | Service policy | Reliability | Implemented |
| 127 | Abnormal spoke crash forensics | N/A | N/A | Orchestrator/forensics | Reliability | Partial |
| 128 | CAN control | N/A | N/A | Hardware descriptors/control | Hardware | Partial |
| 129 | GPIO/LED/USB/display control | N/A | N/A | Hardware daemon | Hardware | Implemented |
| 130 | Device descriptor validation | N/A | Board manifest | Descriptor loader | Hardware | Implemented |
| 131 | Application metric registration | N/A | Missing consumer | ApplicationApi | Telemetry | Partial |
| 132 | Application metric reporting | N/A | Missing consumer | ApplicationApi | Telemetry | Partial |
| 133 | Application config origin | N/A | Missing consumer | ApplicationApi | Configuration | Partial |
| 134 | Peer socket permissions | N/A | Missing client | System files | Security | Partial |
| 135 | Systemd ordering | Deployment | Deployment | Units exist | Deployment | Partial |
| 136 | Package version pinning | Image config | Crate version | VERSION | Deployment | Missing |
| 137 | Matching proto artifacts | Generated | Generated | Generated | Deployment | Missing |
| 138 | In-memory gateway/device harness | Partial tests | Partial tests | C++ tests | Testing | Missing |
| 139 | Pairing/reconnect integration test | Gateway tests | Agent tests | N/A | Testing | Partial |
| 140 | HQ outage integration test | Missing | Missing | Missing | Testing | Missing |
| 141 | istate outage integration test | Missing | Missing | Local tests | Testing | Missing |
| 142 | Operation discovery integration test | Missing | Missing | Catalog tests | Testing | Missing |
| 143 | Duplicate command integration test | Missing | Unit cache | Control tests | Testing | Partial |
| 144 | Revocation integration test | Gateway tests | Agent tests | N/A | Testing | Partial |
| 145 | Secret-leak integration test | Partial | Partial | Existing tests | Testing | Partial |
| 146 | Tenant isolation integration test | API tests | N/A | N/A | Testing | Implemented |
| 147 | Reboot/boot-ID integration test | Missing | Missing | Missing | Testing | Missing |
| 148 | Fault/telemetry while command runs | Partial | Missing | Spoke streams | Testing | Missing |
| 149 | Three-repo release gate | Separate gates | Separate gate | Separate gate | Release | Missing |
| 150 | Rollback compatibility window | Missing | Missing | Missing | Release | Missing |

The matrix is deliberately evidence-oriented. A feature is not complete merely
because a message, route, or unit test exists: the owner, transport, policy,
durability, and end-to-end test must all agree.
