# Step 63: Source-Owned Fallback Stable Key Shadow Gate Report

## 1. Files Changed

- `brain/include/XVatsim/brain/BrainDisplayIntent.h`
- `brain/src/BrainDisplayIntent.cpp`
- `brain/include/XVatsim/brain/PhaseSnapshotPublisher.h`
- `brain/src/PhaseSnapshotPublisher.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/brain_display_stable_key_shadow_gate_off_default.scn`
- `tools/regression_harness/scenarios/brain_display_stable_key_shadow_gate_on_context.scn`
- `tools/regression_harness/scenarios/brain_display_stable_key_shadow_missing_plan_context.scn`
- `tools/regression_harness/scenarios/brain_display_stable_key_consumer_dry_run_duplicate_fallback_polygon.scn`
- `tools/regression_harness/scenarios/brain_display_overlay_cap_one_hidden_row.scn`
- `tools/regression_harness/scenarios/brain_display_overlay_cap_multiple_hidden_rows.scn`
- `tools/regression_harness/scenarios/phase_publisher_source_owned_stable_key_reuse_current_incomplete.scn`
- `tools/regression_harness/scenarios/phase_publisher_source_owned_stable_key_fresh_displaces_previous.scn`
- `tools/regression_harness/scenarios/phase_publisher_source_owned_stable_key_frequency_mismatch.scn`
- `tools/regression_harness/scenarios/phase_publisher_source_owned_stable_key_role_mismatch.scn`
- `outputs/source_owned_stable_key_shadow_gate_report.md`

## 2. Gate Added and Default State

Added a brain/harness request gate:

- `sourceOwnedFallbackStableKeyShadowEnabled`
- `sourceOwnedFallbackStableKeyShadowGateSource`

The gate defaults OFF in both BrainDisplayIntent and PhaseSnapshotPublisher request paths. With the setting absent, the ledger records `gateEnabled=0`, `gateSource=default`, `attempted=0`, and `skipped=shadow-gate-disabled`.

## 3. Gate Source Handling

Gate source diagnostics now classify:

- `default`
- `settings-store`
- `harness`
- `unknown`

The focused harness uses `stable_key.source_owned_fallback_shadow=true`, which records `gateSource=harness`. No plugin/settings-store live behavior was changed.

## 4. Shadow Recompute Fields Added

Per-decision shadow diagnostics include:

- `sourceOwnedFallbackShadowGateEnabled`
- `sourceOwnedFallbackShadowGateSource`
- `shadowRecomputeAttempted`
- `shadowRecomputeSkippedReason`
- `shadowBehaviorConsumerEnabled`
- `shadowFinalBoardHashCurrent`
- `shadowFinalBoardHashSourceOwned`
- `shadowFinalBoardHashMatches`
- `shadowRowOrderingMatches`
- `shadowDedupeGroupsMatch`
- `shadowDuplicateSuppressionMatches`
- `shadowCompletionIdentityMatches`
- `shadowPhaseReuseMatches`
- `shadowOverlayCapMatches`
- `shadowMoreAtcMatches`
- `shadowMissingPlanContextBlocked`
- `shadowDriftDetected`
- `shadowDriftReason`
- `shadowSafeForFutureLiveOptIn`

## 5. Summary Fields Added

Added display and phase-publisher shadow summaries:

- `shadowDecisionCount`
- `shadowGateEnabledCount`
- `shadowRecomputeAttemptedCount`
- `shadowRecomputeSkippedCount`
- `shadowHashMismatchCount`
- `shadowRowOrderingMismatchCount`
- `shadowDedupeMismatchCount`
- `shadowDuplicateSuppressionMismatchCount`
- `shadowCompletionIdentityMismatchCount`
- `shadowPhaseReuseMismatchCount`
- `shadowOverlayCapMismatchCount`
- `shadowMoreAtcMismatchCount`
- `shadowMissingPlanBlockedCount`
- `shadowDriftDetectedCount`
- `shadowSafeForFutureLiveOptInCount`
- `shadowBehaviorConsumerEnabledCount`
- `behaviorChanged`

