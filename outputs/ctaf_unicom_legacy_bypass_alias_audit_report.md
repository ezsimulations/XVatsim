# Step 48: CTAF/UNICOM Legacy Bypass Alias Audit

## 1. Files changed

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
  - Added `BrainOwnedCtafUnicomLegacyBypassAliasAuditDecision`.
  - Added `BrainOwnedCtafUnicomLegacyBypassAliasAuditSummary`.
  - Added alias audit decisions/summary to `BrainOwnedPublisherOutput`.
- `brain/src/BrainOwnedRuntime.cpp`
  - Added the brain-owned static alias inventory.
  - Added alias classification and summary counters.
  - Wired the alias audit into publisher diagnostics.
- `tools/regression_harness/src/main.cpp`
  - Added scenario expectation keys for alias audit summary and decisions.
  - Added harness output for alias audit decisions.
  - Added focused assertion support.
- `tools/regression_harness/scenarios/ctaf_unicom_legacy_bypass_alias_audit_inventory.scn`
  - Added focused Step 48 inventory scenario.
- `outputs/ctaf_unicom_legacy_bypass_alias_audit_report.md`
  - This report.

## 2. Alias inventory

The audit inventory covers the requested bypass-era aliases and additional CTAF/UNICOM fields containing bypass, compatibility, fallback, or live-row language.

Summary:

- `aliasAuditCount=25`
- `renameNowCandidateCount=0`
- `renameLaterCount=9`
- `removeLaterCount=3`
- `harnessOnlyAliasCount=16`
- `reportOnlyAliasCount=3`
- `publicConsumerRiskCount=4`
- `unknownConsumerRiskCount=2`
- `liveAuthorityMisleadingAliasCount=12`
- `authorityInvariantProtectedCount=25`

## 3. Alias classification table

| Alias | Location/category | Current meaning | Risk | Action | Migration target |
| --- | --- | --- | --- | --- | --- |
| `bypassRequired` | brain runtime, harness output, expectations / per-decision field | old compatibility projection would have displayed | high | rename-later | `diagnosticCompatibilityWouldDisplay` |
| `compatibilityOnly` | brain runtime, harness output, expectations / per-decision field | diagnostic compatibility projection only | high | rename-later | `diagnosticCompatibilityOnly` |
| `compatibility-fallback-warning` | brain warning label, reports / warning label | missing evidence warning only | high | rename-later | `missing-evidence-warning-only` |
| `bypassRows` | historical reports, old harness alias / report-only | historical bypass row count | high | remove-later | `legacyDiagnosticBypassRows` |
| `liveRowEmitted` | brain runtime projection field / internal field | historical projection would have emitted row | high | rename-later | `legacyDiagnosticLiveRowEmitted` |
| `legacyDiagnosticBypassRows` | harness output, expectations / harness-only | diagnostic bypass row count | low | keep-current-name | none |
| `legacyDiagnosticBypassFlag` | harness output, expectations / harness-only | diagnostic bypass compatibility flag | low | keep-current-name | none |
| `legacyDiagnosticLiveRows` | harness output, expectations / harness-only | diagnostic live row count from historical projection | low | keep-current-name | none |
| `legacyDiagnosticLiveRowEmitted` | harness output, expectations / harness-only | diagnostic row-emitted flag from historical projection | low | keep-current-name | none |
| `historicalCompatibilityRows` | harness output, expectations / harness-only | historical compatibility projection count | low | keep-current-name | none |
| `diagnosticBypassRows` | harness output, expectations / harness-only | diagnostic-only bypass row count | low | keep-current-name | none |
| `diagnosticBypass` | historical reports, old harness alias / report-only | old name for `diagnosticBypassRows` | medium | remove-later | `diagnosticBypassRows` |
| `liveBypass` | historical reports, old harness alias / report-only | old name for `liveBypassAuthority` | high | remove-later | `liveBypassAuthority` |
| `liveBypassAuthority` | harness output, expectations / authority invariant | count of live bypass authority records | low | keep-current-name | none |
| `bypassLiveAuthority` | harness output, expectations / per-decision authority field | per-decision live bypass authority flag | low | keep-current-name | none |
| `bypassDiagnosticOnly` | harness output, expectations / per-decision authority field | per-decision diagnostic-only bypass flag | low | keep-current-name | none |
| `bypassReason` | brain runtime, harness output, expectations / per-decision reason field | diagnostic bypass or retirement reason | medium | rename-later | `diagnosticCompatibilityReason` |
| `completionBypassCompatibilityOnly` | brain runtime summary field / internal field | compatibility projection is diagnostic-only | medium | rename-later | `diagnosticCompatibilityProjectionOnly` |
| `compatibilityRowsDiagnosticOnly` | harness output, expectations / authority invariant | compatibility rows have no live authority | low | keep-current-name | none |
| `legacyBypassFieldsQuarantined` | harness output, expectations / authority invariant | legacy bypass fields are diagnostic-only | low | keep-current-name | none |
| `liveRowAuthority` | harness output, expectations / authority invariant | actual live row authority source | low | keep-current-name | none |
| `legacyCompatibilityOnly` | harness output, expectations / harness-only | legacy name for compatibility diagnostic-only status | medium | rename-later | `compatibilityRowsDiagnosticOnly` |
| `fallbackWarnings` | harness output, expectations / harness-only | compatibility fallback warning count | medium | rename-later | `missingEvidenceWarningCount` |
| `missingEvidenceFallback` | harness output, expectations / harness-only | missing-evidence warning preserved flag | medium | rename-later | `missingEvidenceWarningOnly` |
| `retiredCompatibilityRows` | harness output, expectations / harness-only | retired diagnostic compatibility row count | low | keep-current-name | none |

