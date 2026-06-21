# Source-Owned Fallback Stable-Key Gated Live Opt-In Report

## 1. Files changed

Step 66 changed:
- `brain/include/XVatsim/brain/BrainDisplayIntent.h`
- `brain/src/BrainDisplayIntent.cpp`
- `brain/include/XVatsim/brain/PhaseSnapshotPublisher.h`
- `brain/src/PhaseSnapshotPublisher.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/brain_display_live_consumption_gate_off_default.scn`
- `tools/regression_harness/scenarios/brain_display_live_consumption_gate_on_clean.scn`
- `tools/regression_harness/scenarios/brain_display_live_consumption_shadow_gate_off_blocked.scn`
- `tools/regression_harness/scenarios/brain_display_live_consumption_missing_plan_blocked.scn`
- `tools/regression_harness/scenarios/phase_publisher_live_consumption_drift_blocked.scn`
- `tools/regression_harness/scenarios/brain_display_live_consumption_duplicate_fallback_polygon.scn`
- `tools/regression_harness/scenarios/brain_display_live_consumption_completion_identity_parity.scn`
- `tools/regression_harness/scenarios/phase_publisher_live_consumption_reuse_current_incomplete.scn`
- `tools/regression_harness/scenarios/phase_publisher_live_consumption_fresh_displaces_previous.scn`
- `tools/regression_harness/scenarios/brain_display_live_consumption_overlay_cap_one_hidden.scn`
- `tools/regression_harness/scenarios/brain_display_live_consumption_overlay_cap_multiple_hidden.scn`
- `outputs/source_owned_stable_key_gated_live_opt_in_report.md`

The worktree already had other dirty files from prior recovery steps. Step 66 did not edit `plugin/src/XVatsimPlugin.cpp`, settings-store, `transceiver_resolver`, `route_sector`, HNL-specific code, or compatibility aliases.

## 2. Contract Gate scope confirmation

Contract scope was brain/harness/report only. No plugin or settings-store/product wiring was added. The new gate is harness/request-only in Step 66.

## 3. Live opt-in gate

Gate name: `sourceOwnedFallbackStableKeyLiveConsumptionEnabled`

Gate source: `sourceOwnedFallbackStableKeyLiveConsumptionGateSource`

Default state: `false`, source `default`

Default mode behavior can not change. With the gate off, generated fallback behavior is retained and the Step 66 ledger records `consumedKeyType=generated-fallback`, `liveConsumptionAllowed=0`, and `defaultModeProtected=1`.

## 4. Live consumption contract

Source-owned live consumption is allowed only when all of these are true:
- Step 66 live gate armed.
- Step 64 proposal gate armed.
- Step 63 shadow gate enabled.
- Shadow recompute attempted.
- Plan context available.
- Source-owned key present.
- Source-owned migration readiness true.
- No shadow drift.
- Hash, row ordering, dedupe, duplicate suppression, completion identity, phase reuse, overlay cap, and `+N more ATC` parity all match.
- Step 63 row-level `shadowSafeForFutureLiveOptIn` is true.
- Step 64 readiness is true: display `readyForFutureLiveConsumption` or phase `liveConsumptionReadyForFutureOptIn`.

The live path does not rewrite final display rows, row order, dedupe, duplicate suppression, completion identity, phase reuse, overlay cap, or `+N more ATC` behavior. It records gated source-owned consumption only after the existing readiness proof passes.

## 5. Fields and counters added

Display intent now has a Step 66 live-consumption decision ledger and summary. Phase publisher records now include gated live-consumption decision fields and a phase live-consumption summary.

Decision fields include the requested gate state, generated/source-owned keys, source-owned readiness, plan context, shadow parity booleans, proposal gate, live gate source, allowed/block reason, consumed key type, behavior changed, and default-mode protection.

Summary counters added:
`liveConsumptionDecisionCount`, `liveConsumptionGateArmedCount`, `liveConsumptionAllowedCount`, `liveConsumptionBlockedCount`, `sourceOwnedConsumedCount`, `generatedFallbackConsumedCount`, `missingPlanBlockedCount`, `shadowGateOffBlockedCount`, `shadowParityNotAttemptedBlockedCount`, `shadowDriftBlockedCount`, `hashMismatchBlockedCount`, `rowOrderingMismatchBlockedCount`, `dedupeMismatchBlockedCount`, `duplicateSuppressionMismatchBlockedCount`, `completionIdentityMismatchBlockedCount`, `phaseReuseMismatchBlockedCount`, `overlayCapMismatchBlockedCount`, `moreAtcMismatchBlockedCount`, `missingSourceOwnedKeyBlockedCount`, `migrationNotReadyBlockedCount`, `defaultModeProtectedCount`, and `behaviorChanged`.

