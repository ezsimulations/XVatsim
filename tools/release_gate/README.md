# XVatsim Release Gates

The release gates turn manual closeout checks into repeatable validation
commands. They do not change live plugin behavior; they verify that the current
build, saved scenarios, release package, and installable artifacts agree with
each other.

## V2 Windows Proof Baseline

V2 development began from a deliberately locked baseline of `451` saved
regression scenarios. Its accepted Step 1 receipt is historical and immutable:

```text
outputs\v2_step_01_windows_proof_baseline_receipt.md
```

The proof runner requires an explicit expected count, receipt destination, and
receipt title. It refuses the historical Step 1 receipt path, so invoking it
without step-specific arguments cannot overwrite accepted evidence.

For V2 Step 2, run from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\release_gate\Run-V2WindowsProofBaseline.ps1 `
  -ExpectedScenarioCount 463 `
  -ReceiptRelativePath "outputs\v2_step_02_ifr_vfr_mode_foundation_receipt.md" `
  -ReceiptTitle "XVatsim V2 Step 2 IFR/VFR Mode Foundation Receipt"
```

The command removes only the validated, repository-contained
`build\v2-proof-baseline` directory, performs a fresh Visual Studio 18 x64
configuration, builds `XVatsimRegressionHarness` and `XVatsimPlugin` in Release,
runs all saved scenarios in ordinal case-insensitive filename order, and writes
an atomic, pending-live receipt to the explicitly supplied repository `outputs`
path. A failed proof leaves an existing successful receipt unchanged.

The receipt records scenario counts and an aggregate scenario-set SHA-256,
elapsed configure/build/regression time, the runner SHA-256, both binary hashes,
the starting HEAD, and the uncommitted working-tree state under test.

The expected count of `451` is intentionally locked. Any future approved
scenario addition or removal must update the expected count as part of that same
reviewed engineering step. The count must not be changed independently or merely
to obtain a passing result. An approved filename or content change must also
change the recorded aggregate scenario-set fingerprint.

## V2 Step 4 offline proof

Step 4 uses two fresh Release configurations: a proof configuration containing
the 547-scenario harness and deterministic visual tool, and a fixture-off normal
plugin configuration. The normal `.xpl` must contain the official VATSIM METAR
host contract and must not contain proof transport markers, synthetic reports,
or alternate weather providers.

The Step 4 evidence is written only to new paths under `outputs`: the offline
summary, Windows proof, receipt, and `v2_step_04_metar_visual_evidence`. Existing
evidence is not cleaned or rewritten. This gate is offline: deployment,
X-Plane/xPilot startup, live VATSIM traffic, and controlled live proof require a
separate approval.

## Final Release Gate

Run this only after the live battle-test gate is complete and the repo hygiene
audit has classified the current source state as release-ready. Store kits are
generated artifacts; they are not proof that the repo itself is clean.

The expected order is:

1. Complete the runtime/repo hygiene audit.
2. Confirm any dirty source files are intentional release source.
3. Confirm the customer license/EULA and proof-of-purchase policy.
4. Build a fresh store kit from the current Release payload.
5. Run the final release gate against that generated kit.
6. Prepare the store email and submission materials.

To build a fresh X-Plane.org Store kit from the current Release payload:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\release_gate\New-StoreSubmissionPackage.ps1
```

Then validate the newest generated kit:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\release_gate\Run-FinalReleaseValidation.ps1
```

The final gate verifies:

- Release build of `XVatsimRegressionHarness` and `XVatsimPlugin`
- Every saved scenario in `tools\regression_harness\scenarios`
- Active source text for old bootstrap/user-agent wording
- Customer package file set and forbidden debug/test artifacts
- Customer package license/EULA presence
- Customer package text for beta/preview/checkpoint wording
- Store submission materials, screenshot assets, and unresolved draft wording
- Manual customer-package smoke test proof in the V1 release audit
- Build, customer package, and installed X-Plane artifact hashes for the plugin,
  transition sound, and packaged authority registry
- Final store-upload zip hash and clean install smoke
- A `Store_Submission_Materials\11_Final_Validation_Result.txt` receipt when
  the gate passes

## Freeware Package Builder

The active public release path is the freeware Windows/X-Plane 12/xPilot
package. Store-submission scripts are historical tooling unless the store path
is deliberately reopened.

To build the V2.0.0 freeware zip from a fresh Release build and place it in the
public release folder:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\release_gate\New-FreewareReleasePackage.ps1 `
  -Version 2.0.0 `
  -BuildRoot .\build\v2.0.0-release `
  -ReleaseOutputRoot 'C:\Users\DARRON\OneDrive\Documents\XVatsim\releases'
```

The builder defaults to V2.0.0. It accepts explicit build and output roots so a
fresh release configuration can be packaged directly into the public release
folder.

The freeware builder creates:

- `releases\XVatsim_<version>_Freeware_Windows_XP12\...`
- `releases\XVatsim_<version>_Freeware_Windows_XP12.zip`

It includes the plugin, transition audio, authority registry, user guide,
README, quick start, freeware license, changelog, and support instructions. It
requires exactly those nine files and rejects extra files, debug symbols,
temporary files, logs, and build-output folders. Local `V2 Test`, source,
backup, evidence, and diagnostics folders are never copied.

## Historical Milestone 6 Gate

This remains available for the old internal Milestone 5 checkpoint validation.
Run from the repository root:

```powershell
.\tools\release_gate\Run-Milestone6Validation.ps1
```

The historical gate verifies:

- Release build of `XVatsimRegressionHarness` and `XVatsimPlugin`
- Every saved scenario in `tools\regression_harness\scenarios`
- Active source and release text for old bootstrap/user-agent wording
- Customer package file set and forbidden debug/test artifacts
- Build, active package, and installed X-Plane artifact hashes
- Internal Milestone 5 checkpoint zip hash and clean install smoke

The internal checkpoint zip is not the final store-upload package. It is only a
frozen Milestone 5 reference.
