# Step 77 X-Plane Airway I-Prefix Header Filter Fix Report

## Files Changed

- `core/src/RouteResolution.cpp`
- `modules/route_sector/src/RouteSectorResolver.cpp`
- `tools/regression_harness/scenarios/route_resolver_voi7842_uj11_diagnostic.scn`
- `tools/regression_harness/scenarios/route_resolver_airway_loader_keeps_i_prefix_waypoint_rows.scn`
- `outputs/xplane_airway_i_prefix_header_filter_fix_report.md`

No UJ11 hardcode was added. No TEDZI1B/SID support was added. No route authority fail-open behavior was added.

## Exact Old Filter

The airway payload loops in both loader paths used:

```cpp
if (line.empty() || line[0] == 'I') {
    continue;
}
```

Affected runtime locations:

- `core/src/RouteResolution.cpp`, `BuildAirwayGraphFromPayloads`
- `modules/route_sector/src/RouteSectorResolver.cpp`, `GetAirwayGraph`

The broad `line[0] == 'I'` condition skipped real X-Plane airway records whose first waypoint ident starts with `I`.

## New Filter Behavior

New helper behavior in both loader paths:

```cpp
const auto trimmed = TrimAsciiWhitespace(line);
return trimmed.empty() || trimmed == "I" || trimmed == "99";
```

The loader now trims only for the header/terminator skip decision. The existing `line` text is still passed to the existing `std::istringstream` parser, preserving token parsing behavior, casing normalization, airway graph directionality, and airway edge semantics.

Filter proof:

- `line=" I"` -> `trimmed="I"` -> skipped
- `line="99"` -> `trimmed="99"` -> skipped
- `line="IGSAM MM 11 LLANO MM 11 N 2 200 600 UJ11"` -> trimmed record text remains a real record -> parsed

## Parser Proof

Updated scenario:

- `route_resolver_voi7842_uj11_diagnostic.scn`

Result:

- `RouteTokenKinds: IGSAM:Point UJ11:Airway CDR:Point UJ40:Airway NLD:Point`
- `ResolvedWaypoints: ACFT IGSAM LLANO IDEAL URVIK CDR FLANE OMEVO SLW NOVUL GABLA NOTAL SASES ONKAL NLD KMDW`
- `ExpandedTokens: UJ11 UJ40`
- `UnresolvedTokens:` empty
- `UnresolvedAirwayTokens:` empty

This proves raw X-Plane/Navigraph-style payload now expands:

`IGSAM > LLANO > IDEAL > URVIK > CDR`

and still expands UJ40:

`CDR > FLANE > OMEVO > SLW > NOVUL > GABLA > NOTAL > SASES > ONKAL > NLD`

New targeted regression scenario:

- `route_resolver_airway_loader_keeps_i_prefix_waypoint_rows.scn`

Result:

- exact `I` header row is skipped
- exact `99` terminator row is skipped
- `IGSAM MM 11 LLANO MM 11 N 2 200 600 UJ11` is retained and parsed
- `ExpandedTokens: UJ11`
- `UnresolvedAirwayTokens:` empty

## TEDZI1B Status

`TEDZI1B` remains a separate unsupported SID/procedure issue. Step 77 intentionally did not add SID support, procedure parsing support, or any route hardcode.

## Step 75 Safety Proof

Step 75 fail-closed Center behavior remains intact.

Focused guardrail:

- `brain_controller_relevance_blocks_reachable_centers_when_route_unavailable.scn` passed.

This keeps radio-only Centers blocked from live display when route authority is unavailable. Route-unavailable radio-range evidence may remain diagnostic evidence, but it does not become live Center display authority.

Additional authority/display guardrails passed:

- valid route-proven Center display
- off-route Center rejection
- route-sector stale/unavailable board protection
- CTAF/UNICOM authority guardrail
- standby controller target unchanged
- direct CTAF behavior
- COM writer controller success

## Build Result

Command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: passed.

## Focused Scenario Result

Focused harness bundle:

- `route_resolver_airway_lookup_uj11_igsam_cdr.scn`
- `route_resolver_voi7842_uj11_diagnostic.scn`
- `route_resolver_airway_loader_keeps_i_prefix_waypoint_rows.scn`
- `brain_controller_relevance_blocks_reachable_centers_when_route_unavailable.scn`
- `block7_enroute_reachable_center_current_and_next_accepted.scn`
- `block7_enroute_reachable_center_off_route_rejected.scn`
- `route_sector_stale_does_not_populate_enroute_board.scn`
- `ctaf_unicom_bypass_retirement_authority_guardrail.scn`
- `standby_assist_decision_ledger_controller_target_unchanged.scn`
- `standby_assist_direct_ctaf_live_gate_on_selects_direct_ctaf.scn`
- `standby_assist_writer_result_controller_success.scn`

Result: passed, `11` scenarios, `0` failed.

## Full Saved Regression Result

Full saved regression was required because Step 77 changes runtime parser behavior.

Result: passed, `435` scenarios, `0` failed.

## Follow-Up Recommendation

Recommended next bug-fix front:

1. Add TEDZI1B/SID support as a separate route procedure parsing step, with no route authority fail-open.
2. Address unresolved-route retry cost/backoff separately, after route parser correctness is restored and proven.
