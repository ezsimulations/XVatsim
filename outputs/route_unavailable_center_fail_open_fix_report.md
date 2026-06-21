# Step 75 Route-Unavailable Center Fail-Open Fix Report

## Files changed

- `brain/src/BrainControllerRelevanceWorker.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/brain_controller_relevance_blocks_reachable_centers_when_route_unavailable.scn`
- `outputs/route_unavailable_center_fail_open_fix_report.md`

No route parsing support was added for `TEDZI1B` or `UJ11`.
`route_sector`, `transceiver_resolver`, CTAF/UNICOM, standby assist, direct CTAF, COM writer, source-owned stable-key gates/settings, generated fallback stable-key behavior, HNL, and compatibility aliases were not changed.

## Root cause

For the live `VOI7842 MMAS->KMDW` failure, route authority was unavailable:

- `unsupportedList=TEDZI1B`
- `unresolvedList=UJ11`
- `unresolvedAirwayList=UJ11`
- `currentSectors=none`
- `nextSectors=none`
- `routeResolved=0`
- `AUTHORITY route unavailable`

`BrainControllerRelevanceWorker` still accepted enroute Center candidates when radio range said they were reachable and route metadata was missing. The unsafe accept condition was:

```cpp
routeMatch.matched || !routeMatch.hasRouteMetadata || station.tuned
```

That allowed radio-only Centers such as `KC_94_CTR` and `MEM_22_CTR` to be treated as live enroute relevance rows with `CURRENT_POLYGON` behavior even though there was no current/next polygon proof.

## Exact decision path fixed

In `BrainControllerRelevanceWorker`, Center candidates are now accepted only when:

- they match a current/next route sector, or
- they are already tuned, which remains a separate explicit brain-owned proof path.

Radio-range-only Center candidates are no longer accepted when route metadata/authority is unavailable.

## New rejection/block reason

New reason:

`center-route-authority-unavailable-radio-only-blocked`

Meaning:

- the Center was present as AFV/radio-range evidence,
- route metadata/authority was unavailable,
- live enroute display was blocked.

The old fail-open reason `center-route-metadata-unavailable-reachable` is no longer emitted from the accepted Center path.

## Proof

Default route-unavailable behavior:

- New scenario: `brain_controller_relevance_blocks_reachable_centers_when_route_unavailable.scn`
- Proves `KC_94_CTR` and `MEM_22_CTR` remain radio-range evidence.
- Proves route/authority relevance is empty.
- Proves `BrainControllerRelevanceWorker` produces no enroute display callsigns.
- Proves both Centers are rejected with `center-route-authority-unavailable-radio-only-blocked`.

Radio-range evidence still exists:

- `expect.radio_reachable_source_candidates=KC_94_CTR@125.725:CTR:AFV_RADIO_RANGE,MEM_22_CTR@132.550:CTR:AFV_RADIO_RANGE`
- `expect.radio_reachable_source_counts=DEL=0,GND=0,TWR=0,APP_DEP=0,CTR=2,ATIS=0,OTHER=0`

Valid route-proven Centers still display:

- `block7_enroute_reachable_center_current_and_next_accepted.scn` passed.

Off-route and stale/unavailable authority guardrails:

- `block7_enroute_reachable_center_off_route_rejected.scn` passed.
- `route_sector_stale_does_not_populate_enroute_board.scn` passed.

Standby/direct CTAF/COM writer behavior was not broadened:

- `standby_assist_decision_ledger_controller_target_unchanged.scn` passed.
- `standby_assist_direct_ctaf_live_gate_on_selects_direct_ctaf.scn` passed.
- `standby_assist_writer_result_controller_success.scn` passed.

CTAF/UNICOM unaffected:

- `ctaf_unicom_bypass_retirement_authority_guardrail.scn` passed.

## Build result

Command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: passed.

## Focused scenario results

Command: focused harness bundle, 17 scenarios.

Result: passed, `17` run, `0` failed.

Scenarios:

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

## Full saved regression result

Command: full harness run over all saved `.scn` files.

Result: passed, `432` scenarios, `0` failed.

## Follow-up recommendation

Keep the unresolved-route performance/retry issue as a separate bug-fix front. This step intentionally did not add `TEDZI1B` or `UJ11` support and did not tune route refresh behavior. The live logs showed repeated unresolved-route refresh cost after the initial load, so a future targeted fix should add retry throttling or unresolved-route backoff without changing brain-owned authority decisions.
