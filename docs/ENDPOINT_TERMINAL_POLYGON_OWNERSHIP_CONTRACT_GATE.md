# Endpoint Terminal Polygon Ownership Contract Gate

**Gate ID:** `ENDPOINT-TERMINAL-POLYGON-OWNERSHIP-01`
**Status:** Implemented, regression proven, live verified, and approved for
commit by Product Owner on 2026-10-22
**Prepared:** 2026-10-22
**Approved:** 2026-10-22
**Amended and approved:** 2026-10-22 — include the client-reported aircraft
electrical-power correction and its regression proof
**Repository baseline:** `a50c562085e9f79d7ad93670ed795cf2349ed5a3`
plus the combined controller-eligibility and Standby Assist work
**Live verification:** Separately authorized and completed on 2026-10-22

## 1. Objective

Give the Brain a source-backed terminal polygon package for exactly two
airports: the filed departure and filed arrival. A live APP/DEP controller
lights its source-defined polygon inside the applicable endpoint package. The
Brain then decides whether that controller is the direct endpoint owner, a
source-listed parent/top-down owner, a nearby terminal controller, unrelated,
or operationally ineligible.

This slice must correct the preserved `THAMES_APP 132.700` failure without
scanning every APP/DEP polygon against the entire flight route and without
weak callsign spelling becoming stronger than operational, polygon, ownership,
frequency, and transmitter facts.

This slice must also correct aircraft-power detection so a powered aircraft
does not lose XVatsim flight-plan and controller service merely because one
battery-switch dataref is false while GPU, APU, engine-generator, another
battery, or an energized electrical bus is supplying the aircraft.

## 2. Governing Bible

> The Brain is the only decision maker. A worker reports requested facts. No
> worker, resolver, cache, publisher, or UI may alter, suppress, classify,
> replace, override, or act on the Brain's decision.

Required flow:

```text
Brain requests departure and arrival terminal facts
    -> source workers return immutable geometry, identity, hierarchy, frequency,
       freshness, and completeness facts
    -> Brain matches live operational controllers to source positions
    -> Brain lights matching endpoint polygons internally
    -> Brain resolves child, parent/top-down, nearby, unrelated, or ineligible
    -> Brain publishes the final immutable controller board
    -> UI renders it without deciding
```

This gate is more specific than the flat terminal-owner vote in
`CONTROLLER-OPERATIONAL-ELIGIBILITY-01`. It does not change that gate's
operational hard guards, Australia alias protection, supported-tie policy, or
Brain-only authority.

## 3. Confirmed Existing Defect

The current terminal resolver downloads and parses the global SimAware TRACON
catalog, but its endpoint output retains only `ownerTokens` and `polygonKeys`
for polygons containing the airport coordinate. It discards the endpoint
geometry and does not preserve a child/parent ownership graph.

The current Brain evidence path then turns a terminal owner-token mismatch into
a negative vote. This reverses the map-driven question. It asks whether a live
controller's text resembles the airport polygon found from the airport point,
rather than first matching the live controller to its published position and
polygon and then determining the controller's relationship to the endpoint.

For the preserved EGLL flight, `THAMES_APP 132.700` was operational, was an
exact raw xPilot correlation, and had an AFV transmitter 3.9 NM from EGLL. It
was hidden after repeated text/owner disagreements outweighed those facts.

## 4. Source Roles

### 4.1 SimAware terminal boundaries

SimAware provides source-defined APP/DEP display polygons and their callsign
prefix/suffix matching facts. It is the baseline geometry and exact live
controller-to-polygon identity source.

It does not by itself define every top-down or bandbox takeover relationship.
Geometric containment or overlap is a fact, but geometry alone may not invent
ownership.

### 4.2 VATGlasses ownership

VATGlasses provides position IDs, callsign prefixes, facility types,
frequencies, airspace owner lists, airport associations, runway-conditioned
sectors, and airport top-down sequences where the regional package supplies
them.

These are source ownership and hierarchy facts. The Brain decides which
currently online position activates which layer and which position has
endpoint precedence.

### 4.3 vNAS ownership

Existing vNAS direct, consolidated, split-sector, and top-down facts remain the
preferred detailed source where they are available in the United States. They
must feed the same Brain contract used by SimAware and VATGlasses rather than a
separate final-decision path.

### 4.4 VATSIM and AFV

VATSIM supplies live controller identity, facility, original primary
frequency, and feed health. AFV supplies matching transmitters, frequencies,
coordinates, and reachability. These sources establish operational and radio
facts; they do not independently invent polygon ownership.

### 4.5 Static VATSpy identity

