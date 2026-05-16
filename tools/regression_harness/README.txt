XVatsim Regression Harness
==========================

Purpose
-------
This harness replays core XVatsim logic without loading X-Plane.
It currently exercises the shared workflow engine used by the live plugin plus the
deterministic replay seams we are extracting behind it:

- departure/enroute/arrival stage transitions
- departure terminal hold/release behavior
- display-board composition for departure, enroute, and arrival
- real ENROUTE controller matching
- synthetic route-sector traversal against explicit polygons
- route token grammar classification
- route text to waypoint/nav-graph resolution

Usage
-----
Executable:

  C:\Users\DARRON\OneDrive\Documents\XVatsim\build\tools\XVatsimRegressionHarness.exe

Run with a scenario file:

  XVatsimRegressionHarness.exe <scenario-file>

Example:

  XVatsimRegressionHarness.exe tools\regression_harness\scenarios\kont_departure_release.scn

Owner route scenario builder
----------------------------
For SimBrief/X-Plane `.fms` files, use the helper workflow in:

  tools\user_route_scenarios

The helper reads an `.fms` route, combines it with a simple controller situation
file, creates a regression `.scn`, and runs it immediately.

Double-click:

  tools\user_route_scenarios\Create_XVatsim_Scenario_From_FMS.bat

Or run directly:

  powershell -NoProfile -ExecutionPolicy Bypass -File tools\user_route_scenarios\New-FmsHarnessScenario.ps1 -FmsPath "C:\X-Plane 12\Output\FMS plans\KORDKSDF01.fms"

The first run creates a situation template to edit. The second run generates the
scenario under `tools\regression_harness\scenarios` so it can become a permanent
learning case.

Scenario format
---------------
The format is a simple key=value text file. Blank lines and lines starting with # are ignored.

Common keys:

- name=KONT Departure Release
- now_seconds=1000
- state.flight_active=true
- state.departure_released=false
- state.arrival_awake=false
- state.airborne_since_seconds=700
- flight.departure_icao=KONT
- flight.destination_icao=KPHL
- aircraft.valid=true
- aircraft.on_ground=false
- aircraft.latitude_deg=34.35
- aircraft.longitude_deg=-118.40
- radio.com1_active=122.800
- departure.terminal_coverage_known=true
- departure.inside_terminal_coverage=false
- departure.coverage.stale=false
- arrival.coverage.stale=false

Station lines:

- departure.station=role=Approach;callsign=SCT_APP;frequency=128.050;online=true;offline=false
- enroute.station=role=Center;callsign=LAX_CTR;frequency=125.800;online=true;offline=false;sectorActive=true;routeEntryDistanceNm=0

Expectations:

- expect.stage=Enroute
- expect.reason=departure-released
- expect.departure_location_confirmed=true
- expect.display_source=Enroute
- expect.display_callsigns=LAX_CTR,PHL_CTR

Replay support
--------------
The harness can now also replay the real ENROUTE controller matcher by providing route sectors
and controller feed entries directly.

Route sectors:

- route.current_sector=identifier=KZAK;entryDistanceNm=0;matchTokens=KZAK;controllerPrefixes=ZAK,OO,OOR
- route.next_sector=identifier=PHZH;entryDistanceNm=1399;matchTokens=PHZH;controllerPrefixes=HCF,HNL
- route.stale=false

Controller feed:

- xpilot.connected=true
- controller.feed_available=true
- controller.feed_stale=false
- controller.feed_force_entries=false
- controller.entry=callsign=HCF_CTR;frequency=127.650;facility=6;actionable=true;atis=false

ENROUTE expectations:

- expect.enroute_available=true
- expect.enroute_callsigns=KZAK,HCF_CTR

Controller authority compiler replay:

- authority_catalog.fir=VHHK|Hong Kong|HKG|VHHK
- authority_catalog.uir=EXAMPLE|Example Upper|EXM|EXAMPLE
- authority_polygon.vatspy=key=VHHK;name=Hong Kong;tokens=VHHK,HKG;polygon=20,112|24,112|24,116|20,116
- authority_polygon.tracon=id=SCT;name=SoCal TRACON;suffix=APP;prefixes=SCT,SOCAL;polygon=32,-119|35,-119|35,-116|32,-116
- controller.entry=callsign=HKG_W_CTR;frequency=125.320;facility=6;actionable=true;atis=false
- expect.authority_catalog_ids=VATSPY_FIR:VHHK
- expect.authority_data_gaps=<none>
- expect.authority_active_matches=HKG_W_CTR:VATSPY_FIR:VHHK:VHHK:HKG_*_CTR
- expect.authority_unmapped_callsigns=<none>
- expect.authority_polygon_ids=VATSPY_BOUNDARY:VHHK,SIMAWARE_TRACON:SCT_APP
- expect.authority_polygon_lookup_keys=VATSPY_BOUNDARY:VHHK:HKG>VHHK,SIMAWARE_TRACON:SCT_APP:SCT>SCT_APP>SOCAL>SOCAL_APP
- expect.authority_polygon_ring_counts=VATSPY_BOUNDARY:VHHK:1,SIMAWARE_TRACON:SCT_APP:1
- expect.authority_polygon_data_gaps=<none>

Overlay/RX presentation replay:

- overlay.stage=Departure
- transceiver.available=true
- transceiver.stale=true
- transceiver.status=RX feed stale
- transceiver.candidate=callsign=SCT_APP;frequency=128.050;distanceNm=12;score=50
- expect.overlay_body_lines=xPilot connected|RX feed stale

Synthetic route traversal replay:

- route.waypoint=ident=ACFT;lat=0;lon=0
- route.waypoint=ident=FIX1;lat=0;lon=10
- traversal.mode=exact
- feature.entry=label=SECTOR_A;tokens=SECTOR_A;controllerPrefixes=AAA;polygon=-5,-5|5,-5|5,5|-5,5
- traversal.route_sample_step_nm=5
- expect.route_resolved=true
- expect.route_current_sectors=SECTOR_A
- expect.route_next_sectors=SECTOR_B,SECTOR_C

Token grammar replay:

- flight.route_text=NUBLE4 NELIE Q75 MXE CLIPR3 58N140W DCT KK45A/N0489F300
- procedure.entry=type=SID;source=departure;name=NUBLE4;transition=NELIE
- procedure.entry=type=STAR;source=arrival;name=CLIPR3;transition=MXE
- expect.route_token_kinds=NUBLE4:Procedure,NELIE:Point,Q75:Airway,MXE:Point,CLIPR3:Procedure,58N140W:Coordinate,DCT:Control,KK45A:Point

Route text to waypoint/nav-graph resolution replay:

- plan.route_text=NUBLE4 NELIE Q75 MXE CLIPR3
 - procedure.entry=type=SID;source=departure;name=NUBLE4;transition=NELIE
 - procedure.entry=type=STAR;source=arrival;name=CLIPR3;transition=MXE
- graph.node=ident=NELIE;region=K1;type=11;lat=43.2000;lon=-70.5000
- graph.edge=startIdent=NELIE;startRegion=K1;startType=11;endIdent=GREKI;endRegion=K1;endType=11;airway=Q75;direction=N
- expect.resolved_waypoints=ACFT,NELIE,GREKI,BIZEX,MXE,KDCA
- expect.resolved_waypoint_points=ACFT@43.6462,-70.3093|NELIE@43.2000,-70.5000|GREKI@42.8000,-70.7000|BIZEX@42.1000,-71.3000|MXE@39.9272,-75.2411|KDCA@38.8512,-77.0402
- expect.resolved_tokens=NELIE,MXE
- expect.expanded_tokens=Q75
- expect.procedure_tokens=NUBLE4,CLIPR3
- expect.procedure_sources=DEP:NUBLE4,ARR:CLIPR3
- expect.procedure_records=SID:ENROUTE:NUBLE4,STAR:ENROUTE:CLIPR3
- expect.procedure_runways=<none>
- expect.procedure_authorities=SID:NUBLE4:PROC,STAR:CLIPR3:PROC
- expect.procedure_catalog_fixes=<none>
- expect.procedure_boundary_fixes=<none>
- expect.procedure_ordered_fixes=<none>
- expect.procedure_synthetic_waypoints=<none>
- expect.procedure_synthetic_sources=<none>
- expect.procedure_application_states=<none>
- expect.procedure_application_blocks=<none>
- expect.procedure_applied_fix_sequences=<none>
- expect.procedure_catalog_transitions=SID:NUBLE4:NELIE,STAR:CLIPR3:MXE
- expect.procedure_support=FORWARD:NUBLE4,BACKWARD:CLIPR3
- expect.procedure_links=SID:NUBLE4:NELIE,STAR:CLIPR3:MXE
- expect.procedure_misses=<none>
- expect.procedure_anchor_links=<none>
- expect.procedure_context_only=<none>
- expect.ignored_tokens=
- expect.unresolved_tokens=