Blocked reasons added:
`live-consumption-gate-not-armed`, `live-consumption-proposal-gate-not-armed`, `shadow-gate-disabled`, `shadow-parity-not-attempted`, `missing-plan-context`, `source-owned-key-missing`, `source-owned-key-not-migration-ready`, `shadow-drift-detected`, `final-board-hash-mismatch`, `row-ordering-mismatch`, `dedupe-group-mismatch`, `duplicate-suppression-mismatch`, `completion-identity-mismatch`, `phase-reuse-mismatch`, `overlay-cap-mismatch`, `more-atc-mismatch`, `shadow-not-safe-for-future-live-opt-in`, and `readiness-not-ready-for-future-live-consumption`.

## 6. Proof summary

Default-off proof:
`brain_display_live_consumption_gate_off_default.scn` passed. It proves gate off, generated fallback consumed, default mode protected, behavior unchanged.

Clean gated opt-in proof:
`brain_display_live_consumption_gate_on_clean.scn` and `phase_publisher_live_consumption_reuse_current_incomplete.scn` passed. They prove source-owned consumed only with live gate armed and readiness clean.

Shadow gate OFF block proof:
`brain_display_live_consumption_shadow_gate_off_blocked.scn` passed. It blocks with `shadow-gate-disabled` and keeps generated fallback.

Missing plan context block proof:
`brain_display_live_consumption_missing_plan_blocked.scn` passed. It blocks with `missing-plan-context` and keeps generated fallback.

Drift block proof:
`phase_publisher_live_consumption_drift_blocked.scn` passed. The drifting displaced row blocks with `source-owned-phase-reuse-match-drift` and keeps generated fallback.

Dedupe/duplicate parity proof:
`brain_display_live_consumption_duplicate_fallback_polygon.scn` passed.

Completion identity proof:
`brain_display_live_consumption_completion_identity_parity.scn` passed.

Phase reuse proof:
`phase_publisher_live_consumption_reuse_current_incomplete.scn` and `phase_publisher_live_consumption_fresh_displaces_previous.scn` passed.

Overlay cap and `+N more ATC` proof:
`brain_display_live_consumption_overlay_cap_one_hidden.scn` and `brain_display_live_consumption_overlay_cap_multiple_hidden.scn` passed. Visible/hidden rows and `moreAtc` counts remained unchanged.

CTAF/UNICOM unaffected proof:
`ctaf_unicom_bypass_retirement_authority_guardrail.scn` passed.

Standby/direct CTAF/COM writer unaffected proof:
`standby_assist_decision_ledger_controller_target_unchanged.scn`, `standby_assist_direct_ctaf_live_gate_on_selects_direct_ctaf.scn`, and `standby_assist_writer_result_controller_success.scn` passed.

Protected subsystem proof:
No Step 66 edits were made to `plugin/src/XVatsimPlugin.cpp`, settings-store, `transceiver_resolver`, `route_sector`, HNL-specific code, or public/header compatibility aliases. Full regression passed, including existing HNL and protected-subsystem scenarios.

## 7. Focused scenario result

Command:
`XVatsimRegressionHarness.exe <scenario-file>` run across 23 focused scenarios.

Result:
23 passed, 0 failed.

Focused list:
- 11 new Step 66 scenarios listed in Files changed.
- `brain_display_stable_key_shadow_gate_off_default.scn`
- `brain_display_stable_key_shadow_gate_on_context.scn`
- `brain_display_stable_key_shadow_missing_plan_context.scn`
- `brain_display_live_consumption_readiness_gate_off_default.scn`
- `brain_display_live_consumption_readiness_gate_on_clean.scn`
- `brain_display_live_consumption_readiness_missing_plan_blocked.scn`
- `phase_publisher_live_consumption_readiness_gate_on_clean.scn`
- `phase_publisher_live_consumption_readiness_drift_blocked.scn`
- `ctaf_unicom_bypass_retirement_authority_guardrail.scn`
- `standby_assist_decision_ledger_controller_target_unchanged.scn`
- `standby_assist_direct_ctaf_live_gate_on_selects_direct_ctaf.scn`
- `standby_assist_writer_result_controller_success.scn`

## 8. Build and full regression

Build command:
`& 'C:\Users\DARRON\Documents\bin\cmake.exe' --build build --config RelWithDebInfo`

Build result:
Passed.

Full saved regression command:
All `.scn` files under `tools/regression_harness/scenarios` were run with `build/tools/XVatsimRegressionHarness.exe`.

Full saved regression result:
424 scenarios passed, 0 failed.

## 9. Required confirmations

No live behavior changed.

Default mode still uses generated fallback behavior.

Source-owned live consumption remains default OFF.

Source-owned live consumption occurs only when the new live opt-in gate is armed and all existing readiness checks pass.

Default-on behavior is not implemented.

No plugin wiring was added.

No settings-store/product wiring was added.

CTAF/UNICOM, standby, direct CTAF, COM writer, transceiver resolver, route sector, HNL, and compatibility aliases were not changed by Step 66.

## 10. Recommended Step 67

Proceed to a product-wiring readiness proposal only: define the settings-store/plugin Contract Gate for exposing `sourceOwnedFallbackStableKeyLiveConsumptionEnabled`, still default OFF, with no default-on behavior and no broad stable-key migration.
