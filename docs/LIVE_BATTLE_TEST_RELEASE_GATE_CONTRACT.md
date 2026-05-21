# Live Battle Test Release Gate Contract

This contract defines the live-flight proof gates required before XVatsim is
handed to a beta streamer/tester or prepared for store release.

This contract does not replace the Authority Evidence Contract, Brain Cadence
Coordinator Contract, Reconnect Workflow Recovery Contract, or Preflight Route
Cache Contract. It is the release-readiness gate that proves those contracts
behave correctly in real X-Plane, xPilot, and VATSIM use.

Status update 2026-05-20: the Radio Board / Brain-owned runtime baseline has
one confirmed live pass. Battle Test #1, KONT -> KPDX / UPS3502, passed with
healthy steady-state performance and no old authority fallback work firing.
The active consecutive pass count is `1`.

Post-pass update 2026-05-20: a live runtime change was made after Battle Test #1
to add the Route Polygon Transition Worker. Battle Test #1 remains historical
evidence for build hash
`53B2D16788CA0A331BFC7AF00CF1BCF31299078313D75B29D7F240D7B31EC4A5`, but the
active streak for the newly installed runtime hash must restart from `0` until
the next live battle test passes on that hash:
`70592A76206F9E22B07A01EB5F1054BF5F228AEDF8A9CD0C7C26F79C9C954C51`.

Display-semantics update 2026-05-20: UAL2862 KIAD -> KSFO on hash
`70592A76206F9E22B07A01EB5F1054BF5F228AEDF8A9CD0C7C26F79C9C954C51` is logged
as a failed live battle test. Controller identification and filtering were
successful, but the UI display state did not clearly separate current-polygon
green rows from next-polygon orange rows with live distance intent. The active
consecutive pass count remains `0` until the Brain Display Intent Contract is
implemented and a new live battle test passes.

Display-intent implementation update 2026-05-20: the Brain Display Intent
Contract has been implemented and installed as hash
`40E32D881AEE270A64A16735E9A297DBDED2CBD11B2D7FB7BCF8DEBF5EC8C3B4`. The
active consecutive pass count remains `0`; the next live battle test is Battle
Test #1 for this hash.

## Purpose

XVatsim is not release-ready because a single test works. It is release-ready
only after consecutive live battle tests show the plugin is accurate,
recoverable, and healthy under real simulator conditions.

The battle-test process must prove:

- Relevant live controllers are displayed when the route enters or approaches
  their authority.
- Irrelevant live controllers are rejected without leaking onto the UI.
- Workflow transitions behave correctly: sleeping, ready, departure, enroute,
  arrival, and recovery.
- The UI does not get stuck or stale.
- Authority proof cadence stays controlled and does not cause visible sim
  stutters.
- Diagnostics explain what happened without requiring guesswork.

## Release Milestones

### Milestone 1: Beta Streamer Candidate

Requirement:

- Complete `5` consecutive valid live battle tests with no failures and healthy
  performance.

Outcome:

- After this milestone passes, a copy of the plugin may be given to the beta
  tester / streamer.

### Milestone 2: Store Release Candidate

Requirement:

- Complete `10` consecutive valid live battle tests with no failures and
  healthy performance.
- The `10` tests include the first `5`; the count resets to zero after any
  confirmed failure.

Outcome:

- After this milestone passes, begin the final full audit and store-release
  packaging pass.

## What Counts As A Valid Live Battle Test

A valid test must include:

- A named callsign.
- A filed VATSIM flight plan that XVatsim receives and parses.
- A known origin and destination.
- A meaningful route, preferably with at least one center, terminal, or airport
  authority condition to verify.
- A post-flight or post-session log review.

A test can be short. It does not always require a full gate-to-gate flight if
the behavior under test is connection, route parse, controller relevance, or
startup performance. A full flight is preferred when validating handoff timing,
arrival wake, or controller transition behavior.

## What Counts As A Failure

Any one of these resets the consecutive pass count to zero:

- A relevant online controller is missed.
- An irrelevant controller is displayed as route-relevant.
- A controller is accepted by a string/frequency shortcut that violates the
  Authority Evidence Contract.