VATSpy exact airport and authority aliases remain identity facts. Fuzzy
spelling remains diagnostic only, except for the already-approved one trailing
character `ML`/`MLL` form.

## 5. Endpoint Package Scope

The Brain requests and retains exactly two terminal packages per active flight:

- departure endpoint terminal package;
- arrival endpoint terminal package.

Each package may contain more than one polygon because an airport can have
split APP/DEP sectors, nested local and parent sectors, overlapping regional
sectors, runway-conditioned sectors, or bandboxed positions.

Each package must preserve, when supplied by a source:

- full Polygon or MultiPolygon geometry;
- polygon and source position IDs;
- exact callsign prefixes and suffixes;
- source facility type and published frequency;
- airport and runway associations;
- child/parent, owner-list, consolidated, bandbox, and top-down relationships;
- geometric containment, intersection, and adjacency facts requested by the
  Brain;
- source generation, freshness, completeness, and explicit data gaps; and
- provenance for every field.

Global source catalogs may be parsed and indexed off the simulator thread.
Runtime terminal evaluation is restricted to the two endpoint packages and
the live controllers that could relate to them. This slice must not compare
every terminal polygon with the complete route.

Center/FIR route polygons remain on the existing route-authority path.

## 6. Brain-Owned Polygon Activation

For every fresh operational APP/DEP controller, the Brain evaluates exact
source position facts in this order:

1. original VATSIM facility and primary-frequency eligibility;
2. exact source callsign pattern, including published sectorized patterns;
3. source frequency where the source declares one;
4. matching SimAware, VATGlasses, or vNAS position and polygon facts;
5. endpoint association, ownership hierarchy, and geometry relationship;
6. AFV transmitter location and reachability; and
7. optional exact xPilot raw correlation.

A source-matched live controller lights its matching polygon in Brain state.
Workers report match facts and geometry only. They do not set `online`,
`active`, `lit`, `owner`, `relevant`, or `displayed` product state.

Unavailable, stale, incomplete, or missing source data remains unknown. It may
not become a negative fact or a clean offline conclusion.

## 7. Endpoint Relationship and Precedence

The Brain assigns one of these explainable relationships:

### 7.1 Direct child owner

The controller's active source position directly owns an endpoint terminal
polygon or is explicitly associated with the endpoint airport. It is the
primary endpoint APP/DEP service.

### 7.2 Parent or top-down owner

The endpoint's direct child position is not online, and a currently online
position is explicitly listed by the source as an owner, parent,
consolidated/bandbox position, or member of the airport's top-down sequence.
The Brain may assign the larger polygon as the primary endpoint owner.

Geometric containment alone is insufficient for takeover. The source must
publish the ownership relationship. This prevents an unrelated large polygon
from acquiring every airport inside it.

### 7.3 Nearby active terminal

The controller is not the direct or parent endpoint owner, but exact source
position facts plus endpoint polygon intersection/adjacency or a usable local
AFV transmitter prove that the position is a nearby terminal service relevant
to the pilot. The controller may display as nearby without being falsely
labelled the airport's primary owner.

### 7.4 Unrelated

Fresh complete facts prove that the active polygon has no endpoint ownership,
no source-backed parent relationship, no applicable endpoint geometry
relationship, and no usable local radio-service relationship.

### 7.5 Ineligible

The existing Brain hard guards reject observer/supervisor service, facility
zero, original primary `199.998`, guard `121.500`, or another explicit
operational failure before endpoint relation is considered.

When direct child and parent controllers are both online, the direct child is
the primary endpoint owner. A parent may remain visible only when it has its
own independently proven nearby/relevant relationship; it may not replace the
child.

## 8. THAMES and EGLL Required Result

The saved current sources establish:

- SimAware `EGLL` is the Heathrow Director polygon and contains EGLL;
- SimAware `THAMES` is the Thames Director polygon, uses prefix `THAMES`, and
  partially overlaps the Heathrow terminal area but does not contain the EGLL
  airport coordinate;
- the pinned VATGlasses England package defines position `TMS` as APP,
  prefixes `THAMES` and `THA`, frequency `132.700`, callsign Thames Director;
- VATGlasses does not list `TMS` in EGLL's direct top-down sequence;
- the live VATSIM record identified `THAMES_APP` as APP on `132.700`;
- the xPilot raw snapshot correlated the controller exactly; and
- the nearest matching AFV transmitter was 3.9 NM from EGLL.

Required Brain result for this preserved flight:

