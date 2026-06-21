# CTAF/UNICOM Public Header Alias Deprecation

The CTAF/UNICOM completion bypass is retired as live authority. Live CTAF/UNICOM rows must come from brain-owned advisory projection when source evidence exists.

The following public/header fields remain emitted only as deprecated compatibility mirrors:

| Deprecated alias | Replacement |
| --- | --- |
| `liveRowEmitted` | `legacyDiagnosticLiveRowEmitted` |
| `completionBypassCompatibilityOnly` | `diagnosticCompatibilityProjectionOnly` |

New diagnostics, harness expectations, and reports should prefer the replacement fields. The deprecated aliases remain present until public/header consumers are proven migrated.

Authority must be proven with invariant fields, not compatibility aliases:

- `noLiveBypassAuthority`
- `compatibilityRowsDiagnosticOnly`
- `liveRowsBrainAdvisoryOwned`
- `standbyRowsAdvisoryOwned`
- `legacyBypassFieldsQuarantined`

Removal is blocked by `public-header-consumer-risk-not-yet-cleared`. Do not remove these aliases until that compatibility window is explicitly closed.
