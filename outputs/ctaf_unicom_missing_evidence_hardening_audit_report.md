# Step 47: CTAF/UNICOM Missing Evidence Hardening Audit

## 1. Files changed

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- `brain/src/BrainOwnedRuntime.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/ctaf_unicom_missing_evidence_audit_*.scn`
- `outputs/ctaf_unicom_missing_evidence_hardening_audit_report.md`

## 2. Missing-evidence audit fields added

Added brain-owned `BrainOwnedCtafUnicomMissingEvidenceAuditDecision` records with:

- `missingEvidenceAuditDecisionId`
- `missingEvidenceEndpoint`
- `missingEvidenceAirportIcao`
- `missingEvidenceRole`
- `missingEvidenceFrequency`
- `missingEvidenceCause`
- `missingSourceEvidence`
- `missingAdvisoryDecision`
- `incompleteAdvisoryDecision`
- `oldCompatibilityWouldDisplay`
- `wouldLoseFrequency`
- `wouldLoseVisibility`
- `warningOnly`
- `warningReason`
- `recoveryHint`
- `liveAuthorityRestored`
- `liveCompatibilityFallbackUsed`
- `standbyConsumesWarning`
- `standbyWriteBlockedByMissingEvidence`
- `authorityInvariantPreserved`
- `failSoftVisible`
- `operatorActionRequired`

## 3. Summary fields added

Added `BrainOwnedCtafUnicomMissingEvidenceAuditSummary` with:

- `missingEvidenceAuditCount`
- `missingSourceEvidenceCount`
- `missingAdvisoryDecisionCount`
- `incompleteAdvisoryDecisionCount`
- `oldCompatibilityWouldDisplayCount`
- `wouldLoseFrequencyCount`
- `wouldLoseVisibilityCount`
- `warningOnlyCount`
- `liveAuthorityRestoredCount`
- `liveCompatibilityFallbackUsedCount`
- `standbyConsumesWarningCount`
- `authorityInvariantPreservedCount`
- `operatorActionRequiredCount`

## 4. Missing source evidence behavior

When source evidence is absent but old compatibility projection had a visible row, the new audit emits a warning-only record. It preserves endpoint, airport, role, and frequency from diagnostic projection evidence where practical.

No live compatibility fallback is restored. The warning reports `liveAuthorityRestored=0`, `liveCompatibilityFallbackUsed=0`, and `warningOnly=1`.

## 5. Missing advisory decision behavior

When source and compatibility projection exist but the advisory decision is missing, the audit emits `missingAdvisoryDecision=1` with `recoveryHint=restore-ctaf-unicom-advisory-decision`.

The old display risk is visible through `oldCompatibilityWouldDisplay`, `wouldLoseFrequency`, and `wouldLoseVisibility`, but it remains diagnostic-only.

## 6. Incomplete advisory decision behavior

Harness-only fault injection can produce an incomplete advisory decision. The audit classifies that as `incompleteAdvisoryDecision=1` and `missingEvidenceCause=incomplete-advisory-decision`.

This does not change normal advisory projection behavior because the diagnostic fault flags default off.

## 7. Failed/pending/empty CTAF remain separate policy cases

Failed, pending, and empty CTAF paths continue to be classified in the bypass retirement policy ledger, not as missing evidence:

- failed lookup: `policy=failed-lookup-non-displayable`, `missingEvidencePolicy=0`
- pending lookup: `policy=defer-pending-lookup`, `missingEvidencePolicy=0`
- empty frequency: `policy=empty-frequency-non-displayable`, `missingEvidencePolicy=0`

The new missing-evidence audit summary reports `audit=0` for those policy cases.

## 8. Warning-only diagnostics do not become live authority

Focused warning scenarios assert:

- `warningOnly=1`
- `liveAuthorityRestored=0`
- `liveCompatibilityFallbackUsed=0`
- `authorityInvariantPreserved=1`

No live compatibility fallback was added.

## 9. Standby assist does not consume warning rows

Focused standby scenarios assert:

- `standbyConsumesWarning=0`
- `standbyWriteBlockedByMissingEvidence=1`
- no selected direct CTAF target when the real advisory decision is absent
- no COM write attempt from warning-only evidence

Standby continues to consume advisory candidates only, not compatibility warnings.

## 10. Normal valid CTAF/UNICOM behavior remains brain-owned

Valid direct CTAF and UNICOM fallback scenarios still report:

- `authority=brain-evidence`
- `brainOwned=1`
- `brainAdvisoryLiveRows=2`
- `liveRowsBrainAdvisoryOwned=1`
- `noLiveBypassAuthority=1`

The new missing-evidence audit remains empty for normal evidence-present cases.

## 11. Authority invariant proof

The authority guardrail continues to assert:

- `noLiveBypassAuthority=1`
- `compatibilityRowsDiagnosticOnly=1`
- `legacyBypassFieldsQuarantined=1`

Warning records also carry `authorityInvariantPreserved=1`.

## 12. What runtime behavior changed

No normal runtime CTAF/UNICOM projection behavior changed.

The only runtime-visible addition is structured diagnostics. Harness-only diagnostic fault injection can omit or damage advisory decisions for focused tests, but all flags default off.

## 13. What runtime behavior remains unchanged

- CTAF/UNICOM completion bypass is not restored as live authority.
- No live compatibility fallback is added.
- Normal valid direct CTAF display remains brain-owned.
- Normal UNICOM fallback display remains brain-owned.
- Pending/failed/empty CTAF remain non-displayable/non-writeable policy cases.
- Standby assist behavior is unchanged.
- Direct CTAF gate behavior is unchanged.
- UNICOM remains excluded from live standby assist.
- COM writer behavior is unchanged.
- Existing controller target/write behavior is unchanged.

## 14. Focused scenario summaries

Passed focused Step 47 scenarios:

- valid direct CTAF: brain-advisory live, no missing warning
- UNICOM fallback: brain-advisory live, no missing warning
- missing departure source evidence: warning-only, no live bypass authority
- missing arrival source evidence: warning-only, no live bypass authority
- missing advisory decision: warning-only, no live bypass authority
- incomplete advisory decision: warning-only, no live bypass authority
- failed CTAF lookup: policy-ledgered, not missing evidence
- pending CTAF lookup: policy-ledgered, not missing evidence
- empty CTAF: policy-ledgered, not missing evidence
- duplicate compatibility projection: diagnostic-only, not missing recovery
- standby gate OFF: warning ignored, no write
- standby gate ON: real advisory still required, no warning-row write
- authority guardrail: Step 46 invariants still pass
- controller target/write behavior unchanged

## 15. Build command/result

Command:

```powershell
& 'C:\Users\DARRON\Documents\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: passed.

## 16. Focused scenario command/result

Command: ran 14 focused Step 47 scenarios through `build/tools/XVatsimRegressionHarness.exe`.

Result:

```text
passed=14
```

## 17. Full saved regression command/result

Command: ran every `.scn` file under `tools/regression_harness/scenarios` through `build/tools/XVatsimRegressionHarness.exe`.

Result:

```text
passed=365
```

## 18. Known gaps after missing-evidence hardening audit

- Missing evidence remains warning-only; there is still no live recovery path by design.
- Harness-only advisory fault injection exists solely to prove warning behavior and should not become product behavior.
- A later cleanup may rename older bypass audit warning labels such as `compatibility-fallback-warning`, but Step 47 now explicitly proves those warnings do not restore live authority.