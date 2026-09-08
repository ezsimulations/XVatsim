# Performance Contract Gate A — Implementation and Offline Proof

Date: 2026-09-01

Status: formally closed after offline proof, hardening amendments, controlled deployment, and controlled live acceptance. Gate B has not begun.

## Outcome

The flight-loop authority call is now submission/harvest only. Authority evaluation runs on one persistent, below-normal-priority worker with one running request and at most one replaceable pending request. The worker never receives the existing `RouteSectorResolver`; it owns a separate `AuthorityRelevanceEngine` and consumes a shared immutable authority dataset compiled on the source-fetch path.

Changed-input publication fails closed. Routine same-input rechecks retain the last accepted fact while the worker runs. Completion publication is controlled by the Brain and requires an exact match for request ID, lifecycle epoch, plan identity, full waypoint-and-sector route digest, controller digest, transceiver digest, and immutable dataset identity. Completion age and aircraft displacement are checked separately.

Worker requests own all inputs. Controller parsing now publishes an immutable shared controller vector and content digest from the existing VATSIM fetch thread. Route and transceiver evidence are likewise carried into the mailbox as immutable shared objects, so the live dispatch does not hash or copy their vectors. Downstream Brain relevance carries the same immutable authority evidence rather than deep-copying it.

The hardening amendment makes the nonblocking claim structural:

- Request, completion, and retirement mailboxes use lock-free pointer atomics (enforced by compile-time assertions); no worker mutex is acquired by submission, harvest, cancellation, status, or dataset reads.
- The worker is started during plugin startup, before flight-loop registration, so submission never creates or joins a thread.
- The resolver publishes dataset pointer, identity, and terminal generation as one immutable atomic publication. Old datasets remain valid through worker teardown.
- Completed snapshots are wrapped in worker-retained leases. Main-thread invalidation only sets an atomic retired bit and releases a non-final reference; the worker performs the final evidence-ledger retirement.
- Superseded request and completion packages are also reclaimed by the worker, not by the flight loop.

## Offline proof

Release targets built successfully:

- `XVatsimPlugin`
- `XVatsimRegressionHarness`

Full frozen-baseline regression:

- 861 scenarios passed
- 0 failed
- 33.539 seconds

Exact synchronous-oracle parity:

- 64 route-sector/authority scenarios passed
- 0 failed
- Each scenario compared a comprehensive digest of every authority snapshot field, evidence ledger, compatibility projection, and live Brain projection.

Deterministic Gate A probe:

- Mailbox submission P99: 4 microseconds
- Complete package-and-submit P99: 3 microseconds with the parser maximum of 5,000 controllers
- Harvest P99: 6 microseconds
- Maximum pending depth: 1
- Starts: 518
- Replaceable pending requests observed: 510
- Completed requests: 5
- Cooperative cancellations: 3
- Worker-thread snapshot retirements observed: 3
- Nonblocking lifecycle cancellation: 1 microsecond
- Forced delays: 500 ms and 2 seconds
- Stale publications: 0

The probe also proves waypoint-sensitive route identity, comprehensive transceiver identity, every stale-completion identity gate, maximum completion age, geographic displacement rejection, exact worker/synchronous parity, latest-only replacement, cancellation, final thread join, and worker-thread retirement of obsolete evidence. Its complete timing window begins before identity assembly and request construction and includes the atomic dataset publication read and mailbox exchange.

Static worker-path scan found zero mutex/condition-variable operations and zero references to X-Plane APIs/debug output, WinHTTP, file streams/filesystem, settings, `RouteSectorResolver`, or process-static cache access. The plugin contains zero synchronous calls to `ResolveBrainScheduledAuthorityVerification`.

## Acceptance status

Gate A and its Calm 1/2 hardening amendments are formally closed. Offline proof established immutable owned inputs, a separate worker engine, latest-only queueing, cooperative cancellation, shared immutable completions, Brain-owned validation/publication, exact parity, deterministic delay behavior, lifecycle cancellation/join, and nonblocking submission/harvest.

The combined controlled live proof then established:

- 2,415 contiguous complete-callback timing records with zero timing-lane loss;
- complete callback P99 of 2.191 milliseconds;
- authority main-thread work no greater than 19 microseconds;
- 15 authority requests, 15 exact-identity publications, and zero stale publications;
- unchanged five-second transceiver observations and controller generation-only observations suppressed without authority dispatch;
- clean Plugin Admin disable/re-enable, xPilot disconnect, diagnostics drainage, worker join, and normal X-Plane shutdown.

The deployed candidate and full live receipt are documented in `outputs/performance_contract_gate_a_calm_2_20260901_112700/GATE_A_CALM_1_TIMING_LANE_AND_GATE_A_CALM_2_OFFLINE_PROOF.md`.

## Gate boundary

Gate B route-rebuild containment is unchanged. The closure flight measured 1.096 seconds of initial expanded-FMS route construction and 59.6 milliseconds on Plugin Admin re-enable. Authority computation did not contribute to either spike. No route cadence, cache lifetime, callback interval, or priority workaround was introduced.
