# Authority Evidence Contract

Status update 2026-05-19: preserved as the evidence/verifier contract, but no
longer the primary live discovery architecture. The primary runtime contract is
now `docs/RADIO_RANGE_AUTHORITY_GATE_CONTRACT.md`. Authority evidence may
verify radio-reachable or destination-targeted candidates; it must not drive
broad live-loop world scanning.

This document is the non-negotiable contract for XVatsim controller authority
work. It exists so future code changes can be compared against the agreed
architecture instead of drifting back into shortcuts.

## Goal

XVatsim should behave like a route-scoped radar authority engine:

1. Load authoritative polygon and ownership source data.
2. Plot the filed route through the internal polygon map.
3. Identify only the authority polygons relevant to that route or aircraft.
4. Match live VATSIM controllers against those polygons using source-backed
   evidence.
5. Display only controllers whose authority can be proven.
6. Log exactly why every displayed controller was accepted.

We do not need to display a public map, but internally we should reason the
same way mature map tools do: active controller plus known polygon plus source
ownership plus geometry relevance.

## Required Source Layers

The authority engine must preserve these source lanes as separate evidence
paths. They must not be collapsed into vague labels.

- `VATSPY_FIR`: baseline FIR/UIR authority and boundary coverage.
- `VATGLASSES_STATIC_OWNER`: source-owned static position, frequency, type, and
  airspace ownership.
- `VATGLASSES_DYNAMIC_OWNER`: live/dynamic VATGlasses ownership assignment.
- `TRANSCEIVER_GEO_ROUTE`: AFV/transceiver station location proves a controller
  geographically belongs to a route-relevant polygon.
- `FREQUENCY_OWNED_MATCH`: published frequency proves ownership when combined
  with source ownership, facility type, and route geometry.
- `SPECIAL_SECTOR_DATA`: source-backed special sector datasets, such as vatSys
  AU/NZ sector data, when implemented.
- `DUPLICATED_ATIS_DERIVED`: explicit source-backed duplicated coverage from
  ATIS/sector text rules, when implemented.

Do not emit a proof source unless the real implementation path exists.

## Decision Model

The resolver should collect evidence first, then decide. It should not make a
single early string decision and skip the remaining evidence.

Each candidate live controller should be evaluated against route-relevant
authority polygons using these proof items:

- Live VATSIM callsign.
- Live VATSIM facility type.
- Live VATSIM controller frequency.
- Published/source-owned controller frequency.
- Published/source-owned position ID.
- Published/source-owned callsign pattern.
- Active dynamic ownership assignment.
- Authority polygon ID and source.
- Aircraft-inside or route-intersects geometry proof.
- AFV/transceiver station position and frequency, when available.
- Source priority when multiple valid authority proofs exist.

Acceptance should be evidence based. Rejection should explain the missing or
contradictory evidence.

## Source Priority

When multiple sources can explain the same live controller, prefer the strongest
source-backed ownership:

1. VATGlasses dynamic owner with matching frequency/type/geometry.
2. VATGlasses static owner with matching frequency/type/geometry.
3. Special sector dataset with matching frequency/type/geometry.
4. Transceiver geography proof for route-relevant center/FSS authority.
5. VATSpy FIR/UIR baseline pattern match.
6. Explicit duplicated/covered-position rule, only if source-backed and tested.

Broad VATSpy wildcard coverage is a fallback, not the final word. Stronger
frequency-owned or dynamic ownership evidence should take precedence.

## No-Shortcut Rules

- Do not add random string aliases to fix a single controller failure.
- Do not let callsign text alone be the only pass/fail gate.
- Do not let frequency alone light a polygon.
- Do not let a broad VATSpy wildcard override a stronger source-owned frequency
  match.
- Do not silently rescue stale or contradictory state with guessed logic.
- Do not label a controller with a proof source that was not actually used.
- Do not collapse accepted controllers back into generic `CATALOG_PATTERN` or
  `CATALOG_EXACT` when stronger proof exists.
- Do not accept a controller unless the relevant polygon is route-intersecting
  or aircraft-containing.
- Do not merge authority logic without a regression scenario for the success
  path and the nearest realistic rejection path.

## Required Diagnostics

Every displayed authority controller must expose:

- Callsign.
- Frequency.
- Authority ID.
- Polygon ID.
- Polygon source.
- Proof source.
- Proof detail.
- Route relevance reason: aircraft inside, route intersects, or both.

Every rejected or suspicious candidate should expose one of:

- `unmapped-controller`
- `missing-authority-polygon`
- `active-not-relevant`
- `frequency-mismatch`
- `facility-mismatch`
- `transceiver-geo-mismatch`
- `missing-source-ownership`
- `stale-source-data`

