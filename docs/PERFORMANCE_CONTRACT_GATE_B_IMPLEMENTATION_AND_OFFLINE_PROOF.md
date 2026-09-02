# Performance Contract Gate B — implementation and offline proof

Date: 2026-09-01  
Branch: `v2-development`  
Status: **Phases B0-B3 pass offline; Phase B4 is not deployed or authorized**

## Outcome

Gate B route preparation has been removed from the production X-Plane flight-loop call graph. The plugin now submits immutable route inputs to a persistent, below-normal-priority, worker-owned route engine; harvests at most one immutable completion without waiting; validates the full semantic and lifecycle identity; and publishes or retires the result without copying its route vectors.

The frozen regression remains `861/861`, all `64/64` combined authority and route parity scenarios pass, all prerequisite focused probes pass, and the Gate B timing, latest-only, forced-delay, cancellation, failure-recovery, lifecycle, source-retirement, and ownership tests pass.

No Gate B binary was deployed. No Gate B commit was created.

## B0 protected checkpoint

- Gate A-Calm checkpoint: `db2265d17873b492def00311c3e19c27803e0b12`
- Product-Calm 1 Phase B0 checkpoint: `9440d6245f63e52b47de5fba99c6041ecfdcc31e`
- Phase B0 commit message: `perf: close Product-Calm 1 endpoint scan`
- Active Product-Calm plugin before and after Gate B work:
  - path: `C:/X-Plane 12/Resources/plugins/XVatsim/win_x64/XVatsim.xpl`
  - bytes: `2,689,024`
  - SHA-256: `3D8E35CBD3B0FAA96CEB24D5422BD73ECB928F8A7F9B7417249FA994E4CD4E70`
- Preserved pre-Product-Calm rollback:
  - bytes: `2,688,000`
  - SHA-256: `786FF6D42831457EF570E054B5ECD22D620CDF1BDCCC9A184A8C5C7D725CDA4E`
- The tracked tree was clean at the B0 boundary. Existing unrelated untracked files were recorded and left untouched.

The exact prechange receipt is in `outputs/performance_contract_gate_b_20260901_133922/00_prechange/`.

## Implemented architecture

### Immutable source publication

- Added a dedicated immutable `RouteSourceDataset` containing the exact compiled center traversal features and controller-authority catalog required by route preparation.
- The persistent source coordinator downloads, compiles, and warms route and authority publications below normal priority.
- The callback's idle source check is an atomic readiness test. It reaches the publication mutex only after the coordinator has completed and released its staging lock.
- Source identity covers boundary, VATSpy authority, and ownership payload content.
- Superseded route datasets are observed and retired by the route worker. They do not accumulate for the full plugin session.
- The established six-hour refresh cadence and ten-minute failure retry are preserved.

### Separate worker-owned engine

- Added `RoutePreparationEngine` with its own mutable navigation, airway, procedure, expanded-FMS, and scratch state.
- The engine does not call the shared `gRouteSectorResolver` or production `RouteSectorResolver::Resolve`.
- Worker filesystem access is available only through injected `RoutePreparationSource` methods.
- Production captures the X-Plane root once during startup. The worker never calls XPLM to discover it.
- A frozen synchronous oracle remains harness-only for exact field and ordering comparison.
- Preflight validation, expanded-FMS discovery and parsing, procedure metadata, grammar/token work, waypoint/airway resolution, polygon traversal, prefix population, and output finalization execute in the route engine.

### Persistent latest-only worker

- Starts before flight-loop registration and runs below normal priority.
- Uses lock-free pointer mailboxes with startup-preallocated request and fact nodes.
- Permits one running request and at most one replaceable pending request.
- New input cancels obsolete running work cooperatively and replaces older pending work.
- Cooperative checks exist between the expensive route phases.
- Exceptions become bounded typed failure facts and the worker remains usable.
- Disable, disconnect, reset, and route replacement only invalidate/cancel nonblockingly. Only plugin stop joins the worker, after the flight loop is unregistered.

### Exact identity and fail-closed publication

Every request and completion carries:

- lifecycle epoch;
- plan identity;
- complete route-relevant network-plan digest;
- immutable route-source dataset identity;
- exact preflight candidate identity;
- route policy identity; and
- route-anchor digest when the aircraft position is a required fallback.

A Brain-owned completion decision publishes only when its request ID is still eligible and every semantic identity component matches both the eligible request and current desired identity. Plan, source, preflight, anchor, policy, or lifecycle changes immediately retire the accepted route and fail closed until an exact completion arrives. Field-by-field stale-completion fixtures cover every identity component. There is no synchronous fallback.

### Immutable publication and worker-side retirement

- Worker completions hold `std::shared_ptr<const RouteSectorSnapshot>` in worker-retained leases.
- Brain route state and authority input now carry the immutable shared route instead of deep-copying its vectors.
- Rejected, replaced, invalidated, and formerly accepted worker routes are marked retired and destroyed on the worker.
- Real polygon transitions return the replaced immutable route explicitly. A fixed-capacity noncontending retirement handoff transfers its final release to the worker.
- The unchanged aircraft-progress path reads immutable route geometry, avoids rebuilding or sorting the sector sequence, creates no derived route, and preserves the exact shared pointer.
- A derived route is materialized only when the selected polygon actually changes.

### Lifecycle order

- Start: authority worker, route worker, source coordinator, then eventual flight-loop registration.
- Disable: unregister first, then invalidate and cancel without joining.
- Re-enable: source coordinator is already persistent; no route thread is created. The new exact request is submitted from the normal callback.
- Stop: unregister, invalidate/cancel, drain bounded transition retirements, join the route worker, stop the source coordinator, reset runtime state, join authority, then clear source datasets.

