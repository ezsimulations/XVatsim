XVatsim Regression Harness
==========================

Purpose
-------
This harness replays core XVatsim logic without loading X-Plane.
It currently exercises the shared workflow engine used by the live plugin plus the
deterministic replay seams we are extracting behind it:

- departure/enroute/arrival stage transitions
- departure terminal hold/release behavior
- display-board composition for departure, enroute, and arrival
- real ENROUTE controller matching
- synthetic route-sector traversal against explicit polygons
- route token grammar classification
- route text to waypoint/nav-graph resolution

Usage
-----
Executable:

  C:\Users\DARRON\OneDrive\Documents\XVatsim\build\tools\XVatsimRegressionHarness.exe

Run with a scenario file:

  XVatsimRegressionHarness.exe <scenario-file>

Example:

  XVatsimRegressionHarness.exe tools\regression_harness\scenarios\kont_departure_release.scn

Owner route scenario builder
----------------------------
For SimBrief/X-Plane `.fms` files, use the helper workflow in:

  tools\user_route_scenarios

The helper reads an `.fms` route, combines it with a simple controller situation
file, creates a regression `.scn`, and runs it immediately.

Double-click:

  tools\user_route_scenarios\Create_XVatsim_Scenario_From_FMS.bat

Or run directly:

  powershell -NoProfile -ExecutionPolicy Bypass -File tools\user_route_scenarios\New-FmsHarnessScenario.ps1 -FmsPath "C:\X-Plane 12\Output\FMS plans\KORDKSDF01.fms"

The first run creates a situation template to edit. The second run generates the
scenario under `tools\regression_harness\scenarios` so it can become a permanent
learning case.

Scenario format
---------------
The format is a simple key=value text file. Blank lines and lines starting with # are ignored.

Common keys:

- name=KONT Departure Release
- now_seconds=1000
- state.flight_active=true
- state.departure_released=false
- state.arrival_awake=false
- state.airborne_since_seconds=700
- flight.departure_icao=KONT
- flight.destination_icao=KPHL
- aircraft.valid=true
- aircraft.on_ground=false
- aircraft.latitude_deg=34.35
- aircraft.longitude_deg=-118.40
- radio.com1_active=122.800
- departure.terminal_coverage_known=true
- departure.inside_terminal_coverage=false
- departure.coverage.stale=false
- arrival.coverage.stale=false

Station lines:

- departure.station=role=Approach;callsign=SCT_APP;frequency=128.050;online=true;offline=false
- enroute.station=role=Center;callsign=LAX_CTR;frequency=125.800;online=true;offline=false;sectorActive=true;routeEntryDistanceNm=0

Expectations:

- expect.stage=Enroute
- expect.reason=departure-released
- expect.departure_location_confirmed=true
- expect.display_source=Enroute
- expect.display_callsigns=LAX_CTR,PHL_CTR

V2 operating-mode foundation replay
-----------------------------------
The Step 2 operating-mode probes exercise the brain-owned IFR/VFR preference,
the settings-store source fact, reset preservation, fail-soft persistence, and
IFR/VFR output parity. They do not enable VFR controller behavior.

Operating-mode inputs:

- operating_mode.probe=settings-load|round-trip|selection|reset-preservation|missing-flight-plan|parity|persistence-failure
- operating_mode.settings_entry=<missing>|<empty>|<malformed>|<unknown>|ifr|vfr
- operating_mode.initial_mode=IFR|VFR
- operating_mode.selection_requests=IFR,VFR,...
- operating_mode.round_trip_modes=IFR,VFR
- operating_mode.reset_paths=runtime,cache-preserving,session,cold-dark,invalid-aircraft,xpilot-disconnect,xpilot-reconnect,callsign-change,plugin-disable-enable
- operating_mode.parity_stage=Departure|Enroute|Arrival
- operating_mode.persistence=temporary|unavailable
- operating_mode.processing_cycles=<nonnegative integer>

Operating-mode expectations:

- expect.operating_mode=IFR|VFR
- expect.operating_mode_load_status=missing|valid|invalid|unavailable
- expect.operating_mode_source=default|settings-store|pilot-menu
- expect.operating_mode_reason=<stable reason>
- expect.operating_mode_generation=<nonnegative integer>
- expect.operating_mode_change_count=<nonnegative integer>
- expect.operating_mode_persistence_requests=<nonnegative integer>
- expect.operating_mode_save_attempts=<nonnegative integer>
- expect.operating_mode_save_successes=<nonnegative integer>
- expect.operating_mode_request_source=pilot-menu|none
- expect.operating_mode_request_reason=explicit-selection|already-active|none

V2 Step 4 METAR contract replay
--------------------------------
The 67 files named v2_step4_*.scn use:

  step4.probe=<locked probe name>

They exercise production brain policy and production parser/presentation code
with proof-only isolated transport binding. Corrective real-WinHTTP cases bind
only to `127.0.0.1` on an ephemeral port and never contact VATSIM or another
external address. Coverage includes target ownership, lookup interaction,
VATSIM-only URL and response rules, send-completion callback registration,
successful/failed terminal ledgers, parsing and category boundaries, minimal
ORB strings and tones, freshness, history, lifecycle cleanup-only draining,
cooperative shutdown, 100,000-cycle unchanged-idle work, and normal-binary
source/fixture isolation. The approved complete saved-scenario count is 561.
- expect.operating_mode_state_unchanged=true|false
- expect.operating_mode_reset_preserved=true|false
- expect.operating_mode_no_automatic_vfr=true|false
- expect.operating_mode_round_trip=IFR,VFR
- expect.operating_mode_parity=true|false
- expect.operating_mode_allowed_parity_difference=state-diagnostic-only
- expect.operating_mode_retry_count=<nonnegative integer>
- expect.operating_mode_processing_cycles=<nonnegative integer>
- expect.operating_mode_missing_plan_processed=true|false
- expect.operating_mode_reset_trace=<ordered path:decision list>
- expect.operating_mode_pipeline_runs=<nonnegative integer>
- expect.operating_mode_pipeline_stage=Departure|Enroute|Arrival
- expect.operating_mode_pipeline_controller_callsigns=<ordered callsign list>
- expect.operating_mode_pipeline_display_callsigns=<ordered callsign list>

An idempotent selection reports the request source/reason separately. It must
not change the brain-owned mode, state source, state reason, or generation.

The three parity scenarios supply ordinary aircraft, workflow, radio,
controller-feed, transceiver, and route facts. The harness resolves the real
workflow stage, builds the radio-reachable controller snapshot, runs the brain
controller-relevance worker and brain-owned publisher, and builds the overlay
view once from an IFR brain state and once from a VFR brain state. The expected
stage and non-empty controller/display callsigns make the comparison
non-vacuous; no board or display result is assigned before those calls run.

The missing-flight-plan scenario commits an actual unavailable
FlightPlanSnapshot through the brain-owned sampling path and then runs an
ordinary processing cycle. It asserts the missing snapshot was processed and
that no mode change or persistence request occurred.

The reset trace is ordered and names the actual production boundary decision
or brain-owned reset sequence exercised for runtime, cache-preserving, session,
cold-and-dark, invalid-aircraft, xPilot disconnect, xPilot reconnect, callsign
change, and plugin disable/enable paths. Each path starts from an independently
seeded VFR selection and must preserve mode, source, reason, and generation.

