# Step 52: CTAF/UNICOM External-Risk Alias Deprecation Window

## 1. Files changed

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- `brain/src/BrainOwnedRuntime.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/ctaf_unicom_external_alias_deprecation_inventory.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_bypass_retirement_missing_source_evidence_fail_soft.scn`
- `outputs/ctaf_unicom_external_alias_deprecation_report.md`

## 2. Four external-risk aliases removed or deprecated

Step 52 adds a brain-owned external alias deprecation ledger. The four Step 51 external-risk aliases are now explicitly handled:

| Alias | Replacement | Step 52 result |
| --- | --- | --- |
| `bypassRequired` | `diagnosticCompatibilityWouldDisplay` | deprecated, still present in active output |
| `compatibilityOnly` | `diagnosticCompatibilityOnly` | deprecated, still present in active output |
| `compatibility-fallback-warning` | `missing-evidence-warning-only` | removed from active warning authority output; retained only in alias audits |
| `bypassReason` | `diagnosticCompatibilityReason` | deprecated, still present in active output |

The deprecation summary is:

`decisions=6,externalRisk=4,externalDeprecated=4,externalRemoved=1,activeRetained=5,replacementPreferred=6,replacementEquivalent=6,publicHeaderRetained=2,liveRowEmittedRetained=1,completionBypassCompatibilityOnlyRetained=1,runtimeChanged=0,noLiveBypassAuthority=1`

## 3. Replacement fields used

Focused Step 52 expectations assert canonical replacement fields:

- `diagnosticCompatibilityWouldDisplay`
- `diagnosticCompatibilityOnly`
- `missing-evidence-warning-only`
- `diagnosticCompatibilityReason`

The focused scenario avoids using the deprecated per-row aliases as authority proof and keeps authority assertions on:

- `noLiveBypassAuthority`
- `compatibilityRowsDiagnosticOnly`
- `liveRowsBrainAdvisoryOwned`
- `standbyRowsAdvisoryOwned`
- `legacyBypassFieldsQuarantined`

## 4. Search proof

Search confirmed the old warning authority label is gone from active warning authority output:

`rg -n "liveRowAuthority=compatibility-fallback-warning|decision->liveRowAuthority = \"compatibility-fallback-warning\"|diagnosticLiveRowAuthority=compatibility-fallback-warning" brain tools\regression_harness\scenarios`

Result: `active-warning-alias-not-found`

Search still finds `bypassRequired`, `compatibilityOnly`, and `bypassReason` in runtime structs, runtime calculations, harness output, and the Step 52 deprecation audit. These are intentionally retained as deprecated compatibility aliases for now.

## 5. Alias audit summary after this step

Step 52 adds:

- `BrainOwnedCtafUnicomExternalAliasDeprecationDecision`
- `BrainOwnedCtafUnicomExternalAliasDeprecationSummary`
- harness summary/decision formatters
- scenario expectations for the Step 52 deprecation ledger

Alias rows prove:

- `bypassRequired`, `compatibilityOnly`, and `bypassReason` are `deprecated-active-output`.
- `compatibility-fallback-warning` is `removed-from-active-warning-output`.
- replacements are preferred by harness expectations and carry equivalent meaning.
- authority invariants remain protected.

## 6. Public/header-risk aliases intentionally retained

The two unknown public/header-risk aliases remain present:

- `liveRowEmitted`
- `completionBypassCompatibilityOnly`

Their replacements remain present:

- `legacyDiagnosticLiveRowEmitted`
- `diagnosticCompatibilityProjectionOnly`

The Step 52 ledger marks both as `retained-unknown-public-header-risk`.

## 7. No live bypass authority proof

Focused scenario authority summary still reports:

- `bypassRetired=1`
- `liveBypassAuthority=0`
- `noLiveBypassAuthority=1`
- `compatibilityRowsDiagnosticOnly=1`
- `liveRowsBrainAdvisoryOwned=1`
- `legacyBypassFieldsQuarantined=1`

No CTAF/UNICOM completion bypass live authority was restored.

## 8. Normal CTAF/UNICOM behavior unchanged

Valid direct CTAF and UNICOM fallback behavior remains brain-owned and unchanged. Pending, failed, and empty CTAF remain non-displayable/non-writeable policy cases. The Step 52 change is diagnostic naming only.

## 9. Missing evidence remains warning-only

Missing evidence still emits warning-only diagnostics and does not become live authority. The active warning authority label now uses `missing-evidence-warning-only`; the old `compatibility-fallback-warning` label is retained only as historical alias inventory.

## 10. Standby assist does not consume warning/bypass aliases

Focused checks preserve the Step 47/51 standby assertions:

- standby gate OFF direct CTAF ignores warning rows and does not write
- standby gate ON direct CTAF still requires a real advisory decision
- standby authority remains advisory/controller sourced
- warning rows and bypass aliases are not standby authority

## 11. Build command/result

Command:

`& 'C:\Users\DARRON\Documents\bin\cmake.exe' --build build --config RelWithDebInfo`

Result: passed.

## 12. Focused check/scenario command/result

Command: PowerShell loop over 17 focused scenarios with:

`& '.\build\tools\XVatsimRegressionHarness.exe' '.\tools\regression_harness\scenarios\<scenario>.scn'`

Focused set included:

- `ctaf_unicom_external_alias_deprecation_inventory.scn`
- Step 51 public/unknown alias proof
- Step 50 report-only alias removal inventory
- Step 47 missing-evidence guardrails
- standby gate OFF/ON warning-consumption checks
- controller behavior unchanged check

Result: `focused-passed=17`

## 13. Full saved regression command/result

Command: PowerShell loop over every saved scenario:

`Get-ChildItem '.\tools\regression_harness\scenarios' -Filter '*.scn' | Sort-Object Name`

Each scenario was run with:

`& '.\build\tools\XVatsimRegressionHarness.exe' <scenario>`

Result: `full-regression-passed=370`

## 14. Known gaps after this alias deprecation step

- `bypassRequired`, `compatibilityOnly`, and `bypassReason` remain in active output as explicitly deprecated compatibility aliases.
- `liveRowEmitted` and `completionBypassCompatibilityOnly` remain because public/header consumer risk is still unknown.
- Historical saved reports still mention old aliases by design.

## 15. Recommended next repo-wide recovery target

Finish the two retained public/header-risk aliases first. After CTAF/UNICOM alias cleanup is fully closed, the next repo-wide recovery target should be the BrainDisplayIntent overlay cap ledger, so capped rows and `+N more ATC` behavior become brain-visible and auditable.
