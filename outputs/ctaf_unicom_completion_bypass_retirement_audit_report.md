# CTAF/UNICOM Completion Bypass Retirement Audit Report

## 1. Files changed

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- `brain/src/BrainOwnedRuntime.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_direct_ctaf_match.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_unicom_fallback_product_gated.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_pending_lookup_blocked.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_failed_lookup_blocked.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_empty_frequency_blocked.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_duplicate_projection_mismatch.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_standby_gate_off_advisory_not_bypass.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_standby_gate_on_direct_ctaf_source.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_controller_behavior_unchanged.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_authority_guardrail.scn`
- `outputs/ctaf_unicom_completion_bypass_retirement_audit_report.md`

## 2. Bypass audit ledger fields added

Added `BrainOwnedCtafUnicomBypassAuditDecision` with:

- `ctafUnicomBypassAuditDecisionId`
- `advisoryDecisionId`
- `sourceEvidenceId`
- `projectionEvidenceId`
- `endpoint`
- `airportIcao`
- `callsign`
- `role`
- `frequency`
- `bypassRequired`
- `bypassReason`
- `advisoryAuthority`
- `advisoryWouldEmitLiveRow`
- `advisoryMatchesBypassRow`
- `roleMatches`
- `frequencyMatches`
- `endpointMatches`
- `airportMatches`
- `visibilityMatches`
- `bypassRowHasBrainEquivalent`
- `brainRowHasBypassEquivalent`
- `wouldRetireSafely`
- `retirementBlockedReason`
- `compatibilityOnly`
- `mismatchReason`
- `missingAdvisoryDecision`
- `missingSourceEvidence`
- `pendingLookup`
- `lookupFailed`
- `emptyFrequency`
- `unicomFallback`
- `standbyConsumesAdvisoryDecision`
- `standbyConsumesBypassRow`

Also added `projectionEvidenceId` to CTAF/UNICOM projection evidence.

## 3. Summary fields added

Added `BrainOwnedCtafUnicomBypassAuditSummary` with:

- `bypassAuditDecisionCount`
- `bypassRowCount`
- `brainOwnedAdvisoryRowCount`
- `matchingBrainEquivalentCount`
- `missingBrainEquivalentCount`
- `mismatchCount`
- `safeToRetireCount`
- `blockedRetirementCount`
- `pendingLookupCount`
- `lookupFailedCount`
- `emptyFrequencyCount`
- `unicomFallbackCount`
- `standbyAdvisoryConsumerCount`
- `standbyBypassConsumerCount`
- `completionBypassCompatibilityOnly`
- `ctafUnicomBypassRetirementReady`

## 4. Cases that appear safe to retire later

- Direct CTAF with valid resolved frequency: bypass projection and brain-owned advisory row match on role, frequency, endpoint, airport, and visibility.
- UNICOM fallback display: bypass projection and brain-owned advisory row match, while standby/live-write policy remains product-gated elsewhere.
- Mixed direct CTAF plus UNICOM fallback guardrail: authority remains `brain-evidence`, compatibility projection remains marked compatibility-only, and audit reports `ready=1`.

## 5. Cases that still block retirement

- Pending CTAF lookup: blocked with `retirementBlockedReason=pending-lookup`.
- Failed CTAF lookup: blocked with `retirementBlockedReason=lookup-failed`.
- Empty CTAF frequency: blocked with `retirementBlockedReason=empty-frequency`.
- Duplicate compatibility projection: blocked with `retirementBlockedReason=duplicate-compatibility-row` and `mismatchReason=duplicate-compatibility-row`.
- Missing source/advisory evidence paths are ledgered as blockers if they occur.

## 6. Standby advisory-consumption proof

The audit records `standbyConsumesAdvisoryDecision=1` and `standbyConsumesBypassRow=0` for CTAF/UNICOM advisory candidates.

Focused standby scenarios also assert:

- Gate OFF direct CTAF: no bypass-driven write, no live target, no write attempt.
- Gate ON direct CTAF: selected target source is `direct-ctaf-advisory`.
- Controller target present: selected target source remains `controller-display-row`.

## 7. Runtime behavior changed

