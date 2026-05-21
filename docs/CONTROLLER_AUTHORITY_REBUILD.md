# Controller Authority Rebuild

This is the active rebuild track for making XVatsim controller display work like
map-driven VATSIM tools: a live controller activates a known authority polygon,
then the UI displays that controller only when aircraft or route geometry proves
relevance.

## Architecture Contract

All future authority/controller work must follow
`docs/AUTHORITY_EVIDENCE_CONTRACT.md`. That document is the checkpoint for
preventing shortcut fixes, vague proof labels, one-off callsign patches, and
logic that cannot be defended by source-backed evidence plus route geometry.

## Non-Negotiable Rule

No displayed controller may come from guessed callsign logic, nearest-plausible
authority, route-token shape, or prefix inheritance. Every displayed controller
must trace to:

- live VATSIM callsign and facility truth
- compiled controller-authority source data
- activated authority polygon
- aircraft or route geometry intersection

If any link is missing, the controller is unmapped and the harness must expose
the data gap.

## Rebuild Sequence

1. Build the controller authority compiler in isolation.
2. Compile VATSpy FIR/UIR authority rows into authority records and activation
   patterns.
3. Compile TRACON/terminal authority rows into the same catalog model.
4. Add explicit extension/ownership rules from VATSIM Radar/VATGlasses-style
   data sources.
5. Activate authority polygons from the live VATSIM controller feed.
6. Resolve active authorities against aircraft and route geometry.
7. Move ENROUTE to consume active authority polygons instead of sector-prefix
   matching.
8. Move DEPARTURE and ARRIVAL to consume terminal and center active authority
   polygons.
9. Remove the old authority-prefix and controller-prefix seams from live module
   decisions.
10. Expand harness coverage for real failures and simulated world coverage.

## Current Pass

Pass 1 added an isolated `core` authority compiler:

- `VATSpy` FIR/UIR rows compile into authority records.
- Non-empty VATSpy callsign prefixes compile explicit center/FSS activation
  patterns such as `HKG_CTR`, `HKG_*_CTR`, `HKG_FSS`, and `HKG_*_FSS`.
- Blank callsign-prefix rows remain data gaps and do not activate controllers.
- VATSIM facility truth is required for activation; a tower/ground facility
  cannot activate a center/FSS authority pattern.
- The regression harness can assert compiled authority IDs, data gaps, active
  matches, and unmapped live callsigns.

Pass 2 adds authoritative polygon compilation:

- VATSpy boundary-style source records compile into center authority polygons.
- SimAware TRACON-style source records compile into terminal authority polygons.
- Polygon lookup keys are source-derived and normalized.
- Invalid or missing polygon rings become explicit data gaps.
- The regression harness can assert compiled polygon IDs, lookup keys, ring
  counts, and polygon data gaps.

Pass 3 adds active polygon activation:

- Live controller callsign plus VATSIM facility truth resolves to controller
  authority.
- Controller authority joins to compiled authority polygons through explicit
  polygon lookup keys.
- A controller can activate a polygon only when both the authority record and
  polygon record exist.
- Missing polygon records become `missing-authority-polygon` data gaps.
- The regression harness can assert active authority polygon matches and active
  polygon data gaps.

Pass 4 adds geometry relevance:

- Active polygons are relevant only when the aircraft is inside them or the
  planned route intersects them.
- Route relevance uses exact route-leg versus polygon boundary entry logic with
  anti-meridian-safe longitude handling.
- Online-but-irrelevant active polygons remain out of relevance results.
- The regression harness can assert aircraft-inside, route-intersection, and
  ignored non-intersecting active polygons.

Pass 5 adds a controlled ENROUTE handoff seam:

- ENROUTE can consume a fresh `AuthorityRelevanceSnapshot` of active,
  geometry-relevant authority polygons.
- When that snapshot is available and fresh, ENROUTE does not fall back to the
  old sector-prefix matching path.
- Irrelevant active polygons produce no ENROUTE display instead of being
  rescued by a guessed prefix match.
- The regression harness can assert that a relevant active polygon displays and
  an irrelevant active polygon fails closed.

Pass 6 starts explicit position ownership ingestion:

- Source records can now map controller position IDs/callsign patterns directly
  to authority polygons.
- Explicit position records do not generate suffix guesses; only the
  source-provided callsign patterns can activate their polygon.
