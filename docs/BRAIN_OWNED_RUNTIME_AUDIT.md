# Brain-Owned Runtime Audit

Date started: 2026-05-21

Status: active cleanup map.

Primary contract: `docs/BRAIN_OWNED_RUNTIME_CONTRACT.md`.

Core rule:

`Brain decides. Modules produce facts. UI displays brain-approved facts.`

## Why This Audit Exists

Engineer 3 fixed the runtime shape that was causing heavy CPU usage: build a
small radio-reachable candidate board, let the brain evaluate only that board
against the route polygon context, and keep broad authority proof out of normal
UI refresh.

The remaining risk is architectural drift. The repo still contains old
Engineer 1/2 display, authority, and board-building paths next to the Engineer
3 runtime. Those paths must be mapped, quarantined, and removed in safe slices
without damaging the working Engineer 3 behavior.

Important clarification from 2026-05-21: new removable modules are the clean
Engineer 3 extension mechanism. A module is not dirty merely because it is new.
The dirty boundary is feature logic in the wrong owner, especially plugin-owned
decision/scheduling logic, module-to-module calls, or UI/display truth outside
the brain.

## Current Live Runtime

The live runtime entry is `RefreshOverlayFromBrain`, which must route directly
to `RefreshOverlayFromBrainEngineer3`.

Engineer 3 live flow:

1. Sample aircraft, xPilot, VATSIM, controller feed, flight plan, network plan,
   and radios.
2. Update brain-owned flight context.
3. Brain runs the Route Polygon Worker.
4. Brain runs the Radio Range Worker.
5. Brain owns any needed terminal-authority fact lookup/cache for APP/DEP
   ownership.
6. Brain builds narrow workflow signals from radio facts and decides the
   workflow phase.
7. Brain phase-gates the radio board.
8. Brain runs Controller Relevance Worker for accepted/rejected candidate facts.
9. Brain Publisher filters accepted completions, runs Display Intent, publishes
   the final phase snapshot, and marks displayed completions.
10. UI renders only the final brain-approved display snapshot.

## Ownership Map

### Brain / Runtime Orchestration

- `plugin/src/XVatsimPlugin.cpp`
  - `RefreshOverlayFromBrainEngineer3`
  - `RefreshBrainRoutePolygonSnapshot`
  - `BuildEngineer3RadioSnapshot`
  - `RefreshBrainControllerRelevance`
  - `RunBrainPublisher`

This is shrinking toward the correct shape. The plugin still hosts the live
X-Plane shell, input sampling, module fact adapters, and diagnostics, but
radio-board cache ownership, route-polygon runtime/cache ownership, controller
relevance, relevance cache ownership, publisher ownership, overlay wake
decisions, workflow/recovery ownership, normal flight-context update decisions,
manual diversion/revert flight-context retarget decisions, and active flight
context storage have moved into brain-owned source files.
Plugin diagnostics timing/log throttle state is grouped under one shell-owned
`PluginDiagnosticsState`, and radio range worker timing is logged as
`radioRange` rather than the older `activeTx` label.
No new feature-specific authority/relevance/display scheduling belongs in the
plugin. The `modules/terminal_authority` worker itself is intentional clean
module architecture; do not delete it as "cleanup." Any future boundary cleanup
must preserve the removable module pattern and shrink plugin orchestration
toward a generic brain-owned runtime dispatcher.

- `brain/src/BrainWorkflow.cpp`
  - Owns workflow phase, current-flight recovery decisions, and normal
    flight-context lock/refresh and retarget decisions.
  - Owns xPilot disconnect/reconnect/callsign boundary decisions.
  - Owns invalid-aircraft-state and cold/dark boundary decisions.
  - `core/WorkflowEngine.h` remains only as a compatibility shim for existing
    callers.

### Fact Workers

- `modules/transceiver_resolver`
  - Radio reachability facts.
  - Must not decide route ownership or UI display.
- `brain/src/BrainRadioRangeWorker.cpp`
  - Radio range worker output shaping.
- `modules/route_sector`
  - Route polygon facts and route-scoped authority facts.
  - Heavy authority proof must remain explicitly brain-scheduled only; the
    remaining broad proof entry is
    `ResolveBrainScheduledAuthorityVerification`.
- `modules/terminal_authority`
  - Departure/destination APP/DEP owner facts from source-backed terminal
    authority polygons.
  - Must not decide display, workflow phase, or candidate acceptance.
  - Must not call other modules.
  - Must parse/cache only when the brain-owned runtime schedules the lookup.
- `brain/src/RoutePolygonTransition.cpp`
  - Route progress/current/next polygon transition facts.
- `brain/src/BrainRoutePolygonWorker.cpp`
  - Route-sector hashing, route-polygon worker output shaping, cache reuse,
    pending retry decisions, transition application, route-state commit, wake
    reason, and relevance invalidation.
- `brain/src/RadioReachableSnapshot.cpp`
  - Radio board grouping, hashes, phase gating, and verification feed shaping.
- `brain/src/BrainControllerRelevanceWorker.cpp`
  - Controller relevance matching, accepted/rejected candidate completions, and
    route-polygon display relation facts.
  - Controller Relevance worker input shaping from brain-owned route/radio
    context.

### Brain Publisher / Display Intent

- `brain/src/BrainDisplayIntent.cpp`
  - Owns current-vs-next display relation, orange distance annotation, final
    board assembly.
  - Preserves accepted-completion relation facts for non-center rows, so an
    APP/DEP row accepted as `CURRENT_POLYGON` remains current/green in final
    display.
  - Publishes final UI rows as `FinalDisplaySnapshot`, not as a raw
    `ModuleBoardSnapshot`.
  - Keeps accepted module boards raw in its output; UI-only annotation and
    remaining-distance formatting are applied directly to
    `FinalDisplayStationSnapshot` rows while assembling `FinalDisplaySnapshot`.
