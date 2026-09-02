# Performance Contract Gate B — Asynchronous Route Preparation

Date: 2026-09-01

Status: implementation-ready engineering contract. Gate B source work may begin only from a separately closed Product-Calm 1 checkpoint. This document does not authorize deployment, rollback removal, or a Gate B commit.

## Decision

Move expanded-FMS route preparation, route-token resolution, waypoint construction, and sector-polygon traversal completely off X-Plane's flight loop. The simulator thread may detect work, package a bounded request, submit it, harvest a completed immutable result, validate its identity, publish it through the Brain, and evaluate a cheap aircraft-progress transition. It must never execute route construction or wait for it.

This is the only remaining known large XVatsim callback source. Gate A-Calm 1/2 and Product-Calm 1 are protected baselines and are not reopened by Gate B.

## Proven baseline and entry condition

Gate A-Calm 1/2 is closed at commit `db2265d17873b492def00311c3e19c27803e0b12`.

Product-Calm 1 controlled-live evidence established:

| Measurement | Result |
|---|---:|
| Complete callback timing records | 2,015 / 2,015, contiguous |
| Established callbacks | 1,315 |
| Established callback P99 | 1.811 ms |
| Established callback maximum | 2.992 ms |
| Authority simulator-thread P99 / maximum | 16 / 16 microseconds |
| Authority publications | 14 / 14 exact identity |
| Stale authority publications | 0 |
| Initial expanded-FMS route callback | 1,115.266 ms |
| Initial route construction | 1,114.978 ms |
| Re-enable callback | 66.586 ms |
| Re-enable route construction | 60.462 ms |

The active Product-Calm candidate has SHA-256 `3D8E35CBD3B0FAA96CEB24D5422BD73ECB928F8A7F9B7417249FA994E4CD4E70`. Its predecessor is preserved for rollback.

Before any Gate B source edit, record the scoped Product-Calm 1 closeout commit. That checkpoint must include only the two flight-plan sampler files, Product-Calm probe and harness registration, and the accepted proof material selected by the repository's evidence policy. It must not broadly stage the dirty repository or unrelated `docs/` and `outputs/` content. Record the resulting commit hash in the Gate B implementation report.

Primary accepted evidence:

- `outputs/product_calm_1_20260901_124906/PRODUCT_CALM_1_OFFLINE_PROOF.md`
- `outputs/product_calm_1_20260901_124906/02_controlled_live_test/05_postshutdown_diagnostics_complete.log`
- `outputs/product_calm_1_20260901_124906/02_controlled_live_test/05_postshutdown_X-Plane_Log_complete.txt`

## Confirmed synchronous cause

The current name `BrainRoutePolygonWorker` does not describe a background worker. The live flight loop calls it synchronously:

1. `plugin/src/XVatsimPlugin.cpp:5422` calls `RefreshBrainRoutePolygonSnapshot`.
2. `plugin/src/XVatsimPlugin.cpp:1998` calls `RunBrainRoutePolygonWorker` inline.
3. `plugin/src/XVatsimPlugin.cpp:1916` applies the preflight cache inline.
4. `plugin/src/XVatsimPlugin.cpp:1920` calls `gRouteSectorResolver.Resolve` inline.
5. `modules/route_sector/src/RouteSectorResolver.cpp:11425` enters the mutable route resolver and may call `BuildSnapshot`.
6. `modules/route_sector/src/RouteSectorResolver.cpp:12558` performs the expensive route build.

That build can perform all of the following before returning to X-Plane:

- scan `Output/FMS plans`, inspect directory entries, read candidate `.fms` files, parse them, and validate the selected plan;
- resolve the X-Plane root by dynamically calling `XPLMGetSystemPath`;
- read local procedure, CIFP, fix, navaid, and airway files;
- initialize or use process-static airway, procedure, boundary, and authority caches;
- build route grammar and procedure catalogs;
- parse the filed route, expand procedures and airways, and resolve waypoint ambiguity;
- rebuild traversal-feature copies from sector polygons;
- traverse route geometry against sector polygons;
- populate controller callsign patterns and prefixes;
- build diagnostic strings and copy the completed route through several value-owned Brain structures.

