# Brain Ownership Recovery Checkpoint After CTAF/UNICOM

This checkpoint documents the current brain-ownership recovery state after the completed migrations for:

- transceiver_resolver
- route_sector authority relevance
- BrainDisplayIntent decision ledger and scoring diagnostics
- CTAF/UNICOM advisory ownership

This is documentation/reporting only. No runtime behavior changed in this step.

## 1. transceiver_resolver Ownership Status

Completed paths:

- normal `Resolve`
- `ResolveAuthorityStations`
- `ResolveAirportCoverage`

Current status:

- Each migrated path reports structured evidence before brain decisions.
- Brain-owned live candidate projection is used when evidence exists.
- Old survivor/candidate vectors remain compatibility/parity data only.
- Normal Resolve no longer treats resolver survivor candidates as live authority when evidence exists.
- Authority-stations and airport-coverage live outputs are brain-owned when evidence exists.

Guardrail assertions:

- `authority=brain-evidence`
- compatibility-only candidates/vectors remain explicit
- `droppedBeforeBrain=0`
- old-vs-brain mismatch count `0`
- focused scenarios prove non-survivor evidence classes remain visible before brain decision

## 2. route_sector Authority Relevance Ownership Status

Current status:

- `AuthorityRelevanceSnapshot` carries structured authority evidence.
- Controller class evidence is visible.
- Route-scope polygon evidence is visible.
- Active/relevant polygon evidence is visible.
- Transceiver route proof evidence is visible.
- Duplicated-ATIS proof evidence is visible.
- Brain-owned preview classifies authority relevance decisions.
- Brain-owned live projection builds `relevantAuthorities` from evidence when evidence exists.
- Old route_sector survivor construction remains as `compatibilityRelevantAuthorities` for diagnostics/parity only.

Guardrail assertions:

- `authority=brain-evidence`
- `relevantAuthoritiesCompatibilityOnly=1`
- `liveRelevantAuthoritiesBrainOwned=1`
- source/evidence controller counts are populated
- `droppedBeforeBrainControllers=0`
- old-vs-brain mismatch count `0`

## 3. BrainDisplayIntent Status

Current status:

- Display decision ledger exists.
- Every focused accepted completion has a structured display decision.
- Hidden-after-accept is visible through decision records and summary counters.
- Duplicate suppression, stage deferral, non-displayable rows, filtered/unknown rows, and fallback-hidden rows are ledgered.
- Normalized score/confidence diagnostics exist.
- Fail-soft preview exists.
- Scoring is diagnostic-only and does not change final display behavior.
- HNL relation-fact protection remains: high-confidence accepted relation fact keeps `HNL_02_CTR@126.500` displayable and visible.

Remaining gaps:

- overlay cap ledger
- phase publisher per-row reuse ledger
- upstream stable completion keys for every accepted completion
- future live score-driven behavior must first run in preview/parity mode

## 4. CTAF/UNICOM Status

Current status:

- CTAF/UNICOM source evidence exists.
- Compatibility projection evidence exists for the old lookup-to-row path.
- Brain-owned advisory preview exists.
- Live CTAF/UNICOM rows are brain-owned when source evidence exists.
- Old lookup projection remains compatibility/parity data only.
- `StationRequiresCompletion` bypass remains active and diagnosed, but is not the desired final ownership model.
- Standby assist is not wired to CTAF/UNICOM advisory decisions yet.

Guardrail assertion:

```text
authority=brain-evidence,source=2,preview=2,live=2,compatibility=2,mismatch=0,bypass=1,brainOwned=1
```

Focused behavior remains unchanged:

- available CTAF rows unchanged
- UNICOM fallback rows unchanged
- pending/failure empty-frequency compatibility behavior unchanged
- final display behavior unchanged

## 5. Remaining Architecture Offenders / TODOs

- Clean up or replace `StationRequiresCompletion` after synthetic advisory completions or equivalent brain-owned completion records exist.
- Add overlay cap ledger so capped rows and `+N more ATC` behavior are brain-visible.
- Add phase publisher per-row reuse ledger so reused rows remain accountable.
- Wire standby assist from brain-owned CTAF/UNICOM advisory decisions in a controlled behavior-changing step.
- Quarantine or remove legacy departure/arrival/enroute board modules after harness callers are proven safe.
- Clean up compatibility vectors only after all live callers are proven to use brain-owned projections.
- Continue auditing remaining source fact loss modules, including parser drops, stale feed starvation, airport-frequency row drops, terminal-authority source filtering, network-plan no-match cases, and flight-plan fallback selection.

## 6. Do-Not-Regress Contract

- Modules report evidence.
- Brain owns decisions.
- Migrated paths must keep `droppedBeforeBrain=0`.
- Hidden-after-accept must stay visible.
- False negatives are worse than false positives for XVatsim.
- Scoring must stay bounded and normalized.
- Hard blocks must be explicit, rare, separate from score size, and test-covered.
- Compatibility vectors must not become authority when evidence exists.
- Compatibility paths should only be removed after replacement live brain-owned callers are proven safe.

## 7. Current Verification

Build command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: PASS.

Focused migrated-path scenario groups:

- transceiver_resolver normal/authority/airport coverage evidence and authority scenarios: covered by saved focused scenarios.
- route_sector authority evidence/proof/preview/authority scenarios: covered by saved focused scenarios.
- BrainDisplayIntent ledger/scoring/fail-soft scenarios: covered by saved focused scenarios.
- CTAF/UNICOM source evidence/advisory preview/advisory authority scenarios: PASS, 3 focused scenarios.

Full saved regression command:

```powershell
$scenarios = @(Get-ChildItem '.\tools\regression_harness\scenarios\*.scn' | Sort-Object Name); $count = 0; foreach ($scenario in $scenarios) { & '.\build\tools\XVatsimRegressionHarness.exe' $scenario.FullName *> $null; if ($LASTEXITCODE -ne 0) { Write-Host "FAILED $($scenario.Name)"; exit $LASTEXITCODE }; $count++ }; Write-Host "Passed $count scenarios"
```

Result: PASS, `278` scenarios.
