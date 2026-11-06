# Canonical Live Authority Polygon Map Contract Gate

**Gate ID:** `CANONICAL-LIVE-AUTHORITY-POLYGON-MAP-01`  
**Status:** Partially superseded by
`RADIO_SCOPED_ROUTE_AUTHORITY_CONSOLIDATION_CONTRACT_GATE.md`. Worldwide static
geometry, exact ownership, conflict, freshness, and Brain-publication rules
remain binding. Worldwide live-controller activation before radio-board
admission is retired.  
**Prepared:** 2026-10-25  
**Repository baseline:** `6b39f7b2351abc2b4c635b4d13650b62e9563d9f` plus the
uncommitted Center-frequency selection and terminal endpoint relationship work  
**Motivating live flight:** `GTI8274`, `KNGU` to `EDFH`, 2026-10-25  
**Scope:** Global Center/FIR/UIR and APP/DEP polygon definitions, live controller
activation, dynamic ownership, endpoint and route queries, Brain decisions,
controller boards, diagnostics, and Standby Assist inputs

## 1. Objective

XVatsim must have one complete, immutable, source-backed authority polygon map.
That map must contain every valid polygon acquired from the accepted Center and
terminal data sources, regardless of whether a polygon is currently on the
filed route or contains the departure or arrival airport.

A live controller must light the exact polygon or polygons assigned to its
source-defined position and current operating configuration. The lit state must
carry the live callsign and live contact frequency. The Brain must query that
same map for aircraft containment, route intersection, departure ownership,
arrival ownership, controller-board publication, and Standby Assist inputs.

The correction is architectural. It may not add a third polygon catalog, a new
vote, a KNGU/PCT exception, a parallel UI interpretation, or another adapter
that leaves the current duplicate paths authoritative.

Required end state:

```text
accepted static geometry and position sources
    + live VATSIM controller identity/frequency
    + current source-published ownership/configuration
    + per-source health, revision, freshness, and completeness
        -> one Brain-owned canonical live authority map
            -> aircraft containment query
            -> filed-route intersection query
            -> departure-airport query
            -> arrival-airport query
                -> one Brain controller decision
                -> immutable controller boards
                -> UI and Standby Assist consume only those boards
```

The governing product invariant is:

```text
what the canonical map lights
    = what the Brain can treat as active authority
    = what every downstream consumer is allowed to use
```

## 2. Product Meaning of “The Polygon Map Includes It All”

### 2.1 Complete static map

The canonical static catalog contains all valid source polygons. Route and
endpoint queries may use spatial indexes to select a bounded subset, but no
query may create a different catalog or discard source geometry from the
canonical publication.

An offline polygon remains in the static catalog. It is simply unlit.

### 2.2 Lit polygon

A polygon is lit only when a current live controller is joined to an exact
source-defined position and that position or an explicit current ownership
assignment owns the polygon.

A lit assignment contains at least:

- canonical polygon identity;
- live controller callsign;
- live VATSIM contact frequency;
- exact source position identity;
- controller role/facility;
- ownership kind, including direct, split, combined, bandboxed, top-down, or
  parent where the source supplies it;
- activation proof and source provenance;
- source and live-feed generations; and
- freshness/completeness state.

Static polygon geometry does not inherently contain a current frequency. A
polygon definition may exist with no controller online. The frequency belongs
to the live controller assignment, with any source-configured position
frequency retained as matching evidence rather than substituted for live
truth.

### 2.3 Multiple assignments

The model must support all of the following without guessing:

- one controller lighting one polygon;
- one combined controller lighting several explicitly owned polygons;
- several split controllers each lighting distinct polygons;
- an explicitly published divided-responsibility case;
- a direct child taking precedence over a parent/top-down controller; and
- a conflict state when fresh sources assign incompatible owners.

The model may not assume that every polygon has exactly one permanent
frequency or exactly one permanent controller callsign.

### 2.4 Internal map and visual proof

This gate governs the internal authority map used by the product. It does not
require adding a full cartographic map to the cockpit overlay. It does require
a deterministic diagnostic GeoJSON or equivalent visual projection so the
complete and lit polygon states can be inspected and compared during offline
and live proof. The diagnostic view renders the canonical state; it may not
recompute ownership.

## 3. Relationship to Existing Contracts

This gate preserves the Brain-only decision law from:

- `docs/BRAIN_OWNED_RUNTIME_CONTRACT.md`;
- `docs/AUTHORITY_EVIDENCE_CONTRACT.md`;
- `docs/ENDPOINT_TERMINAL_POLYGON_OWNERSHIP_CONTRACT_GATE.md`; and
- `docs/TERMINAL_ENDPOINT_AUTHORITY_RELATIONSHIP_CONTRACT_GATE.md`.

It supersedes those documents wherever they allow separate route and endpoint
polygon catalogs, candidate-scoped ownership collection, worker-side polygon
activation as final authority, compatibility survivors as live state, or
radio/ATIS inference to light a polygon without exact source ownership.

The typed terminal relationship already implemented remains the mandatory
endpoint result. Its geometry and ownership facts must eventually come from
this canonical map rather than the separate terminal resolver.

Center-frequency row selection, CTAF/UNICOM policy, and Standby Assist sequence
are not redesigned by this gate. They must consume the corrected boards and
must not bypass the canonical map.

## 4. Audit Method and Preserved Baseline

The audit traced:

- map-data manifest and source-registry acquisition;
- static geometry and position compilation;
- live VATSIM controller activation;
- vNAS facility, position, TCP, and endpoint evidence;
- AFV/transceiver and duplicated-ATIS activation fallbacks;
- route/aircraft relevance filtering;
- terminal endpoint resolution;
- Brain evidence and board publication;
- harness-only legacy departure, arrival, and enroute modules; and
- UI/Standby Assist consumption of final boards.