Recognized procedure tokens are tracked through the dedicated procedure diagnostics above.
They are no longer reported as `ignored_tokens` unless some non-procedure structural token is
actually skipped.

Airport coverage replay:

- airport.coverage_builder_icao=KONT
- airport.coverage_builder_lat=34.0560
- airport.coverage_builder_lon=-117.6010
- airport.terminal_feature=id=SCT;name=SCT_APP;prefixes=SCT;polygon=33,-119|35,-119|35,-116|33,-116
- airport.pending_terminal_feature=id=SCT;name=SCT_APP;prefixes=SCT;polygon=34,-118|35,-118|35,-117|34,-117
- airport.coverage_builds_pre_refresh_snapshot=true
- airport.terminal_probe_lat=34.5
- airport.terminal_probe_lon=-117.5
- airport.terminal_probe_uses_pre_refresh_snapshot=true
- expect.airport_coverage_generations=center:1,authority:1,terminal:2
- expect.airport_terminal_inside=false

Terminal containment checks are generation-gated. A snapshot built from an older terminal
boundary payload must not prove containment against a newer payload even when the terminal
label is unchanged.
Center boundary and authority catalog snapshots are also generation-stamped so resolver and
plugin-level caches can detect source-data changes even when the visible sector labels are
unchanged.

Live route resolver replay:

- resolver.route_resolve=true
- resolver.route_builds_pre_refresh_snapshot=true
- resolver.center_feature=label=OLD;polygon=-1,-1|-1,25|1,25|1,-1
- resolver.authority_catalog_fir=OLD|Old Center|OLD|OLD
- resolver.pending_center_feature=label=NEW;polygon=-1,-1|-1,25|1,25|1,-1
- resolver.pending_authority_catalog_fir=NEW|New Center|NEW|NEW
- expect.resolver_route_current_sectors=NEW
- expect.resolver_route_current_controller_prefixes=NEW:NEW
- expect.resolver_route_generations=center:2,authority:2

This path exercises `RouteSectorResolver::Resolve` directly, including its internal route
snapshot cache, payload refresh seam, center polygon traversal, and authoritative controller
prefix population.

Included scenarios
------------------
- kont_departure_release.scn
  Replays the departure never-released bug path.

- departure_offline_terminal_tuned_does_not_hold.scn
  Proves an offline APP/DEP row tuned on COM1 cannot hold Departure after the release window.

- phzh_controller_match.scn
  Replays the Honolulu/PHZH controller-authority match path without requiring live ATC timing.

- synthetic_sector_chain.scn
  Replays route-sector traversal against explicit polygons and saved waypoints.

