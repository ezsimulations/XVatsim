# Brain Cadence Coordinator Status

Last updated: 2026-05-18

## Block 1: Baseline And Instrumentation

Status: complete.

Production behavior changed: no.

Authority truth rules changed: no.

Implemented diagnostics:

- Expensive refresh work now records a typed job line in `xvatsim_diagnostics.log`.
- Route resolution logs job name, reason, duration, source generations, route key, cache status, and result counts.
- Authority station resolution logs job name, reason, duration, source generations, cache/status, and candidate counts.
- Authority relevance logs job name, reason, duration, source generations, cache/proof status, result counts, work stage, window, and deferred count where available.
- Departure and arrival airport coverage log cache/build status and sector counts.
- Departure, arrival, and enroute board snapshots log board cache hits/builds and station counts.
- Workflow, radio-range resolve, overlay build, and overlay update now appear in the per-refresh job summary.

Verification:

- Release build passed.
- Full regression harness passed: `209 / 209` scenarios.
- Installed X-Plane plugin hash:
  `6F8CCBE263C45AAD03392B3E141FF613BF80E1F75E7241C399147D142255500D`

Next contract block:

- Block 2: Typed Brain Work Model.

## Block 2: Typed Brain Work Model

Status: complete.

Production behavior changed: no.

Authority truth rules changed: no.

Implemented model:

- Added typed `BrainWorkType`, `BrainWorkPriority`, `BrainWorkReason`, `BrainWorkBudget`, `BrainWorkResultStatus`, and `BrainWorkCacheStatus`.
- Added `BrainWorkItem`, `BrainWorkResult`, `BrainWorkTarget`, and `BrainWorkSourceGenerations`.
- Added `BrainDataSnapshot` with route polygon state, accepted controller evidence summaries, rejected candidate summaries, source generations, and pending state.
- Added deterministic queue helpers for heavy-work classification, stable IDs, and priority ordering.

Verification:

- Added regression scenario `brain_work_model_orders_priority_and_metadata.scn`.
- Release build passed.
- Full regression harness passed: `210 / 210` scenarios.
- Installed X-Plane plugin hash:
  `5618277EE3867811292EC3A2A363B888C8E506B787D745639DAE4042D696C71D`

Next contract block:

- Block 3: Scheduler In Shadow Mode.

## Block 3: Scheduler In Shadow Mode

Status: complete.

Production behavior changed: no.

Authority truth rules changed: no.

Implemented scheduler:

- Added `BrainWorkScheduler`.
- Scheduler sorts typed work by contract priority.
- Scheduler allows at most one heavy job per cycle.
- Scheduler allows light/medium work to remain runnable while extra heavy jobs are deferred.
- Scheduler returns requested, runnable, deferred, and heavy-job counts.

Implemented shadow diagnostics:

- Live plugin still executes the old production path.
- Existing Block 1 diagnostic jobs are translated into typed brain work items only when diagnostics are logged.
- At this historical point, diagnostic log lines included
  `shadowScheduler=...`.
- The shadow summary reports requested job count, heavy count, runnable count, deferred count, heavy deferred count, and whether the old refresh asked for multiple heavy jobs.
- This identifies where the future production scheduler would have prevented multi-heavy-job refresh spikes.

Retirement note 2026-05-21:

- The shadow scheduler diagnostic path was later removed from the live plugin
  after Engineer 3 became the unconditional runtime entry. Current diagnostics
  no longer log `shadowScheduler=...`; the plugin shell reports actual recorded
  jobs and no longer reconstructs a parallel scheduler model from old job names.

Verification:

- Added regression scenario `brain_scheduler_shadow_defers_extra_heavy_work.scn`.
- Release build passed.
- Full regression harness passed: `211 / 211` scenarios.
- Installed X-Plane plugin hash:
  `837401FE464548BF9219B73C0A4A1565986B003A93EC2DC8C9F42993D13C1190`
- Real X-Plane runtime gate, KPVD to KMSP as DAL100, passed visible behavior:
  UI awoke in `DEP`, displayed `BOS_CTR`, `KPVD CTAF`, and `PVD_APP`, with route `KPVD -> KMSP`.
- Runtime diagnostics showed one old-path multi-heavy refresh when the VATSIM plan became usable:
  total `2065ms`, with `RouteResolve=972ms`, `AuthorityRelevance=567ms`, and `DepartureAirportCoverage=520ms`.
- Shadow scheduler correctly identified that refresh as `multiHeavy=1` and would have deferred two heavy jobs:
  `RunAuthorityProof` and `ResolveDepartureAirportLocal`.
- Follow-on refreshes after cache fill were stable at about `4-6ms`, confirming the repeated-stutter pattern was not present in this run.

