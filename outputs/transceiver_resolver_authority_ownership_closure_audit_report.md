# Step 73 - Transceiver Resolver Authority Ownership Closure Audit

## 1. Files Inspected

Transceiver resolver target:

- `modules/transceiver_resolver/include/XVatsim/modules/transceiver_resolver/TransceiverResolver.h`
- `modules/transceiver_resolver/src/TransceiverResolver.cpp`
- `modules/transceiver_resolver/CMakeLists.txt`
- `modules/transceiver_resolver/README.md`

Brain ownership boundary:

- `brain/include/XVatsim/brain/BrainTypes.h`
- `brain/include/XVatsim/brain/BrainOwnedWorkerTypes.h`
- `brain/src/BrainRadioRangeWorker.cpp`
- `brain/src/BrainControllerRelevanceWorker.cpp`
- `brain/src/BrainOwnedRuntime.cpp`

Usage/proof:

- `plugin/src/XVatsimPlugin.cpp`
- `tools/regression_harness/src/main.cpp`
- focused scenarios listed in this report

## 2. Files Changed

Step 73 is report-only. The only file changed is:

- `outputs/transceiver_resolver_authority_ownership_closure_audit_report.md`

No code, scenarios, expectations, route-sector files, fallback polygon/geometry stable-key gates/settings, display behavior, row ordering, dedupe, completion identity, phase reuse, overlay cap, `+N more ATC`, CTAF/UNICOM, standby, direct CTAF, COM writer, HNL, or deprecated public/header aliases were changed.

## 3. Current Transceiver Resolver Ownership Model

The current model has three distinct layers:

1. `transceiver_resolver` computes and reports evidence facts.
   - It parses/holds AFV transceiver source data.
   - It normalizes and indexes transceivers by callsign.
   - It computes station distance, radio horizon/range, frequency availability, guard/empty-frequency facts, station score, best-by-module-score markers, feed/cache/holdover status, source evidence, controller evidence, and station evidence.
   - Filtering, normalization, geometry checks, frequency checks, and scoring are evidence/candidate computation. They are not hidden authority unless they can suppress, promote, hide, display, live-project, mark standby-eligible, or write-authorize outside the brain-owned path.

2. `transceiver_resolver` produces compatibility candidate projections.
   - `TransceiverResolutionSnapshot::candidatesCompatibilityOnly` marks resolver candidates as compatibility/parity output when evidence exists.
   - Normal radio range, authority-stations, and airport-coverage paths populate candidates as legacy compatibility projections alongside `controllerEvidence`.
   - These compatibility candidates support regression comparison and old-survivor mismatch proof.

3. The brain makes live relevance/display/standby/write-authority decisions.
   - `BuildBrainRadioRangeWorkerOutput` builds the live radio board from brain preview decisions, not directly from resolver compatibility candidates.
   - `BuildBrainOwnedAuthorityStationsCandidateSnapshot` rebuilds authority-station candidates from brain preview decisions.
   - `BuildBrainOwnedAirportCoverageCandidateSnapshot` rebuilds airport-coverage candidates from brain preview decisions.
   - `BrainControllerRelevanceWorker` owns display/relevance decisions from the live brain-owned candidate feed.
   - `BrainOwnedRuntime` owns standby eligibility, target selection, write permission, and COM writer results.

## 4. Remaining Resolver Decisions Found