After the intentional unavailable-store save failure, processing_cycles runs
ordinary flight-plan sampling plus workflow/controller/publisher/overlay work.
The save-attempt counter is observed across those cycles; retry_count must stay
zero because no ordinary-processing call path invokes settings persistence.

V2 Step 3 ORB rail and drawer contract
---------------------------------------
Step 3 scenarios are executable scripts rather than approved-name selectors.
Each repeated step3.action line invokes a production-facing brain or overlay
operation. Each repeated expect.step3 line compares an operation-produced value:

- step3.action=<verb>:<arguments>
- expect.step3=<observation-path>=<literal or @other-observation-path>

Supported actions cover runtime creation/destruction, operating-mode selection,
history acceptance, ORB requests, presentation projection, concrete lifecycle
entry points, cache-preserving reset, production layout and hit testing, wrapping,
wheel routing, retained presentation updates, and the existing main-card pipeline.

Render evidence is never produced by an action-to-counter table. The harness
creates one production-facing AccessoryPresentationState and drives projected
brain snapshots, layout generations, selection changes, hidden cache changes,
duplicate insertions, and scrolling through that retained object. It observes the
object's active snapshot identity, selection/history/layout generations, render
signatures, cached entry keys/bodies/sequences, live drawer offset and measured
maximum, final marker, and counters. Unchanged updates reuse the same snapshot,
layout, and main-card signature for all 10,000 iterations.

Hit proof derives center, circle-edge, immediately-outside, and inter-ORB gap
points from production layout. The click request used by the exactly-once probe
is constructed from the drawer identity returned by the hit test. Lifecycle
preserve/clear probes seed METAR, ATIS, and PDC together and compare keys, bodies,
accepted sequences, retained bytes, and generations for all three histories.
Callsign-change probes pass the actual old and new callsigns to the dedicated
production lifecycle entry point and inspect the subsequent projection.

The locked compact geometry contract is closed 430 x 374, open 430 x 506, with
52 design-pixel ORBs. The rail spans design y=320..372. METAR, ATIS, and PDC span
x=118..170, 186..238, and 254..306, with centers at x=144, 212, and 280 on
y=346 and 16 design-pixel gaps. The drawer spans y=378..494, with a 6-pixel
rail-to-drawer gap and a 12-pixel open bottom margin. At scale 1.35, open height
must round to 683, leaving 37 pixels of 720p vertical capacity.
Edge clamping may touch an edge; it is not required to create a positional
margin on every edge.

Wheel-routing probes pass the resolved production layout and actual pointer
coordinates. The production router classifies the point as drawer, main card,
or unhandled before any offset changes. No scenario supplies boolean region
truth to the wheel implementation.

History-input probes lock stable keys at 128 accepted / 129 rejected, reject an
empty key and invalid UTF-8 independently in key, title, and body, preserve exact
case and non-CRLF whitespace, and permit only CRLF-to-LF conversion. Limiting
proof accounts for the visible CONTENT LIMITED marker inside the 128-byte title,
8,192-byte body, individual-entry, and 65,536-byte drawer budgets.

Stable keys are normalized from CRLF to LF before their byte limit, storage, and
exact comparison are applied. The focused contract proves that CRLF and LF forms
of the same key deduplicate. Title/body source identity is a SHA-256 digest of the
complete normalized inputs, including unambiguous big-endian title/body
byte-length fields. It is calculated only when an entry is offered to the brain,
never on an update/frame path, and before retained-content limiting. A change
beyond either retained boundary is therefore accepted as an update rather than
mistaken for a duplicate. Focused proof checks a known digest and verifies that
title-tail and body-tail changes each produce a different 32-byte digest.

Text proof calls the production Windows GDI+ measurement path directly. The
caller explicitly initializes and shuts down one measurement context outside DLL
loader activity. That context owns one reusable Bitmap and Graphics instance,
caches bounded Font resources and typography metrics by genuine font role/scale,
and is warmed at 0.85, 1.00, and 1.35 exactly as the plugin host must do. Static
startup/shutdown and per-layout GDI object construction are prohibited.

The single path uses Segoe UI, UnitPixel, AntiAliasGridFit,
GenericTypographic, NoWrap, and MeasureTrailingSpaces with the intended roles:
drawer body at 11.5 design pixels regular, drawer entry title at 11.5 design
pixels bold, ORB labels at 8.0 design pixels bold, and the OPEN indicator at 6.5
design pixels bold. Layout resolution consumes only cached typography metrics;
it never calls GDI+.

Wrapping converts each retained UTF-8 input to UTF-16 once. It submits only a
bounded portion of the remaining paragraph to GDI+, expands that window only
when the whole window fits, verifies the proposed line using the same production
no-wrap measurement, and retreats or advances only at complete UTF-16 scalar
boundaries. It never repeatedly measures the full remaining 8,192-byte suffix.
Word-boundary wrapping and UTF-8-safe character fallback consume these measured
boundaries; no harness width estimate or parallel font implementation is
permitted. allLinesFit is derived from the maximum exact measured output-line
width.

The maximum-token performance action sends 20 distinct 8,192-byte valid UTF-8
inputs through this production path without caching completed wraps. It includes
wide, narrow, mixed, near-tail-different, two-byte, three-byte, and four-byte
content; records every duration plus p50, p95, and maximum; independently checks
every exact line width; and proves exact retained reconstruction. Every operation
must be at or below 16.7 ms. The scale matrix records every measured
maximum/available drawer width plus each ORB label and OPEN measured/available
rectangle in the proof log.

The retained production drawer plan preserves keys, titles, bodies, accepted
sequences, measured title lines, and measured body lines in newest-first pairing.
Title and body lines both contribute to scroll limits. A title-only limiting
probe proves that its visible CONTENT LIMITED marker survives storage,
projection, measured wrapping, and the cached presentation plan. Duplicate and
hidden-cache updates must produce zero GDI measurement and zero render work.
Anchor proof separately covers screen-change clamping
without settings writes and an intentional open drag becoming the restorable
closed anchor. Main-card parity compares both an unchanged production signature
and a deliberately changed production signature; accessory code may report the
former unchanged but must report the latter changed without requesting accessory
or main-card raster work.

The production anchor contract is explicit rather than inferred from drawer
visibility. AccessoryAnchorState retains the current live anchor, saved pre-open
closed anchor, temporary-open-clamp state, and intentional-open-move state.
Initialize, OrdinaryRefresh, OpenDrawer, CloseDrawer, IntentionalMove, and
ScreenBoundsChanged are distinct production operations. Ordinary closed refresh
uses the current live position; only CloseDrawer may request restoration of the
saved anchor. Closed and open drag samples pass their exact clamped coordinates
through the same stateful production operation used by OverlayWindow, so the
card, rail, ORBs, drawer, and hit targets must report identical deltas. Pure
translation retains texture-local rail and drawer signatures and records zero
history visits, copies, wrapping, GDI measurement, raster requests, or upload
requests. The executable aggregate is:

- step3.action=anchor-contract:<observation-prefix>

