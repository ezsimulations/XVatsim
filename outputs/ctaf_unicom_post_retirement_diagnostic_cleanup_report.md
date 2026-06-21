# Step 46: CTAF/UNICOM Post-Retirement Diagnostic Cleanup

## 1. Files changed

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- `brain/src/BrainOwnedRuntime.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/ctaf_unicom_source_evidence_available_ctaf.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_source_evidence_pending_failure.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_source_evidence_unicom_fallback.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_*.scn`
- `tools/regression_harness/scenarios/standby_assist_ctaf_unicom_preview_direct_ctaf_recommendable.scn`
- `tools/regression_harness/scenarios/standby_assist_decision_ledger_ctaf_advisory_skipped.scn`
- `tools/regression_harness/scenarios/standby_assist_decision_ledger_unicom_advisory_skipped.scn`
- `outputs/ctaf_unicom_post_retirement_diagnostic_cleanup_report.md`

## 2. Legacy/stale diagnostic fields identified

The ambiguous historical fields were the CTAF/UNICOM compatibility projection counters and labels that could be mistaken for live authority after Step 45 retired the completion bypass:

- `bypassRows`
- `liveRowEmitted`
- `bypass`
- `live`
- `compatibility`
- `liveBypass`
- `diagnosticBypass`
- `brainAdvisoryLive`
- compatibility projection row counts without a diagnostic-only qualifier

## 3. Fields renamed, quarantined, or marked diagnostic-only

Harness summaries now print explicit legacy/diagnostic names:

- `legacyDiagnosticBypassRows`
- `legacyDiagnosticBypassFlag`
- `legacyDiagnosticLiveRows`
- `legacyDiagnosticLiveRowEmitted`
- `historicalCompatibilityRows`
- `diagnosticBypassRows`
- `brainAdvisoryLiveRows`
- `legacyCompatibilityOnly`

The per-decision historical fields remain for continuity, but they are paired with explicit non-authority fields:

- `bypassRetired=1`
- `bypassLiveAuthority=0`
- `bypassDiagnosticOnly=1`
- `bypassReason=completion-bypass-retired-diagnostic-only`
- `liveRowAuthority=brain-advisory` or `none`
- `standbyAuthority=brain-advisory`, `controller-display-row`, or `none`

## 4. New invariant diagnostics added

Added invariant fields to source evidence, authority, and bypass audit summaries:

- `noLiveBypassAuthority`
- `compatibilityRowsDiagnosticOnly`
- `liveRowsBrainAdvisoryOwned`
- `standbyRowsAdvisoryOwned`
- `legacyBypassFieldsQuarantined`

The authority guardrail now asserts `noLiveBypassAuthority=1`, `compatibilityRowsDiagnosticOnly=1`, and `legacyBypassFieldsQuarantined=1`.

## 5. Harness expectation updates

Focused CTAF/UNICOM scenarios were updated to assert the retired-bypass authority fields instead of stale compatibility counters as authority proof. The updated expectations cover:

- valid direct CTAF
- UNICOM fallback
- pending CTAF lookup
- failed CTAF lookup
- empty CTAF frequency
- duplicate compatibility projection
- missing source evidence fail-soft path
- standby gate OFF direct CTAF
- standby gate ON direct CTAF
- controller target/write preservation
- authority guardrail

## 6. Proof that no live bypass authority remains

Focused scenarios assert:

- `completionBypassRetired=1`
- `liveBypassAuthority=0`
- `noLiveBypassAuthority=1`
- `compatibilityRowsDiagnosticOnly=1`
- `diagnosticBypassRows` may be nonzero, but only as diagnostic evidence

The authority guardrail reports no live bypass authority while preserving diagnostic compatibility evidence.

## 7. Proof that valid direct CTAF and UNICOM display remain brain-owned

Valid CTAF and UNICOM fallback scenarios report:

- `liveRowsBrainAdvisoryOwned=1`
- `brainAdvisoryLiveRows=2`
- `liveRowAuthority=brain-advisory`
- `advisoryProjectionAuthority=1`

This proves display remains sourced from brain-owned advisory projection after bypass retirement.

## 8. Proof that pending/failed/empty CTAF remain non-displayable/non-writeable

Pending, failed, and empty CTAF scenarios remain policy-ledgered and non-live:

- pending lookup: `pendingNonDisplayable=2`, `brainAdvisoryLive=0`
- failed lookup: `failedNonDisplayable=2`, `brainAdvisoryLive=0`
- empty frequency: `emptyNonDisplayable=2`, `brainAdvisoryLive=0`

Standby diagnostics still show these cases do not become write candidates.

## 9. Proof that standby consumes advisory decisions, not bypass rows

Bypass audit summaries assert:

- `standbyAdvisory=2` where advisory rows exist
- `standbyBypass=0`
- `standbyRowsAdvisoryOwned=1`

Direct CTAF standby gate scenarios continue to show selected direct CTAF source as `direct-ctaf-advisory`, not compatibility bypass evidence.

## 10. What runtime behavior changed

No runtime CTAF/UNICOM projection, standby assist, direct CTAF gate, COM writer, controller selection, or write behavior was changed in Step 46.

The only intentional changes are diagnostic names, summary invariants, and harness expectations.

## 11. What runtime behavior remains unchanged

- CTAF/UNICOM completion bypass remains retired as live authority.
- Valid direct CTAF display remains brain-owned.
- UNICOM fallback display remains brain-owned and excluded from live standby assist.
- Pending/failed/empty CTAF remain non-displayable and non-writeable.
- Direct CTAF gate behavior remains unchanged.
- Direct CTAF cannot displace controller targets.
- COM writer behavior remains unchanged.
- Existing controller target/write behavior remains unchanged.

## 12. Focused scenario summaries

Passed focused Step 46 scenarios:

- Valid direct CTAF displays via brain advisory and reports no live bypass authority.
- UNICOM fallback displays via brain advisory and reports no live bypass authority.
- Pending CTAF remains non-displayable and diagnostic-only.
- Failed CTAF remains non-displayable and diagnostic-only.
- Empty CTAF remains hard-blocked/non-displayable and diagnostic-only.
- Duplicate compatibility projection remains diagnostic-only and does not create duplicate live rows.
- Missing source evidence remains warning-ledgered with no live bypass authority.
- Standby gate OFF direct CTAF remains advisory-sourced/no-write.
- Standby gate ON direct CTAF remains advisory-sourced when selected.
- Controller target/write behavior remains unchanged.
- Direct CTAF cannot displace a controller target.
- UNICOM remains excluded from live standby assist.
- Authority guardrail asserts `noLiveBypassAuthority=1`.

## 13. Build command/result

Command:

```powershell
& 'C:\Users\DARRON\Documents\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: passed.

## 14. Focused scenario command/result

Command: ran 13 focused Step 46 scenarios through `build/tools/XVatsimRegressionHarness.exe`.

Result:

```text
passed=13
```

## 15. Full saved regression command/result

Command: ran every `.scn` file under `tools/regression_harness/scenarios` through `build/tools/XVatsimRegressionHarness.exe`.

Result:

```text
passed=351
```

## 16. Known gaps after diagnostic cleanup

- Per-decision compatibility fields such as `bypassRequired` and `compatibilityOnly` still exist for continuity, but are now paired with explicit retired/diagnostic-only authority fields.
- A later compatibility cleanup can fully rename or remove those historical per-decision aliases after downstream consumers migrate.
- Missing source/advisory evidence remains fail-soft and diagnostic-visible; it does not reintroduce live bypass authority.