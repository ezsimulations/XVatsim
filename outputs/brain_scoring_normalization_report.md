# Brain Scoring Normalization Report

Step 28 corrected the BrainDisplayIntent diagnostic scoring direction and updated the brain evidence scoring model. Runtime display behavior is unchanged; scores remain diagnostic only.

## Files Changed

- `docs/BRAIN_EVIDENCE_SCORING_MODEL.md`
- `brain/include/XVatsim/brain/BrainDisplayIntent.h`
- `brain/src/BrainDisplayIntent.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/brain_display_intent_honors_center_relation_fact.scn`
- `tools/regression_harness/scenarios/brain_display_intent_fallback_hidden_center_has_decision.scn`
- `tools/regression_harness/scenarios/brain_display_intent_non_displayable_rows_have_decisions.scn`
- `tools/regression_harness/scenarios/brain_display_intent_duplicate_suppression_has_decision.scn`
- `tools/regression_harness/scenarios/brain_display_intent_stage_deferred_rows_have_decisions.scn`
- `tools/regression_harness/scenarios/brain_display_intent_filtered_unknown_relation_rows_have_decisions.scn`
- `outputs/brain_scoring_normalization_report.md`

## Old Scoring Issue

The display decision ledger used `100/75/50/25/15` style scores. That made a single evidence item look absolute, which recreates the architecture failure the recovery work is meant to remove: one fact can appear to veto or crown a decision instead of contributing bounded evidence.

## New Bounded Scale

BrainDisplayIntent diagnostic scores now use normalized `0.00` to `1.00` values:

- High confidence: about `0.80` to `1.00`
- Medium confidence: about `0.50` to `0.79`
- Low confidence: about `0.20` to `0.49`
- Fallback/unknown: about `0.05` to `0.19`

The focused display scenarios now assert two-decimal score output and score summaries such as:

```text
confidence=high/positive=0.90/negative=0.00/hardBlock=0/winner=positive/recommendation=keep-current-display
```

## Hard Blocks

Hard blocks are now represented as a separate `hardBlock` boolean/category, not as a giant score. Non-displayable rows such as offline or empty-frequency still carry high negative diagnostic weight (`0.90`), but the reason they are hard-blocked is the explicit `hardBlock=true` state.

## HNL Protection

The protected HNL scenario remains functionally unchanged:

- `HNL_02_CTR@126.500` still displays.
- Decision remains `display-accepted`.
- Reason remains `relation-fact-applied`.
- Confidence remains `high`.
- Score is now bounded: `positive=0.90`, `negative=0.00`, `hardBlock=0`.
- Fail-soft recommendation remains `keep-current-display`.

## Focused Scenario Summaries

- HNL relation fact accepted display: `score=0.90/0.00`, `confidence=high`, `winner=positive`.
- Fallback-hidden accepted center: `score=0.00/0.15`, `confidence=fallback`, `winner=negative`, recommendation `prefer-display-with-warning`.
- Non-displayable accepted rows: `score=0.00/0.90`, `hardBlock=1`, recommendation `hard-block-hide`.
- Duplicate suppression: `score=0.00/0.65`, `confidence=medium`, recommendation `keep-current-hide`.
- Stage-deferred rows: `score=0.00/0.65` for board-not-selected and `0.55/0.70` for arrival-prep stage defer.
- Filtered/unknown/hidden relation rows: `score=0.00/0.65`, `confidence=medium`, with fail-soft recommendations preserved.

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
Write-Host "FOCUSED_DISPLAY_NORMALIZED_SCORING_PASSED count=$($scenarios.Count)"
```

Result: PASS, `FOCUSED_DISPLAY_NORMALIZED_SCORING_PASSED count=6`.

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

- Scores are still diagnostic only and do not alter display behavior.
- This does not introduce a global scoring engine.
- Other legacy domains may still contain compatibility score fields outside the BrainDisplayIntent decision ledger.
- Thresholds are documented as starting points and will need domain-specific calibration before scores become live authority.
- `completionStableKey` is still a known upstream identity gap for some display decisions.