| Decision / computation | Classification | Notes |
|---|---|---|
| AFV feed fetch/cache/holdover freshness | evidence-only | Reports source availability and cache state; does not display or write-authorize. |
| Transceiver parsing and indexing by callsign | evidence-only | Source normalization for evidence lookup. |
| Distance, range, radio horizon, and airport coverage geometry checks | evidence-only | Produces station facts and scores; no direct display/live/write authority. |
| Frequency resolution and guard/empty-frequency detection | evidence-only | Records frequency facts and unavailable reasons; brain preview decides live survivor/reject. |
| Candidate score and best-by-module-score marker | evidence-only | Used to record station ranking facts; brain preview rebuilds live candidates. |
| Normal radio range compatibility candidates | compatibility-only | Populated from resolver evidence for old-survivor comparison; brain radio worker rebuilds the live radio board. |
| Authority-stations compatibility candidates | compatibility-only | Populated by resolver, then rebuilt by `BuildBrainOwnedAuthorityStationsCandidateSnapshot`. |
| Airport-coverage compatibility candidates | compatibility-only | Populated by resolver, then rebuilt by `BuildBrainOwnedAirportCoverageCandidateSnapshot`. |
| Resolver status lines and counts | evidence-only | Diagnostics/counters, not final behavior authority. |
| Resolver caches | intentionally retained | Cache evidence snapshots and compatibility candidates; brain-owned wrappers still rebuild live output before use. |
| Brain preview survivor/reject decisions | brain-owned | Live accept/reject authority is in brain preview/rebuild functions. |
| Radio board construction | brain-owned | `BuildBrainRadioRangeWorkerOutput` builds `radioBoard` using brain-owned candidate snapshot. |
| Standby eligibility, target selection, and COM writer permission | brain-owned | Lives in `BrainOwnedRuntime`, not `transceiver_resolver`. |

No remaining resolver decision was found that can independently suppress, promote, hide, display, live-project, mark standby-eligible, or write-authorize outside the brain.

## 5. Live Authority Boundary Answer

Can `transceiver_resolver` still suppress, promote, hide, display, live-project, mark standby-eligible, or write-authorize outside the brain?

- No.

What the resolver can still do:

- compute evidence facts
- filter unusable facts into explicit evidence reasons
- normalize callsigns/frequencies
- compute geometry/range/coverage
- score and mark best evidence stations
- produce old compatibility candidate projections
- cache resolver snapshots

What it cannot do by itself:

- add or remove a final display row
- decide display relevance
- decide dedupe or completion identity
- decide phase reuse
- decide overlay cap or `+N more ATC`
- decide standby eligibility
- choose a standby target
- authorize a COM write
- restore CTAF/UNICOM live bypass authority

## 6. Candidate Compatibility and Live Output State

Resolver candidates remain compatibility/parity data when brain evidence exists.

Normal radio range:

- Resolver produces `controllerEvidence` and compatibility `candidates`.
- Brain builds preview decisions from evidence.
- Brain rebuilds the radio-board candidate snapshot from preview survivors.
- `BuildBrainRadioRangeWorkerOutput` emits the live `radioBoard` with reason `brain-radio-range-worker-evidence-authority`.

Authority-stations:

- Resolver produces evidence plus compatibility `candidates`.
- Brain preview summarizes `authority=brain-evidence`, `compatOnly=1`.
- `BuildBrainOwnedAuthorityStationsCandidateSnapshot` clears/rebuilds live candidates from brain survivor decisions and sets compatibility-only false on the rebuilt snapshot.

Airport-coverage:

- Resolver produces evidence plus compatibility `candidates`.
- Brain preview summarizes `authority=brain-evidence`, `compatOnly=1`.
- `BuildBrainOwnedAirportCoverageCandidateSnapshot` clears/rebuilds live candidates from brain survivor decisions and sets compatibility-only false on the rebuilt snapshot.

The resolver's diagnostic evidence snapshot can remain visible for proof, but live output remains brain-owned.

## 7. Focused Proof

Focused transceiver/resolver/authority scenarios passed:

