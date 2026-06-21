# CTAF/UNICOM Advisory Preview Report

Step 32 adds diagnostic-only brain-owned CTAF/UNICOM advisory preview decisions. The preview reads the CTAF/UNICOM source and projection evidence from Step 31 and reproduces current projected rows side by side.

No final row creation, display behavior, or standby-assist behavior changed in this step.

## Files Changed

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- `brain/src/BrainOwnedRuntime.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/ctaf_unicom_source_evidence_available_ctaf.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_source_evidence_unicom_fallback.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_source_evidence_pending_failure.scn`
- `outputs/ctaf_unicom_advisory_preview_report.md`

## Advisory Preview Fields Added

`BrainOwnedCtafUnicomAdvisoryPreviewDecision` records:

- `advisoryDecisionId`
- `sourceEvidenceId`
- `endpoint`
- `airportIcao`
- `decision`
- `projectedRole`
- `projectedFrequency`
- `fallbackUsed`
- `sourceConfidence`
- `confidenceLevel`
- `positiveScore`
- `negativeScore`
- `hardBlock`
- `reason`
- `wouldEmitLiveRow`
- `matchesCurrentProjection`

`BrainOwnedCtafUnicomAdvisoryPreviewSummary` records:

- `sourceEvidenceCount`
- `projectionEvidenceCount`
- `advisoryPreviewDecisionCount`
- `previewWouldEmitLiveRowCount`
- `previewMatchesCurrentProjectionCount`
- `previewMismatchCount`
- `completionBypassCompatibilityOnly`

The existing `advisoryDecisionCount` remains `0` because no live advisory authority was added yet.

## Preview Rules Implemented

- Available CTAF source:
  - decision: `ctaf-display`
  - projected role: `CTAF`
  - projected frequency: lookup frequency
  - confidence: `high`
  - parity: `matchesCurrentProjection=1`

- Resolved no-CTAF with fallback eligible:
  - decision: `unicom-fallback-display`
  - projected role: `UNICOM`
  - projected frequency: `122.800`
  - fallback used: `1`
  - confidence: `medium`
  - parity: `matchesCurrentProjection=1`

- Pending or unresolved lookup:
  - decision: `defer-pending`
  - current empty-frequency CTAF projection is reported for parity
  - confidence: `fallback` or `unknown`
  - no behavior change

- Lookup failure:
  - decision: `lookup-failed`
  - reason includes failure count
  - current projection is still reported for parity
  - no behavior change

- Invalid source:
  - decision: `reject-invalid-source`
  - diagnostic hard block only
  - no behavior change

## Runtime Output Remained Unchanged

The publisher still uses the old CTAF/UNICOM projection path:

- `StationRequiresCompletion` bypass remains active.
- CTAF/UNICOM row insertion behavior is unchanged.
- Final display rows are unchanged.
- Advisory preview records are not live authority.
- COM1 standby assist is not wired to the preview yet.

The preview now exposes `projectedFrequency`, `wouldEmitLiveRow`, and `matchesCurrentProjection`, which gives the next step a brain-owned advisory record suitable for standby-assist assignment once authority is intentionally flipped.

## Focused Scenario Summaries

### Available CTAF

Scenario: `ctaf_unicom_source_evidence_available_ctaf.scn`

- Preview decisions:
  - `departure KAAA`: `ctaf-display`, `CTAF`, `122.950`
  - `arrival KBBB`: `ctaf-display`, `CTAF`, `123.450`
- Summary: `source=2,projection=2,preview=2,wouldEmit=2,matches=2,mismatch=0,bypass=1`
- Existing final publisher row remained `KAAA:UNKNOWN`.

### UNICOM Fallback

Scenario: `ctaf_unicom_source_evidence_unicom_fallback.scn`

- Preview decisions:
  - `departure KAAA`: `unicom-fallback-display`, `UNICOM`, `122.800`
  - `arrival KBBB`: `unicom-fallback-display`, `UNICOM`, `122.800`
- Summary: `source=2,projection=2,preview=2,wouldEmit=2,matches=2,mismatch=0,bypass=1`
- Existing final publisher row remained `KAAA:UNKNOWN`.

### Pending / Failure

Scenario: `ctaf_unicom_source_evidence_pending_failure.scn`

- Preview decisions:
  - `departure KAAA`: `defer-pending`, current empty-frequency CTAF projection recorded
  - `arrival KBBB`: `lookup-failed`, failure count `2`, current empty-frequency CTAF projection recorded
- Summary: `source=2,projection=2,preview=2,wouldEmit=2,matches=2,mismatch=0,bypass=1`
- Existing final publisher rows remained empty.

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

Result: all three focused CTAF/UNICOM advisory preview scenarios passed.

Full saved regression command:

```powershell
$scenarios = @(Get-ChildItem '.\tools\regression_harness\scenarios\*.scn' | Sort-Object Name); $count = 0; foreach ($scenario in $scenarios) { & '.\build\tools\XVatsimRegressionHarness.exe' $scenario.FullName *> $null; if ($LASTEXITCODE -ne 0) { Write-Host "FAILED $($scenario.Name)"; exit $LASTEXITCODE }; $count++ }; Write-Host "Passed $count scenarios"
```

Result: passed, `278` scenarios.

## Known Gaps Left For Later Steps

- Advisory preview is not live authority.
- `StationRequiresCompletion` bypass remains active and explicitly diagnosed.
- CTAF/UNICOM rows still do not use synthetic accepted completions.
- Standby assist is not yet instructed by CTAF/UNICOM advisory decisions.
- Manual CTAF command output is still outside this publisher preview path.
- Pending or failed lookups still reproduce current empty-frequency projection for parity.
