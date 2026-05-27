# Store-Ready Repo Hygiene Audit

Date: 2026-05-27

Purpose:

This audit keeps the X-Plane.org Store process in the right order:

1. Make the repo clean and intentional.
2. Confirm the plugin source is store-ready.
3. Rebuild the customer package from that clean source state.
4. Prepare the store email and submission materials.

The 2026-05-27 generated store kit was a package-pipeline proof artifact, not a
submission artifact. It was removed from the active release path after review.

## Current Decision

- Do not submit the generated `2026-05-27` store kit.
- Do not treat a generated package as release proof while the repo remains dirty.
- Do not touch live runtime behavior during this hygiene slice.
- Regenerate the final package only after this audit classifies the source tree
  as store-ready.

## Package Proof From The Removed Kit

Before removal, the generated store-upload zip was inspected. It contained only:

- `XVatsim.xpl`
- `ui_transition.mp3`
- `authority_source_registry.json`
- `README.txt`
- `CHANGELOG.txt`
- `QUICK_START.txt`

It did not contain diagnostic logs, `.pdb` files, smoke-test extracts, source
files, screenshots, or internal notes. That proves the package builder concept
can produce a clean customer payload, but it does not prove the repo is ready.

## Repo-State Classification

Current dirty source state includes intentional runtime and harness work from
the battle-tested build:

- brain-owned display relation and standby-state fixes
- terminal-authority and airport-frequency source modules
- 300 NM radio-board candidate envelope
- controller relevance cache invalidation by radio tuning identity
- regression-harness extensions and saved scenarios
- live battle-test documentation and closeout notes

These should be reviewed as release source, not deleted as generated dirt.

### Intentional Release Source Checklist

Review and keep these groups as release source unless a later focused audit
finds a real defect:

- Brain-owned runtime and display:
  - `brain/include/XVatsim/brain/BrainDisplayIntent.h`
  - `brain/include/XVatsim/brain/BrainOwnedRuntime.h`
  - `brain/include/XVatsim/brain/BrainOwnedWorkerTypes.h`
  - `brain/include/XVatsim/brain/BrainTypes.h`
  - `brain/src/BrainControllerRelevanceWorker.cpp`
  - `brain/src/BrainDisplayIntent.cpp`
  - `brain/src/BrainOrchestrator.cpp`
  - `brain/src/BrainOwnedRuntime.cpp`
  - `brain/src/BrainRadioRangeWorker.cpp`
  - `brain/src/RadioReachableSnapshot.cpp`
- Clean fact modules:
  - `modules/terminal_authority/`
  - `modules/airport_frequency_catalog/`
  - `modules/CMakeLists.txt`
- Radio-board envelope worker change:
  - `modules/transceiver_resolver/src/TransceiverResolver.cpp`
- Plugin shell wiring for the brain-owned worker facts:
  - `plugin/CMakeLists.txt`
  - `plugin/src/XVatsimPlugin.cpp`
- Regression proof:
  - `tools/regression_harness/CMakeLists.txt`
  - `tools/regression_harness/src/main.cpp`
  - new scenarios under `tools/regression_harness/scenarios/`
- Release/store tooling:
  - `tools/release_gate/New-StoreSubmissionPackage.ps1`
  - `tools/release_gate/Run-FinalReleaseValidation.ps1`
  - `tools/release_gate/README.md`
- Contracts and handoff docs:
  - `docs/BRAIN_OWNED_RUNTIME_AUDIT.md`
  - `docs/BRAIN_OWNED_RUNTIME_CONTRACT.md`
  - `docs/LIVE_BATTLE_TEST_RELEASE_GATE_CONTRACT.md`
  - `docs/NEXT_SESSION_HANDOFF.md`
  - `docs/NEXT_SESSION_START_PROMPT.txt`
  - `docs/FAA_NASR_FREQUENCY_DECISION_HARDENING_RECAP.md`
  - `docs/STORE_READY_REPO_HYGIENE_AUDIT.md`

### Why These Are Not Generated Dirt

- The installed build hash and `build\dist` hash both verify as
  `81CC5DD85D579A89257670F51A0F477EAE825F5D78A9F360FBF2AE1979EEF96A`.
- The modified source set matches the live-tested features described in the
  Battle Test #1 through #5 closeouts.
- The new modules are clean Engineer 3 fact-source modules, not architecture
  dirt.
- The new regression scenarios are saved proof for the fixes that made the
  live runtime pass.
- The release tooling is process source. It should stay separate from generated
  store packages.

Generated or local artifacts should remain ignored or regenerated:

- `build/`
- package smoke extracts
- generated release zips
- customer package payload folders under release kits
- `.pdb`, `.lib`, `.exp`, `.tmp`, `.log`, and `.xpl` outputs
- archived historical packages under
  `releases/Archived_Previous_Test_Packages_do_not_ship/`

Evidence artifacts are not customer package contents:

- `battle_tests/*.log`
- X-Plane runtime logs
- regression replay logs
- final release-gate logs

### Keep Ignored / Regenerate Checklist

Do not commit these as release source:

- `build/`
- `.vs/`
- generated `.sln` / `.vcxproj*`
- generated package smoke folders under `build\package_smoke`
- generated final release scenario logs under `build\logs`
- generated store kits under
  `releases/XVatsim_XPlaneOrg_Store_Submission_Kit_*/`
- customer payload folders under release kits:
  `releases/**/Resources/plugins/XVatsim/win_x64/`
- release zip files
- X-Plane plugin binaries
- `.pdb`, `.obj`, `.ilk`, `.exp`, `.lib`, `.ipch`, `.tlog`, `.tmp`, `.log`
- `desktop.ini`
- `tools/user_route_scenarios/generated_live/`

Exception:

- `SDK/Libraries/Win/*.lib` is intentionally unignored because it is SDK input,
  not repo-generated build output.

## Hygiene Rules Going Forward

- Final store kits are generated artifacts and are ignored by default.
- The active release package must be rebuilt from `build\dist` after the source
  tree is intentionally reviewed.
- Screenshots and store copy can be prepared, but they are submission materials,
  not runtime proof.
- No runtime cleanup is allowed in the same slice as packaging cleanup unless a
  new Contract Gate explicitly covers that runtime boundary.
- Any edit to authority, workflow, display, performance cadence, or plugin shell
  behavior resets the live gate unless explicitly docs/logging only.

## Next Audit Steps

1. Review the dirty source files and untracked modules/scenarios as one release
   source set.
2. Confirm that each file belongs to the installed battle-tested runtime hash or
   to harness proof for that runtime.
3. Keep release tooling changes only if they remain generated-artifact safe.
4. Run Release build and full harness after source review.
5. Rebuild the final store kit only after the repo state is clean enough to
   describe without caveats.

## Current Audit Verdict

The repo is not yet clean enough for final packaging, but the remaining visible
dirty state is no longer mixed with an active generated store package. The next
work should review and preserve the intentional release source set, then produce
a final clean status story before regenerating the X-Plane.org Store package.