- `brain/src/PhaseSnapshotPublisher.cpp`
  - Owns last-proven final display snapshot reuse.
- `brain/src/BrainOwnedRuntime.cpp`
  - Owns accepted-completion filtering, CTAF/UNICOM replacement, Brain Display
    Intent invocation, phase snapshot publishing, and displayed-completion
    marking.
  - Owns radio phase-gate storage and final published runtime snapshot commits.
  - Owns Brain Publisher input shaping from route context plus relevance and
    CTAF facts.
  - Owns CTAF/UNICOM station shaping and tuned-state decisions from CTAF lookup
    facts plus radio state.
  - Owns standby-assist target selection and display flag application.
  - Owns final published runtime commit shaping from Brain Publisher output and
    the final standby-assisted display board.
  - Owns overlay wake/hide/reason decisions from shell-provided UI facts.
  - Owns enroute initial display-hold state and timing.
  - Owns active-flight flight-plan sampling cadence and cached flight-plan
    snapshot state.
  - Owns cruise target state, filed/current target command decisions,
    source-plan invalidation, gate dwell/reached tracking, and cruise header
    text.
  - Owns workflow progress latches for departure release, arrival wake, and
    airborne-since timing.
  - Owns the xPilot-seen latch used by overlay wake/disconnect behavior.
  - Owns active flight context storage; the plugin commits and clears
    brain-returned context through brain helpers.
  - Owns the cache-reset path that preserves active flight context while
    clearing derived runtime state for a new context.
  - Owns xPilot session boundary state, current-flight recovery request
    latches, and aircraft cold/dark/invalid-state boundary latches.
  - Owns the standby-assist write latch and COM1 standby write/no-write
    decision. The plugin performs only the radio write side effect.
  - Owns diversion override source-key state and the decision to use or clear a
    manual diversion override for the current source VATSIM flight plan.
  - Owns preflight route-cache applied-plan state and the decision to clear,
    validate, or apply the route resolver cache.
  - Owns display override mode (`Auto`, forced open, forced sleep) and
    preserves it across runtime resets.
  - Owns manual query/transient status display state and expiry timing.
  - Owns controller-message display state, sequence tracking, cached recall,
    visibility, and clear/ack behavior.
  - Owns latest sampled aircraft, pilot identity, flight-plan, and network-plan
    fact snapshots for command-side brain decisions.
  - Owns pending overlay text-entry mode for manual CTAF and diversion command
    submissions.
  - Owns network-plan identity-key construction used by flight-context,
    diversion, preflight-route-cache, radio-route, standby-assist, and command
    decisions.
  - Owns relation-fact transfer from accepted candidate completions into Brain
    Display Intent.
  - Owns radio tuning identity in controller relevance reuse so COM active and
    COM standby changes cannot leave stale active/standby display state.
  - Owns standby-assist final-row tuning refresh and may set only standby
    display state after a successful COM1 standby load. It must not use standby
    assist to rewrite polygon relation or fake `next` display state.
  - Owns departure terminal-authority request keys, refresh/backoff decisions,
    cached facts, fact hashing, and relevance invalidation when the terminal
    authority fact changes.
  - Owns workflow phase selection through `ResolveBrainOwnedWorkflowSelection`,
    using `WorkflowSignals` derived from radio facts instead of provisional
    relevance boards.
  - Stores the final brain-approved UI board only as `finalDisplaySnapshot`;
    the duplicate `activeBoardSnapshot` runtime field is gone.

### UI Renderer

- `brain/src/BrainOrchestrator.cpp`
  - Converts the final brain-approved board into overlay text/tone.
  - Overlay tone must come from final `displayRelation` only: current polygon
    green/active tone, next or arrival-prep orange/next tone.
  - Controller row text badges are limited to `Active` for active COM tuning
    and `Standby` for a successfully loaded COM1 standby-assist target.
  - Must not reintroduce Engineer 1/2 text badges such as `NEXT`, `ONLINE`,
    sector `ACTIVE`, or `OFFLINE` for controller frequency rows.
- `modules/overlay`
  - Renders the view model.

UI must not trigger authority, relevance, workflow, route, or fallback work.

### Legacy / Quarantine

These paths are not the Engineer 3 live engine and must be removed or isolated:

Removed from `plugin/src/XVatsimPlugin.cpp` and the old core display surface
by installed hash
`078CF4DC56990C61E4EF60F4192F6B900C20B438BC4426F32165312AEEEBCF1F`:

- the old body that had been quarantined below `RefreshOverlayFromBrain`
- `CollectDepartureBoardCached`
- `CollectArrivalBoardCached`
- `CollectEnrouteBoardCached`
- `ExecuteScheduledAuthorityRelevance`
- `ExecuteScheduledDepartureAuthority`
- `RefreshRadioBoardActiveRoute`
- `RefreshRadioBoardSnapshotRuntime`
- legacy plugin-owned board/authority cache state and reset wiring
- `core::workflow::BuildDisplayBoard`
- the plugin wrapper around the old core display builder
- stale plugin display-decision/board logging caches from the retired path
- `displayStations` ownership from legacy departure/arrival/enroute collectors
  and plugin intermediate publisher boards
- accepted-completion board filtering and final-display completion marking from
  the plugin shell
- publisher assembly, CTAF/UNICOM replacement, and Brain Display Intent
  invocation from the plugin shell
- phase snapshot publisher state and publish invocation from the plugin shell
- controller relevance worker matching/completion logic from the plugin shell
- controller relevance cache reuse and candidate completion cache updates from
  the plugin shell
- radio-board reuse, commit state, diff storage, wake reason, and relevance
  invalidation from the plugin shell
- route-sector hashing and route-polygon worker output shaping from the plugin
  shell
- route-polygon cache reuse, pending retry decisions, transition application,
  route-state commit, wake reason, and relevance invalidation from the plugin
  shell
- radio range worker output shaping from the plugin shell
- radio phase-gate storage and final published runtime snapshot commits from
  the plugin shell
