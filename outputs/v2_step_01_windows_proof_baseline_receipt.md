# XVatsim V2 Step 1 Windows Proof Baseline Receipt

Status: PASSED
Validated local: 2026-08-26 11:19:59 -07:00
Validated UTC: 2026-08-26 18:19:59 UTC

## Repository

- Branch: `v2-development`
- Starting HEAD: `93dc18e41a3a3a2a891aa133a76b03d8871b9cc3`
- Starting HEAD is the committed base; the following uncommitted Step 1 files were the working tree under test.
- The receipt is atomically published after this pre-publication working-tree snapshot.

```text
 M tools/release_gate/README.md
?? outputs/v2_step_01_windows_proof_baseline_receipt.md
?? tools/release_gate/Run-V2WindowsProofBaseline.ps1
```

## Configuration And Build

- CMake: `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
- CMake version: `cmake version 4.3.1-msvc1`
- Generator: `Visual Studio 18 2026`
- Platform: `x64`
- Configuration: `Release`
- Proof build directory: `build/v2-proof-baseline`
- Targets: `XVatsimRegressionHarness`, `XVatsimPlugin`

## Regression

- Expected: 451
- Discovered: 451
- Executed: 451
- Passed: 451
- Failed: 0
- First scenario: `airport_center_token_cannot_match_terminal_airspace.scn`
- Last scenario: `user_panc_vhhh_gti947_hkg_w_center.scn`
- Scenario-set SHA-256: `9A72F2473FB5D690FB6E71D9A773FD30D158868B73E08330AE85E6C4F01734B1`
- Fingerprint input per scenario: UTF-8 repository-relative filename with forward slashes, NUL, raw file bytes, NUL; scenarios use ordinal case-insensitive filename order with ordinal tie-break.
- Detailed log: `build/v2-proof-baseline/logs/scenarios_20260826_111828.log`

## Elapsed Time

- Configure: 00:00:02.6192646
- Release build: 00:00:47.9247685
- Full regression: 00:00:39.9055845
- Total validation: 00:01:30.9359729

## SHA-256 Evidence

- Runner: `tools/release_gate/Run-V2WindowsProofBaseline.ps1`
  - SHA-256: `45A19894AF319C9E3730BC0621C571627DD3C1D2ABE3251D09649F4AEEFA4108`
- Harness: `build/v2-proof-baseline/tools/XVatsimRegressionHarness.exe`
  - Bytes: 2945024
  - SHA-256: `1DCB48FD68E43680115532245786057279D35458A87547B43EA341A290466C02`
- Plugin: `build/v2-proof-baseline/dist/XVatsim/win_x64/XVatsim.xpl`
  - Bytes: 2172928
  - SHA-256: `A7816BF7328596D9825C1F7E656A515EA5B72D339D8336342E9BCC5D1A4A0155`

## Boundary Confirmation

- This validation did not build, modify, install, or repackage any V1.2.3 release artifact.
- No saved regression scenario was changed by the validation entry point.
- The receipt was staged under the ignored proof build directory and published only after complete success.
