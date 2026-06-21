# Step 60 - Upstream Stable Completion Key Source Audit

## 1. Files changed

- `brain/include/XVatsim/brain/BrainDisplayIntent.h`
- `brain/src/BrainDisplayIntent.cpp`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/scenarios/brain_display_upstream_stable_key_source_audit_inventory.scn`

## 2. Producer/source classes inventoried

The new audit inventories eight upstream source classes that can feed display, completion, evidence, or diagnostic rows:

- `controller-relevance-completion`
- `transceiver-resolver`
- `route-sector-authority-relevance`
- `ctaf-unicom-advisory`
- `duplicated-atis-frequency-proof`
- `fallback-polygon-geometry-inference`
- `synthetic-fixture-row`
- `legacy-diagnostic-row`

## 3. Stable key source classification table

| Source class | Producer | Current key source | Priority | Key risk |
|---|---|---|---|---|
| `controller-relevance-completion` | `BrainControllerRelevanceWorker` | `evidence-id` via `sourceEvidenceId` | low | low |
| `transceiver-resolver` | `TransceiverResolutionSnapshot` | missing | medium | dedupe and reuse continuity |
| `route-sector-authority-relevance` | `RouteSectorResolver/AuthorityRelevance` | missing | medium | reuse continuity |
| `ctaf-unicom-advisory` | `BrainOwnedRuntime CTAF/UNICOM advisory projection` | `evidence-id` via `sourceEvidenceId` | none | low |
| `duplicated-atis-frequency-proof` | `Transceiver/route authority proof diagnostics` | missing | low | duplicate proof diagnostics |
| `fallback-polygon-geometry-inference` | `BrainDisplayIntent fallback relation inference` | generated fallback | high | dedupe and reuse continuity |
| `synthetic-fixture-row` | `Regression harness display fixture` | synthetic fixture | none | diagnostic-only |
| `legacy-diagnostic-row` | `Legacy compatibility diagnostic projection` | legacy | none | compatibility-window only |

Audit summary emitted by the focused scenario:

`decisions=8,sourceOwned=0,evidenceId=2,decisionId=0,fallback=1,synthetic=1,legacy=1,missing=3,unknown=0,high=1,medium=2,low=2,dedupeRisk=3,reuseRisk=4,behaviorChangeRequired=0,brainOwned=1`

## 4. Producers already source/evidence-owned

- Controller relevance/completion rows already expose source evidence IDs shaped as `radio-reachable:<candidate-stable-key>`.
- CTAF/UNICOM advisory rows already expose source evidence IDs shaped as `ctaf-unicom:<endpoint>:<airport-icao>`.

Both are classified as `evidence-id` rather than explicit `source-owned` because they do not yet emit a dedicated `stableCompletionKey` field from the producer.

## 5. Producers relying on fallback/generated keys

`fallback-polygon-geometry-inference` is the high-priority fallback source. These rows can be displayed and completed, but stable identity is currently generated downstream from row facts rather than owned by an upstream producer.

Recommended owner: upstream controller or route evidence.

Recommended shape: `source-owned:<producer>|<callsign>|<role>|<frequency>|<plan-context>`.

## 6. Synthetic/legacy/diagnostic-only producers

- Synthetic fixture rows are classified as `synthetic-fixture` and remain harness-only.
- Legacy diagnostic rows are classified as `legacy` and remain compatibility-window diagnostics only.
- Duplicated ATIS/frequency proof rows are diagnostic evidence today and are not display authority.

## 7. Dedupe risk assessment

The audit reports three dedupe-risk classes:

- `transceiver-resolver`
- `duplicated-atis-frequency-proof`
- `fallback-polygon-geometry-inference`

These are warning/planning classifications only. Current dedupe behavior is unchanged.

## 8. Phase reuse continuity risk assessment

The audit reports four reuse-continuity-risk classes:

- `transceiver-resolver`
- `route-sector-authority-relevance`
- `fallback-polygon-geometry-inference`
- `legacy-diagnostic-row`

No phase reuse policy consumes this audit yet; Step 59 phase reuse behavior remains unchanged.

## 9. Recommended stable key owner/shape per producer

- Controller relevance: `BrainControllerRelevanceWorker`, `radio-reachable:<candidate-stable-key>`.
- Transceiver resolver: `TransceiverResolver`, `transceiver:<callsign>|<facility>|<frequency>|<source-generation>`.
- Route sector authority: `RouteSectorResolver-authority-evidence`, `route-sector:<plan-key>|<sector-id>|<callsign>|<frequency>`.
- CTAF/UNICOM advisory: `BrainOwnedRuntime CTAF/UNICOM advisory decision`, `ctaf-unicom:<endpoint>:<airport-icao>`.
- Duplicated proof diagnostics: `proof-evidence-producer`, `proof:<domain>|<callsign>|<frequency>|<reason>`.
- Fallback polygon inference: upstream controller or route evidence, `source-owned:<producer>|<callsign>|<role>|<frequency>|<plan-context>`.
- Synthetic fixtures: harness fixture ownership, `fixture:<scenario>|<callsign>|<frequency>`.
- Legacy diagnostics: compatibility producer ownership, `legacy:<compatibility-domain>|<callsign>|<frequency>`.

## 10. Migration priorities

- High: fallback polygon/geometry inference.
- Medium: transceiver resolver and route-sector authority relevance.
- Low: controller relevance explicit stable key field, duplicated proof diagnostics.
- None: CTAF/UNICOM advisory, synthetic fixtures, legacy diagnostics.

## 11. Behavior changed

No runtime behavior changed. This step adds diagnostics and harness assertions only.

## 12. Behavior unchanged

Unchanged:

- Final display behavior
- Row ordering
- Dedupe behavior
- Completion behavior
- Phase publish/reuse selection behavior
- Overlay cap and `+N more ATC`
- CTAF/UNICOM behavior
- Standby assist behavior
- Direct CTAF gate behavior
- COM writer behavior
- Transceiver resolver behavior
- Route sector behavior
- CTAF/UNICOM live bypass retired state

## 13. Focused scenario/check summaries

Command: ran 17 focused Step 60 scenarios with `build\tools\XVatsimRegressionHarness.exe`.

Result: all focused scenarios passed.

Focused checks covered:

- Upstream source-class inventory and classification.
- Existing Step 59 stable-key focused scenarios.
- Duplicate-key risk reporting without dedupe changes.
- Reuse-continuity warnings without phase reuse changes.
- CTAF/UNICOM authority guardrail.
- Existing controller standby guardrail.

## 14. Build command/result

Command:

```powershell
& 'C:\Users\DARRON\Documents\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: passed. `XVatsimBrain`, `XVatsimPlugin`, `XVatsimPreflightBuilder`, and `XVatsimRegressionHarness` built successfully.

## 15. Focused check/scenario command/result

Command:

```powershell
$scenarios = @(
  'brain_display_upstream_stable_key_source_audit_inventory.scn',
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

Result: passed; all 17 focused scenarios passed.

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

Result: `Passed 398 scenarios`.

## 17. Recommended next stable-key migration step

Start with a no-behavior-change migration that adds explicit source-owned `stableCompletionKey` fields to the high-priority fallback controller/route evidence path. Keep BrainDisplayIntent deriving warnings until parity proves the source-owned key matches the generated fallback shape, then migrate downstream assertions to the source-owned field before any behavior uses it.