The compact production geometry is locked at 430 x 374 closed and 430 x 506
open design pixels. The rail is x=118..306 and y=320..372, the 52-pixel ORB
centers are y=346, and the drawer is x=0..430 and y=378..494. The six-line
drawer capacity is unchanged. At scale 1.35 the open height is 683 physical
pixels, leaving exactly 37 pixels of vertical capacity on a 720-pixel screen.

Drawer capacity is not supplied by a scenario or a second renderer rule. The
production layout resolves the drawer inset, header top/height, content top and
bottom, and the rounded 13-design-pixel line advance. At scales 0.85, 1.00, and
1.35 this yields exactly six complete visible lines. BuildAccessoryHistoryLayout,
BuildAccessoryDrawerRenderPlan, and OverlayWindow's production renderer consume
that same AccessoryLayoutResult. The long-history scenario proves all measured
lines fit, the seventh line would exceed the content rectangle, the final marker
is visibly present at the measured maximum, and a further wheel step is a zero-
work no-op. The relevant executable grammar is:

- step3.action=history-layout:<snapshot-key>:<production-layout-key>:<observation-prefix>
- step3.action=drawer-render-plan:<presenter-key>:<production-layout-key>:<observation-prefix>
- step3.action=presenter-scroll-to-max:<presenter-key>:<production-layout-key>:<observation-prefix>

The production AccessoryClickFactQueue is a fixed eight-fact queue. Produce
assigns the monotonic sequence; consume removes one fact; discard removes every
pending fact without resetting that sequence. The same queue is used by
OverlayWindow. Forced-close and clear scenarios explicitly seed a pending fact,
discard it before invoking each real brain lifecycle entry point, and prove a
later consume cannot recover it. Normal hit-tested clicks still travel through
the queue to the brain exactly once. Queue grammar is:

- step3.action=click-queue-new:<queue-key>
- step3.action=click-queue-produce:<queue-key>:<METAR|ATIS|PDC>:<started-us>:<prefix>
- step3.action=click-queue-produce-hit:<queue-key>:<hit-key>:<started-us>:<prefix>
- step3.action=click-queue-discard:<queue-key>:<prefix>
- step3.action=click-queue-consume:<queue-key>:<prefix>
- step3.action=click-queue-consume-to-brain:<queue-key>:<brain-key>:<prefix>

Production ORB input dispatch is event-driven and does not use the general
250-millisecond flight-loop cadence or its 10-second initial delay. The same
AccessoryInputDispatchCoordinator used by OverlayWindow serializes each accepted
fact through its brain decision, projected presentation, render generation, and
matching completed draw. A later rapid click remains queued until the preceding
selection generation has actually drawn; a draw of another generation cannot
complete its timing record. Forced lifecycle boundaries invalidate any in-flight
fact and discard queued facts without resetting the monotonic request sequence.
The dispatcher registers no timer, recurring callback, or idle poll. Grammar is:

- step3.action=dispatch-new:<dispatcher-key>:<performance-epoch>
- step3.action=dispatch-produce:<dispatcher-key>:<queue-key>:<METAR|ATIS|PDC>:<started-us>:<prefix>
- step3.action=dispatch-begin-present:<dispatcher-key>:<queue-key>:<brain-key>:<presenter-key>:<layout-key>:<main-signature-key>:<layout-generation>:<dispatch-us>:<prefix>
- step3.action=dispatch-draw:<dispatcher-key>:<queue-key>:<presenter-key>:<completed-us>:<current|selection-generation>:<current|render-generation>:<prefix>
- step3.action=dispatch-invalidate:<dispatcher-key>:<queue-key>:<prefix>
- step3.action=dispatch-snapshot:<dispatcher-key>:<prefix>

`v2_step3_click_request_exactly_once.scn` proves prompt post-enable dispatch,
open/switch/close sequencing, rapid-input retention, exact draw-generation
matching, end-to-end action timing, and lifecycle invalidation. The deliberately
wrong draw generation cannot complete the open action. The 10,000-update scenario
also proves a newly idle production dispatcher performs no notification, begin,
binding, completion, or mismatch work.

Live performance collection uses production fixed-size histograms, not a
retained-tail sample or action-to-counter lookup. It separately records rail and
drawer rasterization, actual rail and drawer OpenGL texture upload, completed
accessory draw, open, atomic switch, close, effective scroll, brain/presentation
dispatch wall time, matching accessory-draw wall time, combined action wall time,
Windows current-thread CPU for dispatch/draw/combined work, callback wait, and
the containing X-Plane frame interval. Each category keeps an exact complete-
period count and maximum plus histogram-derived p50 and p95.

The steady-clock fields are named dispatchWallUs, actionDrawWallUs, and
combinedActionWallUs. They are never represented as CPU. Windows GetThreadTimes
user-plus-kernel deltas provide dispatchThreadCpuUs, actionDrawThreadCpuUs, and
combinedActionThreadCpuUs. All four queries occur only for an accepted action and
its matching generation-driven draw; an unavailable query invalidates that live
proof without substituting wall time. The 16,700-microsecond hard ceiling applies
to actual thread CPU and to each rasterization, OpenGL upload, and draw wall
operation. End-to-end open/switch/close latency is not mislabeled as CPU. Each
action retains the required mouse-fact, dispatch-complete, preceding-draw-entry,
matching-draw-entry, and matching-draw-complete timestamps plus expected/matching
draw ordinals. A bounded dispatch-start timestamp additionally prevents time spent
waiting behind an earlier serialized request from being mislabeled as dispatch
work. An over-ceiling end-to-end result is FRAME-CADENCE-LIMITED only when
all CPU components pass, the matching generation completes on its first eligible
draw, missedEligibleDraws is zero, arithmetic is exact, the excess is accounted
for by callback wait, and callback wait is no greater than the measured containing
frame interval plus 1,000 microseconds. A high wall result with passing thread CPU
is NON-CPU-WALL-OUTLIER, not inferred scheduler preemption. The remaining locked
classifications are CPU-WITHIN-BUDGET, CPU-FAILURE, RENDER-WALL-FAILURE, and
TIMING-UNAVAILABLE.

Open, switch, and close retain fixed histograms for brain decision, brain
projection/history copying, overlay layout/presentation, GDI+ measurement and
wrapping, and generation binding wall stages. The first eight review/failure
records are retained in a fixed array; later records increment a dropped count.
No unbounded action sample list exists.

Actual rasterizations are also aggregated by bounded reason: initial,
selection/open/close/switch, effective scale/resize, typography/layout, and
content generation. An effective scroll changes the rendered content generation.
No per-frame sample list or diagnostic line is created. The first CPU failure,
render-wall failure, non-CPU wall outlier, or unavailable timing result sets a
sticky review flag and permits one warning. Aggregate
publication is revision guarded. Disable/enable preserves the epoch and
histograms; stop cannot republish an unchanged revision. Harness grammar is:

- step3.action=perf-new:<collector-key>:<epoch>
- step3.action=perf-record-series:<collector-key>:<category>:<comma-separated-us>:<prefix>
- step3.action=perf-raster-reason:<collector-key>:<rail|drawer>:<reason>:<count>:<prefix>
- step3.action=perf-begin-dual-action:<collector-key>:<Opened|Switched|Closed>:<sequence>:<mouse-wall-us>:<dispatch-start-wall-us>:<dispatch-complete-wall-us>:<selection-generation>:<render-generation>:<expected-draw-ordinal>:<thread-cpu-available>:<dispatch-thread-cpu-us>:<brain-wall-us>:<projection-wall-us>:<overlay-wall-us>:<gdi-wrap-wall-us>:<binding-wall-us>:<prefix>
- step3.action=perf-complete-dual:<collector-key>:<draw-complete-wall-us>:<selection-generation>:<render-generation>:<draw-enter-wall-us>:<preceding-draw-enter-wall-us>:<draw-ordinal>:<action-draw-wall-us>:<thread-cpu-available>:<action-draw-thread-cpu-us>:<render-failure-category-or-none>:<render-failure-us>:<prefix>
- step3.action=perf-begin-phased-action:<collector-key>:<Opened|Switched|Closed>:<sequence>:<mouse-fact-us>:<dispatch-complete-us>:<selection-generation>:<render-generation>:<expected-draw-ordinal>:<prefix>
- step3.action=perf-complete-phased:<collector-key>:<draw-complete-us>:<selection-generation>:<render-generation>:<draw-enter-us>:<preceding-draw-enter-us>:<draw-ordinal>:<accessory-draw-cpu-us>:<prefix>
- step3.action=perf-begin-action:<collector-key>:<Opened|Switched|Closed>:<sequence>:<started-us>:<prefix>
- step3.action=perf-begin-scroll:<collector-key>:<started-us>:<prefix>
- step3.action=perf-complete:<collector-key>:<completed-us>:<prefix>
- step3.action=perf-warning:<collector-key>:<prefix>
- step3.action=perf-publish:<collector-key>:<prefix>
- step3.action=perf-snapshot:<collector-key>:<prefix>

`v2_step3_action_render_work_is_bounded.scn` locks total/percentile/maximum
selection, all six action classifications, dual-clock terminology, thread-query
success/failure truth, generation association, five stage histograms for each
drawer action, fixed eight-record storage, sticky failure, one-warning behavior,
publication guarding, and epoch preservation. The 10,000 unchanged-update proof
also requires zero thread-time queries. The scenario count remains 494;
these are strengthened assertions inside the existing 31 Step 3 scenarios, not
new scenario files.

Future source work remains outside Step 3 but its performance ownership is
locked: METAR becomes eligible only on the scheduled half-hour boundary and uses
cached-content fingerprints; ATIS reacts only to a new VATSIM feed generation
and a relevant-airport content change; PDC/private messages are event-driven.
Hidden history changes do not render. Unchanged flight cycles perform no parsing,
wrapping, history traversal, rasterization, upload, logging, or source polling.

The harness owns no alternate decision implementation, history collection,
layout calculation, or counter lookup. It retains production-returned state and
stores observations needed for comparisons. A missing action,
unknown lifecycle entry point, malformed expectation, or expectation without an
operation-produced observation is a configuration error and exits 2.

Concrete comparison failures exit 1 and emit:

STEP3_ASSERTION_FAILED: <scenario-name>: <observation-path> expected=<value> observed=<value>

The evaluator executes every declared action before checking every expectation.
There is no shared unavailable short-circuit or unconditional failure. Process
restart destroys and recreates BrainOwnedRuntimeState. Plugin disable and enable
invoke their distinct production-facing entry points; enable never issues an ORB
request, and a later explicit click is required to reopen retained history.

Replay support
--------------
The harness can now also replay the real ENROUTE controller matcher by providing route sectors
and controller feed entries directly.

Route sectors:

- route.current_sector=identifier=KZAK;entryDistanceNm=0;matchTokens=KZAK;controllerPrefixes=ZAK,OO,OOR
- route.next_sector=identifier=PHZH;entryDistanceNm=1399;matchTokens=PHZH;controllerPrefixes=HCF,HNL
- route.stale=false

Controller feed:

- xpilot.connected=true
- controller.feed_available=true
- controller.feed_stale=false
- controller.feed_force_entries=false
- controller.entry=callsign=HCF_CTR;frequency=127.650;facility=6;actionable=true;atis=false

ENROUTE expectations:

- expect.enroute_available=true
- expect.enroute_callsigns=KZAK,HCF_CTR

Controller authority compiler replay:

- authority_catalog.fir=VHHK|Hong Kong|HKG|VHHK
- authority_catalog.uir=EXAMPLE|Example Upper|EXM|EXAMPLE
- authority_polygon.vatspy=key=VHHK;name=Hong Kong;tokens=VHHK,HKG;polygon=20,112|24,112|24,116|20,116
- authority_polygon.tracon=id=SCT;name=SoCal TRACON;suffix=APP;prefixes=SCT,SOCAL;polygon=32,-119|35,-119|35,-116|32,-116
- controller.entry=callsign=HKG_W_CTR;frequency=125.320;facility=6;actionable=true;atis=false
- expect.authority_catalog_ids=VATSPY_FIR:VHHK
- expect.authority_data_gaps=<none>
- expect.authority_active_matches=HKG_W_CTR:VATSPY_FIR:VHHK:VHHK:HKG_*_CTR
- expect.authority_unmapped_callsigns=<none>
- expect.authority_polygon_ids=VATSPY_BOUNDARY:VHHK,SIMAWARE_TRACON:SCT_APP
- expect.authority_polygon_lookup_keys=VATSPY_BOUNDARY:VHHK:HKG>VHHK,SIMAWARE_TRACON:SCT_APP:SCT>SCT_APP>SOCAL>SOCAL_APP
- expect.authority_polygon_ring_counts=VATSPY_BOUNDARY:VHHK:1,SIMAWARE_TRACON:SCT_APP:1
- expect.authority_polygon_data_gaps=<none>
- expect.authority_active_polygon_matches=HKG_W_CTR:VATSPY_FIR:VHHK:VATSPY_BOUNDARY:VHHK:VHHK:HKG_*_CTR
- expect.authority_active_polygon_data_gaps=<none>
- route.waypoint=ident=A;lat=0;lon=110
- route.waypoint=ident=B;lat=0;lon=114
- expect.authority_relevant_polygon_matches=HKG_W_CTR:VATSPY_FIR:VHHK:VATSPY_BOUNDARY:VHHK:aircraft=0:route=1:entry=120

Overlay/RX presentation replay:

- overlay.stage=Departure
- transceiver.available=true
- transceiver.stale=true
- transceiver.status=RX feed stale
- transceiver.candidate=callsign=SCT_APP;frequency=128.050;distanceNm=12;score=50
- expect.overlay_body_lines=xPilot connected|RX feed stale

Synthetic route traversal replay:

- route.waypoint=ident=ACFT;lat=0;lon=0
- route.waypoint=ident=FIX1;lat=0;lon=10
- traversal.mode=exact
- feature.entry=label=SECTOR_A;tokens=SECTOR_A;controllerPrefixes=AAA;polygon=-5,-5|5,-5|5,5|-5,5
- traversal.route_sample_step_nm=5
- expect.route_resolved=true
- expect.route_current_sectors=SECTOR_A
- expect.route_next_sectors=SECTOR_B,SECTOR_C

Token grammar replay:

