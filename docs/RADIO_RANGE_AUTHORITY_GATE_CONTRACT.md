# Radio-Range Authority Gate Contract

Date locked: 2026-05-19

Status: primary architecture contract for Engineer 3.

Current block status:

- Block 1: complete.
- Block 2: complete. The typed radio-reachable snapshot model exists, is part
  of `XVatsimBrain`, and is covered by
  `radio_reachable_snapshot_groups_and_hashes.scn`.
- Block 3: complete. The source seam consumes an AFV/transceiver radio-range
  snapshot plus fresh controller feed truth, while local xPilot integration
  remains limited to session/private-message datarefs and does not scrape UI.
- Block 4: complete at the central brain gate layer. The phase gate filters
  radio-reachable candidates by workflow stage before verifier/module
  consumption.
- Block 5: complete. The live authority scheduler now builds a verifier feed
  from the phase-gated radio snapshot and passes that small feed into the
  existing evidence engine instead of the full controller feed.
- Block 6: complete. The phase snapshot publisher stores last-proven
  displayable boards per workflow phase and can reuse only the matching phase's
  last proven snapshot while authority verification is pending. The live UI
  consumes the published board and does not trigger proof.

Guiding principle: do not scan the world to find what the radio already knows.

Engineer 3 replaces the failed Engineer 1 and Engineer 2 discovery models.

Engineer 1 failed because it used brittle string authority as a hard pass/fail.
Engineer 2 failed because it used a powerful evidence engine as the primary live
discovery model and created unacceptable X-Plane CPU spikes.

Engineer 3 keeps the useful evidence work, but puts a radio-reachable gate in
front of it. The plugin should behave like this:

1. xPilot, or an xPilot-equivalent radio-range snapshot, identifies controllers
   the aircraft can actually reach.
2. XVatsim filters that small reachable list by flight phase and route
   relevance.
3. The evidence engine verifies only changed, phase-relevant candidates.
4. The UI displays only the last proven relevant snapshot.

## Purpose

XVatsim should provide a cleaner, route-aware version of the controller list the
pilot can already reach through xPilot.

xPilot's mindset is: show what is around the aircraft.

XVatsim's mindset is: show only what is around the aircraft and relevant to this
flight.

The goal is to return performance close to the low-CPU Engineer 1 behavior while
keeping the trustworthiness learned from Engineer 2.

## Non-Negotiables

- No full authority proof may run inside the X-Plane flight loop.
- No route movement may trigger broad controller-truth recomputation.
- No module may independently scan live controllers, polygons, or authority
  sources.
- The active flight map is used for relevance filtering, not candidate
  discovery.
- The radio-reachable snapshot is the primary live candidate source.
- Evidence proof runs only for changed candidates that pass the phase gate.
- UI displays last proven safe state while proof is pending.
- Arrival prewatch begins at 200nm and is destination-only.
- Terminal authority must be destination-scoped before it can appear in Arrival.
- If a supported xPilot data seam exists, use it. If not, XVatsim may build an
  xPilot-equivalent radio-reachable snapshot from VATSIM/AFV data. XVatsim must
  not depend on scraping xPilot's visual UI.

## Core Data Model

### Active Flight Map

Built once from the filed VATSIM flight plan, then rebuilt only for reconnect,
re-file, reroute, diversion, or manual recovery.

The active flight map contains:

- ordered route authority polygons
- current, next, and arrival polygon identity
- polygon sequence numbers
- route entry and exit distances when available
- destination airport identity
- destination terminal/local metadata
- last proven controller state for each relevant phase

The active flight map does not search all live controllers by itself.

### Radio-Reachable Controller Snapshot

This is the small candidate list that replaces world scanning.

Each candidate should include:

- callsign
- frequency
- facility group: `CTR`, `APP_DEP`, `TWR`, `GND`, `DEL`, `ATIS`, or `OTHER`
- source: `XPILOT_SEAM`, `AFV_RADIO_RANGE`, or another explicit source
- optional radio range / distance metadata
- first-seen and last-seen timestamps
- a stable snapshot hash

