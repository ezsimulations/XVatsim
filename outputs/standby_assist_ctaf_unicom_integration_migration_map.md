# Standby Assist CTAF/UNICOM Integration Migration Map

Step 36 audit only. Runtime behavior was not changed.

## Scope

Audited standby recommendation and COM standby write paths with the CTAF/UNICOM advisory migration in mind. The current CTAF/UNICOM system has source evidence, advisory preview decisions, and brain-owned live row projection. This report documents how standby assist should later consume those advisory decisions safely.

## Executive Summary

Current standby assist is partly brain-owned already: target planning and the write/no-write side-effect decision live in `brain/src/BrainOwnedRuntime.cpp`. The plugin executes the approved side effect by calling the radio_state writer, and the writer validates the COM1 standby dataref and frequency before writing.

The current standby assist does not consume CTAF/UNICOM advisory decisions. It consumes the final display board rows after display intent/publisher processing. Its eligibility filter explicitly excludes `CTAF` and `UNICOM`, so brain-owned CTAF/UNICOM advisory rows can appear correctly in the UI hierarchy but are not eligible standby targets today.

There is no structured standby recommendation decision ledger yet. The current system can identify a target frequency, decide whether to write COM1 standby, and mark the row as `*Standby*`, but it does not produce evidence-backed records explaining target choice, skipped rows, CTAF/UNICOM advisory confidence, empty/pending lookup exclusion, duplicate handling, or write safety.

## Current Standby Assist Source And Write Paths

### 1. Standby target planning

- File/function: `brain/src/BrainOwnedRuntime.cpp::BuildBrainOwnedStandbyAssistPlan`
- Current behavior: Takes `workflowStage`, `planKey`, `RadioStateSnapshot`, and a `FinalDisplaySnapshot` board. It resets every station's `standby` flag, marks `next`, marks `tuned` when COM1 active matches the row frequency, then scans final display rows for eligible standby targets.
- Source used today: Final display rows, not accepted completions and not CTAF/UNICOM advisory decisions.
- Eligibility facts:
  - Allowed workflow stages are `Departure`, `Arrival`, and `Enroute`.
  - Empty plan key or invalid radio snapshot means no target.
  - Offline rows are skipped.
  - Only roles accepted by `IsStandbyEligibleRole` are considered.
  - Empty frequencies are skipped.
  - Guard/blocked frequencies `121.500` and `199.998` are skipped.
  - The chosen target is the first eligible row after the most recent COM1-active row.
  - Target already loaded is detected only against COM1 standby.
- CTAF/UNICOM impact: `IsStandbyEligibleRole` returns false for `StationRole::Ctaf` and `StationRole::Unicom`, so CTAF/UNICOM rows cannot be selected today even when displayed correctly.
- Brain-ownership classification: Brain-owned planning, but missing decision ledger. The role exclusion is a brain display/assist policy decision and should be ledgered before any CTAF/UNICOM expansion.
- Current decision record: None beyond `BrainOwnedStandbyAssistPlanOutput` fields.
- Future evidence/decision record needed: Per-row standby eligibility evidence with role, callsign, frequency, tuned state, source domain, source decision id if available, skip reason, candidate score/confidence, and final selected target.

### 2. Standby side-effect decision

- File/function: `brain/src/BrainOwnedRuntime.cpp::DecideBrainOwnedStandbyAssistSideEffect`
- Current behavior: If there is no target, resets the standby assist latch and returns no write. If the target latch key changed, resets one-write consumption. If standby assist is disabled, returns no write. If enabled, emits `targetFrequency`, `standbyLoaded`, and `shouldWriteCom1Standby` when the target is not already in COM1 standby and the latch has not consumed a write.
- Source used today: Output of `BuildBrainOwnedStandbyAssistPlan` and plugin setting `standbyAssistEnabled`.
- Brain-ownership classification: Brain-owned side-effect decision. Missing ledger for write/no-write reason and safety facts.
- Current decision record: `BrainOwnedStandbyAssistSideEffectDecision` only carries write yes/no, standbyLoaded, and targetFrequency.
- Future evidence/decision record needed: A standby side-effect decision record with opt-in setting, latch key, write already consumed, target already in COM1 standby, target COM, target frequency, write allowed, write attempted, write succeeded, and reason.

