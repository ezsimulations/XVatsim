# XVatsim Next Session Handoff

Updated: 2026-08-15

This is the current no-chat startup handoff.

## Repository

```text
C:\Users\DARRON\OneDrive\Documents\XVatsim
```

Start with:

```powershell
git status --short --branch
git log -5 --oneline --decorate
```

Then read, in order:

1. `docs/BRAIN_OWNED_RUNTIME_CONTRACT.md`
2. `docs/V2_0_0_ROADMAP.md`
3. `docs/ROADMAP_BOUNDARIES.md`
4. `docs/ARCHITECTURE.md`
5. `docs/V1_2_3_PATCH_CLOSEOUT.md`

## Current Product State

- V1.2.3 is the current public freeware Windows/X-Plane 12/xPilot release.
- The plugin, menu labels, network user agents, package tooling, user guide,
  and public update manifest report V1.2.3.
- Users may download from X-Plane.org or the official GitHub Release.
- V1.2.3 is a narrow Version 1 maintenance release, not a V2 feature release.
- Only the V1.2.2 and V1.2.3 release directories and ZIPs are retained locally.
- Release commit: `e4a6269 chore: release xvatsim 1.2.3`.
- Annotated release tag: `v1.2.3`.

## V1.2.3 Fix

The live-tested failure was route SKJ914 from MMTO to KCOS. The PNG-TXO route
leg crossed KZFW, but the old route-traversal entry probe could miss the
polygon. That prevented the brain from receiving KZFW as the next route
polygon and proving the reachable FTW Center controller.

`core/src/RouteTraversal.cpp` now evaluates ordered intervals between exact
segment/polygon boundary crossings and refines entry between the last outside
fraction and an inside fraction. This restores KZFW and FTW Center relevance
without moving controller-relevance or UI ownership outside the brain.

## Release Facts

- Package: `releases/XVatsim_1.2.3_Freeware_Windows_XP12.zip`
- Size: `1663402` bytes
- Package SHA-256:
  `80B013ADB454D6F55AD359825E7E3229BD85C12A146289B4D17A15894049497C`
- Packaged plugin SHA-256:
  `28896800BAD64A5C25933F828D0D10FD63E0ED8C1AF5471760A4F1E599CFE23C`
- GitHub Release:
  `https://github.com/ezsimulations/XVatsim/releases/tag/v1.2.3`
- X-Plane.org:
  `https://forums.x-plane.org/files/file/100224-xvatsim_100_freeware_windows_xp12zip/`

## Verification

- Release `XVatsimRegressionHarness` and `XVatsimPlugin` builds passed.
- The compiled UI reports `V1.2.3`.
- Eight focused route, relevance, and update-notification scenarios passed.
- Full saved regression passed: `451 / 451`.
- The package smoke extraction contained the nine required customer files and
  no forbidden debug/test artifacts.
- The packaged plugin hash matches the validated Release build.
- The seven-page V1.2.3 user guide was rendered and visually inspected.

## Architecture Contract

`Brain decides. Modules produce facts. UI displays brain-approved facts.`

The V1.2.3 route fix stays in core geometry. The update-notice wording is a
brain-approved display fact rendered by the UI. Every future edit requires a
fresh approved Contract Gate as defined in
`docs/BRAIN_OWNED_RUNTIME_CONTRACT.md`.

## Next Session Objective: Begin V2 Preparation

The next session begins V2 planning. Do not reopen V1.2.3 release work unless a
new defect is reported. Do not make V2 source edits during the initial audit.

Start by confirming which V2 workstream Darron wants first:

- dedicated VFR workflow
- Mac support
- Linux support

After the workstream is selected:

1. Audit the relevant current architecture and source boundaries.
2. Identify authoritative inputs, brain-owned decisions, module-produced facts,
   UI outputs, fail-closed behavior, and platform/package constraints.
3. Propose a narrow V2 milestone with explicit non-goals.
4. Present a fresh Contract Gate naming exact files, ownership boundaries,
   replaced/deleted behavior, and focused regression proof.
5. Wait for explicit approval before any code edit.

VFR must define its own flight-state and controller-authority contract rather
than weakening IFR assumptions. Mac/Linux work must begin as a platform audit
covering SDK binaries, dependencies, output layout, signing/notarization, audio,
packaging, and smoke testing; it must not change live controller decisions just
to make another platform compile.

## V1 Scope And Guardrails

- Windows and X-Plane 12 only.
- xPilot is required.
- IFR flight-plan workflow only.
- No dedicated VFR workflow, Mac/Linux port, SimBrief import, Navigraph AIRAC
  import, or private-message/PDC/AUTO_ATC cards.
- Fail closed when route, source, controller, or session evidence is stale or
  unavailable.
- Preserve brain-owned display order, controller relevance, standby assist,
  COM writing, dedupe, completion identity, phase reuse, and overlay cap.
