# Source-Owned Fallback Stable-Key Product Wiring Readiness Report

## 1. Files changed

Step 67 changed only:
- `outputs/source_owned_stable_key_product_wiring_readiness_report.md`

This is report-only. No source, plugin, settings-store, scenario, or harness file was edited for Step 67.

## 2. Runtime behavior confirmation

No runtime behavior changed.

No default mode changed.

No display, row ordering, dedupe, duplicate suppression, completion identity, phase reuse, overlay cap, `+N more ATC`, CTAF/UNICOM, standby assist, direct CTAF, COM writer, `transceiver_resolver`, `route_sector`, HNL, generated fallback key, or compatibility alias behavior changed.

No plugin or settings-store wiring was added.

## 3. Proposed setting

Proposed setting name:
`sourceOwnedFallbackStableKeyLiveConsumptionEnabled`

Proposed gate source field:
`sourceOwnedFallbackStableKeyLiveConsumptionGateSource`

Proposed default value:
`false`

Proposed source behavior:
- Missing/absent setting resolves to `false`, source `default`.
- Settings-store value, if ever wired, may set source to `settings-store`.
- Harness/request proof may continue to use source `harness`.
- Unknown/invalid source should normalize to `unknown` and should not weaken brain-owned eligibility checks.

This setting must not be made public/user-facing in the proposed wiring step. It is an internal gated opt-in only.

## 4. Ownership boundary

Product wiring, if ever implemented, may only pass a passive boolean/source into the brain request path: the passive boolean and passive source string are inputs only, not eligibility decisions.

The plugin/settings-store must not decide eligibility, readiness, fallback identity, display, dedupe, duplicate suppression, completion identity, phase reuse, overlay cap, standby, direct CTAF, CTAF/UNICOM, or COM behavior.

The plugin/settings-store must not decide whether a row is safe, whether plan context is sufficient, whether shadow parity is clean, whether drift exists, whether a source-owned key is migration-ready, or whether source-owned consumption is allowed.

The brain owns all readiness checks and decisions:
- Step 63 shadow safety: `shadowSafeForFutureLiveOptIn`.
- Step 64 readiness: display `readyForFutureLiveConsumption`; phase `liveConsumptionReadyForFutureOptIn`.
- Step 66 live-consumption decision and block reason.

The generated fallback key remains the default behavior unless the brain-owned Step 66 gate and all brain-owned readiness checks pass.

## 5. Proposed future files

Future wiring should be scoped to these files only, pending a separate Step 68 Contract Gate:
- `modules/settings_store/include/XVatsim/modules/settings_store/SettingsStore.h`
- `modules/settings_store/src/SettingsStore.cpp`
- `plugin/src/XVatsimPlugin.cpp`
- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
- `brain/src/BrainOwnedRuntime.cpp`
- `tools/regression_harness/src/main.cpp`
- New focused `.scn` files only if needed.
- Step 68 report under `outputs/`

Do not touch `transceiver_resolver`, `route_sector`, HNL-specific logic, CTAF/UNICOM authority, standby/direct CTAF/COM writer logic, generated fallback key derivation, or compatibility aliases.

## 6. Proposed request / DTO path

Current observed path:
- `plugin/src/XVatsimPlugin.cpp` loads settings through `gSettingsStore.Load()` into `gPluginSettings`.
- Plugin builds `BrainOwnedPublisherFactInput` in the brain-owned publisher path.
- `BuildBrainOwnedPublisherInputFromFacts(...)` converts facts to `BrainOwnedPublisherInput`.
- `RunBrainOwnedPublisher(...)` builds `BrainDisplayIntentInput`.
- `RunBrainOwnedPublisher(...)` also builds `PhaseSnapshotPublishRequest`.
- `BrainDisplayIntentInput` and `PhaseSnapshotPublishRequest` already have Step 66 fields:
  - `sourceOwnedFallbackStableKeyLiveConsumptionEnabled`
  - `sourceOwnedFallbackStableKeyLiveConsumptionGateSource`

Proposed future path:
1. Add internal fields to `PluginSettings` with default OFF/source default.
2. Parse/save the fields in `SettingsStore`, still hidden/internal.
3. In plugin, copy only the passive setting value/source from `gPluginSettings` into `BrainOwnedPublisherFactInput`.
4. Copy those passive fields into `BrainOwnedPublisherInput`.
5. In `RunBrainOwnedPublisher(...)`, pass those fields into both:
   - `BrainDisplayIntentInput`
   - `PhaseSnapshotPublishRequest`
6. Brain-owned Step 63/64/66 logic remains the only authority that can allow or block source-owned consumption.

## 7. Required invariants

Future wiring must preserve:
- Default OFF.
- Generated fallback retained by default.
- Step 66 live gate required.
- Step 63 shadow proof required.
- Step 64 readiness required.
- Missing plan context blocks.
- Any drift blocks.
- Any parity mismatch blocks.
- Missing source-owned key blocks.
- Migration readiness false blocks.
- Plugin/settings-store pass only passive inputs.
- Brain owns final eligibility and consumption decision.
- No default-on behavior.
- No public/user-facing setting exposure.

## 8. Product risk matrix

