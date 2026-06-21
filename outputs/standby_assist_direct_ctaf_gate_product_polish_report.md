# Direct CTAF Standby Assist Gate Product Polish Report

Step 42 adds product-facing documentation and diagnostics around the default-off direct CTAF standby assist gate. Runtime standby behavior remains unchanged from Step 41.

## 1. Files changed

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- `brain/src/BrainOwnedRuntime.cpp`
- `modules/settings_store/include/XVatsim/modules/settings_store/SettingsStore.h`
- `modules/settings_store/src/SettingsStore.cpp`
- `plugin/src/XVatsimPlugin.cpp`
- `tools/regression_harness/src/main.cpp`
- `docs/STANDBY_ASSIST_DIRECT_CTAF_GATE.md`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_gate_polish_missing_setting_default_off.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_gate_polish_standby_assist_only_no_write.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_gate_polish_direct_gate_on_standby_disabled.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_gate_polish_both_gates_on_direct_ctaf_write.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_gate_polish_controller_target_preserved.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_gate_polish_pending_blocked.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_gate_polish_failed_blocked.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_gate_polish_empty_blocked.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_gate_polish_unicom_excluded.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_gate_polish_settings_source_default.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_gate_polish_settings_source_settings_store.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_gate_polish_settings_source_harness.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_gate_polish_writer_controller_source.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_gate_polish_writer_direct_ctaf_source.scn`
- `outputs/standby_assist_direct_ctaf_gate_product_polish_report.md`

## 2. Documentation added

Added `docs/STANDBY_ASSIST_DIRECT_CTAF_GATE.md`.

The doc explains:

- The gate defaults off.
- `standby_assist=true` alone does not enable direct CTAF writes.
- Both standby assist and direct CTAF standby assist must be enabled.
- Only direct resolved CTAF can be affected.
- UNICOM, pending CTAF, failed CTAF, and empty CTAF remain excluded.
- Controller targets are never displaced.
- COM writer behavior is unchanged.
- Settings keys and diagnostics.

## 3. Settings keys and default behavior

Settings keys:

- `standby_assist=true`
- `direct_ctaf_standby_assist=true`
- `standby_assist_direct_ctaf=true`

If the direct CTAF setting is missing, `directCtafStandbyAssistEnabled=false` and `directCtafGateSource=default`.

## 4. Settings diagnostics added

Added `BrainOwnedStandbyAssistSettingsDiagnostics`:

- `standbyAssistEnabled`
- `directCtafStandbyAssistEnabled`
- `directCtafGateSource`
- `directCtafGateEffective`

Valid sources:

- `default`
- `settings-store`
- `harness`
- `unknown`

The plugin summary and harness output now include these diagnostics.

## 5. Feature gate ledger fields added

Added standby decision fields:

- `featureGateRequired`
- `featureGateSatisfied`
- `featureGateBlockedReason`

Direct CTAF advisory rows use `featureGateRequired=direct-ctaf-standby-assist`. Controller display rows use `featureGateRequired=not-required`.

## 6. Proof that standby_assist=true alone does not enable direct CTAF

Focused scenarios prove valid direct CTAF remains dry-run/no-write with:

- `standbyAssistEnabled=1`
- `directCtafStandbyAssistEnabled=0`
- `directCtafGateEffective=0`
- `featureGateSatisfied=0`
- `featureGateBlockedReason=product-gate-disabled`
- writer result `no-write-requested`

## 7. Proof that both gates are required

Focused scenarios prove:

- Direct CTAF gate off plus standby assist on: no write.
- Direct CTAF gate on plus standby assist off: no write.
- Both gates on plus valid direct CTAF with no controller target: direct CTAF may write through the existing COM1 standby path.

## 8. Runtime behavior changed

No standby target selection or COM writer behavior changed. Step 42 adds diagnostics, settings-source tracking, feature-gate ledger fields, and documentation only.

## 9. Runtime behavior unchanged

- Existing controller standby behavior is unchanged.
- COM writer behavior is unchanged.
- Direct CTAF promotion rules are unchanged from Step 40/41.
- Direct CTAF still defaults off.
- UNICOM is still excluded.
- Pending/failed/empty CTAF remain blocked.
- Direct CTAF still cannot displace controller targets.
- CTAF/UNICOM live projection is unchanged.
- `BrainDisplayIntent`, `transceiver_resolver`, `route_sector`, and HNL were not modified.

## 10. Focused scenario summaries

- Missing setting defaults direct CTAF gate off.
- `standby_assist=true` alone keeps direct CTAF dry-run/no-write.
- Direct CTAF gate on with standby assist off remains no-write.
- Both gates on allows valid direct CTAF to use the existing write path.
- Both gates on with a controller target preserves the controller target.
- Pending CTAF remains blocked.
- Failed CTAF remains blocked.
- Empty CTAF remains blocked.
- UNICOM fallback remains excluded.
- Settings diagnostics prove `default`, `settings-store`, and `harness` sources.
- Controller writer diagnostics remain present.
- Direct CTAF writer diagnostics remain present when selected.

## 11. Build command/result

Command:

```powershell
& 'C:\Users\DARRON\Documents\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: Passed.

## 12. Focused scenario command/result

Command:

```powershell
$scenarios = @(
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_gate_polish_missing_setting_default_off.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_gate_polish_standby_assist_only_no_write.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_gate_polish_direct_gate_on_standby_disabled.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_gate_polish_both_gates_on_direct_ctaf_write.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_gate_polish_controller_target_preserved.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_gate_polish_pending_blocked.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_gate_polish_failed_blocked.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_gate_polish_empty_blocked.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_gate_polish_unicom_excluded.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_gate_polish_settings_source_default.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_gate_polish_settings_source_settings_store.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_gate_polish_settings_source_harness.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_gate_polish_writer_controller_source.scn',
'.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_gate_polish_writer_direct_ctaf_source.scn'
)
foreach ($scenario in $scenarios) {
  & '.\build\tools\XVatsimRegressionHarness.exe' $scenario
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
'passed=' + $scenarios.Count
```

Result: `passed=14`.

## 13. Full saved regression command/result

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

Result: `passed=340`.

## 14. Known gaps after product polish

- There is still no in-plugin menu/UI toggle for direct CTAF standby assist.
- Direct CTAF remains COM1 standby only.
- UNICOM remains excluded from live standby assist.
- Pending/failed/empty CTAF remain blocked until a later explicitly gated step changes that.
