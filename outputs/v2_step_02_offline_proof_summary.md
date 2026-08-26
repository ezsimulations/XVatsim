# XVatsim V2 Step 2 Offline Proof Summary

## Focused Red/Green Proof

Before the operating-mode implementation was added, all 12 proposed Step 2
scenarios failed with a nonzero result and the intentional diagnostic
`Operating-mode probe unavailable`. The red run elapsed time was
`00:00:00.2535406`.

After implementation, the initial focused-green run passed all 12 scenarios in
`00:00:00.2459497`. After the final step-neutral receipt wording correction, the
same 12 scenarios were rerun against the final clean Release harness and passed
12/12 in `00:00:00.1798140`:

| Scenario | Contract assertion |
| --- | --- |
| `v2_operating_mode_settings_missing_defaults_ifr.scn` | Missing setting defaults to IFR |
| `v2_operating_mode_settings_valid_values_load.scn` | Valid IFR and VFR values load |
| `v2_operating_mode_settings_invalid_defaults_ifr.scn` | Invalid/unknown values fail safely to IFR with diagnostic truth |
| `v2_operating_mode_settings_round_trip.scn` | IFR and VFR survive save/load round trips |
| `v2_operating_mode_transitions_increment_once.scn` | Both transition directions change brain state exactly once |
| `v2_operating_mode_reselect_is_idempotent.scn` | Active-mode reselection preserves mode, state source, state reason, and generation |
| `v2_operating_mode_all_resets_preserve_selection.scn` | All relevant runtime/session resets preserve mode |
| `v2_operating_mode_missing_plan_never_selects_vfr.scn` | Missing flight-plan data never infers VFR |
| `v2_operating_mode_departure_output_parity.scn` | Departure controller/display output is identical in IFR and VFR |
| `v2_operating_mode_enroute_output_parity.scn` | Enroute controller/display output is identical in IFR and VFR |
| `v2_operating_mode_arrival_output_parity.scn` | Arrival controller/display output is identical in IFR and VFR |
| `v2_operating_mode_persistence_failure_fail_soft.scn` | Save failure is fail-soft and does not retry |

Final focused result:

```text
focusedExpected=12
focusedPassed=12
focusedFailed=0
focusedElapsed=00:00:00.1798140
harnessSha256=E95799998FB16B3C7CB62F8077E3D60D2442772B5171F6D64DE1AD169D0AD11B
```

## Clean Windows Full Proof

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\release_gate\Run-V2WindowsProofBaseline.ps1 `
  -ExpectedScenarioCount 463 `
  -ReceiptRelativePath "outputs\v2_step_02_ifr_vfr_mode_foundation_receipt.md" `
  -ReceiptTitle "XVatsim V2 Step 2 IFR/VFR Mode Foundation Receipt"
```

Result:

```text
Expected scenarios: 463
Discovered scenarios: 463
Executed scenarios: 463
Passed scenarios: 463
Failed scenarios: 0
Scenario-set SHA-256: AD2702A8086FD3EC0C1473A98177B560B868580BADC5687C52F1F3B24CF6B3DB
Configure: 00:00:02.7672536
Release build: 00:00:48.0722290
Full regression: 00:00:39.5017617
Total validation: 00:01:30.8560898
Runner SHA-256: F7DD070C0A3671029EBE0E0D19D4FB2BF6C2DAC39D557F0D2DDF108908258BFC
Harness SHA-256: E95799998FB16B3C7CB62F8077E3D60D2442772B5171F6D64DE1AD169D0AD11B
Plugin SHA-256: EECD79A1C468F636AB467506C217FC7656C59E039D55CBEDF25477C823AB61B6
```

The runner resolved and validated the repository-contained
`build\v2-proof-baseline` directory, removed that directory before
configuration, built `XVatsimRegressionHarness` and `XVatsimPlugin` in Release,
and executed every discovered scenario once in explicit ordinal,
case-insensitive filename order with an ordinal tie-break.

## Intentional Negative Path

The same runner was invoked with `-ExpectedScenarioCount 464`. It failed before
configuration and propagated exit code `1`:

```text
Expected scenarios: 464
Discovered scenarios: 463
Configuration started: false
Build started: false
Scenario execution started: false
XVatsim V2 Windows proof baseline FAILED: Scenario count mismatch: expected=464 discovered=463
exitCode=1
receiptBeforeSha256=B641A02B2BCCC5333C49ADCD9676F06138CE3B23DCC595A4DFA17F2F743AB813
receiptAfterSha256=B641A02B2BCCC5333C49ADCD9676F06138CE3B23DCC595A4DFA17F2F743AB813
receiptUnchanged=True
```

## Architecture And Performance Audit

- The settings store loads a stored-preference fact and writes only a
  brain-requested preference.
- `BrainOwnedRuntimeState` owns effective mode, source, reason, generation, and
  the change/no-change decision.
- The plugin shell maps menu clicks into brain requests, performs at most one
  save for an actual transition, synchronizes checkmarks from brain-owned state,
  and publishes one bounded diagnostic.
- Idempotent selection performs no save, checkmark resynchronization, cache
  reset, overlay work, or state mutation.
- Operating-mode settings I/O occurs only in `XPluginStart` and the explicit
  menu-selection handler.
- No operating-mode reference occurs in `FlightLoopCallback`; no recurring
  network, geometry, controller scan, file access, or logging was added.
- No before/after frame timing run was required because Step 2 adds no recurring
  frame-path work.
- IFR and VFR parity scenarios prove no controller, workflow, or display change
  other than the operating-mode state/diagnostic itself.

## Historical Boundaries

- Starting commit: `2684b2f80c112673b8b9326edee1b0840c0ea9e4`.
- Accepted Step 1 receipt SHA-256 remained
  `7A762200007C2AB0817C2DBE1997624F7CA17416A542012B641C153C18A39727`.
- V1.2.3 tag and `v1-maintenance` remain at `e4a6269`.
- No V1.2.3 package, manifest, release hash, or installed V1 payload was built,
  modified, searched for, moved, or restored.
