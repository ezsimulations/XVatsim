# Gate A-Calm 1 Timing-Lane Amendment and Gate A-Calm 2 Offline Proof

Date: 2026-09-01 (America/Los_Angeles)

Status: formally closed after implementation, offline proof, controlled deployment, startup smoke, independent review, and combined controlled live acceptance. This report belongs to the scoped closure checkpoint. Product-Calm 1 and Gate B remain unopened.

## Preserved entry state

The Gate A-Calm 1 live-proven source/binary checkpoint is retained at:

`outputs/performance_contract_gate_a_calm_2_20260901_112700/00_prechange_gate_a_calm_1_live_pass`

The checkpoint records Git HEAD `60da436ae31a2a83dddeea4ccb5c9459a93f36af`, the prechange source manifest, working-tree status, and the deployed Gate A-Calm 1 binary.

## Timing-lane amendment

Flight-loop completion timing now uses a dedicated fixed-capacity SPSC ring:

- one producer: the X-Plane flight-loop callback;
- one consumer: the diagnostics writer;
- all ring storage is allocated during writer startup;
- producer indexes are compile-time required to be lock-free;
- the producer performs no mutex acquisition, heap allocation, waiting, filesystem access, or formatting;
- timing-lane saturation has its own `droppedFlightLoopTimingFull` counter;
- routine and critical mailbox contention have independent counters;
- ordinary diagnostic records retain their bounded, nonblocking drop behavior;
- writer shutdown drains accepted timing records before join.

The dedicated proof exercised forced routine/critical queue contention, a two-second writer delay, timing-lane saturation, disable/re-enable simulation, shutdown drainage, post-stop rejection, and writer restart.

Final focused result:

`PERFORMANCE_CONTRACT_GATE_A_CALM_1_PASSED`

- 5,000 timing records accepted during the two-second writer delay;
- timing maximum depth: 5,000;
- timing contention losses: zero;
- deliberate routine contention drops: 7;
- deliberate critical contention drops: 7;
- saturation fixture: 8 written and 24 timing-full drops, exactly matching capacity;
- lifecycle fixture: accepted records drained across stop/restart and post-stop submission rejected;
- complete 5,000-call timing burst: 445 microseconds;
- slow-fixture timing submission P99: 0 microseconds, maximum 5 microseconds.

## Gate A-Calm 2 semantic authority inputs

Controller identity and the controller worker input are now compiled on the VATSIM fetch thread from one canonical authority-only projection. It includes callsign, frequency, facility, actionable state, ATIS state, and ownership-bearing controller text. It excludes feed generation, aggregate controller count, visual range, presentation status, and input ordering. The projection is canonically sorted before hashing and worker submission.

Radio resolution now publishes a separate immutable authority projection. Authority identity and worker input include availability, stale state, candidate membership, callsign, frequency, and station geometry. Aircraft distance and score are absent from the projection, not merely excluded from its digest. Feed/cache age, fetch state, parser counters, source-health text, resolver presentation text, and other radio diagnostics are also absent. The projection is canonically sorted before hashing and worker submission.

When a five-second radio refresh has the same semantic identity, the Brain-owned runtime retains the existing immutable authority object while still updating the full radio snapshot. A real semantic change replaces the projection immediately. The plugin compares only the semantic controller and transceiver digests alongside lifecycle, plan, route, and dataset identity. Controller generation and radio observation generations remain diagnostic counters, not invalidation inputs.

Final focused result:

`PERFORMANCE_CONTRACT_GATE_A_CALM_2_PASSED`

- twelve simulated unchanged five-second refreshes: zero changed-input requests;
- controller generation-only change: stable;
- controller ordering and visual-range-only change: stable;
- volatile radio diagnostics, aircraft distance, and score-only change: stable;
- membership, callsign, frequency, station latitude/longitude, availability, and stale-state changes: all invalidate;
- unchanged radio refresh retained the same immutable authority projection;
- real geometry change replaced it;
- five fresh-engine cold-start variants passed: base, score-only, visual-range-only, controller-order, and transceiver-order;
- every variant produced the same full authority snapshot from the synchronous and worker engines;
- every variant produced the same relevant-authority rows and downstream controller-display decisions;
- the determinism fixture produced a real transceiver-geometry center authority and displayed center row.

