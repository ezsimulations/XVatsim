# Step 64: Source-Owned Stable Key Live-Consumption Readiness Report

## Files Changed

- `brain/include/XVatsim/brain/BrainDisplayIntent.h`
- `brain/src/BrainDisplayIntent.cpp`
- `brain/include/XVatsim/brain/PhaseSnapshotPublisher.h`
- `brain/src/PhaseSnapshotPublisher.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/brain_display_live_consumption_readiness_gate_off_default.scn`
- `tools/regression_harness/scenarios/brain_display_live_consumption_readiness_gate_on_clean.scn`
- `tools/regression_harness/scenarios/brain_display_live_consumption_readiness_missing_plan_blocked.scn`
- `tools/regression_harness/scenarios/phase_publisher_live_consumption_readiness_gate_on_clean.scn`
- `tools/regression_harness/scenarios/phase_publisher_live_consumption_readiness_drift_blocked.scn`
- `outputs/source_owned_stable_key_live_consumption_readiness_report.md`

## Scope

Step 64 adds a brain-owned readiness/proposal ledger for future live consumption of source-owned fallback polygon stable keys.

This is diagnostics only. No live behavior consumes the source-owned key.

Unchanged:

- final display behavior
- row ordering
- dedupe
- completion identity
- phase reuse
- overlay cap
- `+N more ATC`
- CTAF/UNICOM
- standby assist
- direct CTAF
- COM writer behavior
- `transceiver_resolver`
- `route_sector`
- HNL behavior

## Proposal Gate

Added a separate proposal/readiness gate to display intent and phase publisher request paths:

- `sourceOwnedFallbackStableKeyLiveConsumptionProposalEnabled`
- `sourceOwnedFallbackStableKeyLiveConsumptionProposalGateSource`

The proposal gate defaults OFF / not armed. It has no settings-store or plugin wiring in this step. Harness-only scenarios can arm it with:

- `stable_key.source_owned_fallback_live_consumption_proposal=true`
- `source_owned_fallback_stable_key_live_consumption_proposal_enabled=true`

Gate source diagnostics classify:

- `default`
- `settings-store`
- `harness`
- `unknown`

## Display Readiness Ledger

Added display readiness records and summary:

- `BrainDisplaySourceOwnedFallbackStableKeyLiveConsumptionReadinessRecord`
- `BrainDisplaySourceOwnedFallbackStableKeyLiveConsumptionReadinessSummary`

The ledger is built after the Step 63 shadow ledger and only reads shadow/dry-run diagnostics.

Per-record diagnostics include:

- proposal gate armed/source
- shadow gate enabled
- shadow recompute attempted
- shadow parity clean
- plan context available
- shadow drift detected
- blocked reason
- ready for future live consumption
- live behavior consumer enabled
- shadow behavior consumer enabled

Summary counters include:

- `proposalGateArmed`
- `shadowParityClean`
- `planContextAvailable`
- `missingPlanBlocked`
- `driftBlocked`
- `shadowNotAttemptedBlocked`
- `readinessBlocked`
- `readyForFutureLiveConsumption`
- `liveBehaviorConsumerEnabled`
- `shadowBehaviorConsumerEnabled`
- `behaviorChanged`

## Phase Readiness Ledger

Phase reuse decisions now carry matching live-consumption readiness diagnostics, and `PhaseSnapshotPublishResult` includes:

- `PhaseSnapshotSourceOwnedFallbackStableKeyLiveConsumptionReadinessSummary`

The phase readiness ledger is refreshed after stable-key duplicate marking and Step 63 shadow diagnostics, then summarized from final phase reuse decision records.

## Readiness Rules

A record is ready for future live consumption only when all are true:

- the proposal gate is armed;
- the Step 63 shadow gate is enabled;
- shadow recomputation was attempted;
- shadow parity is clean across hash, row ordering, dedupe, duplicate suppression, completion identity, phase reuse, overlay cap, and `+N more ATC`;
- plan context is present;
- no shadow drift is detected;
- Step 63 says the row is safe for future live opt-in;
- no source-owned behavior consumer is already enabled.