## 4. Items safe to rename now

None.

Every candidate with authority-shaped wording either appears in public brain output, harness expectations, saved reports, or an unknown-consumer area. Step 48 intentionally leaves names in place and adds the brain-owned audit ledger instead of changing consumer-facing strings.

## 5. Items that must remain temporarily

- Public/consumer-risk names:
  - `bypassRequired`
  - `compatibilityOnly`
  - `compatibility-fallback-warning`
  - `bypassReason`
- Unknown-consumer-risk names:
  - `liveRowEmitted`
  - `completionBypassCompatibilityOnly`
- Harness/report continuity names:
  - `legacyDiagnosticBypassRows`
  - `legacyDiagnosticBypassFlag`
  - `legacyDiagnosticLiveRows`
  - `legacyDiagnosticLiveRowEmitted`
  - `historicalCompatibilityRows`
  - `diagnosticBypassRows`
  - `legacyCompatibilityOnly`
  - `fallbackWarnings`
  - `missingEvidenceFallback`
  - `retiredCompatibilityRows`

These remain protected by post-retirement invariant fields such as `noLiveBypassAuthority`, `compatibilityRowsDiagnosticOnly`, `liveRowsBrainAdvisoryOwned`, and `legacyBypassFieldsQuarantined`.

## 6. Items safe to remove later

After report/output consumers are migrated, these historical report-only aliases can be removed:

- `bypassRows` -> use `legacyDiagnosticBypassRows`
- `diagnosticBypass` -> use `diagnosticBypassRows`
- `liveBypass` -> use `liveBypassAuthority`

## 7. Consumer-risk assessment

- Public/consumer risk: 4 aliases.
- Unknown consumer risk: 2 aliases.
- Harness-only aliases: 16 aliases.
- Report-only aliases: 3 aliases.
- Live-authority-misleading wording: 12 aliases.
- Authority invariant protected: all 25 aliases.

The important distinction is that some old names still sound authoritative, but Step 46/47 invariant fields remain the only authority proof fields. The alias audit records that distinction directly.

## 8. Recommended migration/removal sequence

1. Keep using canonical authority fields for assertions: `noLiveBypassAuthority`, `compatibilityRowsDiagnosticOnly`, `liveRowsBrainAdvisoryOwned`, `standbyRowsAdvisoryOwned`, and `legacyBypassFieldsQuarantined`.
2. Add replacement fields alongside old public names:
   - `diagnosticCompatibilityWouldDisplay`
   - `diagnosticCompatibilityOnly`
   - `missing-evidence-warning-only`
   - `diagnosticCompatibilityReason`
   - `legacyDiagnosticLiveRowEmitted`
   - `diagnosticCompatibilityProjectionOnly`