This code cannot be made safe by calling the existing `gRouteSectorResolver.Resolve` from a new thread. `RouteSectorResolver` owns mutable runtime and source caches, a fetch thread, payload vectors, preflight state, and airport coverage state. Several process-static cache helpers return references to mutable objects after releasing their mutex. The same resolver remains in use on the simulator thread for source harvesting and airport/terminal coverage. Concurrent access would create races and lifetime hazards.

The accepted correction therefore requires a separate route-preparation engine with exclusive worker ownership.

## Gate boundary

Gate B includes:

- immutable publication of route source data;
- a persistent latest-only route-preparation worker;
- worker-owned navigation, procedure, expanded-FMS, and route-construction state;
- Brain-owned request decisions, lifecycle invalidation, completion validation, and publication;
- immutable shared route completion ownership;
- removal of unchanged-callback route snapshot copying and destruction;
- exact synchronous-oracle parity, deterministic stress proof, and controlled-live proof.

Gate B does not include:

- authority algorithm changes, authority identity changes, or authority cadence changes;
- diagnostics writer redesign;
- further FMS endpoint-sampler changes;
- terminal authority, METAR, ATIS, PDC, radio-board, overlay, or accessory behavior changes;
- changing the normal flight-loop interval;
- weakening fail-closed behavior while a materially different route is pending;
- suppressing expanded-FMS support or replacing exact traversal with a cheaper approximation;
- unrelated cleanup in `RouteSectorResolver.cpp`.

## Required architecture

### 1. Immutable route-source publication

Introduce an immutable route source dataset, separate from the authority worker dataset. It should contain only compiled source information read by route construction, for example:

```cpp
struct RouteSourceDataset {
    std::uint64_t identity = 0;
    std::uint64_t centerBoundaryGeneration = 0;
    std::uint64_t authorityCatalogGeneration = 0;

    std::vector<core::route::SectorFeature> traversalFeatures;
    RouteControllerPatternCatalog controllerPatterns;
};
```

The exact representation is an implementation decision, but these rules are mandatory:

- Parse raw boundary and authority payloads once on the existing source-fetch/compile path, never per route request.
- Convert traversal features once; do not rebuild every polygon/ring vector in every route job.
- Publish dataset pointer and identity coherently.
- Keep every published immutable dataset alive until all requests/completions referencing it have retired. Plugin stop is the final safety boundary, but large obsolete route datasets must be reclaimed after an explicit worker/source-owner retirement acknowledgement rather than accumulated without bound for the entire session.
- A route request reads the publication once. It must not independently read a pointer and generations that could belong to different publications.
- Dataset replacement changes the desired route identity exactly once and makes an old completion ineligible for publication.
- The flight loop must not hash or copy raw source payloads, sector polygons, or controller catalogs.

Use the proven Gate A coherent immutable-publication pattern where appropriate, augmented with bounded retirement for these larger route datasets. Do not reuse the authority dataset merely because it exists; route traversal and authority proof have different compiled inputs and ownership boundaries.

### 2. Worker-owned route-preparation engine

Create a `RoutePreparationEngine` or equivalently named type used exclusively by one persistent route worker. Extract the pure route-building work from `RouteSectorResolver::BuildSnapshot` into this engine.

The engine owns:

- the local navigation graph built from fix, navaid, and airway data;
- procedure metadata caches keyed by airport;
- expanded-FMS discovery and parsing state;
- route grammar and token-resolution scratch state;
- any bounded reusable scratch buffers or indexes needed by traversal;
- no state shared mutably with `gRouteSectorResolver`.

Capture the X-Plane root path on the simulator thread during plugin startup and pass it as immutable worker configuration. The worker must never call `XPLMGetSystemPath`, any other X-Plane API, or `SafeXPlaneDebugString`.