The current SimAware terminal file inspected on 2026-10-25 was:

- source: `TRACONBoundaries.geojson` from the current SimAware release;
- SHA-256:
  `5CC21E2EA50B5E6B1ECF1D8ACB7B37277301D5941AF1ABC6B93E750F6E4CDE9A`;
- source features: `1,355`;
- polygon parts: `1,369`;
- derived APP/DEP service records under the current missing-suffix expansion:
  `2,561`;
- unique derived service keys: `2,552`; and
- derived records silently vulnerable to current first-key deduplication: `9`.

This count is preserved as audit evidence, not as a permanent expected source
count. The implementation test must calculate and reconcile the counts from
whatever source revision it is given.

## 5. Confirmed Current Architecture

### 5.1 Capabilities already present

XVatsim already has substantial pieces of the desired system:

- `core::authority::AuthorityPolygonCatalog` stores Center and terminal
  polygon records;
- `core::authority::ControllerAuthorityCatalog` stores source position,
  callsign-pattern, frequency, and polygon associations;
- the route source worker downloads VATSpy, SimAware, VATGlasses, special
  sector data, and the packaged global authority-source registry;
- `ActivateAuthorityPolygons` can join accepted live controller positions to
  polygons;
- `ResolveRelevantAuthorityPolygons` can test aircraft and route geometry;
- live controller frequency is propagated into `RelevantAuthoritySnapshot`;
- vNAS supplies detailed US facility, position, area, TCP, and assumed-sector
  facts; and
- the Brain now applies a typed endpoint terminal relationship before an
  APP/DEP row reaches a final board.

The defect is therefore not a total absence of polygon data. The defect is
that these capabilities are split across multiple owners, reduced into
different partial products, and interpreted through incompatible paths.

### 5.2 Current route authority path

`RouteSectorResolver.cpp` builds an internal `AuthoritySourceDataset` containing
a controller-authority catalog and authority-polygon catalog. It merges:

- VATSpy Center/FIR/UIR definitions and boundaries;
- SimAware terminal geometry and generated APP/DEP position patterns;
- VATGlasses and special-sector position/ownership packages; and
- temporary route-endpoint airport-local polygons.

The route authority engine then filters this catalog to route geometry before
activating controllers. It publishes relevant active controllers, not the
complete polygon map.

The complete source dataset is private to the route module. It is not the
single published runtime authority state for endpoint, UI, and Assist
consumers.

### 5.3 Current terminal endpoint path

`TerminalAuthorityResolver.cpp` independently downloads the SimAware terminal
file from a hardcoded URL, compiles another catalog, and answers one airport at
a time. Its output contains only polygons that contain the airport or intersect
an airport-containing polygon.

It does not contain live controllers, live frequencies, current split/combined
ownership, the global Center map, or the complete global terminal catalog.

### 5.4 Current vNAS path

`BuildVnasTerminalEvidenceSnapshot` takes the current radio-reachable candidate
list and emits endpoint-specific ownership records only for matching APP/DEP
candidates. This makes detailed ownership discovery dependent on an earlier
radio candidate selection. A controller omitted from that list cannot enrich
the map even when vNAS knows its exact active position and assumed sectors.

### 5.5 Current live publication

`AuthorityRelevanceSnapshot` publishes route/aircraft-relevant active
authorities. `BrainTerminalAuthorityWorkerOutput` separately publishes an
airport geometry package. `VnasTerminalEvidenceSnapshot` separately publishes
candidate-scoped dynamic ownership. `BrainControllerEvidencePipeline.cpp`
reassembles these fragments when evaluating APP/DEP rows.

The UI receives final controller rows, but there is no complete published live
polygon state that can be inspected to prove which polygons were offline,
active, conflicted, stale, or unmapped.

## 6. Confirmed Audit Findings

