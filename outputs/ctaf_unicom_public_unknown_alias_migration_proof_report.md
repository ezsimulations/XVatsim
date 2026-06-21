# Step 51: CTAF/UNICOM Public and Unknown-Consumer Alias Migration Proof

## 1. Files changed

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- `brain/src/BrainOwnedRuntime.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/ctaf_unicom_public_unknown_alias_migration_proof_inventory.scn`
- `outputs/ctaf_unicom_public_unknown_alias_migration_proof_report.md`

## 2. Six-alias consumer inventory

Step 51 keeps all six public/unknown-consumer aliases present and adds a brain-owned consumer-risk audit beside the existing alias migration diagnostics.

| Legacy alias | Replacement | Definition | Emission scope | Risk |
| --- | --- | --- | --- | --- |
| `bypassRequired` | `diagnosticCompatibilityWouldDisplay` | `BrainOwnedCtafUnicomBypassAuditDecision` | bypass audit decision summaries | external-consumer risk |
| `compatibilityOnly` | `diagnosticCompatibilityOnly` | `BrainOwnedCtafUnicomBypassAuditDecision` | bypass audit decision summaries | external-consumer risk |
| `compatibility-fallback-warning` | `missing-evidence-warning-only` | warning label | diagnostic live-row authority / warning label | external-consumer risk |
| `bypassReason` | `diagnosticCompatibilityReason` | `BrainOwnedCtafUnicomBypassAuditDecision` | bypass audit decision summaries | external-consumer risk |
| `liveRowEmitted` | `legacyDiagnosticLiveRowEmitted` | `BrainOwnedCtafUnicomProjectionEvidence` | projection evidence summaries | unknown-consumer risk |
| `completionBypassCompatibilityOnly` | `diagnosticCompatibilityProjectionOnly` | projection evidence and summaries | evidence / preview / authority / bypass summaries | unknown-consumer risk |

The audit summary emitted by the focused scenario is:

`aliases=6,sameScope=6,internalMigrated=6,compatibilityOnly=6,readyLater=4,blocked=2,externalRisk=4,unknownRisk=2,runtimeUsage=70,harnessLegacyUsage=33,reportLegacyUsage=44`

The usage counts are the encoded Step 51 consumer-search snapshot for these six retained aliases, captured to make later removal decisions reviewable.

## 3. Consumer-risk table

| Alias | Replacement emitted same scope | Internal consumers migrated | Compatibility-only | Later removal status |
| --- | --- | --- | --- | --- |
| `bypassRequired` | yes | yes | yes | ready later after external compatibility window |
| `compatibilityOnly` | yes | yes | yes | ready later after external compatibility window |
| `compatibility-fallback-warning` | yes | yes | yes | ready later after warning consumers migrate |
| `bypassReason` | yes | yes | yes | ready later after external compatibility window |
| `liveRowEmitted` | yes | yes | yes | blocked by unknown public header consumer risk |
| `completionBypassCompatibilityOnly` | yes | yes | yes | blocked by unknown public header consumer risk |

## 4. Internal consumer migration status

Harness expectations now assert the Step 51 consumer-risk proof with canonical fields:

- `replacementEmittedSameScope`
- `internalConsumersMigrated`
- `aliasCompatibilityOnly`
- `removalReadyLater`
- `removalBlockedReason`
- `nextMigrationAction`

Authority checks continue to use invariant/replacement fields:

- `noLiveBypassAuthority`
- `compatibilityRowsDiagnosticOnly`
- `liveRowsBrainAdvisoryOwned`
- `standbyRowsAdvisoryOwned`
- `legacyBypassFieldsQuarantined`

## 5. Replacement same-scope proof

All six retained aliases report `replacementSameScope=1`. The focused proof scenario asserts each alias and replacement pair in the same consumer audit record, so downstream migration can compare old and new names without changing the diagnostic scope.

