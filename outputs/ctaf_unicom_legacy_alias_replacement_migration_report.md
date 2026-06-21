# Step 49: CTAF/UNICOM Legacy Alias Replacement Migration

## 1. Files changed

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- `brain/src/BrainOwnedRuntime.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/ctaf_unicom_legacy_alias_replacement_migration_inventory.scn`
- CTAF/UNICOM focused scenario expectations migrated to replacement names where practical.
- `outputs/ctaf_unicom_legacy_alias_replacement_migration_report.md`

## 2. Replacement fields added

Added canonical diagnostic names beside the old aliases:

| Legacy alias | Replacement field/name |
| --- | --- |
| `bypassRequired` | `diagnosticCompatibilityWouldDisplay` |
| `compatibilityOnly` | `diagnosticCompatibilityOnly` |
| `compatibility-fallback-warning` | `missing-evidence-warning-only` |
| `bypassReason` | `diagnosticCompatibilityReason` |
| `liveRowEmitted` | `legacyDiagnosticLiveRowEmitted` |
| `completionBypassCompatibilityOnly` | `diagnosticCompatibilityProjectionOnly` |
| `legacyCompatibilityOnly` | `compatibilityRowsDiagnosticOnly` |
| `fallbackWarnings` | `missingEvidenceWarningCount` |
| `missingEvidenceFallback` | `missingEvidenceWarningOnly` |

The alias audit now also records:

- `replacementFieldPresent`
- `replacementFieldName`
- `legacyFieldStillPresent`
- `replacementMatchesLegacy`
- `harnessMigratedToReplacement`
- `oldAliasDeprecated`
- `safeToRemoveLegacyLater`
- `replacementMigrationComplete`
- `replacementMismatchReason`

## 3. Old aliases retained

All old public/unknown-consumer aliases remain present for compatibility. The old names are now legacy mirrors, while replacement names are emitted first in current harness output where practical.

No report-only aliases were removed in this step.

## 4. Harness expectations migrated

The harness formatter now emits canonical replacement fields before legacy aliases. Focused CTAF/UNICOM expectations were migrated to assert replacement names while keeping legacy aliases in the output.

The new Step 49 focused scenario is:

- `ctaf_unicom_legacy_alias_replacement_migration_inventory.scn`

## 5. Replacement-to-legacy parity proof

Alias migration summary now reports:

```text
replacementFields=12
legacyStillPresent=25
replacementMatches=12
replacementMismatch=0
harnessMigrated=9
deprecatedAliases=12
safeToRemoveLater=3
replacementMigrationComplete=1
```

Focused scenarios prove replacement and legacy values match for:

- valid direct CTAF
- UNICOM fallback
- pending CTAF policy
- failed CTAF policy
- empty CTAF policy
- missing source/advisory/incomplete advisory warning paths

## 6. Report-only aliases

None removed.

The report-only aliases remain classified as later-removal candidates:

- `bypassRows` -> `legacyDiagnosticBypassRows`
- `diagnosticBypass` -> `diagnosticBypassRows`
- `liveBypass` -> `liveBypassAuthority`

They remain blocked by saved report/reference compatibility.

## 7. Consumer-risk status

Consumer-risk counts remain intentionally visible:

- `publicRisk=4`
- `unknownRisk=2`
- `reportOnly=3`
- `liveAuthorityMisleading=12`

The migration is safer than Step 48 because the replacement fields now exist and match the legacy values, but old aliases are still retained until downstream consumers are proven migrated.

## 8. No live bypass authority proof

Authority assertions remain centered on post-retirement invariant fields:

- `noLiveBypassAuthority=1`
- `compatibilityRowsDiagnosticOnly=1`
- `liveRowsBrainAdvisoryOwned=1`
- `standbyRowsAdvisoryOwned=1`
- `legacyBypassFieldsQuarantined=1`

No live CTAF/UNICOM bypass authority was restored.

## 9. Normal CTAF/UNICOM behavior unchanged

Valid direct CTAF and UNICOM fallback still display through brain-owned advisory projection. Pending, failed, and empty CTAF remain non-displayable/non-writeable policy cases.

This step changed diagnostic field names only.

## 10. Missing evidence remains warning-only

Missing evidence diagnostics now expose the canonical `warningLabel=missing-evidence-warning-only`. The legacy fallback label remains available only as a compatibility mirror where it already existed.

No live compatibility fallback was added.

## 11. Standby assist proof

Focused standby scenarios still prove:

- Standby gate OFF direct CTAF remains advisory-sourced/no-write.
- Standby gate ON direct CTAF requires a real advisory decision.
- Warning rows are not consumed by standby assist.
- Controller target/write behavior remains unchanged.

## 12. Build command/result

Command:

```powershell
& 'C:\Users\DARRON\Documents\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result:

```text
passed
```

## 13. Focused check/scenario command/result

Alias and replacement search:

```text
alias-and-replacement-search-patterns-found=24
```

Focused scenarios:

```text
focused-passed=14
```

## 14. Full saved regression command/result

Command:

```powershell
Get-ChildItem '.\tools\regression_harness\scenarios' -Filter '*.scn' | Sort-Object Name | ForEach-Object {
  & '.\build\tools\XVatsimRegressionHarness.exe' $_.FullName
}
```

Result:

```text
full-regression-passed=367
```

## 15. Known gaps after replacement migration

- Legacy aliases are still present for compatibility.
- Report-only aliases were not removed.
- Public and unknown-consumer aliases still need downstream migration proof before removal.
- A later cleanup can remove report-only aliases first, then public/unknown-consumer aliases once consumers are migrated.
- Runtime CTAF/UNICOM projection, standby assist, direct CTAF gate behavior, controller behavior, and COM writer behavior remain unchanged.
