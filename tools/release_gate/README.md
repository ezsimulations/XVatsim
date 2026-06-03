# XVatsim Release Gates

The release gates turn manual closeout checks into repeatable validation
commands. They do not change live plugin behavior; they verify that the current
build, saved scenarios, release package, and installable artifacts agree with
each other.

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

To build a fresh freeware zip from the current Release payload:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\release_gate\New-FreewareReleasePackage.ps1
```

The freeware builder creates:

- `releases\XVatsim_<version>_Freeware_Windows_XP12\...`
- `releases\XVatsim_<version>_Freeware_Windows_XP12.zip`

It includes the plugin, transition audio, authority registry, user guide,
README, quick start, freeware license, changelog, and support instructions. It
rejects debug symbols, temporary files, logs, and build-output folders.

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
