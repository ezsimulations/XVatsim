# Brain Controller Relevance Weighted Evidence Ledger

Date locked: 2026-06-12

Status: implemented in the brain relevance worker, wired into the clean
runtime path, and covered by the regression harness.

## Purpose

The KMDW -> KRAP live case as SWA2002 exposed a bad failure mode:
`CHI_Z_APP` on `119.000` was radio-reachable at KMDW, was a VATSIM APP/DEP
controller, and shared the CHI route-center root with the accepted current
center. It was hidden because one terminal authority text token normalized to
`CHI_APP` while the endpoint owner report said `C90_APP`/`C90_DEP`.

That is no longer allowed to be a one-string hard veto.

The controller relevance brain now builds a weighted evidence ledger for every
terminal APP/DEP candidate. The ledger reports:

- raw positive/negative vote count
- weighted positive/negative score
- positive non-FAA evidence family count
- positive, negative, and neutral factor names
- confidence
- final action: `accept-display` or `reject-hide`

## Ownership Boundary

- Modules parse and report facts.
- The plugin remains an X-Plane shell and does not decide controller authority.
- The brain owns the final APP/DEP relevance decision.
- The UI displays only brain-approved facts.

The brain consumes these reports when they are present:

- VATSIM controller identity, frequency, and facility group.
- Radio-board or AFV transceiver reachability and distance.
- AFV station coordinates from transceiver resolution.
- Terminal authority owners and polygons, including SimAware-backed reports.
- Source-owned authority relevance from the scheduled route authority verifier.
- FAA/NASR airport frequency catalog facts for departure and arrival endpoints.
- Route-sector center patterns and prefixes.
- Radio tuning state for cache invalidation.

Modules must not decide "show or hide this APP/DEP controller." They only
provide evidence.

## Evidence Weights

Positive factors:

- `source-owned-authority`: weight `4`, non-FAA family `source-authority`.
- `terminal-owner`: weight `3`, non-FAA family `terminal-source`.
- `route-center-root`: weight `3`, non-FAA family `route-context`.
- `vatsim-appdep`: weight `2`, non-FAA family `vatsim-live`.
- `radio-near5`: weight `2`, non-FAA family `afv-radio`.
- `faa-frequency`: weight `1`, FAA-derived family `faa-frequency`.

Negative factors:

- `terminal-owner`: weight `2` when terminal owner facts are present and do
  not match the candidate.

Neutral facts:

- `radio-distance-over5`: the radio-board candidate has range data, but it is
  farther than 5 NM.
- `afv-station-geo`: AFV station coordinates were available for diagnostics.
- `faa-frequency-miss`: FAA/NASR endpoint facts exist but do not match the
  VATSIM candidate frequency/role.

FAA/NASR is intentionally weak. VATSIM can use virtual or pseudo frequencies,
so FAA/NASR can help when it agrees, but an FAA miss is neutral and cannot hide
a VATSIM controller by itself.

## Decision Rule

The brain accepts and displays a terminal APP/DEP candidate only when all three
conditions are true:

```text
positiveScore > negativeScore
positiveNonFaaScore > negativeScore
positiveNonFaaFamilies >= 2
```

This means a candidate needs at least two independent non-FAA evidence families
to display. FAA/NASR can add a small positive score, but it cannot become the
second required non-FAA family and it cannot rescue a candidate alone.

The brain rejects and hides a candidate when the evidence is tied, negative, or
too narrow. A single string mismatch is no longer a hard kill switch; it is one
negative item inside the weighted ledger.

Every terminal completion reason includes:

```text
votes=<positive>/<negative>:score=<positive>/<negative>:yes=<factor+factor>:no=<factor>:neutral=<factor+factor>:families=<non-faa-family-count>:confidence=<low|medium|high>:final=<accept-display|reject-hide>
```

DEL/GND/TWR airport-local matches remain airport-local authority decisions. FAA
frequency facts are appended for diagnostics, but an FAA miss does not reject a
local airport callsign match.

## Runtime Wiring

The clean runtime path now passes `AuthorityRelevanceSnapshot` into
`BrainOwnedControllerRelevanceInputRequest`. This gives the controller
relevance brain the already scheduled and cached route authority proof without
reopening old module-side display decisions.

The controller relevance cache includes:

- radio board hash
- route polygon hash
- departure and arrival terminal authority hashes
- airport frequency hash
- authority relevance hash
- radio tuning hash
- workflow stage
- current polygon key

That keeps CPU behavior bounded. The brain relaxes on unchanged inputs and only
re-evaluates when a real evidence source changes.

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
CHI_Z_APP:accepted:departure-terminal-majority-match:votes=3/1:score=7/2:yes=vatsim-appdep+radio-near5+route-center-root:no=terminal-owner:neutral=none:families=3:confidence=high:final=accept-display:CHI_APP:owner=C90_APP+C90_DEP:poly=C90
```

That means the old single text mismatch is still visible in diagnostics, but it
no longer hides the controller when three independent non-FAA families support
showing it.

## Guardrail Scenarios

The controller relevance scenario suite records the weighted ledger for prior
edge cases:

- Owner-confirmed terminal controllers accept with `terminal-owner` plus either
  `radio-near5`, `vatsim-appdep`, `faa-frequency`, or another non-FAA family.
- KMDW accepts `CHI_Z_APP` with `score=7/2`, `families=3`, and
  `final=accept-display`.
- KONT rejects `LAX_S_DEP` even though FAA/NASR frequency agrees, because the
  only non-FAA positive family is VATSIM live data and terminal owner says no.
- KONT accepts `SCT_APP` even with `faa-frequency-miss`, because VATSIM live
  and terminal-owner facts are two non-FAA families and the FAA miss is neutral.
- KONT accepts nearby `LAS_F_APP` with low confidence when VATSIM APP/DEP and
  `radio-near5` beat one terminal-owner mismatch.
- KSDF rejects `MEM_E_APP` because VATSIM live alone ties the terminal owner
  mismatch, while the FAA miss is neutral.
- Controllers with only one positive family and one terminal-owner mismatch
  still reject as `final=reject-hide`.
- Arrival and departure owner-filter cases expose why each hidden row was
  hidden instead of silently disappearing.

## Test Documentation

Primary prevention test:

```text
tools/regression_harness/scenarios/brain_controller_relevance_departure_terminal_majority_accepts_kmdw_chi_app.scn
```

This scenario reproduces the KMDW departure problem. It proves that
`CHI_Z_APP` is accepted and displayed when the only rejection is the `CHI_APP`
versus `C90_APP`/`C90_DEP` terminal owner text mismatch.

Relevant validation commands:

```powershell
$p = $env:Path
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $p, 'Process')
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo --target XVatsimRegressionHarness

& '.\build\tools\XVatsimRegressionHarness.exe' '.\tools\regression_harness\scenarios\brain_controller_relevance_departure_terminal_majority_accepts_kmdw_chi_app.scn'

$failed = @()
$count = 0
Get-ChildItem '.\tools\regression_harness\scenarios' -Filter '*.scn' | Sort-Object Name | ForEach-Object {
  $count++
  & '.\build\tools\XVatsimRegressionHarness.exe' $_.FullName *> $null
  if ($LASTEXITCODE -ne 0) {
    $failed += $_.Name
  }
}
if ($failed.Count -eq 0) {
  "passed=$count"
} else {
  "failed=$($failed.Count)/$count " + ($failed -join ',')
}
```

Current validation result:

```text
passed 15 brain_controller_relevance scenarios
passed 263 scenarios
full RelWithDebInfo build completed, including XVatsim.xpl
```