- `THAMES_APP 132.700` is operationally eligible;
- its exact source position lights the THAMES polygon;
- it is not falsely labelled direct EGLL ownership;
- it is accepted as a nearby active terminal controller for EGLL arrival;
- `THAMES` versus `EGLL` text mismatch contributes no negative decision; and
- the receipt shows every fact and the final relationship.

The source-listed London TC/top-down positions, when online while the local
Heathrow APP/DEP child is offline, are the applicable parent-takeover example.

## 9. Decision Model

This slice does not return to one large weighted score and does not allow a
flat collection of weak yes/no text votes to overrule stronger facts.

The Brain resolves claims in separate lanes:

- operational eligibility;
- exact source position identity;
- polygon activation;
- direct endpoint ownership;
- parent/top-down ownership;
- nearby terminal relevance;
- radio service and reachability; and
- final presentation.

A fact has authority only over the claim it can answer. For example, a callsign
text mismatch can say that direct airport-name identity was not proven. It
cannot say an otherwise source-matched operational controller is offline or
unrelated.

The existing supported-tie fail-safe remains for genuinely unresolved
supported cases. This slice should reduce accidental ties by resolving typed
claims and deduplicating repeated interpretations of one text observation.

## 10. Aircraft Electrical-Power Correction

### 10.1 Preserved client evidence

The client used the initial released X-Works A330-200F modification of the
Laminar A330-300. The reported sequence progressed through battery-only, GPU,
APU, and engine-generator power. XVatsim initially worked, recognized EDDP,
returned METAR, and reflected the captain-side radio frequencies. It later
cleared the radio/controller presentation and stopped working before pushback.

The supplied XVatsim diagnostic log independently records:

- `BCS230` became operational with `battery=1` and a matched EDDP-to-LIMC plan;
- approximately 24 seconds later the runtime changed to `battery-off` while
  xPilot remained connected;
- that transition reset the session runtime and stopped operational VATSIM,
  flight-plan, controller, and authority refresh; and
- the still-visible widget was possible because a forced-open or pending
  presentation path can render a session-boundary frame even while the
  operational pipeline is dormant.

This is not evidence of an `OBS001` cache lock. It is evidence that the current
single battery-switch reading is not a sufficient aircraft-powered fact.

### 10.2 Worker facts

The aircraft-state sampler may mechanically report:

- availability and values of every standard
  `sim/cockpit2/electrical/battery_on` array entry;
- the legacy main-battery value when it is the only available battery source;
- availability and values of every standard
  `sim/cockpit2/electrical/bus_volts` array entry;
- array element counts, number of non-zero battery switches, and maximum bus
  voltage as mechanical summaries; and
- source availability and read/type failures.

The sampler may not decide that the aircraft is powered, cold and dark,
operational, or eligible for reset. It may not hardcode X-Works, Laminar,
ToLiss, FlightFactor, JAR, or another aircraft's private datarefs in this
slice.

### 10.3 Brain decision

The Brain owns a typed electrical-power state:

- `Powered`: at least one available standard battery entry is on, or an
  available standard electrical bus has usable voltage;
- `Unpowered`: available battery facts show every battery off and available
  bus facts show no usable voltage for the bounded cold-dark confirmation
  period; or
- `Unknown`: the standard facts are unavailable, unreadable, contradictory,
  or insufficient.

The exact usable-voltage threshold and confirmation period are Brain policy
constants with named diagnostics and focused proof. They may not live in the
sampler.

A transition between battery, GPU, APU, and engine-generator supply must remain
`Powered` while a bus stays energized. A false value from the first battery
array entry must not override another on battery or an energized bus.

`Unknown` may not clear flight context, histories, controller state, or the
last accepted plan. At startup it remains a visible diagnostic state until
power is proven. During an established powered session it preserves the last
accepted power state only for a bounded Brain-owned grace period; it may not
become an unlimited assumption.

Only a Brain-confirmed `Unpowered` state may trigger the existing cold-dark
reset. Runtime activation, recovery, workflow, and presentation boundaries
must consume the same Brain electrical-power decision rather than independently
reading `batteryOn`.

Diagnostics must expose the raw battery array summary, raw bus-voltage
summary, resulting Brain power state, confirmation/grace state, and the reason
for every transition.

## 11. Protected Existing Behavior

The implementation must preserve:

- the Brain as sole decision maker;
- Standby Assist regional sequence and verified UNICOM behavior;
- Cleveland rejection from explicit operational and remote-location facts;
- the `ML`/`MLL` one-trailing-character Australian alias protection;
- vNAS direct, split, consolidated, and top-down behavior;
- Southern California child/parent terminal behavior;
- Potomac and other shared-TRACON behavior;
- center route and handoff behavior;
- supported-tie display policy;
- incomplete/stale-source neutrality;
- pseudo-frequency tolerance except explicit guard/inactive primaries;
- xPilot 3 and xPilot 4 compatibility; and
- UI rendering only the immutable Brain-approved snapshot;
- genuine cold-and-dark shutdown still clears flight-scoped runtime state once;
- normal battery-on startup and battery-off/on recovery behavior; and
- aircraft that correctly publish the existing standard battery dataref.

