# XVatsim Next Session Handoff

Last updated from the Step 63 closeout session.

This file is intentionally self-contained. Assume the next Codex session has no chat history. Start here, then scan the repo before making changes.

## First Actions Next Session

1. Change to the repo:

   ```powershell
   Set-Location 'C:\Users\DARRON\OneDrive\Documents\XVatsim'
   ```

2. Read this file completely.

3. Read the latest report:

   ```text
   outputs/source_owned_stable_key_shadow_gate_report.md
   ```

4. Scan the working tree:

   ```powershell
   git status --short
   Get-ChildItem outputs | Sort-Object LastWriteTime -Descending | Select-Object -First 30 Name,LastWriteTime,Length
   ```

5. Do not assume the worktree is clean. It is not. Many modified and untracked files are intentional artifacts from the brain ownership recovery work.

6. If asked to continue with the next step, wait for the user's exact Step 64 scope and hard limits, then follow the established pattern: narrow implementation, focused scenarios, build, full saved regression, report.

## Current Repo Status

The repo is mid-recovery and intentionally dirty. Do not reset, revert, prune, or broad-clean unless the user explicitly asks.

Known last green baseline:

- Build passed.
- Focused Step 63 scenarios passed: 20.
- Full saved regression passed: 408 scenarios.

Build command that passed:

```powershell
& 'C:\Users\DARRON\Documents\bin\cmake.exe' --build build --config RelWithDebInfo
```

Full regression command that passed:

```powershell
$scenarios = @(Get-ChildItem '.\tools\regression_harness\scenarios\*.scn' | Sort-Object Name)
foreach ($scenario in $scenarios) {
  & '.\build\tools\XVatsimRegressionHarness.exe' $scenario.FullName | Out-Null
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
```

The CMake executable the user provided is:

```text
C:\Users\DARRON\Documents\bin\cmake.exe
```

## Core Architecture Rule

Modules report evidence.

The brain owns decisions.

Do not let workers, source modules, plugin shell code, transceiver_resolver, route_sector, or compatibility paths silently decide display, relevance, standby eligibility, write permission, or authority. If a decision is made, it belongs in the brain and must be ledgered.

The product failure model still matters:

- False positives are bad but recoverable.
- False negatives are worse.
- Missing evidence should usually produce fail-soft diagnostics, not silent hiding.
- Hard blocks should be rare and explicit.

## Completed Recovery Arc Through Step 63

### Pre-Step 37 Foundation

The repo had already moved toward brain-owned evidence and bounded scoring:

- `transceiver_resolver` evidence paths were migrated toward brain-owned authority.
- `route_sector` authority relevance was moved toward evidence ledger plus brain-owned projection.
- Brain display intent gained bounded scoring, confidence, hard-block separation, and HNL protection.
- The important rule remains: fallback geometry must not override high-confidence relation facts.

### Steps 37-42: Standby Assist and Direct CTAF Gate

Completed:

- Step 37: standby assist decision ledger.
- Step 38: CTAF/UNICOM advisory preview integration, diagnostics only.
- Step 39: direct CTAF dry-run live-readiness diagnostics.
- Step 40: direct CTAF live standby wiring behind an explicit product gate.
- Step 41: COM1 standby writer result ledger.
- Step 42: direct CTAF gate product polish and default-off proof.

Current standby behavior:

- Existing controller standby behavior remains the primary/default behavior.
- Direct CTAF can become live standby only when both standby assist and `directCtafStandbyAssistEnabled` are enabled, all safety gates pass, and no controller target is displaced.
- The direct CTAF gate defaults OFF.
- UNICOM fallback remains excluded from live standby assist.
- Pending/failed/empty CTAF never becomes live-write eligible.
- COM writer behavior was not changed.

Key docs/reports:

- `outputs/standby_assist_decision_ledger_report.md`
- `outputs/standby_assist_ctaf_unicom_preview_report.md`
- `outputs/standby_assist_direct_ctaf_dry_run_report.md`
- `outputs/standby_assist_direct_ctaf_live_gate_report.md`
- `outputs/standby_assist_writer_result_ledger_report.md`
- `outputs/standby_assist_direct_ctaf_gate_product_polish_report.md`
- `docs/STANDBY_ASSIST_DIRECT_CTAF_GATE.md`

