# Live Battle Test Release Gate Contract

This contract defines the live-flight proof gates required before XVatsim is
handed to a beta streamer/tester or prepared for store release.

This contract does not replace the Authority Evidence Contract, Brain Cadence
Coordinator Contract, Reconnect Workflow Recovery Contract, or Preflight Route
Cache Contract. It is the release-readiness gate that proves those contracts
behave correctly in real X-Plane, xPilot, and VATSIM use.

Current live-gate update 2026-05-27: installed hash
`81CC5DD85D579A89257670F51A0F477EAE825F5D78A9F360FBF2AE1979EEF96A` has five
confirmed valid live battle-test passes. Battle Test #1 was UPS325 KSDF ->
MMMX. Battle Test #2 was UA1224 / UAL1224 KLAX -> KLAS. Battle Test #3 was
UAL1045 KSFO -> KGEG. Battle Test #4 was UAL1005 KGEG -> KDEN. Battle Test #5
was UPS293 EGSS -> KSDF. The active live streak is now `5`; Milestone 1 /
Beta Streamer Candidate live gate is complete. The next work is the deliberate
brain-owned runtime audit and market-preparation process with the contract open.
This current-state note supersedes older historical hash/streak lines below.

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

Non-counting validation update 2026-05-21: UPS3511 KPDX -> KONT on installed
hash `1A22A8262A39F6C1FD510FD62A96B5EDB8F55E1E176C4ECC9699B99F60501456`
is logged as `INVALID / NON-COUNTING VALIDATION` because no true live ATC
coverage was available. The flight showed healthy Engineer 3 behavior:
flight-plan receipt, route-polygon resolution, DEP -> ENR -> ARR workflow,
empty-enroute sleep, CTAF display at KPDX/KONT, no connected heavy authority
proof, and isolated refresh costs. The active consecutive pass count remains
`0`; the next valid live battle test is still Battle Test #1 for this hash.

Diagnostics trace update 2026-05-21: installed hash
`3ACE28B2E521493FA39A01444ADCFF6E7451B6292426257301EC8F54E992D363`
adds a diagnostics-only Engineer 3 trace for future flights:
`event=radio-board-candidate-diff` records raw radio-board candidate diffs, and
`event=candidate-completion-trace` records post-publisher completion decisions
with displayed/hidden state. This is black-box evidence only; it does not
change workflow, relevance, display intent, UI wake behavior, or the old
authority path. The active consecutive pass count remains `0`; the next valid
live battle test is Battle Test #1 for this hash.

Live failure update 2026-05-21: SWA3187 KELP -> KHOU on installed hash
`3ACE28B2E521493FA39A01444ADCFF6E7451B6292426257301EC8F54E992D363`
is logged as `FAIL` in
`battle_tests/2026-05-21_SWA3187_KELP_KHOU_FAIL.log`. Route resolution and
Controller Relevance correctly identified HOU Center as a route-relevant
`NEXT_POLYGON` controller for KZHU, and rejected ZAK FSS as off-route. Brain
Display Intent / final departure display assembly dropped the next-polygon
center and left only KELP CTAF visible. A corrective build changes only the
brain-owned display boundary so route-pending centers stay hidden until route
context exists, and departure final display includes accepted current and next
route centers. Corrective build hash:
`30FA935A1D4DA1CD1ED7054D8DD20BB4700D44058673E41EE80E2F559E32A5C6`. Full
regression harness passed `236 / 236`, and Release build passed. Installation
is pending because X-Plane had the installed plugin/audio files locked during
the live session. The active consecutive pass count remains `0`; after the
corrective build is installed, the next valid live test is Battle Test #1 for
that hash.

Install update 2026-05-21: after X-Plane disconnected, the corrective
`XVatsim.xpl` was copied into
`C:\X-Plane 12\Resources\plugins\XVatsim\win_x64`. The installed SHA256 now
matches the corrective build:
`30FA935A1D4DA1CD1ED7054D8DD20BB4700D44058673E41EE80E2F559E32A5C6`. The next
valid departure-mode live test is Battle Test #1 for this hash.

Departure-mode corrective validation update 2026-05-21: SWA3187 KELP -> KHOU
was reconnected on corrective hash
`30FA935A1D4DA1CD1ED7054D8DD20BB4700D44058673E41EE80E2F559E32A5C6` and is
logged as `PASS / DEPARTURE-MODE CORRECTIVE VALIDATION` in
`battle_tests/2026-05-21_SWA3187_KELP_KHOU_DEPARTURE_VALIDATION_PASS.log`.
KELP CTAF displayed, HOU Center displayed as `NEXT_POLYGON` with remaining
distance and pilot-observed orange UI color, HOU was removed from the UI when
it logged off, and DEN Center remained captured by the radio board but hidden
as `center-not-route-polygon-match`. The route-pending slice also hid center
rows until route polygon context was available. This validates the corrective
departure display action but does not increment the full release-gate live
streak because the team is intentionally holding at departure-mode tests before
airborne testing.