- DEPARTURE, ENROUTE, ARRIVAL, READY, or sleep state becomes stuck or wrong.
- Reconnect/recovery does not restore the correct current-flight state.
- UI controller distance, active state, or display state becomes materially
  stale.
- XVatsim causes visible simulator stutters, freezes, or repeated heavy CPU
  spikes.
- Diagnostics are missing enough that we cannot explain the behavior.

## What Counts As An Invalid Test

Invalid tests do not count as pass or fail.

Examples:

- VATSIM does not return the filed flight plan.
- xPilot disconnects for an external/network reason.
- VATSIM data feed is unavailable or stale.
- The pilot intentionally changes route/callsign mid-test without documenting
  it as a recovery scenario.
- The test is stopped before the target behavior can be observed.

## Healthy Performance Definition

A live test has healthy performance when:

- No visible stutter or freeze is attributed to XVatsim.
- PluginAdmin does not show sustained heavy XVatsim flight-loop cost.
- `AuthorityRelevance` heavy proof does not repeatedly rebuild from ordinary
  aircraft movement.
- Route and authority proof spikes, if any, are isolated and explainable.
- Steady frames mostly use cached/light work and keep heavy proof work
  serialized by the brain scheduler.

## Battle-Test Logging Contract

Keep the long diagnostics log as the permanent black box. Do not erase it as
the primary workflow.

Add or maintain a per-flight battle-test log so each live test has a clean
evidence packet. Preferred files:

- `xvatsim_diagnostics.log`: long-running master history.
- `xvatsim_current_flight.log`: current callsign/route only.
- `battle_tests/YYYY-MM-DD_CALLSIGN_ORIG_DEST_PASS.log`: archived pass.
- `battle_tests/YYYY-MM-DD_CALLSIGN_ORIG_DEST_FAIL.log`: archived failure.

Until a dedicated per-flight log exists in code, each live review must filter
the master diagnostics by callsign, route, and tick range. The master log should
not be deleted unless explicitly done as a controlled maintenance action.

Each battle-test record should capture:

- Date.
- Callsign.
- Route.
- Aircraft/scenery context if relevant.
- Controllers expected or observed online.
- Workflow stages observed.
- Authority decisions observed.
- Performance notes.
- Verdict: `PASS`, `FAIL`, or `INVALID`.
- If failed, exact root cause or the next investigation target.

## Consecutive Count Rules

- `PASS`: increments the consecutive count by one.
- `FAIL`: resets the consecutive count to zero.
- `INVALID`: does not change the count.
- A pass can only be recorded after log review confirms there is no hidden
  authority or performance problem.
- If we make code changes after a pass, that pass remains historical evidence,
  but the active release-candidate streak should be evaluated honestly against
  the changed code. If the change touches authority, workflow, or performance
  cadence, restart the active streak unless we deliberately classify it as a
  logging/docs-only change.

## Final Store-Release Gate

After the `10` consecutive valid pass milestone:

- Run the full regression harness.
- Complete the final legacy/stale-code audit.
- Verify no old string-only or masking fallback paths can override the central
  evidence engine.
- Verify release packaging uses the same plugin binary that passed the live
  gate.
- Archive the final passing diagnostics and release package hash.

Only then may XVatsim be treated as store-release ready.

## Current Installed Test Hash

- Date: 2026-05-21
- Installed `XVatsim.xpl` SHA256:
  `FD21EB32A49B734B9CF6F5A843FC3D6707405C2DC64358DFCE98A2EC480253DA`
