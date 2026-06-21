# CTAF/UNICOM Source Evidence Visibility Report

Step 31 adds diagnostic-only CTAF/UNICOM source and projection evidence. Runtime row creation and final display behavior are intentionally unchanged.

## Files Changed

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- `brain/src/BrainOwnedRuntime.cpp`
- `plugin/src/XVatsimPlugin.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/ctaf_unicom_source_evidence_available_ctaf.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_source_evidence_unicom_fallback.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_source_evidence_pending_failure.scn`

## Source Evidence Fields Added

`BrainOwnedCtafUnicomSourceEvidence` records, per endpoint:

- `evidenceId`
- `endpoint`
- `airportIcao`
- `lookupAttempted`
- `lookupSkippedReason`
- `cacheHit`
- `fetchInProgress`
- `requestSucceeded`
- `statusCodeClass`
- `resolved`
- `available`
- `frequency`
- `lastAttemptAgeSeconds`
- `failureCount`
- `fallbackEligible`
- `fallbackFrequency`
- `sourceConfidence`
- `sourceReason`
- `pendingReason`

The plugin bridge now carries known lookup facts into `BrainOwnedCtafLookupFact`, including lookup attempt, resolved/cache status, request success class, failure count, last-attempt age when known, and pending/fetch-in-progress reason when inferable from the existing lookup entry.

## Publisher / Projection Evidence Fields Added

`BrainOwnedCtafUnicomProjectionEvidence` records, per endpoint projection:

- `sourceEvidenceId`
- `endpoint`
- `projectedRole`
- `projectedFrequency`
- `fallbackUsed`
- `unresolvedProjectedEmptyFrequency`
- `legacyRowRemovedCount`
- `duplicateSuppressedCount`
- `completionBypassCompatibilityOnly`
- `liveRowEmitted`

`BrainOwnedCtafUnicomEvidenceSummary` records:

- `sourceEvidenceCount`
- `projectionEvidenceCount`
- `liveRowEmittedCount`
- `completionBypassCompatibilityOnly`
- `advisoryDecisionCount`

For this step, `advisoryDecisionCount=0` and `completionBypassCompatibilityOnly=1` when CTAF/UNICOM projection evidence exists. This makes the existing bypass explicit without fixing or removing it yet.

## Runtime Behavior Unchanged

The publisher still:

- filters normal board rows by accepted completions,
- removes legacy CTAF/UNICOM board rows,
- appends lookup-derived CTAF or fallback UNICOM rows,
- allows CTAF/UNICOM through the existing `StationRequiresCompletion` bypass,
- sends the resulting boards through existing display intent behavior.

The new ledger observes these facts and projection results only. It is not used as authority and does not change final rows.

## Compatibility Bypasses Remaining

- `StationRequiresCompletion` still allows CTAF/UNICOM rows to bypass normal accepted completions.
- CTAF/UNICOM rows still do not have brain-owned advisory decisions.
- Legacy board CTAF/UNICOM removal remains compatibility behavior, now counted by `legacyRowRemovedCount`.
- Lookup-derived unresolved CTAF can still project an empty-frequency row into the board path; this is now visible through `unresolvedProjectedEmptyFrequency`.

## Focused Scenario Summaries

### Available CTAF

Scenario: `ctaf_unicom_source_evidence_available_ctaf.scn`

- Departure source: `KAAA`, CTAF available `122.950`, high confidence.
- Arrival source: `KBBB`, CTAF available `123.450`, high confidence.
- Projection: two live CTAF rows emitted.
- Legacy departure CTAF removal is explicitly counted: `legacyRemoved=1`.
- Summary: `source=2,projection=2,live=2,bypass=1,advisory=0`.
- Final publisher row for departure stage remains `KAAA:UNKNOWN`.

### Resolved No-CTAF / UNICOM Fallback

Scenario: `ctaf_unicom_source_evidence_unicom_fallback.scn`

- Departure and arrival lookup are resolved but no CTAF exists.
- Source evidence marks `fallbackEligible=1` and `fallbackFreq=122.800`.
- Projection emits UNICOM rows with `fallback=1`.
- Summary: `source=2,projection=2,live=2,bypass=1,advisory=0`.
- Final publisher row for departure stage remains `KAAA:UNKNOWN`.

### Pending / Failure Empty CTAF Projection

Scenario: `ctaf_unicom_source_evidence_pending_failure.scn`

- Departure lookup is unresolved and fetch-in-progress.
- Arrival lookup is unresolved with failure count `2`.
- Projection emits empty-frequency CTAF rows as before.
- Evidence records `unresolvedProjectedEmptyFrequency=1` for both endpoints.
- Summary: `source=2,projection=2,live=2,bypass=1,advisory=0`.
- Final publisher display rows remain empty.

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

Result: all three focused CTAF/UNICOM source evidence scenarios passed.

Full saved regression command:

```powershell
$scenarios = @(Get-ChildItem '.\tools\regression_harness\scenarios\*.scn' | Sort-Object Name); $count = 0; foreach ($scenario in $scenarios) { & '.\build\tools\XVatsimRegressionHarness.exe' $scenario.FullName *> $null; if ($LASTEXITCODE -ne 0) { Write-Host "FAILED $($scenario.Name)"; exit $LASTEXITCODE }; $count++ }; Write-Host "Passed $count scenarios"
```

Result: passed, `278` scenarios.

## Known Gaps Left For Later Steps

- No brain-owned CTAF/UNICOM advisory decision ledger exists yet.
- `StationRequiresCompletion` bypass remains active and is only diagnosed.
- CTAF/UNICOM source evidence is endpoint keyed, not yet synthetic-completion keyed.
- Manual CTAF command output remains outside the publisher evidence path.
- Real lookup `fetchInProgress` is inferred from the existing lookup entry because the service does not currently expose fetch state in `CtafLookupEntry`.
