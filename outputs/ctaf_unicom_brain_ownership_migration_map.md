# CTAF/UNICOM Brain Ownership Migration Map

Step 30 audited CTAF/UNICOM creation, insertion, replacement, display, and publishing paths. This is audit-only; no runtime behavior was changed.

Core finding: CTAF/UNICOM rows are advisory rows, not controller relevance rows, but they still bypass the normal accepted-completion path. BrainDisplayIntent now records display-format decisions for rows that reach it, but there is no CTAF/UNICOM-specific source evidence ledger or synthetic advisory completion proving why those rows were accepted, replaced, deferred, hidden, or displayed.

## Summary Findings

- `ctaf_lookup` reports source facts, but the brain receives only airport, resolved, available, and frequency. Failure count, last attempt, pending fetch, request status, HTTP status class, payload/parse failure, and source confidence are dropped before the brain can ledger them.
- Legacy departure/arrival modules create CTAF/UNICOM `BoardStationSnapshot` rows directly.
- `BrainOwnedRuntime` exempts CTAF/UNICOM rows from the accepted-completion filter.
- `BrainOwnedRuntime` then removes CTAF/UNICOM rows from departure/arrival boards and appends lookup-derived CTAF/UNICOM rows after the normal completion gate.
- UNICOM fallback is chosen by `BuildCtafStationFromLookupFact` when a lookup is resolved but no CTAF is available. This is a brain-side advisory decision, but it does not currently produce a CTAF/UNICOM decision record.
- Unresolved lookup facts can still become an empty-frequency CTAF station, which display intent later rejects as non-displayable. That rejection is visible as display intent output, but the lookup/pending reason is not.
- Display intent marks CTAF/UNICOM as `acceptedByRelevance=false`, so displayed CTAF/UNICOM rows do not have accepted relevance completions and do not participate in hidden-after-accept counters.
- Phase snapshot reuse and overlay caps can preserve, replace, or hide CTAF/UNICOM rows without a CTAF/UNICOM-specific decision ledger.

## Audit Map