- Controller Relevance worker input shaping from the plugin shell
- Brain Publisher input shaping from the plugin shell
- CTAF/UNICOM station shaping and tuned-state decisions from the plugin shell
- standby-assist target selection and display flag application from the plugin
  shell
- final published runtime input shaping from the plugin shell
- overlay wake/hide/reason decisions from the plugin shell
- workflow/recovery implementation from `core/src/WorkflowEngine.cpp`
- unused pre-Engineer-3 plugin workflow wrapper and unused distance wrappers
- normal flight-context lock/refresh decisions from the plugin shell
- manual diversion/revert flight-context retarget decisions from the plugin
  shell
- enroute initial display-hold state and timing from the plugin shell
- active-flight flight-plan sampling cadence and cached flight-plan snapshot
  state from the plugin shell
- xPilot disconnect/reconnect/callsign boundary decisions from the plugin shell
- invalid-aircraft-state and cold/dark boundary decisions from the plugin shell
- unused plugin-side `LegacyAuthorityRuntimeAllowed`, `RadioBoardDiffChanged`,
  and `IsRadioBoardRouteMapReady` helpers
- cruise target state, filed/current target command decisions, source-plan
  invalidation, gate dwell/reached tracking, and cruise header text from the
  plugin shell
- workflow progress latches for departure release, arrival wake, and
  airborne-since timing from the plugin shell
- xPilot-seen latch used by overlay wake/disconnect behavior from the plugin
  shell
- active flight context storage from the plugin shell
- xPilot session boundary state, current-flight recovery request latches, and
  aircraft cold/dark/invalid-state boundary latches from the plugin shell
- standby-assist write latch and COM1 standby write/no-write decision from the
  plugin shell
- diversion override source-key state and use/clear decision from the plugin
  shell
- preflight route-cache applied-plan state and clear/validate/apply decision
  from the plugin shell
- display override mode from the plugin shell
- manual query/transient status display state and expiry timing from the plugin
  shell
- controller-message display state, sequence tracking, cached recall,
  visibility, and clear/ack behavior from the plugin shell
- shadow brain scheduler diagnostics reconstructed from old plugin diagnostic
  job names
- latest sampled aircraft, pilot identity, flight-plan, and network-plan fact
  snapshot cache from the plugin shell
- pending overlay text-entry mode from the plugin shell
- unused plugin-side hash/active-transceiver helpers from the retired board and
  radio refresh paths
- global stateless `gBrain` object from the plugin shell
- loose plugin diagnostics globals and misleading `activeTx` timing labels from
  the plugin shell
- provisional relevance plus workflow phase selection from the plugin shell
- duplicate final-display storage under the ambiguous `activeBoardSnapshot`
  runtime name
- unused plugin-local departure/arrival/enroute board variables from the
  Engineer 3 refresh shell
- temporary display-mutated `ModuleBoardSnapshot` staging inside Brain Display
  Intent
- raw module-board `displayStations` ownership flag from `ModuleBoardSnapshot`
- raw module-board `next` and `standby` display flags from
  `BoardStationSnapshot`
- raw module-board `annotation` display text from `BoardStationSnapshot`
- raw module-board `displayRelation` state from `BoardStationSnapshot`
- network-plan identity-key construction from the plugin shell

Still contract debt outside the live Engineer 3 path:

- `modules/enroute::EnrouteModule::Collect`
- `modules/departure::DepartureModule::Collect`
- `modules/arrival::*::Collect`

Some of this code may remain temporarily for harness coverage or future
fallback APIs, but none of it may be reachable from ordinary live UI refresh.
The CMake build graph now gates these old board modules behind
`XVATSIM_BUILD_REGRESSION_HARNESS`, so plugin-only builds do not compile them as
part of the live module stack.
Their CMake target names are now explicitly harness legacy:
`XVatsimHarnessLegacyArrival`, `XVatsimHarnessLegacyDeparture`, and
`XVatsimHarnessLegacyEnroute`.
Their public headers also require the explicit
`XVATSIM_ENABLE_HARNESS_LEGACY_BOARD_MODULES` compile definition, which is
exported only by those harness legacy targets.

## Known Contract Risks

1. `XVatsimPlugin.cpp` is a god file.
   Brain orchestration, worker adapters, publisher logic, old runtime, UI update,
   diagnostics, and command handling are mixed together.

2. Legacy fact modules still exist in the repo.
   The old plugin refresh body and plugin-owned live board caches have been
   removed. Older departure/arrival/enroute collectors still compile for
   library/harness coverage. The next cleanup step is to rename/extract them
   as explicit fact-only workers or retire the unused libraries.

3. Raw board/display state split is mostly complete.
   The final UI board is split into `FinalDisplaySnapshot` /
   `FinalDisplayStationSnapshot`, Brain Display Intent no longer creates
   display-mutated `BoardStationSnapshot` rows, raw station rows no longer
   carry `next`, `standby`, `annotation`, or `displayRelation`, and raw module
   boards no longer carry `displayStations`.

4. Workflow selection is now independent from Controller Relevance board
   building. `ResolveBrainOwnedWorkflowSelection` derives narrow
   `WorkflowSignals` from radio facts and passes them to
   `ResolveWorkflowStageFromSignals`.

5. Old modules can still create display boards from authority snapshots.
   The live Engineer 3 path should not rely on those modules for display truth.
   Their behavior must be converted into fact-only workers or quarantined.

6. Heavy authority proof remains compiled in the route-sector module for
   verifier/regression coverage, but it is no longer exposed as ordinary
   relevance discovery. The remaining public entry is explicitly named
   `ResolveBrainScheduledAuthorityVerification` and requires a schedule reason.

## Cleanup Order

### Slice 1: Lock Engineer 3 Live Entry

- `RefreshOverlayFromBrain` always calls `RefreshOverlayFromBrainEngineer3`.
- Remove runtime flag selection for the live path.
- Keep full harness passing.