- flight.route_text=NUBLE4 NELIE Q75 MXE CLIPR3 58N140W DCT KK45A/N0489F300
- procedure.entry=type=SID;source=departure;name=NUBLE4;transition=NELIE
- procedure.entry=type=STAR;source=arrival;name=CLIPR3;transition=MXE
- expect.route_token_kinds=NUBLE4:Procedure,NELIE:Point,Q75:Airway,MXE:Point,CLIPR3:Procedure,58N140W:Coordinate,DCT:Control,KK45A:Point

Route text to waypoint/nav-graph resolution replay:

- plan.route_text=NUBLE4 NELIE Q75 MXE CLIPR3
 - procedure.entry=type=SID;source=departure;name=NUBLE4;transition=NELIE
 - procedure.entry=type=STAR;source=arrival;name=CLIPR3;transition=MXE
- graph.node=ident=NELIE;region=K1;type=11;lat=43.2000;lon=-70.5000
- graph.edge=startIdent=NELIE;startRegion=K1;startType=11;endIdent=GREKI;endRegion=K1;endType=11;airway=Q75;direction=N
- expect.resolved_waypoints=ACFT,NELIE,GREKI,BIZEX,MXE,KDCA
- expect.resolved_waypoint_points=ACFT@43.6462,-70.3093|NELIE@43.2000,-70.5000|GREKI@42.8000,-70.7000|BIZEX@42.1000,-71.3000|MXE@39.9272,-75.2411|KDCA@38.8512,-77.0402
- expect.resolved_tokens=NELIE,MXE
- expect.expanded_tokens=Q75
- expect.procedure_tokens=NUBLE4,CLIPR3
- expect.procedure_sources=DEP:NUBLE4,ARR:CLIPR3
- expect.procedure_records=SID:ENROUTE:NUBLE4,STAR:ENROUTE:CLIPR3
- expect.procedure_runways=<none>
- expect.procedure_authorities=SID:NUBLE4:PROC,STAR:CLIPR3:PROC
- expect.procedure_catalog_fixes=<none>
- expect.procedure_boundary_fixes=<none>
- expect.procedure_ordered_fixes=<none>
- expect.procedure_synthetic_waypoints=<none>
- expect.procedure_synthetic_sources=<none>
- expect.procedure_application_states=<none>
- expect.procedure_application_blocks=<none>
- expect.procedure_applied_fix_sequences=<none>
- expect.procedure_catalog_transitions=SID:NUBLE4:NELIE,STAR:CLIPR3:MXE
- expect.procedure_support=FORWARD:NUBLE4,BACKWARD:CLIPR3
- expect.procedure_links=SID:NUBLE4:NELIE,STAR:CLIPR3:MXE
- expect.procedure_misses=<none>
- expect.procedure_anchor_links=<none>
- expect.procedure_context_only=<none>
- expect.ignored_tokens=
- expect.unresolved_tokens=

Recognized procedure tokens are tracked through the dedicated procedure diagnostics above.
They are no longer reported as `ignored_tokens` unless some non-procedure structural token is
actually skipped.

Airport coverage replay:

- airport.coverage_builder_icao=KONT
- airport.coverage_builder_lat=34.0560
- airport.coverage_builder_lon=-117.6010
- airport.terminal_feature=id=SCT;name=SCT_APP;prefixes=SCT;polygon=33,-119|35,-119|35,-116|33,-116
- airport.pending_terminal_feature=id=SCT;name=SCT_APP;prefixes=SCT;polygon=34,-118|35,-118|35,-117|34,-117
- airport.coverage_builds_pre_refresh_snapshot=true
- airport.terminal_probe_lat=34.5
- airport.terminal_probe_lon=-117.5
- airport.terminal_probe_uses_pre_refresh_snapshot=true
- expect.airport_coverage_generations=center:1,authority:1,terminal:2
- expect.airport_terminal_inside=false

Terminal containment checks are generation-gated. A snapshot built from an older terminal
boundary payload must not prove containment against a newer payload even when the terminal
label is unchanged.
Center boundary and authority catalog snapshots are also generation-stamped so resolver and
plugin-level caches can detect source-data changes even when the visible sector labels are
unchanged.

Live route resolver replay:

- resolver.route_resolve=true
- resolver.route_builds_pre_refresh_snapshot=true
- resolver.center_feature=label=OLD;polygon=-1,-1|-1,25|1,25|1,-1
- resolver.authority_catalog_fir=OLD|Old Center|OLD|OLD
- resolver.pending_center_feature=label=NEW;polygon=-1,-1|-1,25|1,25|1,-1
- resolver.pending_authority_catalog_fir=NEW|New Center|NEW|NEW
- expect.resolver_route_current_sectors=NEW
- expect.resolver_route_current_controller_prefixes=NEW:NEW
- expect.resolver_route_generations=center:2,authority:2

This path exercises `RouteSectorResolver::Resolve` directly, including its internal route
snapshot cache, payload refresh seam, center polygon traversal, and authoritative controller
prefix population.

Included scenarios
------------------
- kont_departure_release.scn
  Replays the departure never-released bug path.

- departure_offline_terminal_tuned_does_not_hold.scn
  Proves an offline APP/DEP row tuned on COM1 cannot hold Departure after the release window.

- phzh_controller_match.scn
  Replays the Honolulu/PHZH controller-authority match path without requiring live ATC timing.

- synthetic_sector_chain.scn
  Replays route-sector traversal against explicit polygons and saved waypoints.

- route_token_grammar.scn
  Replays current route token classification for procedures, airways, coordinates, control tokens, and annotated fixes.
  Procedure tokens are only classified as `Procedure` when the scenario injects authoritative procedure metadata via `procedure.entry=type=...;name=...;transition=...`.
  The harness also tracks whether recognition came from runway-tagged procedure records, enroute transition records, both, or base metadata with `expect.procedure_records=...`.
  When runway-tagged records exist, the exact runway tokens are preserved with `expect.procedure_runways=...`.
  The harness also preserves which catalog authority supplied the procedure metadata with `expect.procedure_authorities=...`.
  When authoritative procedure fix membership exists, it is preserved with `expect.procedure_catalog_fixes=...`.
  When authoritative procedure boundary fixes can be proven, they are preserved with `expect.procedure_boundary_fixes=...`.
  When authoritative procedure fix order exists, it is preserved with `expect.procedure_ordered_fixes=...`.
  When the parser safely injects a procedure-derived waypoint into route resolution, it is preserved with `expect.procedure_synthetic_waypoints=...`.
  The harness also preserves whether that synthetic waypoint came from a unique transition or a boundary fix with `expect.procedure_synthetic_sources=...`.
  The harness also distinguishes between procedures that were only recognized and procedures that were actually applied to route construction with `expect.procedure_application_states=...`.
  When a procedure is recognized but not safely applied, the harness preserves the fail-closed reason with `expect.procedure_application_blocks=...`.
  When the parser applies an ordered multi-fix procedure segment, the exact applied sequence is preserved with `expect.procedure_applied_fix_sequences=...`.
  When transition metadata exists, the exact declared catalog transitions are preserved with `expect.procedure_catalog_transitions=...`.

- ambiguous_symbol_prefers_airway_context.scn
  Proves the typed grammar marks a dual-meaning token as ambiguous and that route context chooses the airway interpretation instead of guessing the point ident.

- route_waypoint_resolution.scn
  Replays synthetic airway-backed waypoint resolution and validates the resolved route chain and diagnostics.