| File / Function | Current Behavior | Classification | Completion or Decision Today | Evidence / Decision Record Needed | Focused Regression Needed |
| --- | --- | --- | --- | --- | --- |
| `modules/ctaf_lookup/include/XVatsim/modules/ctaf_lookup/CtafLookupService.h`, `CtafLookupEntry` | Source result carries `resolved`, `available`, `frequency`, `lastAttemptTickSeconds`, and `failureCount`. | Source lookup evidence. | No completion or brain decision. Only a reduced subset reaches brain-owned publisher facts. | `CtafUnicomSourceEvidence` with airport, endpoint, resolved, available, frequency, last attempt/age, failure count, pending fetch, source status, confidence, and broad failure reason. | Lookup available/unavailable/failure scenarios prove all source fields are visible before any advisory row is built. |
| `modules/ctaf_lookup/src/CtafLookupService.cpp`, `Lookup` | Normalizes airport, returns cached resolved data, rate-limits retry, starts async fetch, and returns current cache entry while unresolved. | Source lookup evidence; source availability fact. | No completion or decision. Brain later sees only the reduced fact built by plugin code. | Evidence should distinguish cache hit, unresolved pending, fetch in progress, retry suppressed, failed start, stale/age, and failure count. | Pending lookup does not silently create an unexplained empty CTAF row; source evidence shows pending/unresolved reason. |
| `modules/ctaf_lookup/src/CtafLookupService.cpp`, `LookupSync` | Downloads VATSIM AIP station JSON. `404` sets `resolved=true, available=false`; `200` with CTAF station sets `available=true`; failures and parse exceptions set failure state. | Source lookup evidence plus parser hygiene. | No completion or decision. HTTP/parse/failure details are not preserved into brain facts. | Evidence should include request succeeded, status code class, empty payload, max payload exceeded, parse exception, no CTAF station found, frequency string, and fallback eligibility. | 404/no-CTAF resolves to explicit UNICOM fallback evidence; parse/download failure remains unresolved evidence and does not look like authoritative no-CTAF. |
| `modules/ctaf_lookup/src/CtafLookupService.cpp`, `RunManualCtafQuery` | Parses `.ctaf` command, runs synchronous lookup, and formats a visible manual query line: CTAF frequency, UNICOM fallback, usage, or lookup failed. | Source lookup evidence plus display/status formatting. | No board row completion. The manual line is committed to brain manual-query state but not represented as a CTAF/UNICOM advisory decision. | Manual query decision ledger with command parse evidence, airport, lookup result, final line verdict, confidence, fallback used, and expiry. | Manual `.ctaf KXXX` success, UNICOM fallback, bad command, and lookup failure each produce a manual-query decision record. |
| `modules/departure/src/DepartureModule.cpp`, `DepartureModule::Collect` | If `ctafLookupService` exists, creates a CTAF station when available, a UNICOM station at `122.800` when resolved but no CTAF, or an empty-frequency CTAF station when unresolved; appends it directly to the departure board. | Accepted-completion bypass in legacy module; source lookup facts converted into board rows. | No `BrainOwnedCandidateCompletion`. In current brain-owned publishing, these legacy rows are later removed/replaced, but direct legacy callers can still receive them. | Module should report lookup/source evidence only. Brain should create synthetic advisory completions or CTAF/UNICOM advisory decisions. | Legacy departure board CTAF/UNICOM source row is visible in evidence but does not become live without a brain-owned advisory decision. |
| `modules/arrival/src/ArrivalModule.cpp`, `ArrivalModule::Collect` | Same direct CTAF/UNICOM station creation and append behavior for the arrival airport. | Accepted-completion bypass in legacy module; source lookup facts converted into board rows. | No completion. Current publisher removes/replaces rows in brain-owned flow, but direct legacy callers can still see them. | Same as departure: evidence-only module output plus brain-owned advisory decision/synthetic completion. | Legacy arrival board CTAF/UNICOM source row is visible in evidence but live display comes from brain-owned advisory decision. |
| `plugin/src/XVatsimPlugin.cpp`, frame CTAF lookup near `gCtafLookupService.Lookup(...)` | When flight context is active, the plugin looks up departure and destination CTAF facts each frame and times the work as `ctafUs/ctafMs`. | Source lookup integration point. | No decision. Plugin decides when lookup is attempted based on active flight context and non-empty airport identifiers. | Brain-owned work-scope evidence should record flight context active, endpoint airport, lookup attempted/skipped reason, and lookup timing/status. | Flight-context inactive, missing airport, departure lookup, and arrival lookup cases all show source work-scope evidence. |
| `plugin/src/XVatsimPlugin.cpp`, `BuildBrainOwnedCtafLookupFact` | Converts `CtafLookupEntry` into `BrainOwnedCtafLookupFact` with only airport, resolved, available, and frequency. Drops failure count and last attempt tick. | Source fact narrowing; source evidence loss. | No completion or decision. | Preserve full source evidence or add a parallel evidence ledger passed into brain-owned publisher. | Failure/backoff/pending lookup facts survive plugin-to-brain transfer and can be asserted in harness output. |
| `brain/include/XVatsim/brain/BrainOwnedRuntime.h`, `BrainOwnedCtafLookupFact` | Brain-owned publisher fact type has only airport, resolved, available, and frequency. | Source lookup evidence, currently incomplete. | No decision field and no ledger. | Add endpoint, source state, age, failure/pending/fallback facts, advisory confidence, and source reason fields. | Publisher fact summary proves source facts exist before station projection. |
| `brain/src/BrainOwnedRuntime.cpp`, `BuildCtafStationFromLookupFact` | Converts source fact to station: available -> CTAF, resolved-but-not-available -> UNICOM `122.800`, unresolved -> empty-frequency CTAF. Sets tuned flag from radios. | Brain-owned advisory decision without ledger; accepted-completion bypass. | No synthetic completion. Display intent may later record display-format or non-displayable decision, but source/advisory decision is missing. | `BrainCtafUnicomDecisionRecord` with endpoint, airport, source fact, chosen role, chosen frequency, fallback used, unavailable reason, confidence, positive/negative scores, hardBlock, and final advisory verdict. | Available CTAF, resolved no CTAF/UNICOM, and unresolved pending each produce one advisory decision before display intent. |
| `brain/src/BrainOwnedRuntime.cpp`, `StationRequiresCompletion` | Returns false for CTAF/UNICOM, allowing those roles through `FilterBrainOwnedBoardByAcceptedCompletions` without accepted completions. | Accepted-completion bypass. | No completion required today. | Replace with synthetic advisory completions or a parallel advisory decision gate so the bypass is explicit and ledgered. | A board CTAF/UNICOM row without advisory decision is counted as bypass; after migration bypass count must be zero. |
| `brain/src/BrainOwnedRuntime.cpp`, `FilterBrainOwnedBoardByAcceptedCompletions` | Keeps CTAF/UNICOM rows without checking `completions`; rejects other rows lacking accepted completions. | Accepted-completion bypass and compatibility behavior. | No per-row decision for the bypass. | Diagnostic ledger should record `completionNotRequiredCompatibility=true` until replaced; final design should require CTAF/UNICOM advisory decision. | Board with unapproved controller plus CTAF proves controller is rejected but CTAF is explicitly accounted for by advisory decision, not a silent exception. |
| `brain/src/BrainOwnedRuntime.cpp`, `RemoveCtafStations` | Removes all CTAF/UNICOM rows from departure and arrival boards before appending lookup-derived rows. | Compatibility replacement behavior; display/publisher mutation. | No decision record for removed legacy CTAF/UNICOM rows. | Record legacy source row removal/replacement evidence: source board, removed role/frequency, replacement source, reason `ctaf-lookup-authoritative-for-advisory-row`. | Legacy board CTAF plus lookup CTAF proves old row removal and replacement are visible in diagnostics. |
| `brain/src/BrainOwnedRuntime.cpp`, `AppendUniqueStation` as used by `RunBrainOwnedPublisher` | Appends lookup-derived departure and arrival CTAF/UNICOM rows after normal completion filtering. Duplicate suppression is silent and endpoint-local. | Accepted-completion bypass; compatibility insertion behavior. | No CTAF/UNICOM advisory completion. Display intent may later record display decisions. | Append should be projection from advisory decision records. Duplicate suppression should emit kept/dropped advisory decision details. | Duplicate lookup/station scenarios prove one row is kept, dropped row has reason, and live row has advisory decision id. |
| `brain/src/BrainOwnedRuntime.cpp`, `RunBrainOwnedPublisher` | Filters boards by accepted completions, removes CTAF/UNICOM from dep/arr boards, appends lookup-derived CTAF/UNICOM rows, then calls display intent. | Central accepted-completion bypass and live row insertion point. | No synthetic CTAF/UNICOM completion. Display intent has row-level display records but not advisory source authority. | Publisher should consume CTAF/UNICOM evidence + advisory decisions and build live rows from those decisions. Diagnostics should expose source count, advisory decision count, live CTAF/UNICOM count, bypass count. | Departure CTAF and arrival UNICOM focused scenario proves live rows are advisory-decision-owned and bypass count is zero. |
| `brain/src/BrainOwnedRuntime.cpp`, `BuildDisplayRelationFacts` | Builds display relation facts only from accepted normal completions with display relations. CTAF/UNICOM produce no relation/advisory facts. | Accepted-completion bypass / missing display evidence link. | No relation fact for CTAF/UNICOM. | Add CTAF/UNICOM advisory facts or synthetic completions consumed by display intent. | Display intent decision for CTAF/UNICOM includes advisory decision id, endpoint, and source confidence. |
| `brain/src/BrainDisplayIntent.cpp`, `StationIsAcceptedRelevanceSubject` | Returns false for CTAF/UNICOM, so display ledger records them as not accepted by relevance. | Compatibility behavior; display decision visibility gap. | Display decisions exist for rows that reach display intent, but `acceptedByRelevance=false`; hidden-after-accept counters do not apply. | Display decision should include `acceptedByAdvisory` or `acceptedByCtafUnicomDecision`, source/advisory decision id, and advisory confidence. | CTAF/UNICOM displayed row has exactly one display decision with advisory acceptance true and no missing decision. |
| `brain/src/BrainDisplayIntent.cpp`, `AddBoardStations` | Converts displayable CTAF/UNICOM rows into final display rows as `display-format-only` when no relation fact exists; non-displayable empty-frequency rows become `display-rejected-non-displayable`; duplicates become `display-rejected-duplicate`. | Display decision ledger exists, but advisory acceptance source is missing. | Display decision record exists only at display layer. No source/advisory verdict. | Link display decisions back to CTAF/UNICOM advisory decision records and include fallback/source confidence. | Available CTAF displays with display decision tied to advisory decision; unresolved empty CTAF rejection cites lookup pending/unresolved source reason. |
| `brain/src/BrainDisplayIntent.cpp`, `BuildDisplayBoard` | Stage selects departure, arrival, or enroute board. CTAF/UNICOM on non-selected departure/arrival board can be stage-deferred, but with `acceptedByRelevance=false`. | Display policy / stage defer; advisory visibility gap. | Display decision may exist, but not tied to advisory acceptance. | CTAF/UNICOM stage-defer decision should preserve advisory decision id and endpoint. | Departure-stage arrival UNICOM is deferred with advisory decision id and stage reason. |
| `brain/src/BrainDisplayIntent.cpp`, frequency ranking | CTAF/UNICOM are ranked near the bottom of departure/arrival display ordering. | Legal display prioritization. | No separate decision; ordering is implicit. | Optional overlay/display ledger should include priority/rank evidence if row is displaced by cap or ordering. | Many-row display scenario proves CTAF/UNICOM rank and any cap displacement are ledgered. |
| `brain/src/PhaseSnapshotPublisher.cpp`, `PublishPhaseSnapshot` | Stores displayable candidate snapshots as last-proven; if candidate is not displayable and verification is pending, reuses last-proven snapshot. This can preserve or replace CTAF/UNICOM rows after display intent. | Publisher replacement/reuse behavior. | No per-row CTAF/UNICOM decision explaining candidate-vs-reused result. | Phase publisher ledger should record when CTAF/UNICOM advisory rows are kept from candidate, reused from last-proven, or displaced by reuse. | Verification-pending scenario proves CTAF/UNICOM row identity and source decision survive or are explicitly replaced by phase reuse. |
| `brain/src/BrainOrchestrator.cpp`, `RoleLabel` / `FormatBoardLine` | Formats CTAF as `CTAF`; formats UNICOM as `NO CTAF / UNICOM`; appends callsign/frequency if present. | Display formatting. | No completion/decision, but this is legal formatting if fed by display ledger. | Overlay/render decision should reference final display station key and source display/advisory decision id. | UNICOM row displays expected label and has overlay-render ledger entry. |
| `brain/src/BrainOrchestrator.cpp`, `BuildOverlayViewModel` | Displays up to `kMaxDisplayedStations`; excess rows become `+N more ATC`. CTAF/UNICOM can be hidden by cap with no row identity in overlay output. | Overlay render/cap behavior. | No per-row overlay decision. | Overlay render ledger with row keys, rendered/capped verdict, cap reason, and source decision id. | Over-cap scenario with CTAF/UNICOM proves capped row identity is visible and not silently hidden. |
| `modules/airport_frequency_catalog/src/AirportFrequencyCatalogResolver.cpp`, `ClassifyFrequencyUse` | Classifies FRQ rows containing `UNICOM` or `CTAF` as roles. Current use is airport-frequency evidence for controller relevance, not live CTAF/UNICOM board insertion. | Source catalog evidence, not a direct insertion path. | No CTAF/UNICOM completion or display row from this path today. | Future CTAF/UNICOM evidence ledger may optionally include catalog confidence as supporting evidence, lower priority than direct CTAF lookup. | Catalog CTAF/UNICOM records are visible as evidence but do not independently create rows without advisory decision. |

