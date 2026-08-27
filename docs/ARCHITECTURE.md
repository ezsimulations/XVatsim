# Architecture

## Governing Rule

XVatsim has one live decision path:

`Brain decides. Modules produce facts. UI displays brain-approved facts.`

Source modules collect bounded observations, the plugin host owns X-Plane
integration and lifecycle side effects, and the brain owns workflow, controller
relevance, display intent, and final UI publication.

Dormant prototype modules must not remain in the live source tree. If a module is
not part of the compiled plugin or regression harness, it should be archived outside
the release source path or removed.

## Live Pieces

### Plugin Host

Responsibilities:

- load inside X-Plane
- own simulator callbacks, commands, menu entries, and lifecycle resets
- sample live aircraft, radio, xPilot, VATSIM, flight-plan, and settings sources
- enforce source freshness and runtime/session boundaries
- pass validated snapshots into workflow, route-sector, board, and brain layers

### Route And Authority Resolution

Responsibilities:

- parse route text through typed grammar
- resolve fixes, airways, procedure metadata, and coordinate tokens
- resolve center and terminal polygons using spherical/geodesic-safe logic
- collapse controller authority through explicit catalogs rather than token guessing
- reject stale boundary/catalog/feed combinations

### Brain

Responsibilities:

- decide which workers run and when they run
- own flight context, workflow phase, route polygon runtime state, radio board
  reuse, controller relevance, display intent, and final UI publication
- accept or reject every reachable controller candidate with a logged reason
- keep broad authority proof out of ordinary UI refresh unless explicitly
  scheduled as a fallback
- own the accessory drawer selection and the three independent METAR, ATIS,
  and PDC/private-message histories, including accepted ordering, stable-key
  deduplication, count and byte limits, oldest-first eviction, lifecycle
  preservation, and lifecycle clearing
- publish immutable, generation-tagged accessory preparation snapshots and
  the complete brain-approved accessory presentation state

### Overlay

Responsibilities:

- render only the brain-issued view model
- clamp text, position, scale, and animation inputs
- expose text-entry and acknowledge/recall requests without owning controller logic
- report bounded ORB-click facts without deciding which drawer opens
- own presentation-only drawer scrolling, shared window geometry, hit testing,
  raster resources, texture-local signatures, and rendering

## V2 Step 3 Accessory Shell

The existing XPLM overlay window contains one visible accessory drawer surface
backed by three independent brain-owned histories: METAR, ATIS, and
PDC/private-message. At most one surface is visible, but selecting, closing, or
switching it does not combine the histories or transfer their contents. Step 3
histories exist only in process memory. Step 3 has no live METAR, ATIS, PDC, or
private-message source; its normal production drawers show explicit empty-state
text until later roadmap steps connect those sources.

The overlay reports a bounded click fact. The plugin consumes that fact exactly
once and asks the brain for the selection decision. The brain owns opening,
closing, switching, ordering, deduplication, limiting, eviction, generations,
and the boundary-specific decision to preserve or clear history. The overlay
owns only presentation concerns: scrolling while a drawer is open, layout,
clamping, hit testing, texture-local render signatures, rasterization, uploads,
and drawing.

Populated drawer text is prepared by one event-driven Windows worker running at
below-normal priority. Startup is asynchronous and fail-closed. The worker
receives only immutable brain-approved snapshots and returns immutable prepared
plans tagged with the exact drawer, history, typography, layout, and scale
generations. Job enqueue and ready-plan publication are nonblocking handoffs;
the X-Plane thread retries only while accessory activity is genuinely pending.
Stale plans are rejected before publication.

The preparation worker must not access XPLM, OpenGL, texture identifiers,
settings, files, network sources, plugin lifecycle callbacks, or mutable brain
state. Its private GDI+ measurement resources are created, used, and destroyed
on the worker. Disable and stop cancel work, wake the worker, join it, and leave
zero worker threads.

Accessory rendering is render-on-change. Once the exact prepared plan is
published and textures are warm, an unchanged frame performs no history copy or
traversal, text wrapping, GDI+ measurement, rasterization, texture upload,
enqueue attempt, file/network operation, or diagnostic logging.

Performance evidence keeps four concepts separate:

- worker preparation wait, which is asynchronous and never represented as
  simulator-thread computation;
- synchronous simulator-thread dispatch and presentation work;
- X-Plane frame-cadence wait until the matching draw callback; and
- measured rasterization, OpenGL upload, and completed accessory draw work.

An accessory action is complete only when the draw matching its exact selection
and render generations finishes. Frame-cadence delay cannot be relabelled as CPU
work, and worker preparation time cannot be folded into main-thread timing.

## Live Module Set

- `aircraft_state`: samples and validates aircraft position, altitude, power, and motion state
- `controller_feed`: converts fresh VATSIM data into controller snapshots
- `ctaf_lookup`: resolves airport CTAF facts and manual CTAF queries
- `diversion_context`: produces manual diversion context facts
- `flight_plan`: samples simulator/FMS flight-plan context
- `network_plan_link`: matches VATSIM network flight plans to the connected pilot
- `overlay`: renders the cockpit UI
- `pilot_identity`: resolves the active pilot callsign
- `radio_state`: samples and writes radio/transponder state
- `route_sector`: resolves route, center, terminal, and authority coverage
- `settings_store`: loads and saves bounded release preferences
- `transceiver_resolver`: resolves usable transceiver/range information from fresh feeds
- `vatsim_data_feed`: fetches and sanitizes the VATSIM public data feed
- `xpilot_bridge`: reads xPilot session state and gated optional message refs

## Harness-Only Legacy Coverage

The old `arrival`, `departure`, and `enroute` board collectors are no longer
part of the live plugin module set. They compile only for
`XVATSIM_BUILD_REGRESSION_HARNESS` as `XVatsimHarnessLegacyArrival`,
`XVatsimHarnessLegacyDeparture`, and `XVatsimHarnessLegacyEnroute` so historical
scenarios can keep guarding old evidence while Engineer 3 remains the single
live runtime.

## Non-Goals For V1

- no private-message, PDC, or AUTO_ATC card presentation
- no SimBrief import
- no Navigraph AIRAC import
- no dedicated VFR workflow
- no second-monitor/out-of-sim window mode

These are candidates for future milestones only after the V1 reliability path is complete.
