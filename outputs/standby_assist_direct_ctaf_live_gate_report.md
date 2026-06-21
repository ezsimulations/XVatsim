# Standby Assist Direct CTAF Live Gate Report

Step 40 promotes only direct resolved CTAF advisory candidates from dry-run readiness into actual COM1 standby target selection, and only behind an explicit brain-owned product gate. UNICOM, pending CTAF, failed CTAF, and empty CTAF remain non-live and non-write-eligible.

## 1. Files changed

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- `brain/src/BrainOwnedRuntime.cpp`
- `modules/settings_store/include/XVatsim/modules/settings_store/SettingsStore.h`
- `modules/settings_store/src/SettingsStore.cpp`
- `plugin/src/XVatsimPlugin.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_live_gate_off_dry_run_only.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_live_gate_on_selects_direct_ctaf.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_live_gate_on_controller_wins.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_live_gate_on_pending_blocked.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_live_gate_on_failed_blocked.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_live_gate_on_empty_blocked.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_live_gate_on_unicom_excluded.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_live_gate_disabled_blocks_direct_ctaf.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_live_gate_already_com1_standby_blocks_direct_ctaf.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_live_gate_controller_behavior_unchanged.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_live_gate_controller_latch_unchanged.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_live_gate_write_diagnostics_source.scn`
- `outputs/standby_assist_direct_ctaf_live_gate_report.md`

## 2. Product gate added and default state

Added `directCtafStandbyAssistEnabled`, owned by the brain standby planner input and defaulting to `false`.

Settings-store keys accepted:

- `direct_ctaf_standby_assist`
- `standby_assist_direct_ctaf`

Harness key accepted:

- `standby_assist.direct_ctaf_enabled`

With the gate off, direct CTAF remains Step 39 dry-run only.

## 3. Direct CTAF live promotion conditions

A CTAF advisory candidate can become an actual live target only when all of these pass:

- Existing standby assist is enabled.
- `directCtafStandbyAssistEnabled` is enabled.
- Candidate type is `direct-ctaf`.
- Frequency resolution state is `resolved-direct-ctaf`.
- Frequency is valid and non-empty.
- Step 39 dry-run readiness passes.
- No controller standby target is already selected.
- The candidate would not displace a controller target.
- Candidate is not COM1 active, COM2 active, or already COM1 standby.
- Existing latch/write side-effect rules allow the write.
- Target remains `COM1_STANDBY`.

## 4. Ledger fields added or reused

The standby ledger now exposes:

- `productGateEnabled`
- `directCtafLivePromotionAllowed`
- `livePromotionReason`
- `livePromotionBlockedReason`
- `promotedFromDryRun`
- `actualSelectedTargetSource`
- `actualSelectedTargetFrequency`
- `actualWriteEligible`
- `noControllerTargetAvailable`
- `controllerTargetPreserved`

The side-effect decision also carries actual write diagnostics:

- `actualSelectedTargetSource`
- `actualSelectedTargetFrequency`
- `actualWriteEligible`
- `actualWriteAttempted`
- `actualWriteSucceededKnown`
- `actualWriteSucceeded`

The harness prints those in a separate `StandbyAssistSideEffectActualSummary` line so existing side-effect summary expectations stay stable.

## 5. Gate OFF behavior

Valid direct CTAF remains visible as:

- `previewRecommendation=preview-recommend-com1-standby`
- `dryRunLiveEligible=1`
- `productGateEnabled=0`
- `directCtafLivePromotionAllowed=0`
- `livePromotionBlockedReason=product-gate-disabled`
- `actualSelectedTargetSource=none`
- `actualWriteEligible=0`

No live target is selected and no write is attempted.

## 6. Gate ON valid direct CTAF behavior

With no controller target selected, valid direct CTAF may become:

- `directCtafLivePromotionAllowed=1`
- `livePromotionReason=promoted-direct-ctaf`
- `promotedFromDryRun=1`
- `actualSelectedTargetSource=direct-ctaf-advisory`
- `actualSelectedTargetFrequency=122.950`
- `actualWriteEligible=1`
- `targetCom=COM1_STANDBY`
- `final=recommend-com1-standby`

The side-effect path remains the existing COM1 standby write path.

## 7. Controller non-displacement behavior

When an existing controller standby target is selected, the controller remains selected and direct CTAF records:

- `directCtafLivePromotionAllowed=0`
- `livePromotionBlockedReason=existing-controller-target`
- `controllerTargetPreserved=1`
- `dryRunBlockedByExistingControllerTarget=1`

Direct CTAF does not displace controller targets.

## 8. Pending/failed/empty CTAF protection behavior

- Pending lookup records `livePromotionBlockedReason=skip-pending-lookup` and remains non-writeable.
- Failed lookup records `livePromotionBlockedReason=skip-lookup-failed` and remains non-writeable.
- Empty frequency records `livePromotionBlockedReason=skip-empty-frequency`, stays hard-blocked, and remains non-writeable.

These reasons take priority over final-board visibility so the ledger shows the actual safety block.

## 9. UNICOM fallback exclusion behavior

UNICOM fallback remains excluded from direct CTAF promotion:

- `directCtafLivePromotionAllowed=0`
- `livePromotionBlockedReason=unicom-excluded`
- `dryRunLiveRecommendation=dry-run-excluded-unicom`
- `liveWriteEligible=0`

UNICOM is still product-gated and not live-write eligible.

## 10. What runtime behavior changed

Only this changed: when existing standby assist is enabled, the direct CTAF product gate is enabled, no controller standby target exists, and all Step 39 dry-run gates pass, a valid direct resolved CTAF advisory can become the actual COM1 standby target.

## 11. What runtime behavior remains unchanged

- Gate-off behavior remains Step 39 dry-run only.
- Existing controller target selection remains unchanged.
- Existing controller write/latch behavior remains unchanged.
- Direct CTAF cannot displace a controller target.
- Pending/failed/empty CTAF cannot become live eligible.
- UNICOM fallback cannot become live eligible.
- COM writer behavior is unchanged.
- CTAF/UNICOM live projection is unchanged.
- `BrainDisplayIntent`, `transceiver_resolver`, `route_sector`, and HNL were not modified for this step.

## 12. Focused scenario summaries

- Gate off: valid direct CTAF stays dry-run only and no write occurs.
- Gate on, no controller target: valid direct CTAF becomes selected COM1 standby target.
- Gate on, controller target present: controller target wins and CTAF is blocked as non-displacing.
- Gate on, pending CTAF: blocked with pending lookup reason.
- Gate on, failed CTAF: blocked with lookup failed reason.
- Gate on, empty CTAF: blocked with empty frequency reason.
- Gate on, UNICOM fallback: excluded and non-writeable.
- Standby disabled: direct CTAF promotion/write is blocked.
- Already COM1 standby: direct CTAF promotion/write is blocked.
- Existing controller behavior: controller selection/write still succeeds unchanged.
- Existing controller latch: already-COM1-standby controller latch/write block remains unchanged.
- Direct CTAF write diagnostics: side-effect diagnostics identify `direct-ctaf-advisory` source and actual write result.

## 13. Build command/result

Command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: Passed.

## 14. Focused scenario command/result

Command:

```powershell
$scenarios = @(
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_live_gate_off_dry_run_only.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_live_gate_on_selects_direct_ctaf.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_live_gate_on_controller_wins.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_live_gate_on_pending_blocked.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_live_gate_on_failed_blocked.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_live_gate_on_empty_blocked.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_live_gate_on_unicom_excluded.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_live_gate_disabled_blocks_direct_ctaf.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_live_gate_already_com1_standby_blocks_direct_ctaf.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_live_gate_controller_behavior_unchanged.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_live_gate_controller_latch_unchanged.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_live_gate_write_diagnostics_source.scn'
)
foreach ($scenario in $scenarios) {
  & '.\build\tools\XVatsimRegressionHarness.exe' $scenario *> $null
  if ($LASTEXITCODE -ne 0) { Write-Output "failed=$scenario"; exit $LASTEXITCODE }
}
'passed=' + $scenarios.Count
```

Result: `passed=12`.

## 15. Full saved regression command/result

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

Result: `passed=314`.

## 16. Known gaps after direct CTAF live wiring

- UNICOM fallback remains excluded from live standby assist.
- Pending/failed/empty CTAF remain blocked and need no live wiring until a later gated step.
- No broader UI surface was added for the direct CTAF gate beyond persisted settings keys and harness controls.
- Direct CTAF remains constrained to COM1 standby and cannot displace controller targets.
- Future steps can decide whether and how to productize UNICOM or broader advisory standby behavior.