## Current Decision Coverage

Today there are two partial layers of accountability:

- BrainDisplayIntent records display decisions for CTAF/UNICOM rows that reach display intent.
- The overlay renders final display rows and labels CTAF/UNICOM correctly.

But the missing ownership layer is before display intent:

- No CTAF/UNICOM source evidence ledger survives end-to-end.
- No synthetic accepted completion exists.
- No brain-owned advisory accept/reject/defer decision owns CTAF/UNICOM row creation.
- The publisher has explicit bypass exceptions for CTAF/UNICOM.
- Replacement of legacy CTAF rows with lookup rows is not ledgered.

## Recommended Evidence / Decision Shape

Add a CTAF/UNICOM advisory evidence ledger beside the existing publisher input:

```text
CtafUnicomSourceEvidence
  endpoint: departure|arrival|manual
  airportIcao
  lookupAttempted
  lookupSkippedReason
  cacheHit
  fetchInProgress
  requestSucceeded
  statusCodeClass
  resolved
  available
  frequency
  lastAttemptAgeSeconds
  failureCount
  fallbackEligible
  fallbackFrequency
  sourceConfidence
  sourceReason
```

Then add a brain-owned advisory decision record:

```text
CtafUnicomDecisionRecord
  decisionId
  endpoint
  airportIcao
  sourceEvidenceId
  decision: ctaf-display|unicom-fallback-display|defer-pending|hide-non-displayable|reject-invalid-source
  role
  frequency
  fallbackUsed
  positiveScore
  negativeScore
  confidence
  hardBlock
  reason
  liveRowEmitted
```

