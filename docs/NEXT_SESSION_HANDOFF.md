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

`CEF8BFF73D2436F7B251CC1F15BB7ED44A76D33B3440AC05682AD2134A3C0C50`

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
- Legacy departure/arrival/enroute collectors may produce fact rows for
  harness coverage, but only Brain Display Intent builds final display rows.
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
- Brain-owned runtime now owns xPilot session boundary state,
  current-flight recovery request latches, and aircraft cold/dark/invalid-state
  boundary latches. The plugin no longer carries those as separate globals.
- Brain-owned runtime now owns the standby-assist write latch and decides when
  the plugin should perform the COM1 standby write side effect.
- Brain-owned runtime now owns diversion override source-key state and decides
  whether a manual diversion override still belongs to the current source
  VATSIM flight plan.
- Brain-owned runtime now owns preflight route-cache applied-plan state and
  decides when the plugin should clear, validate, or apply the route resolver
  cache.
- Brain-owned runtime now owns display override mode (`Auto`, forced open,
  forced sleep) and preserves it across runtime resets.
- Brain-owned runtime now owns manual query/transient status display state and
  expiry timing.
- Brain-owned runtime now owns controller-message display state, sequence
  tracking, cached recall, visibility, and clear/ack behavior.
- Plugin diagnostics no longer build or log a shadow brain scheduler from old
  diagnostic job names. The plugin includes the brain work model types it uses,
  but no longer depends on `BrainWorkScheduler.h`.
- Brain-owned runtime now owns the latest sampled aircraft, pilot identity,
  flight-plan, and network-plan fact snapshots. The plugin commits sampled
  facts to the brain and command handlers read them from brain-owned state
  instead of carrying separate `gLast*Snapshot` globals.
- Brain-owned runtime now owns pending overlay text-entry mode for manual CTAF
  and diversion prompts. The plugin opens/reads the text box, while the brain
  stores and consumes which command the submission belongs to.
- Removed unused plugin-side hash/active-transceiver helper functions left over
  from the retired board and radio refresh paths.
- `BrainOrchestrator::BuildOverlayViewModel` is now a stateless brain API, and
  the plugin no longer carries a global `gBrain` object.
- The old departure/arrival/enroute board modules are now gated behind
  `XVATSIM_BUILD_REGRESSION_HARNESS` in CMake. They still build for harness
  coverage, but plugin-only builds no longer compile them as part of the live
  module stack.
- The old departure/arrival/enroute board libraries are now named
  `XVatsimHarnessLegacyArrival`, `XVatsimHarnessLegacyDeparture`, and
  `XVatsimHarnessLegacyEnroute`. They are harness legacy coverage targets, not
  live Engineer 3 modules.
- The old departure/arrival/enroute board headers now require
  `XVATSIM_ENABLE_HARNESS_LEGACY_BOARD_MODULES`; accidental live includes fail
  at compile time.
- Plugin diagnostics state is now grouped under one shell-owned
  `PluginDiagnosticsState`, and refresh timing logs say `radioRange` instead of
  the older `activeTx` label.
- Brain-owned runtime now owns the provisional relevance pass used for workflow
  phase selection through `ResolveBrainOwnedWorkflowSelection`. The plugin
  supplies facts and receives the brain-owned phase decision plus provisional
  boards.
- Removed the duplicate `activeBoardSnapshot` runtime field. Brain-owned
  runtime stores the final brain-approved UI board as `finalDisplaySnapshot`,
  and the plugin/UI path names that board as final display.
- Removed stale plugin-local departure/arrival/enroute board variables from the
  Engineer 3 refresh shell. The plugin now keeps only the final display board it
  must pass through standby assist and UI rendering.
- Added explicit final-display structs:
  `FinalDisplayStationSnapshot` and `FinalDisplaySnapshot`. Brain Display
  Intent, Phase Snapshot Publisher, standby assist, overlay wake, runtime final
  display storage, and `BrainOrchestrator` now consume the display-specific
  snapshot instead of reusing `ModuleBoardSnapshot` as the final UI board.
- Brain Display Intent now keeps accepted module boards raw in its output and
  assembles `FinalDisplaySnapshot` separately. UI annotations and
  remaining-distance formatting no longer overwrite the publisher/runtime
  module board snapshots.
- Brain Display Intent now builds enroute display rows directly as
  `FinalDisplayStationSnapshot` entries, so display shaping no longer stages
  through a temporary `ModuleBoardSnapshot`.
- Raw `BoardStationSnapshot` no longer carries `next` or `standby`; those
  UI-only flags now live only on `FinalDisplayStationSnapshot`.
- Raw `BoardStationSnapshot` no longer carries `annotation`; UI text
  annotations now live only on `FinalDisplayStationSnapshot`.
- Raw `BoardStationSnapshot` no longer carries `displayRelation`; relevance
  records relation on completions and Display Intent infers final UI relation
  from fact fields.
- Raw `ModuleBoardSnapshot` no longer carries `displayStations`; board display
  ownership exists only in `FinalDisplaySnapshot`.

Regression harness status for this code:

- Release build passed.
- Full harness passed: `234 / 234`.

## Live Battle Test Gate

Read before counting tests:

- `docs/LIVE_BATTLE_TEST_RELEASE_GATE_CONTRACT.md`

Active live streak for the current installed hash:

- `0`

Next valid live test is Battle Test #1 for hash:

`CEF8BFF73D2436F7B251CC1F15BB7ED44A76D33B3440AC05682AD2134A3C0C50`

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
