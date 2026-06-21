# BrainDisplayIntent Decision Ledger Expansion Report

## Files changed

- `brain/include/XVatsim/brain/BrainDisplayIntent.h`
- `brain/src/BrainDisplayIntent.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/brain_display_intent_non_displayable_rows_have_decisions.scn`
- `tools/regression_harness/scenarios/brain_display_intent_duplicate_suppression_has_decision.scn`
- `tools/regression_harness/scenarios/brain_display_intent_stage_deferred_rows_have_decisions.scn`
- `tools/regression_harness/scenarios/brain_display_intent_filtered_unknown_relation_rows_have_decisions.scn`
- `outputs/brain_display_intent_decision_ledger_expansion_report.md`

## New display decision cases ledgered

- Non-displayable accepted rows now produce `display-rejected-non-displayable` with explicit `offline` or `empty-frequency` reasons.
- Duplicate accepted rows now produce `display-rejected-duplicate` with:
  - normalized duplicate key
  - kept decision id
  - dropped decision id
- Boards not selected by the current workflow stage now produce `display-deferred-by-stage` records with stage/source-board reasons.
- Departure-stage enroute centers with `ARRIVAL_PREP` relation still remain hidden from the final board, but now produce a stage-deferred decision reason including stage, source, and relation.
- Existing hidden enroute centers with `FILTERED`, `UNKNOWN`, or `HIDDEN` relation facts now distinguish:
  - `display-rejected-filtered`
  - `display-rejected-unknown`
  - `display-rejected-center-fallback-hidden`

## Summary counters asserted

Focused scenarios now assert:

- `hiddenAfterAcceptCount`
- `filteredAfterAcceptCount`
- `duplicateSuppressedCount`
- `stageSuppressedCount`
- `missingDecisionCount`
- `displayDecisionCount`
- `displayedFinalCount`

Every focused accepted completion has one display decision record, and every focused scenario asserts `missingDecisionCount=0`.

## HNL protected behavior

`brain_display_intent_honors_center_relation_fact.scn` remains unchanged functionally:

- Final display row: `HNL_02_CTR:CURRENT_POLYGON:ACTIVE`
- Decision summary: `accepted=1,decisions=1,displayedFinal=1,hiddenAfterAccept=0,filteredAfterAccept=0,duplicates=0,stageSuppressed=0,missing=0`
- Decision: `display-accepted`
- Reason: `relation-fact-applied`
- Confidence: `high`

The high-confidence accepted relation fact still outranks fallback hidden inference for the HNL class.

## Focused scenario summaries

- `brain_display_intent_non_displayable_rows_have_decisions.scn`
  - Proves offline and empty-frequency accepted rows are explicitly rejected as non-displayable.
  - Summary: `accepted=2,decisions=2,displayedFinal=0,hiddenAfterAccept=2,filteredAfterAccept=0,duplicates=0,stageSuppressed=0,missing=0`

- `brain_display_intent_duplicate_suppression_has_decision.scn`
  - Proves one duplicate displays and one duplicate is suppressed with duplicate key plus kept/dropped ids.
  - Summary: `accepted=2,decisions=2,displayedFinal=1,hiddenAfterAccept=1,filteredAfterAccept=0,duplicates=1,stageSuppressed=0,missing=0`

- `brain_display_intent_stage_deferred_rows_have_decisions.scn`
  - Proves a non-selected arrival board row and an `ARRIVAL_PREP` enroute center both become explicit stage-deferred decisions.
  - Summary: `accepted=2,decisions=2,displayedFinal=0,hiddenAfterAccept=2,filteredAfterAccept=0,duplicates=0,stageSuppressed=2,missing=0`

- `brain_display_intent_filtered_unknown_relation_rows_have_decisions.scn`
  - Proves filtered, unknown, and hidden relation facts are visible as distinct rejection decisions when the row remains hidden by existing behavior.
  - Summary: `accepted=3,decisions=3,displayedFinal=0,hiddenAfterAccept=3,filteredAfterAccept=1,duplicates=0,stageSuppressed=0,missing=0`

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
.\build\tools\XVatsimRegressionHarness.exe .\tools\regression_harness\scenarios\brain_display_intent_non_displayable_rows_have_decisions.scn
.\build\tools\XVatsimRegressionHarness.exe .\tools\regression_harness\scenarios\brain_display_intent_duplicate_suppression_has_decision.scn
.\build\tools\XVatsimRegressionHarness.exe .\tools\regression_harness\scenarios\brain_display_intent_stage_deferred_rows_have_decisions.scn
.\build\tools\XVatsimRegressionHarness.exe .\tools\regression_harness\scenarios\brain_display_intent_filtered_unknown_relation_rows_have_decisions.scn
```

Result: PASS for all five focused scenarios.

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

- The ledger remains diagnostic-only and does not yet implement weighted scoring.
- `completionStableKey` still needs an upstream stable completion identity; the current practical identity is decision id plus callsign/frequency/role/source.
- Cross-source duplicate attribution is improved for resolver-side display decisions, but final row ownership is still inferred from normalized role/callsign/frequency.
- Overlay cap / `+N more ATC` suppression is not yet represented in this ledger.
- CTAF/UNICOM completion approval remains outside this step.
