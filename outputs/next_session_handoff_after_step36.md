# XVatsim Next Session Handoff After Step 36

Prepared at the stopping point after Step 36. This is a next-session working brief for continuing the brain-ownership recovery without re-discovering the repo state from scratch.

## Current Project Context

XVatsim is an X-Plane / VATSIM companion plugin that coexists with xPilot.

The product purpose is clutter reduction:

- xPilot may show many frequencies within radio range.
- XVatsim should reduce that clutter and show the pilot the frequencies that likely belong to them, using filed flight plan, aircraft position, route context, phase of flight, active controllers, and advisory sources.

Product failure policy:

- False positive: showing an extra frequency is bad but recoverable.
- False negative: hiding a relevant frequency is the worst failure.
- When evidence is uncertain, incomplete, ambiguous, or conflicting, XVatsim should fail soft: prefer display, display-with-warning, defer, or needs-more-evidence over silently hiding a potentially relevant frequency.

Current expected version context:

- Current version is XVatsim 1.0.4.
- The user expects this cleanup/recovery release may become v1.2 after full testing.

## Core Architecture Rule

Modules report evidence.

The brain owns decisions.

No worker/helper/source module should accept, reject, hide, display, suppress, veto, or decide relevance. Modules may parse, normalize, calculate, and report facts. The brain weighs those facts and produces explicit decision records.

Do not regress this rule.

## Brain Evidence Scoring Policy

The decision model is now evidence-weighted, bounded, normalized, and fail-soft.

Do not reintroduce giant scores such as 100 vs 1. That recreates the old bug class where one fact becomes an unreviewable veto.

Current scoring principles:

- High confidence: about 0.80 to 1.00.
- Medium confidence: about 0.50 to 0.79.
- Low confidence: about 0.20 to 0.49.
- Fallback/unknown: about 0.05 to 0.19.
- Hard block is a separate explicit category, not a huge numeric weight.

Hard blocks should be rare and reserved for impossible-to-render or safety-critical cases, such as an empty or invalid frequency that cannot actually be written/rendered.

Missing data is not rejection.

Fallback inference must not override high-confidence accepted facts.

HNL-class rule:

- A high-confidence accepted relation fact outranks fallback polygon inference.
- This protects the previous `HNL_02_CTR@126.500` failure class where a relevant center was accepted upstream but later hidden by display inference.

Primary docs:

- `docs/BRAIN_EVIDENCE_SCORING_MODEL.md`
- `docs/BRAIN_OWNERSHIP_RECOVERY_STATUS.md`

## Completed Major Migrations

### transceiver_resolver

All three major transceiver resolver paths have been migrated to evidence-ledger source plus brain-owned live output:

- Normal `TransceiverResolver::Resolve`
- `ResolveAuthorityStations`
- `ResolveAirportCoverage`

Current status:

- Resolver records evidence before legacy survivor filtering matters.
- Old `candidates` vectors remain compatibility/parity data only.
- Brain-owned workers build live outputs when evidence exists.
- Focused guardrails assert `authority=brain-evidence`, compatibility-only candidates, `droppedBeforeBrain=0`, and old-vs-brain mismatch zero.

Do not let resolver `candidates` become authority again when evidence exists.

### route_sector authority relevance

`RouteSectorResolver::ResolveBrainScheduledAuthorityVerification` has been migrated from hidden survivor filtering to:

- structured authority evidence ledger
- brain-owned authority preview
- brain-owned live `relevantAuthorities` projection

Current status:

- `AuthorityRelevanceSnapshot` carries structured evidence for controllers, route-scope polygons, active polygons, transceiver route proof, and duplicated-ATIS proof.
- Old route_sector survivor construction remains as `compatibilityRelevantAuthorities` only.
- Live `relevantAuthorities` is brain-owned when evidence exists.
- Focused guardrails assert `authority=brain-evidence`, `liveRelevantAuthoritiesBrainOwned=1`, `relevantAuthoritiesCompatibilityOnly=1`, `droppedBeforeBrainControllers=0`, and old-vs-brain mismatch zero.

route_sector may compute geometry, route-key, token, and proof facts. The brain owns authority relevance.

