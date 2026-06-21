# Step 58 - Phase Publisher Plan-Key Linkage Report

## 1. Files changed

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- `brain/include/XVatsim/brain/PhaseSnapshotPublisher.h`
- `brain/src/BrainOwnedRuntime.cpp`
- `brain/src/PhaseSnapshotPublisher.cpp`
- `plugin/src/XVatsimPlugin.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/phase_publisher_plan_key_live_product_present.scn`
- `tools/regression_harness/scenarios/phase_publisher_plan_key_missing_current.scn`
- `tools/regression_harness/scenarios/phase_publisher_plan_key_same_plan.scn`
- `tools/regression_harness/scenarios/phase_publisher_plan_key_changed_plan.scn`
- `tools/regression_harness/scenarios/phase_publisher_plan_key_harness_mismatch.scn`
- `tools/regression_harness/scenarios/phase_publisher_plan_key_missing_previous.scn`
- `tools/regression_harness/scenarios/phase_publisher_plan_key_missing_current_context.scn`
- `outputs/phase_publisher_plan_key_linkage_report.md`

## 2. Product plan-key linkage fields added

Phase publish requests, stored slot metadata, and per-row phase reuse decisions now carry:

- `productPlanKey`
- `productPlanKeyAvailable`
- `productPlanKeySource`
- `productPlanKeyMissingReason`
- `previousProductPlanKey`
- `currentProductPlanKey`
- `planContinuityKnown`
- `planContinuityStatus`
- `planMismatchDiagnosticSource`
- `phaseReusePlanContextLinked`

Existing `previousPlanKey` and `currentPlanKey` remain present and now mirror the effective product plan key when available.

## 3. Summary fields added

Added `PhaseSnapshotPlanContextSummary` with:

- `phasePlanContextDecisionCount`
- `productPlanKeyAvailableCount`
- `productPlanKeyMissingCount`
- `liveProductPlanContextCount`
- `harnessPlanProbeCount`
- `missingPlanContextCount`
- `planContinuityKnownCount`
- `planContinuityUnknownCount`
- `livePlanMismatchCount`
- `harnessPlanMismatchCount`
- `phasePlanContextBrainOwned`
- `publishBehaviorChanged`

Harness output exposes this as `PhasePublisherPlanContextSummary`.

## 4. Live-product plan context capture

The plugin now passes its existing `planKey` into `BrainOwnedPublisherFactInput` as diagnostic product plan context. `BuildBrainOwnedPublisherInputFromFacts` copies it into `BrainOwnedPublisherInput`, and `RunBrainOwnedPublisher` passes it to `PublishPhaseSnapshot`.

When present, the source is `live-product`. This is diagnostic-only and does not affect publish selection.

## 5. Missing plan context reporting

When no product plan key is available, the phase ledger records:

- `productPlanKeyAvailable=0`
- `productPlanKeySource=unavailable`
- `productPlanKeyMissingReason=<explicit reason>`
- `planContinuity=missing-current-plan` or `missing-previous-plan`

Missing current plan and missing previous plan are distinct in the per-row ledger.

## 6. Harness plan probes remain distinct

Existing Step 57 harness plan mismatch probes now set `productPlanKeySource=harness`. Live product mismatches report `planMismatchSource=live-product` and increment `liveMismatch`; harness mismatches report `planMismatchSource=harness-probe` and increment `harnessMismatch`.

## 7. Final display parity proof

No display selection code was changed. The plan context is only copied into phase publish diagnostics after BrainDisplayIntent has already produced the final display snapshot.

## 8. Phase publish/reuse behavior parity proof

Publish selection behavior remains unchanged. The publisher still stores displayable candidates and reuses last proven snapshots under the same existing conditions. Plan mismatch classification is ledger-only.

## 9. Overlay cap / `+N more ATC` parity proof

Focused Step 58 included overlay cap scenarios, including one hidden row, multiple hidden rows, and CTAF near cap. All passed unchanged, preserving cap and `+N more ATC` behavior.

## 10. CTAF/UNICOM unaffected proof

The CTAF/UNICOM authority guardrail passed. No live bypass authority returned, and CTAF/UNICOM projection behavior was not modified.

## 11. Standby unaffected proof

The controller standby target scenario passed. Standby assist, direct CTAF gate behavior, and COM writer behavior were not changed.

## 12. Focused scenario summaries

- Live product plan present: `productPlanKeySource=live-product`.
- Missing current product plan: explicit missing reason and `missing-current-plan`.
- Same previous/current plan: `planContinuity=same-plan`.
- Changed previous/current live plan: `planContinuity=changed-plan`, `liveMismatch=1`.
- Harness plan mismatch: `planContinuity=harness-probe`, `harnessMismatch=1`.
- Missing previous plan: `missing-previous-plan`.
- Missing current plan with previous present: `missing-current-plan`.
- Fresh current row behavior: unchanged.
- Reused last-proven behavior: unchanged.
- Plan mismatch block behavior: unchanged and still diagnostic.

## 13. Build command/result

Command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: passed.

## 14. Focused scenario command/result

Command: ran 16 focused Step 58 scenarios with `build\tools\XVatsimRegressionHarness.exe`.

Result:

```text
focused-step58-passed=16
```

## 15. Full saved regression command/result

Command: ran every `tools/regression_harness/scenarios/*.scn` through `build\tools\XVatsimRegressionHarness.exe`.

Result:

```text
full-regression-passed=397
```

## 16. Known gaps after plan-key linkage

- Plan-key diagnostics are now available where the product supplies `planKey`; callers that omit it are explicitly reported as unavailable.
- Plan mismatch remains diagnostic-only. Publish selection and reuse policy were intentionally left unchanged.
- No behavior was changed for final display, row ordering, overlay cap, `+N more ATC`, CTAF/UNICOM, standby assist, direct CTAF gate, or COM writes.