| ID | Severity | Finding | Required correction |
|---|---|---|---|
| A-01 | Critical | The global route catalog and terminal endpoint catalog are separate authoritative interpretations of the same SimAware data. | Publish one canonical source catalog and remove the second production compiler/downloader. |
| A-02 | Critical | The two paths can use different source generations because the route worker uses the map-data manifest while the terminal worker uses its own hardcoded download and cache. | One source acquisition, validation, revision, freshness, and atomic publication path. |
| A-03 | Critical | The supposed global map is owned privately by `route_sector` and activated only after route scoping. | Move canonical compilation and live activation outside route selection; make route scope a query over the live map. |
| A-04 | Critical | The endpoint worker intentionally discards every terminal polygon that neither contains nor intersects the requested airport package. | Endpoint queries return a view; they may not redefine or truncate the canonical catalog. |
| A-05 | Critical | vNAS ownership is collected only for radio-reachable APP/DEP candidates. | Build dynamic ownership facts from the live vNAS controller/position feed independently of radio-board admission. |
| A-06 | Critical | Facility/group semantics differ between paths. The endpoint path adds the SimAware feature ID, such as `PCT`, as a controller prefix for every child feature, while the route path keys the same geometry primarily by each published prefix such as `BWI`, `DCA`, or `IAD`. | Represent facility group, source feature, child prefix, service role, and fallback ownership as separate typed fields with one tested join rule. |
| A-07 | High | `BuildTerminalAuthorityPolygonRecords` uses first-key deduplication. In the audited source revision, 9 valid derived records share service keys and later records are dropped. Examples include `CYVR_DEP`, `LAX_DEP`, `LEMD_APP`, `LEMD_DEP`, `LEZL_APP`, and `LEZL_DEP`. | Never deduplicate by service key alone. Merge geometry parts only under an explicit same-feature identity or retain distinct canonical polygons. |
| A-08 | High | SimAware polygon IDs are not guaranteed unique per source feature/geometry component. Lookup by polygon ID returns the first match. | Generate stable unique canonical IDs from source, revision-independent feature identity, service role set, and geometry component identity. Assert uniqueness. |
| A-09 | High | Geometry topology is interpreted differently. The endpoint resolver treats the first ring as exterior and later rings as holes; the core/route path treats containment in any ring as inside. The route terminal parser also reads only exterior rings. | Use one topology-preserving `MultiPolygon` model with exterior and interior rings and one shared geometry library. |
| A-10 | High | Missing SimAware suffix is represented as duplicated APP and DEP records in one path and a shared APP/DEP polygon in another. | Store a typed service-role set. Do not manufacture duplicate geometry to represent shared service. |
| A-11 | High | Full source packages can be partially downloaded and merged without a published per-entry/region completeness ledger. | Publish source health for every configured lane and region; never describe a partial global map as complete. |
| A-12 | High | Transceiver geography and duplicated ATIS text can create active authority polygons in the route path. | AFV may validate radio compatibility and ATIS may corroborate an already mapped assignment; neither may light a polygon without exact source ownership. |
| A-13 | High | The Brain-owned authority projection currently selects from route-sector compatibility survivors rather than independently deciding from a canonical live-map fact packet. | Publish typed activation facts to a Brain-owned map decision and remove compatibility survivor authority. |
| A-14 | High | The live frequency is attached only late in `RelevantAuthoritySnapshot`; there is no reusable live polygon-assignment object. | Put live callsign/frequency and position proof on the live activation assigned to each polygon. |
| A-15 | Medium | Source catalogs are concatenated without an explicit capability-specific conflict model. Overlapping coarse and detailed geometry can coexist without a named precedence or conflict state. | Define source authority by capability and preserve conflicts; do not use anonymous merge order. |
| A-16 | Medium | Route, aircraft, endpoint, and UI products expose different slices and identifiers, making parity difficult to prove. | Derive every slice from the same immutable map generation and preserve canonical IDs in every receipt. |
| A-17 | Medium | Harness-only departure, arrival, and enroute modules retain independent authority-matching implementations. Their headers block production use, but their logic and fixtures can preserve obsolete expectations. | Delete them after migration or rewrite the harness to exercise only production canonical-map and Brain seams. |
| A-18 | Medium | No diagnostic map export shows all polygon definitions and live activation states. | Add deterministic JSON/GeoJSON proof output generated from the canonical snapshot. |

## 7. Canonical Static Data Model

The implementation may choose names appropriate to the codebase, but the
canonical model must preserve the following concepts without overloading
strings or booleans.

### 7.1 Source revision

Each source publication records:

- source lane and provider;
- configured URL or packaged identity;
- provider revision/commit when available;
- content hash;
- fetch time and age;
- parse status;
- expected and accepted feature/part counts;
- rejected record count and typed rejection reasons;
- applicable geographic region; and
- completeness state: `COMPLETE`, `PARTIAL`, `UNAVAILABLE`, `STALE`, or
  `INVALID`.

### 7.2 Canonical polygon definition

Each definition records:

- globally unique canonical polygon ID;
- source feature ID and source geometry component ID;
- provider/source lane;
- facility/group ID, when supplied;
- display name;
- authority class: Center, terminal, extension, or airport-local;
- terminal service-role set, such as `{APP}`, `{DEP}`, or `{APP, DEP}`;
- exact source position IDs, callsign patterns, prefixes, and lookup tokens;
- topology-preserving geometry;
- precomputed bounding box and spatial-index key;
- source record/revision provenance; and
- data-gap flags.

Facility group and controller position are different identities. `PCT` as a
facility group, `BWI` as a source prefix, and `PCT_APP` as a live controller
position must not be collapsed into one token.

### 7.3 Geometry topology

Geometry must be represented as:

```text
MultiPolygon
    -> Polygon part
        -> one exterior ring
        -> zero or more interior rings/holes
```

The shared geometry implementation must define and test:

- valid ring closure and minimum point count;
- longitude normalization and anti-meridian crossing;
- winding-order independence;
- holes;
- multipolygon components;
- point-on-boundary behavior;
- segment intersection;
- route-entry distance; and
- invalid/self-intersecting geometry handling.

No consumer may maintain its own point-in-polygon implementation.

### 7.4 Controller position definition

Each position definition records:

- source position ID and facility/group identity;
- callsign patterns and facility role;
- configured frequency set, when supplied;
- directly owned canonical polygon IDs;
- explicitly allowed group/fallback polygon IDs;
- parent/child, split, combined, bandbox, and top-down relationships where the
  source supplies them;
- operational constraints such as APP versus DEP; and
- proof source/revision.

Configured frequency is a position-matching constraint. It is not the live
frequency shown to the pilot.

## 8. Canonical Live Data Model

### 8.1 Live controller identity

VATSIM roster truth supplies:

- online status;
- exact callsign;
- live primary frequency;
- facility type;
- controller record identity and generation; and
- ATIS text as non-authoritative descriptive data.

### 8.2 Dynamic ownership

Where available, current operating data supplies:

- exact live position ID;
- current facility/area;
- assumed sectors/TCPs;
- parent and child topology;
- current split/combined/bandbox state;
- endpoint airport association; and
- source generation and freshness.

vNAS is the detailed United States provider currently available to XVatsim.
Its output must join the same canonical model. It may not remain a separate
candidate vote.