The snapshot is recomputed only when the reachable controller list changes,
radio state changes, xPilot reconnects, VATSIM data changes, or the aircraft
moves far enough to affect radio reachability.

### Candidate Verification

The Engineer 2 evidence engine is retained as a verifier only.

For each phase-relevant reachable candidate, verification may use:

- active flight map polygon relevance
- VATSpy FIR ownership
- VATGlasses static or dynamic ownership
- AFV/transceiver geography
- frequency ownership
- source-backed terminal/TRACON ownership
- special-sector data
- duplicated ATIS-derived ownership when source-backed

The verifier must never ask, "Which controllers in the world might matter?"

It may only answer, "Does this reachable or destination-targeted candidate
belong to this flight phase?"

## Phase Gates

### Ready

Before the flight plan is available:

- Do not run authority proof.
- Do not build broad route authority.
- Wait for VATSIM flight plan, xPilot connection, battery, and valid aircraft
  state.

### Departure

Departure may evaluate:

- departure airport `DEL`
- departure airport `GND`
- departure airport `TWR`
- departure CTAF fallback
- departure terminal `APP_DEP`
- current/departure center `CTR`

Departure must ignore:

- unrelated reachable towers
- unrelated reachable ground/delivery
- unrelated terminal controllers
- future route centers not yet reachable unless already proven cheaply from the
  active flight map

### Enroute

Enroute may evaluate:

- reachable `CTR` candidates
- current route polygon
- next route polygon
- route polygons within the configured lookahead window

Enroute must ignore:

- reachable `GND`
- reachable `DEL`
- reachable `TWR`
- reachable `ATIS`
- reachable `APP_DEP` unless a future contract explicitly proves an enroute
  use case

If no reachable `CTR` candidates exist, Enroute does almost nothing.

### Arrival

Arrival wakes at 200nm from destination.

Arrival may evaluate:

- reachable `CTR` candidates for the current/arrival center
- destination airport `TWR`
- destination airport `GND`
- destination airport `DEL` when useful
- destination CTAF fallback
- destination-scoped `APP_DEP`
- destination `ATIS`, if later added to UI behavior

Arrival must not display a terminal controller unless it is destination-scoped.

Example: `LAX_S_DEP` must not display for a `KONT` arrival unless source data
explicitly proves it owns the destination arrival terminal authority for that
flight. Broad geographic proximity or LAX terminal ownership is not enough.

## Refresh Cadence

Every frame:

- aircraft/radio/display-only state
- no proof work

Every light cadence:

- consume last proven snapshots
- update visible UI state

When radio-reachable snapshot changes:

- diff added/removed/changed candidates
- group candidates by facility
- apply phase gate
- enqueue verification only for phase-relevant changed candidates

When flight phase changes:

- reapply phase gates to the last radio-reachable snapshot
- do not rescan the world

When entering Arrival 200nm:

- run destination-only local/terminal prewatch
- do not run broad terminal discovery

## Performance Contract

The target is Engineer 1-level runtime cost with Engineer 2-level verification
quality.

Performance requirements:

- Normal flight-loop cost should stay in the low microsecond range.
- Any proof work above `10ms` must be outside the visible flight loop or
  explicitly sliced.
- Any refresh over `50ms` is a failure unless proven to be unrelated to XVatsim.
- Any authority proof over `100ms` during live flight is a contract violation.
- Repeated proof spikes are a battle-test failure even if the UI result is
  correct.

## Failure Rules

This contract fails if:

- A full authority proof runs from ordinary route movement.
- A full authority proof runs because the UI refreshed.
- A terminal controller appears in Arrival without destination-scoped proof.
- A center controller is dropped because the remaining route collapses to
  `ACFT` near touchdown.
- Radio-reachable candidates are ignored and broad world scanning becomes the
  primary discovery path again.
- The old string-authority engine becomes a hard pass/fail path again.

## Implementation Blocks

### Block 1: Contract Checkpoint

- Mark Engineer 2 as terminated as the primary runtime architecture.
- Preserve Engineer 2 evidence code as a verifier.
- Freeze live battle streak at `0` until Engineer 3 has its own clean test
  baseline.
