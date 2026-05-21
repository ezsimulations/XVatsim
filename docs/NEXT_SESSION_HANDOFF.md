# Next Session Handoff

Date noted: 2026-05-20

## Read First

Before touching code, read:

- `docs/BRAIN_OWNED_RUNTIME_CONTRACT.md`

This is the active top-level architecture contract and must be treated as the
bible for runtime work.

Core rule:

`Brain decides. Modules produce facts. UI displays brain-approved facts.`

No module may:

- talk directly to the UI
- call another live module
- decide workflow phase
- decide display truth
- self-trigger heavy proof
- scan broad/world data unless the brain explicitly scheduled that job
- reprocess completed work when its input hash has not changed

If any proposed change violates this, stop and realign before coding.

## Current Runtime Direction

XVatsim is now on the brain-owned / radio-board runtime path.

The live runtime goal is intentionally simple:

- Build the route polygon context from the filed flight plan.
- Build a reachable-controller radio board.
- Let the brain decide which phase matters now.
- Send only phase-relevant reachable candidates to relevance.
- Let relevance return accepted/rejected facts only.
- Let the brain publisher assemble the final UI snapshot.
- Let the UI render only the brain-approved snapshot.
- If the radio board, route polygon, phase, and completion records are
  unchanged, everyone stays idle.

Engineer 1 string-only authority and Engineer 2 broad live-loop authority proof
are not primary runtime discovery models. Old code may remain compiled only if
it cannot self-trigger and is behind an explicit brain-scheduled fallback.

## Current Installed Test Build

Installed X-Plane plugin:

`C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl`

Current installed SHA256:

`A16D4BA7CA20709A211C3D8624CBABC059FE86940737740D8EA803A1E7CA3A1C`

Reason for this hash:

- Brain-owned display boundary cleanup.
- Relevance no longer builds final display boards.
- Relevance no longer marks candidates as displayed.
- Brain publisher owns accepted-completion filtering, display intent, final
  board assembly, and displayed/hidden completion state.
- Engineer 3 is now the unconditional live refresh entry.
- Brain Display Intent keeps absolute route-entry distance as fact truth and
  writes remaining distance only as display annotation.
- Added `docs/BRAIN_OWNED_RUNTIME_AUDIT.md` as the active cleanup map.
- The old live refresh body, old board collectors, old radio-board runtime, and
  old scheduled authority/departure executors have been deleted from
  `plugin/src/XVatsimPlugin.cpp`.
- Plugin reset wiring now clears only brain-owned runtime state and the brain
  display publisher cache; legacy plugin-owned board/authority caches are gone.
- The plugin no longer owns or links the old departure/arrival/enroute display
  module libraries.
- `core::workflow::BuildDisplayBoard` has been removed; final display assembly
  is owned by `brain/src/BrainDisplayIntent.cpp`.
- The regression harness now checks display output from Brain Display Intent,
  not the retired core display builder.
- Brain Display Intent filters offline/no-frequency rows out of the final UI
  snapshot while preserving them as worker facts/diagnostics.
- Legacy departure/arrival/enroute collectors no longer assert
  `displayStations`; they may produce fact rows for harness coverage, but only
  Brain Display Intent marks a final board displayable.
- Accepted-completion board filtering and final-display completion marking now
  live in `brain/src/BrainOwnedRuntime.cpp`, not the plugin shell.
- `RunBrainOwnedPublisher` in `brain/src/BrainOwnedRuntime.cpp` now owns
  publisher assembly: accepted-completion filtering, CTAF/UNICOM replacement,
  Brain Display Intent, phase snapshot publish state, and displayed-completion
  marking. The plugin supplies facts and diagnostics only.
- `RunBrainControllerRelevanceWorker` now lives in
  `brain/src/BrainControllerRelevanceWorker.cpp`; the plugin supplies worker
  input and diagnostics only. Brain-owned runtime owns relevance cache reuse
  and candidate completion cache updates.
- Brain-owned runtime owns radio-board reuse and commit state through
  `TryReuseBrainOwnedRadioBoard` and `CommitBrainOwnedRadioBoardRefresh`; the
  plugin still runs the transceiver module as a fact producer.
- `brain/src/BrainRoutePolygonWorker.cpp` now owns route-sector hashing and
  route-polygon worker output shaping; the plugin still runs the route-sector
  resolver as a fact producer.
- `brain/src/BrainRoutePolygonWorker.cpp` also owns route-polygon cache reuse,
  pending retry decisions, transition application, route-state commit, wake
  reason, and relevance invalidation.
- `brain/src/BrainRadioRangeWorker.cpp` now owns radio range worker output
  shaping; the plugin still runs the transceiver resolver as a fact producer.
- `brain/src/BrainOwnedRuntime.cpp` owns radio phase-gate storage and final
  published runtime snapshot commits through `RunBrainOwnedRadioPhaseGate` and
  `CommitBrainOwnedPublishedRuntime`.
- `BuildBrainOwnedControllerRelevanceInput` now shapes Controller Relevance
  worker inputs from brain-owned route/radio context.
- `BuildBrainOwnedPublisherInputFromFacts` now shapes Brain Publisher input
  from brain-owned route context plus plugin-supplied relevance and CTAF facts.
- Brain-owned runtime now turns CTAF lookup facts plus radio state into
  CTAF/UNICOM board stations, including the tuned flag; the plugin no longer
  builds CTAF/UNICOM display stations.
- Brain-owned runtime now owns standby-assist target selection and display flag
  application. The plugin only performs the X-Plane COM1 standby write side
  effect, then feeds the result back to the brain.
