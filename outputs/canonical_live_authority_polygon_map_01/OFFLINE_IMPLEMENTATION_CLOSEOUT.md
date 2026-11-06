# Canonical live authority polygon map — offline closeout

**Gate:** `CANONICAL-LIVE-AUTHORITY-POLYGON-MAP-01`  
**Offline result:** PASS  
**Overall gate:** OPEN — controlled live proof and commit authorization remain  
**Date:** 2026-10-25

## Outcome

XVatsim now compiles Center and APP/DEP geometry into one immutable canonical
map, activates that map from the global live controller roster and explicit
dynamic ownership, and only then runs aircraft, route, departure, and arrival
queries. Route or endpoint proximity can select a view of the map; neither can
light a polygon.

The live error that motivated this gate is now represented correctly:
`PCT_APP 119.850` may light its explicitly owned Potomac polygons globally,
but the KNGU endpoint query rejects that assignment when the canonical polygon
does not contain KNGU. The rejection is `UNRELATED` with proof
`active-canonical-assignment-does-not-contain-endpoint`.

## Structural result

- `modules/authority_map` owns the sole compiler and immutable-generation
  store.
- Source feature/group identity, child position identity, service roles,
  polygon components, rings, holes, content identity, and source-health
  receipts are retained separately.
- Global activation precedes aircraft and route relevance.
- Live assignments carry canonical polygon ID, exact position ID, callsign,
  live frequency, state, proof source, and generation.
- Direct child, explicit parent, split/combined ownership, conflict, unmapped,
  stale, offline, and exact activation are typed states.
- vNAS ownership is collected globally and no longer depends on a radio
  candidate list.
- AFV/transceiver location, ATIS text, route proximity, endpoint proximity,
  and callsign similarity cannot light a polygon.
- The old duplicate terminal downloader/compiler and the harness-only arrival,
  departure, and enroute authority engines are deleted.
- The surviving endpoint seam is named `BrainEndpointPolygonQuery*`. It returns
  geometry facts; it is not an authority producer.
- APP/DEP admission has one join: a matching active live-map assignment and
  endpoint containment under the same canonical polygon ID.
- Center-only UI selection and the Standby Assist hierarchy are preserved.

## Proof summary

The Release harness and plugin build passed. The environment-independent suite
passed `925/925`; one machine-specific installed-xPilot path fingerprint test
was explicitly excluded. Focused KNGU/PCT, EGLL/THAMES, topology-hole,
duplicate-key, source-conflict, unmapped-controller, vNAS independence,
Center-click, and Standby Assist tests passed.

Conflict, unmapped, stale-feed, active-parent, and offline states are all
asserted directly in the generated diagnostic GeoJSON.

Performance and lifecycle gates passed with no new thread, timer, or callback
growth. The representative 512-controller performance run completed in
3.773 ms, 10,000 decision iterations in 7.037 ms, and retained 8 KiB of
additional working set during that probe.

See:

- `green_proofs/offline_test_receipt.txt`
- `static_ambiguity_audit.txt`
- `legacy_path_disposition.md`
- `diagnostic_geojson/representative_parent_offline.geojson`
- `diagnostic_geojson/state_receipts.txt`
- `rollback/ROLLBACK_PLAN.md`

## Diagnostic proof

The representative GeoJSON is generated from the already-decided canonical
snapshot. It does not recompute ownership. It shows:

- `PCT_APP 119.850` as `ACTIVE_PARENT` on canonical polygon
  `SIMAWARE_TRACON:DCA_APP`, with vNAS ownership proof;
- a Center polygon in `OFFLINE` state;
- source health and reconciliation counts;
- stable map identity and generation fields; and
- no conflict or unmapped entries for that fixture.

Separate passing fixtures prove a conflicting polygon publishes `CONFLICT` and
no controller row, and an unmapped online controller is recorded diagnostically
while lighting nothing.

## Rollback and authorization

The verified pre-implementation rollback checkpoint remains intact under
`rollback/`. Rollback is source recovery into a separate tree; no runtime flag
can restore retired authority paths.

No plugin was installed. No commit or push was made. The next permitted step is
a separately authorized controlled live proof. Commit remains subject to
explicit Product Owner approval after that proof.