- The harness proves `HKG_W_CTR` can activate Hong Kong from a VATGlasses-style
  position record, while an unsourced `HKG_W_CTR` does not match an `HKG_E_CTR`
  source record.
- This is the catalog direction needed for VATGlasses/VATSIM Radar-style
  position ownership data.

Pass 7 starts the source-data ingestion layer:

- The core now parses VATSIM's map-data manifest into a typed source manifest:
  commit hash, VATSpy.dat URL, FIR boundaries GeoJSON URL, and FIR boundaries
  DAT URL.
- The route-sector resolver now asks the official manifest for current VATSpy
  source URLs before falling back to legacy hardcoded URLs.
- The harness proves the manifest parser accepts complete HTTPS source records
  and rejects incomplete/untrusted URL sets.

Pass 8 adds supplemental source ingestion:

- SimAware TRACON is now carried in the same source manifest path as VATSpy
  instead of using a separate hardcoded downloader.
- The source manifest can carry an optional VATGlasses/ownership URL. It is not
  required for baseline validity, but if supplied it must be HTTPS.
- VATGlasses-style ownership JSON can now ingest explicit position records into
  `AuthorityPositionSourceRecord` entries, so source-owned callsign patterns can
  supplement VATSpy without being manually invented.
- The harness proves supplemental-source defaults and VATGlasses-style position
  ingestion.

Pass 9 wires live ownership into the route-sector source cache:

- The optional VATGlasses/ownership URL is now downloaded with the same source
  package as VATSpy and SimAware.
- Ownership payloads are cached beside VATSpy.dat and participate in the live
  authority catalog cache key.
- Parsed ownership positions supplement VATSpy controller patterns for matching
  route sectors instead of replacing VATSpy baseline coverage.
- The harness proves ownership JSON can fill a VATSpy blank-prefix authority
  gap for a route-sector snapshot.

Pass 10 wires live ENROUTE to authority relevance:

- The live plugin now builds a resolver-owned `AuthorityRelevanceSnapshot` from
  compiled controller authority, compiled polygons, live controller feed truth,
  aircraft position, and route geometry.
- ENROUTE consumes that authority relevance snapshot instead of independently
  interpreting route-sector controller prefixes in the live path.
- If the authority relevance snapshot is present but stale or unavailable,
  ENROUTE fails closed instead of falling back to legacy route-sector matching.
- Departure APP/DEP display also fails closed without terminal-sector authority,
  removing the old airport-prefix fallback for terminal airspace controllers.

Pass 11 adds authority source-parity diagnostics:

- Resolver-built authority relevance snapshots now carry a status line and
  deterministic diagnostics for live controller authority coverage.
- Unmapped live controllers, missing authority polygons, and active but
  route-irrelevant polygons are exposed as explicit diagnostics instead of
  requiring a flight to reveal the gap.
- The harness can assert those diagnostics so real-world failures can become
  saved source-parity tests.

Pass 12 adds frequency-owned VATGlasses authority matching:

- VATGlasses static `positions` objects now compile published position
  prefixes, types, and frequencies into authority records.
- VATGlasses static `airspace` objects now compile owner-linked polygons from
  published DMS coordinate rings.
- When a published position frequency exists, live controller activation must
  match that frequency; arbitrary suffix text no longer decides the position.
- Frequency-owned source matches take precedence over broad VATSpy wildcard
  matches for the same live controller, so exact ownership wins over fallback
  FIR coverage.
- The harness proves `HKG_W_CTR` maps through the published `TRW` Hong Kong
  Radar frequency and exact owner polygon, and rejects the same callsign on the
  wrong frequency.

Pass 13 adds dynamic VATGlasses ownership ingestion:

- Combined dynamic VATGlasses payloads can now carry `positions`, `airspace`,
  and `ownership` data in the same source package.
- Dynamic `ownership.airspace` maps position IDs to the airspace sectors they
  are allowed to own, so position frequencies activate published sector
  polygons instead of broad FIR fallback polygons.
- Dynamic `airspace` object records now compile published DMS coordinate rings
  into exact authority polygons.
- If a source frequency-owned position pattern matches a live callsign/facility
  but the live frequency is wrong, broad VATSpy wildcard fallback is blocked and
  the resolver fails closed with an explicit unmapped-controller diagnostic.