### BrainDisplayIntent

BrainDisplayIntent is inside `brain/`, so it is allowed to make display policy decisions. The migration so far made those decisions visible.

Current status:

- Display decision ledger exists.
- Hidden-after-accept is visible.
- Duplicate suppression is visible.
- Stage deferral is visible.
- Non-displayable rows are visible.
- Filtered/unknown/hidden relation suppression is visible.
- Fallback-hidden accepted centers are visible.
- HNL protected relation-fact scenario remains protected.
- Normalized score/confidence diagnostics exist.
- Fail-soft preview recommendations exist.
- Scoring is diagnostic-only and does not drive final display behavior yet.

Known BrainDisplayIntent gaps:

- Upstream stable completion keys remain incomplete.
- Overlay cap / `+N more ATC` is not ledgered yet.
- Phase publisher reuse is not per-row ledgered yet.
- Scores and fail-soft recommendations do not drive live output yet.

Do not enable score-driven live display behavior without preview/parity and focused tests.

### CTAF/UNICOM

CTAF/UNICOM rows previously bypassed the normal accepted-completion path. They have been migrated to brain-owned advisory row projection.

Current status:

- CTAF/UNICOM source evidence exists.
- Projection evidence exists.
- Brain-owned advisory preview exists.
- Live CTAF/UNICOM rows are projected from brain-owned advisory decisions when source evidence exists.
- Old lookup projection remains compatibility/parity data only.
- `StationRequiresCompletion` bypass remains active but is explicitly diagnosed as temporary compatibility behavior.
- Standby assist is not wired to CTAF/UNICOM advisory decisions yet.

Focused CTAF/UNICOM guardrail:

`authority=brain-evidence,source=2,preview=2,live=2,compatibility=2,mismatch=0,bypass=1,brainOwned=1`

`bypass=1` is expected right now. It has not been cleaned up yet.

## Latest Completed Step

### Step 36: Standby Assist CTAF/UNICOM Integration Audit

Audit-only. No runtime behavior changed.

Report:

- `outputs/standby_assist_ctaf_unicom_integration_migration_map.md`

Key findings:

- Standby assist planning already lives mostly in the brain.
- `BuildBrainOwnedStandbyAssistPlan` consumes final display rows, not CTAF/UNICOM advisory decisions.
- Current standby eligibility explicitly excludes `CTAF` and `UNICOM`.
- The plugin can write COM1 standby through `RadioStateSampler::SetCom1StandbyFrequency`, but only after a brain side-effect decision and user opt-in.
- No structured standby recommendation/write decision ledger exists yet.
- The radio_state writer validates COM1 standby dataref and frequency, but only returns a boolean result. It does not return detailed failure evidence.

Step 36 verification:

- Build passed.
- Full saved regression passed: 278 scenarios.
- Runtime behavior changed: no.

## Current Standby Assist Facts

Current standby assist path:

- `brain/src/BrainOwnedRuntime.cpp::BuildBrainOwnedStandbyAssistPlan`
- `brain/src/BrainOwnedRuntime.cpp::DecideBrainOwnedStandbyAssistSideEffect`
- `plugin/src/XVatsimPlugin.cpp::ApplyStandbyRecommendation`
- `modules/radio_state/src/RadioStateSampler.cpp::SetCom1StandbyFrequency`
- `brain/src/BrainOwnedRuntime.cpp::ApplyBrainOwnedStandbyAssistResult`
- `brain/src/BrainOrchestrator.cpp::FormatFinalDisplayStationLine`

Important behavior:

- Planning input is `FinalDisplaySnapshot`.
- It does not currently read CTAF/UNICOM advisory decisions.
- `IsStandbyEligibleRole` excludes `Atis`, `Ctaf`, `Unicom`, and `Other`.
- It only writes COM1 standby.
- User setting `standby_assist` must be enabled.
- Brain side-effect logic has a one-write latch.
- The writer checks dataref existence, dataref type, writability, parsed channel, and valid COM range.
- Writer returns only boolean success/failure.
- Display row gets `*Standby*` only when the target is not tuned and standby load/write succeeded or was already present.

