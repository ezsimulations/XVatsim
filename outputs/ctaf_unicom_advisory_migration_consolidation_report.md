# CTAF/UNICOM Advisory Migration Consolidation Report

Step 34 consolidates the CTAF/UNICOM advisory ownership migration after the source evidence, advisory preview, and brain-owned live row projection steps.

No runtime/display behavior was changed in this step.

## Files Changed

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- `brain/src/BrainOwnedRuntime.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/ctaf_unicom_source_evidence_available_ctaf.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_source_evidence_unicom_fallback.scn`
- `tools/regression_harness/scenarios/ctaf_unicom_source_evidence_pending_failure.scn`
- `outputs/ctaf_unicom_advisory_migration_consolidation_report.md`

## Current Ownership Status

CTAF/UNICOM advisory rows are now modeled as:

1. Lookup/source facts from CTAF lookup.
2. Structured CTAF/UNICOM source evidence.
3. Compatibility projection evidence for old lookup-to-row behavior.
4. Brain-owned advisory preview decisions.
5. Brain-owned live row projection when source evidence exists.

The old lookup projection path remains for parity/diagnostics only when evidence exists. It must not be treated as live authority.

## Where Things Live

Source evidence:

- `BuildCtafUnicomSourceEvidence` in `brain/src/BrainOwnedRuntime.cpp`
- `BrainOwnedCtafUnicomSourceEvidence` in `brain/include/XVatsim/brain/BrainOwnedRuntime.h`

Advisory preview:

- `BuildCtafUnicomAdvisoryPreviewDecision`
- `BuildCtafUnicomAdvisoryPreviewDecisions`
- `BrainOwnedCtafUnicomAdvisoryPreviewDecision`
- `BrainOwnedCtafUnicomAdvisoryPreviewSummary`

Live projection:

- `AppendCtafUnicomRowsFromAdvisoryDecisions`
- Called inside `RunBrainOwnedPublisher` after compatibility projection evidence and advisory preview decisions are built.

Authority guardrail:

- `BuildCtafUnicomAdvisoryAuthoritySummary`
- `BrainOwnedCtafUnicomAdvisoryAuthoritySummary`

## Documentation / Comments Added

Added type and flow comments making clear that:

- CTAF/UNICOM lookup facts are source evidence only.
- Compatibility projection is parity data when evidence exists.
- Brain-owned advisory decisions are the live row source when evidence exists.
- `StationRequiresCompletion` is a temporary compatibility bypass, not the desired final ownership model.

Relevant anchors:

- `BrainOwnedRuntime.h`: CTAF/UNICOM source evidence and advisory authority comments.
- `BrainOwnedRuntime.cpp`: `RunBrainOwnedPublisher` comments at the compatibility projection and advisory live row projection points.

## Compatibility Paths Remaining

- `StationRequiresCompletion` bypass remains active and diagnosed.
- CTAF/UNICOM rows still do not use synthetic accepted completions.
- Compatibility projection evidence remains so old-vs-brain parity can be measured.
- Pending/failure lookups still preserve current empty-frequency CTAF projection behavior for parity.
- Standby assist is not wired to CTAF/UNICOM advisory decisions yet.
- Manual CTAF command output remains outside the publisher advisory authority path.

## Regression / Harness Guardrails

The focused CTAF/UNICOM scenarios assert the combined authority diagnostic:

```text
authority=brain-evidence,source=2,preview=2,live=2,compatibility=2,mismatch=0,bypass=1,brainOwned=1
```

This proves:

- `authority=brain-evidence`
- source evidence count is populated
- advisory preview decision count is populated
- live advisory row count is populated
- compatibility projection count is populated
- old-vs-brain mismatch count is zero
- completion bypass remains explicit
- live rows are brain-owned

## Focused Scenario Coverage

Available CTAF:

- Scenario: `ctaf_unicom_source_evidence_available_ctaf.scn`
- Proves CTAF rows remain unchanged.
- Proves final publisher row remains `KAAA:UNKNOWN`.
- Proves `authority=brain-evidence`, `brainOwned=1`, and `mismatch=0`.

UNICOM fallback:

- Scenario: `ctaf_unicom_source_evidence_unicom_fallback.scn`
- Proves resolved no-CTAF still maps to UNICOM `122.800`.
- Proves final publisher row remains `KAAA:UNKNOWN`.
- Proves `authority=brain-evidence`, `brainOwned=1`, and `mismatch=0`.

Pending / failure:

- Scenario: `ctaf_unicom_source_evidence_pending_failure.scn`
- Proves pending/failure empty-frequency compatibility behavior remains unchanged.
- Proves final publisher rows remain empty.
- Proves `authority=brain-evidence`, `brainOwned=1`, and `mismatch=0`.

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

Result: all three focused CTAF/UNICOM advisory scenarios passed.

Full saved regression command:

```powershell
$scenarios = @(Get-ChildItem '.\tools\regression_harness\scenarios\*.scn' | Sort-Object Name); $count = 0; foreach ($scenario in $scenarios) { & '.\build\tools\XVatsimRegressionHarness.exe' $scenario.FullName *> $null; if ($LASTEXITCODE -ne 0) { Write-Host "FAILED $($scenario.Name)"; exit $LASTEXITCODE }; $count++ }; Write-Host "Passed $count scenarios"
```

Result: passed, `278` scenarios.

## Remaining CTAF/UNICOM Risks / TODOs

- Remove or replace `StationRequiresCompletion` only after synthetic advisory completions or equivalent brain-owned completion records are implemented.
- Wire standby assist only after the advisory decision-to-live-row contract is accepted.
- Add manual CTAF command evidence/decision coverage if it remains a displayed advisory surface.
- Decide whether pending/failure empty-frequency CTAF projection should be changed under fail-soft rules in a later behavior-changing step.
- Keep `oldVsBrainMismatchCount=0` as a release guardrail until compatibility projection is safely removable.