Terminal-authority failure update 2026-05-21: UPS2153 KONT -> PANC exposed an
APP/DEP authority failure. XVatsim correctly showed KONT CTAF, LAX Center, and
`SCT_APP 128.050`, but incorrectly displayed `SAN_W1_APP 119.600`, which is
San Diego Approach authority and not Ontario departure authority. The bad
candidate was about `69nm` away, while a transient Las Vegas APP candidate also
reported `0nm`, proving distance alone is not trustworthy for airport or
terminal authority. The active consecutive pass count remains `0`.

Terminal-authority corrective install update 2026-05-21: corrective build hash
`2C106BBA63A742F4073715263565D048703521210A20389EC5AAFA3E2BFC87A4` was built,
full harness passed `237 / 237`, and the XPL was installed to
`C:\X-Plane 12\Resources\plugins\XVatsim\win_x64`. Departure APP/DEP now
requires brain-owned terminal-owner proof from the removable
`terminal_authority` worker fact. The KONT regression accepts `SCT_APP` and
rejects `SAN_W1_APP` / `LAS_F_APP` as owner mismatches. The next valid live
test is Battle Test #1 for this installed hash.

Version 1 boundary update 2026-05-21: unknown/magenta/manual visible-hidden
controller rows are not part of the Version 1 runtime. Unproven APP/DEP,
airport local, or center candidates must be rejected/hidden with diagnostic
reasons unless the brain has proof to display them. Any pilot-controlled
visible/hidden handling is a possible Version 2 discussion only.

Clean-module clarification 2026-05-21: the removable
`modules/terminal_authority` worker is intentional Engineer 3 clean
architecture. New modules are the preferred way to add isolated fact producers.
The contract risk is feature decision/scheduling logic leaking into the plugin
shell, not the existence of a new module.

Radio-board candidate envelope update 2026-05-21: a build was produced with an
approved Brain-owned 300nm raw radio-board candidate cap. This prevents
controllers such as `MEM_221_CTR` at `325nm` from entering the raw candidate
board just because controller visual range is `600nm`. The cap is not authority
proof; route polygon proof and terminal authority proof still decide display.
Release build passed, focused scenario
`radio_reachable_source_rejects_over_300nm_transceiver_candidate.scn` passed,
and full harness passed `238 / 238`. Built XPL SHA256:
`616E135A48E61ADBC8537BDB483EC025D12012E641649DDA2BDF95B81D04E9DC`.
After Darron requested the install, this XPL was copied into X-Plane and the
installed SHA256 now matches:
`616E135A48E61ADBC8537BDB483EC025D12012E641649DDA2BDF95B81D04E9DC`.

Terminal-authority guardrail update 2026-05-22: UPS2153 KONT -> PANC exposed a
second APP/DEP leak where `LAX_S_DEP` was accepted for KONT after service
tokens were collapsed too broadly. The fix preserves service-aware terminal
tokens such as `SCT_APP`, `ONT_APP`, and `LAX_DEP`; adds cached arrival
terminal-authority facts matching the existing departure path; and rejects
same-TRACON sibling airport prefixes unless they match the current endpoint
authority. Backend guardrail scenarios now cover departure and arrival for
`KONT`, `KLAX`, `KSAN`, `KLAS`, and `PANC`. During the guardrail work, PANC
exposed a local terminal clue gap because live terminal callsigns use `ANC`;
the terminal-authority resolver now treats four-letter `P*` airport ICAOs the
same way as `K*` ICAOs for three-letter local clues. This is not a global
string-only alias engine: callsign tokens are clues, radio-board/frequency
evidence must be present, and endpoint/route authority still decides display.
Focused guardrail passed `10 / 10`, full harness passed `247 / 247`, Release
plugin build passed, and the installed XPL SHA256 is now:
`C11962103BA224FA57263490319118091D1F791625BAB9B1CFE43EAA70410AB6`. The
active live streak remains `0`; the next valid live test is Battle Test #1 for
this hash.

FAA/NASR frequency decision-hardening update 2026-05-22: the KONT/KLAX SoCal
ambiguity showed that shared SimAware/TRACON text is not enough. A removable
`airport_frequency_catalog` worker now parses endpoint-scoped FAA/NASR
`FRQ.csv` airport frequency facts, and brain-owned Controller Relevance uses
those facts as decision evidence when available. Exact endpoint APP/DEP
frequency can rescue a SimAware text miss; exact endpoint APP/DEP frequency
miss can block a broad/shared SimAware text-only pass. The KONT guardrail
rejects `SCT_APP 124.300` for KONT while accepting `SCT_1_APP 127.000` when
KONT FAA APP/DEP facts are present. Focused SoCal, KSDF FAA frequency proof,
and KONT terminal-authority scenarios passed; full harness passed `250 / 250`;
Release plugin build passed; and the XPL was installed to
`C:\X-Plane 12\Resources\plugins\XVatsim\win_x64`. Installed SHA256 now
matches:
`10937952D56614905E144EBD9DD1C4CE203349A4AEFB88B85B16A2766016CC42`. The
active live streak remains `0`; the next valid live test is Battle Test #1 for
this installed hash. Critical boundary: live FAA/NASR source acquisition is
not yet wired, so do not count a live flight as FAA/NASR disambiguation proof
until diagnostics show the departure/arrival frequency cache was populated.

