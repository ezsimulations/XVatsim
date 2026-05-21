# Brain Cadence Coordinator Contract

Status: terminated as the primary runtime architecture on 2026-05-19.

Termination note: the PANC -> KONT / UPS344 live battle test proved that the
cadence scheduler still allowed heavy route/authority/arrival proof work to run
inside the X-Plane refresh path. This contract remains useful as background
context, but it is superseded by
`docs/RADIO_RANGE_AUTHORITY_GATE_CONTRACT.md`.

Date locked: 2026-05-18

Guiding principle: smooth is fast, fast is smooth.

This contract exists to restructure XVatsim's runtime engine so the simulator stays smooth while the plugin remains trustworthy. The goal is not to weaken authority detection. The goal is to make authority detection disciplined, scheduled, scoped, cached, and owned by one brain.

## 1. Contract Purpose

XVatsim must stop behaving like several modules independently asking expensive questions. The brain must become the single authority that decides:

- what work is needed
- when it runs
- why it runs
- which worker performs it
- which module receives the proven result
- whether the result is cached, deferred, invalidated, or published

The result should be a plugin engine that feels calm, predictable, and synchronized instead of reactive and noisy.

## 2. Non-Negotiables

- The brain runs the modules. The modules do not run the brain.
- Authority truth rules do not change during this performance contract.
- No UI, overlay, board, Departure, Enroute, or Arrival refresh path may directly trigger heavy authority proof.
- Only one heavy job may run per brain cycle.
- Expensive work must be route-scoped, not global, unless rebuilding the active flight map requires global source lookup.
- The global polygon catalog remains read-only source data.
- The active flight map contains only the route-relevant authority polygons for the current flight.
- The active flight map must be rebuildable for reconnect, refile, reroute, diversion, or route edit.
- Workers must do only their assigned job and return results to the brain.
- Modules consume proven snapshots and do not mutate workflow ownership.
- UI displays the last proven safe snapshot while new work is pending.
- Diagnostics must explain what ran, why it ran, how long it took, whether it used cache, and what changed.

## 3. Active Flight Map Rule

The global polygon catalog is the library.

The route-scoped authority map is the active workspace.

Once the VATSIM flight plan is available, the brain builds an active flight map from the global catalog. Workers treat this scoped map as the current world for the flight. If the flight changes, the brain discards or invalidates the active flight map and builds a new one from the global catalog.

The active flight map must include:

- ordered route authority polygons
- polygon sequence numbers
- current polygon
- next polygon
- arrival polygon
- entry and exit distance where available
- source ownership metadata
- cached controller proof state
- unresolved/empty state
- dirty flags

The active flight map must not permanently replace the global catalog.

## 4. Brain Cadence Rule

Every frame or light refresh may update aircraft/radio/display state only.

Every 1-2 seconds may update cheap state and UI snapshots.

Every controller feed refresh should produce diffs, not global recompute.

Route authority work runs only when one of these is true:

- new flight plan received
- route identity changed
- aircraft moved across a meaningful threshold
- current polygon changed
- next polygon entered the lookahead window
- arrival is within 200nm
- relevant controller/transceiver source data changed
- manual recovery requested
- reconnect recovery requested
- diversion/reroute detected

Heavy authority proof is serialized by the brain. If multiple expensive jobs are needed, they are queued and processed in priority order.

## 5. Priority Order

The brain must prioritize work in this order:

1. Safety/current-position state.
2. Departure airport local, departure terminal, and current center.
3. Current enroute center.
4. Next center within the 200nm lookahead window.
5. Arrival center, destination local, and arrival terminal inside the 200nm arrival wake distance.
6. Empty-current-polygon rechecks.
7. Deferred future-route preparation.
8. Diagnostics and cleanup work.

Far-future route polygons must not repeatedly consume live authority proof while they are outside the relevance window.

## 6. Worker Ownership

Workers are narrow and do not own workflow state.

- `RouteScopedMapWorker` builds the ordered route authority plan.
- `AirportLocalAuthorityWorker` resolves DEL/GND/TWR for one airport.
- `TerminalAuthorityWorker` resolves DEP/APP/TRACON ownership for departure or arrival.
- `CenterAuthorityFastPathWorker` performs cheap accepts for obvious center matches. It may accept, but it may not prove an unresolved polygon empty.
- `CenterAuthorityProofWorker` performs heavier evidence checks for unresolved current, next, or arrival-relevant center polygons.
- `BoardSnapshotWorker` builds module display snapshots from proven controller state.
- `UiPublishWorker` publishes the last proven safe snapshot without triggering proof.

Any worker that needs additional information must return a request to the brain. It must not independently call another expensive worker.

## 7. Module Ownership