The synchronous oracle now canonicalizes through the same projection/expansion path as the worker. Consequently, equal semantic identities cannot reach the authority engine with different controller ordering, transceiver ordering, visual range, distance, or score.

One existing scenario expectation was intentionally updated to reflect the approved diagnostic consequence: authority transceiver proof scores are now canonical zero values. The scenario still proves the same relevant `BEST_CTR` authority and enroute row. No visible authority decision changed.

## Compatibility and performance proof

Release builds passed for `XVatsimPlugin` and `XVatsimRegressionHarness`.

- Complete regression: `861/861`, 0 failures, 33.469 seconds.
- Exact synchronous/worker authority parity: `64/64`, 0 failures, 7.564 seconds.
- Original Gate A deterministic proof: pass.
- Complete 5,000-controller package-and-submit P99: 4 microseconds.
- Authority submission P99: 3 microseconds.
- Harvest P99: 1 microsecond.
- Worker pending depth: 1.
- Forced delays: 500 ms and 2 seconds.
- Stale publications: 0.
- Worker-thread evidence retirements: 3.
- `git diff --check`: pass (line-ending notices only).

## Candidate identity

Candidate path:

`build/performance-contract-gate-a-baseline/dist/XVatsim/win_x64/XVatsim.xpl`

- size: 2,688,000 bytes;
- SHA-256: `786FF6D42831457EF570E054B5ECD22D620CDF1BDCCC9A184A8C5C7D725CDA4E`.

## Controlled deployment

X-Plane and xPilot were verified stopped before deployment.

The previous active Gate A-Calm 1 binary was copied to:

`outputs/performance_contract_gate_a_calm_2_20260901_112700/01_controlled_deployment/predeployment_active/win_x64/XVatsim.xpl`

- rollback SHA-256: `254A5AA8E2FFB9BD8E2DF17696AE89A8A146304C0E113C84DF1AD74970C6D538`.

The final candidate was then deployed to:

`C:/X-Plane 12/Resources/plugins/XVatsim/win_x64/XVatsim.xpl`

- deployed size: 2,688,000 bytes;
- deployed SHA-256: `786FF6D42831457EF570E054B5ECD22D620CDF1BDCCC9A184A8C5C7D725CDA4E`;
- candidate/deployed hash equality: pass.

## Combined controlled live proof

The hash-verified candidate was exercised in X-Plane 12 with XVatsim and xPilot connected on the live VATSIM network. The observed flight context was `SWA1315`, `KPHX -> KSAC`, with an expanded-FMS route. The sequence covered startup, settled connected idle operation, Plugin Admin disable, Plugin Admin re-enable, xPilot network disconnect, and normal X-Plane shutdown.

### Complete callback timing

- complete timing sequence: `1..2415`, with zero sequence gaps;
- full-session callback P99: 2,191 microseconds;
- full-session callback P50/P95: 304/961 microseconds;
- settled pre-disable window after bootstrap (`719..1692`) P99: 2,227 microseconds;
- settled pre-disable maximum: 3,123 microseconds, with zero callbacks above 10 ms;
- post-resume window after the one route rebuild (`1698..2415`) P99: 2,191 microseconds;
- post-resume maximum: 2,780 microseconds, with zero callbacks above 3 ms;
- all calm callbacks excluding the two route rebuild callbacks: P99 2,184 microseconds and zero callbacks above 10 ms;
- diagnostic timing submission P99: 3 microseconds; maximum: 7 microseconds.

Two synchronous route events were intentionally excluded from the Gate A-Calm verdict and retained as Gate B evidence:

