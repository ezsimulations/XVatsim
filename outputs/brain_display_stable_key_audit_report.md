# Step 59 - BrainDisplayIntent Stable Completion Key Audit

## 1. Files changed

- `brain/include/XVatsim/brain/BrainTypes.h`
- `brain/include/XVatsim/brain/BrainDisplayIntent.h`
- `brain/include/XVatsim/brain/PhaseSnapshotPublisher.h`
- `brain/src/BrainDisplayIntent.cpp`
- `brain/src/PhaseSnapshotPublisher.cpp`
- `tools/regression_harness/src/main.cpp`
- Focused scenario expectations updated:
  - `tools/regression_harness/scenarios/brain_display_overlay_cap_no_cap_reached.scn`
  - `tools/regression_harness/scenarios/brain_display_overlay_cap_one_hidden_row.scn`
  - `tools/regression_harness/scenarios/brain_display_overlay_cap_multiple_hidden_rows.scn`
  - `tools/regression_harness/scenarios/brain_display_overlay_cap_duplicate_separate.scn`
  - `tools/regression_harness/scenarios/brain_display_overlay_cap_stage_deferred_separate.scn`
  - `tools/regression_harness/scenarios/brain_display_overlay_cap_non_displayable_separate.scn`
  - `tools/regression_harness/scenarios/brain_display_overlay_cap_ctaf_row_ledgered.scn`
  - `tools/regression_harness/scenarios/brain_display_source_link_synthetic_legacy_missing.scn`
  - `tools/regression_harness/scenarios/brain_display_phase_reuse_last_proven_current_incomplete.scn`
  - `tools/regression_harness/scenarios/brain_display_phase_reuse_fresh_displaces_previous.scn`
  - `tools/regression_harness/scenarios/brain_display_phase_reuse_frequency_mismatch_blocked.scn`
  - `tools/regression_harness/scenarios/brain_display_phase_reuse_role_mismatch_blocked.scn`
  - `tools/regression_harness/scenarios/brain_display_phase_reuse_near_cap_linked.scn`

## 2. Stable key audit fields added

Added `BrainDisplayStableKeyAuditRecord` with:

- `stableKeyAuditDecisionId`
- `displayDecisionId`
- `overlayCapDecisionId`
- `phaseReuseDecisionId`
- `sourceEvidenceId`
- `sourceDecisionId`
- `subjectKey`
- `stableCompletionKey`
- `stableCompletionKeyPresent`
- `stableCompletionKeySource`
- `stableCompletionKeyStatus`
- `keyDerivationReason`
- `keyIncludesCallsign`
- `keyIncludesRole`
- `keyIncludesFrequency`
- `keyIncludesEndpoint`
- `keyIncludesAirport`
- `keyMatchesDisplayDecision`
- `keyMatchesCapDecision`
- `keyMatchesPhaseReuseDecision`
- `duplicateKeyDetected`
- `duplicateKeyGroup`
- `keyContinuityKnown`
- `keyChangedAcrossReuse`
- `unsafeSameKeyAcrossChangedFacts`
- `keyAuditWarning`
- `keyAuditWarningReason`

The same stable key diagnostic fields were added to phase reuse records where phase publisher reuse decisions own the continuity decision.

## 3. Summary fields added

Added stable-key audit summary counters for display and phase reuse:

- `stableKeyAuditDecisionCount`
- `stableKeyPresentCount`
- `stableKeyMissingCount`
- `fallbackDerivedKeyCount`
- `syntheticKeyCount`
- `legacyKeyCount`
- `duplicatedKeyCount`
- `changedAcrossReuseCount`
- `unsafeSameKeyCount`
- `keyLedgerLinkedDisplayCount`
- `keyLedgerLinkedCapCount`
- `keyLedgerLinkedPhaseReuseCount`
- `stableKeyAuditBrainOwned`
- `displayBehaviorChanged`

## 4. Stable/fallback/missing/duplicate key classification

Classification is diagnostic-only:

- Source-evidence rows get stable keys from `sourceEvidenceId`.
- Rows without source evidence get generated fallback keys from callsign, role, frequency, endpoint, and airport where available.
- Synthetic and legacy rows are explicitly classified from source linkage status.
- Duplicate keys are detected and warning-ledgered without changing dedupe behavior.
- Missing key counters are present for future inputs; focused cases generated fallback keys rather than producing missing keys.

## 5. Cross-ledger linkage

Display stable-key records link to display decisions and overlay cap decisions where available. Phase reuse records carry matching stable-key fields and are summarized separately by the phase publisher. The harness now emits:

- `BrainDisplayStableKeyAuditSummary`
- `BrainDisplayStableKeyAuditDecisions`
- `PhasePublisherStableKeySummary`

## 6. Reuse continuity key proof

Focused phase reuse probes show:

- Reused last-proven rows preserve source-evidence stable keys.
- Reused rows near overlay cap remain linked to their display/cap decision IDs.
- Fresh current rows receive fallback keys when source evidence is unavailable.
- Frequency and role mismatch probes set `keyChangedAcrossReuse=1` with `keyWarningReason=changed-across-reuse`.

## 7. Duplicate/unsafe key warning proof

The duplicate-hidden focused scenario now gives the visible and duplicate-hidden CAP00 rows the same source evidence ID. The stable-key audit reports:

- `duplicated=2`
- `duplicate=1`
- `duplicateGroup=source-evidence|src-duplicate-key`
- `warningReason=duplicate-stable-key`

Frequency and role mismatch probes prove changed-key warnings across reuse. No unsafe-same-key case is produced by the current focused fixtures because fallback keys include the changed role/frequency facts.

