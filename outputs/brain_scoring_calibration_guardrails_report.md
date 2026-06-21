# Brain Scoring Calibration Guardrails Report

Step 29 locked down the normalized scoring direction for the brain evidence model. This was a documentation and diagnostics guardrail step only; no runtime display behavior changed.

## Files Changed

- `docs/BRAIN_EVIDENCE_SCORING_MODEL.md`
- `brain/include/XVatsim/brain/BrainDisplayIntent.h`
- `outputs/brain_scoring_calibration_guardrails_report.md`

## Scoring Guardrails Added

The scoring model now explicitly forbids:

- `100-vs-1` style scoring
- giant numeric weights
- ordinary single-fact mathematical vetoes
- representing hard blocks as very large scores
- reintroducing module-style pass/fail behavior inside brain decisions

The model now defines the preferred normalized scale:

- High confidence: `0.80` to `1.00`
- Medium confidence: `0.50` to `0.79`
- Low confidence: `0.20` to `0.49`
- Fallback/unknown: `0.05` to `0.19`
- Hard block: separate boolean/category, not a score size

## Aggregation And Fail-Soft Rules

The document now states that scores must support later aggregation, averaging, thresholding, or confidence blending. It also adds rules that correlated evidence must not be double-counted as independent proof, weak negatives should lower confidence before hiding, and missing data is not rejection.

Fail-soft thresholds are documented conceptually:

- Display when positive evidence clearly outweighs negative evidence.
- Display with warning or needs-more-evidence when evidence is mixed, weak, or incomplete.
- Defer when stage or policy says not now but relevance may still be valid.
- Hide only when negative evidence is strong and better supported than positive evidence.
- Hard-block only for impossible-to-render or explicitly safety-critical cases.

## Display Intent Examples Added

The scoring model now includes current BrainDisplayIntent diagnostic examples for:

- HNL high-confidence relation fact vs fallback-hidden inference
- fallback-hidden accepted center
- duplicate suppression
- stage-deferred row
- filtered/unknown relation
- non-displayable offline or empty-frequency row

## Runtime Code Status

No executable runtime logic changed. `BrainDisplayIntent.h` received a guardrail comment clarifying that display scores are diagnostic-only, bounded, and that `hardBlock` is separate from numeric score size.

## Verification

Build command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: PASS.

Full saved regression command:

```powershell
$h = '.\build\tools\XVatsimRegressionHarness.exe'
$scenarios = Get-ChildItem -Path '.\tools\regression_harness\scenarios' -Filter '*.scn' | Sort-Object Name
$passed = 0
foreach ($s in $scenarios) {
  $result = & $h $s.FullName
  if ($LASTEXITCODE -ne 0) { Write-Host "FAILED $($s.Name)"; $result; exit $LASTEXITCODE }
  $passed++
}
Write-Host "ALL_SCENARIOS_PASSED count=$passed"
```

Result: PASS, `ALL_SCENARIOS_PASSED count=275`.

## Remaining Scoring Risks

- Scores are still diagnostic-only and are not live authority.
- Some older compatibility fields outside the BrainDisplayIntent decision ledger still use legacy score-like values.
- Future live scoring needs preview/parity mode before authority flips.
- Calibration thresholds will need domain-specific validation before score-driven display or hide behavior is enabled.
- False-negative risk must remain explicit before any future score-driven hide decision.