Status: complete for the current cleanup slice. The public live entry now calls
Engineer 3 directly. The old refresh body, old plugin board collectors, old
radio-board runtime, old scheduled authority proof executors, and legacy
plugin-owned board/authority cache reset wiring have been deleted from
`plugin/src/XVatsimPlugin.cpp`. Direct plugin globals for the old
departure/arrival/enroute display modules are gone, and the plugin target no
longer links the old departure, arrival, or enroute display-module libraries.
Release build passed and full harness passed `234 / 234` for installed hash
`25A244BED7EB7F9ED96B6A34CE79A3075676326EC12D3F752615FF2AF57DC9F6`.

### Slice 2: Separate Facts From Display State

- Preserve absolute route-entry distance as fact truth.
- Store derived display distance only as annotation/display output.
- Add or maintain tests proving display refresh cannot corrupt route truth.

Status: active and partially complete. `core::workflow::BuildDisplayBoard` has
been removed so core workflow no longer assembles display truth. The regression
harness now checks display rows from `BrainDisplayIntent`, and
`BrainDisplayIntent` filters offline/no-frequency worker rows out of the final
UI snapshot. Release build passed and full harness passed `234 / 234` for
installed hash
`7C98DFC78E850CFB2166A55A67E84AFB4A16B32E880E8E9A0954E00ED5E9AD1A`.

Follow-up update: legacy departure/arrival/enroute collectors and plugin
intermediate publisher boards no longer set `displayStations`; only Brain
Display Intent sets that flag while assembling the final display board.

### Slice 3: Move Brain Runtime Out Of Plugin File

- Extract route polygon worker adapter.
- Extract radio board worker adapter.
- Extract controller relevance worker.
- Extract brain publisher.
- Keep plugin file as X-Plane shell and command/event host.

Status: started. `RunBrainOwnedPublisher` now owns publisher assembly inside
`brain/src/BrainOwnedRuntime.cpp`: accepted-completion filtering, CTAF/UNICOM
replacement, Brain Display Intent invocation, phase snapshot publish state, and
final-display completion marking moved out of `plugin/src/XVatsimPlugin.cpp`.
`RunBrainControllerRelevanceWorker` now lives in
`brain/src/BrainControllerRelevanceWorker.cpp`, so controller matching and
accepted/rejected completion facts are brain-owned.
`RunBrainOwnedControllerRelevance` also owns relevance cache reuse and
candidate completion cache updates. The plugin now supplies inputs and
diagnostics only. `TryReuseBrainOwnedRadioBoard` and
`CommitBrainOwnedRadioBoardRefresh` now own radio-board reuse, commit state,
diff storage, wake reason, and relevance invalidation; the plugin runs the
transceiver module as a fact producer. `BrainRoutePolygonWorker.cpp` now owns
route-sector hashing, route-polygon worker output shaping, route cache reuse,
pending retry decisions, transition application, route-state commit, wake
reason, and relevance invalidation; the plugin runs the route-sector resolver
as a fact producer. `BrainRadioRangeWorker.cpp` now owns radio range worker
output shaping; the plugin runs the transceiver resolver as a fact producer.
`RunBrainOwnedRadioPhaseGate` and `CommitBrainOwnedPublishedRuntime` now own
radio phase-gate storage and final published runtime snapshot commits. Release
`BuildBrainOwnedControllerRelevanceInput` now shapes Controller Relevance worker
inputs from brain-owned route/radio context.
`BuildBrainOwnedPublisherInputFromFacts` now shapes Brain Publisher input from
brain-owned route context plus plugin-supplied relevance and CTAF facts. Release
build passed and full harness passed `234 / 234` for
installed hash
`8A159734EEB1EDE32A54869FA20B634B7798C78ED3376C33BE456E4B480C95F8`.

Follow-up update: Brain-owned runtime now turns plugin-supplied CTAF lookup
facts and radio state into CTAF/UNICOM board stations, including tuned-state
decisions. The plugin only adapts the CTAF module result into neutral facts.
Release build passed and full harness passed `234 / 234` for installed hash
`374EDA031F594BA7C35AFF7DA7B4D75B333801D61E5247E998ACCA19C7F19D16`.

Follow-up update: Brain-owned runtime now owns standby-assist target selection
and display flag application. The plugin only performs the X-Plane COM1
standby write side effect and feeds the result back to the brain. Release build
passed and full harness passed `234 / 234` for installed hash
`C1EF7EEAB8B64A5092B00AA8EB12F9C58155DD34FF5D1E417850F711EC9B10F8`.

Follow-up update: Final published runtime commits now happen through
`CommitBrainOwnedPublishedRuntimeFromPublisherOutput` after the
standby-assisted final display board is available. The plugin no longer
hand-assembles `BrainOwnedPublishedRuntimeInput`, and runtime state now matches
the rendered UI board. Release build passed and full harness passed
`234 / 234` for installed hash
`B107DCF4ADA2D99F00036D1E3BFAAC172EB38F98C0675A4BBF5BF0444ADA610A`.

Follow-up update: `DecideBrainOwnedOverlayWake` now owns overlay
wake/hide/reason decisions from shell facts, including manual-query, text-entry,
controller-message, xPilot wait/disconnect, workflow, and enroute-center
presence. The plugin still performs X-Plane window updates and tracks shell
state, but no longer decides whether the UI should wake. Release build passed
and full harness passed `234 / 234` for installed hash
`43249716748D6035783A1703A8359AF5CF432F425FC743773C7F6A6FBA650D29`.

Follow-up update: Workflow and current-flight recovery implementation moved
from `core/src/WorkflowEngine.cpp` into `brain/src/BrainWorkflow.cpp`. The
plugin now includes `BrainWorkflow.h` directly, and `core/WorkflowEngine.h` is
only a compatibility shim. Release build passed and full harness passed
`234 / 234` for installed hash
`317773ECA22A2706C84AB78CE177396F132756DDFEEA20F48D7780040A04D0C1`.