## 6. Gate OFF Behavior

Gate OFF is default behavior. Shadow recomputation is skipped, behavior consumption remains disabled, and current display/reuse output is untouched.

Focused proof: `brain_display_stable_key_shadow_gate_off_default.scn` reports:

- `gateEnabled=0`
- `attempted=0`
- `skipped=1`
- `safeForFutureLiveOptIn=0`
- `behaviorConsumerEnabled=0`
- `behaviorChanged=0`

## 7. Gate ON Shadow Behavior

Gate ON runs shadow comparison only. It does not feed source-owned keys into live display, dedupe, completion, phase reuse, overlay cap, standby, or COM behavior.

Focused proof: `brain_display_stable_key_shadow_gate_on_context.scn` reports:

- `gateEnabled=1`
- `attempted=1`
- all mismatch counters `0`
- `safeForFutureLiveOptIn=1`
- `behaviorConsumerEnabled=0`
- `behaviorChanged=0`

## 8. Final Board Hash Parity Proof

BrainDisplayIntent shadow records compare `shadowFinalBoardHashCurrent` with `shadowFinalBoardHashSourceOwned`. Gate ON with plan context produced matching hashes and `shadowHashMismatchCount=0`.

## 9. Row Ordering Parity Proof

Shadow row-ordering diagnostics remained matched in focused gate-on, duplicate fallback, phase reuse, and overlay cap scenarios:

- `shadowRowOrderingMatches=1`
- `shadowRowOrderingMismatchCount=0`

## 10. Dedupe and Duplicate Suppression Parity Proof

Duplicate fallback polygon coverage confirmed no dry-run or shadow drift:

- `shadowDedupeGroupsMatch=1`
- `shadowDuplicateSuppressionMatches=1`
- `shadowDedupeMismatchCount=0`
- `shadowDuplicateSuppressionMismatchCount=0`

Current dedupe behavior remains the live behavior.

## 11. Completion Identity Parity Proof

Shadow completion identity comparison remained matched:

- `shadowCompletionIdentityMatches=1`
- `shadowCompletionIdentityMismatchCount=0`

The source-owned key is still diagnostic-only and is not consumed by completion behavior.

## 12. Phase Reuse Parity Proof

Phase publisher reuse diagnostics now include the same shadow gate fields and summary. Focused probes covered:

- current incomplete reuse
- fresh-displaces-previous
- frequency mismatch
- role mismatch

All retained existing phase publish/reuse decisions, and source-owned key behavior consumption stayed disabled.

## 13. Overlay Cap / `+N more ATC` Parity Proof

Overlay cap one-hidden and multiple-hidden scenarios were rerun with the shadow gate ON. Shadow diagnostics reported:

- `shadowOverlayCapMatches=1`
- `shadowMoreAtcMatches=1`
- `shadowOverlayCapMismatchCount=0`
- `shadowMoreAtcMismatchCount=0`

Final cap output and `+N more ATC` counts remain unchanged.

## 14. Missing Plan Context Block Proof

`brain_display_stable_key_shadow_missing_plan_context.scn` proves missing plan context blocks future opt-in readiness without changing behavior:

- `planMissing=1`
- `migrationReady=0`
- `safeForOptIn=0`
- `missingPlanBlocked=1`
- `drift=0`
- `driftReason=missing-plan-context`
- `safeForFutureLiveOptIn=0`

The row remains hidden under the existing no-plan fallback-polygon behavior.

## 15. Live Behavior Consumption Still Disabled

The shadow ledger explicitly reports:

- `shadowBehaviorConsumerEnabled=0`
- `shadowBehaviorConsumerEnabledCount=0`
- `behaviorConsumerEnabled=0`
- `behaviorChanged=0`

No display, dedupe, completion, phase reuse, overlay cap, standby, or COM path consumes the source-owned key.

## 16. CTAF/UNICOM Unaffected Proof

The focused authority guardrail still passed:

- `ctaf_unicom_bypass_retirement_authority_guardrail.scn`

No live CTAF/UNICOM bypass authority returned, and the shadow gate does not touch CTAF/UNICOM projection.

