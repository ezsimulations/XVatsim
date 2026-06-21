# Standby Assist Decision Ledger Report

## 1. Files changed

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- `brain/src/BrainOwnedRuntime.cpp`
- `plugin/src/XVatsimPlugin.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/standby_assist_decision_ledger_controller_target_unchanged.scn`
- `tools/regression_harness/scenarios/standby_assist_decision_ledger_ctaf_advisory_skipped.scn`
- `tools/regression_harness/scenarios/standby_assist_decision_ledger_unicom_advisory_skipped.scn`
- `tools/regression_harness/scenarios/standby_assist_decision_ledger_pending_failed_ctaf_never_write.scn`
- `tools/regression_harness/scenarios/standby_assist_decision_ledger_disabled_blocks_write.scn`
- `tools/regression_harness/scenarios/standby_assist_decision_ledger_already_com1_standby_blocks_write.scn`
- `outputs/standby_assist_decision_ledger_report.md`

No Step 37 edits were made to `BrainDisplayIntent`, `transceiver_resolver`, `route_sector`, or HNL.

## 2. Standby decision ledger fields added

Brain-owned standby recommendation records now carry:

- `standbyDecisionId`
- `subjectKey`
- `sourceDomain`
- `sourceDecisionId`
- `sourceEvidenceId`
- `endpoint`
- `airportIcao`
- `callsign`
- `role`
- `frequency`
- `workflowStage`
- `planKey`
- `boardIndex`
- `displayRelation`
- `candidateVisibleInFinalBoard`
- `acceptedByAdvisory`
- `advisoryDecision`
- `sourceConfidence`
- `confidenceLevel`
- `fallbackUsed`
- `positiveScore`
- `negativeScore`
- `hardBlock`
- `hardBlockReason`
- `alreadyCom1Active`
- `alreadyCom2Active`
- `alreadyCom1Standby`
- `targetCom`
- `eligible`
- `skipReason`
- `finalRecommendation`

Supported recommendation values include `recommend-com1-standby`, `skip-active`, `skip-already-standby`, `skip-role-not-eligible`, `skip-empty-frequency`, `skip-guard-frequency`, `skip-pending-lookup`, `skip-lookup-failed`, `skip-fallback-lower-confidence`, `skip-duplicate`, `skip-stage-deferred`, `needs-more-evidence`, and `no-target`.

## 3. Side-effect/write diagnostic fields added

The standby side-effect decision now exposes:

- `sideEffectDecisionId`
- `standbyDecisionId`
- `standbyAssistEnabled`
- `latchKey`
- `latchConsumed`
- `writeAllowed`
- `writeAttempted`
- `writeSucceededKnown`
- `writeSucceeded`
- `writerTarget`
- `targetFrequency`
- `failureReason`
- `displayStandbyMarkerApplied`

The plugin records the post-write result through the brain-owned completion helper after the existing COM1 standby write path runs.

## 4. Summary fields added

The standby assist summary now exposes:

- `standbyEvidenceCount`
- `standbyCandidateCount`
- `advisoryCandidateCount`
- `selectedTargetCount`
- `writeDecisionCount`
- `writeAttemptCount`
- `writeSuccessCount`
- `writeFailureCount`
- `skippedEmptyFrequencyCount`
- `skippedPendingLookupCount`
- `skippedLookupFailedCount`
- `skippedGuardFrequencyCount`
- `skippedRoleNotEligibleCount`
- `skippedAlreadyActiveCount`
- `standbyRecommendationsBrainOwned`

## 5. Runtime behavior unchanged

- Existing controller standby target selection is preserved.
- CTAF/UNICOM advisory facts are visible to standby diagnostics only.
- CTAF/UNICOM advisory candidates remain non-live and non-write-eligible for standby assist.
- The plugin does not decide standby eligibility, skip reasons, recommendations, selected targets, or write permission.
- COM write behavior is unchanged and still follows the existing COM1 standby write path only when the brain side-effect decision allows it.
- CTAF/UNICOM live projection behavior is unchanged.
- BrainDisplayIntent behavior is unchanged by Step 37.

## 6. Focused scenario summaries

- Controller target unchanged: existing controller row selection still recommends the same COM1 standby target.
- CTAF advisory skipped: CTAF is visible in the standby ledger and skipped with `skip-role-not-eligible`.
- UNICOM fallback skipped: UNICOM fallback is visible in the standby ledger and skipped with `skip-role-not-eligible`.
- Pending/failed CTAF lookup: pending and failed advisory facts are visible, skipped with pending/failure reasons, and never write eligible.
- Standby disabled: a valid controller target remains selected, but the side-effect decision blocks the write with `standby-assist-disabled`.
- Already COM1 standby: a valid controller target remains selected, but the side-effect decision blocks the write with `already-com1-standby`.

## 7. Build command/result

Command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: Passed.

## 8. Focused scenario command/result

Command:

```powershell
$scenarios = @(
'.\tools\regression_harness\scenarios\standby_assist_decision_ledger_controller_target_unchanged.scn',
'.\tools\regression_harness\scenarios\standby_assist_decision_ledger_ctaf_advisory_skipped.scn',
'.\tools\regression_harness\scenarios\standby_assist_decision_ledger_unicom_advisory_skipped.scn',
'.\tools\regression_harness\scenarios\standby_assist_decision_ledger_pending_failed_ctaf_never_write.scn',
'.\tools\regression_harness\scenarios\standby_assist_decision_ledger_disabled_blocks_write.scn',
'.\tools\regression_harness\scenarios\standby_assist_decision_ledger_already_com1_standby_blocks_write.scn'
)
foreach ($scenario in $scenarios) {
  & '.\build\tools\XVatsimRegressionHarness.exe' $scenario
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
```

Result: Passed, 6 focused standby assist ledger scenarios.

## 9. Full saved regression command/result

Command:

```powershell
$failed = @()
$count = 0
Get-ChildItem '.\tools\regression_harness\scenarios' -Filter '*.scn' | Sort-Object Name | ForEach-Object {
  $count++
  & '.\build\tools\XVatsimRegressionHarness.exe' $_.FullName *> $null
  if ($LASTEXITCODE -ne 0) { $failed += $_.Name }
}
if ($failed.Count -eq 0) {
  "passed=$count"
} else {
  "failed=$($failed.Count)/$count " + ($failed -join ',')
  exit 1
}
```

Result: `passed=284`.

## 10. Known gaps left for later steps

- CTAF/UNICOM advisory candidates are not wired into live standby assist eligibility.
- CTAF/UNICOM advisory candidates cannot become selected standby targets yet.
- No new COM write situations were added.
- The ledger may reveal lower-confidence or incomplete advisory reasoning, but those records remain diagnostic-only for Step 37.