Local filesystem work required for navigation and expanded-FMS fidelity may execute on the route worker. It must not execute on the flight loop. Network access, UI access, settings access, X-Plane access, and direct diagnostics-file access are prohibited from the route worker.

The engine must accept an injected filesystem/source interface in the regression harness so expanded-FMS selection, missing files, read failures, late files, and deterministic source identities can be tested without the user's live X-Plane installation.

### 3. Complete semantic request identity

Define one Brain-owned desired identity for a route job. At minimum it must contain:

```cpp
struct RouteWorkerIdentity {
    std::uint64_t requestId = 0;
    std::uint64_t lifecycleEpoch = 0;
    std::string planKey;
    std::uint64_t networkPlanDigest = 0;
    std::uint64_t routeSourceDatasetIdentity = 0;
    std::uint64_t preflightCandidateIdentity = 0;
    std::uint64_t routePolicyIdentity = 0;
    std::uint64_t routeAnchorDigest = 0;
};
```

Names may differ, but identity coverage may not.

- `networkPlanDigest` hashes every network-plan field read by route preparation, including availability/staleness, departure and destination identity/coordinates, route text, and any route-source discriminator.
- `routeSourceDatasetIdentity` covers the exact immutable boundary/catalog publication.
- `preflightCandidateIdentity` distinguishes no candidate, a cleared/rejected candidate, and each exact immutable preflight cache.
- `routePolicyIdentity` prevents a completion built under materially different route tuning or algorithm policy from publishing.
- `routeAnchorDigest` covers the captured aircraft-derived anchor fields when route resolution can use the aircraft because departure coordinates are unavailable. It must not create routine position-driven dispatch churn when the accepted route no longer needs reconstruction.

`BuildRadioBoardRouteRuntimeKey`, route cache decisions, worker equivalence, and completion validation must derive from the same semantic contract. Do not maintain divergent definitions in the plugin, Brain, and route module.

The identity must change for a material route edit, late endpoint enrichment that changes route inputs, source dataset replacement, preflight selection change, lifecycle invalidation, or a route-relevant fallback anchor change. It must remain stable for controller refreshes, transceiver refreshes, diagnostic changes, and ordinary aircraft movement after a usable route has been accepted.

### 4. Owned request package

A route request must own or safely reference all inputs for its full worker lifetime:

```cpp
struct RouteWorkerRequest {
    RouteWorkerIdentity identity;
    AircraftStateSnapshot routeAnchor;
    std::shared_ptr<const NetworkPlanSnapshot> networkPlan;
    const RouteSourceDataset* routeSourceDataset = nullptr;
    std::shared_ptr<const core::preflight::PreflightRouteCache>
        preflightCandidate;

    // Deterministic proof seam; production is zero.
    std::uint32_t cooperativeDelayForTestingMs = 0;
};
```

The precise types may differ. The ownership guarantees may not.

- Do not pass borrowed controller, payload, route, preflight, or string views.
- Do not deep-copy sector features, navigation graphs, procedure catalogs, or an existing route on the simulator thread.
- The normal unchanged callback performs no route request allocation.
- Request publication must be lock-free/nonblocking from the simulator thread. Prefer startup-preallocated intrusive mailbox nodes. If another bounded node design is used, prove that dispatch performs no general-heap allocation.
- Replaced requests and their shared references are cleared and reclaimed on the worker thread, not on the simulator thread.

### 5. Persistent latest-only worker

The route worker must:

- start successfully during `XPluginStart`, before flight-loop registration;
- run at below-normal priority;
- permit one running request and at most one replaceable pending request;
- replace an older pending request with the latest desired identity;
- never process an obsolete backlog;
- provide nonblocking submit, harvest, status, cancellation notification, and retirement notification;
- use cooperative cancellation checkpoints between expensive filesystem, token, airway, waypoint, and polygon phases;
- catch worker exceptions and publish a typed failure fact rather than terminating the process;
- join only after the flight loop is unregistered during `XPluginStop`.