Follow-up update: The unused pre-Engineer-3 plugin workflow wrapper and unused
distance wrappers were removed from `plugin/src/XVatsimPlugin.cpp`, leaving the
live path to call `brain::workflow::ResolveWorkflowStage` through the Engineer
3 wrapper only. Release build passed and full harness passed `234 / 234` for
installed hash
`078CF4DC56990C61E4EF60F4192F6B900C20B438BC4426F32165312AEEEBCF1F`.

Follow-up update: `UpdateFlightContextFromNetworkPlan` now owns normal
flight-context lock/refresh decisions inside `brain/src/BrainWorkflow.cpp`,
including departure confirmation, callsign/route change relock, route text
refresh, and authoritative/missing airport coordinate refresh. The plugin only
applies the brain output context and performs reset/invalidate side effects.
Release build passed and full harness passed `234 / 234` for installed hash
`A7C23053B5172C9533A8EFFE846F531F2E921A8A1190114AA998C952DFA6C139`.

Follow-up update: `RetargetFlightContextToNetworkPlan` now owns manual
diversion/revert flight-context retarget decisions inside
`brain/src/BrainWorkflow.cpp`. The plugin keeps the command/UI shell and applies
only the brain-returned context plus reset/invalidate side effects. Release
build passed and full harness passed `234 / 234` for installed hash
`B8E94F2EF29981D674175FCAD14A634B751374F783DED9DE201BBAEEA53B67F7`.

Follow-up update: Brain-owned runtime now owns enroute initial display-hold
state and timing through `UpdateBrainOwnedEnrouteInitialHold`. The plugin
supplies current time/hold duration and passes the brain-returned active flag
into overlay wake. Release build passed and full harness passed `234 / 234` for
installed hash
`540BA95C15B3E85F703E2E2711CB548C2DB4BDCA3350FB2D4FFAA3FE361BE312`.

Follow-up update: Brain-owned runtime now owns active-flight flight-plan
sampling cadence and cached flight-plan snapshot state through
`DecideBrainOwnedFlightPlanSample` and `CommitBrainOwnedFlightPlanSample`. The
plugin runs `FlightPlanSampler` only when the brain requests a fresh sample.
Release build passed and full harness passed `234 / 234` for installed hash
`AAB7203E2ED9AC9C03E79ED11B360B75E06AB6C9ED5B04CB804C54DC91072DA9`.

Follow-up update: `ResolveXPilotSessionBoundary` now owns xPilot
disconnect/reconnect/callsign boundary decisions inside
`brain/src/BrainWorkflow.cpp`. The plugin stores shell session facts and applies
only the brain-returned preserve/reset/recovery flags. Release build passed and
full harness passed `234 / 234` for installed hash
`8B9C0F54B01E7EA9C2DFE1E330AEA40D252E017D34B19D61F4CADB1E01CF3A66`.

Follow-up update: `ResolveAircraftRuntimeBoundary` now owns
invalid-aircraft-state and cold/dark boundary decisions inside
`brain/src/BrainWorkflow.cpp`. The plugin applies only the brain-returned
reset/latch flags, then hides or renders the X-Plane overlay as the shell side
effect. Release build passed and full harness passed `234 / 234` for installed
hash
`A0350D5D203EE9AC614A6B71C13D63D3C10318D2164D8EFFE1580E310AFBA756`.

Follow-up update: Removed unused plugin-side `LegacyAuthorityRuntimeAllowed`,
`RadioBoardDiffChanged`, and `IsRadioBoardRouteMapReady` helpers from
`plugin/src/XVatsimPlugin.cpp`. Release build passed and full harness passed
`234 / 234` for installed hash
`A2B155710708B2393BCF47EF7202CA3BED2D85D132272DE046DBC77179EA06FF`.

Follow-up update: Brain-owned runtime now owns cruise target state,
filed/current target command decisions, source-plan invalidation,
gate dwell/reached tracking, and cruise header text. The plugin keeps only the
command/status side effects. Release build passed and full harness passed
`234 / 234` for installed hash
`49C96E697904C9332B7752E03E3C3F0F63FEDC2AFB71A49DB8CD3071171CF3AC`.

Follow-up update: Brain-owned runtime now owns workflow progress latches for
departure release, arrival wake, and airborne-since timing. The plugin builds
and commits workflow state through brain helpers instead of carrying those
latches as plugin globals. Release build passed and full harness passed
`234 / 234` for installed hash
`D4AF05D53321D4A97B776138609FB0EAE24A2F8F1FCC00DF3733407851DC6143`.

Follow-up update: Brain-owned runtime now owns the xPilot-seen latch used by
overlay wake/disconnect behavior. The plugin marks the latch through brain
helpers and feeds the brain-owned value into overlay wake. Release build passed
and full harness passed `234 / 234` for installed hash
`7B207FB40740A44D7B1CBDF441B68FC1080A087FCBC9EB713DB77A70735C5C80`.

Follow-up update: Brain-owned runtime now owns active flight context storage.
The plugin no longer carries `gFlightContext`; it reads the context from
`BrainOwnedRuntimeState` and commits/clears brain-returned context through
brain helpers. Workflow state construction now pulls flight context from the
brain-owned runtime. Release build passed and full harness passed `234 / 234`
for installed hash
`0271DCFB0667726B5DFEE0ADBE430704629DF3ED775C07B2BD45273AF5FAA513`.

Follow-up update: Brain-owned runtime now provides
`ResetBrainOwnedRuntimeCachePreservingFlightContext`, and new-context runtime
cache clears use it so a just-locked brain-owned flight context is not erased
with derived runtime state. Release build passed and full harness passed
`234 / 234` for installed hash
`A16D4BA7CA20709A211C3D8624CBABC059FE86940737740D8EA803A1E7CA3A1C`.