- initial bootstrap callback: 1,095,852 microseconds, of which 1,095,521 microseconds was `BrainRoutePolygonWorker` expanded-FMS route construction;
- Plugin Admin re-enable callback: 59,636 microseconds, including 53,640 microseconds of expanded-FMS route construction and 4,187 microseconds of network-plan work.

Authority work did not cause either callback spike. The first authority evaluation took 511,964 microseconds on the worker while main-thread dispatch took 5 microseconds. The re-enable authority evaluation took 422,759 microseconds on the worker while main-thread dispatch took 7 microseconds.

### Authority semantics and correctness

- authority requests/terminals/publications: `15/15/15`;
- stale, obsolete, rejected, or cancelled publications: zero;
- maximum pending depth: one;
- authority dispatch P99/maximum: 8/8 microseconds;
- observed authority-relevance main-thread stage P99/maximum: 19/19 microseconds;
- all 15 authority requests used one unchanged transceiver semantic digest;
- at least 98 unchanged transceiver observations were explicitly suppressed;
- at least 12 controller generation-only observations were explicitly suppressed;
- controller semantic content changed on the live global feed, producing 15 distinct controller digests and the corresponding valid dispatches;
- disable invalidated authority at lifecycle epoch 4 with no worker left running;
- re-enable resumed timing at sequence 1693 after sequence 1692, submitted fresh epoch-4 identities, and published only exact matches;
- xPilot disconnect removed route authority and authority-dependent controller display immediately; no disconnected authority request was submitted;
- normal shutdown advanced through lifecycle epochs 5 and 6 and joined cleanly.

### Diagnostics timing lane and writer shutdown

The final writer receipt was:

- timing submitted/dequeued/written: `2415/2415/2415`;
- timing-full drops: zero;
- timing submissions rejected while stopped: zero;
- all accepted records submitted/dequeued/written: `2609/2609/2609`;
- routine contention drops: 5, all counted and nonblocking;
- critical contention drops: zero;
- routine/critical capacity drops: zero;
- storage failures: zero;
- formatting failures: zero;
- maximum timing depth: 1;
- maximum ordinary queue depth: 3.

The `workerRunning=1` field in the stop receipt is the intentionally pre-stop snapshot written before `Stop()` drains and joins. The subsequent X-Plane log records `[XVatsim] Plugin stopped.`, and both X-Plane and xPilot exited normally.

Immutable evidence snapshots are stored under:

`outputs/performance_contract_gate_a_calm_2_20260901_112700/02_controlled_live_test`

- `xvatsim_diagnostics_2026_09_01_live_snapshot.log`: SHA-256 `8F5280C3D9A2C988EC2F8FF23365C99B7DEB320DBD72284856B7DA9D00798616`;
- `X-Plane_Log_live_snapshot.txt`: SHA-256 `09E40AADAD1D9AE72AE8CE0E1562C9D077F96351912C39D0F23B795B6A2A4C0E`.

## Live verdict and remaining boundary

Gate A-Calm 1 and Gate A-Calm 2 pass the combined controlled live proof. The timing lane made the complete-callback claim auditable with no loss, and semantic radio refreshes no longer create five-second authority churn. Authority evaluation remained off the flight loop, exact-identity publication held, disconnect failed closed, and shutdown drained cleanly.

Overall product calm is not yet complete because synchronous expanded-FMS route construction still exceeded 10 ms at initial bootstrap and re-enable. Those observations are isolated Gate B evidence. Product-Calm 1 and Gate B source were not changed during this gate.

The active X-Plane binary remains the controlled, hash-verified Gate A-Calm candidate documented above.

## Formal closure

Darron approved formal Gate A-Calm 1/2 closure after independent confirmation of the live timing sequence, callback percentile, authority main-thread bound, exact publications, zero stale publications, focused probes, and canonical semantic identities. The closure checkpoint is intentionally limited to the Gate A/Calm implementation, its focused probes, and directly relevant proof reports. Unrelated dirty source, historical evidence trees, binaries, rollback payloads, and other untracked documents are excluded.
