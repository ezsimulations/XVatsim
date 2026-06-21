# Step 61: Fallback Polygon Source-Owned Stable Key Report

## Files changed

- `brain/include/XVatsim/brain/BrainDisplayIntent.h`
- `brain/src/BrainDisplayIntent.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/brain_display_source_owned_stable_key_fallback_polygon_context.scn`
- `tools/regression_harness/scenarios/brain_display_source_owned_stable_key_missing_plan_context.scn`
- `tools/regression_harness/scenarios/brain_display_upstream_stable_key_source_audit_inventory.scn`
- `outputs/fallback_polygon_source_owned_stable_key_report.md`

## Source-owned stable key fields added

The stable-key audit record now carries fallback-polygon source-owned parity diagnostics:

- `sourceOwnedStableCompletionKey`
- `sourceOwnedStableCompletionKeyPresent`
- `sourceOwnedStableCompletionKeySource`
- `sourceOwnedStableCompletionKeyShape`
- `generatedFallbackStableCompletionKey`
- `sourceOwnedMatchesGeneratedFallback`
- `sourceOwnedKeyMismatchReason`
- `sourceOwnedKeyPlanContext`
- `sourceOwnedKeyPlanContextAvailable`
- `sourceOwnedKeyPlanContextSource`
- `sourceOwnedKeyMigrationReady`
- `sourceOwnedKeyBehaviorConsumerEnabled`

These fields are emitted only as diagnostics. Display, dedupe, completion, overlay cap, and phase reuse still use the existing behavior paths.

## Summary fields added

`BrainDisplaySourceOwnedStableKeySummary` was added with:

- `sourceOwnedStableKeyDecisionCount`
- `sourceOwnedStableKeyPresentCount`
- `generatedFallbackKeyPresentCount`
- `sourceOwnedMatchesFallbackCount`
- `sourceOwnedMismatchCount`
- `planContextAvailableCount`
- `planContextMissingCount`
- `migrationReadyCount`
- `behaviorConsumerEnabledCount`
- `behaviorChanged`

The harness prints and can assert the summary as `BrainDisplaySourceOwnedStableKeySummary`.

## Key shape used

Fallback polygon/geometry inference rows now receive this diagnostic key shape:

`source-owned:fallback-polygon-geometry|<callsign>|<role>|<normalized-frequency>|<plan-context>`

The implementation currently uses the existing internal role value used by adjacent stable-key diagnostics, normalized callsign, normalized frequency, and explicit plan context.

## Plan context behavior

When product/display-intent polygon context exists, the source-owned key includes:

`current=<current-polygon>;next=<next-polygon>;arrival=<arrival-polygon>;stage=<workflow-stage>`

When no polygon plan context is available, the ledger emits:

- `sourceOwnedKeyPlanContext=plan-context-unavailable`
- `sourceOwnedKeyPlanContextAvailable=0`
- `sourceOwnedKeyPlanContextSource=unavailable`
- `sourceOwnedKeyMigrationReady=0`

Missing plan context remains diagnostic-only and does not block or alter display.

## Source-owned versus generated fallback parity proof

The existing generated fallback stable key is still emitted as `generatedFallbackStableCompletionKey`.

Focused parity scenarios proved:

- With polygon context, `POLY_CTR` emitted a source-owned key containing callsign, role, frequency, and plan context, while keeping the generated fallback key.
- With missing plan context, `DIST_CTR` emitted a source-owned key with `plan-context-unavailable`, while keeping the generated fallback key.
- Both scenarios reported `sourceOwnedMatchesGeneratedFallback=1`.
- The upstream stable key source audit now classifies `fallback-polygon-geometry-inference` as `source-owned`, while still recording `fallbackDownstream=1` because behavior has not migrated.

This is component parity, not byte equality. The source-owned key intentionally adds plan context that the old generated fallback key did not contain.

## Mismatch and warning proof

The focused scenarios reported:

- `sourceOwnedMismatchCount=0`
- `sourceOwnedKeyMismatchReason=<none>`
- `behaviorConsumerEnabledCount=0`
- `behaviorChanged=0`

A missing-plan-context case is warning/diagnostic only and keeps migration readiness false. Duplicate-key and reuse-continuity risks remain warning-ledgered by the Step 59/60 stable-key diagnostics and do not change dedupe or reuse behavior.