Display intent should then reference the advisory decision id with:

```text
acceptedByAdvisory=true
advisoryDecisionId=<id>
advisoryDomain=ctaf-unicom
```

## Safest Migration Order

1. Evidence ledger / synthetic completion design:
   Add CTAF/UNICOM source evidence and advisory decision types. Preserve existing display behavior. Include lookup pending/failure/fallback facts.

2. Visibility tests:
   Add focused scenarios for available CTAF, resolved-no-CTAF UNICOM fallback, unresolved/pending lookup, lookup failure, legacy row replacement, duplicate suppression, stage defer, phase reuse, and overlay cap.

3. Brain-owned CTAF/UNICOM preview:
   Produce diagnostic-only advisory decisions from evidence and prove preview reproduces current rows.

4. Authority/advisory flip if needed:
   Build live CTAF/UNICOM rows from advisory decisions instead of publisher bypass append paths.

5. Cleanup bypass exceptions:
   Remove or neutralize `StationRequiresCompletion` CTAF/UNICOM bypass, direct legacy module row creation, unledgered `RemoveCtafStations` replacement, and silent append duplicate behavior after live callers are proven safe.

## Build / Regression Status

Build command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: PASS.

Full saved regression command:

```powershell
$h = '.\build\tools\XVatsimRegressionHarness.exe'
$scenarios = Get-ChildItem -Path '.\tools\regression_harness\scenarios' -Filter '*.scn' | Sort-Object Name
$passed = 0
foreach ($s in $scenarios) {
  $result = & $h $s.FullName
  if ($LASTEXITCODE -ne 0) { Write-Host "FAILED $($s.Name)"; $result; exit $LASTEXITCODE }
  $passed++
}
Write-Host "ALL_SCENARIOS_PASSED count=$passed"
```

Result: PASS, `ALL_SCENARIOS_PASSED count=275`.

## Known Risks

- Current display decision records make CTAF/UNICOM visible at display intent, but not as accepted advisory completions.
- Unresolved lookup can become an empty-frequency CTAF row before display intent rejects it; the source reason is not visible in the display decision.
- UNICOM fallback currently has no explicit confidence, fallback reason, or source proof record.
- Legacy departure/arrival modules still directly construct CTAF/UNICOM rows.
- Overlay cap can hide CTAF/UNICOM identity behind `+N more ATC`.
- Manual CTAF lookup is a status/manual-query path, not a board row path, but it still lacks a structured source/decision ledger.
