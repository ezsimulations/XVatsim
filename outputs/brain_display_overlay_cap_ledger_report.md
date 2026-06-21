# Step 55: BrainDisplayIntent Overlay Cap Ledger

## 1. Files changed

- `brain/include/XVatsim/brain/BrainDisplayIntent.h`
- `brain/src/BrainDisplayIntent.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/brain_display_overlay_cap_no_cap_reached.scn`
- `tools/regression_harness/scenarios/brain_display_overlay_cap_one_hidden_row.scn`
- `tools/regression_harness/scenarios/brain_display_overlay_cap_multiple_hidden_rows.scn`
- `tools/regression_harness/scenarios/brain_display_overlay_cap_duplicate_separate.scn`
- `tools/regression_harness/scenarios/brain_display_overlay_cap_stage_deferred_separate.scn`
- `tools/regression_harness/scenarios/brain_display_overlay_cap_non_displayable_separate.scn`
- `tools/regression_harness/scenarios/brain_display_overlay_cap_ctaf_row_ledgered.scn`
- `outputs/brain_display_overlay_cap_ledger_report.md`

## 2. Overlay cap ledger fields added

Added `BrainDisplayOverlayCapDecisionRecord` with:

- `overlayCapDecisionId`
- `sourceDecisionId`
- `sourceEvidenceId`
- `displayDecisionId`
- `subjectKey`
- `callsign`
- `role`
- `frequency`
- `endpoint`
- `airportIcao`
- `displayRelation`
- `workflowStage`
- `boardIndexBeforeCap`
- `boardIndexAfterCap`
- `capLimit`
- `visibleBeforeCap`
- `visibleAfterCap`
- `cappedByOverlayLimit`
- `capReason`
- `contributesToMoreAtcCount`
- `moreAtcCountBeforeRow`
- `moreAtcCountAfterRow`
- `retainedVisibleRowCount`
- `cappedHiddenRowCount`
- `finalDisplayOutcome`
- `confidenceLevel`
- `fallbackUsed`
- `hardBlock`
- `hardBlockReason`

The ledger is built after the final display snapshot and display decision ledger are finalized. It does not write back into final display selection, ordering, stable hash, standby assist, CTAF/UNICOM projection, or overlay rendering.

## 3. Summary fields added

Added `BrainDisplayOverlayCapSummary` with:

- `overlayCapDecisionCount`
- `capLimit`
- `candidateBeforeCapCount`
- `visibleAfterCapCount`
- `cappedHiddenCount`
- `moreAtcCount`
- `contributesToMoreAtcCount`
- `nonCappedHiddenCount`
- `duplicateHiddenCount`
- `stageDeferredHiddenCount`
- `capLedgerBrainOwned`
- `overlayCapBehaviorChanged`

Harness output now emits:

- `BrainDisplayOverlayCapSummary`
- `BrainDisplayOverlayCapDecisions`

## 4. Capped rows versus other hidden rows

Capped rows are reported as `finalDisplayOutcome=hidden-overlay-cap`, `cappedByOverlayLimit=1`, and `contributesToMoreAtcCount=1`.

Non-cap hidden rows remain separate:

- Duplicate suppression: `finalDisplayOutcome=hidden-duplicate`
- Stage deferral: `finalDisplayOutcome=hidden-stage-deferred`
- Non-displayable rows: `finalDisplayOutcome=hidden-non-displayable`
- Other hidden rows: `finalDisplayOutcome=hidden-other`

Only `hidden-overlay-cap` rows increment the diagnostic `moreAtcCount`.

## 5. `+N more ATC` parity proof

The diagnostic cap limit mirrors the current overlay renderer limit of 40 displayed stations. Focused scenarios proved:

- 3 candidates: `moreAtc=0`
- 41 candidates: `moreAtc=1`
- 42 candidates: `moreAtc=2`
- Duplicate/stage/non-displayable rows do not inflate `moreAtc`

## 6. Final board parity proof

The ledger is generated from `output.finalDisplay` after final display construction and sorting. It never mutates `output.finalDisplay`.

Focused cap scenarios asserted unchanged final display counts through `BrainDisplayIntentDecisionSummary`, including `displayedFinal=41` and `displayedFinal=42` cases where overlay diagnostics capped rows only for ledger classification.

## 7. CTAF/UNICOM unaffected proof

