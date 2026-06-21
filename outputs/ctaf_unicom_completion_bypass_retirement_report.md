# CTAF/UNICOM Completion Bypass Retirement Report

## 1. Files changed

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- `brain/src/BrainOwnedRuntime.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/ctaf_unicom_source_evidence_available_ctaf.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_source_evidence_pending_failure.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_source_evidence_unicom_fallback.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_direct_ctaf_match.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_unicom_fallback_product_gated.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_pending_lookup_blocked.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_failed_lookup_blocked.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_empty_frequency_blocked.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_duplicate_projection_mismatch.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_missing_source_evidence_fail_soft.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_standby_gate_off_advisory_not_bypass.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_standby_gate_on_direct_ctaf_source.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_controller_behavior_unchanged.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_authority_guardrail.scn`
- `tools/regression_harness/scenarios/standby_assist_ctaf_unicom_preview_direct_ctaf_recommendable.scn`
- `tools/regression_harness/scenarios/standby_assist_decision_ledger_ctaf_advisory_skipped.scn`
- `tools/regression_harness/scenarios/standby_assist_decision_ledger_unicom_advisory_skipped.scn`
- `outputs/ctaf_unicom_completion_bypass_retirement_report.md`

## 2. What bypass code/path was removed or disabled

- `StationRequiresCompletion` no longer exempts CTAF/UNICOM stations. CTAF/UNICOM rows no longer bypass completion filtering as live authority.
- The no-source-evidence compatibility append path was disabled. Live CTAF/UNICOM rows are appended only from brain-owned advisory decisions.
- Compatibility projection evidence remains, but it is diagnostic-only and reports `completionBypassLiveAuthority=0`.

## 3. Retired-bypass diagnostic fields added or changed

Added or exposed:

- `completionBypassRetired`
- `completionBypassLiveAuthority`
- `completionBypassDiagnosticOnly`
- `retiredBypassCompatibilityRowCount`
- `bypassRetirementFallbackWarning`
- `missingEvidenceFallbackPreserved`
- `advisoryProjectionAuthority`
- `liveRowAuthority`
- `standbyAuthority`
- `bypassRetirementRegressionSafe`

Historical compatibility rows can still be counted for parity evidence, but live authority is now represented separately and remains zero.

## 4. Summary fields added or changed

Added or exposed:

- `completionBypassRetired`
- `liveBypassAuthorityCount`
- `diagnosticBypassRowCount`
- `brainAdvisoryLiveRowCount`
- `compatibilityFallbackWarningCount`
- `missingEvidenceFallbackWarningCount`
- `duplicateLiveRowCount`
- `pendingNonDisplayableCount`
- `failedLookupNonDisplayableCount`
- `emptyFrequencyNonDisplayableCount`
- `standbyBypassConsumerCount`
- `standbyAdvisoryConsumerCount`
- `bypassRetirementSafe`

Authority summaries now report `liveBypass=0` and `diagnosticBypass=2` in normal two-endpoint CTAF/UNICOM cases.

## 5. Valid direct CTAF behavior after retirement

Valid direct CTAF still displays through brain-owned advisory projection:

- `advisoryProjectionAuthority=1`
- `liveRowAuthority=brain-advisory`
- `completionBypassLiveAuthority=0`
- `standbyAuthority=brain-advisory`

Focused direct CTAF scenarios still show the KAAA advisory row and no live bypass authority.

## 6. UNICOM fallback behavior after retirement

UNICOM fallback display still appears through brain-owned advisory projection where existing behavior displayed it. It remains product-gated and excluded from live standby assist:

- display authority: `brain-advisory`
- standby decision: product-gated / role-not-eligible
- live bypass authority: `0`
- standby bypass consumer count: `0`

## 7. Pending/failed/empty CTAF behavior after retirement

- Pending CTAF: `defer-pending-lookup`, non-displayable, non-writeable.
- Failed CTAF: `failed-lookup-non-displayable`, non-displayable, non-writeable.
- Empty CTAF: `empty-frequency-non-displayable`, hard-blocked, non-displayable, non-writeable.

All three keep diagnostic compatibility evidence, but `brainAdvisoryLive=0` and `liveBypassAuthority=0`.

## 8. Duplicate compatibility behavior after retirement

Duplicate compatibility projection remains diagnostic-only. If the duplicate has a matching brain advisory equivalent, it is classified as:

- `compatibility-duplicate-suppressed`
- `duplicateSuppressed=1`
- `duplicateLiveRows=0`
- `liveBypassAuthority=0`

No duplicate live CTAF/UNICOM row is emitted.

## 9. Missing evidence fail-soft behavior after retirement

Missing source/advisory evidence no longer silently uses the compatibility path as live authority. Instead, it remains visible as a warning:

- `fallbackWarnings=1`
- `missingEvidenceWarnings=1`
- `liveRowAuthority=compatibility-fallback-warning`
- `liveBypassAuthority=0`
- `retirementSafe=0`

This preserves the Step 44 constraint that missing evidence must not be silently dropped.

## 10. Standby advisory-consumption proof after retirement

Focused scenarios prove standby consumes advisory decisions, not bypass rows:

- `standbyAdvisory=2`
- `standbyBypass=0`
- direct CTAF selected source remains `direct-ctaf-advisory` when the gate is ON and no controller target wins.
- UNICOM remains excluded and never becomes a live standby target.

## 11. What runtime behavior changed

The CTAF/UNICOM completion bypass is retired as live authority. CTAF/UNICOM rows now appear through brain-owned advisory projection, and compatibility rows are diagnostic-only.

The missing-evidence fault path now emits an explicit diagnostic warning instead of preserving a live compatibility fallback.

## 12. What runtime behavior remains unchanged

- Valid direct CTAF display remains present.
- UNICOM fallback display remains present.
- Pending, failed, and empty CTAF remain non-displayable and non-writeable.
- Direct CTAF standby gate behavior is unchanged.
- Controller targets still win over direct CTAF.
- UNICOM remains excluded from live standby assist.
- COM writer behavior is unchanged.
- Existing controller target/write behavior is unchanged.
- BrainDisplayIntent, transceiver resolver, route sector, and HNL were not changed for this step.

## 13. Focused scenario summaries

- Valid direct CTAF displays via brain advisory, `liveBypassAuthority=0`.
- UNICOM fallback displays via brain advisory and remains standby product-gated.
- Pending CTAF has no live row and is policy-ledgered.
- Failed CTAF has no live row and is policy-ledgered.
- Empty CTAF has no live row and is hard-blocked/policy-ledgered.
- Duplicate compatibility projection stays diagnostic-only and creates no duplicate live row.
- Missing source evidence remains fail-soft with an explicit fallback warning.
- Gate OFF direct CTAF remains advisory-sourced dry-run/no-write.
- Gate ON direct CTAF remains `direct-ctaf-advisory` when selected.
- Controller target/write behavior remains unchanged.
- Controller target still wins over direct CTAF.
- UNICOM remains excluded from live standby assist.
- Authority guardrail reports no live bypass authority.

## 14. Build command/result

Command:

```powershell
& 'C:\Users\DARRON\Documents\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: passed, exit code 0.

## 15. Focused scenario command/result

Command:

```powershell
$scenarios = @(
  '.\tools\regression_harness\scenarios\ctaf_unicom_bypass_retirement_direct_ctaf_match.scn',
  '.\tools\regression_harness\scenarios\ctaf_unicom_bypass_retirement_unicom_fallback_product_gated.scn',
  '.\tools\regression_harness\scenarios\ctaf_unicom_bypass_retirement_pending_lookup_blocked.scn',
  '.\tools\regression_harness\scenarios\ctaf_unicom_bypass_retirement_failed_lookup_blocked.scn',
  '.\tools\regression_harness\scenarios\ctaf_unicom_bypass_retirement_empty_frequency_blocked.scn',
  '.\tools\regression_harness\scenarios\ctaf_unicom_bypass_retirement_duplicate_projection_mismatch.scn',
  '.\tools\regression_harness\scenarios\ctaf_unicom_bypass_retirement_missing_source_evidence_fail_soft.scn',
  '.\tools\regression_harness\scenarios\ctaf_unicom_bypass_retirement_standby_gate_off_advisory_not_bypass.scn',
  '.\tools\regression_harness\scenarios\ctaf_unicom_bypass_retirement_standby_gate_on_direct_ctaf_source.scn',
  '.\tools\regression_harness\scenarios\ctaf_unicom_bypass_retirement_controller_behavior_unchanged.scn',
  '.\tools\regression_harness\scenarios\ctaf_unicom_bypass_retirement_authority_guardrail.scn',
  '.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_live_gate_on_controller_wins.scn',
  '.\tools\regression_harness\scenarios\standby_assist_direct_ctaf_live_gate_on_unicom_excluded.scn'
)
foreach ($scenario in $scenarios) {
  & '.\build\tools\XVatsimRegressionHarness.exe' $scenario
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
'passed=' + $scenarios.Count
```

Result: `passed=13`, exit code 0.

## 16. Full saved regression command/result

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

Result: `passed=351`, exit code 0.

## 17. Known gaps after bypass retirement

- Diagnostic compatibility evidence still carries historical fields such as `bypassRows` and `liveRowEmitted`; live authority is now tracked by the newer `liveBypassAuthority` fields.
- Missing-evidence fault paths remain warning-ledgered and not retirement-safe. A later hardening step can decide whether to add stronger source-evidence recovery.
- UNICOM standby assist remains product-gated and excluded from live standby eligibility.