`XPluginDisable`, disconnect, session reset, and flight-context replacement must not join the worker. They advance/invalidate the lifecycle, cancel pending work nonblockingly, retire accepted route state as required, and leave the persistent worker ready for re-enable.

### 6. Brain-owned submission and publication

The Brain, not the worker, decides whether work is required and whether a completion may publish.

The simulator thread is limited to:

1. Read the coherent route source publication.
2. Harvest at most one completion without waiting.
3. Validate and either publish or retire it.
4. Determine the current desired identity once after relevant input publications are available.
5. Fail closed immediately if a materially different route is now desired.
6. Submit only when no accepted/running/pending request already represents the desired identity.
7. Return the accepted immutable route or a typed pending/unavailable route.

It must not call any synchronous route oracle as a fallback when the worker is busy, unavailable, failed, or late.

A completion may publish only when all identity components exactly match the current desired identity and its request ID is still eligible. An identity mismatch is a normal stale completion. It is retired without changing route, authority, workflow, or overlay state.

If the plan, source dataset, preflight selection, or lifecycle changes, an accepted route from the former identity must not remain authoritative. Route and downstream authority presentation fail closed until an exact completion arrives. Unrelated overlay, METAR, ATIS, PDC, radio, and accessory functions continue.

### 7. Immutable completion and retirement ownership

The worker publishes an immutable completion lease containing:

- exact request identity;
- `std::shared_ptr<const RouteSectorSnapshot>` or an equivalent immutable route product;
- a comprehensive route digest;
- worker phase timings;
- completion/cancellation/failure status and a bounded typed reason;
- optional immutable prepared transition index.

Do not copy the completed route vectors during harvest, Brain commit, authority dispatch, or unchanged cache hits. Refactor value-owned route state where necessary so the accepted route is shared immutably through downstream consumers.

The worker retains completion leases until the main thread marks them retired. Destruction of a large rejected, replaced, or formerly accepted route snapshot occurs on the worker thread. Main-thread invalidation may flip an atomic retirement bit and release a non-final reference only.

Add exact counters for request nodes, replacements, completions, stale rejections, leases created/retired/destroyed, currently outstanding items, and peak retained route bytes/items.

### 8. Aircraft-progress transition fast path

Route construction and route progress are different workloads. Aircraft progress may remain on the simulator thread only under this contract:

- The worker prepares and publishes the full ordered sector sequence and any reusable traversal index.
- An unchanged aircraft-progress evaluation does not copy the full `RouteSectorSnapshot`, rebuild the sector sequence, sort sectors, allocate vectors, or rebuild route hashes.
- It computes progress and selected current/next indices against immutable prepared geometry.
- It materializes or publishes a new derived route view only when the selected polygon actually changes.
- A real polygon transition still changes the authority route digest and triggers the existing exact fail-closed authority workflow.
- The complete unchanged transition fast path satisfies the timing gate below.

If exact offline measurement cannot keep that path bounded without route copying, move only its expensive prepared-data work to the route worker. Do not dispatch a new asynchronous route job on every aircraft observation.

### 9. Preflight and expanded-FMS behavior

Preserve the current source priority exactly:

1. A valid exact preflight route cache.
2. The newest matching expanded-FMS plan selected by the current matching and tie-break rules.
3. Filed-route token/airway/procedure resolution.

Move validation/application into the worker-owned route job. `ApplyPreflightRouteCacheForPlanIfNeeded` must not mutate the shared main-thread resolver as part of live route construction after Gate B.

Preserve:

- route endpoint and route-text matching;
- procedure and airway grammar;
- expanded-FMS evidence-token matching;
- source modification-time and filename tie breaks;
- unresolved-airway behavior and status text;
- exact sector ordering, entry distances, match tokens, callsign patterns, and controller prefixes;
- late FMS enrichment and retry behavior;
- current unavailable/stale/failure semantics.

