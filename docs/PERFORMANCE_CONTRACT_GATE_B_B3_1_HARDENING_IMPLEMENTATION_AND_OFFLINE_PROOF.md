# Performance Contract Gate B B3.1 — hardening implementation and offline proof

Date: 2026-09-01  
Branch: `v2-development`  
Baseline HEAD: `9440d6245f63e52b47de5fba99c6041ecfdcc31e`  
Status: **B3.1 passes offline; Phase B4 remains held**

## Outcome

The five independent-review blockers are corrected without reopening Gate A, Product-Calm 1, or changing route fidelity. The production flight loop now honors Brain `needsWorker`, observes late expanded-FMS changes only on the route worker, uses a sticky fallback anchor, cancels by monotonically increasing mailbox sequence, and treats source freshness and exact publication state as part of route identity.

Release build, the complete focused-probe chain, frozen regression, route-oracle parity, combined authority/route parity, cancellation, lifecycle, source-recovery, and retirement accounting all pass. No candidate was deployed and no commit was created.

## B3.1 corrections

### Bounded unresolved/stale retry

`RefreshBrainRoutePolygonSnapshot` now calls the Brain refresh decision for an accepted current route and passes `runtimeOutput.needsWorker` into the production route coordinator. An unresolved or stale accepted route is fail-closed and resubmitted only when the existing bounded retry deadline is due. It cannot remain permanently accepted without another worker attempt.

### Worker-owned late expanded-FMS observation

The route worker supports a distinct `ObserveExpandedFms` request. It performs the existing exact directory selection, plan matching, modification-time ordering, and filename tie break through the injected worker source. The result is a deterministic semantic observation identity for either the selected matching file or no match.

The coordinator requires one initial observation before route preparation and schedules quiet worker-only observations at a 15-second interval thereafter. An unchanged observation retains the accepted route and produces no route rebuild. A changed observation becomes part of the desired identity, retires the obsolete accepted route, and produces exactly one latest-only route preparation request. No simulator-thread filesystem operation was introduced.

### Sticky fallback anchor

When departure coordinates are unavailable, the first valid aircraft anchor is captured per network-plan digest. Ordinary taxi or airborne movement does not change the route identity. A material plan change resets the capture, and an initially invalid aircraft state may recover once when the first valid position appears.

### Sequence-based cancellation

Route cancellation no longer depends on a shared Boolean that the worker can clear after a newer submission. Each request owns its mailbox sequence and every cooperative checkpoint compares it with the atomic latest sequence. The deterministic start-race seam submits a replacement after the worker marks the old request running but before route work begins; the old request is cancelled and the exact replacement completes.

### Exact source publication and freshness

One coherent atomic route publication contains the immutable dataset pointer, content identity, exact publication generation, availability, and freshness. A failed center refresh publishes stale state immediately without discarding the last compiled dataset. Exact recovery publishes a fresh identity. Wholly identical successful refreshes do not churn the route publication or center/authority content generations. A real center/authority replacement preserves the frozen coherent-package terminal-generation contract.

### Complete live-path timing and accounting

The production submit clock begins before preflight selection, source harvest/publication read, network-plan hashing, anchor selection, observation identity selection, request allocation, and mailbox exchange. Diagnostics report `completeDesiredPackageSubmitUs` separately from `mailboxExchangeUs`. Shutdown accounting now includes FMS observations/changes, observation dispatch and acceptance, unresolved retry dispatches, complete submit maximum, mailbox maximum, and worker observation counters. Exact stale rejections have their own event with full semantic identity.

## Production coordinator and hardening proof

The focused Gate B probe invokes the same production decision, anchor, identity, Brain validation, route engine, and worker functions used by the plugin. It proves:

- initial observation before first route preparation;
- unresolved accepted-route `needsWorker` retry;
- quiet periodic observation without route invalidation;
- late matching FMS appearance and newer-file replacement;
- unchanged FMS observation identity;
- sticky moving-aircraft fallback and valid-position recovery;
- same-content source quietness, failed-refresh staleness, and exact recovery;
- the lost-cancellation interleaving using a deterministic worker-start race seam;
- 1,000 lifecycle/coordinator boundaries with 1,000 stale-completion rejections and 1,000 safe decisions;
- 10,000 rapid semantic replacements with one running and at most one pending request; and
- exact request, fact, lease, route, source-dataset, and worker-thread retirement.

## Final offline results

| Proof | Result |
|---|---:|
| Release plugin and harness | pass |
| Frozen regression | 861/861 |
| Exact route-oracle parity | 64/64 |
| Combined authority/route parity | 64/64 |
| Gate A probe | pass |
| Gate A-Calm 1 probe | pass |
| Gate A-Calm 2 probe | pass |
| Product-Calm 1 probe | pass |
| Gate B B3.1 probe | pass |
| `git diff --check` | pass |

Final Gate B focused metrics:

- complete package-and-submit P99 / max: `10 / 61 us`;
- harvest, validation, and retirement P99 / max: `3 / 23 us`;
- unchanged 512-waypoint transition P99 / max: `16 / 151 us`;
- maximum pending depth: `1`;
- request-node peak: `4` startup-preallocated nodes;
- rapid changes: `9` accepted, `9,991` rejected nonblockingly, `9 us` maximum submit;
- route leases created / retired / destroyed on worker: `104 / 104 / 104`;
- outstanding leases at idle: `0`;
- source observations / rejected observations: `11 / 2`;
- retained source datasets at idle / peak: `1 / 5` (bound `6`);
- lifecycle stale rejections / safe coordinator decisions: `1,000 / 1,000`;
- injected source calls: `10` directory lists, `31` FMS loads, `1` strict preflight validation; and
- stale publications: `0`.

Timing values vary slightly between runs. Executable assertions enforce the contract ceilings of 250 us P99 and 1 ms maximum for submit, harvest, and unchanged transition.

## Static worker-path proof

The worker path is:

1. `RoutePreparationWorker::Implementation::Run`
2. `RoutePreparationEngine::Prepare` or `ObserveExpandedFms`
3. worker-owned route state and exact route/FMS algorithms
4. injected `RoutePreparationSource`
5. immutable completion mailbox

The dedicated worker translation unit has no reference to the shared resolver, `RouteSectorResolver::Resolve`, XPLM/dynamic XPLM lookup, WinHTTP/network, UI/overlay, settings, direct diagnostics-file output, or mutex locks. The production plugin has no `gRouteSectorResolver.Resolve` call. Worker filesystem activity is confined to `ReadTextFile`, `ListRegularFiles`, `LoadFmsPlan`, and `ValidatePreflightCandidate` on the injected source interface.

## Source audit

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h` and `brain/src/BrainControllerRelevanceWorker.cpp`: retain the accepted immutable route through downstream authority consumption.
- `brain/include/XVatsim/brain/BrainOwnedWorkerTypes.h` and `brain/src/BrainRoutePolygonWorker.cpp`: Brain route refresh state, `needsWorker`, full completion identity including FMS observation, exact validation, and shared immutable publication.
- `brain/include/XVatsim/brain/RoutePolygonTransition.h` and `brain/src/RoutePolygonTransition.cpp`: unchanged shared-route fast path and explicit retirement on real transition.
- `modules/route_sector/include/XVatsim/modules/route_sector/RouteSectorResolver.h`: immutable source publication, worker request/fact/lease, observation, cancellation-token, sticky-anchor, and coordinator contracts.
- `modules/route_sector/src/RouteSectorResolver.cpp`: immutable compilation, worker-owned route engine, exact FMS observation, anchor/coordinator decisions, freshness/publication semantics, and harness-only synchronous oracle.
- `modules/route_sector/src/RoutePreparationWorker.cpp`: persistent below-normal-priority latest-only worker, sequence cancellation, lock-free mailboxes, and worker-side reclamation.
- `modules/route_sector/CMakeLists.txt`: compiles the dedicated worker translation unit.
- `plugin/src/XVatsimPlugin.cpp`: Brain-controlled nonblocking coordination, full-path timing, fail-closed invalidation, lifecycle order, and diagnostics.
- `tools/regression_harness/CMakeLists.txt`, `tools/regression_harness/src/main.cpp`, and `PerformanceContractGateBProbe.*`: focused probe registration, exact oracle/combined parity, and B3.1 deterministic production-coordinator fixtures.

No UI, flight-loop cadence, existing authority/source cadence, authority algorithm, controller/transceiver identity, network behavior, or route fidelity was changed. The only new cadence is the below-normal-priority worker-side FMS observation described above.

## Artifacts and hashes

- B3.1 plugin candidate: `outputs/performance_contract_gate_b_b3_1_20260901_152309/01_offline/GateB_B3_1_candidate_XVatsim.xpl`
  - bytes: `2,742,784`
  - SHA-256: `1EB42038D440721E7DEB906C95031DFE270C78DE1BACC4376800BF63721BB4C0`
- B3.1 harness: `outputs/performance_contract_gate_b_b3_1_20260901_152309/01_offline/XVatsimRegressionHarness.exe`
  - bytes: `4,389,376`
  - SHA-256: `57579858F360E4A22E894C8D2675FE675C3EA2A93DC317ED9CAC63E76A55F8F8`
- Preserved pre-B3.1 Gate B candidate:
  - SHA-256: `773B74217B2E9534DD502172F9A39F8E912F28DC55372C7C2D56DF30B668B5AF`
- Active Product-Calm plugin, unchanged:
  - bytes: `2,689,024`
  - SHA-256: `3D8E35CBD3B0FAA96CEB24D5422BD73ECB928F8A7F9B7417249FA994E4CD4E70`

Raw proof summaries, binaries, source and binary manifests, prechange dirty-tree receipt, and deployment hold are under `outputs/performance_contract_gate_b_b3_1_20260901_152309/`.

## Remaining boundary

B3.1 is an offline pass. It is ready for independent review, but Phase B4 is not deployed or authorized by this result. Controlled backup, deployment, startup smoke testing, initial and re-enable route construction, connected live evidence, normal shutdown, and rollback remain a separate explicit gate.

The 15-second worker-side FMS observation performs exact directory/file selection without rebuilding an unchanged route. Its wall time and background CPU impact still require confirmation in the controlled live phase; neither can stall the flight loop, but offline proof is not a substitute for that operational measurement.

No active X-Plane plugin was overwritten. No commit was created. Existing unrelated dirty and untracked repository content was preserved.
