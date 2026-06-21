# CTAF/UNICOM Standby Assist Gate

The CTAF/UNICOM standby assist gate is a narrow opt-in extension of standby assist. It allows a resolved CTAF advisory candidate or a resolved `NO CTAF / UNICOM` fallback advisory candidate to become the COM1 standby target only when the normal standby assist gate is enabled and the direct CTAF gate is also enabled.

## Default State

The CTAF/UNICOM advisory gate defaults off.

If the setting is missing from the settings file, `directCtafStandbyAssistEnabled=false` and diagnostics report `directCtafGateSource=default`.

Legacy standby assist alone does not enable CTAF/UNICOM advisory writes. `standby_assist=true` only keeps CTAF/UNICOM advisory candidates in dry-run/no-write diagnostics unless the direct CTAF gate is also enabled.

## Settings Keys

Either compatibility key enables the CTAF/UNICOM advisory gate when set to `true`:

- `direct_ctaf_standby_assist=true`
- `standby_assist_direct_ctaf=true`

Normal standby assist remains controlled separately by:

- `standby_assist=true`

Both gates must be on before CTAF or `NO CTAF / UNICOM` can become a live COM1 standby target.

## Safety Boundaries

CTAF/UNICOM standby assist only applies to resolved CTAF advisory candidates and resolved `NO CTAF / UNICOM` fallback advisory candidates.

It does not apply to:

- Pending CTAF lookup.
- Failed CTAF lookup.
- Empty CTAF frequency.
- Guard or invalid frequencies.

CTAF/UNICOM advisory standby never displaces an existing controller standby target. If a controller target is selected, the controller target wins and the advisory candidate remains blocked in diagnostics.

COM writer behavior is unchanged. CTAF/UNICOM advisory standby uses the same existing brain-approved COM1 standby write path as controller standby assist, and only after all brain-owned safety gates pass.

## Diagnostics

Standby assist settings diagnostics include:

- `standbyAssistEnabled`
- `directCtafStandbyAssistEnabled`
- `directCtafGateSource`
- `directCtafGateEffective`

Valid gate source values are:

- `default`
- `settings-store`
- `harness`
- `unknown`

The standby decision ledger includes:

- `featureGateRequired=direct-ctaf-standby-assist`
- `featureGateSatisfied`
- `featureGateBlockedReason`

Writer result diagnostics remain separate and brain-owned. They identify whether the selected writer source was `controller-display-row`, `direct-ctaf-advisory`, or `none`.

UNICOM fallback writes are identified with `writerResultSource=unicom-fallback-advisory`.