No acceptance claim may be based on disabling FMS directory discovery, reducing the route, skipping polygons, or substituting a direct departure-to-destination line.

### 10. Lifecycle order

Implement and prove this ordering:

- **Start:** capture immutable worker configuration, start the route worker, then register the flight loop.
- **Material route change:** advance desired identity, fail closed, submit latest.
- **Disconnect/session reset/flight recovery replacement:** advance lifecycle, cancel pending nonblockingly, clear or retire accepted state, reject old completions.
- **Plugin Admin disable:** unregister flight loop first, advance lifecycle, cancel pending nonblockingly, preserve only explicitly permitted non-route flight context.
- **Plugin Admin re-enable:** do not create a thread; submit a new exact route request and remain responsive while it runs.
- **Source replacement:** publish a new immutable dataset, change desired identity exactly once, retire old completion only after its users release it.
- **Plugin stop:** unregister flight loop, invalidate lifecycle, request cancellation, join route worker, verify request/completion/lease retirement, then destroy route datasets and resolver/source state.

The worker must not outlive any raw retained dataset it may reference.

## Required diagnostics

Use the existing asynchronous diagnostics writer. The simulator thread must enqueue bounded typed data only; it must not format a large route trace or write files.

Required events:

- `route-worker-dispatch`
- `route-worker-terminal`
- `route-worker-disposition`
- `route-worker-stale-rejected`
- `route-worker-pending-replaced`
- `route-worker-cancelled`
- `route-worker-stop`

Record request ID, lifecycle, semantic identity components, queue state, accepted/rejected disposition, result digest, and separately measured simulator/worker times.

Worker phase telemetry must distinguish at least:

- immutable source acquisition;
- navigation dataset initialization/cache hit;
- preflight validation;
- FMS directory discovery and candidate reads;
- procedure metadata acquisition;
- grammar/token parsing;
- waypoint/airway resolution;
- traversal preparation;
- polygon traversal;
- controller-prefix population;
- output finalization;
- worker total.

Simulator-thread telemetry must distinguish:

- publication read and desired identity;
- harvest and validation;
- retirement handoff;
- request package and mailbox exchange;
- transition fast path;
- diagnostics enqueue;
- complete route-stage time.

All counters and timings must be included in final shutdown accounting.

## Implementation sequence

### Phase B0 — protected checkpoint

- Close Product-Calm 1 in a separately scoped commit after explicit commit authorization.
- Record baseline commit, active binary hash, rollback hash, and pre-Gate-B dirty-tree manifest.
- Do not stage unrelated existing files.

### Phase B1 — extract deterministic route engine

- Introduce immutable route source publication.
- Extract `RoutePreparationEngine` with injected filesystem/source access.
- Keep the current synchronous route path as a harness-only oracle.
- Prove exact output parity before introducing concurrency.
- Do not deploy B1.

### Phase B2 — worker and Brain contract

- Add request/fact/lease types and persistent latest-only worker.
- Add Brain desired-identity, pending, publication, stale-rejection, and lifecycle decisions.
- Convert accepted route ownership to immutable sharing.
- Remove synchronous route construction from the plugin flight-loop call graph.
- Retain transition handling only under the fast-path contract.

### Phase B3 — hardening and observability

- Add cooperative cancellation, exception conversion, worker-side retirement, counters, phase timings, and static path scans.
- Add forced-delay, rapid replacement, lifecycle, source replacement, and allocation/retirement stress fixtures.
- Prove the plugin and harness in Release configuration.

### Phase B4 — controlled deployment and live acceptance

- Preserve the active Product-Calm binary and record hashes before deployment.
- Deploy only after offline acceptance and explicit deployment authorization.
- Conduct one controlled flight that exercises initial expanded-FMS construction, settled connected operation, Plugin Admin disable/re-enable, a second settled period, xPilot disconnect, and normal shutdown.
- Preserve complete diagnostics and X-Plane logs after shutdown.

