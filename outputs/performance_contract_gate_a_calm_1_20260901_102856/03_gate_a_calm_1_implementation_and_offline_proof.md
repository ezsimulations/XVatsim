# Performance Contract Gate A-Calm 1 — Implementation and Offline Proof

Date: 2026-09-01 (America/Los_Angeles)

Status: implemented, independently offline-proven, and hash-verified into the active controlled-test payload. Startup smoke and controlled live-flight acceptance remain pending. No commit was created.

## Outcome

Gate A-Calm 1 is isolated from Gate A-Calm 2, Product-Calm 1, and Gate B.

The X-Plane callback no longer performs diagnostic directory creation, retention scans, file opening, file writing, flushing, or closing. Those operations now belong to one persistent, below-normal-priority diagnostics writer. The producer uses a bounded two-lane queue and `try_lock`; it never waits for the writer mutex. The writer releases the queue mutex before deferred formatting, artificial delay, retention, or storage access.

Large diagnostic formatting was moved off the simulator thread for:

- authority terminal, disposition, and dispatch records;
- authority-proof/slow-refresh/summary records and diagnostic-job expansion;
- radio candidate-difference and candidate-completion traces;
- accessory input and publication records;
- the large accessory performance report.

Routine capacity is 960 records and the independently reserved critical capacity is 64 records. Queue contention, capacity loss, formatting failure, storage failure, maximum depth, producer maximum, worker identity, and priority establishment are observable. Storage or formatting failures discard diagnostics only; they cannot change Brain, authority, controller, radio, overlay, or X-Plane state.

## Truthful callback measurement

Every registered flight-loop callback path now starts timing at function entry, including the accessory-active fast path. The emitted `event=flight-loop-complete` record contains:

- `completeUs`: all callback work through cadence selection and all normal diagnostic submissions;
- `refreshUs`: the original Brain/overlay refresh scope;
- `diagnosticsSubmissionUs`: the main-thread diagnostic decision/package/submit scope;
- `accessoryPath` and `activeCadence` classifications.

The declared boundary is `immediately-before-telemetry-publish`. Therefore the only excluded work is the final fixed-size, nonblocking timing-record publication itself. That publication uses the same bounded producer and is independently covered by its producer maximum counter.

## Preserved rollback state

Before implementation, the exact live-proven Gate A state was captured under:

`outputs/performance_contract_gate_a_calm_1_20260901_102856/00_prechange_gate_a_checkpoint`

The checkpoint contains:

- Git HEAD `60da436ae31a2a83dddeea4ccb5c9459a93f36af` and branch `v2-development`;
- hashes for 1,571 source files;
- copies of all 18 Gate A modified/untracked implementation files;
- the complete tracked Gate A binary patch;
- the repository candidate, regression harness, and active three-file payload;
- a complete checkpoint artifact manifest.

The predeployment/live-proven Gate A plugin is:

- size: 2,622,976 bytes;
- SHA-256: `790ED5EF437FC26A5AEB8FEFE6B211DAF192AE1ACF7E2733530C633FE5E4BA86`.

The immediate deployment rollback copy is:

`outputs/performance_contract_gate_a_calm_1_20260901_102856/02_controlled_deployment/predeployment_active/win_x64/XVatsim.xpl`

## Offline proof

### Build

Release builds passed for:

- `XVatsimPlugin`;
- `XVatsimRegressionHarness`.

### Gate A-Calm 1 deterministic fault probe

Final artifact run:

- forced writer/storage delay: 2 seconds;
- producer submissions during delay: 5,000;
- submission P99: 0 microseconds;
- submission maximum: 1 microsecond;
- complete 5,000-call burst: 462 microseconds;
- bounded routine drops under deliberate saturation: 4,996;
- maximum total queue depth: 5 of 6 configured test slots;
- critical record accepted while the routine lane was full: yes;
- forced unavailable storage producer P99: 0 microseconds;
- forced storage failures observed: 594;
- unavailable-storage filesystem path created: no;
- 16,384-byte deferred formatter ran on the writer thread: yes;
- deterministic formatter exception isolated: yes;
- writer continued after formatter exception: yes;
- below-normal worker priority established: yes;
- clean joins: yes.

Result: `PERFORMANCE_CONTRACT_GATE_A_CALM_1_PASSED`.

### Original Gate A deterministic probe

- submission P99: 4 microseconds;
- complete 5,000-controller package-and-submit P99: 4 microseconds;
- harvest P99: 1 microsecond;
- maximum pending depth: 1;
- worker-thread evidence retirements: 3;
- forced authority delays: 500 ms and 2 seconds;
- stale publications: 0.

Result: `PERFORMANCE_CONTRACT_GATE_A_PASSED`.

### Exact parity and frozen regression

- exact synchronous/worker authority parity: 64/64 passed in 7.660 seconds;
- frozen complete regression: 861/861 passed in 33.935 seconds.

## Static and scope audit

- `git diff --check`: pass;
- diagnostic filesystem owner: only `AsyncDiagnosticsWriter.cpp`;
- diagnostic filesystem calls in `XVatsimPlugin.cpp`: zero;
- X-Plane, UI, WinHTTP, settings, and route-resolver references in the writer: zero;
- protected Gate A engine/feed files checked against the checkpoint: 12;
- protected Gate A engine/feed files changed by this subgate: zero;
- Gate A-Calm 2 semantic identity changes: zero;
- Product-Calm 1 FMS changes: zero;
- preallocated authority request-node changes: zero;
- coherent evidence-bundle changes: zero;
- Gate B route changes: zero.

The eight-file Gate A-Calm 1 source manifest and binary manifest are retained in `01_offline_proof`.

## Controlled deployment identity

X-Plane and xPilot were confirmed stopped before deployment.

- candidate/deployed size: 2,658,304 bytes;
- candidate/deployed SHA-256: `254A5AA8E2FFB9BD8E2DF17696AE89A8A146304C0E113C84DF1AD74970C6D538`;
- active path: `C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl`;
- deployment hash verification: pass;
- X-Plane launch: not performed;
- startup smoke: pending;
- controlled live flight: pending.

## Live acceptance boundary for this subgate

Gate A-Calm 1 live success requires:

- complete established-flight callback P99 at or below 3 ms;
- no established callback above 10 ms after the permitted bootstrap;
- diagnostic submission remains bounded and storage-independent;
- normal storage produces zero queue drops, formatting failures, and storage failures;
- authority main-thread P99 below 250 microseconds after removing synchronous diagnostic I/O/formatting;
- zero stale authority publications;
- clean xPilot disconnect, plugin shutdown, and diagnostics-writer join.

The existing five-second semantic transceiver/controller churn is intentionally unchanged. It is Gate A-Calm 2 evidence, not a Gate A-Calm 1 regression. The initial 1.169-second route build remains Gate B evidence.

## Required next action

Run one controlled live flight with the deployed hash. Capture a settled ground window after flight-plan bootstrap, a moving/enroute window, at least one controller or advisory transition if practical, normal xPilot disconnect, and normal X-Plane shutdown. Do not commit or begin Gate A-Calm 2, Product-Calm 1, or Gate B until that evidence is reviewed.
