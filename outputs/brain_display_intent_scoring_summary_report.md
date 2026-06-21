# BrainDisplayIntent Scoring Summary Report

## Files changed

- `brain/include/XVatsim/brain/BrainDisplayIntent.h`
- `brain/src/BrainDisplayIntent.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/brain_display_intent_honors_center_relation_fact.scn`
- `tools/regression_harness/scenarios/brain_display_intent_fallback_hidden_center_has_decision.scn`
- `tools/regression_harness/scenarios/brain_display_intent_non_displayable_rows_have_decisions.scn`
- `tools/regression_harness/scenarios/brain_display_intent_duplicate_suppression_has_decision.scn`
- `tools/regression_harness/scenarios/brain_display_intent_stage_deferred_rows_have_decisions.scn`
- `tools/regression_harness/scenarios/brain_display_intent_filtered_unknown_relation_rows_have_decisions.scn`
- `outputs/brain_display_intent_scoring_summary_report.md`

## Scoring/confidence fields populated

The existing `positiveScore`, `negativeScore`, `confidenceLevel`, and `hardBlock` fields are now accompanied by:

- `scoreSummary`

The harness now prints `scoreSummary` for each display decision. Format:

```text
confidence=<level>/positive=<score>/negative=<score>/hardBlock=<0|1>/winner=<positive|negative|balanced|blocked>
```

## Scoring rules used in this step

- High-confidence accepted relation fact display:
  - `confidence=high`
  - `positiveScore=100`
  - `negativeScore=0`
  - `winner=positive`

- Fallback display allowed by existing compatibility behavior:
  - `confidence=fallback`
  - `positiveScore=25`
  - `negativeScore=0`
  - `winner=positive`

- Non-displayable rows, offline or empty frequency:
  - `confidence=high`
  - `positiveScore=0`
  - `negativeScore=100`
  - `hardBlock=true`
  - `winner=blocked`

- Duplicate suppression:
  - `confidence=medium`
  - `positiveScore=0`
  - `negativeScore=50`
  - `winner=negative`

- Stage-deferred rows:
  - non-selected board: `positiveScore=0`, `negativeScore=50`
  - departure-hidden `ARRIVAL_PREP` center: `positiveScore=40`, `negativeScore=80`
  - `winner=negative`

- Filtered/unknown/explicit hidden relation facts:
  - `confidence=medium`
  - `positiveScore=0`
  - `negativeScore=75`
  - `winner=negative`

- Fallback-hidden center:
  - `confidence=fallback`
  - `positiveScore=0`
  - `negativeScore=15`
  - `winner=negative`

These scores are diagnostic-only. They do not drive final display output.

## HNL protected behavior

`brain_display_intent_honors_center_relation_fact.scn` still displays:

```text
HNL_02_CTR:CURRENT_POLYGON:ACTIVE
```

The HNL decision remains:

```text
display-accepted / relation-fact-applied / confidence=high / score=100/0 / winner=positive
```

This preserves the rule that a high-confidence accepted relation fact outranks fallback-hidden inference.

## Focused scenario summaries

- `brain_display_intent_honors_center_relation_fact.scn`
  - Proves high-confidence accepted relation fact has positive score greater than negative score.

- `brain_display_intent_fallback_hidden_center_has_decision.scn`
  - Proves fallback-hidden has fallback confidence and negative score greater than positive score.

- `brain_display_intent_non_displayable_rows_have_decisions.scn`
  - Proves offline and empty-frequency rows have high-confidence hard-block negative scoring.

- `brain_display_intent_duplicate_suppression_has_decision.scn`
  - Proves duplicate suppression has explicit medium-confidence negative scoring.

- `brain_display_intent_stage_deferred_rows_have_decisions.scn`
  - Proves stage-deferred rows have explicit negative scoring, including an `ARRIVAL_PREP` center that remains hidden during departure.

- `brain_display_intent_filtered_unknown_relation_rows_have_decisions.scn`
  - Proves filtered/unknown/hidden relation facts produce explicit negative scores and `filteredAfterAcceptCount=1` for filtered relation.

## Build command and result

Command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: PASS.

## Focused scenario command and result

Commands:

```powershell
.\build\tools\XVatsimRegressionHarness.exe .\tools\regression_harness\scenarios\brain_display_intent_honors_center_relation_fact.scn
.\build\tools\XVatsimRegressionHarness.exe .\tools\regression_harness\scenarios\brain_display_intent_fallback_hidden_center_has_decision.scn
.\build\tools\XVatsimRegressionHarness.exe .\tools\regression_harness\scenarios\brain_display_intent_non_displayable_rows_have_decisions.scn
.\build\tools\XVatsimRegressionHarness.exe .\tools\regression_harness\scenarios\brain_display_intent_duplicate_suppression_has_decision.scn
.\build\tools\XVatsimRegressionHarness.exe .\tools\regression_harness\scenarios\brain_display_intent_stage_deferred_rows_have_decisions.scn
.\build\tools\XVatsimRegressionHarness.exe .\tools\regression_harness\scenarios\brain_display_intent_filtered_unknown_relation_rows_have_decisions.scn
```

Result: PASS for all six focused scenarios.

## Full saved regression command and result

Command:

```powershell
$h = '.\build\tools\XVatsimRegressionHarness.exe'
$scenarios = Get-ChildItem -Path '.\tools\regression_harness\scenarios' -Filter '*.scn' | Sort-Object Name
$passed = 0
foreach ($s in $scenarios) {
    $result = & $h $s.FullName
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED $($s.Name)"
        $result
        exit $LASTEXITCODE
    }
    $passed++
}
Write-Host "ALL_SCENARIOS_PASSED count=$passed"
```

Result: PASS, `ALL_SCENARIOS_PASSED count=275`.

## Known gaps left for later steps

- Scores are local BrainDisplayIntent diagnostics only; no global scoring engine was added.
- Scores are not yet used to change display output.
- `completionStableKey` still awaits true upstream completion identity.
- Overlay cap / `+N more ATC` behavior is still outside this ledger.
- CTAF/UNICOM completion approval is still outside this step.