## Offline acceptance gates

Gate B does not pass offline until every item below is demonstrated.

### Build and protected behavior

- Release plugin and regression harness build successfully.
- Frozen regression remains `861/861`.
- Authority synchronous/worker parity remains `64/64`.
- Gate A, Gate A-Calm 1, Gate A-Calm 2, and Product-Calm 1 focused probes all pass.
- `git diff --check` passes.
- No unrelated source, behavior, cadence, or UI change is present.

### Exact route parity

Run every saved scenario that can produce or consume `RouteSectorSnapshot` through both the frozen synchronous oracle and `RoutePreparationEngine`. Compare every output field, ordered waypoint, ordered current/next sector, entry distance, token, callsign pattern, prefix, generation, status, cache reason, diagnostic reason, and comprehensive digest.

The parity set must include dedicated fixtures for:

- valid preflight cache, rejected preflight cache, and no preflight cache;
- expanded FMS with multiple matching files and exact modification-time/filename tie breaks;
- missing/unreadable/malformed FMS files;
- late expanded-FMS appearance and late endpoint enrichment;
- SID, STAR, airway, direct, coordinate, duplicated-ident, unresolved-token, and unresolved-airway routes;
- unavailable/stale network plan and unavailable/replaced boundary source;
- source generation replacement while a job is running;
- departure-coordinate unavailable aircraft-anchor fallback;
- routes crossing multiple center polygons, overlapping polygons, equal-distance sectors, final-sector transition, and no-sector results;
- Plugin Admin disable/re-enable and session reset identities.

Zero parity differences are allowed. If an intentional correction is discovered, stop and amend this contract with a separately reviewed behavioral change; do not silently update the oracle.

### Nonblocking and bounded work

Add a deterministic `--performance-contract-gate-b` probe proving:

- complete desired-identity/package/submit P99 at or below **250 microseconds** and maximum at or below **1 millisecond**;
- harvest/validate/retirement-handoff P99 at or below **250 microseconds** and maximum at or below **1 millisecond**;
- unchanged transition fast-path P99 at or below **250 microseconds** and maximum at or below **1 millisecond** on the largest saved route;
- no simulator-thread filesystem, X-Plane, network, UI, settings, route-construction, polygon-traversal, thread-create/join, mutex-wait, or large-object destruction operation;
- one running request and at most one replaceable pending request;
- maximum pending depth one;
- exact latest-only result after rapid A/B/C route replacement;
- forced 500-millisecond and 2-second worker delays cause no caller stall and no synchronous fallback;
- cancellation and stale completion retirement do not block the caller;
- zero stale publications;
- worker exceptions/failures produce bounded unavailable facts and leave the worker usable;
- final worker join, zero active worker threads, and bounded outstanding object counts.

### Allocation, retirement, and memory stress

- Normal unchanged route callbacks allocate no route request and copy no route vector.
- Dispatch and harvest allocate no general-heap request/fact nodes on the simulator thread.
- Replaced requests and rejected/retired route snapshots are destructed on the worker thread.
- At least 10,000 rapid semantic route changes under forced slow work retain only the running request, latest pending request, accepted completion, and explicitly bounded retirement ledger.
- After cancellation/join, requests, facts, leases, retained route snapshots, and retained bytes return to their defined idle baseline.
- Superseded route-source datasets remain bounded to the explicitly permitted current/running/pending retirement set and return to the current-only baseline after worker acknowledgement.
- An accelerated lifecycle replay covers at least 1,000 disable/re-enable, disconnect/reconnect, reset, plan replacement, and source replacement boundaries without a leaked thread or unbounded object count.

### Static worker-path proof

Provide a transitive static scan of the production route-worker entry path. It must show no call or reference to:

- `gRouteSectorResolver` or `RouteSectorResolver::Resolve`;
- X-Plane APIs or dynamic XPLM function lookup;
- overlay/UI code;
- settings storage;
- WinHTTP/network functions;
- direct diagnostics-file output;
- process-static mutable route caches shared with a simulator-thread caller.

Local filesystem reads are permitted only through the injected worker-owned route source interface. List those exact call sites in the proof.

## Controlled-live acceptance gates

The controlled flight must prove:

- the deployed file exactly matches the offline candidate hash;
- rollback file and hash are preserved;
- every callback timing record is contiguous with zero timing-lane loss;
- established callback P99 is at or below **3 milliseconds**;
- no route-related callback exceeds **10 milliseconds**;
- initial expanded-FMS construction produces a worker job rather than a synchronous route spike;
- Plugin Admin re-enable produces a worker job rather than a synchronous route spike;
- route simulator-thread package/submit and harvest/validation each have P99 at or below **250 microseconds**;
- route simulator-thread maximum is at or below **1 millisecond** outside explicitly classified plugin/session startup work;
- worker duration may remain hundreds of milliseconds or more, but no callback waits for it;
- the accepted live route digest and identity match the completed request exactly;
- authority receives only the accepted exact route and retains Gate A performance;
- worker queue depth remains one and stale route publications remain zero;
- no timing, critical diagnostic, storage, or formatting failure occurs;
- disable/re-enable, disconnect, worker cancellation, diagnostics drainage, route-worker join, and X-Plane shutdown complete cleanly;
- final request/fact/lease/thread counters are coherent and bounded.

The live report must classify every callback above 3 milliseconds and show the route-stage contribution. A remaining 1.1-second worker duration is acceptable; a 1.1-second simulator callback is not.

## Approaches to reject

Do not accept any of the following as Gate B:

- increasing the callback interval or delaying route observation;
- disabling expanded-FMS discovery or exact polygon traversal;
- prewarming the same synchronous work and claiming the later cache hit as success;
- calling the existing shared `RouteSectorResolver` from a background thread;
- placing a mutex around the resolver and allowing the flight loop to wait for it;
- using an unbounded job queue;
- permitting more than one replaceable pending request;
- copying a completed route into several main-thread value objects;
- destructing replaced route vectors on the flight loop;
- retaining an old route after a materially different plan/source/lifecycle identity is desired;
- accepting a completion by plan key alone;
- performing X-Plane API calls or `XPLMDebugString` on the route worker;
- hiding route time outside the callback measurement boundary;
- combining unrelated refactors or product behavior changes into the Gate B diff;
- committing or deploying before the required proof and authorization boundary.

## Required deliverables from Codex

Codex must return all of the following:

1. A concise source audit identifying every changed file and why it belongs to Gate B.
2. `docs/PERFORMANCE_CONTRACT_GATE_B_IMPLEMENTATION_AND_OFFLINE_PROOF.md` containing architecture, ownership, identities, lifecycle order, static scan, build results, regression/parity results, focused-probe metrics, stress counters, and unresolved risks.
3. Preserved raw offline proof outputs in one timestamped `outputs/performance_contract_gate_b_*` directory.
4. Release plugin and harness paths, byte sizes, and SHA-256 hashes.
5. A predeployment dirty-tree and candidate manifest that excludes unrelated files.
6. A clear statement that no active X-Plane plugin was overwritten and no commit was created unless Darron separately authorized those actions.
7. After controlled deployment is authorized and completed, a final live report with immutable diagnostics/X-Plane logs, callback classifications, lifecycle accounting, candidate/active/rollback hashes, and an explicit pass/rollback recommendation.

## Codex execution directive

Treat this document as the controlling Gate B contract. First verify the Product-Calm 1 checkpoint boundary and inspect the current dirty tree. Then implement only Phases B1 through B3, prove every offline gate, and stop before deployment or commit unless Darron explicitly authorizes the next boundary. Do not solve the problem with cadence, a shared-resolver thread wrapper, incomplete identity, synchronous fallback, or reduced route fidelity.