- kpwm_kdca_q75_resolution.scn
  Replays the `NUBLE4 NELIE Q75 MXE CLIPR3` airway bug class offline, proves `Q75` expands through a saved graph instead of masquerading as a random point, and proves the filed SID/STAR are treated as real procedure metadata instead of unsupported text.

- procedure_transition_unmatched.scn
  Proves the parser distinguishes between a recognized procedure and a matched procedure transition by recording a stage-aware transition miss when the filed anchor does not match the authoritative metadata.

- procedure_source_both.scn
  Proves the parser records whether a recognized procedure came from departure metadata, arrival metadata, or both, and that merged source attribution survives into the saved diagnostics.

- procedure_support_both.scn
  Proves the parser can report a `BOTH:` support direction when a dual-role procedure has believable anchors on both sides.

- procedure_runway_record.scn
  Proves the parser distinguishes runway-tagged procedure records from enroute transition records and preserves that truth in saved diagnostics.

- procedure_sid_both_record.scn
  Proves the parser records `SID:BOTH:` when one procedure has both runway-tagged and enroute transition metadata, and that runway dependency still wins the fail-closed decision.

- procedure_star_both_record.scn
  Proves the parser records `STAR:BOTH:` when one procedure has both runway-tagged and enroute transition metadata, and that runway dependency still wins the fail-closed decision.

- procedure_anchor_without_transition.scn
  Proves the parser can still establish a stage-aware anchor relationship on the correct side even when no explicit transition metadata exists.

- procedure_context_only.scn
  Proves the parser distinguishes metadata-only procedure recognition from a procedure that also has an adjacent anchor on the correct side.

- procedure_false_anchor_rejected.scn
  Proves the parser no longer treats an arbitrary neighboring fix as anchor support when that fix is not actually part of the authoritative procedure metadata.

- procedure_internal_fix_rejected.scn
  Proves the parser no longer treats an internal non-boundary procedure fix as valid forward support when authoritative no-transition procedure metadata says the exit boundary is somewhere else.

- procedure_sid_boundary_synthesized.scn
  Proves the parser can safely inject a SID boundary waypoint from authoritative procedure metadata when the filed route omits that fix before an airway segment.

- procedure_sid_boundary_without_route_context_blocked.scn
  Proves the parser refuses to inject a single SID boundary waypoint when no following airway or route context proves that boundary belongs in the resolved route.

- procedure_sid_boundary_unreachable_airway_blocked.scn
  Proves single SID boundary synthesis fails closed when the boundary fix cannot enter the following airway and reach the filed exit anchor.

- procedure_star_boundary_synthesized.scn
  Proves the parser can safely inject a STAR boundary waypoint from authoritative procedure metadata when an airway arrives at a filed procedure token without an explicit final fix.

- procedure_star_boundary_unreachable_airway_blocked.scn
  Proves STAR boundary diagnostics are not marked synthesized or applied when the preceding airway cannot reach the STAR boundary fix.

- procedure_sid_transition_synthesized.scn
  Proves the parser can safely inject a SID waypoint from a uniquely provable transition when the filed route omits that anchor before an airway segment.

- procedure_sid_ordered_sequence_synthesized.scn
  Proves the parser can safely inject a full ordered SID fix sequence before an airway when one authoritative non-runway, no-transition path exists.

- procedure_star_transition_synthesized.scn
  Proves the parser can safely inject a STAR waypoint from a uniquely provable transition when an airway arrives at a filed procedure token without an explicit final fix.

- procedure_star_ordered_sequence_synthesized.scn
  Proves the parser can safely inject a full ordered STAR fix sequence after an airway when one authoritative non-runway, no-transition path exists.

- procedure_multi_transition_blocked.scn
  Proves the parser records a fail-closed blocker when a procedure has multiple declared transitions and no single safe application path can be chosen.

- procedure_runway_blocked.scn
  Proves the parser records a fail-closed blocker when a procedure is runway-dependent and therefore cannot be safely applied from generic route text.

- procedure_dual_role_blocked.scn
  Proves the parser records a fail-closed blocker when one procedure token is dual-role and there is no safe way to assume SID-only or STAR-only application.

- procedure_star_transition_unmatched.scn
  Proves the parser records the STAR-side unmatched-transition blocker when the previous proven anchor does not match the authoritative STAR transition metadata.

- procedure_star_multi_transition_blocked.scn
  Proves the parser records the STAR-side multi-transition blocker when multiple STAR transitions exist and no single safe application path can be chosen.

- procedure_star_runway_blocked.scn
  Proves the parser records the STAR-side runway-dependent blocker and refuses generic airway-to-STAR synthesis when runway context is required.

- procedure_star_context_only.scn
  Proves the parser records the STAR-side `NO_PROVABLE_PATH` blocker when a STAR is recognized from metadata but there is no usable anchor, transition, or boundary context.

- procedure_star_false_anchor_rejected.scn
  Proves the parser records the STAR-side `INSUFFICIENT_CONTEXT` blocker when a neighboring point exists but is not the authoritative STAR boundary or transition anchor.

- procedure_star_anchor_without_transition.scn
  Proves the parser records the STAR-side `NOT_NEEDED` case when the filed route already carries the authoritative STAR boundary fix and no synthetic application is required.

- procedure_star_runway_record.scn
  Proves the parser records the STAR-side `NOT_NEEDED` runway-record case when the filed route already carries the authoritative STAR boundary fix even though the procedure metadata is runway-tagged.

- oceanic_coordinate_resolution.scn
  Replays the long-haul coordinate-token class of bug and validates annotated fixes plus oceanic coordinates resolve into route waypoints instead of being dropped.

- duplicate_ident_resolution.scn
  Replays the duplicate-ident class of bug and makes the current reference-point bias explicit so duplicate point selection can be regression-tested while we keep rebuilding the nav graph.

- duplicate_ident_point_lookahead_resolution.scn
  Proves standalone duplicate point resolution uses the next filed point as route context when that context gives a clearer candidate than previous-point distance alone.

- duplicate_ident_destination_context_resolution.scn
  Proves standalone duplicate point resolution uses the destination as route context when the duplicate is the final filed point.

- duplicate_ident_airway_entry_context_resolution.scn
  Proves a duplicate waypoint immediately before an airway is resolved by exact airway reachability to the next filed anchor instead of nearest/destination scoring.

- duplicate_ident_airway_endpoint_resolution.scn
  Proves airway expansion now chooses the reachable duplicate endpoint that actually lives on the airway, instead of trusting a geographically closer off-airway duplicate.

- duplicate_ident_sid_airway_entry_context_resolution.scn
  Proves a synthesized SID ordered-sequence exit fix uses the following airway graph to choose the reachable duplicate fix before airway expansion begins.

- duplicate_ident_star_sequence_airway_entry.scn
  Proves airway-to-STAR ordered sequence entry uses the airway-resolved duplicate boundary fix and rebuilds the remaining ordered procedure sequence from that corrected anchor.

- duplicate_ident_star_boundary_airway_entry_context_resolution.scn
  Proves single-fix STAR boundary synthesis takes the reachable duplicate endpoint from airway graph expansion instead of any pre-resolved nearest point.

