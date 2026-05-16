# XVatsim Release Gate

Milestone 6 turns the manual closeout checks into a repeatable validation gate.
This does not change live plugin behavior; it checks whether the current build,
saved scenarios, active release materials, and internal checkpoint package still
agree with each other.

Run from the repository root:

```powershell
.\tools\release_gate\Run-Milestone6Validation.ps1
```

The gate currently verifies:

- Release build of `XVatsimRegressionHarness` and `XVatsimPlugin`
- Every saved scenario in `tools\regression_harness\scenarios`
- Active source and release text for old bootstrap/user-agent wording
- Customer package file set and forbidden debug/test artifacts
- Build, active package, and installed X-Plane artifact hashes
- Internal Milestone 5 checkpoint zip hash and clean install smoke

The internal checkpoint zip is still not the final store-upload package. It is
only a frozen Milestone 5 reference until Milestones 6 through 9 are complete.