No runtime row projection, standby assist, direct CTAF gate, or COM writer behavior was changed. This step adds diagnostics and harness assertions only.

## 8. Runtime behavior unchanged

- `StationRequiresCompletion` bypass remains present.
- CTAF/UNICOM live projection behavior is unchanged.
- Standby assist behavior is unchanged.
- Direct CTAF product gate behavior is unchanged.
- COM writer behavior is unchanged.
- UNICOM remains excluded from live standby assist.
- Pending/failed/empty CTAF do not become live standby targets.
- Controller standby targets remain preserved.

## 9. Focused scenario summaries

- `ctaf_unicom_bypass_retirement_direct_ctaf_match.scn`: valid direct CTAF rows match advisory rows and are marked safe.
- `ctaf_unicom_bypass_retirement_unicom_fallback_product_gated.scn`: UNICOM fallback matches advisory projection and remains standby product-gated.
- `ctaf_unicom_bypass_retirement_pending_lookup_blocked.scn`: pending lookup is compatibility-only and blocked.
- `ctaf_unicom_bypass_retirement_failed_lookup_blocked.scn`: failed lookup is compatibility-only and blocked.
- `ctaf_unicom_bypass_retirement_empty_frequency_blocked.scn`: empty frequency is blocked.
- `ctaf_unicom_bypass_retirement_duplicate_projection_mismatch.scn`: duplicate compatibility row is recorded as a mismatch/blocker.
- `ctaf_unicom_bypass_retirement_standby_gate_off_advisory_not_bypass.scn`: gate-off direct CTAF remains no-write and advisory-sourced.
- `ctaf_unicom_bypass_retirement_standby_gate_on_direct_ctaf_source.scn`: gate-on direct CTAF selects `direct-ctaf-advisory`, not bypass.
- `ctaf_unicom_bypass_retirement_controller_behavior_unchanged.scn`: controller target and writer source remain unchanged.
- `ctaf_unicom_bypass_retirement_authority_guardrail.scn`: authority guardrail reports brain-owned live rows plus compatibility-only bypass status.

## 10. Build command/result

Command:

```powershell
& 'C:\Users\DARRON\Documents\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: Passed.

## 11. Focused scenario command/result

Command:

```powershell
$scenarios = @(
'.\tools\regression_harness\scenarios\ctaf_unicom_bypass_retirement_direct_ctaf_match.scn',
'.\tools\regression_harness\scenarios\ctaf_unicom_bypass_retirement_unicom_fallback_product_gated.scn',
'.\tools\regression_harness\scenarios\ctaf_unicom_bypass_retirement_pending_lookup_blocked.scn',
'.\tools\regression_harness\scenarios\ctaf_unicom_bypass_retirement_failed_lookup_blocked.scn',
'.\tools\regression_harness\scenarios\ctaf_unicom_bypass_retirement_empty_frequency_blocked.scn',
'.\tools\regression_harness\scenarios\ctaf_unicom_bypass_retirement_duplicate_projection_mismatch.scn',
'.\tools\regression_harness\scenarios\ctaf_unicom_bypass_retirement_standby_gate_off_advisory_not_bypass.scn',
'.\tools\regression_harness\scenarios\ctaf_unicom_bypass_retirement_standby_gate_on_direct_ctaf_source.scn',
'.\tools\regression_harness\scenarios\ctaf_unicom_bypass_retirement_controller_behavior_unchanged.scn',
'.\tools\regression_harness\scenarios\ctaf_unicom_bypass_retirement_authority_guardrail.scn'
)
foreach ($scenario in $scenarios) {
  & '.\build\tools\XVatsimRegressionHarness.exe' $scenario
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
'passed=' + $scenarios.Count
```

Result: `passed=10`.

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

Result: `passed=350`.

## 13. Known gaps before actual bypass retirement

- The `StationRequiresCompletion` bypass has not been removed.
- Pending/failed/empty CTAF projection still needs a final product/display policy before retirement.
- Duplicate compatibility rows need an explicit cleanup or suppression decision before bypass removal.
- A later step still needs the actual bypass retirement patch and another full regression proof.
