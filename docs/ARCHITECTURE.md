# Architecture

## Governing Rule

XVatsim has one live decision path:

`Brain decides. Modules produce bounded mechanical facts. UI displays brain-approved facts.`

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
- own accessory drawer selection, the independent METAR and ATIS histories,
  and the single flight-bound PDC snapshot, including accepted ordering,
  stable-key identity, bounds, unread/viewed state, lifecycle preservation,
  lifecycle clearing, and PDC capture closure
- publish immutable, generation-tagged accessory preparation snapshots and
  the complete brain-approved accessory presentation state

### Overlay

Responsibilities:

- render only the brain-issued view model
- clamp text, position, scale, and animation inputs
- expose text-entry facts without owning controller logic
- report bounded ORB-click and exact first-visible revision facts without
  deciding drawer selection, acknowledgement, status, content, or lifecycle
- own presentation-only drawer scrolling, shared window geometry, hit testing,
  raster resources, texture-local signatures, and rendering

### Asynchronous Fact Workers

The plugin binds generic worker hosts and forwards bounded clock, workflow,
flight-context, connectivity, text-entry, and lifecycle facts. The brain decides
if and when a request exists. A removable source module executes that one
request and returns immutable facts. Feature scheduling loops and policy do not
belong in `XVatsimPlugin.cpp`.

For Step 4, the production METAR module requests only one normalized airport
from `https://metar.vatsim.net/:icao?format=json`. The brain owns automatic
primary targeting, pilot lookup, priority, cadence, failure backoff, parsing,
classification, freshness, cache, history, transient presentation, and stale
completion rejection. Network wait is measured separately from simulator-thread
acceptance work.

## V2 Accessory System — Steps 3 Through 6

The XPLM overlay contains one accessory rail with three completed independent
Brain-owned products: METAR, VATSIM ATIS, and one-shot PDC. At most one drawer
is visible. Selecting, closing, or switching a drawer does not combine product
state or transfer contents. All retained product state is process-local.

Step 4 connects METAR to the official single-airport VATSIM METAR endpoint.
Step 5 obtains ATIS mechanically from the existing shared VATSIM network-data
feed and adds no second ATIS endpoint, request cadence, worker, cache authority,
or selector. Step 6 obtains bounded mechanical xPilot private-source facts and
retains one flight-bound waiting-message snapshot; it is not a combined
PDC/private-message inbox.

For PDC, the Brain first requires a complete normalized flight identity. While
acquisition is armed, the qualified xPilot bridge may report connection,
sequence, sender bytes, body bytes, and bounded mechanical sampling outcomes.
The Brain accepts the first stable-connected positive-sequence observation with
a successfully read non-empty bounded body. Sender spelling, controller roster,
controller position, message wording, `PDC`/`ACARS` labels, IFR/VFR selection,
and workflow stage do not classify or veto that first mechanically valid
waiting message.

Acceptance is atomic in the Brain: the snapshot is bound to the current flight
identity and departure, unread state is established, capture completes, and the
Brain commands a complete private-source sampling stop. The plugin performs no
further identity, capability, sequence, sender, or body read for that flight.
Later revisions are intentionally left to xPilot. Temporary xPilot disconnect,
overlay sleep, and Plugin Admin suspend/resume preserve the captured snapshot;
flight-identity change, cold/dark or session reset, explicit full reset, and
plugin unload clear it and re-arm the next qualifying flight.

The Brain projects exact PDC states `IDLE`, `SOURCE`, `CHECK`, `NEW 1`, `OPEN`,
and `MSG 1`. The drawer title is `PDC — <DEPARTURE ICAO>` and its warning is
`CAPTURED SNAPSHOT — CHECK XPILOT FOR REVISIONS`. The overlay does not infer
capture, counts, uncertainty, wording, selection, or acknowledgement. It renders
the exact projection and returns only bounded click and visible-publication
facts; the Brain alone acknowledges the captured revision.

The overlay reports a bounded click fact. The plugin consumes that fact exactly
once and asks the brain for the selection decision. The brain owns opening,
closing, switching, METAR/ATIS ordering and bounds, PDC snapshot state,
generations, and the boundary-specific decision to preserve or clear retained
product state. The overlay
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
- `xpilot_bridge`: qualifies xPilot and required datarefs, performs bounded
  coherent private-source reads only when commanded, and stops payload sampling
  after Brain-owned one-shot capture

The `xpilot_bridge` mechanically qualifies the installed xPilot binary and
required datarefs, performs bounded coherent sampling only when commanded, and
serializes no sender or message body into diagnostics. The plugin supplies
flight context and transports bridge facts to the Brain. Once the Brain reports
capture complete, the plugin clears queued source facts and returns before any
further private-source identity, capability, sequence, sender, or body read.
There is no second production sampler or message-classification path.

## V2.0.0 Beta Boundary

Steps 1 through 6 are accepted, complete, and frozen. The accepted Step 6
implementation is `94825f07248dafc038d4c29e06ea61e40f49cfc3`; proof and
closeout is `380039a4494f0b373542eced807345471f214036`. Final proof is
production-renderer visual `14/14`, focused Step 6 `61/61`, complete regression
`861/861` twice, and a Product Owner normal-use live PASS. The accepted scenario
fingerprint is
`227121979F59CB3BB359B96DE57AC7DF601E0BE3736F82F38C19EE08437431D9`.
Protected proof remains `80 manifests / 4,695 rows / 0 mismatches`, and the
preparation backup and untracked evidence must not be cleaned.

The exact candidate and active deployment retain `3/3` parity:

| File | Bytes | SHA-256 |
|---|---:|---|
| `XVatsim.xpl` | `2,572,288` | `CA2840F6FE61124E37881124DF78E4B0A0049FD47C5BF4FA6EE179D9A6E77934` |
| `authority_source_registry.json` | `40,340` | `3676CA43E5AFB8A5E443FDE6D04616918029E004E17EFAA022695D91E23DB60B` |
| `ui_transition.mp3` | `27,116` | `C7BBE97DAD356C68FDFE40F9E8C1CF4EADEBCCD125463214F963F66356F8D9F1` |

Accepted live maximum synchronous accessory work was `23` microseconds and
maximum click-to-terminal was `32,467` microseconds. The single `1,713 ms`
route/authority refresh warning occurred after capture, was unrelated to PDC
sampling/rendering, caused no perceived lag, and did not block release.
Post-release observation continues verifying that bounded startup preparation
settles into stable, calm, low-churn operation.

## Harness-Only Legacy Coverage

The old `arrival`, `departure`, and `enroute` board collectors are no longer
part of the live plugin module set. They compile only for
`XVATSIM_BUILD_REGRESSION_HARNESS` as `XVatsimHarnessLegacyArrival`,
`XVatsimHarnessLegacyDeparture`, and `XVatsimHarnessLegacyEnroute` so historical
scenarios can keep guarding old evidence while Engineer 3 remains the single
live runtime.

## Non-Goals For V2.0.0

- no general private-message inbox or AUTO_ATC card presentation
- no SimBrief import
- no Navigraph AIRAC import
- no dedicated VFR controller-evidence workflow
- no second-monitor/out-of-sim window mode

V2.0.0 is the current public Windows/X-Plane 12/xPilot freeware release. The
accepted IFR/VFR mode foundation remains, but a dedicated VFR evidence engine
or live VFR projection is only an optional Version 3 decision with no
implementation promise.
