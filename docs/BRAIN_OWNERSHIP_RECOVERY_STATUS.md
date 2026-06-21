# Brain Ownership Recovery Status

Checkpoint: after transceiver_resolver, route_sector authority relevance, BrainDisplayIntent diagnostics, and CTAF/UNICOM advisory migration.

Core contract:

- Modules report facts and evidence.
- Brain-owned workers make accept, reject, display, hide, suppress, advisory, and relevance decisions.
- Migrated paths must keep `droppedBeforeBrain=0`.
- Hidden-after-accept must remain visible in the display decision ledger.
- False negatives are higher severity than false positives for XVatsim.
- Scoring must remain bounded and normalized; hard blocks are explicit categories, not giant weights.
- Compatibility vectors are diagnostics/parity only when evidence exists.

## Completed

### transceiver_resolver

The three public transceiver_resolver decision paths are migrated to evidence-ledger inputs with brain-owned live projections:

- normal `Resolve`
- `ResolveAuthorityStations`
- `ResolveAirportCoverage`

The resolver still emits compatibility candidates, but migrated live output comes from brain-owned workers when evidence exists:

- `BuildBrainRadioRangeWorkerOutput`
- `BuildBrainOwnedAuthorityStationsCandidateSnapshot`
- `BuildBrainOwnedAirportCoverageCandidateSnapshot`

Guardrails assert `authority=brain-evidence`, compatibility-only candidates, `droppedBeforeBrain=0`, and old-vs-brain mismatch `0` for the focused migrated scenarios.

### route_sector authority relevance

`AuthorityRelevanceSnapshot` carries structured controller, polygon, transceiver proof, and duplicated-ATIS proof evidence.

Brain-owned route authority logic provides:

- preview: `BuildBrainAuthorityRelevanceDecisionPreview`
- live projection: `BuildBrainOwnedAuthorityRelevanceSnapshot`

Live `AuthorityRelevanceSnapshot::relevantAuthorities` is brain-owned when evidence exists. Old route_sector survivor construction remains in `compatibilityRelevantAuthorities` for diagnostics/parity only.

Guardrails assert `authority=brain-evidence`, `relevantAuthoritiesCompatibilityOnly=1`, `liveRelevantAuthoritiesBrainOwned=1`, `droppedBeforeBrainControllers=0`, and old-vs-brain mismatch `0`.

### BrainDisplayIntent

BrainDisplayIntent now has:

- a structured display decision ledger
- hidden-after-accept counters and decision records
- normalized score/confidence diagnostics
- fail-soft preview recommendations
- HNL relation-fact protection proving high-confidence accepted relation facts remain displayed

Scoring and fail-soft recommendations are diagnostic-only. They do not yet change final display behavior.

Remaining display gaps:

- overlay cap ledger
- phase publisher per-row reuse ledger
- upstream stable completion keys for every accepted completion

### CTAF/UNICOM

CTAF/UNICOM now has:

- source evidence from lookup facts
- compatibility projection evidence for old lookup-to-row behavior
- brain-owned advisory preview decisions
- brain-owned live row projection when source evidence exists

The old lookup projection remains compatibility/parity data only. `StationRequiresCompletion` remains active and diagnosed as temporary compatibility behavior. Standby assist is not wired to CTAF/UNICOM advisory decisions yet.

Focused guardrails assert:

```text
authority=brain-evidence,source=2,preview=2,live=2,compatibility=2,mismatch=0,bypass=1,brainOwned=1
```

## Remaining Offenders / TODOs

- Clean up or replace `StationRequiresCompletion` after synthetic advisory completions or equivalent brain-owned completion records exist.
- Add overlay cap ledger so capped rows and `+N more ATC` behavior are brain-visible.
- Add phase publisher per-row reuse ledger so reused rows remain accountable.
- Wire standby assist from brain-owned CTAF/UNICOM advisory decisions in a controlled behavior-changing step.
- Quarantine or remove legacy departure/arrival/enroute board modules after harness callers are proven safe.
- Clean up compatibility vectors only after all live callers are proven to use brain-owned projections.
- Continue auditing remaining source fact loss modules, including parser drops, stale feed starvation, airport-frequency row drops, terminal-authority source filtering, network-plan no-match cases, and flight-plan fallback selection.

## Current Verification

- Build: PASS.
- Focused migrated-path groups:
  - transceiver_resolver normal/authority/airport coverage evidence and authority scenarios: covered by saved focused scenarios.
  - route_sector authority evidence/proof/preview/authority scenarios: covered by saved focused scenarios.
  - BrainDisplayIntent decision ledger, scoring, and fail-soft scenarios: covered by saved focused scenarios.
  - CTAF/UNICOM source evidence/advisory preview/advisory authority scenarios: PASS, 3 focused scenarios.
- Full saved regression suite: PASS, 278 scenarios.

## Do Not Regress

- Do not use compatibility vectors as authority when evidence exists.
- Do not add module-side accept/reject/hide/display decisions.
- Do not allow migrated paths to drop candidates before brain evidence.
- Keep hidden-after-accept visible and explain every hide/suppress/defer decision.
- Treat false negatives as worse than false positives.
- Keep evidence scores bounded and normalized.
- Keep hard blocks explicit, rare, separate from score size, and test-covered.
- Do not remove compatibility paths until replacement live brain-owned callers are proven safe.