## 6. Replacement-to-legacy parity proof

The focused scenario keeps the Step 49/50 parity guardrails active:

- `replacementFields=9`
- `legacyStillPresent=22`
- `replacementMatches=9`
- `replacementMismatch=0`
- `replacementMigrationComplete=1`

For the six public/unknown aliases, the new consumer audit additionally proves `sameScope=6` and `compatibilityOnly=6`.

## 7. Aliases ready for later removal

Ready after a compatibility window:

- `bypassRequired`
- `compatibilityOnly`
- `compatibility-fallback-warning`
- `bypassReason`

These remain present in Step 51 and are only classified as later-removal candidates.

## 8. Aliases still blocked

Blocked by unknown-consumer risk:

- `liveRowEmitted`
- `completionBypassCompatibilityOnly`

Both are public header fields with compatibility meaning. They should remain until a later step proves no public/header consumers depend on them.

## 9. No live bypass authority proof

The focused scenario asserts:

- `bypassRetired=1`
- `liveBypassAuthority=0`
- `noLiveBypassAuthority=1`
- `compatibilityRowsDiagnosticOnly=1`
- `liveRowsBrainAdvisoryOwned=1`
- `legacyBypassFieldsQuarantined=1`

No CTAF/UNICOM completion bypass live authority was restored.

## 10. Normal CTAF/UNICOM behavior unchanged

Valid direct CTAF and UNICOM fallback rows still display through brain-owned advisory projection. Pending, failed, and empty CTAF remain policy-ledgered and non-displayable/non-writeable. No CTAF/UNICOM live projection behavior was changed.

## 11. Missing evidence remains warning-only

Missing source/advisory evidence remains warning-only. The Step 47 warning protections remain covered by the focused set, including missing departure source, missing arrival source, missing advisory decision, incomplete advisory decision, duplicate diagnostic rows, and failed/pending/empty CTAF policy separation.

## 12. Standby does not consume warning/bypass aliases

Standby authority remains advisory/controller sourced. The proof scenarios preserve `standbyBypass=0`, `standbyRowsAdvisoryOwned=1`, and the gate-off/gate-on direct CTAF cases from the post-retirement audit. Warning rows and legacy aliases are not standby authority.

## 13. Build command/result

Command:

`& 'C:\Users\DARRON\Documents\bin\cmake.exe' --build build --config RelWithDebInfo`

Result: passed. The initial sandboxed attempt could not update the build directory; rerunning the same command with approved build-directory write access passed.

## 14. Focused check/scenario command/result

Command: PowerShell loop over 16 focused scenarios with:

`& '.\build\tools\XVatsimRegressionHarness.exe' '.\tools\regression_harness\scenarios\<scenario>.scn'`

Focused scenarios included:

- `ctaf_unicom_public_unknown_alias_migration_proof_inventory.scn`
- `ctaf_unicom_report_only_alias_removal_inventory.scn`
- all Step 47 missing-evidence guardrail scenarios
- gate-off/gate-on direct CTAF warning-consumption checks
- controller behavior unchanged check

Result: `focused-passed=16`

## 15. Full saved regression command/result

Command: PowerShell loop over every saved scenario:

`Get-ChildItem '.\tools\regression_harness\scenarios' -Filter '*.scn' | Sort-Object Name`

Each scenario was run with:

`& '.\build\tools\XVatsimRegressionHarness.exe' <scenario>`

Result: `full-regression-passed=369`

## 16. Recommended next migration/removal step

Next step can remove or deprecate the four external-risk aliases only after a final downstream compatibility window is accepted:

- `bypassRequired`
- `compatibilityOnly`
- `compatibility-fallback-warning`
- `bypassReason`

Keep `liveRowEmitted` and `completionBypassCompatibilityOnly` until public/header consumer risk is explicitly cleared. All future removals should keep authority assertions centered on the post-retirement invariant fields, not on legacy compatibility aliases.