Departure Module receives departure snapshots.

Enroute Module receives current and near-future center snapshots.

Arrival Module receives arrival snapshots.

The modules may display, format, and select from proven data. They may not trigger route proof, source proof, transceiver proof, or global authority scans.

## 8. Cache And Dirty Flags

The brain must use explicit cache keys and invalidation reasons.

Cache invalidation reasons include:

- flight plan changed
- route hash changed
- departure or destination changed
- VATSIM refile/reconnect recovery occurred
- diversion/reroute occurred
- source registry generation changed
- boundary generation changed
- controller feed relevant diff occurred
- transceiver feed relevant diff occurred
- current polygon changed
- next polygon entered 200nm lookahead
- arrival entered 200nm wake distance
- manual recovery requested

Controller and transceiver updates must first become targeted dirty flags. Dirty flags target route polygons, airport local groups, or terminal groups. They must not automatically trigger full-world authority recompute.

## 9. Diagnostics Requirement

Every expensive job must log:

- job name
- stage
- reason
- priority
- started/deferred/skipped/completed status
- duration
- cache hit or miss
- source generations used
- route map generation used
- accepted controller evidence summary
- rejected candidate reason when applicable

This is required so we can prove the brain is controlling the engine and not letting old logic leak through.

## 10. Implementation Blocks

### Block 1: Baseline And Instrumentation

Confirm current expensive job paths and improve diagnostics without changing behavior.

Acceptance:

- Full harness passes.
- Logs show job names, reasons, durations, cache status, and stage.
- No workflow behavior changes.

### Block 2: Typed Brain Work Model

Create typed work items, priorities, reasons, results, and snapshots.

Acceptance:

- Full harness passes.
- Work ordering and metadata are testable.
- Production behavior remains unchanged.

### Block 3: Scheduler In Shadow Mode

Install the scheduler as an observer before it controls production.

Acceptance:

- Full harness passes.
- Shadow logs show what the brain would have scheduled.
- We identify remaining multi-heavy-job ticks.

### Block 4: Route-Scoped Authority Plan

Build the active flight map from the global source catalog.

Acceptance:

- Known battle-test routes produce correct route polygon order.
- Global catalog remains read-only.
- Controller truth logic remains unchanged.

### Block 4A: Reroute And Diversion Rebuild Safety

Make the active flight map rebuildable.

Acceptance:

- Refile, reconnect, reroute, or diversion can rebuild route scope.
- Stale route controllers do not leak into the UI.
- Last proven safe snapshot remains visible while rebuilding.

### Block 5: Departure Ownership Cutover

Brain feeds Departure Module with departure snapshots.

Acceptance:

- DEL/GND/TWR/DEP/CTR departure cases pass.
- No controller cases pass.
- Battery and xPilot wake behavior remains correct.

### Block 6: Enroute Ownership Cutover

Brain feeds Enroute Module with current and next center snapshots only.

Acceptance:

- Known center failures remain fixed.
- Empty polygons are safely rechecked.
- Next center wakes at 200nm.
- Far-future polygons do not create recurring CPU work.

### Block 7: Arrival Ownership Cutover

Brain feeds Arrival Module at the 200nm arrival wake point.

Acceptance:

- Destination local, approach, and center cases pass.
- No controller cases pass.
- Existing 200nm arrival behavior remains intact.

### Block 8: Source Diff And Cache Discipline

Controller and transceiver feed changes become targeted dirty flags.

Acceptance:

- Relevant controller logon is detected.
- Irrelevant controller logon is ignored or deferred.
- Controller logoff updates without full route rebuild.
- Diagnostics show why refreshes ran.

### Block 9: Production Scheduler Cutover

Scheduler becomes the only production path for expensive work.

Acceptance:

- Full harness passes.
- Short-haul battle tests pass.
- Long-haul battle tests pass.
- CPU diagnostics show no recurring heavy spike cadence.
- No authority truth rules changed.

### Block 10: Legacy Audit And Cleanup

Remove stale self-trigger paths and old direct-call heavy work.

Acceptance:

- Full harness passes.
- Saved battle-test repository passes.
- Logs show expensive work is brain-scheduled.
- No known direct module proof path remains.

## 11. Completion Definition

This contract is complete only when:

- the brain owns all expensive scheduling
- modules consume snapshots only
- route authority work is scoped to the active flight map
- reconnect, refile, reroute, and diversion can rebuild active route scope
- source changes become targeted dirty flags
- no old display/module path can trigger heavy authority proof
- diagnostics prove every expensive job was scheduled by the brain
- harness and battle tests pass
- CPU spikes are reduced to acceptable, explainable, non-cadence events

Until those items are true, this contract is not complete.
