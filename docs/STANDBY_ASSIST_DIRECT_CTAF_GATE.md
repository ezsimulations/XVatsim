# Direct CTAF Standby Assist Gate

Direct CTAF standby assist is a narrow opt-in extension of standby assist. It allows a direct, resolved CTAF advisory candidate to become the COM1 standby target only when the normal standby assist gate is enabled and the direct CTAF gate is also enabled.

## Default State

The direct CTAF gate defaults off.

If the setting is missing from the settings file, `directCtafStandbyAssistEnabled=false` and diagnostics report `directCtafGateSource=default`.

Legacy standby assist alone does not enable direct CTAF writes. `standby_assist=true` only keeps direct CTAF in dry-run/no-write diagnostics unless the direct CTAF gate is also enabled.

## Settings Keys

Either key enables the direct CTAF gate when set to `true`:

- `direct_ctaf_standby_assist=true`
- `standby_assist_direct_ctaf=true`

Normal standby assist remains controlled separately by:

- `standby_assist=true`

Both gates must be on before direct CTAF can become a live COM1 standby target.

## Safety Boundaries

Direct CTAF standby assist only applies to direct resolved CTAF advisory candidates.

It does not apply to:

- UNICOM fallback.
- Pending CTAF lookup.
- Failed CTAF lookup.
- Empty CTAF frequency.
- Guard or invalid frequencies.

Direct CTAF never displaces an existing controller standby target. If a controller target is selected, the controller target wins and the CTAF candidate remains blocked in diagnostics.

COM writer behavior is unchanged. Direct CTAF uses the same existing brain-approved COM1 standby write path as controller standby assist, and only after all brain-owned safety gates pass.

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