## 17. Standby / Direct CTAF / COM Writer Unaffected Proof

Focused standby/direct CTAF checks passed:

- `standby_assist_decision_ledger_controller_target_unchanged.scn`
- `standby_assist_direct_ctaf_live_gate_on_selects_direct_ctaf.scn`

No standby target selection, direct CTAF gate, latch/write, or COM writer behavior changed.

## 18. Focused Scenario Summaries

Focused Step 63 command covered 20 scenarios:

- Gate OFF default shadow skipped.
- Gate ON with fallback polygon plan context matched current behavior.
- Gate ON missing plan context blocked future opt-in readiness.
- Duplicate fallback polygon rows produced no dedupe/duplicate drift.
- Phase reuse current-incomplete matched.
- Phase reuse fresh-displaces-previous matched.
- Frequency mismatch and role mismatch remained behavior-neutral.
- Overlay cap one-hidden and multiple-hidden matched cap and `+N more ATC`.
- CTAF/UNICOM authority, standby, direct CTAF, and upstream stable-key audit guardrails remained clean.

Result: passed 20 focused Step 63 scenarios.

## 19. Build Command / Result

Command:

```powershell
& 'C:\Users\DARRON\Documents\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: passed. `XVatsimBrain`, `XVatsimPlugin`, `XVatsimPreflightBuilder`, and `XVatsimRegressionHarness` built successfully.

## 20. Focused Scenario Command / Result

Command:

```powershell
$scenarios = @(
  'brain_display_stable_key_shadow_gate_off_default.scn',
  'brain_display_stable_key_shadow_gate_on_context.scn',
  'brain_display_stable_key_shadow_missing_plan_context.scn',
  'brain_display_source_owned_stable_key_fallback_polygon_context.scn',
  'brain_display_source_owned_stable_key_missing_plan_context.scn',
  'brain_display_stable_key_consumer_dry_run_duplicate_fallback_polygon.scn',
  'phase_publisher_source_owned_stable_key_reuse_current_incomplete.scn',
  'phase_publisher_source_owned_stable_key_fresh_displaces_previous.scn',
  'phase_publisher_source_owned_stable_key_frequency_mismatch.scn',
  'phase_publisher_source_owned_stable_key_role_mismatch.scn',
  'brain_display_overlay_cap_one_hidden_row.scn',
  'brain_display_overlay_cap_multiple_hidden_rows.scn',
  'ctaf_unicom_bypass_retirement_authority_guardrail.scn',
  'standby_assist_decision_ledger_controller_target_unchanged.scn',
  'standby_assist_direct_ctaf_live_gate_on_selects_direct_ctaf.scn',
  'brain_display_phase_reuse_last_proven_current_incomplete.scn',
  'brain_display_phase_reuse_fresh_displaces_previous.scn',
  'brain_display_phase_reuse_frequency_mismatch_blocked.scn',
  'brain_display_phase_reuse_role_mismatch_blocked.scn',
  'brain_display_upstream_stable_key_source_audit_inventory.scn'
)
foreach ($scenario in $scenarios) {
  & '.\build\tools\XVatsimRegressionHarness.exe' (Join-Path 'tools\regression_harness\scenarios' $scenario)
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
```

Result: passed 20 focused Step 63 scenarios.

## 21. Full Saved Regression Command / Result

Command:

```powershell
$scenarios = @(Get-ChildItem '.\tools\regression_harness\scenarios\*.scn' | Sort-Object Name)
foreach ($scenario in $scenarios) {
  & '.\build\tools\XVatsimRegressionHarness.exe' $scenario.FullName | Out-Null
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
```

Result: passed 408 scenarios.

## 22. Recommended Next Stable-Key Migration Step

Keep the shadow gate default OFF and continue with a second, still-default-off live-consumer readiness step only after product/settings-store wiring is explicit and shadow parity remains clean. The next useful step is to add a separate live-consumption gate proposal/ledger that refuses opt-in unless shadow parity is clean, plan context is present, and `shadowBehaviorConsumerEnabled` is still proven false in default mode.
