# CTAF/UNICOM Bypass Retirement Blocker Cleanup Report

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
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_missing_source_evidence_fail_soft.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_standby_gate_off_advisory_not_bypass.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_standby_gate_on_direct_ctaf_source.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_controller_behavior_unchanged.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_authority_guardrail.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_source_evidence_pending_failure.scn`
- `outputs/ctaf_unicom_bypass_retirement_blocker_cleanup_report.md`

## 2. Retirement policy fields added or changed

Added these brain-owned fields to each CTAF/UNICOM bypass audit decision:

- `retirementPolicy`
- `retirementPolicyReason`
- `retirementBlockerClass`
- `retirementBlockerResolved`
- `retirementStillBlocked`
- `retirementSafeAfterPolicy`
- `compatibilityDuplicateSuppressed`
- `duplicateSuppressionReason`
- `nonDisplayableByPolicy`
- `deferredByPolicy`
- `failedLookupByPolicy`
- `emptyFrequencyByPolicy`
- `missingEvidenceByPolicy`
- `wouldLoseFrequencyIfBypassRemoved`
- `wouldLoseVisibilityIfBypassRemoved`
- `safeToRemoveBypassAfterCleanup`

The audit now applies narrow policies after the Step 43 parity/mismatch checks. Valid parity remains `parity-safe`; unresolved or invalid CTAF states are explicitly classified; duplicate compatibility projection can be marked as diagnostic-only when a matching brain advisory row exists; missing evidence remains fail-soft and unsafe.

## 3. Summary fields added or changed

Added these summary counters:

- `retirementPolicyDecisionCount`
- `resolvedBlockerCount`
- `stillBlockedCount`
- `policyNonDisplayableCount`
- `policyDeferredCount`
- `policyFailedLookupCount`
- `policyEmptyFrequencyCount`
- `duplicateSuppressedCount`
- `missingEvidencePolicyCount`
- `wouldLoseFrequencyCount`
- `wouldLoseVisibilityCount`
- `bypassRemovalSafeCandidateCount`
- `bypassRemovalStillUnsafeCount`

`ctafUnicomBypassRetirementReady` is now based on the cleanup result, not only on the raw Step 43 `wouldRetireSafely` parity flag.

## 4. Pending CTAF policy

Pending CTAF lookup is classified as:

- `retirementPolicy=defer-pending-lookup`
- `retirementPolicyReason=pending-lookup-no-resolved-frequency`
- `retirementBlockerClass=pending-lookup`
- `nonDisplayableByPolicy=1`
- `deferredByPolicy=1`
- `retirementBlockerResolved=1`
- `safeToRemoveBypassAfterCleanup=1`

Pending CTAF does not produce a brain-owned live advisory row without a resolved frequency and remains non-writeable.

## 5. Failed CTAF policy

Failed CTAF lookup is classified as:

- `retirementPolicy=failed-lookup-non-displayable`
- `retirementPolicyReason=failed-lookup-no-valid-frequency`
- `retirementBlockerClass=lookup-failed`
- `nonDisplayableByPolicy=1`
- `failedLookupByPolicy=1`
- `retirementBlockerResolved=1`
- `safeToRemoveBypassAfterCleanup=1`

Failed lookup does not create a live row and does not affect standby writes.

## 6. Empty CTAF policy

Empty CTAF frequency is classified as:

- `retirementPolicy=empty-frequency-non-displayable`
- `retirementPolicyReason=empty-frequency-hard-block`
- `retirementBlockerClass=empty-frequency`
- `nonDisplayableByPolicy=1`
- `emptyFrequencyByPolicy=1`
- `retirementBlockerResolved=1`
- `safeToRemoveBypassAfterCleanup=1`

The brain advisory path hard-blocks empty frequency from live advisory emission and standby write eligibility.

## 7. Duplicate compatibility projection policy

Duplicate compatibility projection is treated as resolved only when the row has a matching brain-owned advisory equivalent:

- `retirementPolicy=compatibility-duplicate-suppressed`
- `retirementPolicyReason=duplicate-compatibility-row-has-brain-equivalent`
- `compatibilityDuplicateSuppressed=1`
- `duplicateSuppressionReason=identical-brain-equivalent`
- `retirementBlockerResolved=1`
- `safeToRemoveBypassAfterCleanup=1`

No brain-owned advisory row is suppressed. If a duplicate cannot be proven identical, it remains a stable retirement blocker through the raw mismatch path.

## 8. Missing evidence policy

Missing source or advisory evidence remains fail-soft:

- `retirementPolicy=fail-soft-missing-evidence`
- `retirementBlockerClass=missing-evidence`
- `missingEvidenceByPolicy=1`
- `wouldLoseFrequencyIfBypassRemoved=1`
- `wouldLoseVisibilityIfBypassRemoved=1`
- `retirementStillBlocked=1`
- `safeToRemoveBypassAfterCleanup=0`

The harness now has a narrow fault injection setting to omit departure or arrival CTAF/UNICOM source evidence so this path is explicit and regression-covered.

## 9. What blockers were resolved

- Valid direct CTAF remains parity-safe.
- UNICOM fallback remains parity-safe for display and product-gated for standby.
- Pending CTAF lookup is now an explicit deferred, non-displayable policy case.
- Failed CTAF lookup is now an explicit failed, non-displayable policy case.
- Empty CTAF frequency is now an explicit hard-blocked, non-displayable policy case.
- Duplicate compatibility projection is resolved when it is proven identical to a brain-owned advisory row.

## 10. What blockers still remain and why

- Missing source/advisory evidence remains unsafe because removing the bypass could lose a real visible frequency or airport row.
- Any future unclassified parity mismatch remains unsafe until it is either resolved or given a product policy.
- Duplicate projection that is not proven identical remains blocked.

## 11. What runtime behavior changed, if anything

No standby assist, direct CTAF gate, controller standby, COM writer, transceiver resolver, route sector, or HNL behavior was changed.

The only intentional change is diagnostic/policy classification in the brain-owned CTAF/UNICOM advisory and bypass audit path: pending, failed, and empty-frequency CTAF advisory decisions are now visibly non-emitting for retirement readiness. The temporary `StationRequiresCompletion` compatibility bypass remains present.

## 12. What runtime behavior remains unchanged

- `StationRequiresCompletion` bypass remains present.
- CTAF/UNICOM compatibility projection remains available for the current step.
- Existing controller standby selection and write behavior remain unchanged.
- Direct CTAF standby gate behavior remains unchanged.
- UNICOM remains excluded from live standby assist.
- Pending, failed, and empty CTAF remain non-live and non-writeable.
- COM writer behavior and writer result diagnostics remain unchanged.
- Standby assist consumes advisory decision diagnostics, not bypass rows.

## 13. Focused scenario summaries

- Valid direct CTAF: bypass row and brain advisory row match, `parity-safe`, removal-safe candidate.
- UNICOM fallback: display parity is preserved, standby remains product-gated/non-writeable.
- Pending CTAF: classified as `defer-pending-lookup`, non-displayable, non-writeable, cleanup-resolved.
- Failed CTAF: classified as `failed-lookup-non-displayable`, non-displayable, non-writeable, cleanup-resolved.
- Empty CTAF: classified as `empty-frequency-non-displayable`, hard-blocked, non-displayable, non-writeable.
- Duplicate compatibility projection: classified as `compatibility-duplicate-suppressed` when identical brain equivalent exists.
- Missing source evidence: classified as `fail-soft-missing-evidence`, still blocked, would lose frequency/visibility if bypass were removed.
- Standby gate OFF direct CTAF: no bypass-driven write; advisory diagnostics only.
- Standby gate ON direct CTAF: selected source remains `direct-ctaf-advisory`, not bypass row.
- Controller target/write behavior: unchanged.
- Authority guardrail: still reports brain-owned rows plus compatibility-only bypass status.

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
  '.\tools\regression_harness\scenarios\ctaf_unicom_bypass_retirement_authority_guardrail.scn'
)
foreach ($scenario in $scenarios) {
  & '.\build\tools\XVatsimRegressionHarness.exe' $scenario
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
'passed=' + $scenarios.Count
```

Result: `passed=11`, exit code 0.

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

## 17. Whether the next step may safely remove the bypass

A later step may remove the bypass only under these constraints:

- Keep valid direct CTAF and UNICOM fallback display authority brain-owned.
- Preserve the policy that pending, failed, and empty-frequency CTAF are non-displayable and non-writeable until resolved.
- Do not allow UNICOM to become live standby eligible.
- Do not use bypass rows as standby authority.
- Keep duplicate compatibility rows suppressed only when a matching brain advisory row is proven identical.
- Retain fail-soft diagnostics or a compatibility fallback for missing source/advisory evidence, because that path is still unsafe for blind removal.

Under normal evidence-present cases, the cleanup now marks the bypass as removal-safe. Missing evidence and unclassified mismatches remain the explicit constraints for the actual retirement step.
