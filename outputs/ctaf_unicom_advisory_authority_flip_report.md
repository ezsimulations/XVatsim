# CTAF/UNICOM Advisory Authority Flip Report

Step 33 flips live CTAF/UNICOM row projection to brain-owned advisory decisions when source evidence exists. Final functional output remains unchanged.

## Files Changed

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- `brain/src/BrainOwnedRuntime.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/ctaf_unicom_source_evidence_available_ctaf.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_source_evidence_unicom_fallback.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_source_evidence_pending_failure.scn`
- `outputs/ctaf_unicom_advisory_authority_flip_report.md`

## Authority Flip Point

The flip lives in `BrainOwnedRuntime.cpp`, inside `RunBrainOwnedPublisher`.

Old behavior:

- Filter boards by accepted completions.
- Remove legacy CTAF/UNICOM rows.
- Append lookup-derived CTAF/UNICOM rows directly from `input.departureCtafStation` and `input.arrivalCtafStation`.

New behavior:

- Filter boards by accepted completions.
- Remove legacy CTAF/UNICOM rows.
- Build compatibility projection evidence from the old lookup-derived rows.
- Build advisory preview decisions from source evidence and compatibility projection evidence.
- When source evidence exists, append live CTAF/UNICOM rows from advisory decisions via `AppendCtafUnicomRowsFromAdvisoryDecisions`.
- Fall back to compatibility append only if no source evidence exists.

The live row authority is now represented by `BrainOwnedCtafUnicomAdvisoryAuthoritySummary`:

- `advisoryAuthority=brain-evidence`
- `liveRowsBrainOwned=1`
- `oldVsBrainMismatchCount=0` in focused scenarios

## Compatibility Projection Remaining

`ctafUnicomProjectionEvidence` remains as compatibility/parity data. It records what the old lookup projection would have emitted:

- projected role
- projected frequency
- fallback used
- empty-frequency unresolved CTAF projection
- legacy CTAF/UNICOM rows removed
- completion bypass compatibility flag

This vector is no longer the live authority when source evidence exists.

## Guardrail Diagnostics Added

`BrainOwnedCtafUnicomAdvisoryAuthoritySummary` records:

- `advisoryAuthority`
- `sourceEvidenceCount`
- `advisoryPreviewDecisionCount`
- `liveAdvisoryRowCount`
- `compatibilityProjectionCount`
- `oldVsBrainMismatchCount`
- `completionBypassCompatibilityOnly`
- `liveRowsBrainOwned`

Focused scenarios assert:

```text
authority=brain-evidence,source=2,preview=2,live=2,compatibility=2,mismatch=0,bypass=1,brainOwned=1
```

## Final Output Remained Unchanged

Focused scenario final row behavior is unchanged:

- Available CTAF: final publisher row remains `KAAA:UNKNOWN`.
- UNICOM fallback: final publisher row remains `KAAA:UNKNOWN`.
- Pending/failure empty-frequency CTAF: final publisher rows remain empty.

`StationRequiresCompletion` remains active and is still diagnosed as compatibility behavior. Standby assist was not wired in this step.

## Focused Scenario Summaries

### Available CTAF

Scenario: `ctaf_unicom_source_evidence_available_ctaf.scn`

- Advisory decisions:
  - departure `KAAA`: `ctaf-display`, CTAF `122.950`
  - arrival `KBBB`: `ctaf-display`, CTAF `123.450`
- Authority summary: `authority=brain-evidence`, `live=2`, `compatibility=2`, `mismatch=0`, `brainOwned=1`
- Final publisher row unchanged: `KAAA:UNKNOWN`

### UNICOM Fallback

Scenario: `ctaf_unicom_source_evidence_unicom_fallback.scn`

- Advisory decisions:
  - departure `KAAA`: `unicom-fallback-display`, UNICOM `122.800`
  - arrival `KBBB`: `unicom-fallback-display`, UNICOM `122.800`
- Authority summary: `authority=brain-evidence`, `live=2`, `compatibility=2`, `mismatch=0`, `brainOwned=1`
- Final publisher row unchanged: `KAAA:UNKNOWN`

### Pending / Failure

Scenario: `ctaf_unicom_source_evidence_pending_failure.scn`

- Advisory decisions:
  - departure `KAAA`: `defer-pending`, current empty-frequency CTAF projection preserved
  - arrival `KBBB`: `lookup-failed`, failure count `2`, current empty-frequency CTAF projection preserved
- Authority summary: `authority=brain-evidence`, `live=2`, `compatibility=2`, `mismatch=0`, `brainOwned=1`
- Final publisher rows unchanged: none displayed

## Verification

Build command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: passed.

Focused scenario command:

```powershell
.\build\tools\XVatsimRegressionHarness.exe .\tools\regression_harness\scenarios\ctaf_unicom_source_evidence_available_ctaf.scn
.\build\tools\XVatsimRegressionHarness.exe .\tools\regression_harness\scenarios\ctaf_unicom_source_evidence_unicom_fallback.scn
.\build\tools\XVatsimRegressionHarness.exe .\tools\regression_harness\scenarios\ctaf_unicom_source_evidence_pending_failure.scn
```

Result: all three focused CTAF/UNICOM advisory authority scenarios passed.

Full saved regression command:

```powershell
$scenarios = @(Get-ChildItem '.\tools\regression_harness\scenarios\*.scn' | Sort-Object Name); $count = 0; foreach ($scenario in $scenarios) { & '.\build\tools\XVatsimRegressionHarness.exe' $scenario.FullName *> $null; if ($LASTEXITCODE -ne 0) { Write-Host "FAILED $($scenario.Name)"; exit $LASTEXITCODE }; $count++ }; Write-Host "Passed $count scenarios"
```

Result: passed, `278` scenarios.

## Known Gaps Left For Later Steps

- `StationRequiresCompletion` bypass remains active and diagnosed, not removed.
- CTAF/UNICOM rows still do not use synthetic accepted completions.
- Standby assist is not wired to advisory decisions yet.
- Manual CTAF command output remains outside the publisher advisory authority path.
- Pending or failed lookups still preserve current empty-frequency CTAF projection for compatibility.