- duplicate_ident_star_sequence_rebuild_context_resolution.scn
  Proves remaining STAR ordered-sequence fixes rebuilt after airway entry use next-fix region/context instead of nearest-reference distance alone.

- dense_europe_authority_collapse.scn
  Replays the dense Europe shard-collapse class of bug and proves overlapping sector fragments collapse into canonical authorities instead of bloating the route board.

- enroute_requires_authoritative_route.scn
  Negative regression case proving that a live Center alone is not enough to create ENROUTE truth when the route engine has not produced authoritative sectors.

- controller_feed_stale_does_not_populate_live_boards.scn
  Proves stale controller-feed rows cannot populate Departure, Arrival, or live ENROUTE boards even if a malformed snapshot still carries controller entries.

- controller_feed_unavailable_does_not_populate_live_boards.scn
  Proves unavailable controller-feed rows cannot populate Departure, Arrival, or live ENROUTE boards even if a malformed snapshot still carries controller entries.

- airport_sector_stale_does_not_populate_terminal_airspace.scn
  Proves stale airport-sector terminal coverage cannot populate Departure or Arrival APP/DEP rows.

- departure_terminal_requires_sector_authority.scn
  Proves departure APP/DEP board entries require terminal-sector authority when that data exists, while airport-local tower matching remains callsign-scoped.

- terminal_airspace_rejects_suffix_without_facility_truth.scn
  Proves terminal APP/DEP rows require VATSIM Approach facility truth in addition to callsign suffix and terminal-sector token matches.

- airport_local_accepts_facility_truth.scn
  Proves airport-local Delivery, Ground, and Tower rows display when the VATSIM facility class matches the callsign role.

- airport_local_rejects_suffix_without_facility_truth.scn
  Proves airport-local Delivery, Ground, and Tower rows cannot be created by suffix shape alone when the VATSIM facility class disagrees.

- airport_center_token_cannot_match_terminal_airspace.scn
  Proves center coverage rows cannot masquerade as terminal APP/DEP authority even if a center boundary token has a terminal-looking suffix.

- departure_terminal_rejects_prefix_without_sector_data.scn
  Proves departure APP/DEP rows cannot be created from airport-prefix matching when terminal-sector authority is unavailable.

- airport_coverage_terminal_tokens_authoritative.scn
  Proves airport coverage builder terminal match tokens come from explicit TRACON prefixes, not loose `id`/`name` strings or broad underscore-prefix expansion.

- airport_coverage_no_catalog_no_identifier_fallback.scn
  Proves missing authority catalog data does not turn a center boundary identifier into a live controller prefix.

- airport_coverage_authority_catalog_prefix_only.scn
  Proves catalog rows with explicit controller prefixes use those prefixes without padding in boundary identifiers as extra matches.

- airport_coverage_ignores_arbitrary_boundary_properties.scn
  Proves center boundary `name` and arbitrary string properties cannot become authority lookup tokens.

- airport_coverage_rejects_boundary_without_catalog_refresh.scn
  Proves a refreshed center boundary is not applied unless its matching VATSpy authority catalog also arrives.

- airport_coverage_rejects_catalog_without_boundary_refresh.scn
  Proves a refreshed VATSpy authority catalog is not applied against old center boundaries.

- airport_coverage_accepts_complete_center_refresh.scn
  Proves a complete center boundary plus authority refresh replaces the previous generation atomically.

- airport_terminal_cache_rebuilds_after_refresh.scn
  Proves cached airport terminal coverage rebuilds after terminal-only boundary refresh.

- airport_terminal_accepts_fresh_snapshot_after_refresh.scn
  Proves fresh airport terminal coverage is accepted after terminal-only boundary refresh.

- airport_terminal_rejects_stale_snapshot_after_refresh.scn
  Proves stale airport terminal coverage cannot prove containment after terminal-only boundary refresh.

- enroute_requires_explicit_controller_prefixes.scn
  Proves ENROUTE live center matching requires explicit controller prefixes and will not match or display a controller only because its prefix equals a sector identifier.

- enroute_authority_gap_does_not_display_offline_row.scn
  Proves an ENROUTE sector with no explicit controller prefixes does not create a pilot-facing offline row; the authority gap remains a resolver/status diagnostic instead.

- enroute_diagnostic_offline_row_not_displayed.scn
  Proves an offline ENROUTE diagnostic row is not promoted into the active display board in ENROUTE mode.

- enroute_offline_row_uses_sector_identifier.scn
  Proves an ENROUTE offline row is labeled from the proven sector identifier, not renamed from a tempting center-like match token.

- enroute_accepts_data_driven_fss_facility.scn
  Proves ENROUTE matching accepts route-authorized FSS/oceanic-style controllers when the VATSIM facility code marks them as enroute service and the authority catalog prefix matches.

- enroute_rejects_ctr_suffix_without_enroute_facility.scn
  Proves a `_CTR` callsign suffix alone cannot make a controller eligible for ENROUTE when the controller feed facility code says it is not center/FSS service.

- enroute_authority_snapshot_stale_blocks_legacy_route_sector.scn
  Proves stale authority relevance blocks legacy route-sector matching instead of letting old ENROUTE fallback rows leak into live display logic.

- authority_compiler_vatspy_hkg_activates_polygon.scn
  Proves a VATSpy FIR row compiles explicit HKG activation patterns and maps HKG_W_CTR to the VHHK authority polygon.

- authority_compiler_blank_prefix_is_unmapped_gap.scn
  Proves a blank VATSpy callsign-prefix row remains an unmapped data gap and does not infer PAZA_FSS from the boundary identifier.

- authority_compiler_rejects_wrong_facility.scn
  Proves a matching callsign pattern still cannot activate a center authority when the VATSIM facility code is not center/FSS service.

- authority_polygons_vatspy_boundary_compiles.scn
  Proves a VATSpy boundary-style polygon compiles with source-derived lookup keys and ring truth.

- authority_polygons_simaware_tracon_compiles.scn
  Proves a SimAware TRACON-style polygon compiles as terminal authority with suffix-aware lookup keys.

- authority_polygons_invalid_ring_is_data_gap.scn
  Proves invalid polygon geometry becomes an explicit data gap instead of a usable authority polygon.

- authority_activation_controller_lights_polygon.scn
  Proves a live center controller activates a compiled authority polygon only through controller-authority truth plus polygon-source truth.

- authority_activation_missing_polygon_is_data_gap.scn
  Proves a matching controller authority cannot activate anything when the polygon record is missing, and instead reports a missing-polygon data gap.

- authority_relevance_aircraft_inside_active_polygon.scn
  Proves an active authority polygon becomes relevant when the aircraft position is inside it.

- authority_relevance_route_intersects_active_polygon.scn
  Proves an active authority polygon becomes relevant when the planned route crosses it, including an entry-distance annotation.

- authority_relevance_ignores_non_intersecting_active_polygon.scn
  Proves an online active polygon is not relevant when neither aircraft position nor route geometry intersects it.

- resolver_authority_relevance_feeds_enroute.scn
  Proves resolver-built active authority polygon relevance feeds the ENROUTE board without relying on legacy route-sector matching.

- resolver_authority_reports_unmapped_controller_gap.scn
  Proves the resolver reports a live controller as an explicit unmapped authority gap instead of silently ignoring or guessing it.