- route_token_grammar.scn
  Replays current route token classification for procedures, airways, coordinates, control tokens, and annotated fixes.
  Procedure tokens are only classified as `Procedure` when the scenario injects authoritative procedure metadata via `procedure.entry=type=...;name=...;transition=...`.
  The harness also tracks whether recognition came from runway-tagged procedure records, enroute transition records, both, or base metadata with `expect.procedure_records=...`.
  When runway-tagged records exist, the exact runway tokens are preserved with `expect.procedure_runways=...`.
  The harness also preserves which catalog authority supplied the procedure metadata with `expect.procedure_authorities=...`.
  When authoritative procedure fix membership exists, it is preserved with `expect.procedure_catalog_fixes=...`.
  When authoritative procedure boundary fixes can be proven, they are preserved with `expect.procedure_boundary_fixes=...`.
  When authoritative procedure fix order exists, it is preserved with `expect.procedure_ordered_fixes=...`.
  When the parser safely injects a procedure-derived waypoint into route resolution, it is preserved with `expect.procedure_synthetic_waypoints=...`.
  The harness also preserves whether that synthetic waypoint came from a unique transition or a boundary fix with `expect.procedure_synthetic_sources=...`.
  The harness also distinguishes between procedures that were only recognized and procedures that were actually applied to route construction with `expect.procedure_application_states=...`.
  When a procedure is recognized but not safely applied, the harness preserves the fail-closed reason with `expect.procedure_application_blocks=...`.
  When the parser applies an ordered multi-fix procedure segment, the exact applied sequence is preserved with `expect.procedure_applied_fix_sequences=...`.
  When transition metadata exists, the exact declared catalog transitions are preserved with `expect.procedure_catalog_transitions=...`.

- ambiguous_symbol_prefers_airway_context.scn
  Proves the typed grammar marks a dual-meaning token as ambiguous and that route context chooses the airway interpretation instead of guessing the point ident.

- route_waypoint_resolution.scn
  Replays synthetic airway-backed waypoint resolution and validates the resolved route chain and diagnostics.

- kpwm_kdca_q75_resolution.scn
  Replays the `NUBLE4 NELIE Q75 MXE CLIPR3` airway bug class offline, proves `Q75` expands through a saved graph instead of masquerading as a random point, and proves the filed SID/STAR are treated as real procedure metadata instead of unsupported text.

- procedure_transition_unmatched.scn
  Proves the parser distinguishes between a recognized procedure and a matched procedure transition by recording a stage-aware transition miss when the filed anchor does not match the authoritative metadata.

- procedure_source_both.scn
  Proves the parser records whether a recognized procedure came from departure metadata, arrival metadata, or both, and that merged source attribution survives into the saved diagnostics.

- procedure_support_both.scn
  Proves the parser can report a `BOTH:` support direction when a dual-role procedure has believable anchors on both sides.

- procedure_runway_record.scn
  Proves the parser distinguishes runway-tagged procedure records from enroute transition records and preserves that truth in saved diagnostics.

- procedure_sid_both_record.scn
  Proves the parser records `SID:BOTH:` when one procedure has both runway-tagged and enroute transition metadata, and that runway dependency still wins the fail-closed decision.

- procedure_star_both_record.scn
  Proves the parser records `STAR:BOTH:` when one procedure has both runway-tagged and enroute transition metadata, and that runway dependency still wins the fail-closed decision.

- procedure_anchor_without_transition.scn
  Proves the parser can still establish a stage-aware anchor relationship on the correct side even when no explicit transition metadata exists.

- procedure_context_only.scn
  Proves the parser distinguishes metadata-only procedure recognition from a procedure that also has an adjacent anchor on the correct side.

- procedure_false_anchor_rejected.scn
  Proves the parser no longer treats an arbitrary neighboring fix as anchor support when that fix is not actually part of the authoritative procedure metadata.

- procedure_internal_fix_rejected.scn
  Proves the parser no longer treats an internal non-boundary procedure fix as valid forward support when authoritative no-transition procedure metadata says the exit boundary is somewhere else.

- procedure_sid_boundary_synthesized.scn
  Proves the parser can safely inject a SID boundary waypoint from authoritative procedure metadata when the filed route omits that fix before an airway segment.

- procedure_sid_boundary_without_route_context_blocked.scn
  Proves the parser refuses to inject a single SID boundary waypoint when no following airway or route context proves that boundary belongs in the resolved route.

- procedure_sid_boundary_unreachable_airway_blocked.scn
  Proves single SID boundary synthesis fails closed when the boundary fix cannot enter the following airway and reach the filed exit anchor.

- procedure_star_boundary_synthesized.scn
  Proves the parser can safely inject a STAR boundary waypoint from authoritative procedure metadata when an airway arrives at a filed procedure token without an explicit final fix.

