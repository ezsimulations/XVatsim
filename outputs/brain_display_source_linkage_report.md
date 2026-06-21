# Step 56 - BrainDisplayIntent Source Linkage Report

## 1. Files changed

- `brain/include/XVatsim/brain/BrainTypes.h`
- `brain/include/XVatsim/brain/BrainDisplayIntent.h`
- `brain/src/BrainDisplayIntent.cpp`
- `brain/src/BrainControllerRelevanceWorker.cpp`
- `brain/src/BrainOwnedRuntime.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/brain_display_overlay_cap_no_cap_reached.scn`
- `tools/regression_harness/scenarios/brain_display_overlay_cap_one_hidden_row.scn`
- `tools/regression_harness/scenarios/brain_display_overlay_cap_multiple_hidden_rows.scn`
- `tools/regression_harness/scenarios/brain_display_overlay_cap_duplicate_separate.scn`
- `tools/regression_harness/scenarios/brain_display_overlay_cap_stage_deferred_separate.scn`
- `tools/regression_harness/scenarios/brain_display_overlay_cap_non_displayable_separate.scn`
- `tools/regression_harness/scenarios/brain_display_overlay_cap_ctaf_row_ledgered.scn`
- `tools/regression_harness/scenarios/brain_display_source_link_synthetic_legacy_missing.scn`
- `outputs/brain_display_source_linkage_report.md`

## 2. Source linkage fields added

Display/cap records now carry:

- `sourceEvidenceId`
- `sourceEvidenceType`
- `sourceEvidenceDomain`
- `sourceEvidenceLinked`
- `sourceEvidenceLinkStatus`
- `sourceEvidenceMissingReason`
- `sourceDecisionId`
- `sourceDecisionLinked`
- `displayDecisionLinked`
- `capDecisionLinked`
- `linkageConfidence`
- `linkageFallbackUsed`

The row snapshots also carry source evidence metadata so BrainDisplayIntent can link display and cap decisions without changing board output.

## 3. Summary fields added

Added `BrainDisplaySourceLinkSummary` with:

- `displaySourceLinkDecisionCount`
- `displaySourceLinkedCount`
- `displaySourceMissingCount`
- `capSourceLinkedCount`
- `capSourceMissingCount`
- `syntheticRowCount`
- `legacyRowCount`
- `unknownSourceLinkCount`
- `sourceLinkageBrainOwned`
- `displayBehaviorChanged`

Harness output exposes this as `BrainDisplaySourceLinkSummary`.

## 4. Display decision source linkage

Display decisions now inherit source evidence from `BoardStationSnapshot` when available. Controller relevance rows receive a diagnostic controller source evidence ID, CTAF/UNICOM advisory rows inherit advisory/source evidence IDs, and fixture rows can provide explicit source metadata.

Rows without evidence are no longer ambiguous: they report statuses such as `unavailable`, `synthetic-row`, or `legacy-row` with a concrete missing reason.

## 5. Overlay cap source linkage

Overlay cap decisions inherit the display decision linkage fields. A cap record links to its display decision and carries source evidence when the display decision has it. If the cap row only has a display decision fallback, it remains `displayDecisionLinked=1` while `sourceDecisionLinked=0`.

## 6. Missing-linkage reasons

Missing linkage is recorded explicitly through `sourceEvidenceLinkStatus` and `sourceEvidenceMissingReason`. Covered reasons include unavailable source evidence, fixture synthetic rows, fixture legacy rows, and CTAF/UNICOM advisory source-evidence gaps.

## 7. Final board parity proof

Focused cap scenarios continue to assert the same final board rows and overlay body lines. The one-hidden-row scenario still emits the same visible 40 rows and `+1 more ATC`; only the diagnostic linkage fields changed.

## 8. `+N more ATC` parity proof

Step 55 overlay cap counters remain unchanged. Focused scenarios covered no cap, one hidden row, multiple hidden rows, duplicate-hidden rows, stage-deferred rows, non-displayable rows, and CTAF near-cap rows. Cap-hidden rows contribute to `moreAtcCount`; duplicate/stage/non-displayable rows remain separate.

## 9. CTAF/UNICOM unaffected proof

The CTAF near-cap scenario still displays the advisory row through brain-owned advisory projection and now links the cap/display records to `ctaf-unicom:departure:KELP`. The existing CTAF/UNICOM authority guardrail still passes with no live bypass authority restored.

## 10. Standby unaffected proof

Focused standby coverage included the existing controller standby target scenario. Standby assist behavior, direct CTAF gate behavior, and COM writer behavior were not changed.

## 11. Focused scenario summaries

- No cap reached: all rows visible, source linkage populated where provided, `moreAtcCount=0`.
- One cap-hidden controller row: hidden row is `hidden-overlay-cap` and keeps its source evidence.
- Multiple cap-hidden rows: hidden rows retain distinct source evidence.
- Duplicate plus cap: duplicate-hidden row stays separate from cap-hidden linkage.
- Stage-deferred plus cap: stage-deferred row remains separate from cap-hidden linkage.
- Non-displayable plus cap: non-displayable row remains separate from cap-hidden linkage.
- CTAF near cap: advisory row remains brain-owned and source-linked.
- Synthetic/legacy fixture: missing linkage is explicit and warning-free.
- HNL protected relation-fact: unchanged.
- Controller standby: unchanged.
- CTAF/UNICOM authority guardrail: unchanged.

## 12. Build command/result

Command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: passed.

## 13. Focused scenario command/result

Command: ran 11 focused Step 56 scenarios with `build\tools\XVatsimRegressionHarness.exe`.

Result:

```text
focused-step56-passed=11
```

## 14. Full saved regression command/result

Command: ran every `tools/regression_harness/scenarios/*.scn` through `build\tools\XVatsimRegressionHarness.exe`.

Result:

```text
full-regression-passed=380
```

## 15. Known gaps after source linkage

- Source evidence is now linked where available, but some legacy/synthetic rows still naturally report missing linkage.
- Linkage confidence is intentionally conservative: rows without source evidence use fallback diagnostics rather than invented evidence.
- Public display behavior, cap behavior, CTAF/UNICOM projection, standby assist, and COM writes remain unchanged.
