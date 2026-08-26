# XVatsim V2 Step 2 IFR/VFR Mode Foundation Receipt

Status: PASSED
Finalized local: 2026-08-26 13:08:15 -07:00
Finalized UTC: 2026-08-26 20:08:15 UTC

## Repository And Commit Boundary

- Branch: `v2-development`
- Starting commit: `2684b2f80c112673b8b9326edee1b0840c0ea9e4`
- Proven implementation commit: `8b10841fd19a1ee1a908b74ffd3246e36b029647`
- Receipt closeout commit: the receipt-only commit containing this file, immediately following the proven implementation commit.
- The receipt cannot embed the hash of its own commit without changing that hash; `git log -1` identifies the receipt closeout commit.
- Step 2 uses the approved two-commit closeout: implementation, scenarios, tooling, and evidence in the proven implementation commit; only this finalized receipt in the receipt closeout commit.

## Complete Step 2 Changed-File List

The proven implementation commit contains these 34 files:

```text
brain/include/XVatsim/brain/BrainOwnedRuntime.h
brain/src/BrainOwnedRuntime.cpp
modules/settings_store/include/XVatsim/modules/settings_store/SettingsStore.h
modules/settings_store/src/SettingsStore.cpp
outputs/v2_step_02_live_evidence/01_ifr_default_no_preference.png
outputs/v2_step_02_live_evidence/02_ifr_card_before_mode_change.png
outputs/v2_step_02_live_evidence/03_vfr_selected_card_unchanged.png
outputs/v2_step_02_live_evidence/04_vfr_after_reset_session.png
outputs/v2_step_02_live_evidence/05_vfr_after_xpilot_reconnect.png
outputs/v2_step_02_live_evidence/06_vfr_after_restart.png
outputs/v2_step_02_live_evidence/07_ifr_selected_before_restart.png
outputs/v2_step_02_live_evidence/08_ifr_after_restart.png
outputs/v2_step_02_live_evidence/09_xvatsim_live_diagnostics_full.txt
outputs/v2_step_02_live_evidence/10_xplane_live_log.txt
outputs/v2_step_02_live_evidence/11_live_proof_summary.md
outputs/v2_step_02_offline_proof_summary.md
plugin/src/XVatsimPlugin.cpp
tools/regression_harness/CMakeLists.txt
tools/regression_harness/README.txt
tools/regression_harness/scenarios/v2_operating_mode_all_resets_preserve_selection.scn
tools/regression_harness/scenarios/v2_operating_mode_arrival_output_parity.scn
tools/regression_harness/scenarios/v2_operating_mode_departure_output_parity.scn
tools/regression_harness/scenarios/v2_operating_mode_enroute_output_parity.scn
tools/regression_harness/scenarios/v2_operating_mode_missing_plan_never_selects_vfr.scn
tools/regression_harness/scenarios/v2_operating_mode_persistence_failure_fail_soft.scn
tools/regression_harness/scenarios/v2_operating_mode_reselect_is_idempotent.scn
tools/regression_harness/scenarios/v2_operating_mode_settings_invalid_defaults_ifr.scn
tools/regression_harness/scenarios/v2_operating_mode_settings_missing_defaults_ifr.scn
tools/regression_harness/scenarios/v2_operating_mode_settings_round_trip.scn
tools/regression_harness/scenarios/v2_operating_mode_settings_valid_values_load.scn
tools/regression_harness/scenarios/v2_operating_mode_transitions_increment_once.scn
tools/regression_harness/src/main.cpp
tools/release_gate/README.md
tools/release_gate/Run-V2WindowsProofBaseline.ps1
```

The receipt closeout commit adds only:

```text
outputs/v2_step_02_ifr_vfr_mode_foundation_receipt.md
```

## Architecture And Product Result

