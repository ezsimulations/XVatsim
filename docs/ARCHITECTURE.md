# Architecture

## Governing Rule

XVatsim has one live decision path. Source modules collect bounded observations,
the plugin host owns X-Plane integration and lifecycle, the core workflow resolves
flight-stage ownership, route-sector modules resolve authoritative controller
coverage, and the brain converts the resulting state into the overlay view model.

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

### Core Workflow

Responsibilities:

- resolve Departure, Enroute, Arrival, or None from explicit state
- keep departure-release, arrival-wake, and cruise-target state deterministic
- avoid controller-selection shortcuts or guessed source substitution

### Route And Authority Resolution

Responsibilities:

- parse route text through typed grammar
- resolve fixes, airways, procedure metadata, and coordinate tokens
- resolve center and terminal polygons using spherical/geodesic-safe logic
- collapse controller authority through explicit catalogs rather than token guessing
- reject stale boundary/catalog/feed combinations

### Brain

Responsibilities:

- build the overlay view model from already-resolved state
- format display text safely
- keep rendering decisions separate from source acquisition and route authority

### Overlay

Responsibilities:

- render only the brain-issued view model
- clamp text, position, scale, and animation inputs
- expose text-entry and acknowledge/recall requests without owning controller logic

## Live Module Set

- `aircraft_state`: samples and validates aircraft position, altitude, power, and motion state
- `arrival`: builds destination-local and destination-airspace boards
- `controller_feed`: converts fresh VATSIM data into controller snapshots
- `ctaf_lookup`: resolves airport CTAF display lines and manual CTAF queries
- `departure`: builds departure-airport boards
- `diversion_context`: owns manual diversion override state
- `enroute`: builds route-center boards from resolved route sectors
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

## Non-Goals For V1

- no private-message, PDC, or AUTO_ATC card presentation
- no SimBrief import
- no Navigraph AIRAC import
- no dedicated VFR workflow
- no second-monitor/out-of-sim window mode

These are candidates for future milestones only after the V1 reliability path is complete.