Follow-up update: Brain-owned runtime now owns xPilot session boundary state,
current-flight recovery request latches, and aircraft cold/dark/invalid-state
boundary latches. The plugin no longer carries those as `gLastXPilotConnected`,
`gLastConnectedPilotCallsign`, `gDisconnectedPilotCallsign`,
`gPendingAutomaticFlightRecovery`, `gManualFlightRecoveryRequested`,
`gColdDarkResetApplied`, or `gAircraftStateInvalidBoundaryActive`; it supplies
facts and applies brain helper commits. Release build passed and full harness
passed `234 / 234` for installed hash
`7541089D7D7EA2104C9A94BE571E00716F74B580C3EA004DD20876D7EFCB8F40`.

Follow-up update: Brain-owned runtime now owns the standby-assist write latch
and decides whether the plugin should perform a COM1 standby write for the
brain-selected target. The plugin only executes the radio side effect when the
brain asks and then applies the brain-owned standby result to the final board.
Release build passed and full harness passed `234 / 234` for installed hash
`933E5F4AAC1EDD2AD63336DCD7EAB0C516FB685F73CA01992167071F8935DA51`.

Follow-up update: Brain-owned runtime now owns diversion override source-key
state and decides whether a manual diversion override still belongs to the
current source VATSIM flight plan. The plugin still performs the X-Plane
airport lookup and module reset/effective-plan side effects, but it no longer
carries `gDiversionOverrideSourceKey` or decides override validity. Release
build passed and full harness passed `234 / 234` for installed hash
`607D50B58A25E32B96AC4C37D9A9248576FCDBE4F12AB6E6EDC233F52A297EC7`.

Follow-up update: Brain-owned runtime now owns preflight route-cache
applied-plan state and decides when the plugin should clear, validate, or apply
the route resolver cache. The plugin still performs cache file IO, core
validation, and route-resolver cache side effects, but no longer carries
`gPreflightRouteCacheAppliedPlanKey` or independently decides cache reuse.
Release build passed and full harness passed `234 / 234` for installed hash
`2576A86E309F085EC35FF5D638FC378325081C00F8CE93F4D38461F904DBF190`.

Follow-up update: Brain-owned runtime now owns display override mode (`Auto`,
forced open, forced sleep) and preserves it across runtime resets. The plugin
command/menu/settings layer only sets the brain-owned mode and performs overlay
window side effects. Release build passed and full harness passed `234 / 234`
for installed hash
`0A0A7A626DF447E43BB834885929C043F199FE487F93EE053836C8AC132427F9`.

Follow-up update: Brain-owned runtime now owns manual query/transient status
display state and expiry timing. The plugin supplies manual CTAF query results
and user-command status lines as facts, while overlay wake/build reads the
brain-owned snapshot. Release build passed and full harness passed `234 / 234`
for installed hash
`4D7AAB367D3F2A30BCB58EF984660781374180170C032BBDDCEDB6CAA7D79ACE`.

Follow-up update: Brain-owned runtime now owns controller-message display
state, sequence tracking, cached recall, visibility, and clear/ack behavior.
The plugin only polls xPilot private-message facts, consumes overlay button
requests, and renders the brain-owned controller message card. Release build
passed and full harness passed `234 / 234` for installed hash
`C6529D8176F0E71773C7D2E1201E190067E21FAAED1C33E0E2F23B6D0D8DF7DB`.

Follow-up update: Removed the plugin-side shadow brain scheduler diagnostics
that translated old diagnostic job names into synthetic `BrainWorkItem`s and
logged `shadowScheduler=...`. Diagnostics now report the actual recorded jobs,
timings, route status, authority status, and authority proof summary without a
parallel scheduler model in the X-Plane shell. Release build passed and full
harness passed `234 / 234` for installed hash
`A50003DE3B7483597DE659E85EA422A54758D311CB0F82E9B405633774C606DC`.

Follow-up update: Brain-owned runtime now owns the latest sampled aircraft,
pilot identity, flight-plan, and network-plan fact snapshots. The plugin commits
the current refresh facts through `CommitBrainOwnedLastSampledFacts`, command
handlers read those facts from `BrainOwnedRuntimeState`, and the old
`gLast*Snapshot` plugin globals are gone. Release build passed and full harness
passed `234 / 234` for installed hash
`8B5E7DA823D1DDCBA375D9BD39B61E9EC893CC86FD8C9FC2567881AE5690E6FE`.

Follow-up update: Brain-owned runtime now owns pending overlay text-entry mode
for manual CTAF and diversion command submissions. The plugin opens and reads
the overlay text box, but it no longer stores `gPendingTextEntryMode`; it sets
and consumes the brain-owned prompt mode instead. Release build passed and full
harness passed `234 / 234` for installed hash
`00AE47855BC49B2CD35152CD2B6771B4EA92546E6B6952D53B3F6A9355925C28`.

Follow-up update: Removed unused plugin-side `HashRadioBoardInputs`,
`HashCtafLookupEntry`, and `NeedsTransceiverResolution` helpers left over from
retired board and radio refresh paths. Release build passed and full harness
passed `234 / 234` for installed hash
`F81C86F85F836912CF5A7A441AFB0C5A446FC7C8C4E8DE4E8BF08FC472503762`.

Follow-up update: `BrainOrchestrator::BuildOverlayViewModel` is now a stateless
brain API and the plugin no longer carries a global `gBrain` object. Release
build passed and full harness passed `234 / 234` for installed hash
`C00D070DE02ADD449A0881FE340D98DF6B67A6C43140EB3319FC80496C4B5CA4`.

Follow-up update: The CMake build graph now gates the old
departure/arrival/enroute board modules behind `XVATSIM_BUILD_REGRESSION_HARNESS`.
They still build for regression coverage, but plugin-only builds no longer
compile them as part of the live Engineer 3 module stack. Release build passed
and full harness passed `234 / 234`; installed hash remained
`C00D070DE02ADD449A0881FE340D98DF6B67A6C43140EB3319FC80496C4B5CA4` because the
plugin binary was unchanged.