Blocked reasons include:

- `live-consumption-proposal-gate-not-armed`
- `behavior-consumer-already-enabled`
- `shadow-gate-disabled`
- `shadow-parity-not-attempted`
- `missing-plan-context`
- `shadow-parity-mismatch`
- shadow drift reason, such as `source-owned-phase-reuse-match-drift`
- `shadow-not-safe-for-future-live-opt-in`

## Focused Scenarios

New focused Step 64 scenarios:

- `brain_display_live_consumption_readiness_gate_off_default.scn`
- `brain_display_live_consumption_readiness_gate_on_clean.scn`
- `brain_display_live_consumption_readiness_missing_plan_blocked.scn`
- `phase_publisher_live_consumption_readiness_gate_on_clean.scn`
- `phase_publisher_live_consumption_readiness_drift_blocked.scn`

Focused guardrails rerun:

- `ctaf_unicom_bypass_retirement_authority_guardrail.scn`
- `standby_assist_decision_ledger_controller_target_unchanged.scn`
- `standby_assist_direct_ctaf_live_gate_on_selects_direct_ctaf.scn`

Focused result: passed 8 focused Step 64 scenarios.

## Proof Points

Default OFF / not armed:

- display proposal gate default reported `proposalGateArmed=0`
- readiness blocked with `live-consumption-proposal-gate-not-armed`
- live behavior consumer stayed `0`

Clean parity with plan context:

- display and phase armed harness probes reported `readyForFutureLiveConsumption=1`
- shadow parity clean was `1`
- plan context available was `1`
- live behavior consumer stayed `0`

Missing plan context:

- display missing-plan probe reported `planContextAvailable=0`
- readiness blocked with `missing-plan-context`
- ready count stayed `0`

Drift:

- phase plan-context drift probe reported `driftBlocked=1`
- readiness blocked with `source-owned-phase-reuse-match-drift`
- ready count stayed blocked for the drifted row

## Build

Command:

```powershell
& 'C:\Users\DARRON\Documents\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: passed. `XVatsimBrain`, `XVatsimPlugin`, `XVatsimPreflightBuilder`, and `XVatsimRegressionHarness` built successfully.

## Focused Verification

Command:

```powershell
$scenarios = @(
  'brain_display_live_consumption_readiness_gate_off_default.scn',
  'brain_display_live_consumption_readiness_gate_on_clean.scn',
  'brain_display_live_consumption_readiness_missing_plan_blocked.scn',
  'phase_publisher_live_consumption_readiness_gate_on_clean.scn',
  'phase_publisher_live_consumption_readiness_drift_blocked.scn',
  'ctaf_unicom_bypass_retirement_authority_guardrail.scn',
  'standby_assist_decision_ledger_controller_target_unchanged.scn',
  'standby_assist_direct_ctaf_live_gate_on_selects_direct_ctaf.scn'
)
foreach ($scenario in $scenarios) {
  & '.\build\tools\XVatsimRegressionHarness.exe' (Join-Path 'tools\regression_harness\scenarios' $scenario) | Out-Null
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
```

Result: passed 8 focused Step 64 scenarios.

## Full Saved Regression

Command:

```powershell
$scenarios = @(Get-ChildItem '.\tools\regression_harness\scenarios\*.scn' | Sort-Object Name)
$count = 0
foreach ($scenario in $scenarios) {
  & '.\build\tools\XVatsimRegressionHarness.exe' $scenario.FullName | Out-Null
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  $count++
}
```

Result: passed 413 scenarios.

## Final State

- Live source-owned fallback stable key behavior consumption remains disabled.
- Proposed live-consumption gate defaults OFF / not armed.
- Readiness requires clean shadow parity and plan context.
- Missing plan context blocks readiness.
- Drift blocks readiness.
- No CTAF/UNICOM, standby assist, direct CTAF, COM writer, route-sector, transceiver-resolver, HNL, display, dedupe, completion, phase reuse, overlay cap, or `+N more ATC` behavior changed.