### Steps 43-54: CTAF/UNICOM Completion Bypass Retirement and Alias Cleanup

Completed:

- Step 43: bypass retirement-readiness ledger.
- Step 44: blocker cleanup and policy classification.
- Step 45: retired CTAF/UNICOM `StationRequiresCompletion` bypass as live authority.
- Step 46: quarantined stale post-retirement diagnostics.
- Step 47: missing source/advisory evidence hardening audit.
- Step 48: legacy bypass alias audit.
- Step 49: replacement-field migration.
- Step 50: report-only alias removal.
- Step 51: public/unknown alias migration proof.
- Step 52: external-risk alias deprecation/removal window.
- Step 53: public/header alias closure audit.
- Step 54: formal deprecation of retained public/header aliases.

Current CTAF/UNICOM behavior:

- Completion bypass is no longer live authority.
- Live CTAF/UNICOM rows come from brain-owned advisory projection.
- Compatibility projection evidence is diagnostic-only.
- Valid direct CTAF display remains brain-owned.
- UNICOM fallback display remains brain-owned where existing behavior displayed it.
- Pending/failed/empty CTAF remain non-displayable/non-writeable by policy.
- Missing evidence emits warning-only diagnostics and does not restore live compatibility fallback.
- Authority invariants remain centered on:
  - `noLiveBypassAuthority`
  - `compatibilityRowsDiagnosticOnly`
  - `liveRowsBrainAdvisoryOwned`
  - `standbyRowsAdvisoryOwned`
  - `legacyBypassFieldsQuarantined`

Compatibility window still open:

- `liveRowEmitted` remains present but deprecated; prefer `legacyDiagnosticLiveRowEmitted`.
- `completionBypassCompatibilityOnly` remains present but deprecated; prefer `diagnosticCompatibilityProjectionOnly`.

Key docs/reports:

- `outputs/ctaf_unicom_completion_bypass_retirement_audit_report.md`
- `outputs/ctaf_unicom_bypass_retirement_blocker_cleanup_report.md`
- `outputs/ctaf_unicom_completion_bypass_retirement_report.md`
- `outputs/ctaf_unicom_post_retirement_diagnostic_cleanup_report.md`
- `outputs/ctaf_unicom_missing_evidence_hardening_audit_report.md`
- `outputs/ctaf_unicom_legacy_bypass_alias_audit_report.md`
- `outputs/ctaf_unicom_legacy_alias_replacement_migration_report.md`
- `outputs/ctaf_unicom_report_only_alias_removal_report.md`
- `outputs/ctaf_unicom_public_unknown_alias_migration_proof_report.md`
- `outputs/ctaf_unicom_external_alias_deprecation_report.md`
- `outputs/ctaf_unicom_public_header_alias_risk_closure_report.md`
- `outputs/ctaf_unicom_public_header_alias_deprecation_report.md`
- `docs/CTAF_UNICOM_PUBLIC_HEADER_ALIAS_DEPRECATION.md`

### Steps 55-63: BrainDisplayIntent Cap, Reuse, Plan, and Stable-Key Recovery

Completed:

- Step 55: overlay cap / `+N more ATC` decision ledger.
- Step 56: display/cap source evidence linkage audit.
- Step 57: phase publisher reuse ledger.
- Step 58: live product plan-key linkage diagnostics.
- Step 59: stable completion key audit ledger.
- Step 60: upstream stable completion key source audit.
- Step 61: source-owned stable keys for fallback polygon/geometry inference, parity only.
- Step 62: dry-run consumer parity ledger for source-owned fallback polygon stable keys.
- Step 63: default-off source-owned fallback stable-key shadow behavior gate.

Current BrainDisplayIntent state:

- Final display behavior unchanged.
- Row ordering unchanged.
- Dedupe behavior unchanged.
- Completion behavior unchanged.
- Overlay cap and `+N more ATC` unchanged.
- Phase publish/reuse behavior unchanged.
- Capped rows, hidden rows, source linkage, reuse decisions, plan context, and stable key status are now ledgered.