Next contract block:

- Block 4: Route-Scoped Authority Plan.

## Block 4: Route-Scoped Authority Plan

Status: complete.

Production behavior changed: diagnostics/cache only. Module ownership and authority truth rules remain unchanged.

Authority truth rules changed: no.

Implemented active flight map model:

- Added typed `RouteAuthorityPlan` as the route-scoped active flight map.
- Added `BrainRoutePolygonState` metadata for sequence, entry/exit distance, current/next/arrival flags, coverage kind, source ownership, and dirty/unresolved state.
- Added `BuildRouteAuthorityPlanFromRouteSectorSnapshot(...)` to convert existing exact route traversal output into the active route authority plan.
- The global polygon/source catalog remains read-only; the active plan is a small ordered view for the current flight.
- Live plugin now builds/caches the active route plan after route resolution and logs it as `RouteAuthorityPlan`.
- Departure, Enroute, Arrival, and authority acceptance logic were not cut over in this block.

Verification:

- Added regression scenario `brain_route_authority_plan_builds_ordered_active_map.scn`.
- Block 4 scenario proved ordered active map sequence, current/next/arrival flags, and source ownership metadata.
- Release build passed.
- Full regression harness passed: `212 / 212` scenarios.
- Installed X-Plane plugin hash:
  `9F3449A6711E60E8A71CA7F7E1E41A1DC1E0EE5510B553A881EE8A1CDCEE32EB`

Next contract block:

- Block 4A: Reroute And Diversion Rebuild Safety.

## Block 4A: Reroute And Diversion Rebuild Safety

Status: complete for the route-plan cache layer.

Production behavior changed: diagnostics/cache safety only. Module ownership and authority truth rules remain unchanged.

Authority truth rules changed: no.

Implemented route rebuild safety:

- Added `UpdateRouteAuthorityPlanCache(...)` as the single route-plan cache update seam.
- A changed route identity rebuilds the active `RouteAuthorityPlan` from the read-only global catalog output.
- A pending or unresolved route rebuild preserves the last proven route plan instead of publishing an empty/poisoned map.
- Pending route rebuilds are explicitly marked with `pendingRebuild` and `usingLastProvenPlan`.
- Rebuild diagnostics now classify the reason, including destination, departure, route hash, center-boundary generation, or authority-catalog generation changes.
- Live plugin route-plan refresh now goes through the brain cache seam instead of hand-rolled cache logic.
- Departure, Enroute, Arrival, and authority acceptance logic were not cut over in this block.

Verification:

- Added regression scenario `brain_route_authority_plan_rebuilds_and_preserves_last_proven.scn`.
- Block 4A scenario proved changed-route rebuild and unresolved-route last-proven preservation.
- Release build passed.
- Full regression harness passed: `213 / 213` scenarios.
- Installed X-Plane plugin hash:
  `6119B6A31D654E112CB4028F00FC643DA9BC5BE2B0AA0D88BB4CF195C42A2AB3`

Next contract block:

- Block 5: Departure Ownership Cutover.

## Block 5: Departure Ownership Cutover

Status: complete through Block 5C.

Production behavior changed: yes, for the departure snapshot seam only.

Authority truth rules changed: no.

Implemented Block 5A:

- Added typed `DepartureAuthoritySnapshot`.
- Added `BuildDepartureAuthorityWorkQueue(...)` to express the intended brain order: airport local, departure terminal, current center, then departure board snapshot.
- Added `UpdateDepartureAuthoritySnapshotCache(...)` as the brain-owned departure snapshot cache seam.
- Pending/stale departure snapshot rebuilds preserve the last proven safe departure board instead of publishing a broken snapshot.
- Live plugin now passes departure airport coverage and the departure board through the brain-owned snapshot cache before workflow/display consumes them.
- Added `BrainDepartureSnapshot` diagnostics so logs show cache status, pending state, last-proven use, station count, and generation.
- Existing authority evidence rules and existing Departure Module collection rules were not changed.

Implemented Block 5B:

- Added production `ExecuteScheduledDepartureAuthority(...)`.
- Live departure execution now builds a brain departure work queue, sends it through `BrainWorkScheduler`, and only runs the runnable departure jobs.
- Scheduler-controlled departure work now covers airport coverage, departure terminal/local consumption, current center consumption, and departure board snapshot publication.
- Added `BrainDepartureSchedule` diagnostics so logs show requested, runnable, deferred, run list, and defer list.
- Added `BrainDepartureCurrentCenter` diagnostics so current-center consumption is visible in the departure sequence.
- Fixed departure work ordering so current-center work cannot sort ahead of airport-local and terminal work.
- Existing authority evidence rules and existing Departure Module collection rules were not changed.

Verification:

- Added regression scenario `brain_departure_authority_snapshot_owns_order_and_last_proven.scn`.
- Block 5 scenario proved ordered departure work, scheduler runnable/deferred behavior, and last-proven preservation.
- Block 5C real X-Plane runtime test `CYMJ -> KABQ DAL102` showed the scheduled departure path behaved correctly:
  `BrainDepartureSchedule`, `DepartureAirportCoverage`, `BrainDepartureCurrentCenter`, `DepartureBoard`, and `BrainDepartureSnapshot` all appeared in diagnostics.
- The same runtime test showed no visible stutters, but also exposed remaining unscheduled `AuthorityRelevance` proof spikes outside the departure snapshot seam.
- Release build passed.
- Full regression harness passed: `214 / 214` scenarios.
- Installed X-Plane plugin hash:
  `3945D1491A387250824BED634F7F5E988604E3349555791B9CA554EC2F88A3F7`

Next contract block:

- Block 6: Enroute Ownership Cutover.

## Block 6: Enroute Ownership Cutover

Status: in progress. Blocks 6A, 6B, 6C, 6D, and 6E complete.

Production behavior changed: yes, for authority relevance cadence ownership only.

Authority truth rules changed: no.

Implemented Block 6A:

- Added a brain-owned production gate around `AuthorityRelevance`.
- Added `BrainAuthoritySchedule` diagnostics so runtime logs show whether authority proof was run, deferred, or satisfied from a last-proven cadence hit.
- Added a last-proven authority relevance snapshot cache keyed to the active route authority plan.
- If route build already consumed the heavy-job slot, authority proof is deferred instead of running in the same refresh.
- If the active authority snapshot is still inside the safe watch cadence, source-feed twitches reuse the last proven authority snapshot instead of immediately rebuilding proof.
- Empty windows keep a short watch cadence; proven controlled windows relax to the existing slower cadence.
- Existing evidence acceptance/rejection rules were not changed.

Implemented Block 6B:

- Reduced the auxiliary radio RX activity flight-loop callback from `20 Hz` to `10 Hz`.
- Increased the RX latch window so the lower poll rate should not create RX indicator flicker.
- Throttled repeated optional/missing radio dataref lookup retries instead of searching every main refresh.
- Replaced per-sample heap allocation for xPilot station callsign datarefs with a bounded stack buffer.
- Existing radio truth rules and displayed controller rules were not changed.

Implemented Block 6C:

- Added microsecond-level baseline refresh instrumentation to the live plugin diagnostics.
- Diagnostic lines now include `totalUs` alongside `totalMs`.
- Diagnostic lines now include `usTimings=...` with aircraft sampling, xPilot polling, VATSIM feed polling, controller snapshot build, flight-plan sampling, network-plan polling, radio sampling, controller-message handling, manual-query handling, flight-context updates, CTAF lookup, route resolution, route-authority plan, authority-station resolution, authority relevance, departure/arrival/enroute board work, workflow, standby assist, wake decision, radio-range resolve, overlay build/update, display logging, tracked total, and untracked total.
- Existing authority truth rules, workflow rules, display rules, and controller acceptance/rejection rules were not changed.

Implemented Block 6D:

- Used the Block 6C `usTimings` output to identify steady on-ground workflow cost as the dominant baseline loop cost after authority proof cadence was tamed.
- Gated departure terminal polygon geometry so the live refresh only evaluates departure terminal coverage when it can affect workflow state.
- On-ground frames, arrival-awake frames, and already-released departure frames no longer run departure terminal containment checks.
- `IsInsideAirportTerminalCoverage(...)` now only runs after the cheaper `CanEvaluateAirportTerminalCoverage(...)` says terminal coverage exists and the workflow can actually use it.
- Existing authority truth rules, workflow rules, display rules, and controller acceptance/rejection rules were not changed.

Implemented Block 6E:

- Used the post-6D runtime log to identify the remaining visible PluginAdmin flicker as an authority relevance proof burst.
- Confirmed steady frames dropped to about `1.0-1.2ms`, with `workflow=0us`; the remaining burst was `AuthorityRelevance` rebuilding after a `signature-or-source-dirty` input change.
- Tightened authority relevance dirty detection so transceiver station candidates only dirty the heavy scoped relevance signature when the station is proximate to the active route authority polygons.
- This aligns the heavy signature with the existing route-scoped watch signature and prevents unrelated worldwide transceiver/controller changes from forcing a proof rebuild.
- Existing authority truth rules, workflow rules, display rules, and controller acceptance/rejection rules were not changed.

Verification:

- Release build passed.
- Saved regression scenarios passed: `214 / 214`.
- Final release gate active-source scan and customer-package text scans passed.
- Final release gate package/zip hash checks failed only because the active store package and zip still contain the older packaged `.xpl`, which is expected during active development.
- Built and installed X-Plane plugin hash:
  `13D77D5FA2D16C8624649C12401AD915618A6235DD1722DC05F695B007117875`

Next Block 6 step:

- Block 6F: rerun a short real X-Plane connect-and-hold baseline and confirm the `signature-or-source-dirty` authority proof flicker no longer repeats from unrelated worldwide controller/transceiver changes; if steady cost remains visible, optimize the `flightPlan` sampler baseline next.

## 2026-05-18 Runtime Follow-Up: NY_CTR Timing And Progress Refresh

Status: implemented and installed for live testing.

Real flight observed:

- `TJSJ -> KBUF`, callsign `JBU634`.
- Departure and ENROUTE handoff behaved correctly.
- No heavy stutters or sustained PluginAdmin cost were observed.
- `NY_CTR 125.325` was correctly identified over the unusual KZNY Atlantic
  polygon, proving the authority evidence engine handled a difficult suffix /
  polygon case that the old string-first path likely would have missed.

Issue found:

- `NY_CTR` appeared late, around `71nm` instead of the intended `200nm`
  authority window.
- Controller distance displayed in large stale chunks, roughly `40nm` apart,
  because the board mileage came from the cached heavy authority proof snapshot
  instead of a cheap live progress refresh.

Fixes implemented:

- Added route-progress proof tracking so the resolver's internal authority
  cache cannot reuse a stale empty proof when the route authority window has
  changed.
- Added `RefreshAcceptedAuthorityProgress(...)` so already-proven controllers
  have their route-entry distance and aircraft-inside/active state recomputed
  cheaply from current aircraft position and route geometry.
- The heavy authority evidence rules remain unchanged. This is a display and
  cadence correctness fix, not a controller acceptance shortcut.

Verification:

- `XVatsimPlugin` release build passed.
- `XVatsimRegressionHarness` release build passed.
- Full regression harness passed: `214 / 214` scenarios.
- Installed X-Plane plugin hash:
  `E859C9C5ACC1219B0914A1B59FF81ABF026572463D51A22AF826EB2E0ECE83AA`

Next validation:

- Live-test an enroute center window again and confirm the UI wakes at/near the
  `200nm` authority window.
- Confirm controller mileage updates smoothly instead of in large proof-cadence
  chunks.
- Confirm active state turns green promptly when entering the polygon and clears
  promptly after exiting.
- If this passes, move to the final legacy/stale-code audit before release
  packaging.

## 2026-05-19 Runtime Follow-Up: UAL298 KBUF To KIAD Authority Proof Cadence

Status: implemented and installed for live retest.

Live test observed:

- `KBUF -> KIAD`, callsign `UAL298`.
- Route resolution was correct and exact.
- Route-scoped authority plan built `KZOB` as current, `KZNY` as next, and
  `KZDC` as arrival.
- No authority-accuracy failure was found in this pass.

Issue found:

- The previous NY_CTR timing fix made route progress invalidate the heavy
  authority proof too aggressively.
- Aircraft movement and changing route-entry distances were causing repeated
  `route-progress-dirty` / `signature-or-source-dirty` proof rebuilds, with
  observed spikes in the `130-650ms` range.
- This violated the Brain Cadence contract: distance/inside progress should be
  cheap display freshness, not controller-truth recomputation.

Fix implemented:

- Heavy authority proof signatures now use structural route scope only:
  active route authority identities, controller patterns, source generations,
  stage/window state, and source/feed evidence.
- Aircraft coordinates and changing sector entry distances no longer dirty the
  authority truth proof.
- Already-accepted controllers still receive the lightweight
  `RefreshAcceptedAuthorityProgress(...)` update for smooth distance and
  inside/active status.
- Controller acceptance/rejection truth rules were not changed.

2026-05-21 Engineer 3 cleanup note: this public
`RefreshAcceptedAuthorityProgress(...)` API has since been retired while
quarantining broad authority proof behind
`ResolveBrainScheduledAuthorityVerification`.

Verification:

- `XVatsimPlugin` release build passed.
- `XVatsimRegressionHarness` release build passed.
- Full regression harness passed: `214 / 214` scenarios.
- Installed X-Plane plugin hash:
  `8C119B0EEBC2FA0C4BCB9F72B611E39088B057CBBC9AB03384DA659A67B994EF`

Next validation:

- Rerun a short live connect/flight pass and confirm `route-progress-dirty`
  no longer repeats from ordinary aircraft movement.
- Confirm authority proof spikes do not recur while route polygons stay the
  same.
- Confirm a center still appears when a new route authority polygon enters the
  `200nm` work window.