- resolver_authority_reports_active_not_relevant.scn
  Proves the resolver reports active-but-irrelevant authority polygons so online controllers outside the route/aircraft footprint remain traceable without display.

- authority_position_json_vatglasses_static_frequency_matches_suffix.scn
  Proves a VATGlasses static position maps a live arbitrary-suffix controller through published prefix/type/frequency and source-owned polygon key.

- authority_position_json_vatglasses_static_rejects_wrong_frequency.scn
  Proves the same arbitrary-suffix controller cannot activate a VATGlasses static position when the live frequency does not match the published position frequency.

- resolver_vatglasses_static_frequency_relevance_feeds_enroute.scn
  Proves resolver-built ENROUTE relevance consumes VATGlasses static position frequency plus owner polygon data and prefers that exact source over broad VATSpy coverage.

- resolver_vatglasses_dynamic_frequency_relevance_feeds_enroute.scn
  Proves resolver-built ENROUTE relevance consumes dynamic VATGlasses positions, airspace, and ownership data to light the exact source-owned polygon by published frequency.

- resolver_vatglasses_dynamic_frequency_blocks_broad_fallback.scn
  Proves a source-owned frequency mismatch blocks broad VATSpy wildcard fallback instead of lighting a plausible but unproven polygon.

- resolver_vatglasses_frequency_rejects_transceiver_geo_mismatch.scn
  Proves fresh live transceiver geography can reject a source-owned same-frequency controller when its station is incompatible with the claimed polygon.

- resolver_duplicated_atis_derived_relevance_feeds_enroute.scn
  Proves ATIS text can activate a source-owned, route-relevant covered position only through `DUPLICATED_ATIS_DERIVED` proof.

- resolver_duplicated_atis_derived_rejects_wrong_facility.scn
  Proves ATIS-derived covered-position proof rejects a non-center/FSS facility instead of lighting an ENROUTE polygon.

- source_package_combines_vatglasses_dynamic_files.scn
  Proves VATGlasses dynamic positions, airspace, and ownership files combine into the parser-ready source package payload.

- source_manifest_parses_vatglasses_dynamic_directory.scn
  Proves the source manifest can describe a live VATGlasses dynamic directory package without using a hand-built ownership payload URL.

- resolver_authority_blank_prefix_is_data_gap.scn
  Proves a VATSpy FIR/UIR row with a blank callsign-prefix field does not invent the boundary identifier as a controller prefix and instead surfaces an explicit route authority gap.

- resolver_authority_gap_identifiers_trace_current_and_next.scn
  Proves route authority-gap diagnostics name both current and next sectors so missing catalog rows can be traced without a live flight retest.

- resolver_boundary_callsign_property_is_not_authority_key.scn
  Proves a center boundary `callsign` property cannot become an authority-catalog lookup key or create a controller prefix match.

- resolver_route_rejects_boundary_without_catalog_refresh.scn
  Proves a refreshed route-sector boundary is not applied unless its matching VATSpy authority catalog also arrives.

- resolver_route_rejects_catalog_without_boundary_refresh.scn
  Proves a refreshed VATSpy authority catalog is not applied against old route-sector boundaries.

- resolver_route_rebuilds_after_center_catalog_refresh.scn
  Proves route-sector resolution rebuilds when a complete center boundary plus authority-catalog refresh arrives.

- route_collapse_no_controller_prefix_leak.scn
  Proves route-sector collapse grouping keys do not become controller prefixes when no explicit authority prefix exists.

- route_collapse_explicit_prefixes_only.scn
  Proves route-sector collapse preserves explicit controller prefixes without padding the collapsed authority identifier back into live matching data.

- narrow_crossing_sampled_miss.scn
  Documents the old sampled traversal weakness by showing a narrow sector crossing missed when only the end sample lands outside the polygon.

- narrow_crossing_exact_hit.scn
  Proves the new exact traversal path catches the same narrow crossing without depending on route sampling density.

- antimeridian_exact_crossing.scn
  Proves the exact traversal path catches a dateline crossing against an anti-meridian-spanning sector polygon without depending on sampled route points.

Step 3 bounded preparation-worker proof contract
------------------------------------------------

The `worker-contract` evaluator uses the production
`AccessoryPreparationWorker`, immutable brain preparation snapshots, GDI+
text measurement, wrapping, and generation-checked presentation publication.
It proves one below-normal event-driven worker; one executing job; at most one
latest queued and one ready plan per drawer; bounded replacement and stale
rejection; exact UTF-8 title/body/key reconstruction; final-history markers;
three maximum-retained-byte drawer preparations; constant-time prepared open
and atomic switch; disable/enable resource recreation with history retention;
session-reset and callsign-change stale-plan rejection; and zero worker job,
history, wrapping, raster, or upload activity through 10,000 unchanged updates.
Ready-plan publication uses a single `try_lock` attempt and returns immediately
when the worker publication path is contended; it never waits or takes a second
counter lock on the simulator thread. The executable negative proof holds the
real worker publication critical section, observes two immediate null readiness
results, then proves only the exact generation publishes after contention clears.

Worker startup is an explicit `Starting` to `Ready`, `Failed`, or `Stopped`
lifecycle. `Start` does not report success until both below-normal priority and
the worker-owned GDI+ context succeed. Injected priority and measurement failures
fail closed, reject work, publish at most one bounded failure diagnostic, do not
retry during 10,000 idle observations, and leave zero worker threads after stop.
The normal successful startup path is then exercised again.

Worker launch is asynchronous. `Start` publishes `Starting`, launches exactly
one worker, and returns without waiting for priority assignment or the private
GDI+ context. A repeated call during `Starting` is an immediate no-op. Requests
received during `Starting` use the same one-latest-request-per-drawer queue and
are processed after `Ready`; startup failure cancels that bounded pending work.
The delayed-start proof gates real initialization for 250 ms and proves both
startup calls remain below 16.7 ms, the state remains `Starting`, the queued
exact generation publishes after success, and delayed priority/GDI failures
become latched `Failed` states without retry, spin, or surviving threads.

Job submission uses one nonblocking `try_lock` attempt. Contention leaves the
caller's submitted key unchanged and returns immediately; the overlay retains
only the latest exact unsent request per drawer and retries it during a later
already-eligible accessory update. Atomic enqueue-attempt, contention, success,
replacement, and maximum-duration counters require no second worker lock. The
negative proof holds the production queue/publication mutex, verifies three
rapid drawer submissions return below 16.7 ms without being marked submitted,
then retries and publishes only their exact newest generations. The resulting
METAR to ATIS to PDC brain requests remain exactly-once and end with one visible
PDC drawer. Ten thousand unchanged observations add no enqueue attempt, job,
history, wrapping, rasterization, upload, or diagnostic work.

Performance observations use synchronous main-thread wall time. The historical
per-action `GetThreadTimes` experiment is not part of the current pass/fail
contract. Worker preparation wait is reported separately. Draw histograms count
each unique draw once, while action records retain draw-sample identity, shared
sample truth, and fan-out. Scroll-only raster work is reported as
`presentation-scroll`; `content-generation` remains reserved for changed brain
history content. Up to eight retained violations must be serialized at the
disable/stop aggregate boundary, together with retained, serialized, and
dropped counts.
