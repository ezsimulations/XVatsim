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
3. Build a fresh store kit from the current Release payload.
4. Run the final release gate against that generated kit.
5. Prepare the store email and submission materials.

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
- Customer package text for beta/preview/checkpoint wording
- Build, customer package, and installed X-Plane artifact hashes for the plugin,
  transition sound, and packaged authority registry
- Final store-upload zip hash and clean install smoke
- A `Store_Submission_Materials\11_Final_Validation_Result.txt` receipt when
  the gate passes

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