- procedure_star_boundary_unreachable_airway_blocked.scn
  Proves STAR boundary diagnostics are not marked synthesized or applied when the preceding airway cannot reach the STAR boundary fix.

- procedure_sid_transition_synthesized.scn
  Proves the parser can safely inject a SID waypoint from a uniquely provable transition when the filed route omits that anchor before an airway segment.

- procedure_sid_ordered_sequence_synthesized.scn
  Proves the parser can safely inject a full ordered SID fix sequence before an airway when one authoritative non-runway, no-transition path exists.

- procedure_star_transition_synthesized.scn
  Proves the parser can safely inject a STAR waypoint from a uniquely provable transition when an airway arrives at a filed procedure token without an explicit final fix.

- procedure_star_ordered_sequence_synthesized.scn
  Proves the parser can safely inject a full ordered STAR fix sequence after an airway when one authoritative non-runway, no-transition path exists.

- procedure_multi_transition_blocked.scn
  Proves the parser records a fail-closed blocker when a procedure has multiple declared transitions and no single safe application path can be chosen.

- procedure_runway_blocked.scn
  Proves the parser records a fail-closed blocker when a procedure is runway-dependent and therefore cannot be safely applied from generic route text.

- procedure_dual_role_blocked.scn
  Proves the parser records a fail-closed blocker when one procedure token is dual-role and there is no safe way to assume SID-only or STAR-only application.

- procedure_star_transition_unmatched.scn
  Proves the parser records the STAR-side unmatched-transition blocker when the previous proven anchor does not match the authoritative STAR transition metadata.

- procedure_star_multi_transition_blocked.scn
  Proves the parser records the STAR-side multi-transition blocker when multiple STAR transitions exist and no single safe application path can be chosen.

- procedure_star_runway_blocked.scn
  Proves the parser records the STAR-side runway-dependent blocker and refuses generic airway-to-STAR synthesis when runway context is required.

- procedure_star_context_only.scn
  Proves the parser records the STAR-side `NO_PROVABLE_PATH` blocker when a STAR is recognized from metadata but there is no usable anchor, transition, or boundary context.

- procedure_star_false_anchor_rejected.scn
  Proves the parser records the STAR-side `INSUFFICIENT_CONTEXT` blocker when a neighboring point exists but is not the authoritative STAR boundary or transition anchor.

- procedure_star_anchor_without_transition.scn
  Proves the parser records the STAR-side `NOT_NEEDED` case when the filed route already carries the authoritative STAR boundary fix and no synthetic application is required.

- procedure_star_runway_record.scn
  Proves the parser records the STAR-side `NOT_NEEDED` runway-record case when the filed route already carries the authoritative STAR boundary fix even though the procedure metadata is runway-tagged.

- oceanic_coordinate_resolution.scn
  Replays the long-haul coordinate-token class of bug and validates annotated fixes plus oceanic coordinates resolve into route waypoints instead of being dropped.

- duplicate_ident_resolution.scn
  Replays the duplicate-ident class of bug and makes the current reference-point bias explicit so duplicate point selection can be regression-tested while we keep rebuilding the nav graph.

- duplicate_ident_point_lookahead_resolution.scn
  Proves standalone duplicate point resolution uses the next filed point as route context when that context gives a clearer candidate than previous-point distance alone.

- duplicate_ident_destination_context_resolution.scn
  Proves standalone duplicate point resolution uses the destination as route context when the duplicate is the final filed point.

- duplicate_ident_airway_entry_context_resolution.scn
  Proves a duplicate waypoint immediately before an airway is resolved by exact airway reachability to the next filed anchor instead of nearest/destination scoring.

- duplicate_ident_airway_endpoint_resolution.scn
  Proves airway expansion now chooses the reachable duplicate endpoint that actually lives on the airway, instead of trusting a geographically closer off-airway duplicate.

- duplicate_ident_sid_airway_entry_context_resolution.scn
  Proves a synthesized SID ordered-sequence exit fix uses the following airway graph to choose the reachable duplicate fix before airway expansion begins.

- duplicate_ident_star_sequence_airway_entry.scn
  Proves airway-to-STAR ordered sequence entry uses the airway-resolved duplicate boundary fix and rebuilds the remaining ordered procedure sequence from that corrected anchor.