| Risk | If ignored | Required mitigation |
| --- | --- | --- |
| Default accidentally ON | Source-owned keys could affect live behavior without proof | Settings default must be false; absent setting must be false |
| Plugin decides eligibility | Ownership boundary regresses from brain-owned safety | Plugin may pass only boolean/source; brain decides all readiness |
| Missing plan context allowed | Fallback rows could consume unstable identities | Brain must keep `missing-plan-context` blocking |
| Shadow parity bypassed | Display/order/dedupe/reuse/cap drift could be hidden | Step 63 shadow gate and recompute must be required |
| Step 64 readiness bypassed | Clean parity could be inferred without proposal/readiness proof | Require existing Step 64 readiness fields |
| Drift ignored | Completion identity or phase reuse may change | Any drift or parity mismatch blocks consumption |
| Product setting exposed too early | Users may enable an internal migration gate without support | Keep internal only until explicit product decision |
| CTAF/UNICOM authority regression | Retired bypass or UNICOM standby behavior could reappear | Rerun CTAF/UNICOM authority guardrails |
| Standby/COM regression | Frequency write behavior could change indirectly | Rerun standby/direct CTAF/COM writer guardrails |
| Default-on requested later | Stable-key migration would become broad behavior change | Require separate default-on approval and full regression |

## 9. Required focused scenarios for future wiring

Any future implementation must add or rerun focused proof for:
- Settings absent: default OFF, generated fallback retained.
- Settings present false: default OFF, generated fallback retained.
- Settings present true: passive gate reaches brain request path with source `settings-store`.
- Gate true plus clean Step 63/64 readiness: source-owned consumed only for targeted fallback polygon rows.
- Gate true plus shadow gate off: blocked.
- Gate true plus missing plan context: blocked `missing-plan-context`.
- Gate true plus drift: blocked.
- Gate true plus hash/order/dedupe/duplicate/completion/phase/cap/more-ATC parity mismatch: blocked.
- Duplicate fallback polygon rows: parity clean required.
- Completion identity continuity.
- Phase reuse current-incomplete.
- Phase reuse fresh-displaces-previous.
- Overlay cap one-hidden and multiple-hidden rows.
- Plugin/settings-store source propagation remains passive; no plugin eligibility decision exists.

## 10. Guardrails to rerun

Rerun:
- All Step 66 focused live-consumption scenarios.
- Step 63 shadow gate scenarios.
- Step 64 readiness scenarios.
- CTAF/UNICOM authority guardrail.
- Standby controller-target unchanged guardrail.
- Direct CTAF gate behavior guardrail.
- COM writer guardrail.
- HNL/protected live saved scenarios in full regression.

## 11. Full regression requirement

If Step 68 changes code, full saved regression is required.

For Step 67, full saved regression was intentionally not rerun because this step is report-only and no code changed. Build and focused guardrails were rerun.

## 12. Verification performed

Build command:
`& 'C:\Users\DARRON\Documents\bin\cmake.exe' --build build --config RelWithDebInfo`

Build result:
Passed.

Focused command:
`build/tools/XVatsimRegressionHarness.exe <scenario-file>` across the Step 66 focused and guardrail set.

Focused result:
23 scenarios passed, 0 failed.

Focused scenarios:
- `brain_display_live_consumption_gate_off_default.scn`
- `brain_display_live_consumption_gate_on_clean.scn`
- `brain_display_live_consumption_shadow_gate_off_blocked.scn`
- `brain_display_live_consumption_missing_plan_blocked.scn`
- `phase_publisher_live_consumption_drift_blocked.scn`
- `brain_display_live_consumption_duplicate_fallback_polygon.scn`
- `brain_display_live_consumption_completion_identity_parity.scn`
- `phase_publisher_live_consumption_reuse_current_incomplete.scn`
- `phase_publisher_live_consumption_fresh_displaces_previous.scn`
- `brain_display_live_consumption_overlay_cap_one_hidden.scn`
- `brain_display_live_consumption_overlay_cap_multiple_hidden.scn`
- `brain_display_stable_key_shadow_gate_off_default.scn`
- `brain_display_stable_key_shadow_gate_on_context.scn`
- `brain_display_stable_key_shadow_missing_plan_context.scn`
- `brain_display_live_consumption_readiness_gate_off_default.scn`
- `brain_display_live_consumption_readiness_gate_on_clean.scn`
- `brain_display_live_consumption_readiness_missing_plan_blocked.scn`
- `phase_publisher_live_consumption_readiness_gate_on_clean.scn`
- `phase_publisher_live_consumption_readiness_drift_blocked.scn`
- `ctaf_unicom_bypass_retirement_authority_guardrail.scn`
- `standby_assist_decision_ledger_controller_target_unchanged.scn`
- `standby_assist_direct_ctaf_live_gate_on_selects_direct_ctaf.scn`
- `standby_assist_writer_result_controller_success.scn`

## 13. Recommended Step 68

Recommended Step 68:
Implement a tiny internal passive wiring step behind a new Contract Gate:
- Add internal settings-store fields default OFF/source default.
- Pass only passive boolean/source through plugin to `BrainOwnedPublisherFactInput`, `BrainOwnedPublisherInput`, `BrainDisplayIntentInput`, and `PhaseSnapshotPublishRequest`.
- Do not expose a public/user-facing setting.
- Do not let plugin/settings-store decide eligibility.
- Require focused settings propagation scenarios, Step 66 guardrails, and full saved regression.
