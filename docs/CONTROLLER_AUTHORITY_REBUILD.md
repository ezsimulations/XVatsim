# Controller Authority Rebuild

This is the active rebuild track for making XVatsim controller display work like
map-driven VATSIM tools: a live controller activates a known authority polygon,
then the UI displays that controller only when aircraft or route geometry proves
relevance.

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

## Not Done Yet

- The live ENROUTE/DEPARTURE/ARRIVAL modules are not switched to the new
  authority engine yet.
- TRACON terminal polygon records compile, but terminal controller activation
  rules are not implemented yet.
- VATGlasses/VATSIM Radar-style extension rules are not implemented yet.
- Live ENROUTE/DEPARTURE/ARRIVAL modules do not consume relevant active
  polygons yet.
- Old live authority seams still exist and must be removed after replacement.
