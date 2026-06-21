# Standby Assist Writer Result Ledger Report

Step 41 adds structured brain-owned COM1 standby writer result diagnostics around the existing standby assist side-effect path. The COM writer call, dataref names, write timing, write format, and write eligibility gates are unchanged.

## 1. Files changed

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- `brain/src/BrainOwnedRuntime.cpp`
- `plugin/src/XVatsimPlugin.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/standby_assist_writer_result_controller_success.scn`
- `tools/regression_harness/scenarios/standby_assist_writer_result_controller_no_target.scn`
- `tools/regression_harness/scenarios/standby_assist_writer_result_disabled_blocks_controller.scn`
- `tools/regression_harness/scenarios/standby_assist_writer_result_already_com1_standby_blocks_controller.scn`
- `tools/regression_harness/scenarios/standby_assist_writer_result_direct_ctaf_gate_off_no_writer.scn`
- `tools/regression_harness/scenarios/standby_assist_writer_result_direct_ctaf_gate_on_success.scn`
- `tools/regression_harness/scenarios/standby_assist_writer_result_pending_ctaf_no_writer.scn`
- `tools/regression_harness/scenarios/standby_assist_writer_result_failed_ctaf_no_writer.scn`
- `tools/regression_harness/scenarios/standby_assist_writer_result_empty_ctaf_no_writer.scn`
- `tools/regression_harness/scenarios/standby_assist_writer_result_unicom_no_writer.scn`
- `tools/regression_harness/scenarios/standby_assist_writer_result_dataref_not_writable.scn`
- `tools/regression_harness/scenarios/standby_assist_writer_result_controller_latch_unchanged.scn`
- `outputs/standby_assist_writer_result_ledger_report.md`

## 2. Writer result diagnostic fields added

Added `BrainOwnedStandbyAssistWriterResult` with:

- `writerResultKnown`
- `writerResultCode`
- `writerFailureReason`
- `writerFailureDomain`
- `writerInputFrequency`
- `writerNormalizedFrequency`
- `writerTargetCom`
- `writerDatarefName`
- `writerDatarefAvailable`
- `writerDatarefWritable`
- `writerValidationPassed`
- `writerWriteAttempted`
- `writerWriteSucceeded`
- `writerWriteBlockedBeforeSimWrite`
- `writerWriteFailedAtSimLayer`
- `writerResultSource`
- `writerResultDecisionId`
- `writerResultLinkedStandbyDecisionId`

Supported result codes include the requested no-write, validation, dataref, sim, success, and unknown states.

## 3. Writer result summary counters added

Added counters to the standby recommendation summary:

- `writerResultCount`
- `writerSuccessCount`
- `writerFailureCount`
- `writerBlockedBeforeWriteCount`
- `writerUnknownResultCount`
- `writerDatarefMissingCount`
- `writerDatarefNotWritableCount`
- `writerInvalidFrequencyCount`
- `writerNoTargetCount`
- `writerNoWriteRequestedCount`
- `writerControllerSourceCount`
- `writerDirectCtafSourceCount`

The harness prints these as `StandbyAssistWriterCounterSummary`.

## 4. Brain-owned/plugin-shell split

The brain owns writer-result classification through:

- `BuildBrainOwnedStandbyAssistWriterResult`
- `BuildBrainOwnedStandbyAssistWriterResultFromCode`
- the writer-result completion overload for `CompleteBrainOwnedStandbyAssistSideEffectDecision`

The plugin still only executes the already-approved write:

```cpp
if (sideEffectDecision.shouldWriteCom1Standby) {
    gRadioStateSampler.SetCom1StandbyFrequency(...);
}
```

It passes the boolean writer outcome back into the brain-owned result builder. It does not decide standby eligibility, selected target, write permission, or writer result semantics.

## 5. Writer behavior unchanged

- `RadioStateSampler::SetCom1StandbyFrequency` behavior is preserved.
- Dataref names, write format, validation rules, and write timing are unchanged.
- No new COM write situations were added.
- Existing controller write and latch behavior is unchanged.
- Direct CTAF gate behavior from Step 40 is unchanged.
- Pending/failed/empty CTAF and UNICOM do not reach the writer.

## 6. Controller writer result behavior

Controller success records:

- `writerResultCode=write-succeeded`
- `writerResultSource=controller-display-row`
- `writerInputFrequency` and normalized frequency
- `writerTargetCom=COM1_STANDBY`
- writer success counter incremented

