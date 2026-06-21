# Step 62: Source-Owned Stable Key Dry-Run Consumer Parity Report

## Files changed

- `brain/include/XVatsim/brain/BrainTypes.h`
- `brain/include/XVatsim/brain/BrainDisplayIntent.h`
- `brain/include/XVatsim/brain/PhaseSnapshotPublisher.h`
- `brain/src/BrainDisplayIntent.cpp`
- `brain/src/PhaseSnapshotPublisher.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/brain_display_source_owned_stable_key_fallback_polygon_context.scn`
- `tools/regression_harness/scenarios/brain_display_source_owned_stable_key_missing_plan_context.scn`
- `tools/regression_harness/scenarios/brain_display_stable_key_consumer_dry_run_duplicate_fallback_polygon.scn`
- `tools/regression_harness/scenarios/phase_publisher_source_owned_stable_key_reuse_current_incomplete.scn`
- `tools/regression_harness/scenarios/phase_publisher_source_owned_stable_key_fresh_displaces_previous.scn`
- `tools/regression_harness/scenarios/phase_publisher_source_owned_stable_key_frequency_mismatch.scn`
- `tools/regression_harness/scenarios/phase_publisher_source_owned_stable_key_role_mismatch.scn`
- `tools/regression_harness/scenarios/brain_display_overlay_cap_one_hidden_row.scn`
- `tools/regression_harness/scenarios/brain_display_overlay_cap_multiple_hidden_rows.scn`
- `outputs/source_owned_stable_key_dry_run_consumer_parity_report.md`

## Dry-run consumer parity fields added

BrainDisplayIntent now emits `BrainDisplayStableKeyConsumerDryRunRecord` fields:

- `dryRunStableKeyConsumerDecisionId`
- `subjectKey`, `callsign`, `role`, `frequency`, `endpoint`, `airportIcao`
- `currentBehaviorKey`
- `sourceOwnedStableCompletionKey`
- `generatedFallbackStableCompletionKey`
- `currentBehaviorKeySource`
- `sourceOwnedKeyPresent`
- `sourceOwnedKeyMigrationReady`
- `behaviorConsumerEnabled`
- `dryRunDedupeGroupCurrent`
- `dryRunDedupeGroupSourceOwned`
- `dryRunDedupeGroupWouldChange`
- `dryRunDuplicateSuppressionWouldChange`
- `dryRunCompletionIdentityWouldChange`
- `dryRunPhaseReuseMatchCurrent`
- `dryRunPhaseReuseMatchSourceOwned`
- `dryRunPhaseReuseWouldChange`
- `dryRunRowOrderingWouldChange`
- `dryRunOverlayCapWouldChange`
- `dryRunMoreAtcWouldChange`
- `dryRunDriftDetected`
- `dryRunDriftReason`
- `dryRunSafeForOptIn`
- `dryRunBlockedReason`

The phase publisher reuse ledger now carries the same dry-run comparison fields on `PhaseSnapshotReuseDecisionRecord` where source-owned row metadata is available.

## Summary fields added

Added display and phase dry-run summaries:

- `dryRunStableKeyConsumerDecisionCount`
- `sourceOwnedKeyPresentCount`
- `migrationReadyCount`
- `dedupeGroupWouldChangeCount`
- `duplicateSuppressionWouldChangeCount`
- `completionIdentityWouldChangeCount`
- `phaseReuseWouldChangeCount`
- `rowOrderingWouldChangeCount`
- `overlayCapWouldChangeCount`
- `moreAtcWouldChangeCount`
- `driftDetectedCount`
- `safeForOptInCount`
- `behaviorConsumerEnabledCount`
- `displayBehaviorChanged`

Harness outputs:

- `BrainDisplayStableKeyConsumerDryRunSummary`
- `BrainDisplayStableKeyConsumerDryRunDecisions`
- `PhasePublisherStableKeyConsumerDryRunSummary`

## Current behavior key versus source-owned key comparison

Current behavior remains on the existing key paths:

- Display/dedupe still uses the rendered station duplicate key.
- Stable completion behavior still uses the existing `completionStableKey`/generated fallback path.
- Phase reuse still uses the current publisher matching rules.

The dry-run ledger compares those current keys against:

`source-owned:fallback-polygon-geometry|<callsign>|<role>|<normalized-frequency>|<plan-context>`

The source-owned key is diagnostic-only. `behaviorConsumerEnabled=0` in all focused scenarios.

## Dedupe parity proof

The dry-run display ledger computes current dedupe groups and source-owned dedupe groups, then compares group membership.

Focused results:

- Single fallback polygon row with plan context: `dedupeGroupWouldChange=0`.
- Duplicate fallback polygon rows: `dedupeGroupWouldChange=0`, `duplicateSuppressionWouldChange=0`.
- Missing plan context: no dedupe drift, but opt-in blocked by `missing-plan-context`.

