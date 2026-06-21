# BrainDisplayIntent Fail-Soft Preview Report

Step 27 added diagnostic-only fail-soft preview recommendations to the existing BrainDisplayIntent display decision ledger. Runtime display behavior is unchanged; the preview only explains where the documented XVatsim fail-soft policy would keep the current result, warn, defer, lower priority, hard-block, or ask for more evidence.

## Files Changed

- `brain/include/XVatsim/brain/BrainDisplayIntent.h`
- `brain/src/BrainDisplayIntent.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/brain_display_intent_honors_center_relation_fact.scn`
- `tools/regression_harness/scenarios/brain_display_intent_fallback_hidden_center_has_decision.scn`
- `tools/regression_harness/scenarios/brain_display_intent_non_displayable_rows_have_decisions.scn`
- `tools/regression_harness/scenarios/brain_display_intent_duplicate_suppression_has_decision.scn`
- `tools/regression_harness/scenarios/brain_display_intent_stage_deferred_rows_have_decisions.scn`
- `tools/regression_harness/scenarios/brain_display_intent_filtered_unknown_relation_rows_have_decisions.scn`
- `outputs/brain_display_intent_fail_soft_preview_report.md`

## Fail-Soft Fields Added

Each `BrainDisplayDecisionRecord` now carries:

- `failSoftRecommendation`
- `failSoftReason`
- `currentHideButFailSoftWouldShowOrWarn`

The recommendation is diagnostic only and does not feed final display construction.

## Summary Counters Added

`BrainDisplayFailSoftPreviewSummary` records:

- `failSoftPreviewCount`
- `recommendKeepDisplayCount`
- `recommendKeepHideCount`
- `recommendDisplayWithWarningCount`
- `recommendStageDeferCount`
- `recommendLowerPriorityDisplayCount`
- `recommendHardBlockHideCount`
- `recommendNeedsMoreEvidenceCount`
- `currentHideButFailSoftWouldShowOrWarnCount`

The regression harness prints and asserts these through `BrainDisplayIntentFailSoftSummary`.

## Rules Implemented

- `display-accepted` and `display-format-only` recommend `keep-current-display`.
- Explicit non-displayable rows caused by offline or empty frequency recommend `hard-block-hide`.
- Fallback-hidden accepted center rows recommend `prefer-display-with-warning` unless explicit high-confidence negative evidence exists.
- Duplicate suppression recommends `keep-current-hide` for identical role/callsign/frequency rows without unique relation evidence, or `prefer-lower-priority-display` when unique relation evidence is present.
- Stage-deferred rows recommend `prefer-stage-defer`.
- Accepted `Filtered` rows recommend `prefer-display-with-warning` unless explicit high-confidence negative evidence exists.
- Accepted `Unknown` rows recommend `needs-more-evidence`.
- Accepted hidden relation facts without high-confidence negative evidence recommend `needs-more-evidence`.

## HNL Protection

The protected HNL relation-fact scenario remains unchanged:

- Final display still includes `HNL_02_CTR@126.500`.
- Display decision remains `display-accepted`.
- Reason remains `relation-fact-applied`.
- Confidence remains `high`.
- Positive score remains greater than negative score.
- Fail-soft recommendation is `keep-current-display`.

This preserves the rule that a high-confidence accepted relation fact beats fallback hidden inference.

## Focused Scenario Summaries

- `brain_display_intent_honors_center_relation_fact.scn`: `preview=1`, `keepDisplay=1`, `currentHideButFailSoftWouldShowOrWarn=0`.
- `brain_display_intent_fallback_hidden_center_has_decision.scn`: `preview=1`, `displayWithWarning=1`, `currentHideButFailSoftWouldShowOrWarn=1`.
- `brain_display_intent_non_displayable_rows_have_decisions.scn`: `preview=2`, `hardBlockHide=2`.
- `brain_display_intent_duplicate_suppression_has_decision.scn`: `preview=2`, `keepDisplay=1`, `keepHide=1`.
- `brain_display_intent_stage_deferred_rows_have_decisions.scn`: `preview=2`, `stageDefer=2`.
- `brain_display_intent_filtered_unknown_relation_rows_have_decisions.scn`: `preview=3`, `displayWithWarning=1`, `needsMoreEvidence=2`, `currentHideButFailSoftWouldShowOrWarn=1`.

## Verification

Build command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: PASS.

Focused scenario command:

```powershell
$h = '.\build\tools\XVatsimRegressionHarness.exe'
$scenarios = @(
  '.\tools\regression_harness\scenarios\brain_display_intent_honors_center_relation_fact.scn',
  '.\tools\regression_harness\scenarios\brain_display_intent_fallback_hidden_center_has_decision.scn',
  '.\tools\regression_harness\scenarios\brain_display_intent_non_displayable_rows_have_decisions.scn',
  '.\tools\regression_harness\scenarios\brain_display_intent_duplicate_suppression_has_decision.scn',
  '.\tools\regression_harness\scenarios\brain_display_intent_stage_deferred_rows_have_decisions.scn',
  '.\tools\regression_harness\scenarios\brain_display_intent_filtered_unknown_relation_rows_have_decisions.scn'
)
foreach ($s in $scenarios) {
  $result = & $h $s
  if ($LASTEXITCODE -ne 0) { Write-Host "FAILED $s"; $result; exit $LASTEXITCODE }
  Write-Host "PASSED $s"
}
Write-Host "FOCUSED_DISPLAY_FAIL_SOFT_PASSED count=$($scenarios.Count)"
```

Result: PASS, `FOCUSED_DISPLAY_FAIL_SOFT_PASSED count=6`.

Full saved regression command:

```powershell
$h = '.\build\tools\XVatsimRegressionHarness.exe'
$scenarios = Get-ChildItem -Path '.\tools\regression_harness\scenarios' -Filter '*.scn' | Sort-Object Name
$passed = 0
foreach ($s in $scenarios) {
  $result = & $h $s.FullName
  if ($LASTEXITCODE -ne 0) { Write-Host "FAILED $($s.Name)"; $result; exit $LASTEXITCODE }
  $passed++
}
Write-Host "ALL_SCENARIOS_PASSED count=$passed"
```

Result: PASS, `ALL_SCENARIOS_PASSED count=275`.

## Known Gaps

- Fail-soft preview is diagnostic only and does not change display output yet.
- This step does not introduce a global weighted scoring engine.
- Duplicate uniqueness is limited to currently available role/callsign/frequency and relation-fact metadata.
- Overlay cap behavior is outside this step.
- CTAF/UNICOM completion policy is outside this step.
- `completionStableKey` is still not fully populated by upstream identity, so the ledger continues to rely on best available callsign/frequency/role keys where needed.
