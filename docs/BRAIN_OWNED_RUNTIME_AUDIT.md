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

## Current Live Runtime

The live runtime entry is `RefreshOverlayFromBrain`, which must route directly
to `RefreshOverlayFromBrainEngineer3`.

Engineer 3 live flow:

1. Sample aircraft, xPilot, VATSIM, controller feed, flight plan, network plan,
   and radios.
2. Update brain-owned flight context.
3. Brain runs the Route Polygon Worker.
4. Brain runs the Radio Range Worker.
5. Brain performs provisional relevance only to support workflow selection.
6. Brain phase-gates the radio board.
7. Brain runs Controller Relevance Worker for accepted/rejected candidate facts.
8. Brain Publisher filters accepted completions, runs Display Intent, publishes
   the final phase snapshot, and marks displayed completions.
9. UI renders only the final brain-approved display snapshot.

## Ownership Map

### Brain / Runtime Orchestration

- `plugin/src/XVatsimPlugin.cpp`
  - `RefreshOverlayFromBrainEngineer3`
  - `RefreshBrainRoutePolygonSnapshot`
  - `BuildEngineer3RadioSnapshot`
  - `RefreshBrainControllerRelevance`
  - `RunBrainPublisher`

This is shrinking toward the correct shape. The plugin still hosts the live
X-Plane shell, input sampling, and diagnostics, but controller relevance,
relevance cache ownership, and publisher ownership have moved into brain-owned
source files.

### Fact Workers

- `modules/transceiver_resolver`
  - Radio reachability facts.
  - Must not decide route ownership or UI display.
- `modules/route_sector`
  - Route polygon facts and route-scoped authority facts.
  - Heavy authority proof must remain explicitly brain-scheduled only.
- `brain/src/RoutePolygonTransition.cpp`
  - Route progress/current/next polygon transition facts.
- `brain/src/RadioReachableSnapshot.cpp`
  - Radio board grouping, hashes, phase gating, and verification feed shaping.
- `brain/src/BrainControllerRelevanceWorker.cpp`
  - Controller relevance matching, accepted/rejected candidate completions, and
    route-polygon display relation facts.

### Brain Publisher / Display Intent

- `brain/src/BrainDisplayIntent.cpp`
  - Owns current-vs-next display relation, orange distance annotation, final
    board assembly.
- `brain/src/PhaseSnapshotPublisher.cpp`
  - Owns last-proven phase snapshot reuse.
- `brain/src/BrainOwnedRuntime.cpp`
  - Owns accepted-completion filtering, CTAF/UNICOM replacement, Brain Display
    Intent invocation, phase snapshot publishing, and displayed-completion
    marking.

### UI Renderer

- `brain/src/BrainOrchestrator.cpp`
  - Converts the final brain-approved board into overlay text/tone.
- `modules/overlay`
  - Renders the view model.

UI must not trigger authority, relevance, workflow, route, or fallback work.

### Legacy / Quarantine

These paths are not the Engineer 3 live engine and must be removed or isolated:

Removed from `plugin/src/XVatsimPlugin.cpp` and the old core display surface
by installed hash
`20866286AEB014965999BA2608D7B8179BF3BFDE75E6BC73138EE8D9CEC29759`:

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

Still contract debt outside the live Engineer 3 path:

- `modules/enroute::EnrouteModule::Collect`
- `modules/departure::DepartureModule::Collect`
- `modules/arrival::*::Collect`

Some of this code may remain temporarily for harness coverage or future
fallback APIs, but none of it may be reachable from ordinary live UI refresh.

## Known Contract Risks

1. `XVatsimPlugin.cpp` is a god file.
   Brain orchestration, worker adapters, publisher logic, old runtime, UI update,
   diagnostics, and command handling are mixed together.

2. Legacy fact modules still exist in the repo.
   The old plugin refresh body and plugin-owned live board caches have been
   removed. Older departure/arrival/enroute collectors still compile for
   library/harness coverage, but they no longer assert `displayStations`; the
   next cleanup step is to rename/extract them as explicit fact-only workers or
   retire the unused libraries.

3. `BoardStationSnapshot` mixes fact state and display state.
   Route-entry distance, display relation, annotation, active/next flags, and
   online/tuned facts share one struct. This caused the OAK `0nm` failure when
   display-ready state was reused as relevance truth.

4. Workflow selection depends on provisional relevance.
   Engineer 3 currently runs a provisional relevance pass before final phase
   gating. That may be acceptable short-term, but it should become an explicit
   brain workflow input instead of an implicit board-building dependency.

5. Old modules can still create display boards from authority snapshots.
   The live Engineer 3 path should not rely on those modules for display truth.
   Their behavior must be converted into fact-only workers or quarantined.

6. Heavy authority proof remains compiled near the runtime.
   It is guarded, but the final architecture should expose only an explicit
   brain-scheduled verifier API.

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
diagnostics only. Release build passed and full harness passed `234 / 234` for
installed hash
`20866286AEB014965999BA2608D7B8179BF3BFDE75E6BC73138EE8D9CEC29759`.

### Slice 4: Quarantine Legacy Runtime

- Move old runtime functions into a clearly named legacy/quarantine unit.
- Remove ordinary live calls.
- Leave only explicit brain-scheduled fallback entry points where still needed.

### Slice 5: Convert Or Retire Old Modules

- Convert departure/arrival/enroute modules to fact-only workers, or remove
  them from the Engineer 3 runtime.
- No module may decide UI display truth.

### Slice 6: Remove Ambiguous State Types

- Split candidate facts, accepted completion facts, display intent rows, and UI
  view rows into separate structs.
- Stop reusing `ModuleBoardSnapshot` as both raw board and final display board.

## Guardrails

- No broad cleanup without a passing harness after each slice.
- No old authority proof in normal UI refresh.
- No UI state reused as relevance input.
- No candidate displayed without a brain-approved accepted completion.
- No module-to-module live calls.
- No repeated worker work when input hashes are unchanged.