- Supported operating modes are exactly IFR and VFR.
- `BrainOwnedRuntimeState` is the only owner of effective mode, state source, state reason, generation, and change/no-change decisions.
- The settings store produces a stored-preference fact and persists only a brain-requested preference.
- The plugin shell translates menu clicks into brain requests, performs requested persistence, synchronizes checkmarks from brain-owned state, and publishes bounded diagnostics.
- Controller modules, workflow code, and overlay code do not infer or decide the operating mode.
- No automatic VFR inference exists; missing VATSIM flight-plan data remains IFR unless the pilot explicitly selects VFR.
- The persistence format is exactly `operating_mode=ifr` or `operating_mode=vfr`.
- Missing, empty, malformed, unknown, and unavailable values fail safely to IFR with truthful source/reason diagnostics.
- An actual selection changes brain state once and advances generation once.
- Re-selecting the active mode preserves brain-owned mode, source, reason, and generation; it performs no persistence, cache reset, menu resynchronization, or display work.
- A save failure leaves the requested brain-owned mode active for the session, keeps the menu aligned with brain state, emits one failure diagnostic, and schedules no retry.
- `Reset XVatsim Session`, cold-and-dark reset, invalid-aircraft reset, runtime-cache reset, xPilot disconnect/reconnect, and callsign changes preserve the selected mode.
- `plugin/src/XVatsimPlugin.cpp` was touched only to load the preference at startup, create/synchronize the two menu choices, forward explicit pilot requests to the brain, persist brain-requested changes, and log bounded diagnostics.
- No overlay label, banner, ORB, drawer, controller policy, frequency policy, route behavior, flight-plan behavior, or other live IFR/VFR behavioral difference was introduced.

## Focused Red/Green Proof

- Pre-implementation red: 12/12 proposed scenarios failed nonzero with the intentional `Operating-mode probe unavailable` diagnostic; elapsed `00:00:00.2535406`.
- Initial post-implementation green: 12/12 passed; elapsed `00:00:00.2459497`.
- Final green against the clean Release proof harness: 12 expected, 12 passed, 0 failed; elapsed `00:00:00.1798140`.
- Final focused harness SHA-256: `E95799998FB16B3C7CB62F8077E3D60D2442772B5171F6D64DE1AD169D0AD11B`.

The focused scenarios prove:

1. Missing setting defaults to IFR.
2. Valid IFR and VFR values load.
3. Invalid/unknown stored values default safely to IFR with diagnostic truth.
4. IFR and VFR survive save/load round trips.
5. IFR-to-VFR and VFR-to-IFR change brain state exactly once.
6. Active-mode reselection is idempotent and preserves state source/reason/generation.
7. Every relevant runtime/session reset preserves mode.
8. Missing flight-plan data never selects VFR automatically.
9. Departure controller/display output is identical in IFR and VFR.
10. Enroute controller/display output is identical in IFR and VFR.
11. Arrival controller/display output is identical in IFR and VFR.
12. Persistence failure is fail-soft and does not repeatedly retry.

Detailed focused evidence: `outputs/v2_step_02_offline_proof_summary.md`.

## Clean Windows Release Proof

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\release_gate\Run-V2WindowsProofBaseline.ps1 `
  -ExpectedScenarioCount 463 `
  -ReceiptRelativePath "outputs\v2_step_02_ifr_vfr_mode_foundation_receipt.md" `
  -ReceiptTitle "XVatsim V2 Step 2 IFR/VFR Mode Foundation Receipt"
```