3. Migrate harness expectations and saved report templates to the replacement names.
4. Remove report-only aliases: `bypassRows`, `diagnosticBypass`, and `liveBypass`.
5. Rename public/unknown-consumer fields only after downstream consumers are proven migrated.
6. Keep authority guardrail assertions centered on post-retirement invariant fields, not legacy compatibility counters.

## 9. Proof that no live bypass authority remains

Focused authority scenarios continued to assert:

- `liveBypassAuthority=0`
- `noLiveBypassAuthority=1`
- `compatibilityRowsDiagnosticOnly=1`
- `liveRowsBrainAdvisoryOwned=1`
- `legacyBypassFieldsQuarantined=1`

The alias audit scenario also pinned `authorityInvariantProtected=25`.

## 10. Proof that normal CTAF/UNICOM behavior remains unchanged

Focused scenarios covered valid direct CTAF, UNICOM fallback, pending lookup, failed lookup, empty frequency, standby gate OFF, standby gate ON, and controller behavior. All passed with existing post-retirement authority summaries intact.

## 11. Proof that missing evidence remains warning-only

The Step 47 missing-evidence scenarios still pass. Missing source/advisory/incomplete advisory paths remain warning-only diagnostics with no live compatibility fallback and no restored live bypass authority.

## 12. Proof that standby assist does not consume warning/bypass aliases

Focused standby scenarios still prove:

- Gate OFF direct CTAF remains advisory-sourced and no-write.
- Gate ON direct CTAF requires a real advisory decision.
- Missing-evidence warning rows are not consumed by standby assist.
- Controller target/write behavior remains unchanged.
- UNICOM remains excluded from live standby assist.

## 13. Build command/result

Command:

```powershell
& 'C:\Users\DARRON\Documents\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: passed.

Note: a sandboxed build attempt failed because CMake could not write generated build files under `build/`. The escalated rerun with the same command passed.

## 14. Focused check/scenario command/result

Alias search command checked the requested bypass-era labels across `brain`, `plugin`, `modules`, `tools`, `docs`, and `outputs`.

Result:

```text
alias-search-patterns-found=16
```

Focused scenario command ran 11 scenarios:

- `ctaf_unicom_legacy_bypass_alias_audit_inventory.scn`
- `ctaf_unicom_missing_evidence_audit_authority_guardrail.scn`
- `ctaf_unicom_missing_evidence_audit_valid_direct_ctaf_no_warning.scn`
- `ctaf_unicom_missing_evidence_audit_unicom_no_warning.scn`
- `ctaf_unicom_missing_evidence_audit_missing_departure_source.scn`
- `ctaf_unicom_missing_evidence_audit_pending_lookup_policy.scn`
- `ctaf_unicom_missing_evidence_audit_failed_lookup_policy.scn`
- `ctaf_unicom_missing_evidence_audit_empty_frequency_policy.scn`
- `ctaf_unicom_missing_evidence_audit_standby_gate_off_ignores_warning.scn`
- `ctaf_unicom_missing_evidence_audit_standby_gate_on_requires_advisory.scn`
- `ctaf_unicom_missing_evidence_audit_controller_behavior_unchanged.scn`

Result:

```text
focused-passed=11
```

## 15. Full saved regression command/result

Command:

```powershell
Get-ChildItem '.\tools\regression_harness\scenarios' -Filter '*.scn' | Sort-Object Name | ForEach-Object {
  & '.\build\tools\XVatsimRegressionHarness.exe' $_.FullName
}
```

Result:

```text
full-regression-passed=366
```

## 16. Known gaps after alias audit

- No legacy aliases were removed in Step 48.
- Public and unknown-consumer aliases still need a later migration step before rename/removal.
- Saved historical reports still contain old wording by design.
- The next cleanup should migrate consumers to replacement diagnostic names, then remove report-only aliases first.
- Runtime CTAF/UNICOM projection, standby assist, direct CTAF gate behavior, controller behavior, and COM writer behavior were not changed.