- Reason: Engineer 3 locked as the unconditional live refresh entry;
  display-intent distance is now non-destructive so route-entry fact truth is
  not overwritten by remaining-distance UI annotation; old plugin refresh,
  board cache, radio-board runtime, scheduled authority, and scheduled departure
  executor bodies have been removed instead of left quarantined; reset wiring now
  clears only brain-owned runtime/display publisher state; the plugin no longer
  links old departure/arrival/enroute display modules; the old core display
  board builder has been removed; Brain Display Intent is the display assembly
  owner and filters offline/no-frequency rows from the final UI snapshot;
  legacy departure/arrival/enroute collectors and plugin intermediate publisher
  boards no longer assert display ownership;
  accepted-completion board filtering and final-display completion marking now
  live in brain-owned runtime code instead of the plugin shell;
  `RunBrainOwnedPublisher` now owns publisher assembly, CTAF/UNICOM
  replacement, Brain Display Intent, phase snapshot publish state, and
  displayed-completion marking while the plugin supplies facts and diagnostics
  only; `RunBrainControllerRelevanceWorker` now lives in brain-owned source
  code, and brain-owned runtime owns relevance cache reuse and candidate
  completion cache updates while the plugin supplies input and diagnostics only;
  brain-owned runtime now owns radio-board reuse and commit state while the
  plugin runs the transceiver module as a fact producer;
  `brain/src/BrainRoutePolygonWorker.cpp` now owns route-sector hashing and
  route-polygon worker output shaping while the plugin runs the route-sector
  resolver as a fact producer; brain-owned route-polygon runtime now owns cache
  reuse, pending retry decisions, transition application, state commit, wake
  reason, and relevance invalidation;
  `brain/src/BrainRadioRangeWorker.cpp` now owns radio range worker output
  shaping while the plugin runs the transceiver resolver as a fact producer;
  brain-owned runtime now owns radio phase-gate storage and final published
  runtime snapshot commits;
  brain-owned controller relevance input shaping now comes from the
  brain-owned route/radio context;
  brain-owned publisher input shaping now comes from brain-owned route context
  plus plugin-supplied relevance and CTAF facts;
  brain-owned runtime now turns CTAF lookup facts plus radio state into
  CTAF/UNICOM board stations, including tuned-state decisions, while the plugin
  only adapts the CTAF module output into neutral facts;
  brain-owned runtime now owns standby-assist target selection and display flag
  application while the plugin only performs the COM1 standby radio-write side
  effect and feeds the result back to the brain;
  final published runtime commits now happen through
  `CommitBrainOwnedPublishedRuntimeFromPublisherOutput` after the
  standby-assisted final display board is available, keeping runtime state and
  rendered UI aligned;
  `DecideBrainOwnedOverlayWake` now owns overlay wake/hide/reason decisions
  from shell facts while the plugin only updates the X-Plane overlay window;
  workflow/recovery implementation now lives in brain-owned source
  (`brain/src/BrainWorkflow.cpp`) while `core/WorkflowEngine.h` remains only as
  a compatibility shim;
  the unused pre-Engineer-3 plugin workflow wrapper and unused distance wrappers
  have been removed; normal flight-context lock/refresh decisions now live in
  `brain/src/BrainWorkflow.cpp` through `UpdateFlightContextFromNetworkPlan`,
  while the plugin only applies the brain output and reset/invalidate
  side effects; manual diversion/revert flight-context retarget decisions now
  live in `brain/src/BrainWorkflow.cpp` through
  `RetargetFlightContextToNetworkPlan`, while the plugin only applies the
  returned context and reset/invalidate side effects; brain-owned runtime now
  owns enroute initial display-hold state and timing while the plugin supplies
  current time/hold duration and passes the returned active flag into overlay
  wake; brain-owned runtime now owns active-flight flight-plan sampling cadence
  and cached flight-plan snapshot state while the plugin runs
  `FlightPlanSampler` only when the brain requests a fresh sample;
  `ResolveXPilotSessionBoundary` now owns xPilot disconnect/reconnect/callsign
  boundary decisions while the plugin applies the returned preserve/reset and
  recovery flags; `ResolveAircraftRuntimeBoundary` now owns
  invalid-aircraft-state and cold/dark boundary decisions while the plugin
  applies the returned reset/latch flags and hides/renders the X-Plane overlay;
  unused plugin-side legacy authority quarantine and old radio-board readiness
  helpers have been removed; brain-owned runtime now owns cruise target state,
  filed/current target command decisions, source-plan invalidation, gate
  dwell/reached tracking, and cruise header text while the plugin only handles
  command/status side effects; brain-owned runtime now owns workflow progress
  latches for departure release, arrival wake, and airborne-since timing while
  the plugin only builds/commits workflow state through brain helpers;
  brain-owned runtime now owns the xPilot-seen latch used by overlay
  wake/disconnect behavior; brain-owned runtime now owns active flight context
  storage while the plugin commits and clears brain-returned context through
  brain helpers instead of carrying separate `gFlightContext` shell state;
  brain-owned runtime now provides a cache-reset path that preserves active
  flight context during new-context runtime clears; brain-owned runtime now
  owns xPilot session boundary state, current-flight recovery request latches,
  and aircraft cold/dark/invalid-state boundary latches while the plugin only
  applies side effects from brain decisions; brain-owned runtime now owns the
  standby-assist write latch and decides when the plugin should perform the
  COM1 standby write side effect; brain-owned runtime now owns diversion
  override source-key state and decides whether the manual diversion override
  still belongs to the current source VATSIM flight plan; brain-owned runtime
  now owns preflight route-cache applied-plan state and decides when the plugin
  should clear, validate, or apply the route resolver cache; brain-owned
  runtime now owns display override mode and preserves it across runtime resets;
  brain-owned runtime now owns manual query/transient status display state and
  expiry timing; brain-owned runtime now owns controller-message display state,
  sequence tracking, cached recall, visibility, and clear/ack behavior; plugin
  diagnostics no longer reconstruct a shadow brain scheduler from old
  diagnostic job names, and the plugin depends on the brain work model header
  instead of the scheduler header; brain-owned runtime now owns the latest
  sampled aircraft, pilot identity, flight-plan, and network-plan fact
  snapshots while the plugin only commits sampled facts and reads them from
  brain-owned state for command side effects; brain-owned runtime now owns
  pending overlay text-entry mode for manual CTAF and diversion prompts while
  the plugin only opens and reads the overlay text box; unused plugin-side
  hash/active-transceiver helpers from retired board and radio refresh paths
  have been removed; `BrainOrchestrator::BuildOverlayViewModel` is now a
  stateless brain API and the plugin no longer carries a global `gBrain`
  object; old departure/arrival/enroute board modules are now gated behind
  `XVATSIM_BUILD_REGRESSION_HARNESS` so plugin-only builds do not compile them
  as part of the live module stack; those harness-only legacy board libraries
  are now named `XVatsimHarnessLegacyArrival`,
  `XVatsimHarnessLegacyDeparture`, and `XVatsimHarnessLegacyEnroute` so they are
  not mistaken for live plugin modules; old departure/arrival/enroute board
  headers now require `XVATSIM_ENABLE_HARNESS_LEGACY_BOARD_MODULES`, so
  accidental live includes fail at compile time; plugin diagnostics state is now
  grouped under one shell-owned `PluginDiagnosticsState`, and refresh timing logs say
  `radioRange` instead of `activeTx`; brain-owned runtime now owns provisional
  relevance plus workflow phase selection through
  `ResolveBrainOwnedWorkflowSelection`; duplicate final-display runtime storage
  under `activeBoardSnapshot` has been removed, leaving
  `finalDisplaySnapshot` as the single final UI board state; stale plugin-local
  departure/arrival/enroute board variables have been removed from the Engineer
  3 refresh shell; final UI display now uses `FinalDisplaySnapshot` /
  `FinalDisplayStationSnapshot` instead of reusing `ModuleBoardSnapshot`; Brain
  Display Intent keeps accepted module boards raw and applies UI-only
  annotations only while building `FinalDisplaySnapshot`; Brain Display Intent
  now builds enroute display rows directly as `FinalDisplayStationSnapshot`
  entries instead of staging display-shaped rows through a temporary
  `ModuleBoardSnapshot`; raw `BoardStationSnapshot` no longer carries `next`
  or `standby`, leaving those UI-only flags on `FinalDisplayStationSnapshot`;
  raw `BoardStationSnapshot` no longer carries `annotation`, leaving UI text
  annotations on `FinalDisplayStationSnapshot`; raw `BoardStationSnapshot` no
  longer carries `displayRelation`, leaving relation on candidate completions
  and final display rows; raw `ModuleBoardSnapshot` no longer carries
  `displayStations`; brain-owned runtime now owns network-plan identity-key
  construction while the plugin shell only consumes the brain-owned key;
  brain-owned runtime audit map updated.
- Active live streak: remains `0`; the next valid live test is Battle Test #1
  for this installed hash.