### 8.3 Live polygon activation

Each controller-to-polygon assignment has one of these states:

- `ACTIVE_EXACT`: exact source position owns the polygon;
- `ACTIVE_DYNAMIC`: fresh current operating configuration assigns it;
- `ACTIVE_PARENT`: fresh explicit parent/top-down relationship assigns it;
- `CONFLICT`: fresh applicable sources assign incompatible owners;
- `UNMAPPED`: the live controller cannot be joined to a source position;
- `STALE`: required source or live facts are stale; or
- `OFFLINE`: the polygon is valid but has no current live assignment.

The activation records the live callsign and live frequency. `CONFLICT`,
`UNMAPPED`, `STALE`, and `OFFLINE` do not create a controller row.

## 9. Source Authority Boundaries

Each source may answer only the question it actually owns.

| Source/fact | May prove | May not prove |
|---|---|---|
| VATSIM roster | online callsign, facility, live contact frequency | polygon ownership by itself |
| VATSpy | FIR/UIR identity and baseline Center boundary association | detailed terminal split/combined ownership |
| SimAware terminal data | terminal facility/position prefixes and published geometry | live frequency or current takeover by itself |
| VATGlasses/special sector data | source position, configured frequency, polygon ownership, hierarchy where published | live online status |
| vNAS | current US position, facility, area, TCP, split/combined and endpoint ownership facts | terminal polygon geometry it does not publish |
| AFV/transceivers | radio frequency/station compatibility and reachability | polygon ownership or activation |
| Published airport frequencies | role/frequency corroboration | current online controller ownership |
| ATIS text | diagnostics or corroboration of an already source-owned position | new polygon ownership or activation |
| Route/aircraft/airport geometry | spatial relevance and containment | controller identity or ownership |
| Polygon intersection/adjacency | geometric context | shared ownership, parentage, or activation |

Unavailable higher-detail data may expose an unknown or fallback state, but it
may not promote a weaker fact outside its authority boundary.

## 10. Source Precedence and Conflict Rules

Precedence is capability-specific, not one global provider ordering.

1. VATSIM remains authoritative for whether a controller is online and for its
   current contact frequency.
2. Fresh current operating configuration is authoritative for split, combined,
   bandbox, and parent/top-down assignment.
3. Exact published position-to-polygon ownership is authoritative for static
   mapping.
4. Facility-level SimAware/VATSpy geometry is fallback coverage only where no
   more specific applicable position ownership exists.
5. AFV, route proximity, airport proximity, callsign similarity, and ATIS text
   cannot replace an ownership mapping.

When two fresh sources make incompatible claims in the same capability lane,
the map records `CONFLICT` with both claims and applies a named reviewed
precedence rule if one exists. It may not cancel the claims into a vote, select
the nearest geometry, or silently keep whichever record was merged first.

## 11. Canonical Activation Algorithm

For every actionable online Center/FSS/APP/DEP controller:

1. Preserve the original live VATSIM identity, facility, and frequency.
2. Find exact source positions whose callsign pattern and facility role match.
3. When the source position declares configured frequencies, require a valid
   frequency match under the established normalization rules.
4. Apply fresh dynamic operating configuration to determine assumed, split,
   combined, parent, or top-down ownership.
5. Resolve the resulting position assignments to canonical polygon IDs.
6. Light every explicitly owned polygon with one activation record carrying
   live callsign, live frequency, exact position ID, ownership kind, and proof.
7. If several exact assignments remain incompatible, publish `CONFLICT` rather
   than choose by order, distance, or vote.
8. If no exact source position or explicit allowed fallback can be established,
   publish `UNMAPPED` and leave the polygons unlit.

A generic facility controller may light a group of child polygons only when
the source model explicitly defines that group fallback. Prefix containment,
shared facility spelling, polygon overlap, or polygon size may not invent the
group assignment.

AFV reachability, route intersection, endpoint containment, and aircraft
position are applied after activation. They decide relevance to the flight;
they do not decide which controller owns the polygon.

## 12. Query Contract

All queries use one immutable canonical map snapshot and return its generation.

### 12.1 Aircraft query

Returns active assignments whose exact polygon geometry contains the current
aircraft point.

### 12.2 Route query

Returns active assignments whose polygons intersect the filed route, with
entry distance and current/next ordering. It may use a bounded spatial index,
but it may not activate controllers or compile a smaller authority map.

### 12.3 Endpoint query

For the exact departure or arrival airport coordinate, returns:

- every containing canonical polygon;
- every active controller assignment on those polygons;
- exact position and service-role compatibility;
- fresh explicit parent/top-down assignments; and
- a typed relationship result for each evaluated live APP/DEP controller.

Polygon overlap or adjacency may be included as diagnostic context only.

### 12.4 Offline polygon query

Diagnostics may query unlit polygon definitions. Offline polygons may not
produce controller rows or standby frequencies.

## 13. Brain Ownership and Publication

Source and network workers may fetch, parse, validate, and publish immutable
facts. They may not decide final active authority or controller display.

The Brain-owned map decision must publish one immutable
`LiveAuthorityMapSnapshot` or equivalent containing:

- canonical source-map identity;
- per-source revision/health ledger;
- controller-feed generation;
- dynamic-ownership generation;
- every static polygon definition by shared immutable reference;
- active/conflicted/unmapped assignment records;
- stable indexes for polygon ID, source position, callsign, and spatial query;
  and
- deterministic diagnostics.

The route resolver, terminal endpoint query, controller evidence pipeline,
display publisher, UI, and Standby Assist become consumers of this snapshot.
None may independently map a controller to a polygon.

