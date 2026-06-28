# XVatsim Next Session Handoff

Last updated after the live-tested CTAF/UNICOM standby assist hotfix on 2026-06-20.

This file is intentionally self-contained. Assume the next Codex session has no chat history. Start here, then scan the repo before making changes.

## First Actions Next Session

1. Change to the repo:

   ```powershell
   Set-Location 'C:\Users\DARRON\OneDrive\Documents\XVatsim'
   ```

2. Read this file completely.

3. Check current state:

   ```powershell
   git status --short
   git log -5 --oneline
   Get-ChildItem outputs | Sort-Object LastWriteTime -Descending | Select-Object -First 30 Name,LastWriteTime,Length
   ```

4. If the user asks to package or close out, first confirm whether live online testing is complete.

5. Do not rely on prior chat. The relevant state is recorded below.

## Current Git State

Known latest commits:

- `3cddba5 fix: allow UNICOM fallback standby assist`
- `88c1b12 chore: close brain ownership recovery arc`

The CTAF/UNICOM standby assist hotfix was live-tested by the user and committed. The repo was clean after that commit.

If this file itself is dirty when the next session starts, that is expected: it was written as the durable no-chat startup handoff after the hotfix.

## Current Product State

The brain ownership recovery arc is closed.

Closed fronts:

- CTAF/UNICOM completion bypass retirement.
- Standby assist, direct CTAF gate, and COM writer result ledger.
- BrainDisplayIntent overlay/source/reuse/stable-key ledgers.
- Fallback polygon/geometry source-owned stable-key migration front through internal passive wiring and closeout.
- `route_sector` authority ownership closure.
- `transceiver_resolver` authority ownership closure.

The next user intent is release completion, not more recovery-report churn. The user confirmed live testing looked good and requested Git update plus Freeware Package V1.2.1 production.

## Latest Verified Build and Regression State

Before the final recovery closeout:

- Build passed.
- Focused guardrail bundle passed.
- Full saved regression passed: 431 scenarios.

After the CTAF/UNICOM standby assist hotfix:

- Build passed.
- Focused standby assist hotfix/guardrail bundle passed: 10 scenarios.
- Full saved regression passed: 431 scenarios.
- Updated `XVatsim.xpl` was copied to:

  ```text
  C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl
  ```

- Copied binary SHA256:

  ```text
  DDB40F25D3F1DA91A61F0BA1008BA8A6A816C70D338BB06A7D26839AF535F224
  ```

Build command used:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

Full regression command used:

```powershell
$h = '.\build\tools\XVatsimRegressionHarness.exe'
$scenarios = @(Get-ChildItem '.\tools\regression_harness\scenarios\*.scn' | Sort-Object Name)
foreach ($scenario in $scenarios) {
  & $h $scenario.FullName *> $null
  if ($LASTEXITCODE -ne 0) {
    Write-Host "FAILED $($scenario.Name)"
    exit $LASTEXITCODE
  }
}
Write-Host "Passed $($scenarios.Count) scenarios"
```

## Core Architecture Rule

Modules report evidence.

The brain owns decisions.

Do not let workers, source modules, plugin shell code, `transceiver_resolver`, `route_sector`, or compatibility paths silently decide display, relevance, standby eligibility, write permission, fallback identity, or authority. If a decision is made, it belongs in the brain and must be ledgered.

Product failure model:

- False positives are bad but recoverable.
- False negatives are worse.
- Missing evidence should usually produce fail-soft diagnostics, not silent hiding.
- Hard blocks should be rare and explicit.

## CTAF/UNICOM and Standby Assist State

CTAF/UNICOM completion bypass live authority remains retired.

Live CTAF/UNICOM rows come from brain-owned advisory projection. Compatibility projection evidence remains diagnostic-only.

The old statement "UNICOM fallback is excluded from live standby assist" is no longer true.

Current standby assist behavior:

- Controller standby targets still win.
- Direct CTAF can become a COM1 standby target only when standby assist and the CTAF/UNICOM advisory gate are enabled and safety gates pass.
- `NO CTAF / UNICOM` fallback can also become a COM1 standby target under the same standby assist plus CTAF/UNICOM advisory gate conditions.
- Pending CTAF lookup, failed CTAF lookup, empty frequency, guard/invalid frequencies, active frequencies, and already-in-standby cases remain blocked.
- CTAF/UNICOM advisory standby does not displace an existing controller standby target.
- COM writer behavior remains brain-owned.