- `transceiver_resolver_evidence_ledger_records_filtered_candidates.scn`
- `transceiver_resolver_authority_evidence_records_filtered_candidates.scn`
- `transceiver_resolver_airport_coverage_evidence_records_filtered_candidates.scn`
- `transceiver_resolver_holdover_keeps_sea_ctr_reachable.scn`
- `radio_reachable_source_uses_transceiver_candidates_only.scn`
- `radio_reachable_source_rejects_over_300nm_transceiver_candidate.scn`
- `radio_reachable_verifier_feed_uses_gated_changed_candidates.scn`
- `resolver_transceiver_geo_rescues_unmatched_center.scn`
- `resolver_transceiver_geo_rejects_remote_center.scn`
- `resolver_transceiver_geo_rejects_non_center_facility.scn`
- `resolver_transceiver_geo_rejects_known_unrelated_callsign_base.scn`
- `resolver_transceiver_geo_rejects_foreign_source_owner.scn`
- `resolver_transceiver_geo_rejects_unowned_neighbor_polygon.scn`
- `resolver_transceiver_geo_rejects_source_owned_broad_container.scn`
- `resolver_transceiver_geo_rescues_non_route_source_match.scn`
- `resolver_route_scoped_geometry_keeps_family_polygon_for_transceiver.scn`
- `resolver_vatglasses_frequency_rejects_transceiver_geo_mismatch.scn`
- `route_sector_authority_proof_evidence_records_transceiver_candidates.scn`

CTAF/UNICOM and standby/direct CTAF/COM guardrails passed:

- `ctaf_unicom_bypass_retirement_authority_guardrail.scn`
- `standby_assist_decision_ledger_controller_target_unchanged.scn`
- `standby_assist_direct_ctaf_live_gate_on_selects_direct_ctaf.scn`
- `standby_assist_writer_result_controller_success.scn`
- `standby_assist_writer_result_dataref_not_writable.scn`
- `standby_assist_writer_result_direct_ctaf_gate_on_success.scn`
- `standby_assist_direct_ctaf_live_gate_controller_behavior_unchanged.scn`

Focused result: passed, 25 scenarios.

Important boundary proofs present:

- resolver evidence visibility includes `compatOnly=1`
- brain preview summaries show `authority=brain-evidence`
- old survivor counts and preview survivor/reject counts are ledgered
- non-actionable, missing-transceiver, over-max-distance, beyond-range, guard-frequency, empty-frequency, non-best, out-of-coverage, and all-stations-failed reasons are evidence/preview decisions
- live radio status includes `brain-radio-range-worker-evidence-authority`
- standby/direct CTAF/COM writer authority remains brain-owned and unaffected

## 8. Missing Proof

No missing proof blocks transceiver resolver authority ownership closure for the audited boundary.

Optional future proof, only if this front is reopened:

- add a narrow scenario proving cached resolver snapshots still flow through brain-owned rebuild before live use
- add a narrow scenario that intentionally creates an old-survivor mismatch and proves brain-owned output follows preview decisions rather than resolver compatibility candidates

These are optional because the current focused suite already proves evidence visibility, compatibility-only resolver candidates, brain preview decisions, and unaffected standby/write guardrails.

## 9. Closure Decision

`transceiver_resolver` authority ownership is closed for the audited boundary.

Closure basis:

- resolver evidence computation is allowed and visible
- resolver compatibility candidates are diagnostic/parity output
- live radio-board, authority-station, airport-coverage, relevance, display, standby, and write-authority decisions are brain-owned
- no resolver path was found that can independently suppress, promote, hide, display, live-project, mark standby-eligible, or write-authorize outside the brain
- focused scenarios passed
- CTAF/UNICOM and standby/direct CTAF/COM guardrails passed

No narrow code follow-up is required by this audit.

## 10. Verification

Build command:

```powershell
& 'C:\Users\DARRON\Documents\bin\cmake.exe' --build build --config RelWithDebInfo
```

Build result: passed.

Focused scenario command:

```powershell
build\tools\XVatsimRegressionHarness.exe <25 transceiver/resolver/authority/standby/write scenarios>
```

Focused result: passed, 25 scenarios.

Full regression:

- Intentionally skipped because Step 73 is report-only and made no code changes.
- Full saved regression is required if a future follow-up changes code.

## 11. Recommended Step 74

Do not continue with another report-only ownership checklist unless it closes a concrete decision or unblocks real code work.

Recommended Step 74: stop the route-sector/transceiver-resolver ownership audit chain and pick the next real authority pocket or implementation target. If there is no specific code target queued, stop here rather than producing documentation churn. If a target is queued, open a new Contract Gate with the exact files, behavior invariants, focused scenarios, and full-regression requirement.
