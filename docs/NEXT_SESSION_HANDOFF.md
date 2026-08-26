# XVatsim Next Session Handoff

Updated: 2026-08-26

This is the current no-chat startup handoff.

## Repository

```text
C:\Users\DARRON\OneDrive\Documents\XVatsim-V2
```

Start with:

```powershell
git status --short --branch
git log -5 --oneline --decorate
```

Then read, in order:

1. `docs/V2_0_0_ROADMAP.md`
2. `docs/V2_EXECUTION_PROTOCOL.md`
3. `docs/V2_STEP_01_ENGINEERING_BRIEF.md`
4. `docs/BRAIN_OWNED_RUNTIME_CONTRACT.md`
5. `docs/ARCHITECTURE.md`
6. `docs/V1_2_3_PATCH_CLOSEOUT.md`

## Current Product State

- V1.2.3 is the closed public freeware Windows/X-Plane 12/xPilot baseline.
- Release commit: `e4a6269 chore: release xvatsim 1.2.3`.
- Annotated release tag: `v1.2.3`.
- Release plugin/harness builds, eight focused scenarios, all `451 / 451`
  saved scenarios, package smoke validation, and user-guide review passed.
- V2 work occurs on `v2-development` and must not reopen V1.2.3 unless a new
  Version 1 defect is reported.

## Locked V2.0 Scope

- Windows x64, X-Plane 12, and xPilot only
- explicit dedicated VFR mode
- METAR ORB and expandable information drawer
- VATSIM ATIS ORB and expandable information drawer
- approved PDC/private-message ORB and expandable information drawer
- unchanged main-card design with a three-ORB accessory rail
- strict performance, visual, regression, live-test, and package proof

Mac and Linux are deferred because native validation resources are not
available. They are not V2.0 acceptance requirements.

## Architecture Contract

`Brain decides. Modules produce facts. UI displays brain-approved facts.`

No implementation may add feature-specific authority, relevance, targeting,
display, fallback, or cadence ownership to the plugin shell.

## Execution Rule

One step at a time:

1. issue a bounded brief
2. inspect and present a Contract Gate
3. obtain Darron's explicit approval
4. implement only the approved scope
5. prove focused, full, visual, performance, and live behavior as applicable
6. commit the clean slice
7. obtain director/user acceptance before starting another task

## Current Objective

V2 Step 1 is ready for Codex Contract Gate:

`docs/V2_STEP_01_ENGINEERING_BRIEF.md`

The objective is a repeatable Windows V2 proof-baseline entry point. It must
build the Release plugin and harness, run all saved scenarios, record counts,
time, and hashes, and produce a reviewable receipt without changing live
plugin behavior.

Codex must inspect and present the exact Step 1 Contract Gate before editing.