- Status: complete.

### Block 2: Radio-Reachable Snapshot Model

- Create a typed snapshot model for reachable controllers.
- Group candidates by facility.
- Include frequency, callsign, source, timestamps, and stable hash.
- Add diagnostics showing snapshot size and change reason.
- Status: complete. Harness coverage confirms grouping, counts, candidate
  summaries, and stable-hash change detection.

### Block 3: Snapshot Source Seam

- Investigate whether xPilot exposes a supported data seam.
- If supported, consume it.
- If unsupported, build an xPilot-equivalent snapshot from VATSIM/AFV
  transceiver/radio-range data.
- Do not scrape xPilot's visual UI.
- Status: complete for the non-UI source seam. Current xPilot bridge exposes
  connection and private-message datarefs only, so Engineer 3 uses the existing
  AFV/transceiver radio-range resolver as the xPilot-equivalent source. Harness
  coverage proves non-reachable controllers are ignored and stale radio-range
  snapshots cannot produce candidates.

### Block 4: Phase Gate

- Add a central phase gate that decides which facility groups matter.
- Departure, Enroute, and Arrival modules must consume gated candidates only.
- Enroute must ignore non-center candidates.
- Arrival must be destination-only for local and terminal candidates.
- Status: complete for the central gate. Harness coverage proves Departure and
  Arrival keep local/terminal/center candidates, Enroute keeps only center
  candidates, and Ready/None keeps nothing. Destination-only arrival proof is
  enforced by the verifier adapter in Block 5, because it requires active flight
  map authority context.

### Block 5: Verifier Adapter

- Wrap the existing evidence engine so it verifies one candidate, or a small
  candidate diff, against the active flight map.
- Remove or quarantine any path that asks the verifier to scan all live
  controllers.
- Status: complete for the live verifier feed. The adapter can build changed
  candidate diffs, can select zero work for unchanged snapshots, and the live
  authority scheduler now uses the phase-gated radio snapshot as the only
  controller feed passed into `ResolveAuthorityRelevance`. If the radio gate is
  unavailable or stale, the verifier receives an unavailable empty feed instead
  of falling back to the full live controller list.

### Block 6: Snapshot Publisher

- Publish last proven phase snapshots for UI consumption.
- UI must never trigger proof.
- UI must keep last safe state while new candidate verification is pending.
- Status: complete. `PhaseSnapshotPublisher` is part of `XVatsimBrain`, harness
  coverage proves same-phase reuse and phase isolation, and the live plugin
  publishes the display board before overlay build without adding any discovery
  or proof trigger.

### Block 7: Regression Harness

Add harness scenarios for:

- empty xPilot/radio snapshot produces CTAF-only departure
- reachable departure tower accepted only for matching departure airport
- reachable unrelated tower ignored
- reachable center accepted for current/next route polygon
- reachable center rejected when off route
- arrival 200nm destination tower accepted
- arrival 200nm unrelated `APP_DEP` rejected
- `LAX_S_DEP` rejected for `KONT` arrival
- touchdown keeps last proven center until intentional workflow clear
- no full authority proof from ordinary route movement
- Status: complete. Added 10 saved `block7_*.scn` scenarios covering the
  regression list above, including positive and negative radio-gated authority
  cases, publisher workflow-clear behavior, and ordinary route movement staying
  on light scheduled work. Full harness passed with 232 scenarios.

### Block 8: Live Battle Gate

Only after Blocks 1-7 pass:

- Start new live battle streak at `0`.
- Require 5 consecutive live battle passes before streamer/beta handoff.
- Require 10 consecutive live battle passes before final audit/store package.

## Audit Requirement

After Engineer 3 is implemented and battle-tested, perform a full stale-code
audit.

The audit must remove or quarantine:

- Engineer 1 string-only authority pass/fail seams
- Engineer 2 world-scan authority discovery paths
- UI-triggered proof paths
- route-movement-triggered full proof paths
- stale terminal fallback paths
- broad arrival terminal authority that is not destination-scoped

Until that audit is complete, Engineer 3 is not store-ready.