- The harness proves a dynamic source-owned controller lights the exact
  published polygon and proves the wrong frequency cannot be rescued by broad
  fallback coverage.

Pass 14 adds transceiver geography proof and production source packaging:

- The transceiver resolver now exposes a non-UI authority-station snapshot with
  live controller transceiver frequency and position data. This is separate
  from the overlay's "currently receivable" range display.
- Resolver-built authority relevance can now reject source-owned
  frequency-matched polygons when fresh transceiver evidence proves the live
  station is geographically incompatible with the claimed polygon.
- This guard is intentionally conservative: stale or missing transceiver data
  does not hide otherwise proven route/polygon/frequency matches, but fresh
  contradictory geography fails closed.
- The map-data manifest can now provide a VATGlasses dynamic directory or
  explicit dynamic file URLs. The downloader combines `positions.json`,
  `airspace.json`, and `ownership/*.json` into the parser-ready
  `positions + airspace + ownership` payload automatically.
- The harness proves far-away same-frequency transceiver evidence blocks a
  source-owned authority match and proves dynamic VATGlasses source-package
  combination without a live flight.

Pass 15 adds the authority evidence decision layer:

- Controller authority evaluation now builds typed `AuthorityEvidence` records
  before producing accepted `ActiveControllerAuthority` matches.
- `AuthorityDecision` owns the accept/reject result, preserving callsign,
  facility, frequency, published ownership, proof source, proof detail, and
  rejection reasons.
- Live ENROUTE authority relevance now evaluates route-scoped rejected
  decisions instead of silently skipping them when final activation fails.
- Frequency mismatch, facility mismatch, and missing source ownership are now
  explicit diagnostics for route-relevant controllers.
- Broad VATSpy fallback remains blocked when stronger frequency-owned
  ownership evidence exists but the live controller fails that source proof.
- The harness now asserts both successful proof sources and false-positive
  rejection diagnostics.

Pass 16 adds a typed special-sector source lane:

- `SPECIAL_SECTOR_DATA` is now a real `AuthoritySource` instead of being
  hidden behind VATSpy, VATGlasses, or a generic extension label.
- Special-sector payloads must explicitly declare their source before they are
  parsed as special sector data, preventing accidental relabeling of normal
  VATGlasses ownership.
- Special-sector position, frequency, ownership, and polygon records compile
  through the same evidence engine as other authority sources.
- Special-sector source-owned polygons participate in route relevance and
  transceiver-geometry compatibility checks.
- The harness proves a special-sector source-owned controller can feed ENROUTE
  with proof source `SPECIAL_SECTOR_DATA`, published frequency evidence, and
  route relevance proof.

Pass 17 adds duplicated ATIS-derived authority and stricter rejection proof:

- Live controller `text_atis` is now part of the controller snapshot identity
  used by the plugin and route authority resolver.
- `DUPLICATED_ATIS_DERIVED` is now emitted only when ATIS text contains a
  coverage/combined-position phrase naming an already source-owned authority
  position whose polygon is route-relevant.
- The duplicated-ATIS path records facility type, covered position text,
  source-owned authority, and route-relevant polygon proof items.
- Non-center/FSS duplicated-ATIS candidates are rejected with an explicit
  `facility-mismatch` diagnostic.
- Previously silent rejected authority candidates now produce contract
  diagnostics: `unmapped-controller`, `active-not-relevant`, or
  `transceiver-geo-mismatch` as appropriate.
- The harness proves duplicated ATIS success, wrong-facility rejection,
  unmapped-controller reporting, active-not-relevant reporting, and remote
  transceiver geography rejection.

## Not Done Yet

- TRACON terminal polygon records compile, but terminal controller activation
  rules are not implemented yet.
- VATSIM Radar-style extension rules beyond explicit/static ownership position
  records are not implemented yet.
- DEPARTURE/ARRIVAL modules do not consume relevant active polygons yet.
- Automatic live download/compilation of full special-sector datasets, such as
  vatSys AU/NZ, is not implemented yet.
- Old live authority seams still exist and must be removed after replacement.
- Source-data parity against VATSIM Radar/VATSpy/Navigraph-style coverage still
  requires broader imported dynamic ownership/extension datasets and parity
  reporting against known world/event controller splits.