Follow-up update: The harness-only legacy board libraries were renamed to
`XVatsimHarnessLegacyArrival`, `XVatsimHarnessLegacyDeparture`, and
`XVatsimHarnessLegacyEnroute`, and `docs/ARCHITECTURE.md` plus
`modules/README.md` now describe `arrival`, `departure`, and `enroute` as
harness-only legacy coverage rather than live plugin modules. Release build
passed and full harness passed `234 / 234`; installed hash remained
`C00D070DE02ADD449A0881FE340D98DF6B67A6C43140EB3319FC80496C4B5CA4` because the
plugin binary was unchanged.

Follow-up update: The harness-only legacy board headers now require
`XVATSIM_ENABLE_HARNESS_LEGACY_BOARD_MODULES`, and only the
`XVatsimHarnessLegacyArrival`, `XVatsimHarnessLegacyDeparture`, and
`XVatsimHarnessLegacyEnroute` targets export that definition. This turns any
accidental live include of those old board modules into a compile-time failure.
Release build passed and full harness passed `234 / 234`; installed hash
remained
`CEF8BFF73D2436F7B251CC1F15BB7ED44A76D33B3440AC05682AD2134A3C0C50` because the
plugin binary was unchanged.

Follow-up update: Plugin diagnostics state is now grouped under
`PluginDiagnosticsState`, replacing the loose refresh/perf-warning log throttle
globals in `plugin/src/XVatsimPlugin.cpp`. Refresh diagnostics now log the radio
range worker timing as `radioRange` instead of `activeTx`, matching the
Engineer 3 worker boundary. Release build passed and full harness passed
`234 / 234` for installed hash
`1E0B9DB0BF9920B26D3E77E17B9005F1349E82AC358B8D1941387015F4C37A07`.

Historical follow-up update: Brain-owned runtime first took ownership of the
provisional relevance pass used for workflow phase selection through
`ResolveBrainOwnedWorkflowSelection`. A later cleanup replaced this with narrow
`WorkflowSignals`; this entry remains as slice history.
Release build passed and full harness passed `234 / 234` for installed hash
`56AE27E03BF2048CF4A32DAA8BDCF0677DF792D8F66F07A9BDEB272559A84361`.

Follow-up update: Removed the duplicate `activeBoardSnapshot` runtime field and
renamed the plugin/UI path to pass `finalDisplaySnapshot` into
`BrainOrchestrator::BuildOverlayViewModel`. The brain-owned final UI board now
has one runtime name: `finalDisplaySnapshot`. Release build passed and full
harness passed `234 / 234` for installed hash
`55A6E4F98B7507B30D2BD101F2138E5E8BC124E8E8460EDE716493CD788FC388`.

Follow-up update: Removed stale plugin-local departure/arrival/enroute board
variables from `RefreshOverlayFromBrainEngineer3`. After brain workflow
selection and publisher ownership, the plugin keeps only the final display board
needed for standby assist, commit, wake, and UI rendering. Release build passed
and full harness passed `234 / 234` for installed hash
`16D00B1CF1583C29EACCF8B6200FDD97F426F409273509EACACCDF7E67591862`.

Follow-up update: Added display-specific final UI types:
`FinalDisplayStationSnapshot` and `FinalDisplaySnapshot`. Brain Display Intent
now converts accepted worker board rows into a final display snapshot; Phase
Snapshot Publisher stores/reuses final display snapshots; standby assist,
overlay wake, runtime final display state, `BrainOrchestrator`, the plugin UI
handoff, and regression harness display-intent checks now use the display type
instead of reusing `ModuleBoardSnapshot` as the final UI board. Release build
passed and full harness passed `234 / 234` for installed hash
`C23142AFA326A0BF52A562451CB9C53649DC80C6DE70D1092D741DE4C076B07C`.

Follow-up update: Brain Display Intent no longer publishes display-mutated
enroute rows back into `BrainDisplayIntentOutput::enrouteBoard`. It now keeps
accepted module boards raw in its output and uses a local display-only enroute
board to build `FinalDisplaySnapshot`, so remaining-distance annotations and
current/next UI shaping cannot overwrite publisher/runtime module board
snapshots. Release build passed and full harness passed `234 / 234` for
installed hash
`52FC2B8E92151FE8B56B3B70C62EBF131A498FDAE0278B3836A7011300F36363`.

Follow-up update: Brain Display Intent now builds enroute display rows directly
as `FinalDisplayStationSnapshot` entries and sorts/dedupes them as
`FinalDisplaySnapshot`, instead of staging display-shaped rows through a
temporary `ModuleBoardSnapshot`. Accepted departure/arrival/enroute output
boards remain raw module facts. Release build passed and full harness passed
`234 / 234` for installed hash
`4898FEBA40F2E48F40907D6B20FE920363B8D05C3E8F49D31E0889290B62CA63`.

Follow-up update: Removed raw-board `next` and `standby` display flags from
`BoardStationSnapshot`. Those UI-only flags now exist only on
`FinalDisplayStationSnapshot`, and the regression harness accepts old scenario
syntax for those fields only as ignored legacy input. Release build passed and
full harness passed `234 / 234` for installed hash
`02944C4FF9541CF3B045F11FD6D8FE1F633F575C6507E4A458E121E93E36BAA7`.

Follow-up update: Removed raw-board `annotation` display text from
`BoardStationSnapshot`. Brain Display Intent still writes orange distance and
other UI annotations onto `FinalDisplayStationSnapshot`, while legacy
departure/arrival/enroute collectors and harness station parsing no longer
store annotations on module fact rows. Release build passed and full harness
passed `234 / 234` for installed hash
`41094E64705B00113F08D0FAD390357E2A4BC4D0734B049E68589769A3D27277`.