Current stable-key state:

- Fallback polygon/geometry rows have source-owned stable key diagnostics.
- The generated fallback key remains present for parity.
- Source-owned key behavior consumption remains disabled.
- Step 62 dry-run parity compares current behavior key to source-owned key at dedupe and phase reuse points.
- Step 63 adds a default-off shadow gate:
  - `sourceOwnedFallbackStableKeyShadowEnabled`
  - `sourceOwnedFallbackStableKeyShadowGateSource`
- Gate OFF: shadow recomputation is skipped, behavior unchanged.
- Gate ON: shadow comparison runs only, behavior unchanged.
- Missing plan context blocks future live opt-in readiness.

Step 63 report:

- `outputs/source_owned_stable_key_shadow_gate_report.md`

Step 63 focused scenarios added/updated:

- `brain_display_stable_key_shadow_gate_off_default.scn`
- `brain_display_stable_key_shadow_gate_on_context.scn`
- `brain_display_stable_key_shadow_missing_plan_context.scn`
- `brain_display_stable_key_consumer_dry_run_duplicate_fallback_polygon.scn`
- `brain_display_overlay_cap_one_hidden_row.scn`
- `brain_display_overlay_cap_multiple_hidden_rows.scn`
- `phase_publisher_source_owned_stable_key_reuse_current_incomplete.scn`
- `phase_publisher_source_owned_stable_key_fresh_displaces_previous.scn`
- `phase_publisher_source_owned_stable_key_frequency_mismatch.scn`
- `phase_publisher_source_owned_stable_key_role_mismatch.scn`

## Step 63 Technical Notes

Primary files changed for Step 63:

- `brain/include/XVatsim/brain/BrainDisplayIntent.h`
- `brain/src/BrainDisplayIntent.cpp`
- `brain/include/XVatsim/brain/PhaseSnapshotPublisher.h`
- `brain/src/PhaseSnapshotPublisher.cpp`
- `tools/regression_harness/src/main.cpp`

New/important display shadow fields:

- `sourceOwnedFallbackShadowGateEnabled`
- `sourceOwnedFallbackShadowGateSource`
- `shadowRecomputeAttempted`
- `shadowRecomputeSkippedReason`
- `shadowBehaviorConsumerEnabled`
- `shadowFinalBoardHashCurrent`
- `shadowFinalBoardHashSourceOwned`
- `shadowFinalBoardHashMatches`
- `shadowRowOrderingMatches`
- `shadowDedupeGroupsMatch`
- `shadowDuplicateSuppressionMatches`
- `shadowCompletionIdentityMatches`
- `shadowPhaseReuseMatches`
- `shadowOverlayCapMatches`
- `shadowMoreAtcMatches`
- `shadowMissingPlanContextBlocked`
- `shadowDriftDetected`
- `shadowDriftReason`
- `shadowSafeForFutureLiveOptIn`

New/important summary counters:

- `shadowDecisionCount`
- `shadowGateEnabledCount`
- `shadowRecomputeAttemptedCount`
- `shadowRecomputeSkippedCount`
- `shadowHashMismatchCount`
- `shadowRowOrderingMismatchCount`
- `shadowDedupeMismatchCount`
- `shadowDuplicateSuppressionMismatchCount`
- `shadowCompletionIdentityMismatchCount`
- `shadowPhaseReuseMismatchCount`
- `shadowOverlayCapMismatchCount`
- `shadowMoreAtcMismatchCount`
- `shadowMissingPlanBlockedCount`
- `shadowDriftDetectedCount`
- `shadowSafeForFutureLiveOptInCount`
- `shadowBehaviorConsumerEnabledCount`
- `behaviorChanged`

Harness keys:

- `stable_key.source_owned_fallback_shadow`
- `source_owned_fallback_stable_key_shadow_enabled`
- `stable_key.source_owned_fallback_shadow_source`
- `source_owned_fallback_stable_key_shadow_gate_source`

Harness expectations:

- `expect.brain_display_stable_key_shadow_summary`
- `expect.brain_display_stable_key_shadow_decisions_contains`
- `expect.phase_publisher_stable_key_shadow_summary`

## Known Gaps / Likely Next Target

Do not start this without the user's next explicit step text, but the natural next stable-key migration target is:

- keep the Step 63 shadow gate default OFF;
- add a separate, still default-off live-consumption readiness/product gate proposal or ledger;
- refuse live consumption unless:
  - shadow parity is clean,
  - plan context is present,
  - no dedupe/duplicate/completion/reuse/order/cap/more-ATC drift exists,
  - `shadowBehaviorConsumerEnabled` remains false in default mode,
  - CTAF/UNICOM and standby guardrails remain unchanged.

The source-owned fallback stable key is not ready to become live behavior by default. Missing plan context remains an explicit blocker.

## Guardrails for Future Steps

Preserve unless the user explicitly scopes otherwise:

- Do not restore CTAF/UNICOM completion bypass live authority.
- Do not add live compatibility fallback.
- Do not enable UNICOM live standby eligibility.
- Do not allow pending/failed/empty CTAF to display or write.
- Do not change COM writer behavior.
- Do not let direct CTAF displace a controller standby target.
- Do not change row ordering, display behavior, cap policy, `+N more ATC`, dedupe, completion, or phase reuse unless a later user step explicitly authorizes it.
- Do not modify `transceiver_resolver`, `route_sector`, or HNL unless explicitly scoped.
- Do not broad-clean the worktree.

## Report Index From This Session

Read newest first when orienting:

- `outputs/source_owned_stable_key_shadow_gate_report.md`
- `outputs/source_owned_stable_key_dry_run_consumer_parity_report.md`
- `outputs/fallback_polygon_source_owned_stable_key_report.md`
- `outputs/upstream_stable_key_source_audit_report.md`
- `outputs/brain_display_stable_key_audit_report.md`
- `outputs/phase_publisher_plan_key_linkage_report.md`
- `outputs/brain_display_phase_reuse_ledger_report.md`
- `outputs/brain_display_source_linkage_report.md`
- `outputs/brain_display_overlay_cap_ledger_report.md`
- `outputs/ctaf_unicom_public_header_alias_deprecation_report.md`
- `outputs/ctaf_unicom_public_header_alias_risk_closure_report.md`
- `outputs/ctaf_unicom_external_alias_deprecation_report.md`
- `outputs/ctaf_unicom_public_unknown_alias_migration_proof_report.md`
- `outputs/ctaf_unicom_report_only_alias_removal_report.md`
- `outputs/ctaf_unicom_legacy_alias_replacement_migration_report.md`
- `outputs/ctaf_unicom_legacy_bypass_alias_audit_report.md`
- `outputs/ctaf_unicom_missing_evidence_hardening_audit_report.md`
- `outputs/ctaf_unicom_post_retirement_diagnostic_cleanup_report.md`
- `outputs/ctaf_unicom_completion_bypass_retirement_report.md`
- `outputs/ctaf_unicom_bypass_retirement_blocker_cleanup_report.md`
- `outputs/ctaf_unicom_completion_bypass_retirement_audit_report.md`
- `outputs/standby_assist_direct_ctaf_gate_product_polish_report.md`
- `outputs/standby_assist_writer_result_ledger_report.md`
- `outputs/standby_assist_direct_ctaf_live_gate_report.md`
- `outputs/standby_assist_direct_ctaf_dry_run_report.md`
- `outputs/standby_assist_ctaf_unicom_preview_report.md`
- `outputs/standby_assist_decision_ledger_report.md`

## Practical Next-Session Reminder

The safest rhythm for the next step:

1. Read this file.
2. Read the latest report.
3. Run `git status --short`.
4. Inspect touched files before editing.
5. Implement only the user-scoped step.
6. Add focused scenarios.
7. Build with the provided CMake exe.
8. Run focused scenarios.
9. Run full saved regression.
10. Write the required report under `outputs/`.

Future-me: this repo has made real progress, but it is still carrying a lot of recovery scaffolding. Move in small, ledgered steps.
