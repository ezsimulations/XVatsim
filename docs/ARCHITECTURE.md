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

### Overlay

Responsibilities:

- render only the brain-issued view model
- clamp text, position, scale, and animation inputs
- expose text-entry and acknowledge/recall requests without owning controller logic

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