## 8. Final board parity proof

Final display behavior is unchanged. Focused scenarios retained the existing display/cap expectations, including visible rows, hidden overlay-cap rows, duplicate-hidden rows, stage-deferred rows, and non-displayable rows.

## 9. Overlay cap / +N more ATC parity proof

Overlay cap summaries remained unchanged in focused scenarios:

- No cap reached: `moreAtc=0`
- One hidden accepted row: `moreAtc=1`
- Multiple hidden accepted rows: `moreAtc=2`
- Duplicate-hidden, stage-deferred, and non-displayable rows stay outside `+N more ATC`

## 10. Phase publish/reuse parity proof

Phase publish/reuse behavior remained unchanged. Focused scenarios still report the same reuse decisions:

- `fresh-current-row`
- `reused-last-proven-row`
- `displaced-by-fresh-current-row`
- `blocked-frequency-mismatch`
- `blocked-role-mismatch`

The new fields only annotate the already-selected reuse outcome.

## 11. CTAF/UNICOM unaffected proof

The CTAF row near cap remains advisory/source-evidence owned and behavior-identical. The stable-key audit now records:

- `sourceEvidence=ctaf-unicom:departure:KELP`
- `sourceDecision=advisory:KELP_CTAF`
- `stableKey=source-evidence|ctaf-unicom:departure:KELP`

The CTAF/UNICOM authority guardrail still passes and no live bypass authority returned.

## 12. Standby unaffected proof

The focused standby controller scenario still passes. No standby assist target selection, direct CTAF gate behavior, or COM writer behavior changed.

## 13. Focused scenario summaries

Command: ran 16 focused Step 59 scenarios with `build\tools\XVatsimRegressionHarness.exe`.

Result: all focused scenarios passed.

Scenarios:

- `brain_display_overlay_cap_no_cap_reached.scn`
- `brain_display_overlay_cap_one_hidden_row.scn`
- `brain_display_overlay_cap_multiple_hidden_rows.scn`
- `brain_display_overlay_cap_duplicate_separate.scn`
- `brain_display_overlay_cap_stage_deferred_separate.scn`
- `brain_display_overlay_cap_non_displayable_separate.scn`
- `brain_display_overlay_cap_ctaf_row_ledgered.scn`
- `brain_display_source_link_synthetic_legacy_missing.scn`
- `brain_display_phase_reuse_last_proven_current_incomplete.scn`
- `brain_display_phase_reuse_fresh_displaces_previous.scn`
- `brain_display_phase_reuse_frequency_mismatch_blocked.scn`
- `brain_display_phase_reuse_role_mismatch_blocked.scn`
- `brain_display_phase_reuse_near_cap_linked.scn`
- `brain_display_intent_honors_center_relation_fact.scn`
- `ctaf_unicom_bypass_retirement_authority_guardrail.scn`
- `standby_assist_decision_ledger_controller_target_unchanged.scn`

## 14. Build command/result

Command:

```powershell
& 'C:\Users\DARRON\Documents\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: passed. `XVatsimBrain`, `XVatsimPlugin`, `XVatsimPreflightBuilder`, and `XVatsimRegressionHarness` built successfully.

## 15. Focused scenario command/result

Command:

```powershell
$scenarios = @(
  'brain_display_overlay_cap_no_cap_reached.scn',
  'brain_display_overlay_cap_one_hidden_row.scn',
  'brain_display_overlay_cap_multiple_hidden_rows.scn',
  'brain_display_overlay_cap_duplicate_separate.scn',
  'brain_display_overlay_cap_stage_deferred_separate.scn',
  'brain_display_overlay_cap_non_displayable_separate.scn',
  'brain_display_overlay_cap_ctaf_row_ledgered.scn',
  'brain_display_source_link_synthetic_legacy_missing.scn',
  'brain_display_phase_reuse_last_proven_current_incomplete.scn',
  'brain_display_phase_reuse_fresh_displaces_previous.scn',
  'brain_display_phase_reuse_frequency_mismatch_blocked.scn',
  'brain_display_phase_reuse_role_mismatch_blocked.scn',
  'brain_display_phase_reuse_near_cap_linked.scn',
  'brain_display_intent_honors_center_relation_fact.scn',
  'ctaf_unicom_bypass_retirement_authority_guardrail.scn',
  'standby_assist_decision_ledger_controller_target_unchanged.scn'
)
foreach ($s in $scenarios) {
  & '.\build\tools\XVatsimRegressionHarness.exe' "tools\regression_harness\scenarios\$s" *> $null
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
```

Result: passed; all 16 focused scenarios passed.

## 16. Full saved regression command/result

Command:

```powershell
$scenarios = @(Get-ChildItem '.\tools\regression_harness\scenarios\*.scn' | Sort-Object Name)
$count = 0
foreach ($scenario in $scenarios) {
  & '.\build\tools\XVatsimRegressionHarness.exe' $scenario.FullName *> $null
  if ($LASTEXITCODE -ne 0) {
    Write-Output "FAILED $($scenario.Name)"
    exit $LASTEXITCODE
  }
  $count++
}
Write-Output "Passed $count scenarios"
```

Result: `Passed 397 scenarios`.

## 17. Known gaps after stable key audit

- Upstream stable completion keys are still incomplete; generated fallback keys remain diagnostic warnings rather than behavior inputs.
- Missing-key counters exist, but current focused coverage mostly generates fallback keys instead of truly missing keys.
- Stable key audit is not yet used to alter dedupe, completion, phase reuse, overlay cap, or display behavior.
- Unsafe-same-key warning support exists; current focused mismatch probes produce changed-key warnings because fallback keys include role and frequency.
