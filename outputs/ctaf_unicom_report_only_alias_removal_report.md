# Step 50: CTAF/UNICOM Report-Only Alias Removal

## 1. Files changed

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- `brain/src/BrainOwnedRuntime.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/ctaf_unicom_report_only_alias_removal_inventory.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_legacy_bypass_alias_audit_inventory.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_legacy_alias_replacement_migration_inventory.scn`
- `outputs/ctaf_unicom_report_only_alias_removal_report.md`

## 2. Report-only aliases removed

Removed from active alias-audit generated output:

- `bypassRows`
- `diagnosticBypass`
- `liveBypass`

The active alias inventory now has 22 rows instead of 25.

## 3. Replacement fields used

The canonical replacement fields remain active:

- `legacyDiagnosticBypassRows`
- `diagnosticBypassRows`
- `liveBypassAuthority`

## 4. Search proof for removed aliases

Exact-token search over active paths (`brain`, `plugin`, `modules`, `tools`, `docs`) found no active generated-output/template occurrences of the removed aliases. Historical saved reports under `outputs/` were deliberately excluded because they document earlier steps.

Result:

```text
exact-report-only-alias-active-hits=0;replacement-fields-present=3
```

## 5. Alias audit summary after removal

The new alias audit summary is:

```text
aliasAudit=22
renameLater=9
removeLater=0
reportOnly=0
replacementFields=9
legacyStillPresent=22
replacementMatches=9
replacementMismatch=0
harnessMigrated=9
reportOnlyRemoved=3
reportOnlyRemovalSafe=3
reportOnlyStillFound=0
replacementMigrationComplete=1
```

## 6. Public/unknown aliases retained

The following public or unknown-consumer aliases remain present intentionally:

- `bypassRequired`
- `compatibilityOnly`
- `compatibility-fallback-warning`
- `bypassReason`
- `liveRowEmitted`
- `completionBypassCompatibilityOnly`

## 7. No live bypass authority proof

Focused scenarios still assert:

- `noLiveBypassAuthority=1`
- `compatibilityRowsDiagnosticOnly=1`
- `liveRowsBrainAdvisoryOwned=1`
- `standbyRowsAdvisoryOwned=1`
- `legacyBypassFieldsQuarantined=1`

No live bypass authority was restored.

## 8. Normal CTAF/UNICOM behavior unchanged

Valid direct CTAF and UNICOM fallback behavior remains brain-advisory owned. Pending, failed, and empty CTAF remain non-displayable/non-writeable policy cases.

## 9. Missing evidence remains warning-only

Missing evidence still emits warning-only diagnostics. No live compatibility fallback was added.

## 10. Standby assist proof

Focused scenarios still prove standby assist consumes advisory decisions, not warning/bypass rows. Standby gate OFF/ON direct CTAF behavior, controller target behavior, and writer behavior remain unchanged.

## 11. Build command/result

Command:

```powershell
& 'C:\Users\DARRON\Documents\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result:

```text
passed
```

## 12. Focused check/scenario command/result

Focused scenarios included report-only alias removal inventory, alias audit inventory, replacement migration inventory, authority guardrail, valid CTAF, UNICOM fallback, missing evidence, pending/failed/empty policy paths, standby gate OFF/ON, and controller unchanged.

Result:

```text
focused-passed=13
```

## 13. Full saved regression command/result

Command:

```powershell
Get-ChildItem '.\tools\regression_harness\scenarios' -Filter '*.scn' | Sort-Object Name | ForEach-Object {
  & '.\build\tools\XVatsimRegressionHarness.exe' $_.FullName
}
```

Result:

```text
full-regression-passed=368
```

## 14. Known gaps after report-only alias removal

- Public and unknown-consumer aliases remain temporarily.
- Historical saved reports still mention removed report-only aliases as prior-step context.
- A later step can migrate/remove public or unknown-consumer aliases after downstream consumers are proven safe.
- Runtime CTAF/UNICOM projection, standby assist, direct CTAF gate behavior, COM writer behavior, and controller behavior were not changed.