## 12. Prohibited Shortcuts

The implementation may not:

- create or require a new xPilot bridge;
- copy or scrape the xPilot displayed board;
- hardcode `THAMES_APP`, EGLL, Southern California, or another airport/controller
  exception into live decision logic;
- treat every geometrically containing large polygon as an owner;
- treat fuzzy callsign similarity as polygon activation or ownership;
- let a worker select the winning owner or suppress a controller;
- evaluate every world terminal polygon against the complete route on each
  refresh;
- let missing source data become negative evidence; or
- commit, push, package, or release before required offline and live proof;
- hardcode an aircraft-specific electrical dataref to repair the X-Works case;
- treat generator or APU switch position alone as proof of delivered power;
- allow the sampler or plugin host to make the final powered/cold-dark
  decision; or
- clear the current flight from an unknown or one-sample electrical gap.

## 13. Required Offline Proof

Before installing a test plugin, the focused harness must prove:

1. exact child APP/DEP online -> child polygon lit and child primary;
2. child offline plus source-listed parent online -> parent polygon lit and
   parent takeover;
3. child and parent online -> child remains primary;
4. large containing polygon without published ownership -> no takeover;
5. overlapping nearby sector with exact source and local AFV proof -> nearby
   terminal display without false direct ownership;
6. preserved EGLL/THAMES data -> `THAMES_APP 132.700` displays with the required
   receipt;
7. preserved Cleveland data -> `CLT_APP` remains rejected and no fuzzy rescue
   occurs;
8. Australia `ML`/`MLL` alias case remains accepted;
9. existing Southern California vNAS child/parent cases remain green;
10. stale, unavailable, or incomplete hierarchy data stays neutral and does
    not suppress an otherwise supported controller;
11. departure and arrival packages are isolated from unrelated route terminal
    polygons;
12. workers cannot publish or override Brain activation/ownership state; and
13. decision receipts expose raw source identity, geometry relation,
    hierarchy relation, operational facts, radio facts, final relationship,
    and final display result;
14. a multi-battery aircraft with battery entry zero off and another battery on
    remains powered;
15. battery-to-GPU-to-APU-to-engine-generator handoffs with energized bus
    voltage never stop operational refresh or clear the matched plan;
16. a momentary battery/bus fact gap does not reset a live flight;
17. persistent all-batteries-off and de-energized-bus facts produce one
    confirmed cold-dark reset after the bounded confirmation period;
18. unavailable or contradictory power facts become `Unknown`, remain visible
    in diagnostics, and do not masquerade as a clean battery-off fact;
19. startup battery-on, startup battery-off, later battery-off/on, xPilot
    disconnect/reconnect, and plugin disable/re-enable lifecycle proofs remain
    green; and
20. the power correction uses only standard X-Plane facts and introduces no
    aircraft-name or private-dataref branch.

After focused proof, the complete regression suite must pass with no changed
expected result unless this gate explicitly requires it.

## 14. Rollback Gate

Before application code changes begin:

1. record the full working-tree status and hashes of all changed tracked files;
2. preserve a binary-safe patch of every current tracked modification;
3. preserve a manifest and copy of every current untracked source, contract,
   scenario, and probe file needed to reproduce the present state;
4. record the installed XVatsim plugin hash and preserve the existing installed
   rollback binary; and
5. verify that the rollback artifacts restore the current uncommitted
   controller-eligibility and Standby Assist state, not merely `HEAD`.

No product code may be changed until this rollback checkpoint is complete.

## 15. Live Verification Gate

Offline success authorizes building and installing a test plugin only after
X-Plane is shut down.

Live proof must record at least:

- an endpoint with a direct APP/DEP child;
- an endpoint where a source-listed larger parent takes over while the child is
  offline, when such a live opportunity is available;
- a nearby regional position such as THAMES when available;
- no regression in Standby Assist; and
- Brain receipts identifying which polygon lit and why the controller was
  classified as direct, parent, nearby, unrelated, or ineligible;
- a normal aircraft electrical transition or a deliberately exercised
  battery/GPU/APU/generator sequence where available, proving XVatsim remains
  operational while an electrical bus remains energized; and
- a genuine cold-dark shutdown proving the confirmed reset still occurs.