## Recommended Next Step

Proceed with Step 37:

Standby assist recommendation evidence ledger only.

This must be visibility only:

- Do not wire CTAF/UNICOM into live standby assist yet.
- Do not change standby target selection.
- Do not change COM write behavior.
- Do not write to radios in new situations.
- Do not alter final display behavior.

Step 37 should add a diagnostic-only standby recommendation ledger that records:

- every final display row considered for standby
- current eligibility decisions
- role skip reasons
- frequency skip reasons
- already-active / already-standby facts
- COM1-only policy facts
- user opt-in setting
- selected target if any
- no-target reason
- current CTAF/UNICOM advisory candidates as evidence but not live authority yet
- missing decision count

This ledger should prove current behavior before standby assist is expanded to CTAF/UNICOM.

## Exact Next User Instruction

Use this exact prompt to start the next coding session:

```text
Step 36 passes.

Proceed to Step 37: standby assist recommendation evidence ledger only.

Scope:

* brain-owned standby assist planning
* standby side-effect decision diagnostics
* COM1 standby write-result evidence shape if practical
* harness assertions/scenarios as needed

Goal:

Add a structured standby recommendation evidence ledger so every displayed row or advisory candidate considered for standby assist has an explicit brain-owned decision record.

This is visibility only. Do not change standby behavior.

Hard limits:

* Do not wire CTAF/UNICOM into live standby assist yet.
* Do not change standby target selection.
* Do not change COM write behavior.
* Do not write to radios in new situations.
* Do not modify CTAF/UNICOM live projection.
* Do not modify BrainDisplayIntent behavior.
* Do not modify transceiver_resolver.
* Do not modify route_sector.
* Do not patch HNL.
* Do not perform broad cleanup.

Task:

Add a diagnostic-only standby assist decision ledger.

Minimum per-candidate fields:

* standbyDecisionId
* subjectKey
* sourceDomain: display-row | ctaf-unicom-advisory | controller-completion | unknown
* sourceDecisionId if available
* sourceEvidenceId if available
* endpoint if applicable
* airportIcao if applicable
* callsign
* role
* frequency
* workflowStage
* planKey
* boardIndex
* displayRelation if available
* candidateVisibleInFinalBoard
* acceptedByAdvisory if applicable
* advisoryDecision if applicable
* sourceConfidence if applicable
* confidenceLevel
* fallbackUsed
* positiveScore
* negativeScore
* hardBlock
* hardBlockReason
* alreadyCom1Active
* alreadyCom2Active
* alreadyCom1Standby
* targetCom
* eligible
* skipReason
* finalRecommendation:

  * recommend-com1-standby
  * skip-active
  * skip-already-standby
  * skip-role-not-eligible
  * skip-empty-frequency
  * skip-guard-frequency
  * skip-pending-lookup
  * skip-lookup-failed
  * skip-fallback-lower-confidence
  * skip-duplicate
  * skip-stage-deferred
  * needs-more-evidence
  * no-target

Minimum side-effect/write diagnostic fields:

* sideEffectDecisionId
* standbyDecisionId
* standbyAssistEnabled
* latchKey
* latchConsumed
* writeAllowed
* writeAttempted
* writeSucceeded if known
* writerTarget
* targetFrequency
* failureReason if known
* displayStandbyMarkerApplied

Minimum summary fields:

* standbyEvidenceCount
* standbyCandidateCount
* advisoryCandidateCount
* selectedTargetCount
* writeDecisionCount
* writeAttemptCount
* writeSuccessCount
* writeFailureCount
* skippedEmptyFrequencyCount
* skippedPendingLookupCount
* skippedLookupFailedCount
* skippedGuardFrequencyCount
* skippedRoleNotEligibleCount
* skippedAlreadyActiveCount
* standbyRecommendationsBrainOwned

Requirements:

1. Preserve current standby behavior exactly.
2. Preserve current CTAF/UNICOM exclusion from live standby eligibility.
3. If CTAF/UNICOM advisory decisions are visible to the standby ledger, they must be preview/evidence only for this step.
4. Existing controller standby assist behavior must remain unchanged.
5. Add focused scenarios proving:

   * current controller standby target selection remains unchanged
   * CTAF row is visible to the ledger but skipped with `skip-role-not-eligible`
   * UNICOM fallback row is visible to the ledger but skipped with `skip-role-not-eligible`
   * pending CTAF lookup is skipped with pending/failure/empty-frequency reason and never write eligible
   * standby disabled blocks write with explicit reason
   * already COM1 standby loaded blocks write with explicit reason
6. No new live write behavior is allowed.

Report:

Write:

outputs/standby_assist_decision_ledger_report.md

The report must include:

1. Files changed.
2. Standby decision ledger fields added.
3. Side-effect/write diagnostic fields added.
4. Summary fields added.
5. What runtime behavior remains unchanged.
6. Focused scenario summaries.
7. Build command/result.
8. Focused scenario command/result.
9. Full saved regression command/result.
10. Known gaps left for later steps.

Acceptance criteria:

* Build passes.
* Focused standby assist ledger scenarios pass.
* Full saved regression suite passes.
* No standby target behavior changes.
* No new COM write behavior.
* CTAF/UNICOM advisory rows are visible to standby diagnostics but not live eligible yet.
* Standby decisions are visible as structured records.
```