### 3. Plugin execution of the write

- File/function: `plugin/src/XVatsimPlugin.cpp::ApplyStandbyRecommendation`
- Current behavior: Builds the standby plan, asks the brain for a side-effect decision, and if `shouldWriteCom1Standby` is true calls `gRadioStateSampler.SetCom1StandbyFrequency(sideEffectDecision.targetFrequency)`. It then applies the result to the display board.
- Source used today: Brain-owned side-effect decision.
- Brain-ownership classification: Legal actuator execution of a brain decision. The plugin is not selecting the frequency, but it does execute the write and currently does not expose a structured write-result ledger.
- Current decision record: No durable structured record. The display board can show `*Standby*` only if the side effect succeeded or the frequency was already present.
- Future evidence/decision record needed: Write attempt/result facts should be returned to a standby ledger: writer called yes/no, target dataref available, dataref writable, parsed channel, valid channel, write result, and failure reason.

### 4. COM1 standby writer

- File/function: `modules/radio_state/src/RadioStateSampler.cpp::SetCom1StandbyFrequency`
- Current behavior: Resolves datarefs, rejects when COM1 standby dataref is missing, not integer, not writable, unparsable, or outside valid COM frequency range. If valid, writes the channel with `XPLMSetDatai`.
- Source used today: Frequency selected by the brain side-effect decision and passed through plugin execution.
- Brain-ownership classification: Radio_state is an actuator and validation layer. It should not choose the standby target; currently it does not. Its validations are legal hard-block facts that should be reported back to the brain.
- Current decision record: Boolean return only.
- Future evidence/decision record needed: Writer result ledger with hard-block facts: missing dataref, non-int dataref, not writable, parse failure, invalid COM channel, write attempted, write succeeded.

### 5. Display annotation

- File/function: `brain/src/BrainOwnedRuntime.cpp::ApplyBrainOwnedStandbyAssistResult`
- Current behavior: Returns the board unchanged if no target, bad target index, workflow stage is `Enroute`, or target station is already tuned. Otherwise sets the target station's `standby` flag based on `standbyLoaded`.
- File/function: `brain/src/BrainOrchestrator.cpp::FormatFinalDisplayStationLine`
- Current behavior: Renders `*Standby*` when `station.standby` is true and `station.tuned` is false.
- Brain-ownership classification: Display formatting of a standby assist result. Missing ledger link to the standby recommendation and write result.
- Current decision record: None beyond the row flag.
- Future evidence/decision record needed: Display link fields such as `standbyRecommendationId`, `standbyWriteResult`, and `standbyDisplayReason`.

### 6. Settings and opt-in

- Files/functions:
  - `plugin/src/XVatsimPlugin.cpp::EnableStandbyAssist`
  - `plugin/src/XVatsimPlugin.cpp::DisableStandbyAssist`
  - `modules/settings_store/src/SettingsStore.cpp::Load`
  - `modules/settings_store/src/SettingsStore.cpp::Save`
- Current behavior: User setting `standby_assist` enables or disables standby side effects. The overlay displays `ASST ON` or `ASST OFF`.
- Brain-ownership classification: Legal user opt-in gate. It must remain a hard gate before live CTAF/UNICOM standby wiring.
- Current decision record: Setting is present in `RadioStateSnapshot`, but no standby ledger explains when the setting blocked a write.
- Future evidence/decision record needed: `userOptIn`, `settingSource`, `settingBlockedWrite`, and `reason=standby-assist-disabled`.

## Current CTAF/UNICOM Advisory Inputs Available For Future Integration