- duplicate_ident_star_boundary_airway_entry_context_resolution.scn
  Proves single-fix STAR boundary synthesis takes the reachable duplicate endpoint from airway graph expansion instead of any pre-resolved nearest point.

- duplicate_ident_star_sequence_rebuild_context_resolution.scn
  Proves remaining STAR ordered-sequence fixes rebuilt after airway entry use next-fix region/context instead of nearest-reference distance alone.

- dense_europe_authority_collapse.scn
  Replays the dense Europe shard-collapse class of bug and proves overlapping sector fragments collapse into canonical authorities instead of bloating the route board.

- enroute_requires_authoritative_route.scn
  Negative regression case proving that a live Center alone is not enough to create ENROUTE truth when the route engine has not produced authoritative sectors.

- controller_feed_stale_does_not_populate_live_boards.scn
  Proves stale controller-feed rows cannot populate Departure, Arrival, or live ENROUTE boards even if a malformed snapshot still carries controller entries.

- controller_feed_unavailable_does_not_populate_live_boards.scn
  Proves unavailable controller-feed rows cannot populate Departure, Arrival, or live ENROUTE boards even if a malformed snapshot still carries controller entries.

- airport_sector_stale_does_not_populate_terminal_airspace.scn
  Proves stale airport-sector terminal coverage cannot populate Departure or Arrival APP/DEP rows.

- departure_terminal_requires_sector_authority.scn
  Proves departure APP/DEP board entries require terminal-sector authority when that data exists, while airport-local tower matching remains callsign-scoped.

- terminal_airspace_rejects_suffix_without_facility_truth.scn
  Proves terminal APP/DEP rows require VATSIM Approach facility truth in addition to callsign suffix and terminal-sector token matches.

- airport_local_accepts_facility_truth.scn
  Proves airport-local Delivery, Ground, and Tower rows display when the VATSIM facility class matches the callsign role.

- airport_local_rejects_suffix_without_facility_truth.scn
  Proves airport-local Delivery, Ground, and Tower rows cannot be created by suffix shape alone when the VATSIM facility class disagrees.

- airport_center_token_cannot_match_terminal_airspace.scn
  Proves center coverage rows cannot masquerade as terminal APP/DEP authority even if a center boundary token has a terminal-looking suffix.

- departure_terminal_fallback_without_sector_data.scn
  Proves airport-token APP/DEP fallback remains available when no terminal coverage data exists, preserving no-data behavior without overriding authority data.

- airport_coverage_terminal_tokens_authoritative.scn
  Proves airport coverage builder terminal match tokens come from explicit TRACON prefixes, not loose `id`/`name` strings or broad underscore-prefix expansion.

- airport_coverage_no_catalog_no_identifier_fallback.scn
  Proves missing authority catalog data does not turn a center boundary identifier into a live controller prefix.

- airport_coverage_authority_catalog_prefix_only.scn
  Proves catalog rows with explicit controller prefixes use those prefixes without padding in boundary identifiers as extra matches.

- airport_coverage_ignores_arbitrary_boundary_properties.scn
  Proves center boundary `name` and arbitrary string properties cannot become authority lookup tokens.

- airport_coverage_rejects_boundary_without_catalog_refresh.scn
  Proves a refreshed center boundary is not applied unless its matching VATSpy authority catalog also arrives.

- airport_coverage_rejects_catalog_without_boundary_refresh.scn
  Proves a refreshed VATSpy authority catalog is not applied against old center boundaries.

- airport_coverage_accepts_complete_center_refresh.scn
  Proves a complete center boundary plus authority refresh replaces the previous generation atomically.

- airport_terminal_cache_rebuilds_after_refresh.scn
  Proves cached airport terminal coverage rebuilds after terminal-only boundary refresh.

- airport_terminal_accepts_fresh_snapshot_after_refresh.scn
  Proves fresh airport terminal coverage is accepted after terminal-only boundary refresh.

- airport_terminal_rejects_stale_snapshot_after_refresh.scn
  Proves stale airport terminal coverage cannot prove containment after terminal-only boundary refresh.