Diagnostics are part of the product, not temporary debug noise. They are how we
prove the plugin is trustworthy.

## Definition Of Done For Future Authority Changes

A controller-authority change is not complete until all items below are true:

- The implementation uses source-backed evidence, not a one-off callsign patch.
- The proof source survives from authority activation into
  `RelevantAuthoritySnapshot`.
- The diagnostics explain both accepted and rejected candidate controllers.
- The change is route-scoped and does not increase per-frame world scanning.
- The regression harness has at least one passing scenario for the intended
  behavior.
- The regression harness has at least one rejection scenario for the nearest
  false-positive risk.
- Existing saved scenarios still pass.
- The code comments or docs identify which source layer is being used.
- The final explanation says which proof source changed and why.

## Live Release Gate

Authority evidence can be functionally correct in the harness and still not be
release-ready. Store-readiness is governed by
`docs/LIVE_BATTLE_TEST_RELEASE_GATE_CONTRACT.md`.

Current release gates:

- `5` consecutive valid live battle-test passes with healthy performance before
  giving the plugin to the beta streamer/tester.
- `10` consecutive valid live battle-test passes with healthy performance
  before starting the final audit and store-release package.

Any controller-authority or performance-cadence code change should reset the
active live-test streak unless we explicitly classify the change as docs/logging
only.

## Current Implementation Checkpoint

As of 2026-05-17, XVatsim has these active pieces:

- VATSpy FIR/UIR baseline authority catalog.
- VATSpy boundary polygon compilation.
- VATGlasses static owner parsing.
- VATGlasses dynamic source package parsing.
- Frequency-owned VATGlasses matching.
- Route-scoped authority polygon relevance.
- Transceiver geography route rescue and contradiction guard.
- Proof-source logging through `RelevantAuthoritySnapshot`.
- Accepted proof-stack details including source, proof items, polygon, and
  route relevance.
- SimAware TRACON terminal boundary polygons compiled into typed
  `SIMAWARE_TRACON` authority records for APP/DEP terminal airspace.
- SimAware TRACON APP/DEP authority derives both exact callsign patterns
  (`PHX_APP`) and source-owned sectorized patterns (`PHX_*_APP`) from the
  published prefix plus facility suffix, so controllers such as `PHX_A_APP`
  are accepted through the same central evidence engine instead of one-off
  aliases.
- Departure and Arrival APP/DEP airspace rows consume accepted terminal
  authorities from `AuthorityRelevanceSnapshot` when available, and the
  resolver keeps source-owned terminal authorities alive until geometry
  relevance proves or rejects them.
- Airport-local DEL/GND/TWR rows now consume centrally activated
  `AIRPORT_LOCAL_FACILITY` authority evidence from `AuthorityRelevanceSnapshot`
  for route endpoint airports. Route-endpoint local authorities are generated
  only when a live local candidate matches a departure or arrival airport, then
  evaluated through the same source-backed decision path as other authorities.
  This proof path records airport, endpoint, callsign pattern, facility type,
  and route-endpoint relevance, supports delivery aliases such as `CLR`, and
  produces rejection diagnostics for wrong-facility local candidates.
- Typed `SPECIAL_SECTOR_DATA` ingestion when a payload explicitly declares that
  source.
- Manifest-declared `special_sector_data_url` and
  `special_sector_data_urls` payloads are now accepted only from trusted HTTPS
  URLs, downloaded with the live source package, and merged into a multi-source
  authority package without collapsing the `SPECIAL_SECTOR_DATA` proof lane
  into VATGlasses or VATSpy.
- Multiple supplemental `SPECIAL_SECTOR_DATA` packages can now be carried in
  one source package wrapper and selected by route relevance, with unrelated
  packages rejected instead of leaking into the displayed authority list.
- Manifest-declared `terminal_authority_data_url` and
  `terminal_authority_data_urls` payloads are accepted only from trusted HTTPS
  URLs, downloaded with the live source package, and parsed as
  `SIMAWARE_TRACON` terminal ownership evidence. Frequency-owned terminal
  package matches can now override broad SimAware APP/DEP terminal patterns,
  while wrong-frequency candidates are rejected before any broad terminal
  fallback can leak through.
- Manifest-declared `authority_source_registry_url` and
  `authority_source_registry_urls` payloads are accepted only from trusted
  HTTPS URLs. Registry entries must declare a recognized source lane
  (`VATGLASSES`, `SPECIAL_SECTOR_DATA`, or `TERMINAL_AUTHORITY`) and a trusted
  HTTPS data URL before the live downloader will add that payload to the
  source package. The harness locks this with real VATGlasses raw URLs for
  Vancouver (`zvr.json`) and Hong Kong/Macau (`vh-vm.json`), plus rejection
  coverage for untrusted or unknown registry entries.
