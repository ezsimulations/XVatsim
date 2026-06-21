# BrainDisplayIntent Decision Ledger Report

## Files changed

- `brain/include/XVatsim/brain/BrainDisplayIntent.h`
- `brain/src/BrainDisplayIntent.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/brain_display_intent_honors_center_relation_fact.scn`
- `tools/regression_harness/scenarios/brain_display_intent_fallback_hidden_center_has_decision.scn`
- `outputs/brain_display_intent_decision_ledger_report.md`

## Decision ledger fields added

`BrainDisplayDecisionRecord` was added to `BrainDisplayIntentOutput` with:

- stable diagnostic identifiers: `decisionId`, `completionStableKey`
- subject facts: `callsign`, `frequency`, `role`, `sourceBoard`, `workflowStage`
- relevance/relation facts: `acceptedByRelevance`, `acceptedRelation`, `relationFactPresent`, `relationFactValue`, `fallbackRelationUsed`, `fallbackRelationValue`, `finalRelation`
- route/polygon facts: `stationPolygonKey`, `currentPolygonKey`, `nextPolygonKey`, `arrivalPolygonKey`
- display outcome facts: `displayable`, `duplicateSuppressed`, `stageSuppressed`, `displayedInFinalSnapshot`
- decision text: `decision`, `reason`, `confidenceLevel`
- scoring placeholders: `positiveScore`, `negativeScore`, `hardBlock`

## Summary fields added

`BrainDisplayDecisionSummary` was added to `BrainDisplayIntentOutput` with:

- `acceptedCompletionCount`
- `displayDecisionCount`
- `displayedFinalCount`
- `hiddenAfterAcceptCount`
- `filteredAfterAcceptCount`
- `duplicateSuppressedCount`
- `stageSuppressedCount`
- `missingDecisionCount`

The regression harness now prints and can assert:

- `BrainDisplayIntentDecisionSummary`
- `BrainDisplayIntentDecisions`

## Display decisions now recorded

The ledger records diagnostic-only display outcomes beside the existing behavior:

- `display-accepted`
- `display-rejected-non-displayable`
- `display-rejected-center-fallback-hidden`
- `display-rejected-filtered`
- `display-rejected-unknown`
- `display-rejected-duplicate`
- `display-deferred-by-stage`
- `display-format-only`

The enroute center display path now records relation facts, fallback relation inference, final relation, displayability, duplicate suppression, and whether the row survived into the final snapshot. Departure/arrival board rows also record display-format, relation-fact acceptance, duplicate suppression, and non-displayable rejection.

## Behavior unchanged

The ledger is not used to build or filter final display rows. Existing append, skip, sort, and final display behavior remains the runtime authority for this step. The added records and summary are diagnostic-only.

## Focused scenario summary

- `brain_display_intent_honors_center_relation_fact.scn`
  - Final display remains `HNL_02_CTR:CURRENT_POLYGON:ACTIVE`.
  - Ledger summary: `accepted=1,decisions=1,displayedFinal=1,hiddenAfterAccept=0,filteredAfterAccept=0,duplicates=0,stageSuppressed=0,missing=0`.
  - Decision proves `HNL_02_CTR@126.500` is `display-accepted` because `relation-fact-applied` with high confidence.

- `brain_display_intent_fallback_hidden_center_has_decision.scn`
  - Final display remains empty.
  - Ledger summary: `accepted=1,decisions=1,displayedFinal=0,hiddenAfterAccept=1,filteredAfterAccept=0,duplicates=0,stageSuppressed=0,missing=0`.
  - Decision proves `HNL_02_CTR@126.500` is explicitly `display-rejected-center-fallback-hidden` because `fallback-hidden`.

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
```

Result: PASS / PASS.

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

Result: PASS, `ALL_SCENARIOS_PASSED count=271`.

## Known gaps left for later steps

- `completionStableKey` is present but not yet populated from a true upstream completion identity; records currently use best-available callsign/frequency/role/source sequencing.
- The ledger uses score placeholders only. Full weighted scoring is intentionally not implemented in this step.
- Overlay cap / `+N more ATC` rendering is not yet represented as a display decision ledger domain.
- CTAF/UNICOM completion approval remains outside this Step 23 scope.
- The ledger exposes hidden-after-accept decisions but does not yet change or clean up fallback hiding policy.