- enroute_requires_explicit_controller_prefixes.scn
  Proves ENROUTE live center matching requires explicit controller prefixes and will not match or display a controller only because its prefix equals a sector identifier.

- enroute_authority_gap_does_not_display_offline_row.scn
  Proves an ENROUTE sector with no explicit controller prefixes does not create a pilot-facing offline row; the authority gap remains a resolver/status diagnostic instead.

- enroute_diagnostic_offline_row_not_displayed.scn
  Proves an offline ENROUTE diagnostic row is not promoted into the active display board in ENROUTE mode.

- enroute_offline_row_uses_sector_identifier.scn
  Proves an ENROUTE offline row is labeled from the proven sector identifier, not renamed from a tempting center-like match token.

- enroute_accepts_data_driven_fss_facility.scn
  Proves ENROUTE matching accepts route-authorized FSS/oceanic-style controllers when the VATSIM facility code marks them as enroute service and the authority catalog prefix matches.

- enroute_rejects_ctr_suffix_without_enroute_facility.scn
  Proves a `_CTR` callsign suffix alone cannot make a controller eligible for ENROUTE when the controller feed facility code says it is not center/FSS service.

- authority_compiler_vatspy_hkg_activates_polygon.scn
  Proves a VATSpy FIR row compiles explicit HKG activation patterns and maps HKG_W_CTR to the VHHK authority polygon.

- authority_compiler_blank_prefix_is_unmapped_gap.scn
  Proves a blank VATSpy callsign-prefix row remains an unmapped data gap and does not infer PAZA_FSS from the boundary identifier.

- authority_compiler_rejects_wrong_facility.scn
  Proves a matching callsign pattern still cannot activate a center authority when the VATSIM facility code is not center/FSS service.

- authority_polygons_vatspy_boundary_compiles.scn
  Proves a VATSpy boundary-style polygon compiles with source-derived lookup keys and ring truth.

- authority_polygons_simaware_tracon_compiles.scn
  Proves a SimAware TRACON-style polygon compiles as terminal authority with suffix-aware lookup keys.

- authority_polygons_invalid_ring_is_data_gap.scn
  Proves invalid polygon geometry becomes an explicit data gap instead of a usable authority polygon.

- resolver_authority_blank_prefix_is_data_gap.scn
  Proves a VATSpy FIR/UIR row with a blank callsign-prefix field does not invent the boundary identifier as a controller prefix and instead surfaces an explicit route authority gap.

- resolver_authority_gap_identifiers_trace_current_and_next.scn
  Proves route authority-gap diagnostics name both current and next sectors so missing catalog rows can be traced without a live flight retest.

- resolver_boundary_callsign_property_is_not_authority_key.scn
  Proves a center boundary `callsign` property cannot become an authority-catalog lookup key or create a controller prefix match.

- resolver_route_rejects_boundary_without_catalog_refresh.scn
  Proves a refreshed route-sector boundary is not applied unless its matching VATSpy authority catalog also arrives.

- resolver_route_rejects_catalog_without_boundary_refresh.scn
  Proves a refreshed VATSpy authority catalog is not applied against old route-sector boundaries.

- resolver_route_rebuilds_after_center_catalog_refresh.scn
  Proves route-sector resolution rebuilds when a complete center boundary plus authority-catalog refresh arrives.

- route_collapse_no_controller_prefix_leak.scn
  Proves route-sector collapse grouping keys do not become controller prefixes when no explicit authority prefix exists.

- route_collapse_explicit_prefixes_only.scn
  Proves route-sector collapse preserves explicit controller prefixes without padding the collapsed authority identifier back into live matching data.

- narrow_crossing_sampled_miss.scn
  Documents the old sampled traversal weakness by showing a narrow sector crossing missed when only the end sample lands outside the polygon.

- narrow_crossing_exact_hit.scn
  Proves the new exact traversal path catches the same narrow crossing without depending on route sampling density.

- antimeridian_exact_crossing.scn
  Proves the exact traversal path catches a dateline crossing against an anti-meridian-spanning sector polygon without depending on sampled route points.