Passing offline tests means the implementation is ready for online testing. It
does not authorize commit, push, packaging, or user release. Those actions
remain blocked until the Product Owner confirms live behavior.

## 16. Approval

Product Owner approval of this gate authorizes the rollback checkpoint,
implementation, focused offline proof, full regression run, and preparation of
a test plugin while X-Plane is shut down. It does not authorize commit, push,
package publication, or release.

## 17. Implementation and Offline Proof

Implementation completed on 2026-10-22 without creating an xPilot board DLL
bridge and without adding an aircraft-specific dataref branch.

The terminal resolver now preserves source callsign prefixes, suffixes, and
full geometry for the departure and arrival endpoint packages. It returns the
airport-containing anchor polygons and directly intersecting polygons. The
Brain alone matches live APP/DEP positions to those facts and distinguishes a
direct endpoint polygon from a distinct overlapping nearby polygon. Positions
that merely share the same broad polygon key, such as unrelated SCT position
prefixes, do not gain nearby status from geometry alone.

Weak endpoint text absence and mismatch facts are neutral in the approved V2
decision path. Exact one-character Australia aliases remain positive. The
legacy rollback switch retains its prior negative behavior. Operational hard
guards and distance contradictions remain authoritative within their own
claims.

The aircraft worker now reports every standard X-Plane battery switch and the
maximum standard electrical-bus voltage. The Brain resolves those raw facts to
`Powered`, `Unpowered`, or `Unknown`, preserves an established powered session
through incomplete or momentary transition facts, and requires two continuous
seconds of all-batteries-off plus de-energized-bus facts before applying the
cold-and-dark reset.

The 2026-10-22 route-scope amendment removes the old open-ended APP/DEP
augmentation that promoted every worldwide AFV controller record into detailed
Brain voting. The Brain now admits additional non-receivable APP/DEP facts only
when exact endpoint polygon position facts, the protected endpoint alias rule,
or a transmitter within the endpoint radius ties the position to the filed
departure or arrival. Existing radio-reachable candidates and route Center/FIR
authority remain on their established paths. The scope receipt reports global
records received, endpoint candidates admitted, and records excluded outside
the two endpoint packages.

This amendment also prevents global VATSIM roster-count changes from changing
every local candidate's diagnostic identity and hard-bounds diagnostic storage
at 8 MiB per retained date, three dates total. These are post-decision logging
rules and cannot alter Brain facts, votes, or display authority.

Offline results:

- focused aircraft electrical proof: passed multi-battery, energized-bus,
  transition-gap, confirmed cold-dark, and unknown-fact cases;
- focused endpoint polygon proof: passed direct anchor plus distinct nearby
  overlap behavior;
- saved EGLL/THAMES source geometry: `THAMES_APP 132.700` accepted as nearby,
  with Brain score `2 positive / 0 negative / 8 neutral` in the focused
  receipt;
- preserved Cleveland false-controller proof: rejected by the original
  primary `199.998` hard guard when endpoint-scoped, while unrelated
  `CLT_APP` is excluded before detailed Cleveland voting;
- endpoint-scope proof: `THAMES_APP` was admitted from the EGLL terminal
  package while unrelated `EDDF_N_APP` was excluded from detailed voting;
- diagnostic churn proof: 1,000 global roster-count changes emitted zero
  duplicate local candidate receipts, while a real roster-state change emitted
  one receipt;
- diagnostic storage proof: rolling preserved the newest record and kept all
  three retained test files within the configured hard limit;
- Australia `ML`/`MLL` proof: passed;
- Southern California same-polygon filtering and vNAS precedence cases:
  passed;
- Release plugin build: passed;
- complete applicable regression set: `911 / 911` passed; and
- the one excluded machine-locked scenario is the previously documented
  xPilot 3.0.2 binary fingerprint test. This machine has the xPilot 4 beta
  binary (`2,257,072` bytes, SHA-256
  `DEA6B12F64C0E35F9E61398B9AA659C3A8896886B4890D4EC89C50ACD6CD22A2`),
  while that test requires the unavailable 3.0.2 binary. Its relative-path
  companion test passed.

The final Release test plugin was installed while X-Plane and xPilot were shut
down. Installed SHA-256:
`B55975860FD36351C6BDCD4E5199B9B3287BBC899DE54C5D95FF24EFA42B77E9`.
The Product Owner verified EGLL North Approach and THAMES Approach online,
then verified that THAMES disappeared when the official VATSIM roster removed
it. The Product Owner authorized the combined source commit on 2026-10-22.
Packaging and release remain a separate later step.
