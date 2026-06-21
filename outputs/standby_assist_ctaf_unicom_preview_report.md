# Standby Assist CTAF/UNICOM Preview Report

## 1. Files changed

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- `brain/src/BrainOwnedRuntime.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/standby_assist_ctaf_unicom_preview_controller_target_unchanged.scn`
- `tools/regression_harness/scenarios/standby_assist_ctaf_unicom_preview_direct_ctaf_recommendable.scn`
- `tools/regression_harness/scenarios/standby_assist_ctaf_unicom_preview_pending_ctaf_not_ready.scn`
- `tools/regression_harness/scenarios/standby_assist_ctaf_unicom_preview_failed_ctaf_not_ready.scn`
- `tools/regression_harness/scenarios/standby_assist_ctaf_unicom_preview_empty_ctaf_blocked.scn`
- `tools/regression_harness/scenarios/standby_assist_ctaf_unicom_preview_unicom_product_gated.scn`
- `tools/regression_harness/scenarios/standby_assist_ctaf_unicom_preview_disabled_blocks_controller_write.scn`
- `tools/regression_harness/scenarios/standby_assist_ctaf_unicom_preview_already_com1_standby_blocks_controller_write.scn`
- `outputs/standby_assist_ctaf_unicom_preview_report.md`

No Step 38 edits were made to `BrainDisplayIntent`, `transceiver_resolver`, `route_sector`, HNL, CTAF/UNICOM live projection, or COM writer behavior.

## 2. New or changed preview ledger fields

Standby recommendation decisions now include:

- `previewEligible`
- `previewRecommendation`
- `previewSkipReason`
- `liveWriteEligible`
- `advisoryProductGate`
- `advisoryWritePolicy`
- `advisoryFrequencyResolutionState`
- `advisoryCandidateType`

The harness standby decision summaries also expose `liveEligible` as an explicit alias for the existing live planner `eligible` bit.

## 3. Eligibility separation

- Live eligibility remains the existing controller/display-row planner eligibility path. CTAF/UNICOM advisory candidates keep `eligible=false` and `liveEligible=0`.
- Preview eligibility is advisory-only diagnostic evaluation. Direct resolved CTAF can become `previewEligible=1` with `preview-recommend-com1-standby`.
- Write eligibility is separate as `liveWriteEligible`. Advisory candidates always remain `liveWriteEligible=0`.
- Side-effect write permission remains in the existing side-effect decision fields: `writeAllowed`, `writeAttempted`, `failureReason`, and the existing latch behavior.

## 4. CTAF direct-frequency preview behavior

Direct CTAF with a valid resolved frequency is now visible as:

- `advisoryCandidateType=direct-ctaf`
- `advisoryFrequencyResolutionState=resolved-direct-ctaf`
- `previewEligible=1`
- `previewRecommendation=preview-recommend-com1-standby`
- `advisoryWritePolicy=preview-only-no-write`
- `liveWriteEligible=0`

The live recommendation remains non-writeable for Step 38.

## 5. CTAF pending/failed/empty protection behavior

- Pending lookup: `previewRecommendation=preview-not-ready`, `previewSkipReason=skip-pending-lookup`, `liveWriteEligible=0`.
- Failed lookup: `previewRecommendation=preview-not-ready`, `previewSkipReason=skip-lookup-failed`, `liveWriteEligible=0`.
- Empty frequency: `previewRecommendation=preview-blocked`, `previewSkipReason=skip-empty-frequency`, `advisoryWritePolicy=blocked-no-write`, `liveWriteEligible=0`.

## 6. UNICOM fallback product-gate behavior

UNICOM fallback is visible as:

- `advisoryCandidateType=unicom-fallback`
- `advisoryFrequencyResolutionState=resolved-unicom-fallback`
- `previewRecommendation=preview-product-gated`
- `previewSkipReason=product-decision-required`
- `advisoryProductGate=product-decision-required`
- `advisoryWritePolicy=product-gated-no-write`
- `liveWriteEligible=0`

It remains recommendation-only and cannot become a live write target in this step.

## 7. Runtime behavior unchanged

- Existing controller standby target selection is unchanged.
- Existing live selected target is unchanged.
- No CTAF/UNICOM advisory candidate can become a live standby target.
- No CTAF/UNICOM advisory candidate can trigger a COM write.
- UNICOM fallback is not live-write eligible.
- CTAF/UNICOM live projection is unchanged.
- COM write behavior and latch behavior are unchanged.

## 8. Focused scenario summaries

- Controller target unchanged: existing `NCT_APP 125.350` controller target remains selected and live-write eligible.
- Direct CTAF valid frequency: CTAF appears as `preview-recommend-com1-standby` but `liveWriteEligible=0`.
- Pending CTAF lookup: preview is not ready and write eligibility remains blocked.
- Failed CTAF lookup: explicit lookup-failed preview skip is recorded and write eligibility remains blocked.
- Empty CTAF frequency: frequency is hard-blocked/skipped and never preview/live write eligible.
- UNICOM fallback: advisory is product-gated and remains non-writeable.
- Standby disabled: existing controller side-effect decision blocks write with `standby-assist-disabled`.
- Already COM1 standby: existing controller side-effect decision blocks write with `already-com1-standby`.

## 9. Build command/result

Command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: Passed.

## 10. Focused scenario command/result

Command:

```powershell
$scenarios = @(
'.\tools\regression_harness\scenarios\standby_assist_ctaf_unicom_preview_controller_target_unchanged.scn',
'.\tools\regression_harness\scenarios\standby_assist_ctaf_unicom_preview_direct_ctaf_recommendable.scn',
'.\tools\regression_harness\scenarios\standby_assist_ctaf_unicom_preview_pending_ctaf_not_ready.scn',
'.\tools\regression_harness\scenarios\standby_assist_ctaf_unicom_preview_failed_ctaf_not_ready.scn',
'.\tools\regression_harness\scenarios\standby_assist_ctaf_unicom_preview_empty_ctaf_blocked.scn',
'.\tools\regression_harness\scenarios\standby_assist_ctaf_unicom_preview_unicom_product_gated.scn',
'.\tools\regression_harness\scenarios\standby_assist_ctaf_unicom_preview_disabled_blocks_controller_write.scn',
'.\tools\regression_harness\scenarios\standby_assist_ctaf_unicom_preview_already_com1_standby_blocks_controller_write.scn'
)
foreach ($scenario in $scenarios) {
  & '.\build\tools\XVatsimRegressionHarness.exe' $scenario
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
```

Result: Passed, 8 focused Step 38 scenarios.

## 11. Full saved regression command/result

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

Result: `passed=292`.

## 12. Known gaps left before live wiring

- CTAF/UNICOM advisory candidates are still not live standby targets.
- CTAF direct-frequency preview does not write radios.
- UNICOM fallback still requires a later product decision before any live standby eligibility.
- Pending, failed, and empty CTAF lookup paths remain diagnostic blocks only.
- A later gated step must define if and when direct CTAF preview recommendations can become live COM write candidates.
