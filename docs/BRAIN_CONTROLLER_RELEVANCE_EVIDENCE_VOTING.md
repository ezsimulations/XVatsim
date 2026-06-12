# Brain Controller Relevance Evidence Voting

Date locked: 2026-06-12

Status: implemented in the brain relevance worker and covered by the
regression harness.

## Purpose

The KMDW -> KRAP live case as SWA2002 exposed a bad failure mode:
`CHI_Z_APP` on `119.000` was radio-reachable at KMDW, was a VATSIM APP/DEP
controller, and shared the CHI route-center root with the accepted current
center, but it was hidden because one terminal authority text token normalized
to `CHI_APP` while the endpoint owner report said `C90_APP`/`C90_DEP`.

That is no longer allowed to be a one-string hard veto.

The controller relevance brain now records multiple deciding factors for
terminal APP/DEP candidates and reports the vote ledger in every terminal
accept/reject completion reason.

## Ownership Boundary

- Modules parse and report facts.
- The plugin remains an X-Plane shell and does not decide controller authority.
- The brain owns the final APP/DEP relevance decision.
- The UI displays only brain-approved facts.

The brain consumes these reports when they are present:

- VATSIM controller identity, frequency, and facility group.
- Radio-board or AFV transceiver reachability and distance.
- Terminal authority owners and polygons, including SimAware-backed reports.
- Airport frequency catalog facts for departure and arrival endpoints.
- Route-sector center patterns and prefixes.

Modules must not decide "show or hide this APP/DEP controller." They only
provide evidence.

## Terminal Vote Factors

For APP/DEP candidates, the brain builds a terminal decision evidence packet.
Each factor is named in the completion reason.

Positive factors:

- `vatsim-appdep`: the live VATSIM controller candidate is APP/DEP.
- `radio-near5`: the radio-range report identifies the controller within 5 NM.
- `terminal-owner`: terminal authority owner/polygon facts match the candidate.
- `endpoint-frequency`: airport frequency facts match the endpoint and role.
- `route-center-root`: the APP/DEP root matches an accepted or matching route
  center root, such as `CHI_Z_APP` sharing `CHI` with `CHI_35_CTR`.

Negative factors:

- `terminal-owner`: terminal authority owner/polygon facts are present but do
  not match the candidate.
- `endpoint-frequency`: endpoint frequency facts exist but do not support the
  candidate frequency/role.

The radio-board candidate itself is still the candidate source. `radio-near5`
is a positive vote when the candidate is 5 NM or closer; it is not a blind pass
by itself.

## Decision Rule

The brain accepts and displays a terminal APP/DEP candidate when at least two
positive factors exist and the positive count is greater than the negative
count.

The brain rejects and hides a candidate when there is not enough independent
positive evidence, or when the vote count is tied or negative.

Endpoint frequency facts remain strong evidence, but a single endpoint
frequency miss is no longer a hard veto. If two other independent factors say
yes and only endpoint frequency says no, the brain accepts and reports the
negative vote.

Terminal owner mismatch is now evidence against the candidate, not an automatic
kill switch.

Every terminal completion reason includes:

```text
votes=<positive>/<negative>:yes=<factor+factor>:no=<factor>:final=<accept-display|reject-hide>
```

## KMDW SWA2002 Decision

Live symptom:

- Flight: `SWA2002`
- Route: `KMDW -> KRAP`
- Phase: Departure, sitting on the ground at KMDW
- Expected TRACON: `CHI_Z_APP` on `119.000`
- Displayed before fix: CTAF and `CHI_35_CTR` on `134.875`

Facts reported to the brain:

- `CHI_Z_APP` was present in the reachable radio candidates.
- `CHI_Z_APP` had facility group `APP_DEP`.
- Radio distance was `1 NM`, so the brain counted `radio-near5`.
- Terminal authority owner text reported `C90_APP`/`C90_DEP`, which did not
  text-match `CHI_APP`, so the brain counted one negative `terminal-owner`.
- The route center context accepted `CHI_35_CTR`, so the `CHI` root matched
  the current route center and counted `route-center-root`.

Brain result after this change:

```text
CHI_Z_APP:accepted:departure-terminal-majority-match:votes=3/1:yes=vatsim-appdep+radio-near5+route-center-root:no=terminal-owner:final=accept-display:CHI_APP:owner=C90_APP+C90_DEP:poly=C90
```

That means the old single text mismatch is still visible in diagnostics, but it
no longer hides the controller when three independent factors support showing
it.

## Guardrail Scenarios

The existing scenario suite now records the same vote ledger for prior cases:

- Owner-confirmed terminal controllers accept as `votes=3/0` or `2/0`.
- Endpoint frequency proof can accept a controller even when owner text says
  no, as in the KONT SoCal disambiguation case.
- Endpoint frequency mismatch is now one negative vote, not a hard veto. The
  KONT SoCal `SCT_APP` case accepts as `votes=2/1` when VATSIM APP/DEP and
  terminal owner facts say yes.
- A nearby mismatch is also one negative vote, not a hard veto. The KONT
  `LAS_F_APP` case accepts as `votes=2/1` because VATSIM APP/DEP and
  `radio-near5` say yes while terminal owner says no.
- Controllers with only one positive and one negative factor still reject as a
  tie, which keeps unsupported text mismatches visible as `final=reject-hide`.
- Arrival and departure owner-filter cases now expose why each hidden row was
  hidden instead of silently disappearing.

## Test Documentation

Primary prevention test:

```text
tools/regression_harness/scenarios/brain_controller_relevance_departure_terminal_majority_accepts_kmdw_chi_app.scn
```

This scenario reproduces the KMDW departure problem. It proves that
`CHI_Z_APP` is accepted and displayed by a 3 positive / 1 negative majority
when the only rejection is the `CHI_APP` versus `C90_APP`/`C90_DEP` terminal
owner text mismatch.

Relevant validation commands:

```powershell
$p = $env:Path
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $p, 'Process')
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo --target XVatsimRegressionHarness

& '.\build\tools\XVatsimRegressionHarness.exe' '.\tools\regression_harness\scenarios\brain_controller_relevance_departure_terminal_majority_accepts_kmdw_chi_app.scn'

$scenarios = Get-ChildItem '.\tools\regression_harness\scenarios\*.scn' | Sort-Object Name
foreach ($s in $scenarios) {
  & '.\build\tools\XVatsimRegressionHarness.exe' $s.FullName > $null
  if ($LASTEXITCODE -ne 0) {
    Write-Host "FAILED $($s.Name)"
    exit $LASTEXITCODE
  }
}
"passed $($scenarios.Count) scenarios"
```

Current validation result:

```text
passed 15 brain_controller_relevance scenarios
passed 263 scenarios
```