- File/types: `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- Source evidence: `BrainOwnedCtafUnicomSourceEvidence`
- Projection evidence: `BrainOwnedCtafUnicomProjectionEvidence`
- Advisory decisions: `BrainOwnedCtafUnicomAdvisoryPreviewDecision`
- Authority summary: `BrainOwnedCtafUnicomAdvisoryAuthoritySummary`

Useful advisory fields for standby assist:

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

Current live CTAF/UNICOM projection is brain-owned when source evidence exists:

- File/function: `brain/src/BrainOwnedRuntime.cpp::BuildBrainOwnedPublisherOutput`
- Current behavior: Removes legacy CTAF/UNICOM rows, records compatibility projection evidence, builds advisory preview decisions, and appends live CTAF/UNICOM rows from `AppendCtafUnicomRowsFromAdvisoryDecisions` when source evidence exists.
- Important future guardrail: Standby assist should consume advisory decisions, not compatibility projection rows, and should require `advisoryAuthority=brain-evidence`, `liveRowsBrainOwned=true`, and `oldVsBrainMismatchCount=0` for advisory-driven standby recommendations.

## Brain-Ownership Violations And Risks Found

### A. No standby recommendation decision ledger

- Current behavior: The brain selects a standby target and decides whether to request a COM1 standby write, but there is no structured per-candidate or final recommendation ledger.
- Risk: A frequency can be written or marked standby without an inspectable evidence trail explaining why this row was selected over others.
- Classification: Brain-owned decision needing ledger.
- Future regression: A focused standby assist scenario should assert one decision record per displayed candidate row, one final selected target, explicit skip reasons for every non-target, and `missingStandbyDecisionCount=0`.

### B. CTAF/UNICOM excluded by role eligibility

- File/function: `brain/src/BrainOwnedRuntime.cpp::IsStandbyEligibleRole`
- Current behavior: `CTAF` and `UNICOM` return false.
- Risk: Brain-owned advisory rows can be displayed but never selected for standby assist. This is current intended behavior, but future CTAF/UNICOM integration must make the policy explicit and evidence-backed.
- Classification: Brain-owned assist policy decision needing ledger before behavior changes.
- Future regression: A focused scenario with a displayed CTAF row should first prove current behavior skips it with `reason=role-not-standby-eligible`, then future preview should prove advisory CTAF can become `recommend-com1-standby` only when explicitly enabled.

### C. Standby assist consumes final display rows instead of source decisions

- File/function: `brain/src/BrainOwnedRuntime.cpp::BuildBrainOwnedStandbyAssistPlan`
- Current behavior: Reads `FinalDisplaySnapshot board`.
- Risk: Advisory evidence, source confidence, fallback status, and pending/failed lookup state are lost by the time standby planning runs. A displayed row alone cannot tell whether it came from direct CTAF, UNICOM fallback, empty pending compatibility projection, or a failed lookup compatibility row.
- Classification: Hidden source-fact loss for standby decision accountability.
- Future regression: Advisory standby preview must assert direct CTAF, UNICOM fallback, pending lookup, failed lookup, and duplicate advisory row handling using advisory decision ids rather than final row text.

### D. Write result is boolean only

- File/function: `modules/radio_state/src/RadioStateSampler.cpp::SetCom1StandbyFrequency`
- Current behavior: Returns false for multiple distinct hard-block conditions.
- Risk: The brain cannot explain whether a standby write failed because of no dataref, not writable, invalid channel, parse failure, or another validation fact.
- Classification: Source/actuator availability fact not fully reported to the brain.
- Future regression: Radio writer dry-run/fake-writer scenarios should assert distinct failure reasons and ensure no final standby marker appears unless the write result is success or already-loaded.

### E. COM1-only behavior is implicit

- Current behavior: Planner checks COM1 active for `tuned`, checks COM1 standby for already loaded, and writer only writes COM1 standby. COM2 active is used by some general tuning checks elsewhere, but not this standby writer path.
- Risk: Future behavior could accidentally imply COM2 support or tune the wrong target without explicit policy.
- Classification: Compatibility behavior needing explicit decision ledger fields.
- Future regression: Standby ledger should assert `targetCom=COM1_STANDBY`, `com2WriteSupported=false`, and skip/accept reasons when COM2 is active or relevant.

### F. Enroute display annotation blocked after planning

- File/function: `brain/src/BrainOwnedRuntime.cpp::ApplyBrainOwnedStandbyAssistResult`
- Current behavior: Enroute can build a target and side-effect decision, but `ApplyBrainOwnedStandbyAssistResult` does not mark `*Standby*` during `Enroute`.
- Risk: Write behavior and display annotation can diverge without a ledger explaining stage policy.
- Classification: Brain-owned display/assist policy decision needing ledger.
- Future regression: Enroute standby scenario should assert whether write is allowed, display annotation suppressed, and reason `display-annotation-suppressed-enroute` or equivalent.

### G. Pending/failed CTAF compatibility rows currently can exist as empty-frequency rows

- Current CTAF/UNICOM behavior: Pending or failed lookup compatibility projection can preserve empty-frequency CTAF rows for parity, while final display may not render them.
- Risk: Future advisory standby integration must not recommend or write an empty/pending/failed advisory frequency.
- Classification: Future hard-block policy needed.
- Future regression: Pending and failed CTAF scenarios should assert `skip-empty-frequency`, `skip-pending-lookup`, or `skip-lookup-failed`, with `writeAllowed=false` and `writeAttempted=false`.

## Recommended Standby Decision Ledger Shape

Add a brain-owned standby recommendation ledger before wiring CTAF/UNICOM advisory decisions into live behavior.

Suggested per-candidate fields:

- `standbyDecisionId`
- `subjectKey`
- `sourceDomain`: `display-row`, `ctaf-unicom-advisory`, `controller-completion`, or `unknown`
- `sourceDecisionId`
- `sourceEvidenceId`
- `endpoint`
- `airportIcao`
- `callsign`
- `role`
- `frequency`
- `workflowStage`
- `planKey`
- `displayRelation`
- `boardIndex`
- `candidateVisibleInFinalBoard`
- `acceptedByAdvisory`
- `advisoryDecision`
- `sourceConfidence`
- `confidenceLevel`
- `fallbackUsed`
- `positiveScore`
- `negativeScore`
- `hardBlock`
- `hardBlockReason`
- `alreadyCom1Active`
- `alreadyCom2Active`
- `alreadyCom1Standby`
- `targetCom`
- `eligible`
- `skipReason`
- `finalRecommendation`: `recommend-com1-standby`, `skip-active`, `skip-already-standby`, `skip-role-not-eligible`, `skip-empty-frequency`, `skip-guard-frequency`, `skip-pending-lookup`, `skip-lookup-failed`, `skip-fallback-lower-confidence`, `skip-duplicate`, `skip-stage-deferred`, `needs-more-evidence`, or `no-target`

Suggested side-effect/write fields:

- `sideEffectDecisionId`
- `standbyDecisionId`
- `standbyAssistEnabled`
- `latchKey`
- `latchConsumed`
- `writeAllowed`
- `writeAttempted`
- `writeSucceeded`
- `writerTarget`
- `targetFrequency`
- `datarefAvailable`
- `datarefWritable`
- `parsedChannel`
- `validChannel`
- `failureReason`
- `displayStandbyMarkerApplied`

Suggested summary fields:

- `standbyEvidenceCount`
- `standbyCandidateCount`
- `advisoryCandidateCount`
- `selectedTargetCount`
- `writeDecisionCount`
- `writeAttemptCount`
- `writeSuccessCount`
- `writeFailureCount`
- `skippedEmptyFrequencyCount`
- `skippedPendingLookupCount`
- `skippedLookupFailedCount`
- `skippedGuardFrequencyCount`
- `skippedRoleNotEligibleCount`
- `skippedAlreadyActiveCount`
- `completionBypassCompatibilityOnly`
- `advisoryAuthority`
- `liveRowsBrainOwned`
- `standbyRecommendationsBrainOwned`

## Safe CTAF/UNICOM Standby Integration Design

Future standby assist should consume CTAF/UNICOM advisory decisions directly, with final board rows used only for stage/display context.

Direct CTAF:

- Accept as a standby candidate only when advisory decision is `ctaf-display`, frequency is non-empty, source confidence is high, `wouldEmitLiveRow=true`, and advisory authority is brain-evidence.

UNICOM fallback:

- Treat `unicom-fallback-display` as lower confidence than direct CTAF.
- Allow only if product policy wants fallback UNICOM in standby assist and the user opt-in remains active.
- Ledger `fallbackUsed=true` and `confidenceLevel=medium`.

Pending/unresolved/failure:

- `defer-pending`, `lookup-failed`, `hide-non-displayable`, and `reject-invalid-source` must not write COM1 standby.
- Pending and failure cases should be recommendations like `needs-more-evidence` or `skip-lookup-failed`, never a live write.

Duplicate/stage-deferred advisory rows:

- Duplicate CTAF/UNICOM advisory rows should be suppressed or lower-priority only through an explicit standby decision record.
- Stage-deferred rows should not drive standby unless the stage policy explicitly allows preparation for the next endpoint.

Active vs standby safety:

- Do not write a target already active on COM1.
- Consider COM2 active as a safety fact before writing COM1 standby.
- Do not write guard frequencies or empty frequencies.
- Keep COM1 standby as the only live target until COM2 support is intentionally designed and tested.

Opt-in:

- `standby_assist=true` must remain required for live writes.
- Dry-run/preview diagnostics may run with the setting off, but must show `writeAllowed=false`.

## Proposed Migration Order

1. Standby recommendation evidence ledger
   - Add per-row candidate evidence, skip reasons, selected target record, and side-effect/write result facts.
   - No behavior change.

2. Advisory preview integration
   - Add CTAF/UNICOM advisory decisions as candidate evidence.
   - Produce standby preview recommendations for direct CTAF, UNICOM fallback, pending, failed, duplicate, and stage-deferred cases.
   - No write behavior change.

3. Dry-run parity tests
   - Prove current controller standby behavior remains unchanged.
   - Prove advisory CTAF/UNICOM recommendations are visible but not live.
   - Assert no empty/pending/failed lookup can become write-eligible.

4. Opt-in live behavior wiring
   - Allow direct CTAF advisory decisions to drive COM1 standby only when `standby_assist=true`, advisory authority is brain-evidence, frequency is valid, and focused tests pass.
   - Decide separately whether UNICOM fallback is write-eligible or recommendation-only.

5. Safety and rollback tests
   - Fake writer tests for dataref missing, not writable, parse failure, invalid channel, already-active, already-standby, latch reuse, and setting disabled.
   - Rollback/kill switch should leave display rows unchanged and prevent writes.

## Focused Regression Recommendations

Add scenarios for:

- Existing controller standby assist still selects the same next controller and writes only when enabled.
- Displayed direct CTAF advisory row is visible to standby preview with `recommend-com1-standby` but no live behavior change during preview.
- UNICOM fallback advisory row is visible with lower confidence and explicit fallback reason.
- Pending CTAF lookup produces `skip-pending-lookup` and `writeAttempted=false`.
- Failed CTAF lookup produces `skip-lookup-failed` and `writeAttempted=false`.
- Empty CTAF frequency produces a hard-block skip separate from numeric score.
- Duplicate CTAF/UNICOM advisory rows produce one selected or one suppressed recommendation with duplicate reason.
- Stage-deferred advisory row does not write unless future policy explicitly allows it.
- COM1 active/COM1 standby/COM2 active safety facts are visible.
- Writer failures expose distinct reasons instead of only boolean false.

## Verification

Build command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

Build result: Passed.

Full saved regression command:

```powershell
$scenarios = @(Get-ChildItem '.\tools\regression_harness\scenarios\*.scn' | Sort-Object Name); $count = 0; foreach ($scenario in $scenarios) { & '.\build\tools\XVatsimRegressionHarness.exe' $scenario.FullName *> $null; if ($LASTEXITCODE -ne 0) { Write-Host "FAILED $($scenario.Name)"; exit $LASTEXITCODE }; $count++ }; Write-Host "Passed $count scenarios"
```

Full saved regression result: Passed 278 scenarios.

## Files Changed

- `outputs/standby_assist_ctaf_unicom_integration_migration_map.md`

No runtime files were changed.