- `CommitBrainOwnedPublishedRuntimeFromPublisherOutput` now commits the final
  brain-approved board after standby assist has been applied, so runtime state
  and the rendered UI snapshot stay aligned.
- `DecideBrainOwnedOverlayWake` now owns overlay wake/hide/reason decisions
  from shell facts. The plugin still updates X-Plane window state, but it no
  longer decides whether the UI should wake.
- Workflow/recovery implementation now lives in
  `brain/src/BrainWorkflow.cpp`; `core/WorkflowEngine.h` is only a compatibility
  shim for existing callers.
- The unused pre-Engineer-3 plugin workflow wrapper and unused distance wrappers
  have been removed from `plugin/src/XVatsimPlugin.cpp`.
- `UpdateFlightContextFromNetworkPlan` now owns normal flight-context
  lock/refresh decisions, including departure confirmation, callsign/route
  change relock, route text refresh, and authoritative/missing airport
  coordinate refresh. The plugin only applies the brain output and performs
  reset/invalidate side effects.
- `RetargetFlightContextToNetworkPlan` now owns manual diversion/revert
  flight-context retarget decisions. The plugin only applies the returned
  context and reset/invalidate side effects.
- Brain-owned runtime now owns enroute initial display-hold state and timing.
  The plugin supplies current time/hold duration and passes the returned active
  flag into brain overlay wake.
- Brain-owned runtime now owns active-flight flight-plan sampling cadence and
  cached flight-plan snapshot state. The plugin runs `FlightPlanSampler` only
  when the brain requests a fresh sample.
- `ResolveXPilotSessionBoundary` now owns xPilot disconnect/reconnect/callsign
  boundary decisions. The plugin applies the brain-returned preserve/reset and
  recovery flags.
- `ResolveAircraftRuntimeBoundary` now owns invalid-aircraft-state and
  cold/dark boundary decisions. The plugin applies the brain-returned reset and
  latch flags, then hides/renders the X-Plane overlay.
- Removed unused plugin-side legacy authority quarantine and old radio-board
  readiness helpers.
- Brain-owned runtime now owns cruise target state, filed/current target
  command decisions, source-plan invalidation, gate dwell/reached tracking, and
  cruise header text. The plugin only handles command/status side effects.
- Brain-owned runtime now owns workflow progress latches for departure release,
  arrival wake, and airborne-since timing. The plugin only builds/commits
  workflow state through brain helpers.
- Brain-owned runtime now owns the xPilot-seen latch used by overlay
  wake/disconnect behavior.
- Brain-owned runtime now owns the active flight context storage. The plugin
  commits and clears brain-returned context through brain helpers instead of
  carrying `gFlightContext` as separate shell state.
- Brain-owned runtime now has a cache-reset helper that preserves the active
  flight context, so new-context runtime cache clears do not erase the locked
  flight.

Regression harness status for this code:

- Release build passed.
- Full harness passed: `234 / 234`.

## Live Battle Test Gate

Read before counting tests:

- `docs/LIVE_BATTLE_TEST_RELEASE_GATE_CONTRACT.md`

Active live streak for the current installed hash:

- `0`

Next valid live test is Battle Test #1 for hash:

`A16D4BA7CA20709A211C3D8624CBABC059FE86940737740D8EA803A1E7CA3A1C`

Rules:

- A valid pass increments the active streak.
- A confirmed fail resets the active streak to `0`.
- An invalid test does not change the streak.
- If runtime authority, workflow, display, or performance cadence code changes,
  restart the active streak unless the change is explicitly docs/logging only.

## Brain-Owned Runtime Audit

The contract cleanup has started because live testing exposed failures caused
by old/runtime display ownership mixing with Engineer 3 state.

Read:

- `docs/BRAIN_OWNED_RUNTIME_AUDIT.md`

The audit is not permission for random refactors. Cleanup must proceed in small
verified slices:

- protect Engineer 3 first
- keep broad authority proof out of ordinary UI refresh
- separate fact state from display state
- move brain-owned runtime seams out of `plugin/src/XVatsimPlugin.cpp`
- quarantine old Engineer 1/2 display and authority paths
- run focused tests, Release build, and full harness after each slice

## What To Check During Each Live Test

Confirm and log:

- XVatsim receives the VATSIM flight plan.
- Correct workflow stage: Departure, Enroute, Arrival, Ready, or sleep.
- Reachable relevant controllers are displayed.
- Reachable irrelevant controllers are filtered.
- Current-polygon centers display as current/green.
- Next-polygon centers display as next/orange with distance intent.
- Arrival wakes at 200nm and only then shows destination local/TRACON authority.
- Unchanged radio board goes idle.
- No repeated heavy authority fallback.
- No visible stutters, freezes, or sustained PluginAdmin FPS hit.

If a test fails:

- Pull logs.
- Identify whether the failure was controller relevance, route polygon,
  workflow phase, display intent, UI rendering, reconnect/recovery, or
  performance.
- Fix only the responsible contract block.
- Re-run harness.
- Install the new build.
- Reset the active live streak for the new runtime hash.

## Current Caution

The repo contains older code from previous architecture attempts.

That is not a reason to start random cleanup now.

Until the 5-pass live gate is reached, protect the current runtime path:

- No broad refactor.
- No speculative cleanup.
- No adding back Engineer 2 as a live discovery loop.
- No module-to-module shortcuts.
- No UI updates from modules.

After 5 consecutive valid passes, perform the deliberate cleanup/audit with the
brain-owned runtime contract open.