Controller pre-write blocks record `write-not-allowed` with explicit reasons such as `standby-assist-disabled` or `already-com1-standby`.

## 7. Direct CTAF writer result behavior

Gate-off direct CTAF remains no-write with `writerResultCode=no-write-requested` and `writerFailureReason=product-gate-disabled`.

Gate-on valid direct CTAF uses the same writer result ledger as controller targets and records `writerResultSource=direct-ctaf-advisory` on success.

## 8. Pending/failed/empty CTAF no-writer behavior

- Pending CTAF: `no-write-requested`, `skip-pending-lookup`.
- Failed CTAF: `no-write-requested`, `skip-lookup-failed`.
- Empty CTAF: `empty-frequency`, `skip-empty-frequency`, validation blocked before write.

None of these reaches the writer.

## 9. UNICOM no-writer/product-gated behavior

UNICOM fallback records `no-write-requested` with `writerFailureReason=unicom-excluded`. It remains product-gated and non-writeable.

## 10. Sim/dataref failure diagnostics

Production writer failure still returns only a boolean, so opaque production false results are classified as `writer-result-unknown` unless the brain is given more explicit writer evidence.

The harness can model explicit writer result codes. Step 41 includes `com1-standby-dataref-not-writable`, which records:

- `writerFailureDomain=dataref`
- `writerDatarefAvailable=1`
- `writerDatarefWritable=0`
- `writerWriteBlockedBeforeSimWrite=1`
- `writerDatarefNotWritableCount=1`

## 11. Focused scenario summaries

- Controller write success produces structured writer success diagnostics.
- Controller no-target path records `no-target`.
- Standby disabled records `write-not-allowed` before writer attempt.
- Already COM1 standby records `write-not-allowed` before writer attempt.
- Gate-off direct CTAF remains dry-run/no-write with product-gate reason.
- Gate-on valid direct CTAF records `direct-ctaf-advisory` writer source.
- Pending CTAF never reaches writer.
- Failed CTAF never reaches writer.
- Empty CTAF records empty-frequency before write.
- UNICOM fallback never reaches writer.
- Modeled dataref-not-writable failure records explicit dataref diagnostics.
- Controller latch/write summary remains unchanged.

## 12. Build command/result

Command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: Passed.

## 13. Focused scenario command/result

Command:

```powershell
$scenarios = @(
'.\tools\regression_harness\scenarios\standby_assist_writer_result_controller_success.scn',
'.\tools\regression_harness\scenarios\standby_assist_writer_result_controller_no_target.scn',
'.\tools\regression_harness\scenarios\standby_assist_writer_result_disabled_blocks_controller.scn',
'.\tools\regression_harness\scenarios\standby_assist_writer_result_already_com1_standby_blocks_controller.scn',
'.\tools\regression_harness\scenarios\standby_assist_writer_result_direct_ctaf_gate_off_no_writer.scn',
'.\tools\regression_harness\scenarios\standby_assist_writer_result_direct_ctaf_gate_on_success.scn',
'.\tools\regression_harness\scenarios\standby_assist_writer_result_pending_ctaf_no_writer.scn',
'.\tools\regression_harness\scenarios\standby_assist_writer_result_failed_ctaf_no_writer.scn',
'.\tools\regression_harness\scenarios\standby_assist_writer_result_empty_ctaf_no_writer.scn',
'.\tools\regression_harness\scenarios\standby_assist_writer_result_unicom_no_writer.scn',
'.\tools\regression_harness\scenarios\standby_assist_writer_result_dataref_not_writable.scn',
'.\tools\regression_harness\scenarios\standby_assist_writer_result_controller_latch_unchanged.scn'
)
foreach ($scenario in $scenarios) {
  & '.\build\tools\XVatsimRegressionHarness.exe' $scenario
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
'passed=' + $scenarios.Count
```

Result: `passed=12`.

## 14. Full saved regression command/result

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

Result: `passed=326`.

## 15. Known gaps after writer result diagnostics

- Production `SetCom1StandbyFrequency` still returns only `bool`, so production false results cannot distinguish every dataref/sim cause without a later non-behavior-changing diagnostic API.
- `sim-write-failed` and dataref-specific codes are supported by the brain-owned result shape; dataref-not-writable is covered by harness evidence in this step.
- UNICOM remains excluded from live standby writes.
- Pending/failed/empty CTAF remain blocked before writer.
