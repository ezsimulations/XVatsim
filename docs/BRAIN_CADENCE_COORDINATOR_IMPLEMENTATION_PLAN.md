# Brain Cadence Coordinator Implementation Plan

Status: draft implementation plan, not production code.

Purpose: convert XVatsim from several modules independently requesting expensive work into one brain-owned cadence model. The brain decides what work is needed, when it runs, and which module receives the result. This plan does not change authority truth rules. It only changes orchestration, workload pacing, caching, and ownership.

## Core Principle

The brain runs the modules. The modules do not run the brain.

Departure, Enroute, Arrival, and overlay code should consume proven snapshots. They should not independently trigger expensive route, authority, transceiver, terminal, or board-building work from display refresh paths.

## Existing Modules To Adjust

- `XVatsimPlugin.cpp`: main flight loop and workflow owner. This becomes the primary caller into the cadence coordinator instead of letting expensive work fire from scattered paths.
- `RouteSectorResolver`: remains the route geometry, authority evidence, and source-data resolver, but expensive calls become scheduled work items rather than ad hoc module calls.
- `DepartureModule`: becomes a consumer of departure snapshots: airport local, departure terminal, and current center.
- `EnrouteModule`: becomes a consumer of center snapshots for current and near-future route polygons only.
- `ArrivalModule` and arrival support modules: become consumers of arrival snapshots: destination airport local, arrival terminal, current/next center.
- Controller/VATSIM/transceiver feed modules: provide source snapshots and dirty flags. They should not decide workflow state.
- Overlay/UI code: consumes last proven display snapshot and should never directly trigger heavy authority proof.

## New Components

### BrainWorkScheduler

Owns the work queue and cadence rules.

Responsibilities:

- Accept work requests from the brain only.
- Run at most one heavy job per brain cycle.
- Allow lightweight aircraft/radio/UI snapshot updates every frame or light refresh.
- Serialize expensive work so route updates, authority proof, board rebuilds, and source diffs cannot pile up in the same tick.
- Log every job: scheduled, skipped, deferred, started, completed, duration, reason, and output generation.

### BrainWorkItem

Typed job request.

Required fields:

- job type
- priority
- reason
- workflow stage
- route polygon sequence target
- max budget class
- source generation inputs
- cache key

Example job types:

- `BuildRouteScopedMap`
- `ResolveDepartureAirportLocal`
- `ResolveDepartureTerminal`
- `ResolveCurrentCenter`
- `ResolveNextCenterWindow`
- `ResolveArrivalAirportLocal`
- `ResolveArrivalTerminal`
- `RunAuthorityFastPath`
- `RunAuthorityProof`
- `BuildDepartureSnapshot`
- `BuildEnrouteSnapshot`
- `BuildArrivalSnapshot`
- `PublishUiSnapshot`

### BrainDataSnapshot

Immutable output passed to modules.

Required fields:

- flight identity
- workflow stage
- aircraft position
- route polygon sequence
- current polygon
- next polygon within threshold
- destination distance
- accepted controllers with evidence summary
- rejected candidates with reason summary for diagnostics
- cache/source generations used

### RouteAuthorityPlan

Route-scoped authority map.

This is not a second world map. It is a small ordered view into the global source polygons for the current flight only.

Required fields:

- ordered route polygon IDs
- polygon sequence number
- entry/exit route distance
- current/next/arrival flags
- source ownership available for each polygon
- unresolved status for each polygon
- cached controller status for each polygon

## Worker Ownership

Each worker has one job and does not mutate workflow state directly.

- `RouteScopedMapWorker`: takes the parsed flight plan and global polygon catalog, then builds the ordered route polygon view.
- `AirportLocalAuthorityWorker`: resolves DEL/GND/TWR for a specific airport only.
- `TerminalAuthorityWorker`: resolves DEP/APP/TRACON ownership for departure or arrival only.
- `CenterAuthorityFastPathWorker`: cheaply accepts obvious center matches from source-owned prefixes, known ownership, and already cached evidence. It cannot reject a polygon as empty by itself.
- `CenterAuthorityProofWorker`: runs heavier evidence checks for unresolved route polygons only, including source ownership, frequency ownership, transceiver geometry, and route relevance.
- `BoardSnapshotWorker`: converts proven controller state into Departure, Enroute, or Arrival display snapshots.
- `UiPublishWorker`: publishes the last proven snapshot to the overlay without causing proof work.

