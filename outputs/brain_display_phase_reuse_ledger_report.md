# Step 57 - BrainDisplayIntent Phase Reuse Ledger Report

## 1. Files changed

- `brain/include/XVatsim/brain/BrainTypes.h`
- `brain/include/XVatsim/brain/PhaseSnapshotPublisher.h`
- `brain/src/BrainDisplayIntent.cpp`
- `brain/src/BrainOwnedRuntime.cpp`
- `brain/src/PhaseSnapshotPublisher.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/brain_display_phase_reuse_fresh_current_row.scn`
- `tools/regression_harness/scenarios/brain_display_phase_reuse_last_proven_current_incomplete.scn`
- `tools/regression_harness/scenarios/brain_display_phase_reuse_fresh_displaces_previous.scn`
- `tools/regression_harness/scenarios/brain_display_phase_reuse_plan_mismatch_blocked.scn`
- `tools/regression_harness/scenarios/brain_display_phase_reuse_stage_mismatch_blocked.scn`
- `tools/regression_harness/scenarios/brain_display_phase_reuse_frequency_mismatch_blocked.scn`
- `tools/regression_harness/scenarios/brain_display_phase_reuse_role_mismatch_blocked.scn`
- `tools/regression_harness/scenarios/brain_display_phase_reuse_stale_blocked.scn`
- `tools/regression_harness/scenarios/brain_display_phase_reuse_near_cap_linked.scn`
- `tools/regression_harness/scenarios/brain_display_phase_reuse_no_candidate.scn`
- `outputs/brain_display_phase_reuse_ledger_report.md`

## 2. Phase reuse ledger fields added

Added `PhaseSnapshotReuseDecisionRecord` with:

- `phaseReuseDecisionId`
- `displayDecisionId`
- `capDecisionId`
- `sourceDecisionId`
- `sourceEvidenceId`
- `subjectKey`
- `callsign`
- `role`
- `frequency`
- `endpoint`
- `airportIcao`
- `previousWorkflowStage`
- `currentWorkflowStage`
- `previousPlanKey`
- `currentPlanKey`
- `previousSnapshotKey`
- `currentSnapshotKey`
- `previousBoardIndex`
- `currentBoardIndex`
- `reuseCandidate`
- `reusedFromPreviousSnapshot`
- `freshCurrentEvidenceAvailable`
- `freshCurrentEvidenceAccepted`
- `freshCurrentEvidenceIncomplete`
- `reusedBecauseCurrentIncomplete`
- `displacedByFreshEvidence`
- `staleReuseBlocked`
- `reuseAllowed`
- `reuseBlockedReason`
- `reuseDecision`
- `sourceEvidenceLinked`
- `sourceEvidenceLinkStatus`
- `confidenceLevel`
- `fallbackUsed`

`FinalDisplayStationSnapshot` now carries diagnostic-only `displayDecisionId` and `overlayCapDecisionId` so phase reuse can link back to display/source/cap decisions where available.

## 3. Summary fields added

Added `PhaseSnapshotReuseSummary` with:

- `phaseReuseDecisionCount`
- `freshCurrentRowCount`
- `reusedLastProvenRowCount`
- `displacedByFreshEvidenceCount`
- `blockedReuseCount`
- `staleReuseBlockedCount`
- `planMismatchBlockedCount`
- `stageMismatchBlockedCount`
- `frequencyMismatchBlockedCount`
- `roleMismatchBlockedCount`
- `noReuseCandidateCount`
- `phaseReuseLedgerBrainOwned`
- `displayBehaviorChanged`

## 4. Fresh versus reused row classification

Fresh rows are recorded as `fresh-current-row` when the current candidate snapshot is displayable and accepted by the publisher. Rows reused from the previous proven snapshot are recorded as `reused-last-proven-row`, with `reusedBecauseCurrentIncomplete=1` when reuse occurs during a pending/incomplete current refresh.

Fresh current evidence that replaces an older proven row is ledgered separately as `displaced-by-fresh-current-row`.

## 5. Reuse block reasons

Blocked reuse decisions now distinguish:

- `blocked-stale-reuse`
- `blocked-plan-mismatch`
- `blocked-stage-mismatch`
- `blocked-frequency-mismatch`
- `blocked-role-mismatch`
- `no-reuse-candidate`

These are diagnostic classifications on the publisher result. Existing publish selection behavior remains unchanged for existing runtime calls.

## 6. Linkage to display/source/cap decisions

BrainDisplayIntent writes diagnostic `displayDecisionId` onto final rows and the overlay cap ledger writes `overlayCapDecisionId` onto visible final rows. The phase publisher then preserves those IDs in reuse records along with source evidence metadata.

The focused near-cap reuse scenario proves a reused row carrying:

- `displayDecision=display:cap39`
- `capDecision=overlay-cap|39`
- `sourceEvidence=source:cap39`

## 7. Final board parity proof

The publisher still returns the same snapshot selected by the pre-existing store/reuse path. The new ledger is emitted on `PhaseSnapshotPublishResult`; it is not fed back into display selection, row ordering, or overlay rendering.

## 8. Overlay cap / `+N more ATC` parity proof

Focused Step 57 included the Step 55 cap scenarios for no cap, one hidden row, multiple hidden rows, duplicate-hidden, stage-deferred, non-displayable, and CTAF near-cap rows. All passed unchanged. Cap-hidden rows remain cap decisions, not phase reuse decisions.

## 9. CTAF/UNICOM unaffected proof

The CTAF/UNICOM authority guardrail scenario passed. No live bypass authority returned, CTAF/UNICOM projection behavior was not modified, and advisory rows remain brain-owned.

## 10. Standby unaffected proof

The existing controller standby target scenario passed. Standby assist behavior, direct CTAF gate behavior, and COM writer behavior were not changed.

## 11. Focused scenario summaries

- Fresh current row: `fresh-current-row`, source/display/cap linked.
- Current incomplete with prior proven row: `reused-last-proven-row`.
- Fresh evidence replaces prior row: `displaced-by-fresh-current-row`.
- Plan mismatch: `blocked-plan-mismatch`.
- Stage mismatch: `blocked-stage-mismatch`.
- Frequency mismatch: `blocked-frequency-mismatch`.
- Role mismatch: `blocked-role-mismatch`.
- Stale prior snapshot: `blocked-stale-reuse`.
- Reused row near overlay cap: reuse record remains distinct from cap record and carries the cap decision ID.
- No candidate: `no-reuse-candidate`.
- Duplicate/stage/non-displayable hidden rows: covered by existing cap scenarios and remain separate from reuse records.

## 12. Build command/result

Command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: passed.

## 13. Focused scenario command/result

Command: ran 20 focused Step 57 scenarios with `build\tools\XVatsimRegressionHarness.exe`.

Result:

```text
focused-step57-passed=20
```

## 14. Full saved regression command/result

Command: ran every `tools/regression_harness/scenarios/*.scn` through `build\tools\XVatsimRegressionHarness.exe`.

Result:

```text
full-regression-passed=390
```

## 15. Known gaps after phase reuse ledger

- Runtime publish requests still do not pass a product plan key, so plan mismatch diagnostics are covered by focused harness probes rather than live product metadata.
- Hidden cap, duplicate, stage-deferred, and non-displayable rows remain owned by the display/cap ledgers; the phase reuse ledger only covers published/reused snapshot rows and reuse candidates.
- No behavior was changed for final display, row ordering, overlay cap, `+N more ATC`, CTAF/UNICOM, standby assist, or COM writes.