## Remaining Roadmap After Step 37

Likely next steps:

1. Standby assist CTAF/UNICOM advisory preview integration
   - CTAF/UNICOM advisory decisions become standby preview candidates.
   - Still no live writes.

2. Dry-run parity tests
   - Prove no pending/failed/empty CTAF can be written.
   - Prove direct CTAF can be recommended in preview.
   - Decide whether UNICOM fallback should be write-eligible or recommendation-only.

3. Opt-in live behavior wiring
   - Only after preview passes.
   - Only with `standby_assist=true`.
   - Probably direct CTAF first.
   - UNICOM fallback needs separate product decision.

4. Radio writer result ledger
   - Distinguish dataref missing, not writable, parse failure, invalid channel, and success.

5. Overlay cap ledger
   - Identify capped rows behind `+N more ATC`.

6. Phase publisher per-row reuse ledger
   - Identify candidate rows displaced by last-proven snapshot reuse.

7. Clean up compatibility vectors and bypasses
   - Only after all live callers are proven safe.

8. Legacy departure/arrival/enroute board module quarantine/removal
   - Only after harness callers are migrated or removed.

## Current Repo State Warning

The working tree is intentionally messy from the controlled recovery steps. Do not broadly clean, reformat, or squash behavior.

Use narrow steps.

Every next task should report:

- files changed
- exact behavior changed, or no behavior changed
- focused scenario result
- full saved regression result
- whether runtime/display behavior changed
- remaining known gaps
- direct link to the output report

No broad patching.

No drive-by cleanup.

No hidden authority.

No module-side decisions.

## Useful Recent Output Reports

- `outputs/brain_ownership_recovery_checkpoint_after_ctaf_unicom.md`
- `outputs/ctaf_unicom_advisory_migration_consolidation_report.md`
- `outputs/ctaf_unicom_advisory_authority_flip_report.md`
- `outputs/ctaf_unicom_advisory_preview_report.md`
- `outputs/ctaf_unicom_source_evidence_visibility_report.md`
- `outputs/ctaf_unicom_brain_ownership_migration_map.md`
- `outputs/standby_assist_ctaf_unicom_integration_migration_map.md`

## Last Known Verification

From Step 36:

- Build command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

- Build result: passed.

- Full regression command:

```powershell
$scenarios = @(Get-ChildItem '.\tools\regression_harness\scenarios\*.scn' | Sort-Object Name); $count = 0; foreach ($scenario in $scenarios) { & '.\build\tools\XVatsimRegressionHarness.exe' $scenario.FullName *> $null; if ($LASTEXITCODE -ne 0) { Write-Host "FAILED $($scenario.Name)"; exit $LASTEXITCODE }; $count++ }; Write-Host "Passed $count scenarios"
```

- Full regression result: passed 278 scenarios.

No build or regression was rerun for this handoff document because it is documentation-only and was created after the verified Step 36 stopping point.
