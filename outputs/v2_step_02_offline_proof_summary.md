# XVatsim V2 Step 2 Offline Proof Summary

## Focused Red/Green Proof

Before the operating-mode implementation was added, all 12 proposed Step 2
scenarios failed with a nonzero result and the intentional diagnostic
`Operating-mode probe unavailable`. The red run elapsed time was
`00:00:00.2535406`.

After implementation, the initial focused-green run passed all 12 scenarios in
`00:00:00.2459497`. Director review later determined that six of those probes
overstated what their harness construction actually proved. The implementation
commits were preserved and the proof paths alone were corrected.

The corrected focused suite passed 12/12 against the fresh Release harness in
`00:00:00.1710702`:

| Scenario | Contract assertion |
| --- | --- |
| `v2_operating_mode_settings_missing_defaults_ifr.scn` | Missing setting defaults to IFR |
| `v2_operating_mode_settings_valid_values_load.scn` | Valid IFR and VFR values load |
| `v2_operating_mode_settings_invalid_defaults_ifr.scn` | Invalid/unknown values fail safely to IFR with diagnostic truth |
| `v2_operating_mode_settings_round_trip.scn` | IFR and VFR survive save/load round trips |
| `v2_operating_mode_transitions_increment_once.scn` | Both transition directions change brain state exactly once |
| `v2_operating_mode_reselect_is_idempotent.scn` | Active-mode reselection preserves mode, state source, state reason, and generation |
| `v2_operating_mode_all_resets_preserve_selection.scn` | The actual brain reset/clear and boundary-decision sequences represented by session, cold-and-dark, invalid-aircraft, xPilot disconnect/reconnect, callsign change, and plugin disable/enable preserve mode/source/reason/generation |
| `v2_operating_mode_missing_plan_never_selects_vfr.scn` | An actual unavailable flight-plan snapshot processed through sampling and ordinary runtime work leaves IFR selected with zero automatic changes or persistence requests |
| `v2_operating_mode_departure_output_parity.scn` | Identical Departure facts run through workflow, radio reachability, controller relevance, publisher, and overlay produce the same non-empty IFR/VFR result |
| `v2_operating_mode_enroute_output_parity.scn` | Identical Enroute facts run through workflow, radio reachability, controller relevance, publisher, and overlay produce the same non-empty IFR/VFR result |
| `v2_operating_mode_arrival_output_parity.scn` | Identical Arrival facts run through workflow, radio reachability, controller relevance, publisher, and overlay produce the same non-empty IFR/VFR result |
| `v2_operating_mode_persistence_failure_fail_soft.scn` | After one failed save, five ordinary processing cycles run without a persistence retry path |

Final focused result:

```text
focusedExpected=12
focusedPassed=12
focusedFailed=0
focusedElapsed=00:00:00.1710702
harnessSha256=00E312F865759E1221AC2B32B8EBBB761E752EB548C455729C5A7416415785D9
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
Scenario-set SHA-256: 4EDA675432281727A6DC8E8658DEBC3D1B0C5BD302BD3399E220D45FF7082903
Configure: 00:00:02.5586067
Release build: 00:00:47.5227523
Full regression: 00:00:39.7902230
Total validation: 00:01:30.3711786
Runner SHA-256: F7DD070C0A3671029EBE0E0D19D4FB2BF6C2DAC39D557F0D2DDF108908258BFC
Harness SHA-256: 00E312F865759E1221AC2B32B8EBBB761E752EB548C455729C5A7416415785D9
Plugin SHA-256: 1DB4CA933B868B88E1B08A59536A5215BD4B67ABE92BA169B98F77193FB2BFC8
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
receiptBeforeSha256=8DDD986E599E9193C2AEABF09EEB7A99E6C167F4856474D7DA30CDBD2FBB974B
receiptAfterSha256=8DDD986E599E9193C2AEABF09EEB7A99E6C167F4856474D7DA30CDBD2FBB974B
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
- IFR and VFR parity scenarios now derive workflow, controller, publisher, and
  overlay results independently from identical inputs in each mode and require
  non-empty stage-specific controller/display callsigns before accepting parity.
- The correction changed only harness source, six scenario contracts, and proof
  documentation. No plugin, brain, settings-store, or other runtime source was
  modified, so repeating live X-Plane proof was not required.

## Historical Boundaries

- Starting commit: `2684b2f80c112673b8b9326edee1b0840c0ea9e4`.
- Accepted Step 1 receipt SHA-256 remained
  `7A762200007C2AB0817C2DBE1997624F7CA17416A542012B641C153C18A39727`.
- V1.2.3 tag and `v1-maintenance` remain at `e4a6269`.
- No V1.2.3 package, manifest, release hash, or installed V1 payload was built,
  modified, searched for, moved, or restored.
