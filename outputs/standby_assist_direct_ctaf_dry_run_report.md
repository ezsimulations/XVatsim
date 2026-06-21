# Standby Assist Direct CTAF Dry-Run Report

## 1. Files changed

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- `brain/src/BrainOwnedRuntime.cpp`
- `plugin/src/XVatsimPlugin.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_dry_run_controller_target_unchanged.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_dry_run_ready_no_controller.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_dry_run_controller_non_displacement.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_dry_run_pending_blocked.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_dry_run_failed_blocked.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_dry_run_empty_blocked.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_dry_run_unicom_excluded.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_dry_run_disabled_blocked.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_dry_run_already_com1_standby_blocked.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_dry_run_controller_write_side_effect_unchanged.scn`
- `outputs/standby_assist_direct_ctaf_dry_run_report.md`

No Step 39 edits were made to `BrainDisplayIntent`, `transceiver_resolver`, `route_sector`, HNL, CTAF/UNICOM live projection, or COM writer behavior.

## 2. New or changed dry-run ledger fields

Standby recommendation decisions now include:

- `dryRunLiveEligible`
- `dryRunLiveRecommendation`
- `dryRunSkipReason`
- `dryRunSafetyGate`
- `dryRunWouldSelectTarget`
- `dryRunWouldDisplaceControllerTarget`
- `dryRunBlockedByExistingControllerTarget`
- `dryRunBlockedByStandbyDisabled`
- `dryRunBlockedByAlreadyCom1Standby`
- `dryRunBlockedByFrequencyState`
- `dryRunTargetCom`
- `dryRunTargetFrequency`
- `dryRunPromotionClass`

The planner input also carries the existing `standbyAssistEnabled` setting as diagnostic context so dry-run readiness can explain standby-disabled blocks without changing side-effect behavior.

## 3. Separation model

- Preview recommendation remains the Step 38 advisory preview: `previewEligible`, `previewRecommendation`, and `previewSkipReason`.
- Dry-run readiness is direct-CTAF-only diagnostic proof: `dryRunLiveEligible`, `dryRunLiveRecommendation`, and related dry-run block fields.
- Actual live eligibility remains the existing planner bit: `eligible` / harness `liveEligible`. CTAF/UNICOM advisory records stay `false`.
- Actual write eligibility remains `liveWriteEligible`. CTAF/UNICOM advisory records stay `false`.
- Side-effect write permission remains the side-effect decision: `writeAllowed`, `writeAttempted`, latch fields, writer target, and failure reason.

## 4. Direct CTAF valid-frequency dry-run behavior

When a direct CTAF advisory has a resolved valid frequency and no existing controller target would be displaced, the ledger records:

- `previewRecommendation=preview-recommend-com1-standby`
- `dryRunLiveEligible=1`
- `dryRunLiveRecommendation=dry-run-recommend-com1-standby`
- `dryRunSafetyGate=pass`
- `dryRunWouldSelectTarget=1`
- `dryRunTargetCom=COM1_STANDBY`
- `dryRunPromotionClass=direct-ctaf-only`

It still remains actual `liveEligible=0`, `liveWriteEligible=0`, and side-effect `no-target`.

## 5. Direct CTAF pending/failed/empty protection behavior

- Pending lookup: `dryRunLiveRecommendation=dry-run-not-ready`, `dryRunSkip=skip-pending-lookup`, `dryRunBlockedByFrequencyState=1`.
- Failed lookup: `dryRunLiveRecommendation=dry-run-not-ready`, `dryRunSkip=skip-lookup-failed`, `dryRunBlockedByFrequencyState=1`.
- Empty frequency: `dryRunLiveRecommendation=dry-run-blocked-frequency-state`, `dryRunSkip=skip-empty-frequency`, `dryRunBlockedByFrequencyState=1`.

All remain actual live/write ineligible.

## 6. UNICOM fallback exclusion behavior

UNICOM fallback records are explicitly excluded:

- `dryRunLiveRecommendation=dry-run-excluded-unicom`
- `dryRunSkip=unicom-excluded`
- `dryRunSafetyGate=unicom-excluded`
- `dryRunPromotionClass=unicom-excluded`
- `liveWriteEligible=0`

UNICOM remains product-gated and outside direct-CTAF promotion.

## 7. Controller non-displacement behavior

When an existing controller standby target is selected, direct CTAF dry-run records show:

- `dryRunLiveEligible=0`
- `dryRunLiveRecommendation=dry-run-blocked-existing-controller-target`
- `dryRunBlockedByExistingControllerTarget=1`
- `dryRunWouldDisplaceControllerTarget=0`

The existing controller target remains the selected live target.

## 8. Runtime behavior unchanged

- Existing controller standby target selection is unchanged.
- Direct CTAF is not an actual selected live standby target.
- CTAF/UNICOM advisory candidates cannot write radios.
- UNICOM fallback remains non-live and non-write-eligible.
- COM write behavior and latch behavior are unchanged.
- CTAF/UNICOM live projection is unchanged.

## 9. Focused scenario summaries

- Controller target unchanged: existing controller row remains the standby target.
- Direct CTAF ready/no controller: valid CTAF is dry-run ready, but actual live/write ineligible.
- Controller non-displacement: valid CTAF is blocked by existing controller target and does not displace it.
- Pending CTAF: dry-run blocked by frequency state.
- Failed CTAF: dry-run blocked with lookup-failed reason.
- Empty CTAF: dry-run blocked by frequency state and empty frequency.
- UNICOM fallback: excluded from direct-CTAF promotion.
- Standby disabled: dry-run blocked by standby-disabled gate.
- Already COM1 standby: dry-run blocked by already-COM1-standby gate.
- Controller write side-effect: existing controller write path still writes only through the existing side-effect decision.

## 10. Build command/result

Command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: Passed.

## 11. Focused scenario command/result

Command:

```powershell
$scenarios = @(
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_dry_run_controller_target_unchanged.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_dry_run_ready_no_controller.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_dry_run_controller_non_displacement.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_dry_run_pending_blocked.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_dry_run_failed_blocked.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_dry_run_empty_blocked.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_dry_run_unicom_excluded.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_dry_run_disabled_blocked.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_dry_run_already_com1_standby_blocked.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_dry_run_controller_write_side_effect_unchanged.scn'
)
foreach ($scenario in $scenarios) {
  & '.\build\tools\XVatsimRegressionHarness.exe' $scenario
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
```

Result: Passed, 10 focused Step 39 scenarios.

## 12. Full saved regression command/result

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

Result: `passed=302`.

## 13. Known gaps before opt-in live wiring

- Direct CTAF dry-run readiness does not promote CTAF to live standby target selection.
- No CTAF/UNICOM advisory candidate can write COM1 standby.
- A later opt-in step must define the product gate for direct CTAF live promotion.
- UNICOM fallback still needs a separate product decision before any live standby eligibility.
- Dry-run readiness currently proves safety and non-displacement; it does not alter the live target ordering algorithm.