- CMake: `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
- CMake version: `4.3.1-msvc1`
- Generator: `Visual Studio 18 2026`
- Platform: `x64`
- Configuration: `Release`
- Validated clean build directory: `build/v2-proof-baseline`
- Release targets: `XVatsimRegressionHarness`, `XVatsimPlugin`
- Expected scenarios: `463`
- Discovered scenarios: `463`
- Executed scenarios: `463`
- Passed scenarios: `463`
- Failed scenarios: `0`
- First scenario: `airport_center_token_cannot_match_terminal_airspace.scn`
- Last scenario: `v2_operating_mode_transitions_increment_once.scn`
- Scenario-set SHA-256: `AD2702A8086FD3EC0C1473A98177B560B868580BADC5687C52F1F3B24CF6B3DB`
- Ordering: explicit ordinal, case-insensitive filename comparison with ordinal tie-break.
- Fingerprint input: UTF-8 repository-relative filename using forward slashes, NUL, raw scenario bytes, NUL.
- Detailed regression log: `build/v2-proof-baseline/logs/scenarios_20260826_130203.log`

Elapsed times:

- Configure: `00:00:02.7672536`
- Release builds: `00:00:48.0722290`
- Full regression: `00:00:39.5017617`
- Total validation: `00:01:30.8560898`
- Final focused rerun: `00:00:00.1798140`

Final proof hashes:

- Runner, 21,024 bytes: `F7DD070C0A3671029EBE0E0D19D4FB2BF6C2DAC39D557F0D2DDF108908258BFC`
- Release harness, 3,006,976 bytes: `E95799998FB16B3C7CB62F8077E3D60D2442772B5171F6D64DE1AD169D0AD11B`
- Release plugin, 2,179,584 bytes: `EECD79A1C468F636AB467506C217FC7656C59E039D55CBEDF25477C823AB61B6`
- Complete scenario set: `AD2702A8086FD3EC0C1473A98177B560B868580BADC5687C52F1F3B24CF6B3DB`

The final clean rebuild occurred after a step-neutral wording correction in the proof runner. No plugin, brain, settings-store, harness, or scenario source changed after live proof. The live-deployed plugin was therefore built from identical runtime source; its live binary SHA-256 was `307A4B873EA937F028C23787A4A583AF812EA544247E63B6168745E46FD1CB3D`.

## Intentional Negative Path

The runner was invoked with an intentionally incorrect expected count of 464.

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

This proves the nonzero result propagated, configuration did not begin, and the atomically published successful Windows/pending-live receipt remained unchanged.

## Live Windows/X-Plane Proof

Darron performed and witnessed the complete live procedure on 2026-08-26:

- With no valid operating-mode preference, startup checked only IFR.
- Selecting VFR immediately checked only VFR and persisted successfully.
- Reset Session preserved VFR.
- xPilot disconnect/reconnect preserved VFR.
- X-Plane/plugin restart loaded VFR from the settings store.
- Selecting IFR immediately checked only IFR and persisted successfully.
- X-Plane/plugin restart loaded IFR from the settings store.
- The XVatsim card/controller output did not visibly change solely because mode changed.
- Diagnostics recorded initialization source, actual changes, idempotent requests, and persistence success without per-frame spam.
- Fail-soft persistence failure was proven deterministically in the focused harness; no destructive live failure injection was required.

The complete bounded operating-mode event sequence contained exactly seven events across the live sessions. Restart events used `stateSource=settings-store`, explicit transitions used `stateSource=pilot-menu`, and idempotent request diagnostics used `requestSource=pilot-menu` without falsely changing stored state source/reason.

Live evidence directory: `outputs/v2_step_02_live_evidence`

| Evidence | SHA-256 |
| --- | --- |
| `01_ifr_default_no_preference.png` | `663C879DCB85D4B7A8DCEE4999B5F82B8CF271D61729CF5103E2C7F604ACB69B` |
| `03_vfr_selected_card_unchanged.png` | `D22970EC7C8DEB28B8BD2D3192379EAB6ABCEE4C645D549436C545AC0AAEC028` |
| `04_vfr_after_reset_session.png` | `B7A2189E39E667A37BA32FB8F4758726727F06BF5AD9B4E5E887636E162E499D` |
| `05_vfr_after_xpilot_reconnect.png` | `D0F14435B1A47849F284A1AD38F9586859E11E5A63DB0425CF4A28D23F483A39` |
| `06_vfr_after_restart.png` | `71B6BBA5AE3D0B1099F8EF44CB07AB8743B2305D566AFB335821EB75E97B6288` |
| `07_ifr_selected_before_restart.png` | `8848E376C5ED5198FB8128636F5DD969195FB79729D0FBDCBA4A431AAC7815BC` |
| `08_ifr_after_restart.png` | `2B2FAE2EDDAFA44C975D23C180684A120BA9C246446032245E48ADF320C5FF38` |
| `09_xvatsim_live_diagnostics_full.txt` | `65B79F796CEF5293965198753431AFB64335E5BBA3DD81E1101C51798814058C` |
| `10_xplane_live_log.txt` | `246A875FE651A5EEC9B8B228C3032720D9EB07B61B89D332CB774E8F9EA634CC` |
| `11_live_proof_summary.md` | `9D32B90B62226C49E6A38005931B5AED91119A282D7813C57321310F3CF75ADA` |

The live payload also contained only the required assets:

- `authority_source_registry.json`: `3676CA43E5AFB8A5E443FDE6D04616918029E004E17EFAA022695D91E23DB60B`
- `ui_transition.mp3`: `C7BBE97DAD356C68FDFE40F9E8C1CF4EADEBCCD125463214F963F66356F8D9F1`

## Performance Conclusion

- `FlightLoopCallback` contains no operating-mode reference or new mode work.
- Settings-file reads occur once during `XPluginStart`.
- Settings-file writes occur only after an explicit pilot menu request that the brain classifies as an actual change.
- Idempotent requests perform no save, menu synchronization, cache reset, display rebuild, geometry work, controller scan, or network work.
- Mode diagnostics occur only at initialization and explicit selection.
- No recurring frame-path work was introduced, so before/after frame timing was not required.
- Full regression and live diagnostics showed no mode-related performance regression or logging spam.

## Controlled Live-Test Rollback

After X-Plane and xPilot were stopped, the controlled active slot was restored to its exact pre-Step-2 inventory:

- `C:\X-Plane 12\Resources\plugins\XVatsim` contains only `logs` and `V2 Test`.
- The active `win_x64` directory is absent.
- Active XVatsim `.xpl` count is zero.
- `V2 Test` remains present and contains zero `.xpl` files.
- Original `XVatsim.prf` restored: `E866EBAC005DAF39B7359A547A795D39D9AE6ED60C018B4637DA8753F67E8504`.
- Original X-Plane `Log.txt` restored: `D4E54021690B8CEE2CCA18838C16A449705B83F38F89CCE7AB51B78193FC7E71`.
- Original August 23 diagnostic restored: `0C42FBA5FB4DE0D8BE2E4120E8C4EBC5FBF6B20A017B586677BCDDC209EAAFB4`.
- Original August 24 diagnostic restored: `B636260B67BD48AD697DDC600B561BC1E626EE25D1EDC7C12E88A87295B13656`.
- Original August 25 diagnostic restored: `1D3F83427ECD5DA2AD608AD5B22E14F42F87B92429D7B40F906C0C370FBDF087`.
- Original August 26 diagnostic restored: `BD9C7B29EE6BBF058EBE600846C0B0DAA07EB86D95843E91F94BDEFFB57BB628`.
- The staging/backup folder remains at `C:\X-Plane 12\Resources\plugins\XVatsim\V2 Test\step_02_pretest_backup`.
- No V1 payload was searched for, moved, overwritten, or restored.

## Historical Evidence And Scope Integrity

- Accepted Step 1 receipt was untouched and remains SHA-256 `7A762200007C2AB0817C2DBE1997624F7CA17416A542012B641C153C18A39727`.
- The Step 1 receipt path is explicitly refused by the evolved runner.
- V1.2.3 tag and `v1-maintenance` still resolve to commit `e4a626975513db93105b9c625f4fbb941e7f9c05`.
- No V1.2.3 package, manifest, recorded release hash, release branch content, or installed V1 payload was modified.
- The final scope audit covered tracked modifications, untracked additions, staged new files, and whitespace errors in new files.
- `git diff --cached --check` passed before the proven implementation commit.
- No existing saved scenario was weakened or modified; exactly 12 approved scenarios were added, changing the locked total from 451 to 463.

## Known Limitations And Deferred VFR Behavior

- VFR is only an explicit, persistent operating-mode foundation in Step 2.
- VFR does not yet change workflow stages, controller evidence, controller selection or ordering, frequencies, flight-plan processing, route polygons, overlay output, or any controller/display result.
- No VFR controller projection, ORB rail, information drawer, METAR, ATIS, PDC, private-message, or network behavior is implemented.
- Existing IFR flight-plan requirements and controller behavior remain unchanged.
- No product-version bump was performed; the existing menu version label remains unchanged by contract.
- Persistence failure is covered deterministically by the harness and bounded diagnostic contract; the successful live procedure did not intentionally damage the user preference path.

## Acceptance Conclusion

V2 Step 2 satisfies the approved contract: the brain exclusively owns IFR/VFR mode decisions, persistence is safe and fail-soft, all required reset and parity behavior is proven, both Release targets build, all 463 saved scenarios pass, intentional failure propagates before configuration without overwriting successful evidence, Darron witnessed the complete live menu/restart procedure, performance boundaries are preserved, external live-test mutations were rolled back exactly, and Step 1/V1.2.3 historical evidence remains untouched.