The large immutable geometry may be shared by pointer between generations.
The flight loop and UI thread may not copy or rebuild the world catalog.

## 14. Required Structural Corrections

### 14.1 Move the global map out of `route_sector`

The global controller/polygon source publication must become a dedicated
canonical authority-map component. `RouteSectorResolver` may retain route
preparation and geometry-query responsibilities, but it may not own the only
full authority catalog.

### 14.2 Retire the duplicate terminal resolver

Production use of `TerminalAuthorityResolver` as a downloader/compiler must be
removed. Departure and arrival containment must query the canonical map.

Any temporary adapter must:

- wrap the canonical snapshot rather than fetch or parse data;
- be marked migration-only;
- contain no ownership logic;
- have a removal test and deadline; and
- be incapable of surviving gate closure.

### 14.3 Integrate vNAS before radio-board filtering

`VnasDataClient` must publish current live controller-position ownership facts
for the applicable US facilities independently of `RadioReachableControllerCandidate`.
The Brain joins those facts to the canonical map. Radio reachability remains a
later presentation/service fact.

### 14.4 Replace route activation fallbacks

`ActivateAuthorityPolygonsByTransceiverRouteProof` may remain only as a
diagnostic compatibility check or must be removed. It may not create a lit
polygon.

`ActivateAuthorityPolygonsByDuplicatedAtisProof` may corroborate a source-owned
mapping but may not create one from unstructured text.

### 14.5 Remove compatibility authority

`compatibilityRelevantAuthorities`, `oldCompatibilityRelevantSurvivor`, and
the current Brain projection that selects only from old route-sector survivors
must not remain authoritative. The Brain must decide from typed canonical-map
facts.

### 14.6 Retire parallel harness modules

Harness-only legacy `DepartureModule`, `ArrivalAirspaceModule`,
`ArrivalLocalModule`, `ArrivalModule`, and `EnrouteModule` logic must be removed
or replaced with production-seam tests. A compile-time production guard is not
sufficient if obsolete logic continues to define expected behavior.

## 15. Required Legacy-Path Disposition

Before closure, the implementation closeout must complete this table with
final file locations, replacement symbols, and zero-authority proof.

| Current symbol/path | Current role | Required disposition |
|---|---|---|
| `AuthoritySourceDataset` inside `RouteSectorResolver.cpp` | Hidden complete source catalog | Move to the dedicated canonical authority-map publication. |
| `BuildTerminalAuthorityPositionRecords` | Generates SimAware APP/DEP positions | Replace with one topology- and group-aware canonical compiler. |
| `BuildTerminalAuthorityPolygonRecords` | Generates route terminal polygons with first-key dedup | Replace; preserve every feature/component and assert counts. |
| `TerminalAuthorityResolver` | Second downloader, parser, cache, and endpoint catalog | Remove from production and delete after migration. |
| `BrainTerminalPolygonFact` / `BrainTerminalAuthorityWorkerOutput` | Separate endpoint polygon packet | Replace with canonical endpoint-query facts carrying canonical IDs/generation. |
| `BuildVnasTerminalEvidenceSnapshot(...candidates...)` | Candidate-scoped dynamic ownership | Replace candidate input with live position/facility facts independent of radio-board admission. |
| `ActivateAuthorityPolygonsByTransceiverRouteProof` | Can create active polygons from radio geography | Remove activation authority; diagnostic-only or delete. |
| `ActivateAuthorityPolygonsByDuplicatedAtisProof` | Can create active polygons from ATIS text | Remove activation authority; corroboration-only or delete. |
| `SourceAuthorityMatch` | Rejoins a route-relevance slice during terminal evaluation | Consume exact canonical activation/endpoint-query result instead. |
| `EvaluateTerminalPolygonRelationship` | Rejoins separate terminal geometry to candidates | Consume typed canonical endpoint relationship. |
| `compatibilityRelevantAuthorities` projection | Preserves route resolver survivors as Brain input | Remove live authority; canonical typed facts become Brain input. |
| Harness-only board modules | Parallel matching implementations | Delete or rewrite tests through production seams. |
| Any terminal/authority rollback flag | Potential old-path resurrection | Remove or fail closed; no flag may restore split catalogs. |

No entry may be closed as “left in place but unused” without a static reference
audit and a regression proving it cannot affect production or test authority.

## 16. Completeness and Parser Invariants

Every source compiler must produce a reconciliation receipt:

```text
input features
    = accepted canonical features
    + explicitly merged same-feature components
    + rejected features with typed data gaps
```

Required invariants:

- every valid source geometry component has a unique canonical identity;
- no valid geometry is dropped because a service key already exists;
- all exact duplicate merging is deterministic and preserves every geometry
  component and source record;
- source feature group and child position prefix remain distinct;
- missing service suffix becomes a role set, not duplicated geometry;
- every canonical polygon ID is unique;
- every position-owned polygon reference resolves;
- every unresolved reference becomes a typed data gap;
- no lookup returns an arbitrary first record from a non-unique key;
- per-source and per-region completeness is published; and
- source revision changes cannot mix old position mappings with new geometry in
  one accepted snapshot.

## 17. Diagnostic and Visual Proof Contract

Every accepted map generation must be able to emit a bounded deterministic
diagnostic package containing:

### 17.1 Catalog receipt

- source revisions and health;
- feature, polygon-part, position, ownership, and data-gap counts;
- duplicate source keys and their explicit disposition;
- unique-ID audit; and
- unresolved position-to-polygon references.

### 17.2 Live activation receipt

For every actionable online authority controller:

- live callsign, facility, and frequency;
- exact matched position IDs;
- rejected mappings and reasons;
- current dynamic ownership facts;
- canonical polygons lit;
- activation state and proof;
- conflict or unmapped reason; and
- generation tuple.