## No behavior consumes the new key

`sourceOwnedKeyBehaviorConsumerEnabled` is always `0` in Step 61. The source-owned key is not used by display selection, row ordering, dedupe, completion, phase reuse, overlay cap, standby assist, CTAF/UNICOM, or COM writing.

## Final display, dedupe, and reuse parity proof

Focused coverage included overlay cap, duplicate-hidden, stage-deferred, non-displayable, phase reuse, HNL protected relation-fact, CTAF/UNICOM authority, standby, and direct CTAF gate scenarios. All passed with no behavior-change flags.

Full saved regression also passed, proving existing final display, row ordering, dedupe, completion, and phase publish/reuse behavior remain unchanged.

## Overlay cap / `+N more ATC` parity proof

Focused overlay cap scenarios passed, including one-hidden, multiple-hidden, duplicate separate from cap, stage-deferred separate from cap, and non-displayable separate from cap. Existing `+N more ATC` behavior remains unchanged.

## CTAF/UNICOM unaffected proof

`ctaf_unicom_bypass_retirement_authority_guardrail.scn` passed. No live bypass authority returned, CTAF/UNICOM display remains brain-advisory owned, and pending/failed/empty protections remain unchanged.

## Standby unaffected proof

`standby_assist_decision_ledger_controller_target_unchanged.scn` and `standby_assist_direct_ctaf_live_gate_on_selects_direct_ctaf.scn` passed. Existing controller standby behavior and direct CTAF gate behavior remain unchanged.

## Focused scenario summaries

Focused Step 61 run covered 18 scenarios:

- source-owned fallback polygon key with plan context
- source-owned fallback polygon key with missing plan context
- upstream stable key source audit inventory
- overlay cap one-hidden, multiple-hidden, duplicate, stage-deferred, and non-displayable cases
- phase reuse last-proven, frequency mismatch, role mismatch, and near-cap linkage cases
- synthetic/legacy source-link case
- live product plan-key linkage case
- HNL protected relation-fact guardrail
- CTAF/UNICOM authority guardrail
- controller standby unchanged
- direct CTAF live-gate selected-source guardrail

Result: `Passed 18 focused Step 61 scenarios`.

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
  'brain_display_upstream_stable_key_source_audit_inventory.scn',
  'brain_display_overlay_cap_one_hidden_row.scn',
  'brain_display_overlay_cap_multiple_hidden_rows.scn',
  'brain_display_overlay_cap_duplicate_separate.scn',
  'brain_display_overlay_cap_stage_deferred_separate.scn',
  'brain_display_overlay_cap_non_displayable_separate.scn',
  'brain_display_phase_reuse_last_proven_current_incomplete.scn',
  'brain_display_phase_reuse_frequency_mismatch_blocked.scn',
  'brain_display_phase_reuse_role_mismatch_blocked.scn',
  'brain_display_phase_reuse_near_cap_linked.scn',
  'brain_display_source_link_synthetic_legacy_missing.scn',
  'phase_publisher_plan_key_live_product_present.scn',
  'brain_display_intent_honors_center_relation_fact.scn',
  'ctaf_unicom_bypass_retirement_authority_guardrail.scn',
  'standby_assist_decision_ledger_controller_target_unchanged.scn',
  'standby_assist_direct_ctaf_live_gate_on_selects_direct_ctaf.scn'
)
foreach ($scenario in $scenarios) {
  & '.\build\tools\XVatsimRegressionHarness.exe' (Join-Path 'tools\regression_harness\scenarios' $scenario)
}
```

Result: `Passed 18 focused Step 61 scenarios`.

## Full saved regression command/result

Command:

```powershell
$scenarios = @(Get-ChildItem '.\tools\regression_harness\scenarios\*.scn' | Sort-Object Name)
foreach ($scenario in $scenarios) {
  & '.\build\tools\XVatsimRegressionHarness.exe' $scenario.FullName
}
```

Result: `Passed 400 scenarios`.

## Recommended next stable-key migration step

Add a dry-run consumer parity ledger that compares the new source-owned fallback polygon key against the current behavior key at dedupe and phase reuse decision points, without enabling behavior consumption yet. Once that dry-run proves no row ordering, dedupe, or reuse drift, the source-owned key can be considered for opt-in behavior migration.