- A pinned broader VATGlasses source registry now lives at
  `assets/source_data/authority_source_registry.json`, generated from
  `lennycolton/vatglasses-data` commit
  `a9fb0bb82d05e7bb12770fd90e6dca510754e0e5`. It selects all 139 top-level
  `data/*.json` packages and all 14 dynamic-directory packages that contain
  `positions.json`, `airspace.json`, and `ownership/default.json`. The registry
  parser, package expansion path, and live downloader support both shapes.
  On 2026-05-17, all 153 entries / 181 raw URLs were reachable, and content
  validation confirmed 139 static packages with `positions` + `airspace` and
  14 dynamic packages with parseable positions/airspace/ownership JSON.
- The broader registry now has a runtime activation policy. The packaged
  `authority_source_registry.json` is copied beside the plugin build and used
  as the global authority-source fallback when the live map-data manifest does
  not declare registry URLs. Registry expansion, source-package download, and
  authority cache warming run inside the async source fetcher instead of the
  X-Plane flight loop.
- Global source data is not treated as permission to do global live relevance
  work. Controller identity and source ownership still use the full evidence
  catalog so "known elsewhere" rejections remain safe, while expensive route
  geometry is scoped to route-relevant authority polygons. Family/child
  polygons such as `LECM-ALL` or `ZJSY-O` remain available for
  transceiver/geometry proof without letting broad VATSpy string matches
  outrank stronger transceiver evidence.
- The pinned VATGlasses registry is also the broader real-world terminal
  ownership source. Validation against commit
  `a9fb0bb82d05e7bb12770fd90e6dca510754e0e5` found typed terminal records
  across the selected packages, including `APP`, `DEP`, `TWR`, `GND`, and
  `DEL` positions. These records now stay typed as terminal authority after
  acceptance instead of inheriting generic polygon kind, so source-owned
  VATGlasses APP/DEP/TWR matches can feed terminal relevance without leaking
  into ENROUTE center display.
- Explicit duplicated/covered-position ATIS-derived proof for ENROUTE center
  authority when the ATIS text names a source-owned, route-relevant position.
- Rejected candidate diagnostics for unmapped controllers, active-but-not-route
  authorities, wrong facility duplicated-ATIS candidates, and remote
  transceiver geography candidates.
- Regression harness assertions for representative proof sources.
- Contract checkpoint harness: 193 saved scenarios passed on 2026-05-17 after
  adding airport-local central-evidence cases including delivery alias
  acceptance, source-owned sectorized TRACON APP/DEP acceptance plus
  wrong-facility rejection cases, multi-URL special-sector source package
  registry/wrapper tests, terminal-authority source package ingestion, and
  terminal frequency-owned acceptance plus wrong-frequency rejection cases.
  The current checkpoint also includes typed authority-source registry parsing,
  trusted registry URL validation, real VATGlasses URL acceptance, unknown or
  untrusted registry entry rejection, registry-selected package expansion,
  dynamic-directory VATGlasses registry support, and the selected 153-entry
  broader VATGlasses registry fixture. It also includes a route-scoped
  geometry activation test proving family/child polygons remain available for
  transceiver proof without becoming broad string-match authority.
  The current checkpoint adds VATGlasses real-world terminal ownership
  coverage for route-relevant APP/TWR acceptance, wrong-frequency rejection,
  and terminal-kind preservation so tower records cannot feed ENROUTE.
  The saved LPPT-to-DAAG battle test proves LPPT local/terminal authority,
  LPPC/Spanish/DAAA enroute authority, and DAAG local/terminal authority in
  the same route-scoped evidence snapshot.
- Final legacy board ownership audit is complete for the ENROUTE, DEPARTURE,
  and ARRIVAL display seams. ENROUTE now fails closed when the authority
  relevance snapshot is missing, unavailable, or stale, and no longer falls
  back to raw route-sector/controller-feed prefix matching. DEPARTURE and
  ARRIVAL local/airspace boards no longer scan the raw live controller feed for
  airport-prefix or terminal-sector string matches when central authority
  evidence is missing. CTAF/UNICOM lookup remains separate because it is airport
  advisory frequency support, not controller authority ownership.

Current contract status:

- The stated authority evidence contract is complete at this checkpoint.
- Future proof layers or source feeds must still follow this same contract:
  evidence producer first, central authority decision second, board display last,
  with both success and nearest false-positive rejection harness coverage.