## Duplicate suppression parity proof

Duplicate fallback polygon rows remained one visible row with one duplicate-hidden accepted row. Dry-run source-owned grouping matched current duplicate grouping, so duplicate suppression would not change.

## Completion identity parity proof

Dry-run completion identity remains semantic parity, not byte-equality. The source-owned key intentionally has a different shape and plan context, but focused scenarios reported `completionIdentityWouldChange=0` because grouping and row identity semantics did not split or merge rows.

## Phase reuse parity proof

Phase publisher dry-run scenarios proved:

- Current-incomplete reuse remains `reused-last-proven-row`.
- Fresh current evidence still displaces previous evidence.
- Frequency mismatch remains blocked by `frequency-mismatch`.
- Role mismatch remains blocked by `role-mismatch`.

All source-owned phase dry-run scenarios reported `phaseReuseWouldChange=0`, `drift=0`, and `behaviorConsumerEnabled=0`.

## Row ordering parity proof

All focused dry-run scenarios reported `rowOrderingWouldChange=0`. The new key fields are not used by display sorting.

## Overlay cap / `+N more ATC` parity proof

Overlay cap one-hidden and multiple-hidden scenarios were updated to assert dry-run parity:

- One hidden row: `overlayCapWouldChange=0`, `moreAtcWouldChange=0`, `moreAtc=1`.
- Multiple hidden rows: `overlayCapWouldChange=0`, `moreAtcWouldChange=0`, `moreAtc=2`.

Existing cap behavior and `+N more ATC` counts are unchanged.

## Drift and warning cases

No dry-run drift was detected in focused Step 62 scenarios.

The missing plan context case is explicitly blocked:

- `sourceOwnedKeyMigrationReady=0`
- `dryRunSafeForOptIn=0`
- `dryRunBlockedReason=missing-plan-context`

This is warning/diagnostic only and does not alter display, dedupe, or reuse.

## Future opt-in readiness

Source-owned fallback polygon keys are safe for future opt-in only when:

- source-owned key is present
- migration readiness is true
- plan context is available
- dry-run drift is zero
- behavior consumer remains disabled during proof

The focused plan-context, duplicate, phase reuse, frequency, role, and cap cases satisfy those dry-run conditions. Missing plan context remains a hard opt-in blocker.

## Behavior consumption remains disabled

The source-owned key is not consumed by display, dedupe, completion, phase reuse, overlay cap, CTAF/UNICOM, standby assist, direct CTAF gate logic, or COM writing. All focused dry-run summaries reported `behaviorConsumerEnabled=0`.

## CTAF/UNICOM unaffected proof

`ctaf_unicom_bypass_retirement_authority_guardrail.scn` passed. No live bypass authority returned, and CTAF/UNICOM behavior remains unchanged.

## Standby/direct CTAF/COM writer unaffected proof

`standby_assist_decision_ledger_controller_target_unchanged.scn` and `standby_assist_direct_ctaf_live_gate_on_selects_direct_ctaf.scn` passed. Standby behavior, direct CTAF gate behavior, and COM writer behavior remain unchanged.

## Focused scenario summaries

Focused Step 62 verification covered 17 scenarios:

- fallback polygon with plan context
- fallback polygon with missing plan context
- duplicate fallback polygon rows
- source-owned phase reuse current-incomplete
- source-owned fresh-displaces-previous
- source-owned frequency mismatch
- source-owned role mismatch
- overlay cap one-hidden
- overlay cap multiple-hidden
- CTAF/UNICOM authority guardrail
- controller standby unchanged
- direct CTAF gate unchanged
- existing phase reuse current-incomplete
- existing fresh-displaces-previous
- existing frequency mismatch
- existing role mismatch
- upstream stable key source audit inventory

Result: `Passed 17 focused Step 62 scenarios`.

## Build command/result

Command:

```powershell
& 'C:\Users\DARRON\Documents\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: passed. `XVatsimBrain`, `XVatsimPlugin`, `XVatsimPreflightBuilder`, and `XVatsimRegressionHarness` built successfully.

## Focused scenario command/result

Command:

```powershell
$scenarios = @(
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
}
```

Result: `Passed 17 focused Step 62 scenarios`.

## Full saved regression command/result

Command:

```powershell
$scenarios = @(Get-ChildItem '.\tools\regression_harness\scenarios\*.scn' | Sort-Object Name)
foreach ($scenario in $scenarios) {
  & '.\build\tools\XVatsimRegressionHarness.exe' $scenario.FullName
}
```

Result: `Passed 405 scenarios`.

## Recommended next stable-key migration step

Add a default-off behavior gate for source-owned fallback polygon stable-key consumption in dry-run/live-compare mode, still preserving current behavior by default. The gate should first allow a shadow recomputation of dedupe and phase reuse results using source-owned keys, then compare final board hash, row ordering, cap output, and phase publisher decisions before any live opt-in consumption is allowed.
