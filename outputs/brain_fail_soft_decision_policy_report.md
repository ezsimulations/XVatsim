# Brain Fail-Soft Decision Policy Report

## Files changed

- `docs/BRAIN_EVIDENCE_SCORING_MODEL.md`
- `outputs/brain_fail_soft_decision_policy_report.md`

## Summary of fail-soft policy

The scoring model now documents XVatsim's asymmetric failure risk:

- false positive: an irrelevant frequency is displayed
- false negative: a relevant frequency is hidden

The document explicitly states that a false negative is the higher-severity failure for XVatsim because the product is a clutter-reduction filter, not an ATC authority system.

The model now requires fail-soft behavior:

- uncertain evidence should not hard-hide a candidate
- weak negative evidence should not override strong positive evidence
- fallback inference must not hide when high-confidence accepted evidence exists
- incomplete evidence should usually produce displayed-with-warning, deferred, or lower-priority output rather than silent suppression
- missing data should not equal rejection

Display verdict tiers were added:

- `display`
- `display-low-confidence`
- `defer-by-stage`
- `hide-strong-evidence`
- `hard-block`

Default conflict rules were updated so high-confidence positive evidence beats fallback negative evidence, hard blocks remain rare and explicit, and close evidence should favor display-low-confidence or defer rather than silent hide.

The policy was applied to:

- radio-range
- authority relevance
- controller relevance
- display intent
- phase publisher reuse
- overlay cap
- CTAF/UNICOM

The HNL-class rule now explicitly states that accepted relation facts are high confidence, fallback polygon inference is low/fallback confidence, and fallback inference must not hide an accepted center when a high-confidence relation fact supports display.

## Runtime code changed

No runtime code changed. This step updated documentation/reporting only.

## Build command and result

Command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: PASS.

## Full regression command and result

Command:

```powershell
$h = '.\build\tools\XVatsimRegressionHarness.exe'
$scenarios = Get-ChildItem -Path '.\tools\regression_harness\scenarios' -Filter '*.scn' | Sort-Object Name
$passed = 0
foreach ($s in $scenarios) {
    $result = & $h $s.FullName
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED $($s.Name)"
        $result
        exit $LASTEXITCODE
    }
    $passed++
}
Write-Host "ALL_SCENARIOS_PASSED count=$passed"
```

Result: PASS, `ALL_SCENARIOS_PASSED count=275`.