Brain display contract source update 2026-05-22: live UPS2183 KONT -> KDFW
exposed a display-contract failure, not a controller-relevance failure.
Controller Relevance accepted `SCT_APP 128.050` as `CURRENT_POLYGON`, but the
final non-center display row lost that relation and old Engineer 1/2 text/tone
assumptions let standby/next/online style state influence the row. Source has
been updated so accepted-completion relation facts flow into Brain Display
Intent, overlay tone comes only from final `displayRelation`, departure APP/DEP
sorts before Center, radio tuning changes invalidate stale relevance reuse, and
controller row text is limited to `Active` or `Standby`. The pilot-facing
current/next distinction is now color-only: green for current polygon, orange
for next/arrival-prep. Harness verification passed, including the new
`brain_display_intent_departure_app_current_active_standby_advances.scn`
scenario and full harness `251 / 251`. Release plugin build/install was not
performed for this source closeout. The installed X-Plane plugin hash therefore
remains `10937952D56614905E144EBD9DD1C4CE203349A4AEFB88B85B16A2766016CC42`
until a future approved build/install verifies a new hash. The active live
streak remains `0`; the first valid live test after installing a build with
this runtime display change must be Battle Test #1 for that installed hash.

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

- After this milestone passes, begin the deliberate audit and market-preparation
  process with the brain-owned runtime contract open.
- A copy of the plugin may be given to the beta tester / streamer only after
  that post-Milestone-1 review confirms the same installed binary is still the
  intended candidate.

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

- Date: 2026-05-22
- Installed `XVatsim.xpl` SHA256:
  `10937952D56614905E144EBD9DD1C4CE203349A4AEFB88B85B16A2766016CC42`
- Reason: FAA/NASR frequency decision hardening for airport local and terminal
  APP/DEP relevance; removable `airport_frequency_catalog` worker added as a
  fact producer; brain-owned airport frequency cache added; Controller
  Relevance now uses endpoint FAA frequency evidence in accept/reject
  decisions when facts are present; KONT/KLAX SoCal frequency disambiguation
  covered by regression; focused SoCal/KONT, KSDF FAA frequency proof, and KONT
  terminal-authority scenarios passed; full harness passed `250 / 250`;
  release plugin build passed; live FAA/NASR source acquisition still requires
  a future approved gate before live flights can prove frequency
  disambiguation; service-aware terminal authority for APP/DEP; cached departure and
  arrival endpoint terminal-authority facts; KONT rejects `LAX_S_DEP`,
  `SAN_W1_APP`, and `LAS_F_APP` while accepting `SCT_APP`/sectorized `SCT_APP`
  authority; backend departure/arrival guardrail scenarios for `KONT`, `KLAX`,
  `KSAN`, `KLAS`, and `PANC`; PANC/ANC terminal clue normalization; focused
  guardrail passed `10 / 10`; full harness passed `247 / 247`; release plugin
  build passed; controller identity remains radio-board scoped and
  frequency-backed, not a global string-only alias table; approved 300nm
  radio-board candidate envelope; controllers farther
  than 300nm from the aircraft to the nearest AFV transceiver are rejected
  before raw board relevance; corrective build for UPS2153 KONT -> PANC
  terminal APP/DEP filtering;
  departure APP/DEP now requires brain-owned terminal-owner proof from the
  removable `terminal_authority` worker fact, and the KONT regression accepts
  `SCT_APP` while rejecting `SAN_W1_APP` and `LAS_F_APP` as owner mismatches;
  corrective build for SWA3187 KELP -> KHOU departure display plus
  diagnostics-only Engineer 3 trace added for radio-board candidate
  diffs and post-publisher completion/display results; Engineer 3 locked as the
  unconditional live refresh entry;
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
  `radioRange` instead of `activeTx`; brain-owned runtime now owns workflow
  phase selection through `ResolveBrainOwnedWorkflowSelection`, using
  `WorkflowSignals` from radio facts instead of provisional relevance boards;
  duplicate final-display runtime storage
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
  construction while the plugin shell only consumes the brain-owned key; broad
  route-sector authority proof is now exposed only as
  `ResolveBrainScheduledAuthorityVerification`, requires a schedule reason, and
  is used by regression coverage rather than ordinary live UI refresh; workflow
  selection no longer runs a provisional Controller Relevance pass;
  brain-owned runtime audit map updated.
- Current status superseding the long hash history: installed hash
  `81CC5DD85D579A89257670F51A0F477EAE825F5D78A9F360FBF2AE1979EEF96A` has
  active live streak `5`; Milestone 1 / Beta Streamer Candidate live gate is
  complete.
