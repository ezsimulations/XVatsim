# V2 Initial Flight-Plan Performance Audit

## Conclusion

`INITIAL-PLAN WORKLOAD ACCEPTED AS CURRENT BASELINE — NO SAFE NARROW CHANGE PROPOSED`

This was a read-only audit. No route, authority, controller-relevance, cache,
or scheduling production source was changed.

## Observed live baseline

The protected live diagnostic recorded one KDFW-KSAN establishment cycle:

- complete flight-loop wall/CPU observation: 1,544,330 microseconds;
- route resolution/traversal: 1,063,700 microseconds;
- authority relevance: 478,990 microseconds;
- combined route/authority: 1,542,690 microseconds;
- subsequent stable cycles: approximately 0.8 milliseconds.

The measured stages are synchronous and sequential in the current flight-loop
call graph. The diagnostic does not expose finer CPU-versus-wall subdivisions
inside either stage, so those internal costs cannot be assigned more precisely
without new instrumentation outside this correction.

## Findings

1. The preflight cache was unavailable because the deployed three-file payload
   did not contain the optional cache file beside `XVatsim.xpl`. Startup logged
   `Preflight route cache not found`; no otherwise valid cache was rejected.
2. Absence is an expected supported condition. The resolver falls back first to
   an expanded FMS route cache and then to normal route resolution.
3. The accepted route used `EXPANDED_FMS:KDFWKSAN01.fms` and already supplied 16
   immutable waypoints. It avoided textual airway/procedure resolution, but it
   did not avoid exact sector traversal, authority-key population, or authority
   relevance construction.
4. Source inspection identifies exact traversal as the dominant credible route
   cost: for every route segment it iterates the sector features, checks polygon
   boundary intersections, constructs current/next sector matches, collapses
   authority identities, and populates controller patterns. The live diagnostic
   does not isolate these subfunctions, so this remains source-backed attribution,
   not a separately measured substage.
5. The authority stage begins with a cold scope/cache build, creates route-scoped
   authority catalogs and indexes, computes signatures and proof evidence,
   evaluates controller mappings/activation, filters polygons, and builds the
   brain-owned authority result. Its individual subcosts are likewise not
   separately timed.
6. The dominant classes are geometry traversal/intersection and cold authority
   catalog/index/proof construction, not METAR, network wait, or repeated data
   loading on every cycle.
7. Route output is an input to authority relevance, so the two accepted results
   are semantically sequential. Independent source loading may be parallelizable
   in another design, but the current proof cannot safely separate result
   production.
8. Plugin path discovery, X-Plane flight-plan/FMS sampling, aircraft state, and
   lifecycle access are simulator-thread-bound. The resulting aircraft,
   network-plan, controller, transceiver, route waypoint, boundary, and authority
   snapshots are immutable enough for worker input, but publication would need
   generation, cancellation, staleness, and commit ownership.
9. The expanded FMS waypoint vector is already reused. The remaining exact
   traversal and authority result depend on live route/source generations and
   aircraft/controller state.
10. No duplicate computation with an independently proven removable cost was
    found. A spatial index for sector edges or persistent preflight cache could
    reduce work, but either changes cache/data correctness boundaries and needs
    dedicated proof.
11. Such changes could alter route sector ordering, entry distances, controller
    matching, authority scope, or phase-visible results unless held to a much
    broader parity suite.
12. Moving the work off-thread requires a route/authority generation,
    cancellation, lifecycle, stale-completion, and brain-commit architecture.
    That is a broad redesign, not a narrow correction.

The existing cache-hit behavior proves the cost is concentrated in initial
establishment rather than recurring unchanged loops. Under the Director's
locked boundary, no performance modification is proposed.