The CTAF near-cap scenario shows a CTAF-shaped display row is ledgered by the overlay cap diagnostic without changing display behavior. Existing CTAF/UNICOM authority guardrail also passed and continued to report no live bypass authority.

No CTAF/UNICOM projection, bypass-retirement, missing-evidence, standby eligibility, or writer behavior was changed.

## 8. Standby unaffected proof

The focused controller standby scenario passed unchanged. The overlay cap ledger does not feed standby assist planning, selected targets, direct CTAF gate behavior, latch/write decisions, or COM writer diagnostics.

## 9. Focused scenario summaries

- `brain_display_overlay_cap_no_cap_reached.scn`: all rows visible, `moreAtc=0`.
- `brain_display_overlay_cap_one_hidden_row.scn`: one accepted row ledgered as `hidden-overlay-cap`, `moreAtc=1`.
- `brain_display_overlay_cap_multiple_hidden_rows.scn`: two accepted rows ledgered as capped, `moreAtc=2`.
- `brain_display_overlay_cap_duplicate_separate.scn`: duplicate row ledgered as `hidden-duplicate`, not counted in `moreAtc`.
- `brain_display_overlay_cap_stage_deferred_separate.scn`: stage-deferred row ledgered separately, not counted in `moreAtc`.
- `brain_display_overlay_cap_non_displayable_separate.scn`: offline row ledgered as `hidden-non-displayable`, not counted in `moreAtc`.
- `brain_display_overlay_cap_ctaf_row_ledgered.scn`: CTAF row ledgered near cap without changing CTAF/UNICOM behavior.
- `brain_display_intent_honors_center_relation_fact.scn`: HNL protected relation-fact behavior unchanged.
- `standby_assist_decision_ledger_controller_target_unchanged.scn`: controller standby behavior unchanged.
- `ctaf_unicom_bypass_retirement_authority_guardrail.scn`: no live CTAF/UNICOM bypass authority remains.

## 10. Build command/result

Command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: passed.

## 11. Focused scenario command/result

Command:

```powershell
$exe='build\tools\XVatsimRegressionHarness.exe'
$scenarios=@(
  'tools\regression_harness\scenarios\brain_display_overlay_cap_no_cap_reached.scn',
  'tools\regression_harness\scenarios\brain_display_overlay_cap_one_hidden_row.scn',
  'tools\regression_harness\scenarios\brain_display_overlay_cap_multiple_hidden_rows.scn',
  'tools\regression_harness\scenarios\brain_display_overlay_cap_duplicate_separate.scn',
  'tools\regression_harness\scenarios\brain_display_overlay_cap_stage_deferred_separate.scn',
  'tools\regression_harness\scenarios\brain_display_overlay_cap_non_displayable_separate.scn',
  'tools\regression_harness\scenarios\brain_display_overlay_cap_ctaf_row_ledgered.scn',
  'tools\regression_harness\scenarios\brain_display_intent_honors_center_relation_fact.scn',
  'tools\regression_harness\scenarios\standby_assist_decision_ledger_controller_target_unchanged.scn',
  'tools\regression_harness\scenarios\ctaf_unicom_bypass_retirement_authority_guardrail.scn'
)
foreach($scenario in $scenarios){
  & $exe $scenario
  if($LASTEXITCODE -ne 0){ exit $LASTEXITCODE }
}
```

Result: `focused-step55-passed=10`.

## 12. Full saved regression command/result

Command:

```powershell
$scenarios = @(Get-ChildItem '.\tools\regression_harness\scenarios\*.scn' | Sort-Object Name)
$count = 0
foreach ($scenario in $scenarios) {
  & '.\build\tools\XVatsimRegressionHarness.exe' $scenario.FullName *> $null
  if ($LASTEXITCODE -ne 0) {
    Write-Host "FAILED $($scenario.Name)"
    exit $LASTEXITCODE
  }
  $count++
}
Write-Host "full-regression-passed=$count"
```

Result: `full-regression-passed=379`.

## 13. Known gaps after overlay cap ledger

- `sourceEvidenceId` is currently empty for display-intent cap rows because the existing display decision record does not carry source evidence IDs.
- The cap limit is mirrored for diagnostics from the current overlay renderer value; a future cleanup could centralize the constant if desired.
- The ledger is available through brain output and harness diagnostics, but no operator UI surface was added in this diagnostics-only step.
