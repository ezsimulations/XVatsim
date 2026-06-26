# Step 76 VOI7842 UJ11 Airway Resolution Diagnostic Report

## Files inspected

- `core/src/RouteResolution.cpp`
- `core/include/XVatsim/core/RouteResolution.h`
- `modules/route_sector/src/RouteSectorResolver.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/route_resolver_airway_lookup_uj11_igsam_cdr.scn`
- `tools/regression_harness/scenarios/route_resolver_voi7842_uj11_diagnostic.scn`
- `C:\X-Plane 12\Custom Data\cycle_info.txt`
- `C:\X-Plane 12\Custom Data\cycle.json`
- `C:\X-Plane 12\Custom Data\earth_awy.dat`
- `C:\X-Plane 12\Custom Data\earth_fix.dat`
- `C:\X-Plane 12\Custom Data\earth_nav.dat`
- `C:\X-Plane 12\Resources\plugins\XVatsim\logs\xvatsim_diagnostics.log`

## Files changed

- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/route_resolver_airway_lookup_uj11_igsam_cdr.scn`
- `tools/regression_harness/scenarios/route_resolver_voi7842_uj11_diagnostic.scn`
- `outputs/voi7842_uj11_airway_resolution_diagnostic_report.md`

Changes are diagnostic/harness-only. No runtime behavior was changed. No airway was hardcoded. Step 75 fail-closed route-unavailable Center behavior was not changed.

## Navdata Source

XVatsim route-sector loads X-Plane airway/fix/nav data from:

- `C:\X-Plane 12\Custom Data\earth_awy.dat`
- `C:\X-Plane 12\Custom Data\earth_fix.dat`
- `C:\X-Plane 12\Custom Data\earth_nav.dat`

If those are unavailable, it falls back to `C:\X-Plane 12\Resources\default data\...`.

Installed AIRAC:

- Cycle: `2606`
- Revision: `1`
- Valid: `11/JUN/2026 - 09/JUL/2026`
- Source: Navigraph X-Plane 12 custom data

## Findings

`UJ11` exists in the installed airway file. `IGSAM` exists in `earth_fix.dat`, and `CDR` exists in `earth_nav.dat`.

The raw Navigraph airway data contains the needed UJ11 bridge:

- `IGSAM MM 11 LLANO MM 11 N 2 200 600 UJ11`
- `IDEAL MM 11 LLANO MM 11 N 2 200 600 UJ11`
- `IDEAL MM 11 URVIK MM 11 N 2 200 600 UJ11`
- `CDR MM 3 URVIK MM 11 N 2 200 600 UJ11`

With safe header filtering, `IGSAM UJ11 CDR` expands as:

`IGSAM > LLANO > IDEAL > URVIK > CDR`

With the current XVatsim payload filter, it does not expand. The current filter skips any line whose first character is `I`, which removes valid airway records beginning with `IGSAM`, `IDEAL`, `IKBAN`, and `IRBEK`.

Relevant code paths:

- `core/src/RouteResolution.cpp:2234`
- `modules/route_sector/src/RouteSectorResolver.cpp:5074`

Current filter:

```cpp
if (line.empty() || line[0] == 'I') {
    continue;
}
```

This was meant to skip X-Plane header lines, but it also skips valid airway records where the first waypoint ident starts with `I`.

## UJ40 Control Case

`UJ40` expands because the `CDR UJ40 NLD` path does not depend on any airway row starting with `I`:

`CDR > FLANE > OMEVO > SLW > NOVUL > GABLA > NOTAL > SASES > ONKAL > NLD`

That explains why the VOI7842 route could expand later airways while failing specifically at `IGSAM UJ11 CDR`.

## TEDZI1B

`TEDZI1B` remains a separate unsupported procedure/SID issue. It is not the UJ11 root cause and was not fixed in Step 76. The live log still shows the route as incomplete with `unsupported 1` and `unresolved-airways 1`.

## Root Cause Classification

Root cause: parser/filter issue.

More specifically: the X-Plane airway payload loader treats all lines beginning with `I` as header lines. This silently removes valid airway segments required to connect `IGSAM` to `CDR` on `UJ11`.

Not classified as:

- data missing: the raw Navigraph data contains UJ11, IGSAM, CDR, and the needed bridge rows
- wrong AIRAC/source path: XVatsim is using current Navigraph 2606 custom data
- airway unsupported: other airways and UJ11 catalog presence prove airway parsing is active
- route-authority fail-open: Step 75 safety remains the correct behavior while route authority is unavailable

## Diagnostic Proof

Raw parser comparison against installed Navigraph 2606:

- Current-style filter: `UJ11 IGSAM-CDR=<none>`
- Safe header-only filter: `UJ11 IGSAM-CDR=IGSAM>LLANO>IDEAL>URVIK>CDR`
- Current-style filter skipped six UJ11 rows beginning with `I`, including the three required bridge rows.
- Both filters expand `UJ40 CDR-NLD`.

Focused scenarios:

- `route_resolver_airway_lookup_uj11_igsam_cdr.scn`: explicit graph control, proves UJ11 expands when the graph contains the edges.
- `route_resolver_voi7842_uj11_diagnostic.scn`: raw X-Plane payload reproduction, proves current parser leaves `UJ11` unresolved while `UJ40` expands.
- `brain_controller_relevance_blocks_reachable_centers_when_route_unavailable.scn`: Step 75 safety remains intact.

## Step 77 Minimal Fix Proposal

Fix the header-line filter in both duplicate loader paths:

- `core/src/RouteResolution.cpp`
- `modules/route_sector/src/RouteSectorResolver.cpp`

Replace the broad `line[0] == 'I'` skip with a header-only check after trimming, such as skipping only empty lines, exact `I` header lines, and `99` terminator lines.

Then update `route_resolver_voi7842_uj11_diagnostic.scn` to require:

- `expanded_tokens=UJ11,UJ40`
- no unresolved `UJ11`
- resolved path includes `IGSAM,LLANO,IDEAL,URVIK,CDR`

Do not hardcode `UJ11`. Do not add route-authority fail-open behavior. Do not change CTAF/UNICOM, standby/direct CTAF/COM writer, source-owned stable-key gates/settings, or display authority behavior.

## Verification

Build command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: passed.

Focused scenario command: harness run over 19 scenarios.

Result: passed, `19` run, `0` failed.

Focused scenarios included:

- `route_resolver_airway_lookup_uj11_igsam_cdr.scn`
- `route_resolver_voi7842_uj11_diagnostic.scn`
- `brain_controller_relevance_blocks_reachable_centers_when_route_unavailable.scn`
- `block7_enroute_reachable_center_current_and_next_accepted.scn`
- `block7_enroute_reachable_center_off_route_rejected.scn`
- `route_sector_stale_does_not_populate_enroute_board.scn`
- `radio_reachable_source_uses_transceiver_candidates_only.scn`
- `radio_reachable_source_rejects_stale_transceiver_snapshot.scn`
- `route_sector_authority_evidence_records_filtered_controllers.scn`
- `route_sector_authority_proof_evidence_records_transceiver_candidates.scn`
- `transceiver_resolver_authority_evidence_records_filtered_candidates.scn`
- `ctaf_unicom_bypass_retirement_authority_guardrail.scn`
- `standby_assist_decision_ledger_controller_target_unchanged.scn`
- `standby_assist_direct_ctaf_live_gate_on_selects_direct_ctaf.scn`
- `standby_assist_writer_result_controller_success.scn`
- `source_owned_live_consumption_settings_absent_default_off.scn`
- `source_owned_live_consumption_settings_true_clean.scn`
- `brain_display_live_consumption_gate_off_default.scn`
- `brain_display_live_consumption_gate_on_clean.scn`

Full saved regression:

- Required because the harness was changed for raw-payload diagnostics.
- Result: passed, `434` scenarios, `0` failed.
