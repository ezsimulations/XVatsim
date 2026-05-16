# XVatsim Release Gates

The release gates turn manual closeout checks into repeatable validation
commands. They do not change live plugin behavior; they verify that the current
build, saved scenarios, release package, and installable artifacts agree with
each other.

## Final Release Gate

Run this after Milestones 1 through 9 are complete and the customer package has
been refreshed from `build\dist`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\release_gate\Run-FinalReleaseValidation.ps1 -ZipPath .\releases\XVatsim_XPlaneOrg_Store_Submission_Kit_1.0.0_2026-05-15\XVatsim_1.0.0_Windows_XP12_store_upload_2026-05-16.zip
```

The final gate verifies:

- Release build of `XVatsimRegressionHarness` and `XVatsimPlugin`
- Every saved scenario in `tools\regression_harness\scenarios`
- Active source text for old bootstrap/user-agent wording
- Customer package file set and forbidden debug/test artifacts
- Customer package text for beta/preview/checkpoint wording
- Build, customer package, and installed X-Plane artifact hashes
- Final store-upload zip hash and clean install smoke

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
