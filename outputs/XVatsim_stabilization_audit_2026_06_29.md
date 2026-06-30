# XVatsim Stabilization Audit - 2026-06-29

## Scope

This stabilization pass covered workflow-level radio ordering and downstream ownership guardrails. No new features were added. Runtime behavior was not changed in this pass; only high-value scenario guardrails were expanded.

## Covered Workflows

| Area | Existing coverage identified | Gap closed in this pass |
| --- | --- | --- |
| Departure order | `brain_frequency_intent_departure_tower_active_standby_tracon.scn`, `standby_assist_decision_ledger_controller_target_unchanged.scn`, `brain_display_intent_departure_next_center_over_ctaf.scn` | No new scenario needed. Existing guardrails cover Clearance/Delivery, Ground, Tower, Departure/Approach, Center, and CTAF fallback behavior. |
| Enroute order | `brain_display_intent_current_next_distance.scn`, `brain_display_intent_distance_refresh.scn` | Strengthened `brain_display_intent_current_next_distance.scn` to assert display source, final callsign order, overlay cap before/after order, and overlay body order. |
| Arrival order | `brain_display_intent_arrival_center_next_polygon_sorts_before_terminal.scn`, `brain_frequency_intent_arrival_center_active_standby_tracon.scn`, `standby_assist_arrival_uses_brain_display_order_center_before_app_ground.scn` | Strengthened arrival standby guardrail by explicitly setting `arrival_polygon=KZSE` where current/next arrival center behavior is expected. |
| Route transition behavior | Current/next and arrival current/next scenarios already existed | Added explicit `arrival_polygon=KDEN` to the enroute current/next transition scenario and asserted that current center remains before next centers through final display and overlay publication. |
| Standby assist | Multiple standby assist scenarios, including `standby_assist_arrival_uses_brain_display_order_center_before_app_ground.scn` | Verified standby assist consumes the display board order by index and does not sort the station rows. |
| UI/overlay | Overlay body line expectations existed in departure/arrival scenarios | Added enroute overlay body-line order assertion to prove UI/overlay consumes the brain-published order for current/next centers. |
| Authority relevance | `brain_controller_relevance_blocks_reachable_centers_when_route_unavailable.scn`, `block7_enroute_reachable_center_off_route_rejected.scn`, `resolver_vatglasses_frequency_requires_route_owned_polygon.scn` | No new scenario needed. Existing guardrails cover radio-range-only center rejection when route ownership is unavailable or unproven. |
| Performance | `resolver_authority_cache_reuses_proof_for_watch_only_controller_churn.scn`, `radio_reachable_verifier_feed_skips_unchanged_snapshot.scn`, `block7_ordinary_route_movement_does_not_run_proof.scn` | No new scenario needed. Existing guardrails cover stable proof-input reuse and unchanged snapshot skipping. |

## Ownership Guardrail Result

BrainDisplayIntent remains the final station-order owner.

- `BrainDisplayIntent` performs the authoritative final row sort in `SortFrequencyIntent`.
- `BrainOwnedRuntime` assigns `output.finalDisplay = output.displayIntent.finalDisplay` and passes that snapshot to `PhaseSnapshotPublisher`.
- `PhaseSnapshotPublisher` copies/stores the candidate snapshot as-is; it does not sort station rows.
- `BrainOwnedRuntime` standby assist builds `orderedEligibleIndices` by iterating `output.board.stations` in published order; it does not apply its own station sort.
- `BrainOrchestrator` builds overlay body rows by iterating `finalDisplaySnapshot.stations[index]`.
- `OverlayWindow` copies rendered body lines by index into `sections.listLines`; it does not reorder rows.
- The only plugin-side overlay mutation found is the controller-message card path, which replaces the station list with a message card and is not a station-ordering path.
- Other `std::sort` / `std::stable_sort` hits are upstream/domain sorting, diagnostics, cache keys, harness expectations, route/authority data, or the intentional BrainDisplayIntent sort.

## Missing Risks Closed

- Enroute current-center-before-next-center behavior now has stronger final-display and overlay-order assertions.
- Route transition inputs now explicitly include `currentPolygon`, `nextPolygon`, and `arrivalPolygon` in the enroute guardrail.
- Arrival current/next center standby behavior now explicitly includes `arrivalPolygon`.
- UI/overlay ownership is now covered by an order-sensitive overlay assertion, not only source inspection.

## Remaining Known Risks

- Saved scenarios validate deterministic harness behavior and published row ownership, not all live VATSIM feed timing volatility.
- PhaseSnapshotPublisher row preservation is verified by source audit and full regression; it does not have a dedicated multi-row publisher-only scenario in this pass.
- Performance guardrails cover proof-cache behavior and unchanged snapshot skipping, but this pass did not add wall-clock performance benchmarks.
- Authority relevance remains dependent on freshness and correctness of route/sector authority data.

## Verification

- Focused stabilization guardrails: passed.
- Full saved regression: passed, 440 scenarios.
- `git diff --check`: passed.

## Release Recommendation

Public release is allowed from the saved-regression and ownership-guardrail standpoint. A normal live-test checklist should still be completed before public distribution, with attention to departure order, enroute current/next center transitions, arrival center-before-terminal ordering, standby assist COM1 standby selection, overlay row order, radio-range-only center rejection, and proof-cache stability under unchanged route/proof inputs.
