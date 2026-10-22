# CTAF/UNICOM Standby Assist Gate

> **Current scope:** This compatibility gate continues to control direct
> CTAF/UNICOM promotion during **Departure**. The approved
> `STANDBY-ASSIST-REGIONAL-SEQUENCE-01` contract supersedes it for Arrival and
> Enroute. In those stages, the normal Standby Assist switch is the only user
> enable required for the Brain-owned regional sequence.

The CTAF/UNICOM standby assist gate is a narrow opt-in extension of Departure
standby assist. It allows a resolved Departure CTAF advisory candidate or a
resolved Departure `NO CTAF / UNICOM` fallback advisory candidate to become
the COM1 standby target only when the normal standby assist gate is enabled
and the direct CTAF gate is also enabled.

## Default State

The CTAF/UNICOM advisory gate defaults off.

If the setting is missing from the settings file, `directCtafStandbyAssistEnabled=false` and diagnostics report `directCtafGateSource=default`.

For Departure, Standby Assist alone does not enable CTAF/UNICOM advisory
writes. `standby_assist=true` keeps those advisory candidates in
dry-run/no-write diagnostics unless the direct CTAF gate is also enabled.

For Arrival and Enroute, see
`docs/STANDBY_ASSIST_REGIONAL_SEQUENCE_CONTRACT_GATE.md`. The direct CTAF gate
does not block the approved Arrival CTAF/UNICOM sequence or the Enroute UNICOM
fallback.

## Settings Keys

Either compatibility key enables the CTAF/UNICOM advisory gate when set to `true`:

- `direct_ctaf_standby_assist=true`
- `standby_assist_direct_ctaf=true`

Normal standby assist remains controlled separately by:

- `standby_assist=true`

Both gates must be on before a Departure CTAF or Departure
`NO CTAF / UNICOM` advisory can become a live COM1 standby target.

## Safety Boundaries

This compatibility gate applies only to resolved Departure CTAF advisory
candidates and resolved Departure `NO CTAF / UNICOM` fallback advisory
candidates.

It does not apply to:

- Pending CTAF lookup.
- Failed CTAF lookup.
- Empty CTAF frequency.
- Guard or invalid frequencies.

In Departure, CTAF/UNICOM advisory standby never displaces an existing
controller standby target. If a controller target is selected, the controller
target wins and the advisory candidate remains blocked in diagnostics.

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