Follow-up update: Removed raw-board `displayRelation` state from
`BoardStationSnapshot`. Controller Relevance now records relation directly on
candidate completions, and Brain Display Intent infers final UI relation from
fact fields such as polygon key, active/tuned state, and route-entry distance.
The display-intent regression scenarios now express current/next/hidden state
through those fact fields instead of raw-board relation hints. Release build
passed and full harness passed `234 / 234` for installed hash
`5B3277315E5E0A27A428C665245E855402F3FCE87315CDC5CAA49B82F0AC0FE9`.

Follow-up update: Removed raw-board `displayStations` from
`ModuleBoardSnapshot`. Raw module boards now carry only availability, source,
airport, and fact rows; final-display availability belongs to
`FinalDisplaySnapshot`. Release build passed and full harness passed
`234 / 234` for installed hash
`CEF8BFF73D2436F7B251CC1F15BB7ED44A76D33B3440AC05682AD2134A3C0C50`.

Follow-up update: Brain-owned runtime now owns network-plan identity-key
construction through `BuildBrainOwnedPlanIdentityKey` and
`BuildBrainOwnedNetworkPlanIdentityKey`. The plugin shell no longer normalizes
plan identity or keeps local matched-plan helper logic; it only consumes the
brain-owned key for standby assist, route runtime keys, preflight cache,
diversion, cruise commands, and publisher inputs. Release build passed and full
harness passed `234 / 234` for installed hash
`FD21EB32A49B734B9CF6F5A843FC3D6707405C2DC64358DFCE98A2EC480253DA`.

### Slice 4: Quarantine Legacy Runtime

- Move old runtime functions into a clearly named legacy/quarantine unit.
- Remove ordinary live calls.
- Leave only explicit brain-scheduled fallback entry points where still needed.

Status: active. The broad route-sector authority proof API has been renamed
from `ResolveAuthorityRelevance` to
`ResolveBrainScheduledAuthorityVerification`, and it now refuses to run without
an explicit schedule reason. The unused
`RefreshAcceptedAuthorityProgress` public API and its progress-only helper code
were removed. The regression harness still exercises the verifier with an
explicit harness schedule reason; ordinary live refresh does not call this
path. Release build passed and full harness passed `234 / 234` for installed
hash
`C9150317B9EA79BEDA890A52295E2F72B45BEEFD8C9821B7BA14508ECE210FFA`.

Follow-up update: Workflow selection no longer runs Controller Relevance as a
provisional board builder. `ResolveBrainOwnedWorkflowSelection` now derives
`WorkflowSignals` directly from radio facts and calls
`ResolveWorkflowStageFromSignals`, so phase decisions use narrow workflow facts
instead of departure/enroute board snapshots. Release build passed and full
harness passed `234 / 234` for installed hash
`1A22A8262A39F6C1FD510FD62A96B5EDB8F55E1E176C4ECC9699B99F60501456`.

Follow-up update: The plugin now emits diagnostics-only Engineer 3 trace lines
for future live validation. `event=radio-board-candidate-diff` records raw
radio-board candidate diffs with phase and route-polygon context, and
`event=candidate-completion-trace` records post-publisher completion decisions
with displayed/hidden state. This is black-box evidence only; runtime workflow,
relevance, display intent, UI wake behavior, and the old authority path are
unchanged. Release build passed and full harness passed `234 / 234` for
installed hash
`3ACE28B2E521493FA39A01444ADCFF6E7451B6292426257301EC8F54E992D363`.

Follow-up update: KONT departure APP/DEP filtering now uses a removable
`modules/terminal_authority` fact worker plus brain-owned terminal-authority
cache/request logic. Controller Relevance accepts departure APP/DEP only when
the candidate owner matches the cached terminal authority owner for the
departure airport; KONT accepts `SCT_APP` and rejects `SAN_W1_APP` /
`LAS_F_APP` as owner mismatches. Version 1 fails closed: unproven APP/DEP is
rejected/hidden with a diagnostic reason, not displayed as unknown. Release
build passed, the focused KONT regression passed, and full harness passed
`237 / 237` for installed hash
`2C106BBA63A742F4073715263565D048703521210A20389EC5AAFA3E2BFC87A4`.

Contract cleanup note: the Version 2 unknown/magenta/manual visible-hidden
idea must not be implemented in the Version 1 runtime. The plugin still has
minimal terminal-authority worker shell wiring; do not expand it. The next
plugin cleanup should remove feature-specific worker invocation from
`plugin/src/XVatsimPlugin.cpp` behind a generic brain-owned runtime dispatcher.

### Slice 5: Convert Or Retire Old Modules

- Convert departure/arrival/enroute modules to fact-only workers, or remove
  them from the Engineer 3 runtime.
- No module may decide UI display truth.

### Slice 6: Remove Ambiguous State Types

- Split candidate facts, accepted completion facts, display intent rows, and UI
  view rows into separate structs.
- Stop reusing `ModuleBoardSnapshot` as both raw board and final display board.

Status: active. The final UI board now uses `FinalDisplaySnapshot` /
`FinalDisplayStationSnapshot`, the live UI path no longer consumes
`ModuleBoardSnapshot` as final display truth, and Display Intent no longer
publishes or stages display-mutated enroute rows as runtime module board state.
Raw worker board rows also no longer carry `next` or `standby` display flags.
Raw worker board rows also no longer carry `annotation` display text or
`displayRelation` state, and raw module boards no longer carry
`displayStations`. Workflow selection also no longer borrows provisional
relevance boards; remaining work is in the broader legacy module quarantine and
plugin-shell reduction.

## Guardrails

- No broad cleanup without a passing harness after each slice.
- No old authority proof in normal UI refresh.
- No UI state reused as relevance input.
- No candidate displayed without a brain-approved accepted completion.
- No module-to-module live calls.
- No repeated worker work when input hashes are unchanged.
- No Version 2 unknown/magenta/manual visible-hidden controller rows in the
  Version 1 runtime.
- No new feature-specific authority/relevance/display scheduling in the plugin.