### 17.3 Query receipt

For aircraft, route, departure, and arrival queries:

- canonical map generation;
- query coordinate/route identity;
- candidate polygon IDs;
- exact containment/intersection result;
- active assignments returned;
- typed endpoint relationships; and
- final controller-board disposition.

### 17.4 Diagnostic GeoJSON

Offline and authorized live proof must be able to export canonical polygons
with properties including:

- canonical polygon ID;
- source/provider;
- facility/group and source position IDs;
- service roles;
- activation state;
- live callsign/frequency when active;
- ownership kind and proof source; and
- source health/generation.

External map screenshots may be used as corroboration, but another site's
rendered result is not an authority source and must not be scraped into
production decisions.

## 18. Mandatory Pre-Fix Red Proofs

Before implementation changes production behavior, tests using current
production seams must preserve these failures:

1. **Split catalog proof:** show that route authority and terminal endpoint
   authority are compiled and published through different owners/generations.
2. **PCT group proof:** show the route and endpoint paths produce different
   group/child mappings for representative `PCT`, `BWI`, `DCA`, or `IAD`
   features.
3. **Dropped geometry proof:** feed two valid terminal features with the same
   derived service key and prove the current route builder retains only the
   first.
4. **Current-source reconciliation proof:** preserve the 2026-10-25 source
   example where 2,561 derived service records become only 2,552 unique keys
   under first-key deduplication.
5. **Candidate-scoped vNAS proof:** show an active vNAS position produces no
   ownership fact when its controller is absent from the radio candidate list.
6. **Topology divergence proof:** show a polygon with a hole is classified
   differently by the core/route and terminal point-in-polygon implementations.
7. **Inference activation proof:** show transceiver or duplicated-ATIS fallback
   can currently create an active polygon without an exact source-position
   assignment.
8. **KNGU/PCT proof:** preserve the existing live-derived case where PCT is not
   a direct or explicit parent owner of KNGU.

If a claimed failure cannot be reproduced through the actual code seam, this
gate must be amended before implementation continues.

## 19. Required Focused Regression Matrix

### 19.1 Source ingestion

1. Every valid feature and multipolygon component is reconciled.
2. Duplicate service keys retain all distinct geometry.
3. Exact duplicate source records merge deterministically without losing
   geometry or provenance.
4. Polygon IDs remain unique across repeated facility names, service roles,
   sources, and components.
5. Missing suffix produces `{APP, DEP}` on one geometry definition.
6. Exterior rings and holes are preserved.
7. Anti-meridian polygons and route legs are handled correctly.
8. Invalid rings become typed gaps without invalidating unrelated valid
   features.
9. A failed regional registry entry makes that region `PARTIAL` or
   `UNAVAILABLE`; the rest of the map remains explicitly healthy.
10. A source refresh publishes atomically and never mixes revisions.

### 19.2 Controller activation

11. Exact Center position lights its exact Center polygon with live frequency.
12. Exact APP position lights only its exact terminal polygon or explicitly
    defined group fallback.
13. Exact DEP position respects departure service compatibility.
14. One combined controller lights all explicitly assumed polygons.
15. Split controllers light only their assigned sectors.
16. Fresh vNAS split/combined ownership changes the activation set without
    depending on radio reachability.
17. Wrong live frequency fails a source-configured frequency constraint.
18. Wrong VATSIM facility type cannot light the polygon.
19. Same-frequency controllers remain distinct by controller and position
    identity.
20. Unmapped online controller is visible in diagnostics but lights nothing.
21. AFV reachability alone lights nothing.
22. Route intersection alone lights nothing.
23. Airport containment alone lights nothing.
24. Polygon overlap/adjacency alone lights nothing.
25. ATIS text alone lights nothing.
26. Conflicting fresh assignments produce `CONFLICT`, not merge-order success.
27. Stale dynamic ownership cannot replace a fresh exact static assignment or
    invent a takeover.

### 19.3 Flight queries

28. Aircraft inside an active polygon returns that assignment.
29. Route intersection returns ordered active assignments and entry distance.
30. Offline polygons intersecting the route return no controller rows.
31. Departure and arrival query the exact airport coordinate, not the aircraft
    position or polygon adjacency.
32. KNGU resolves direct terminal ownership from its containing polygon and
    rejects PCT without explicit ownership.
33. A real PCT-served airport accepts the appropriate direct or explicit
    current owner.
34. PCT generic group fallback, BWI/DCA/IAD split positions, and child
    precedence follow one explicit mapping model.
35. SCT APP/DEP shared and split examples retain all geometry and correct role
    compatibility.
36. CYVR and Madrid duplicate-key examples retain all source parts.
37. EGLL/THAMES overlap cannot become endpoint ownership.
38. Parent/top-down ownership is accepted only from an explicit fresh graph.
39. Source loss yields `UNKNOWN`/unavailable rather than a fabricated owner.

### 19.4 Consumer parity

40. Route, departure, arrival, final display, and diagnostic export report the
    same canonical polygon IDs and generation.
41. Every displayed Center/APP/DEP row traces to one active canonical
    assignment.
42. No APP/DEP row bypasses the typed terminal relationship invariant.
43. Standby Assist consumes only the final approved board.
44. Center-row click behavior remains limited to Center and still tunes COM1.
45. TWR/GND/DEL/CTAF/UNICOM behavior remains unchanged unless a separately
    approved gate changes it.
46. A stale or conflicted map cannot resurrect a row through legacy code.

## 20. Performance and Threading Contract

Completeness does not authorize per-frame world scans.

Required runtime design:

- fetch and parse all static source data off the simulator and UI threads;
- compile the immutable global catalog once per accepted source generation;
- build canonical indexes by polygon ID, source position, callsign pattern,
  facility/group, region, and geometry bounding box;
- update live activations from controller and dynamic-ownership generation
  changes, not every flight-loop callback;
- use the spatial index for aircraft, route, and endpoint queries;
- share immutable geometry across snapshots;
- bound retained generations and retire large snapshots off the flight loop;
- publish atomic last-known-good snapshots; and
- preserve the existing release performance gates.

Route scoping remains a performance optimization for queries. It may not be a
data-ownership or controller-activation rule.

The closeout must report:

- source download/parse time;
- static compile time and retained bytes;
- live activation time for representative and high-density rosters;
- aircraft, route, and endpoint query time;
- UI/flight-loop wall time impact;
- retained snapshot count and peak bytes; and
- cancellation, replacement, and teardown behavior.

## 21. Rollback Plan

Rollback is a source-state recovery procedure, not a runtime feature switch.
No production flag may restore the split catalog, vote, inference-activation,
or compatibility-survivor behavior retired by this gate.

Before implementation began, the complete uncommitted working state was
preserved under:

`outputs/canonical_live_authority_polygon_map_01/rollback`

The checkpoint contains:

- `pre_implementation_tracked.patch`, a full-index binary patch of every
  tracked change relative to baseline
  `6b39f7b2351abc2b4c635b4d13650b62e9563d9f`;
- `pre_implementation_approved_untracked.zip`, containing every pre-existing
  untracked contract, proof artifact, and regression scenario after the
  approval record was written; and
- `ROLLBACK_PLAN.md`, recording validation and recovery procedure.

Checkpoint hashes:

- tracked patch SHA-256:
  `33A8F90E46B9853546312B74B8C1A146A3AE95FA5A2F0161421E8AB1E5AA4885`;
- approved untracked archive SHA-256:
  `F0BEB5F35871EEE5A44D090253A7A3C5012ABBAA0685488B70607396A401222B`.

The checkpoint was verified to contain 17 untracked files and a readable patch
beginning at the expected Brain decision changes.

Implementation must also preserve a patch and test receipt after each major
phase. If a phase fails its focused tests or creates unbounded ambiguity, the
phase is reversed from its own patch or reconstructed in a separate recovery
directory from the baseline plus the approved checkpoint. The working tree may
not be destructively reset.

Rollback acceptance requires:

1. reconstruct the pre-implementation state in a separate directory;
2. verify both checkpoint hashes before use;
3. apply the tracked patch to the exact baseline;
4. extract the untracked archive without overwriting the active tree;
5. compare the reconstructed tree with the approved pre-implementation
   manifest;
6. preserve the failed implementation attempt before replacing any files;
7. restore only reviewed affected files;
8. rebuild and rerun the terminal relationship and Center-frequency focused
   suites; and
9. record Product Owner authorization before any installed plugin is rolled
   back.

At every intermediate phase, the source must compile and the prior accepted
behavior must remain testable. A half-migrated tree is not a rollback target.

## 22. Implementation Sequence

After explicit Product Owner approval of this gate:

1. preserve the mandatory red proofs and current source receipt;
2. introduce the topology-preserving canonical geometry and stable identity
   model;
3. implement parser reconciliation and per-source health ledgers;
4. move source acquisition and the global catalogs out of `route_sector` into
   one immutable canonical source publication;
5. implement exact position and dynamic-ownership facts independent of flight
   relevance and radio-board admission;
6. implement the Brain-owned live activation map;
7. implement indexed aircraft, route, departure, and arrival queries;
8. migrate Center/FIR route consumers;
9. migrate APP/DEP endpoint consumers and preserve the typed relationship gate;
10. migrate diagnostics, board publication, UI, and Standby Assist;
11. remove the duplicate terminal downloader/compiler and inference activators;
12. remove compatibility-survivor authority and parallel harness modules;
13. run the static ambiguity audit;
14. run focused regressions and real-source reconciliation;
15. run the complete environment-independent suite;
16. build the Release plugin;
17. generate diagnostic GeoJSON and reviewed map receipts;
18. only after separate authorization, install for controlled live proof;
19. perform live Center, direct terminal, split, combined/parent, and negative
    adjacency tests; and
20. request explicit authorization before commit.

No phase may be skipped by adding a temporary fallback that remains capable of
publishing controller rows.

## 23. Prohibited Shortcuts

The implementation may not:

- retain separate route and endpoint polygon catalogs as co-authorities;
- hardcode KNGU, ORF, PCT, BWI, DCA, IAD, SCT, THAMES, CYVR, Madrid, or another
  facility/controller pair;
- deduplicate valid geometry by service key alone;
- use an unordered first match as canonical identity;
- treat missing suffix as two independently parsed geometries;
- let AFV range, route proximity, airport proximity, overlap, adjacency,
  callsign similarity, or ATIS text light a polygon;
- build vNAS ownership only after radio-board candidate filtering;
- treat a configured position frequency as the pilot's live contact frequency;
- silently describe a partially downloaded region as complete;
- merge contradictory sources without a named conflict/precedence result;
- let route scoping decide whether a controller owns a polygon;
- keep compatibility survivors capable of feeding production rows;
- add a rollback flag that restores any retired path;
- let the UI, Standby Assist, or a resolver recompute ownership;
- scrape another map's rendered output as production data;
- perform global polygon scans on the simulator callback or UI thread; or
- install, commit, push, package, or release before the applicable approvals.

## 24. Static Ambiguity Audit Required for Closure

The implementation closeout must preserve exact search output proving:

- one production source compiler for authority polygons;
- one production source acquisition/publication owner;
- zero hardcoded secondary terminal-boundary downloads;
- zero production `TerminalAuthorityResolver` ownership decisions;
- zero candidate-scoped vNAS ownership dependency on radio-board admission;
- zero transceiver- or ATIS-created polygon activations;
- zero first-key polygon geometry deduplication;
- zero independent point-in-polygon implementations outside the shared geometry
  component;
- zero production compatibility-survivor authority;
- zero harness-only parallel board implementations;
- every displayed Center/APP/DEP row carries canonical map generation,
  polygon ID, position ID, callsign, frequency, activation proof, and final
  Brain decision; and
- Standby Assist consumes only final immutable boards.

Any retained legacy type must have no producer capable of changing live state,
no production consumer, a removal owner, and a regression proving it cannot
affect output.

## 25. Offline Exit Criteria

The offline gate passes only when all of the following are true:

- all mandatory red proofs were preserved before behavior changed;
- parser reconciliation accounts for every input feature/component;
- the audited duplicate-key examples retain all geometry;
- the canonical global catalog contains all accepted Center and terminal
  polygons independent of route/endpoint scope;
- one live activation model attaches exact callsign/frequency/position proof;
- vNAS enrichment is independent of radio-board candidate admission;
- aircraft, route, departure, and arrival queries use the same generation;
- diagnostic GeoJSON accurately renders offline, active, conflicted, stale,
  and unmapped state;
- the complete focused regression matrix is green;
- the environment-independent full suite is green with every exclusion named;
- the Release build is green;
- the static ambiguity audit is green;
- no previous Center-frequency click or Standby Assist behavior regresses; and
- no live install or commit has occurred without separate authorization.

## 26. Live Exit Criteria

After separate live-install authorization, controlled proof must demonstrate:

- Center polygons light and hand off according to exact live assignments;
- direct APP/DEP polygons light with the roster callsign and live frequency;
- a split terminal configuration lights only its assigned child polygons;
- a combined/parent configuration lights every and only explicitly assumed
  polygon;
- KNGU does not acquire PCT without direct or explicit parent ownership;
- an airport truly served by PCT still receives the correct controller;
- source loss, stale ownership, conflict, and unmapped controllers fail closed
  with visible diagnostics;
- UI rows, COM tuning, and Standby Assist use the same canonical receipts;
- the diagnostic polygon projection agrees with the board for the same map
  generation; and
- performance remains inside the live release gates.

An external map can be observed as a sanity comparison, but closure is based on
our source receipts, canonical geometry, live controller facts, and deterministic
tests.

## 27. Required Closeout Artifacts

The implementation closeout must preserve:

- this audit and approved gate;
- current-source and implementation-source reconciliation receipts;
- all mandatory red outputs;
- focused green outputs;
- full-suite output and exclusions;
- Release build output;
- static ambiguity search output;
- legacy-path final-disposition table;
- source-health and conflict receipts;
- representative controller-to-polygon activation receipts;
- departure, arrival, aircraft, and route query receipts;
- diagnostic GeoJSON and reviewed renders;
- performance/memory/threading report;
- exact built and installed hashes if live proof is authorized;
- live logs/screenshots; and
- Product Owner authorization to commit.

## 28. Definition of Done

This gate is complete only when:

1. one canonical catalog contains every accepted Center and APP/DEP polygon;
2. every input geometry is accepted, explicitly merged, or rejected with a
   typed data gap;
3. canonical polygon IDs are unique and topology is preserved;
4. one Brain-owned live map attaches exact controllers and live frequencies to
   exact owned polygons;
5. current split, combined, bandbox, and parent ownership enters through an
   explicit source graph;
6. radio, route, airport, adjacency, and text clues cannot light polygons;
7. route and endpoint selection are queries over the same map generation;
8. every displayed Center/APP/DEP row traces to one active canonical assignment
   and one final Brain decision;
9. the duplicate terminal resolver, candidate-scoped vNAS ownership, inference
   activators, compatibility survivors, and parallel harness authority paths
   are removed;
10. complete map health and live activation can be inspected through
    deterministic receipts and diagnostic geometry;
11. all offline tests, static audits, performance gates, and the Release build
    pass;
12. separately authorized live proof passes; and
13. the Product Owner explicitly authorizes commit.

Until every item is proven, the gate remains open.

## 29. Approval Record

**Product Owner direction:** One complete polygon map with correctly lit live
controllers is required.  
**Contract approved:** 2026-10-25.  
**Implementation authorized:** Yes, 2026-10-25.  
**Rollback checkpoint:** Preserved and hash-verified before implementation.  
**Live installation authorized:** No.  
**Commit authorized:** No.

## 30. Offline Implementation Receipt

Offline implementation passed on 2026-10-25. The Release plugin and regression
harness built successfully; the environment-independent suite passed 925 of
925 scenarios with one named machine-specific installed-xPilot path test
excluded. Focused KNGU/PCT, EGLL/THAMES, topology, duplicate-key, vNAS,
conflict, unmapped-controller, Center-selection, and Standby Assist proofs are
green. Performance and runtime activation gates are green.

The duplicate terminal resolver and parallel harness board engines are deleted.
The endpoint geometry seam is now `BrainEndpointPolygonQuery*`, which cannot
establish ownership. The sole APP/DEP authority admission path joins an active
canonical live-map assignment to endpoint containment by the same canonical
polygon ID.

The closeout package is preserved under:

`outputs/canonical_live_authority_polygon_map_01`

No live install or commit was performed. This contract remains open until the
separately authorized live exit criteria pass and the Product Owner authorizes
commit.

