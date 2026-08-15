# XVatsim Next Session Handoff

Updated: 2026-08-15

This file is the current no-chat startup handoff. Read it before changing or
packaging the repository.

## Repository

```text
C:\Users\DARRON\OneDrive\Documents\XVatsim
```

Start with:

```powershell
git status --short --branch
git log -5 --oneline --decorate
```

## Current Product State

- V1.2.2 is the current public freeware Windows/X-Plane 12/xPilot release.
- The repository source and documentation are prepared for V1.2.3.
- V1.2.3 is a narrow Version 1 maintenance release, not a V2 feature release.
- The V1.2.3 archive has not been generated yet.
- The public update manifest still advertises V1.2.2 by design. Do not publish
  V1.2.3 in `docs/xvatsim_update.json` until the archive, size, package hash,
  and plugin hash have been generated and verified.

## V1.2.3 Fix

The live-tested failure was route SKJ914 from MMTO to KCOS. The PNG-TXO route
leg crossed KZFW, but the route traversal entry probe could miss the polygon,
which prevented the brain from receiving KZFW as the next polygon and proving
the reachable FTW Center controller.

The fix is committed as:

```text
664b640 fix: detect route polygon entry at exact crossings
```

Implementation:

- `core/src/RouteTraversal.cpp` now evaluates ordered intervals between exact
  segment/polygon boundary crossings.
- When an interval is inside the feature, entry is refined between the last
  outside fraction and an inside fraction.
- The old fixed `fraction +/- 1e-6` boundary probe is gone.

Regression coverage:

- `route_traversal_kzfw_png_txo_exact_crossing.scn`
- `route_traversal_skj914_mmto_kcos_includes_kzfw.scn`
- `brain_controller_relevance_accepts_ftw_when_kzfw_next.scn`
- Existing narrow-crossing and anti-meridian scenarios remain guardrails.

## V1.2.3 Version State

The following active metadata is 1.2.3:

- root CMake project version
- installed plugin version and menu labels
- network user-agent strings
- regression harness default installed version
- freeware package-builder default version and changelog
- user guide source and PDF

The following remains 1.2.2 until package publication:

- `docs/xvatsim_update.json`
- the root README's current-public-release package and hashes
- the existing `releases/XVatsim_1.2.2_Freeware_Windows_XP12.zip`

Historical closeout documents and explicitly versioned update-notification test
fixtures retain the release numbers they document. They are not stale product
metadata.

## Verified Preparation State

Verification completed on 2026-08-15:

- Release `XVatsimRegressionHarness` build passed.
- Release `XVatsimPlugin` build passed.
- The compiled harness reports `OverlayVersionText: V1.2.3`.
- Seven focused route traversal, controller relevance, and update scenarios
  passed.
- Full saved regression passed: `451 / 451`.
- The regenerated V1.2.3 user-guide PDF has seven pages; every page was
  rendered and visually inspected.

Visual Studio 18/MSVC 14.51 requires cpprestsdk's legacy coroutine suppression
when regenerating the CMake cache. The standard `build` directory is already
configured with:

```text
/D_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS /EHsc
```

The root README records the corresponding fresh-configuration command.

## Architecture Contract

The governing rule remains:

`Brain decides. Modules produce facts. UI displays brain-approved facts.`

The V1.2.3 fix stays inside core route geometry. It does not move controller
relevance into the route worker or plugin shell. The plugin changes for V1.2.3
are version-label changes only.

Every future edit still requires a fresh approved Contract Gate as defined in
`docs/BRAIN_OWNED_RUNTIME_CONTRACT.md`.

## Packaging Is The Next Separate Step

Wait for the user's packaging instructions before creating the V1.2.3 archive.
The intended sequence is:

1. Confirm the committed source tree is clean.
2. Build `XVatsimRegressionHarness` and `XVatsimPlugin` in Release mode.
3. Run all saved regression scenarios.
4. Run the freeware builder under `tools/release_gate`.
5. Inspect the generated file set and smoke extraction.
6. Compute the archive and packaged-plugin SHA-256 values and archive size.
7. Update `README.md`, `docs/MILESTONE_STATUS.md`, and
   `docs/xvatsim_update.json` with the actual V1.2.3 artifact facts.
8. Commit the release closeout and only then publish/push the manifest when the
   user authorizes it.

Do not reuse V1.2.2 hashes or claim V1.2.3 is public before these steps pass.

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
- Do not broad-clean or refactor during release preparation.

## Current Public Release

V1.2.2 package:

```text
releases/XVatsim_1.2.2_Freeware_Windows_XP12.zip
```

Package SHA-256:

```text
A329CAF1AE589A828421A78DE7CE3B15645492B04CDD983A122A4E5CAC01834F
```

Packaged runtime SHA-256:

```text
95DDA79E9C6948A06DE2D3D9920E72D677D0C7E819386656EEA78E32A5E7D06E
```