The user live-tested the hotfix on route `VOI560 MMTO -> MMPR`; the original bug was that `NO CTAF / UNICOM MMTO 122.800` displayed but did not tune COM1 standby. The hotfix fixed that and was committed as:

```text
3cddba5 fix: allow UNICOM fallback standby assist
```

Primary files in the hotfix:

- `brain/src/BrainOwnedRuntime.cpp`
- `docs/STANDBY_ASSIST_DIRECT_CTAF_GATE.md`
- `tools/regression_harness/scenarios/standby_assist_ctaf_unicom_preview_unicom_product_gated.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_dry_run_unicom_excluded.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_gate_polish_unicom_excluded.scn`
- `tools/regression_harness/scenarios/standby_assist_direct_ctaf_live_gate_on_unicom_excluded.scn`
- `tools/regression_harness/scenarios/standby_assist_writer_result_unicom_no_writer.scn`

Scenario filenames still contain older `unicom_excluded` naming in some places. Do not infer current behavior from those filenames; inspect expectations and code.

## Source-Owned Fallback Stable-Key State

The fallback polygon/geometry source-owned stable-key migration subfront is closed for now.

Current defaults:

- Generated fallback stable key remains default behavior.
- Source-owned fallback live consumption remains internal and default OFF.
- Internal passive setting exists:
  - `sourceOwnedFallbackStableKeyLiveConsumptionEnabled`
  - `sourceOwnedFallbackStableKeyLiveConsumptionGateSource`
- Missing setting resolves to `false` / `default`.
- Settings-origin source can only be `settings-store`.
- Settings-origin impersonation attempts such as `harness` or explicit `default` normalize/ledger as `unknown`.
- Plugin/settings-store pass passive values only.
- Brain-owned Step 63/64/66 readiness checks remain the only authority.
- Public exposure is blocked.
- Default-on is blocked.
- Generated fallback remains the rollback path.

Important reports:

- `outputs/source_owned_fallback_stable_key_migration_closeout_report.md`
- `outputs/source_owned_stable_key_hidden_internal_release_risk_checklist.md`
- `outputs/source_owned_stable_key_public_exposure_readiness_audit_report.md`
- `outputs/source_owned_stable_key_internal_product_wiring_report.md`
- `outputs/source_owned_stable_key_product_wiring_readiness_report.md`
- `outputs/source_owned_stable_key_gated_live_opt_in_report.md`
- `outputs/source_owned_stable_key_live_consumption_blocker_audit_report.md`
- `outputs/source_owned_stable_key_live_consumption_readiness_report.md`

## Route-Sector and Transceiver-Resolver State

`route_sector`:

- May compute/report geometry, key, token, and evidence facts.
- May retain compatibility projections where intentionally retained.
- Must not suppress, promote, hide, display, or live-project authority outside the brain.
- Live `relevantAuthorities` remains brain-owned.
- Closure report: `outputs/route_sector_authority_ownership_closure_audit_report.md`

`transceiver_resolver`:

- May compute, normalize, rank, filter, score, and report evidence/candidates.
- May retain compatibility candidate projections.
- Must not suppress, promote, hide, display, live-project, mark standby-eligible, or write-authorize outside the brain-owned path.
- Live relevance/display/standby/write-authority decisions remain brain-owned.
- Closure report: `outputs/transceiver_resolver_authority_ownership_closure_audit_report.md`

## Final Recovery Closeout

Final closeout report:

```text
outputs/brain_ownership_recovery_final_closeout_report.md
```

Final recovery arc status from Step 74:

- Closed.
- Recommendation was commit/tag/stop or open a new named Contract Gate for a new front.

The user has not yet asked to create the final package after the live tests. Do not package preemptively.

## Live Testing and Performance Notes

The user is doing further online testing before final cleanup/package work.

Last tested route:

```text
VOI560 MMTO -> MMPR
```

Live issue found and fixed:

- `NO CTAF / UNICOM MMTO 122.800` displayed but did not tune COM1 standby.
- Fixed by allowing resolved UNICOM fallback advisory candidates through standby assist under the same standby assist plus CTAF/UNICOM advisory gate as direct CTAF.