## Offline acceptance results

All results below are from the final Release candidate after the ownership amendment.

| Proof | Result |
|---|---:|
| Release plugin build | pass |
| Release regression harness build | pass |
| Frozen regression with exact route parity | `861/861` |
| Exact route-oracle scenarios | `64/64` |
| Combined authority and route exact parity | `64/64` |
| Gate A focused probe | pass |
| Gate A-Calm 1 focused probe | pass |
| Gate A-Calm 2 focused probe | pass |
| Product-Calm 1 focused probe | pass |
| Gate B focused probe | pass |
| `git diff --check` | pass (line-ending notices only) |

### Gate B focused metrics

- Complete package-and-submit P99: `3 us`
- Package-and-submit maximum: `12 us`
- Harvest/validation/retirement P99: `3 us`
- Harvest/validation/retirement maximum: `27 us`
- Unchanged 512-waypoint transition P99: `23 us`
- Unchanged 512-waypoint transition maximum: `436 us`
- Maximum pending depth: `1`
- Worker starts: `136`
- Pending replacements: `7`
- Cooperative cancellations: `3`
- Rapid semantic changes attempted: `10,000`
- Rapid changes accepted by the bounded mailbox: `7`
- Rapid changes rejected without blocking: `9,993`
- Rapid submit maximum: `0 us` at the probe's microsecond resolution
- Route leases created/retired/destroyed on worker: `104/104/104`
- Outstanding leases at idle: `0`
- Request nodes in use at idle: `0`
- Peak retained route bytes reported by the fixture: `828`
- Source replacements: `10`
- Source dataset observations: `11`
- Retained source datasets at idle: `1` (the permitted current publication)
- Peak retained source datasets during forced stress: `5` (contract bound: `6`)
- Stale publications: `0`
- Worker remained usable after an injected exception.
- Forced `500 ms` and `2 s` delays did not stall submission or invoke a synchronous fallback.
- A real Brain polygon transition proved explicit nonblocking retirement handoff of the replaced immutable route.
- Final join left no active worker, pending request, completion, lease, retained route bytes, or retained source dataset.

Timing values can vary slightly between runs; the executable assertions enforce P99 at or below `250 us` and maximum at or below `1 ms` for package/submit, harvest, and unchanged transition.

### Source and behavior coverage

The injected source fixture covers:

- exact preflight priority and validation;
- multiple matching expanded-FMS files;
- newest-modification selection;
- exact equal-time filename tie breaking;
- missing, unreadable, and malformed files;
- late expanded-FMS appearance;
- source-interface directory listing, FMS loading, and preflight validation; and
- strict production preflight source verification.

The saved 64-scenario route parity set covers the existing route, airway, procedure, endpoint, unavailable/stale, traversal, ordering, generation, status, diagnostic, and lifecycle cases and compares all route output fields against the frozen synchronous oracle.

## Static production-path proof

Production callback path:

1. `FlightLoopCallback`
2. `RefreshBrainRoutePolygonSnapshot`
3. atomic source readiness/publication read
4. nonblocking `RoutePreparationWorker::TryHarvest`
5. exact identity validation and shared immutable Brain publication, or worker retirement
6. optional nonblocking `RoutePreparationWorker::StartLatest`
7. unchanged `BeginBrainOwnedRoutePolygonRefresh` progress fast path

Worker path:

1. `RoutePreparationWorker::Implementation::Run`
2. `RoutePreparationEngine::Prepare`
3. `PrepareRouteSnapshotCore`
4. worker-owned injected source, navigation/procedure/airway state, and exact traversal
5. immutable completion lease publication

The dedicated worker translation unit contains no XPLM, WinHTTP/network, UI, settings, direct diagnostics-file, `gRouteSectorResolver`, `RouteSectorResolver::Resolve`, or mutex-lock reference. The engine resides in the legacy resolver translation unit for staged extraction, but the transitive engine path is selected through thread-local worker-owned state and does not call the XPLM/debug helpers or shared resolver instance.

Permitted filesystem calls are confined to the injected source boundary:

- `RoutePreparationSource::ReadTextFile` for worker navigation/procedure data;
- `RoutePreparationSource::ListRegularFiles` for the expanded-FMS directory;
- `RoutePreparationSource::LoadFmsPlan` for selected FMS candidates; and
- `RoutePreparationSource::ValidatePreflightCandidate` for strict production source validation.

The production plugin has no call to `gRouteSectorResolver.Resolve` in its route refresh path.

## Candidate artifacts

- Offline plugin candidate:
  - `build/product-calm-1/dist/XVatsim/win_x64/XVatsim.xpl`
  - bytes: `2,727,936`
  - SHA-256: `773B74217B2E9534DD502172F9A39F8E912F28DC55372C7C2D56DF30B668B5AF`
- Regression harness:
  - `build/product-calm-1/tools/XVatsimRegressionHarness.exe`
  - bytes: `4,372,992`
  - SHA-256: `FCBBB7369B16258C0A91CDBB2DFB0647CD674EDD6109D67B7E1CFDCDCD8AFFAA`
- Evidence root: `outputs/performance_contract_gate_b_20260901_133922/`

## Scope boundary and next gate

Gate B B0-B3 is an **offline pass**, not formal product closure. Phase B4 still requires separate explicit authorization for controlled backup, deployment, startup smoke testing, the specified connected flight, evidence collection, and rollback if necessary.

The installed plugin remains the Product-Calm candidate. No active X-Plane file was overwritten, no Gate B commit was made, and unrelated dirty/untracked repository content was neither staged nor modified as part of this gate.
