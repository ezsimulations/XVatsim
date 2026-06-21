# Step 54: CTAF/UNICOM Public-Header Alias Deprecation

## 1. Files changed

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- `brain/src/BrainOwnedRuntime.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/ctaf_unicom_public_header_alias_risk_closure_inventory.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_public_header_alias_deprecation_inventory.scn`
- `docs/CTAF_UNICOM_PUBLIC_HEADER_ALIAS_DEPRECATION.md`
- `outputs/ctaf_unicom_public_header_alias_deprecation_report.md`

## 2. Deprecated public/header aliases retained

Both public/header aliases remain present and are now formally marked as deprecated compatibility mirrors:

- `liveRowEmitted`
- `completionBypassCompatibilityOnly`

The active deprecation summary is:

`aliases=2,sameScope=2,replacementMatches=2,compatibilityOnly=2,deprecatedPublicHeaderAliasCount=2,deprecatedPublicHeaderAliasRetained=2,deprecatedAliasReplacementMatch=2,deprecatedAliasReplacementMismatch=0,deprecatedAliasRemovalBlocked=2,canDeprecateNow=2,canRemoveLater=0,removalBlocked=2,pluginUsage=0,moduleUsage=0,harnessLegacyUsage=9,publicHeaderRisk=2,deprecatedAliasDocumentationPresent=1,publicHeaderCompatibilityWindowOpen=1,ctafUnicomAliasCleanupClosedExceptCompatibilityWindow=1`

## 3. Replacement fields

| Deprecated alias | Replacement |
| --- | --- |
| `liveRowEmitted` | `legacyDiagnosticLiveRowEmitted` |
| `completionBypassCompatibilityOnly` | `diagnosticCompatibilityProjectionOnly` |

Harness expectations prefer the replacement/invariant fields where possible.

## 4. Deprecation comments/annotations

Header comments were added beside retained compatibility mirrors in `BrainOwnedRuntime.h`:

- `completionBypassCompatibilityOnly` is documented as a deprecated compatibility mirror; use `diagnosticCompatibilityProjectionOnly`.
- `liveRowEmitted` is documented as a deprecated compatibility mirror; use `legacyDiagnosticLiveRowEmitted`.
- Summary mirror fields using `completionBypassCompatibilityOnly` are also marked as deprecated compatibility mirrors.

## 5. Developer documentation

Added `docs/CTAF_UNICOM_PUBLIC_HEADER_ALIAS_DEPRECATION.md`.

The document states:

- CTAF/UNICOM completion bypass is retired as live authority.
- These aliases remain only for temporary public/header compatibility.
- Future diagnostics should use replacements.
- Authority proof must use invariant fields: `noLiveBypassAuthority`, `compatibilityRowsDiagnosticOnly`, `liveRowsBrainAdvisoryOwned`, `standbyRowsAdvisoryOwned`, and `legacyBypassFieldsQuarantined`.
- Removal remains blocked by `public-header-consumer-risk-not-yet-cleared`.

## 6. Replacement-to-deprecated parity proof

Focused diagnostics report:

- `deprecatedAliasReplacementMatch=2`
- `deprecatedAliasReplacementMismatch=0`
- each retained alias has `replacementMatchesDeprecatedAlias=1`
- each retained alias has `deprecatedAliasStillEmitted=1`
- each retained alias has `replacementPreferred=1`

## 7. Why aliases are not removed yet

Both aliases are public/header fields. Step 53 proved plugin usage and module usage outside brain are zero, but public/header consumer risk is not cleared. Removal remains blocked by:

`public-header-consumer-risk-not-yet-cleared`

## 8. Authority invariant proof

The focused scenario still asserts:

- `bypassRetired=1`
- `liveBypassAuthority=0`
- `noLiveBypassAuthority=1`
- `compatibilityRowsDiagnosticOnly=1`
- `liveRowsBrainAdvisoryOwned=1`
- `legacyBypassFieldsQuarantined=1`

No CTAF/UNICOM bypass live authority was restored.

## 9. Normal CTAF/UNICOM behavior unchanged

Valid direct CTAF and UNICOM fallback behavior remains brain-owned and unchanged. Pending, failed, and empty CTAF remain non-displayable/non-writeable policy cases. This step adds comments, docs, and deprecation diagnostics only.

## 10. Missing evidence remains warning-only

Missing evidence remains warning-only and non-authoritative. No live compatibility fallback was added.

## 11. Standby assist does not consume warning/bypass aliases

Focused checks preserve the existing standby guarantees:

- gate OFF direct CTAF ignores warning rows and does not write
- gate ON direct CTAF still requires a real advisory decision
- standby authority remains advisory/controller sourced
- warning rows and bypass aliases are not standby authority

## 12. Build command/result

Command:

`& 'C:\Users\DARRON\Documents\bin\cmake.exe' --build build --config RelWithDebInfo`

Result: passed.

## 13. Focused check/scenario command/result

Command: PowerShell loop over 19 focused scenarios with:

`& '.\build\tools\XVatsimRegressionHarness.exe' '.\tools\regression_harness\scenarios\<scenario>.scn'`

Focused set included:

- `ctaf_unicom_public_header_alias_deprecation_inventory.scn`
- Step 53 public/header risk closure inventory
- Step 52 external alias deprecation inventory
- Step 51 public/unknown alias proof
- Step 50 report-only alias removal inventory
- Step 47 missing-evidence guardrails
- standby gate OFF/ON checks
- controller behavior unchanged check

Result: `focused-passed=19`

## 14. Full saved regression command/result

Command: PowerShell loop over every saved scenario:

`Get-ChildItem '.\tools\regression_harness\scenarios' -Filter '*.scn' | Sort-Object Name`

Each scenario was run with:

`& '.\build\tools\XVatsimRegressionHarness.exe' <scenario>`

Result: `full-regression-passed=372`

## 15. Final CTAF/UNICOM cleanup status

CTAF/UNICOM cleanup is closed except for the explicit public/header compatibility window:

- completion bypass is not live authority
- compatibility rows are diagnostic-only
- direct CTAF and UNICOM display remain brain-owned
- missing evidence remains warning-only
- retained public/header aliases are formally deprecated and replacement-backed

## 16. Recommended next repo-wide recovery target

Move to the BrainDisplayIntent overlay cap ledger next, so capped rows and `+N more ATC` behavior become brain-visible and auditable.