Performance investigation notes:

- Startup/context-establishment spikes are expected and were observed:
  - route rebuild around 1.1s
  - authority proof build around 450-480ms
  - total startup/context spikes around 1.5-1.6s
- The user considers initial VATSIM flight-plan load spikes acceptable because cockpit setup is still underway.
- Pushback/taxi stutters occurred later, around BetterPushback/PMCO activity, not during XVatsim heavy startup work.
- Tail-window XVatsim diagnostics around pushback showed no slow-refresh markers:
  - refresh around 1.3-1.6ms
  - route work 0us
  - authority relevance around 18-22us
  - standby assist around 39-52us
- Conclusion: pushback stutter was unlikely to be XVatsim's runtime brain loop.
- X-Plane crash was not attributed to XVatsim. X-Plane log ended with a ToLiss/X-Plane flight model `vx_wrl value is nan or inf` error and there were Map Enhancement / scenery DSF load issues earlier.

Deferred official bug-fix note:

- Reduce unchanged `radio-board-candidate-diff` diagnostic log churn.
- Current logs may emit repeated no-op radio-board diff trace lines every generation even when candidate counts and hashes are unchanged.
- This is diagnostic-log hygiene, not a brain decision change.
- Likely file: `plugin/src/XVatsimPlugin.cpp`.
- Do not change runtime display, standby behavior, authority, or brain decision ownership for this.

Suggested future Contract Gate for that deferred bug fix:

1. Files intended to change:
   - `plugin/src/XVatsimPlugin.cpp`
   - focused diagnostics scenario only if existing harness supports this path without overbuilding.
2. Behavior change:
   - Runtime behavior: none.
   - Diagnostic logging only: suppress unchanged/no-op radio-board trace lines.
3. Brain authority:
   - Unchanged. Brain still owns decisions.
   - Plugin only gates diagnostic log emission.
4. Proposed suppression:
   - Do not emit `radio-board-candidate-diff` when previous/current hashes match, candidate counts match, added/removed are zero, and there are no meaningful candidate changes.
   - Still emit on real candidate add/remove/change, route hash change, source/stale change, or meaningful non-empty trace.
5. Verification:
   - Build.
   - Focused existing radio-board/standby/CTAF guardrails.
   - Full regression if code changes.

Do not do this unless the user explicitly asks. The user said to make note of it only and address it in the next official bug fix.

## Guardrails for Future Work

Preserve unless the user explicitly scopes otherwise:

- Do not restore CTAF/UNICOM completion bypass live authority.
- Do not add live compatibility fallback.
- Do not let pending/failed/empty CTAF display or write.
- Do not let CTAF/UNICOM advisory standby displace a controller target.
- Do not change COM writer behavior outside an approved Contract Gate.
- Do not change display behavior, row ordering, dedupe, duplicate suppression, completion identity, phase reuse, overlay cap, or `+N more ATC` unless explicitly scoped.
- Do not make source-owned fallback stable-key consumption default ON.
- Do not expose internal stable-key setting publicly without a new Contract Gate and product decision.
- Do not modify `route_sector`, `transceiver_resolver`, or HNL unless explicitly scoped.
- Do not remove deprecated public/header aliases unless explicitly scoped.
- Do not broad-clean or refactor.

## Package / Release Notes for Future Session

The user moved from live testing into V1.2.1 release preparation:

1. Clean up the repo.
2. Update the Git repository.
3. Produce the final Freeware Package V1.2.1.

Before packaging:

- Confirm user says live testing is complete.
- Run build.
- Run focused guardrails.
- Run full saved regression.
- Use the existing release gate/package tooling under `tools/release_gate/`.
- Inspect `tools/release_gate/README.md` before running package scripts.
- Do not invent a new release process unless the existing one is insufficient.

## Practical Next-Session Reminder

The safest rhythm:

1. Read this file.
2. Run `git status --short`.
3. Inspect the latest commits and outputs.
4. Ask for or wait for the user's exact next Contract Gate if code changes are requested.
5. Keep changes narrow.
6. Build.
7. Run focused scenarios.
8. Run full regression for code changes.
9. Commit only when the user asks.

Future-me: the recovery arc is closed; do not reopen it with more report-only churn unless it closes a real decision or unblocks real code work.