## Block 1: Baseline And Instrumentation

Goal: prove current behavior before changing ownership.

Code scope:

- Add or verify diagnostics for every expensive job path.
- Log job name, reason, workflow stage, duration, source generations, route cache key, and whether the result came from cache or proof.
- Do not change controller truth logic.
- Do not change workflow stage logic.

Battle-test gate:

- Full harness passes.
- One short route log review confirms we can see what expensive jobs ran and why.
- No UI behavior change expected.

## Block 2: Typed Brain Work Model

Goal: define the language the brain uses to schedule work.

Code scope:

- Add typed `BrainWorkItem`, `BrainWorkResult`, `BrainWorkPriority`, `BrainWorkReason`, and `BrainDataSnapshot`.
- Add compile-time-safe job names rather than string-based scheduling.
- Add tests for priority ordering and cache-key identity.
- Keep existing production behavior wired as-is.

Battle-test gate:

- Full harness passes.
- Unit or harness test proves work items sort by priority and preserve reason/source metadata.
- No UI behavior change expected.

## Block 3: Scheduler In Shadow Mode

Goal: install the scheduler without letting it control production yet.

Code scope:

- Add `BrainWorkScheduler`.
- Feed it the same events the plugin currently sees.
- Scheduler logs what it would run, but old code still runs the actual behavior.
- Detect moments where old code runs multiple heavy jobs in one tick.

Battle-test gate:

- Full harness passes.
- One real or simulated route shows shadow logs matching the expected brain sequence.
- We identify the worst remaining multi-job ticks before cutover.

## Block 4: Route-Scoped Authority Plan

Goal: stop thinking globally during live flight.

Code scope:

- Build `RouteAuthorityPlan` once a flight plan is known.
- Sequence the route polygons from departure to arrival.
- Mark current polygon, next polygon, and arrival polygon.
- Keep the global polygon catalog read-only and cached.
- Cache the route-scoped plan by flight identity, route hash, source registry generation, and boundary generation.
- Treat the route-scoped plan as the active flight map, not the permanent world map.
- Keep the global polygon catalog available so the active flight map can be rebuilt if the route changes.

Battle-test gate:

- Harness routes confirm correct polygon order for known real failures and recent battle tests.
- PANC-VHHH, KDFW-ZSAM, EDDB-LBSF, EGUL-LKPR, KDCA-TJSJ remain sane.
- No controller acceptance logic changes in this block.

## Block 4A: Reroute And Diversion Rebuild Safety

Goal: make the scoped flight map small without making it brittle.

Code scope:

- Detect route identity changes from VATSIM flight plan updates, manual recovery, diversion context, or future SimBrief/FMS input.
- Rebuild the `RouteAuthorityPlan` from the read-only global catalog when the active route changes.
- Preserve already proven controller evidence only when it still belongs to the new route-scoped authority plan.
- Invalidate stale polygon sequence numbers when the new route changes order or destination.
- Publish a pending state while the new route-scoped map is being rebuilt so modules continue showing the last proven safe snapshot instead of going blank.
- Log the rebuild reason: original flight plan, VATSIM refile, reconnect recovery, diversion, manual recovery, or route edit.

Battle-test gate:

- Start with one route, then simulate a changed route and confirm the old route polygons are no longer active.
- Simulate a diversion and confirm the new destination receives arrival authority prep at 200nm.
- Confirm stale controllers from the abandoned route do not leak into the UI.
- Confirm the global polygon catalog is reused rather than reparsed from scratch unless source data changed.

## Block 5: Departure Ownership Cutover

Goal: let the brain feed Departure Module instead of Departure Module pulling broad work.

Code scope:

- Brain schedules departure jobs in order: airport local, departure terminal, current center.
- Departure Module receives one departure snapshot.
- Departure display never triggers heavy proof directly.
- Keep existing evidence engine as the source of truth.

Battle-test gate:

- Ground tests with DEL/GND/TWR/DEP/CTR online.
- Ground tests with no controllers online.
- Confirm battery plus xPilot connection wake behavior remains unchanged.
- Confirm no new CPU spikes on flight-plan receipt beyond the scheduled jobs.

## Block 6: Enroute Ownership Cutover

Goal: Enroute Module only cares about current and near-future center authority.

