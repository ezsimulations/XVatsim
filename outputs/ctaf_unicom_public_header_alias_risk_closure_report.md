# Step 53: CTAF/UNICOM Public-Header Alias Risk Closure Audit

## 1. Files changed

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- `brain/src/BrainOwnedRuntime.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/ctaf_unicom_public_header_alias_risk_closure_inventory.scn`
- `outputs/ctaf_unicom_public_header_alias_risk_closure_report.md`

## 2. Public/header alias inventory

Step 53 adds a focused brain-owned public/header alias closure ledger for the two remaining high-risk names:

| Alias | Replacement | Header exposure | Runtime writes | Harness legacy expectations |
| --- | --- | --- | --- | --- |
| `liveRowEmitted` | `legacyDiagnosticLiveRowEmitted` | `BrainOwnedCtafUnicomProjectionEvidence` | projection evidence, evidence summary, parity/missing-evidence audits | 4 |
| `completionBypassCompatibilityOnly` | `diagnosticCompatibilityProjectionOnly` | projection/evidence/preview/authority/bypass summaries | projection evidence plus CTAF/UNICOM summaries | 5 |

The closure summary is:

`aliases=2,sameScope=2,replacementMatches=2,compatibilityOnly=2,canDeprecateNow=2,canRemoveLater=0,removalBlocked=2,pluginUsage=0,moduleUsage=0,harnessLegacyUsage=9,publicHeaderRisk=2`

## 3. Consumer-risk table

| Alias | Plugin usage | Module usage outside brain | Docs usage | Report usage | Risk status |
| --- | ---: | ---: | ---: | ---: | --- |
| `liveRowEmitted` | 0 | 0 | 0 | 16 | public/header risk not cleared |
| `completionBypassCompatibilityOnly` | 0 | 0 | 0 | 18 | public/header risk not cleared |

Both aliases are internal to brain/runtime+harness code in the active repo, but they remain public header fields, so removal is still blocked.

## 4. Replacement same-scope proof

The Step 53 scenario asserts both replacements in the same diagnostic scope:

- `liveRowEmitted` pairs with `legacyDiagnosticLiveRowEmitted`
- `completionBypassCompatibilityOnly` pairs with `diagnosticCompatibilityProjectionOnly`

The audit reports `sameScope=2`.

## 5. Replacement-to-legacy parity proof

The closure ledger reports `replacementMatches=2`. The focused scenario also asserts projection evidence with:

- `diagnosticCompatibilityProjectionOnly=1`
- `legacyDiagnosticBypass=1`
- `legacyDiagnosticLiveRowEmitted=1`

This proves the canonical replacement values remain aligned with the retained legacy/header values.

## 6. `liveRowEmitted` readiness

Result: retained, deprecate-ready, not removal-ready.

- `canDeprecateNow=1`
- `canRemoveLater=0`
- blocker: `public-header-consumer-risk-not-yet-cleared`
- recommended action: `retain-now-deprecate-next`

## 7. `completionBypassCompatibilityOnly` readiness

Result: retained, deprecate-ready, not removal-ready.

- `canDeprecateNow=1`
- `canRemoveLater=0`
- blocker: `public-header-consumer-risk-not-yet-cleared`
- recommended action: `retain-now-deprecate-next`

## 8. Removed or deprecated aliases

No public/header aliases were removed in Step 53. No header field was formally deprecated in code comments yet; this step is audit/closure planning only. The new ledger proves both can be deprecated in a later compatibility step while retained.

## 9. No live bypass authority proof

The focused scenario still asserts:

- `bypassRetired=1`
- `liveBypassAuthority=0`
- `noLiveBypassAuthority=1`
- `compatibilityRowsDiagnosticOnly=1`
- `liveRowsBrainAdvisoryOwned=1`
- `legacyBypassFieldsQuarantined=1`

No CTAF/UNICOM completion bypass live authority was restored.

## 10. Normal CTAF/UNICOM behavior unchanged

Valid direct CTAF and UNICOM fallback behavior remains brain-owned and unchanged. Pending, failed, and empty CTAF remain non-displayable/non-writeable policy cases. This step only adds diagnostic closure records.

## 11. Missing evidence remains warning-only

Missing source/advisory evidence remains warning-only and non-authoritative. Step 52's active warning authority label remains `missing-evidence-warning-only`; no live compatibility fallback was added.

## 12. Standby assist does not consume warning/bypass aliases

Focused checks preserve the existing standby guarantees:

- gate OFF direct CTAF ignores warning rows and does not write
- gate ON direct CTAF still requires a real advisory decision
- standby authority remains advisory/controller sourced
- warning rows and bypass aliases are not standby authority

## 13. Build command/result

Command:

`& 'C:\Users\DARRON\Documents\bin\cmake.exe' --build build --config RelWithDebInfo`

Result: passed.

## 14. Focused check/scenario command/result

Command: PowerShell loop over 18 focused scenarios with:

`& '.\build\tools\XVatsimRegressionHarness.exe' '.\tools\regression_harness\scenarios\<scenario>.scn'`

Focused set included:

- `ctaf_unicom_public_header_alias_risk_closure_inventory.scn`
- Step 52 external alias deprecation inventory
- Step 51 public/unknown alias proof
- Step 50 report-only alias removal inventory
- Step 47 missing-evidence guardrails
- standby gate OFF/ON checks
- controller behavior unchanged check

Result: `focused-passed=18`

## 15. Full saved regression command/result

Command: PowerShell loop over every saved scenario:

`Get-ChildItem '.\tools\regression_harness\scenarios' -Filter '*.scn' | Sort-Object Name`

Each scenario was run with:

`& '.\build\tools\XVatsimRegressionHarness.exe' <scenario>`

Result: `full-regression-passed=371`

## 16. Recommendation

Close CTAF/UNICOM cleanup with one final compatibility step that formally deprecates, but still retains, `liveRowEmitted` and `completionBypassCompatibilityOnly` while documenting their replacements. Do not remove them until public/header consumer risk is cleared.

After that, move to the BrainDisplayIntent overlay cap ledger so capped rows and `+N more ATC` behavior become brain-visible and auditable.