Code scope:

- Brain schedules center work for current polygon immediately.
- Brain schedules next polygon only when within the configured lookahead window, normally 200nm.
- Far-route polygons remain cached as route plan only, not live authority proof.
- Empty current polygons get safe recheck cadence so controllers logging on are not missed.
- Proven online centers relax to slower refresh cadence.

Battle-test gate:

- Known failures remain fixed: HKG_W_CTR, CZVR_CTR, Seattle/Oakland/SLC examples.
- Empty-center route does not leak unrelated controllers.
- UI wakes at 200nm from relevant next center as already confirmed with SLC.
- CPU logs show no recurring global authority proof loop.

## Block 7: Arrival Ownership Cutover

Goal: Arrival Module wakes at 200nm and receives ordered arrival data without global scans.

Code scope:

- At 200nm destination distance, brain schedules arrival center, destination airport local, and arrival terminal in order.
- Arrival Module receives one arrival snapshot.
- Keep the existing 200nm arrival wake rule.
- Arrival display never triggers heavy proof directly.

Battle-test gate:

- Routes with destination GND/TWR/APP/CTR online.
- Routes with only destination local online.
- Routes with no arrival controllers online.
- Confirm existing 200nm wake behavior stays intact.

## Block 8: Source Diff And Cache Discipline

Goal: source changes create dirty flags, not full recompute panic.

Code scope:

- Controller feed changes produce relevant diffs by facility/type/frequency/callsign.
- Transceiver feed changes produce relevant geometry/frequency diffs.
- Dirty flags target specific route polygons, airport local groups, or terminal groups.
- Cache invalidation is explicit: flight plan changed, source generation changed, controller relevant diff, transceiver relevant diff, current polygon changed, 200nm boundary crossed, or workflow stage changed.

Battle-test gate:

- Controller logs on inside current empty polygon and is discovered.
- Controller logs on far ahead and is deferred until relevant.
- Controller logs off and UI updates without full route rebuild.
- Diagnostics show why each refresh ran.

## Block 9: Production Scheduler Cutover

Goal: the scheduler becomes the only production path for expensive work.

Code scope:

- Replace scattered direct expensive calls with scheduled jobs.
- Enforce one heavy job maximum per brain cycle.
- UI always displays the last proven snapshot while work is pending.
- Add a safety log if any module attempts an unscheduled expensive call.

Battle-test gate:

- Full harness passes.
- Short-haul and long-haul battle tests pass.
- CPU diagnostics show no recurring heavy spike cadence.
- No authority truth rules changed.

## Block 10: Legacy Audit And Cleanup

Goal: remove stale behavior after the scheduler owns the system.

Code scope:

- Audit `XVatsimPlugin.cpp`, `RouteSectorResolver`, `DepartureModule`, `EnrouteModule`, `ArrivalModule`, and overlay paths.
- Remove old direct proof triggers, stale latches, duplicate board rebuilds, and old fallback loops.
- Keep diagnostics for source evidence and scheduler decisions.

Battle-test gate:

- Full harness passes.
- Re-run saved battle-test repository.
- One real flight log review shows stable cadence.
- Contract can be marked complete only after no known old self-trigger path remains.

## Non-Negotiables

- No authority evidence truth changes in this performance pass.
- No module may trigger heavy authority proof from UI/display refresh.
- Fast path may accept obvious proof, but it may not reject unresolved polygons as empty.
- Heavy proof only runs for current, near-future, arrival, or explicitly dirty unresolved authority groups.
- The pilot may see a short delay while proof is pending; the simulator must not freeze.
- Diagnostics must explain what ran, why it ran, what it proved, and what it deferred.

## Contract Candidate

This plan becomes a contract only after review. The likely contract acceptance wording is:

- The brain owns cadence and work scheduling.
- Modules consume snapshots and do not self-trigger expensive work.
- Only one heavy job may run per brain cycle.
- Work priority is current safety, departure/arrival relevance, near-route relevance, then future route preparation.
- UI displays last proven data while new proof is pending.
- Route authority work is scoped to the route polygon plan, not the whole world.
- Controller and transceiver feed changes become targeted dirty flags.
- All proof and rejection decisions remain governed by the existing authority evidence contract.
- Every expensive job logs its reason, duration, cache status, and result.
- The final pass removes legacy direct-call paths that bypass the scheduler.
