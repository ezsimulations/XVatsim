# Next Session Handoff

Date noted: 2026-06-08

## Current Product State - V1.0.2 Freeware Patch Release

XVatsim V1.0.2 is the current freeware Windows/X-Plane 12/xPilot release.

Active V1.0.2 freeware package:

- `releases\XVatsim_1.0.2_Freeware_Windows_XP12.zip`
- Package SHA-256:
  `C4F4DD6C7AF60A96DC0840D73CBD8AFEB9602FE8FCAC6B8FA5BFB0E136324ADB`
- Packaged plugin/runtime SHA-256:
  `037430F5BB2BFF346AB8E8621CF9CE30D5E78625E1FD518CB18A09382F4D9F72`

Installed simulator plugin:

- `C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl`
- Installed SHA-256:
  `037430F5BB2BFF346AB8E8621CF9CE30D5E78625E1FD518CB18A09382F4D9F72`
- Previous installed build backup:
  `C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl.bak-20260603-102134-7AF0EDE64524`

The X-Plane.org Store submission path is no longer active for Version 1 because
the store requested a Mac version. Version 1 is a freeware Windows release.

V1.0.2 patch summary:

- Fixed missed airway route sector resolution by preferring validated expanded
  FMS route geometry before raw airway expansion.
- Added unresolved airway diagnostics so failed airway expansion cannot collapse
  into an exact direct route silently.
- Added KSFO-KMCI Q126/SLC Center and unresolved-airway regression coverage.
- Added bounded cached-transceiver holdover so route-relevant enroute
  controllers stay visible during short AFV radio-range refresh gaps.
- Added KSFO-CYVR/SEA Center stale-refresh regression coverage.
- Moved final frequency display order, `Active` row ownership, and Standby
  Assist target selection under brain ownership.
- Added COM1-only assist pointer behavior so COM2 cannot move the standby
  assist target.
- Added notify-only plugin update checks with 24-hour automatic cadence,
  manual `Check for Updates`, and public manifest parsing.
- Added `docs\xvatsim_update.json` for plugin update notification.

Freeware package validation:

- Fresh RelWithDebInfo plugin and harness build passed.
- Focused contract diagnostics passed: `6 / 6`.
- Full regression harness passed: `262 / 262`.
- Freeware zip smoke extraction passed.
- Package file set verified.
- Packaged plugin hash matched the verified Release runtime hash.
- Package text scan found no store-submission, serial-key, or proof-of-purchase
  wording.
- Package scan found no `.pdb`, `.lib`, `.exp`, `.log`, temp, debug, or
  store-submission files.
- Packaged plugin hash matched the installed simulator plugin hash.
- Darron reported the live simulator test passed before commit/package
  closeout.

User guide:

- `docs\user_guide\XVatsim_User_Guide.md`
- `docs\user_guide\XVatsim_User_Guide.pdf`
- `docs\user_guide\assets\*.jpg`

Diagnostic logging remains intentional for customer bug reports, but generated
logs are not shipped in the package.

Next product work:

- Start XVatsim V2.0.0.
- Initial V2 targets are dedicated VFR implementation, Mac support, and Linux
  support.
- Before V2 source changes, produce a new Contract Gate and wait for
  `Approved to edit`.

Closeout docs:

- `docs\V1_0_1_PATCH_CLOSEOUT.md`
- `docs\V1_0_2_PATCH_CLOSEOUT.md`
- `docs\V1_0_0_FREEWARE_CLOSEOUT.md`
- `docs\V2_0_0_ROADMAP.md`

## Current Verified Runtime State - 2026-05-27 Battle Test #5

Installed X-Plane plugin:

- `C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl`
- Installed SHA256:
  `81CC5DD85D579A89257670F51A0F477EAE825F5D78A9F360FBF2AE1979EEF96A`
- Installed file timestamp:
  `2026-05-22 21:43:18` local

Live battle-test status for this installed hash:

- Battle Test #1 passed: UPS325 KSDF -> MMMX.
- Battle Test #2 passed: UA1224 / UAL1224 KLAX -> KLAS.
- Battle Test #3 passed: UAL1045 KSFO -> KGEG.
- Battle Test #4 passed: UAL1005 KGEG -> KDEN.
- Battle Test #5 passed: UPS293 EGSS -> KSDF.
- Active live battle-test streak is `5`.
- Milestone 1 / Beta Streamer Candidate live gate is complete for this
  installed hash.
- Next work is the deliberate brain-owned runtime audit and market-preparation
  process with the contract open.
- Store submission is paused until the repo hygiene audit classifies the
  current dirty source state as release-ready.
- A generated 2026-05-27 store kit proved the package-builder concept, but it
  was removed from the active release path because the repo itself still needs
  cleanup before any final package is submitted.
- Active store-readiness audit:
  `docs/STORE_READY_REPO_HYGIENE_AUDIT.md`.
- Current audit target:
  classify remaining dirty state into intentional release source versus
  ignored/regenerated artifacts before rebuilding any final store package.
- Current classification:
  brain runtime changes, new fact modules, plugin shell worker wiring, harness
  scenarios, contracts, and release tooling are intentional release-source
  candidates; `build/`, smoke extracts, generated store kits/zips, package
  payload folders, `.pdb`/`.log` outputs, archived packages, and battle-test
  logs remain ignored or regenerated evidence/artifacts.

Battle Test #5 proof summary:

- VATSIM flight plan loaded for UPS293 EGSS -> KSDF.
- At EGSS, XVatsim correctly displayed no airport CTAF/UNICOM 122.800 fallback
  as appropriate for the departure context.
- Initial route diagnostic resolved exactly:
  `EGSS->KSDF rawRoute="UTAVA Q75 BUZAD T420 TNT UL28 PENIL M144 BAGSO DCT CON DCT DEVOL DCT DOGAL/M084F330 DCT 54N020W 54N030W 54N040W 54N050W DCT NEEKO/N0482F340 DCT ANCER DCT YUL/N0477F360 DCT SSENA DCT ART DCT JHW DCT UKATS DLAMP8"` and
  `ROUTE 36 pts 3/13 sectors exact authority-gaps 4`.
- After an overnight simulator continuation with xPilot/VATSIM disconnected,
  reconnect recovery preserved the current flight context and accepted the
  fresh matched plan:
  `Automatic current-flight recovery accepted reason=recovery-enroute-airborne stage=ENROUTE preserved=1 plan=1 route=EGSS->KSDF`.
- The manual reset/current-flight menu function was used and accepted:
  `Manual current-flight recovery accepted reason=recovery-enroute-airborne stage=ENROUTE preserved=1 plan=1 route=EGSS->KSDF`.
- Recovered route diagnostic resolved exactly:
  `ROUTE 8 pts 4/3 sectors exact authority-gaps 1`.
- Arrival woke at the 200 NM arrival gate and displayed the current KSDF CTAF
  frequency.
- Arrival diagnostics showed repeated `ARR` summaries with
  `reason=arrival-distance`, `wakeReason=arrival-board`, `routeResolved=1`,
  and `finalStations=1`, then destination-ground summaries with the same final
  station count.
- Reachable irrelevant centers were processed and hidden with reasons:
  NY_CTR 125.325 and TOR_CTR 125.775 rejected as
  `center-not-route-polygon-match` against KZID.
- Logs showed no heavy fallback, clean brain-owned cache reuse, light captured
  diagnostics (`max totalMs=0`, `max totalUs=838`), no Version 2 display
  states, and clean plugin shutdown.
- Battle-test artifact:
  `battle_tests/2026-05-27_UPS293_EGSS_KSDF_PASS.log`.

Battle Test #4 proof summary:

- VATSIM flight plan loaded for KGEG -> KDEN.
- Route diagnostic resolved exactly:
  `KGEG->KDEN rawRoute="BLUNT J153 REO TCH J154 OCS RAMMS8"` and
  `ROUTE 9 pts 1/2 sectors exact`.
- KGEG departure correctly identified CTAF.
- DEN_17_CTR 127.650 was accepted/displayed as NEXT_POLYGON while the aircraft
  was still in KZLC/SLC-region airspace, with
  `center-next-polygon-match:pattern:DEN_*_CTR`; pilot observation confirmed the
  row rendered orange.
- Standby assist loaded the DEN Center frequency and labeled it `Standby`.
- DEN Center went offline before entry into DEN/KZDV airspace, and the UI
  updated/removed the row correctly.
- Arrival woke at the 200 NM arrival gate and displayed CTAF for KDEN arrival.
- Irrelevant/reachable candidates were rejected/hidden with reasons, including
  SLC_K_APP/SLC_K1_APP and BZN_APP as arrival terminal-owner mismatches against
  D01_APP+DEN_APP, and SLC_44_CTR as center-not-route-polygon-match.
- Logs showed no heavy fallback, exact route resolution after startup/pending,
  light steady-state summaries, no Version 2 display states, and clean plugin
  load/enable/disable/stop.
- Battle-test artifact:
  `battle_tests/2026-05-26_UAL1005_KGEG_KDEN_PASS.log`.

Battle Test #3 proof summary:

- VATSIM flight plan loaded for KSFO -> KGEG.
- Route diagnostic resolved exactly:
  `KSFO->KGEG rawRoute="TRUKN2 DEDHD KNZIE/N0451F350 BREWW KS12I GEG"` and
  `ROUTE 7 pts 1/1 sectors exact`.
- SFO_TWR 120.500 was captured as a live radio-board candidate and
  accepted/displayed as CURRENT_POLYGON with `departure-airport-match`.
- SEA_16_CTR 135.450 was accepted/displayed as NEXT_POLYGON while KZOA was
  current and KZSE was next, with `center-next-polygon-match:pattern:SEA_*_CTR`.
- After the KZSE transition, SEA_16_CTR 135.450 was accepted/displayed as
  CURRENT_POLYGON with `center-current-polygon-match:pattern:SEA_*_CTR`.
- Arrival woke at the 200 NM arrival gate, the final UI snapshot carried two
  final stations, and pilot observation confirmed the expected CTAF display.
- Irrelevant/reachable arrival candidates were rejected/hidden with reasons:
  PDX local rows as phase/airport filtered and BOI/BZN/EUG/SLC APP_DEP rows as
  arrival terminal-owner mismatches against GEG_APP.
- Logs showed no heavy fallback, no old authority path takeover, no unexplained
  XVatsim errors/failures, exact route resolution, light steady-state refreshes,
  and clean plugin load/disable/stop.

Battle Test #2 proof summary:

- VATSIM flight plan loaded for KLAX -> KLAS.
- Route diagnostic resolved exactly:
  `KLAX->KLAS rawRoute="ORCKA5 MISEN RNDRZ4"` and
  `ROUTE 3 pts 1/0 sectors exact`.
- LAX_S_TWR 120.950 accepted/displayed as CURRENT_POLYGON with
  `departure-airport-match`.
- LAX_S_DEP 124.300 accepted/displayed as CURRENT_POLYGON with
  `departure-terminal-owner-match:LAX_DEP`.
- SCT_APP 128.050 accepted/displayed as CURRENT_POLYGON with
  `departure-terminal-owner-match:SCT_APP`.
- LAX Center 126.525 accepted/displayed as CURRENT_POLYGON with
  `center-current-polygon-match`.
- Irrelevant candidates were rejected/hidden with reasons, including SAN_W_APP
  as terminal-owner mismatch and OAK_62_CTR as not route-polygon matched.
- Logs showed no heavy fallback, no old authority path takeover, no unexplained
  errors/failures, exact route resolution, and clean plugin load/disable/stop.

Do not count this as live FAA/NASR source-acquisition proof unless future live
diagnostics explicitly show the departure/arrival FAA frequency cache populated.

## Read First

Before touching code, read:

- `docs/BRAIN_OWNED_RUNTIME_CONTRACT.md`

This is the active top-level architecture contract and must be treated as the
bible for runtime work.

Core rule:

`Brain decides. Modules produce facts. UI displays brain-approved facts.`

Clean module rule:

- Creating a new removable module is the preferred clean Engineer 3 path when a
  new fact source is needed.
- A module is not repo dirt just because it is new.
- Repo dirt is feature logic leaking into the plugin shell, modules talking to
  each other, modules making display/workflow decisions, or patching around bad
  code instead of replacing the wrong boundary.

Hard pre-code rule:

- Before any code edit, produce a Contract Gate and wait for Darron to say
  `Approved to edit`.
- The Contract Gate must list files, layer ownership, whether the plugin is
  touched, why the change is Version 1 work, what bad code will be deleted
  instead of patched, and the regression proof.
- If this gate is skipped, stop. Treat it as a contract violation.

No module may:

- talk directly to the UI
- call another live module
- decide workflow phase
- decide display truth
- self-trigger heavy proof
- scan broad/world data unless the brain explicitly scheduled that job
- reprocess completed work when its input hash has not changed

If any proposed change violates this, stop and realign before coding.

Version 1 hard boundary:

- Do not add magenta, unknown, unresolved, or pilot mark-visible/mark-hidden
  controller rows to the live runtime.
- That is Version 2 discussion only.
- Version 1 must display proven relevant frequencies and reject/hide unproven
  or irrelevant frequencies with logged reasons.
- Distance alone must never prove airport local authority or terminal
  authority.
- Display row color is brain relation truth: current polygon is green,
  next/arrival-prep is orange, unknown is normal/hidden. Do not add textual
  `NEXT`, `ONLINE`, or sector `ACTIVE` badges to controller frequency rows.
- The only controller frequency row text badges are `Active` when tuned in an
  active COM radio, and `Standby` when standby assist has actually loaded COM1
  standby.
- The plugin is the X-Plane shell only. Do not add feature-specific authority,
  relevance, display, or fallback scheduling to
  `plugin/src/XVatsimPlugin.cpp`.

## Immediate Next Session Objective

Milestone 1 is complete. Begin the deliberate brain-owned runtime audit and
market-preparation process with the contract open.

Do not treat Milestone 1 as permission for random refactors. Any runtime code
change still needs a Contract Gate and Darron's explicit `Approved to edit`.

## Latest Source Closeout - 2026-05-22 UPS2183 SCT_APP Display Contract

Live context:

- Callsign/route: `UPS2183` / `KONT -> KDFW`.
- VATSIM flight plan was received.
- LAX Center was correctly identified on `126.525`.
- SCT_APP was correctly identified on VATSIM virtual frequency `128.050`.
- COM1 active was tuned to `128.050`.
- Failure observed: the UI displayed `APP SCT_APP 128.050` in orange and
  remained effectively standby/next-looking even though Controller Relevance had
  accepted `SCT_APP@128.050` as `CURRENT_POLYGON`.

Root cause:

- Controller Relevance was correct. Diagnostics showed:
  `SCT_APP@128.050 decision=accepted relation=CURRENT_POLYGON matched=KZLA`.
- The final display path lost non-center accepted-completion relation facts
  when converting module board rows into final display rows.
- Standby assist and overlay formatting still carried old Engineer 1/2 display
  assumptions: `next`, `online`, and sector-active flags could affect pilot
  text or tone even though Engineer 3 relation color already communicates
  current vs outside-polygon status.
- Relevance cache reuse did not include radio tuning identity, so a radio tune
  change could leave stale tuned/standby display state.

Source changes made:

- `brain/include/XVatsim/brain/BrainDisplayIntent.h`
  - Added `BrainDisplayRelationFact`.
  - `BrainDisplayIntentInput` now carries relation facts from accepted
    completions.
- `brain/src/BrainDisplayIntent.cpp`
  - Final display rows for non-center controllers now preserve brain-approved
    `CURRENT_POLYGON`, `NEXT_POLYGON`, and `ARRIVAL_PREP` relation facts.
- `brain/src/BrainOwnedRuntime.cpp`
  - Publisher builds relation facts from accepted completions before running
    Brain Display Intent.
  - Standby assist refreshes final-row tuned state from current radios before
    selecting the next hierarchy target.
  - Standby assist no longer uses `next` as a failed standby marker.
- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`,
  `brain/include/XVatsim/brain/BrainOwnedWorkerTypes.h`, and
  `brain/src/BrainControllerRelevanceWorker.cpp`
  - Radio tuning identity is now part of controller relevance reuse state, so
    COM1/COM2 active and COM1 standby changes invalidate stale display-relevant
    reuse.
- `brain/src/BrainOrchestrator.cpp`
  - Overlay tone now comes from `displayRelation`, not standby/next/online
    flags or tuning.
  - Departure ordering treats APP/DEP before Center.
  - Controller frequency rows append only `Active` or `Standby`.
  - Removed legacy text output for `NEXT`, `ONLINE`, sector `ACTIVE`, and
    `OFFLINE` on controller frequency rows.
- `tools/regression_harness/src/main.cpp`
  - Harness can inject display relation facts.
  - Harness can apply standby assist and assert overlay body tones.
- Added regression:
  `tools/regression_harness/scenarios/brain_display_intent_departure_app_current_active_standby_advances.scn`

Important display contract:

- Green/orange is not a UI guess. It is the brain-owned final display relation.
- Dialing a frequency does not make it green.
- If a controller belongs to the current polygon, the brain sends
  `CURRENT_POLYGON`, and the UI paints it green.
- If a controller belongs to a future/outside route polygon, the brain sends
  `NEXT_POLYGON` or `ARRIVAL_PREP`, and the UI paints it orange.
- The pilot does not need a `NEXT` text label. Orange is the next/outside
  signal.
- `Active` means the frequency is tuned in an active COM radio.
- `Standby` means standby assist actually loaded the frequency into COM1
  standby.
- `ONLINE`, sector `ACTIVE`, `OFFLINE`, and textual `NEXT` are not Engineer 3
  controller-row display states.

Verification completed:

- MSBuild harness build passed:
  `build/tools/regression_harness/XVatsimRegressionHarness.vcxproj`.
- Focused SCT live-case scenario passed.
- Existing KONT terminal-owner scenario passed.
- Existing SoCal/KONT frequency-disambiguation scenario passed.
- Focused display current/next distance scenario passed.
- Focused KONT departure release scenario passed.
- Full regression harness passed: `251 / 251`.

Critical install boundary:

- These are source changes and harness verification only.
- A new Release plugin build/install was not performed in this closeout.
- Current installed X-Plane plugin hash remains:
  `10937952D56614905E144EBD9DD1C4CE203349A4AEFB88B85B16A2766016CC42`.
- Do not claim the installed live plugin contains the UPS2183/SCT_APP display
  fix until a Release plugin is built, copied into X-Plane, and the installed
  hash is verified.
- Because this change touches runtime display behavior, the active live
  battle-test streak remains `0` and the next installed runtime hash must start
  at Battle Test #1.

Engineer 3 cleanliness reminder from this fix:

- No code was intentionally added to the plugin for this bug class.
- Brain owns relation, display intent, tuned/standby display facts, and final
  UI snapshot.
- Modules remain workers.
- Modules do not talk to each other.
- UI renders the brain-approved final display snapshot.
- Future live bug fixes must preserve this direction: delete/replace old
  Engineer 1/2 display assumptions rather than layering patches on top.

## Latest Closeout Snapshot - 2026-05-22 FAA/NASR Frequency Hardening

Read this recap before discussing the next code change:

- `docs/FAA_NASR_FREQUENCY_DECISION_HARDENING_RECAP.md`

Installed X-Plane plugin:

- `C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl`
- Installed SHA256:
  `10937952D56614905E144EBD9DD1C4CE203349A4AEFB88B85B16A2766016CC42`
- Previous installed build backup:
  `C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl.bak-20260522-124450-C11962103BA2`

Verification at closeout:

- Focused SoCal KONT frequency-disambiguation scenario passed.
- Existing KSDF FAA frequency proof scenario passed.
- Existing KONT terminal-authority scenario passed.
- Full regression harness passed: `250 / 250`.
- Release plugin build passed.
- Installed XPL hash matches the repo Release build.
- Active live battle-test streak remains `0`.
- Next valid live test is Battle Test #1 for hash:
  `10937952D56614905E144EBD9DD1C4CE203349A4AEFB88B85B16A2766016CC42`.

What changed:

- Added the removable `modules/airport_frequency_catalog` worker.
- Added brain-owned airport-frequency cache/input/output types.
- The brain now can carry departure/arrival FAA/NASR `FRQ.csv` endpoint
  frequency facts into Controller Relevance.
- Controller Relevance now uses FAA endpoint frequency evidence as decision
  evidence, not only diagnostic text.
- A matching endpoint APP/DEP frequency can rescue a SimAware text miss.
- An endpoint APP/DEP frequency miss can block a broad/shared SimAware
  text-only terminal pass.
- The KONT/KLAX SoCal ambiguity is locked by regression:
  `SCT_APP 124.300` is rejected for KONT, while `SCT_1_APP 127.000` is
  accepted for KONT when KONT FAA APP/DEP facts are present.

Critical live boundary:

- The decision path, data structures, parser worker, cache behavior, and
  harness proof are in the repo and the current build is installed.
- The live FAA/NASR source-acquisition path is not wired yet.
- The next session must not claim a live flight has proven FAA/NASR frequency
  disambiguation unless diagnostics show the departure/arrival FAA frequency
  cache was populated.
- The clean next gate is to add brain-owned source acquisition/worker scheduling
  for FAA/NASR facts without putting decision logic in the plugin.

Repo cleanliness reminder:

- Plugin is shell only.
- Brain owns decisions.
- Modules are workers and only return facts.
- Modules do not talk to each other.
- New fact sources should be new removable modules when that keeps boundaries
  clean and bug tracking obvious.

## Earlier Closeout Snapshot - 2026-05-22 Terminal Authority Guardrail

Installed X-Plane plugin:

- `C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl`
- Installed SHA256:
  `C11962103BA224FA57263490319118091D1F791625BAB9B1CFE43EAA70410AB6`
- Previous installed build backup:
  `C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl.bak-20260522-000145-7E7967AAC838`

Verification at closeout:

- Release plugin build passed.
- Focused departure/arrival terminal-authority wall passed: `10 / 10`.
- Full regression harness passed: `247 / 247`.
- Active live battle-test streak remains `0`.
- Next valid live test is Battle Test #1 for hash:
  `C11962103BA224FA57263490319118091D1F791625BAB9B1CFE43EAA70410AB6`.

What changed in the final session:

- KONT -> PANC live failure root cause was confirmed: `LAX_S_DEP` collapsed to
  `LAX`, while SimAware containment at KONT included a shared SCT/LAX approach
  clue. The old collapse erased APP/DEP service and let `S LAX Departure`
  appear as valid KONT departure authority.
- Terminal authority tokens now preserve service role: examples are
  `SCT_APP`, `ONT_APP`, and `LAX_DEP`, not broad owner-only strings.
- Departure and arrival now both have cached endpoint terminal-authority facts.
  Arrival was explicitly added so the same shared-TRACON problem does not
  repeat on destination APP/DEP.
- Controller Relevance compares radio-board APP/DEP candidates against the
  cached endpoint authority fact. `SCT_APP` / `SCT_1_APP` pass for KONT;
  `LAX_S_DEP`, `SAN_W1_APP`, and `LAS_F_APP` fail for KONT.
- Backend guardrail scenarios were added for `KONT`, `KLAX`, `KSAN`, `KLAS`,
  and `PANC`, each in departure and arrival mode.
- The guardrail exposed a PANC-specific clue gap: SimAware/VATSIM terminal
  callsigns use `ANC` while the airport is `PANC`. A small terminal-authority
  airport-token normalization fix now treats four-letter `P*` ICAOs similarly
  to `K*` ICAOs for local three-letter clues.

Darron's critical correction for the next session:

- Do not turn this into a world-scale string alias table. That was the
  Engineering 1 disaster mode.
- Callsign tokens such as `ONT`, `SCT`, `PANC`, or `ANC` are only clues.
- The runtime decision must remain scoped to what is on the radio board.
- Frequency/radio-board evidence is the second required piece of the puzzle.
- Authority proof decides only among live/reachable candidates being evaluated
  for the current endpoint or route polygon.
- Do not try to solve every airport, center, TRACON, ground, and tower
  worldwide up front. Evaluate the candidates that are actually presented to
  the pilot and prove whether those frequencies belong to the current flight.

Runtime/performance boundary from this session:

- No new network polling was added.
- No new per-frame SimAware parsing was added.
- No broad world scan was added to the live loop.
- The added matrix is backend harness coverage.
- The only runtime change after the installed KONT fix was cheap string-token
  normalization inside the terminal-authority resolver.

The repo is now a single Engineer 3 live core:

- The active runtime is brain-owned.
- Modules produce facts.
- The brain decides workflow, relevance, display intent, idle/cadence, and
  fallback eligibility.
- The UI renders only the final brain-approved display snapshot.

Current caution:

- The installed build includes a new removable `modules/terminal_authority`
  worker for departure APP/DEP owner facts.
- Brain-owned runtime owns the terminal-authority cache, request key, backoff,
  accept/reject use, and relevance invalidation.
- Do not remove `modules/terminal_authority` as "cleanup." The module is the
  clean extension point. If the approach fails later, the module can be removed
  as a single replaceable unit.
- Boundary caution: do not add more feature-specific logic to
  `plugin/src/XVatsimPlugin.cpp`. Any future cleanup should preserve the module
  pattern and move orchestration toward a generic brain-owned runtime
  dispatcher so the plugin only pumps the runtime and applies X-Plane side
  effects.

Remaining repo debt is not in the ordinary live decision path:

- `plugin/src/XVatsimPlugin.cpp` is still large and can shrink later.
- Harness-only legacy departure/arrival/enroute modules still exist for
  regression coverage.
- Broad route-sector authority proof still exists only as the explicitly named
  `ResolveBrainScheduledAuthorityVerification` verifier API.

Do not continue speculative cleanup before live testing. The next work is to
tail the live logs while Darron flies, compare what the plugin displays against
what xPilot and the pilot see, classify the failing Engineer 3 block, then fix
only that block.

Live log paths:

- `C:\X-Plane 12\Resources\plugins\XVatsim\logs\xvatsim_diagnostics.log`
- `C:\X-Plane 12\Log.txt`

Live test focus:

- Any reachable frequency that belongs to the current flight must appear once
  the brain identifies it.
- Any reachable frequency that does not belong to the current flight should be
  captured by the radio board, processed by relevance, marked complete, and
  rejected with enough diagnostic detail to prove why it stayed off the UI.
- Future flights now emit `event=radio-board-candidate-diff` and
  `event=candidate-completion-trace` lines in `xvatsim_diagnostics.log` for
  exact black-box comparison against the xPilot radio board.
- Current-polygon center should display as current/green.
- Next-polygon center should render orange with remaining distance, without a
  textual `NEXT` badge.
- OAK Center / OAK_62_CTR behavior is an important first retest target because
  previous live testing exposed empty UI / distance ambiguity there.
- No ordinary UI refresh may trigger heavy authority proof.
- Unchanged board/route/phase/completions should go idle.

## Session Closeout Notes

Closeout date: 2026-05-21

Important human correction:

- Darron explicitly clarified that new removable modules are how this repo
  stays clean under Engineer 3.
- Do not describe a new module as dirty repo state.
- The clean/dirty boundary is architectural behavior: Brain owns decisions,
  modules produce facts, plugin remains shell, UI renders brain-approved facts.

Current source/build state:

- `modules/terminal_authority` is intentional clean module architecture for
  terminal owner facts.
- The late-session 300nm radio-board candidate envelope is implemented in
  Brain/transceiver code, not in plugin code.
- The 300nm cap rejects raw radio-board candidates farther than 300nm from the
  aircraft to the nearest AFV transceiver before they become board candidates.
- This specifically addresses the live observation where `MEM_221_CTR` entered
  raw capture at `325nm` because controller visual range was `600nm`.
- The 300nm cap does not prove airport authority or terminal authority; it only
  shrinks the candidate envelope. Terminal authority proof and route polygon
  proof still decide display.

Verification for the late-session 300nm build:

- Release build passed.
- Focused scenario passed:
  `radio_reachable_source_rejects_over_300nm_transceiver_candidate.scn`.
- Full regression harness passed: `238 / 238`.
- Built XPL SHA256 in `build/dist`:
  `616E135A48E61ADBC8537BDB483EC025D12012E641649DDA2BDF95B81D04E9DC`.
- After Darron requested the install, this XPL was copied into X-Plane and the
  installed hash was verified.

Installed X-Plane plugin at closeout:

- `C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl`
- Installed SHA256:
  `616E135A48E61ADBC8537BDB483EC025D12012E641649DDA2BDF95B81D04E9DC`.

Next-session warning:

- Do not attempt cleanup by deleting the terminal-authority module.
- Do not revert user/session changes without explicit approval.
- Before any source-code edit, present a Contract Gate and wait for
  `Approved to edit`.

## Most Recent Post-Flight Review

KONT terminal-authority live failure and corrective build:

- Date: 2026-05-21
- Callsign: UPS2153
- Route: KONT -> PANC
- Live context: loaded in simulator, connected to VATSIM, flight plan received.
- Correctly identified: LAX Center, SCT_APP, and KONT CTAF.
- Failure: XVatsim displayed `SAN_W1_APP 119.600`, which belongs to San Diego
  Approach authority, not Ontario departure authority.
- Important observation: distance was not trustworthy by itself. Live data
  showed correct `SCT_APP 128.050` at `0nm`, bad `SAN_W1_APP 119.600` at
  about `69nm`, and a transient Las Vegas APP candidate also reported as
  `0nm`.
- Root issue: APP/DEP relevance had a broad departure-terminal reachable path
  that could accept APP/DEP without terminal-owner proof.

Corrective behavior:

- Added a removable `modules/terminal_authority` worker that parses terminal
  authority data once, caches it, and returns airport terminal-owner fact
  tokens to Brain.
- Brain-owned runtime decides when to request/refresh/cache that worker fact.
- Controller Relevance now accepts departure APP/DEP only when the candidate
  owner matches the cached terminal authority owner for the departure airport.
- In the KONT regression, `SCT_APP` is accepted as
  `departure-terminal-owner-match`, while `SAN_W1_APP` and `LAS_F_APP` are
  rejected as owner mismatches.
- Version 1 fail-closed rule applies: if departure terminal authority is
  unavailable, APP/DEP is rejected/hidden instead of displayed as unknown.
- No Version 2 unknown/magenta/manual visible-hidden behavior is allowed.

Validation:

- Focused KONT regression passed:
  `brain_controller_relevance_departure_terminal_owner_filters_kont.scn`.
- Release build passed.
- Full regression harness passed: `237 / 237`.
- Current installed XPL SHA256:
  `2C106BBA63A742F4073715263565D048703521210A20389EC5AAFA3E2BFC87A4`.

Departure-mode corrective validation:

- Date: 2026-05-21
- Callsign: SWA3187
- Route: KELP -> KHOU
- Filed route: `NEVUE3 FST CSI REUBE SAT BELLR6`
- Battle-test record:
  `battle_tests/2026-05-21_SWA3187_KELP_KHOU_DEPARTURE_VALIDATION_PASS.log`
- Verdict: `PASS / DEPARTURE-MODE CORRECTIVE VALIDATION`
- Installed hash:
  `30FA935A1D4DA1CD1ED7054D8DD20BB4700D44058673E41EE80E2F559E32A5C6`

Observed:

- Same SWA3187 KELP -> KHOU flight plan loaded after reconnect.
- KELP CTAF displayed correctly.
- HOU Center was online at startup, delayed while route context resolved, then
  displayed on the UI in orange with remaining distance.
- HOU Center logged off and XVatsim removed it from the UI.
- xPilot then showed only DEN Center, and XVatsim correctly did not display
  DEN.

Diagnostics:

- Current post-update route-pending slice hid centers:
  Brain Display Intent logged `displayed=0`, `hidden=2`, `finalStations=1`.
- After route resolution, HOU logged as `decision=accepted`, `complete=1`,
  `display=displayed`, `relation=NEXT_POLYGON`, `matched=KZHU`,
  `entryNm=220`, `reason=center-next-polygon-match:pattern:HOU_*_CTR`.
- DEN logged as `decision=rejected`, `complete=1`, `display=hidden`,
  `relation=HIDDEN`, `matched=none`, `entryNm=310`,
  `reason=center-not-route-polygon-match`.
- When HOU logged off, radio-board diff logged
  `removedKeys=HOU_461_CTR|132.775|CTR`; final display returned to one CTAF
  row.
- Engineer 3 runtime remained clean:
  `authorityProof=0`, `noHeavyFallback=1`.

Counting:

- This proves the corrective departure UI behavior.
- It does not increment the full release-gate live streak because the team is
  intentionally staying in departure-mode validation before airborne tests.

Live failure and corrective build:

- Date: 2026-05-21
- Callsign: SWA3187
- Route: KELP -> KHOU
- Filed route: `NEVUE3 FST CSI REUBE SAT BELLR6`
- Battle-test record:
  `battle_tests/2026-05-21_SWA3187_KELP_KHOU_FAIL.log`
- Verdict: `FAIL`
- Failed installed hash:
  `3ACE28B2E521493FA39A01444ADCFF6E7451B6292426257301EC8F54E992D363`
- Corrective build hash:
  `30FA935A1D4DA1CD1ED7054D8DD20BB4700D44058673E41EE80E2F559E32A5C6`

Observed:

- Route resolved as KELP -> KHOU, current polygon KZAB, next/arrival polygon
  KZHU.
- Radio board captured HOU Center and ZAK FSS.
- Controller Relevance accepted HOU Center as `NEXT_POLYGON` for KZHU with
  distance intent, and rejected ZAK FSS as off-route.
- Before shutdown, the Radio board also captured `DEN_17_CTR@127.650`, matching
  xPilot. Candidate completion marked it `complete=1`, rejected it as hidden,
  and logged `reason=center-not-route-polygon-match` with no matched route
  polygon. That is correct filtering for KELP -> KHOU because the active route
  polygons are KZAB current and KZHU next/arrival.
- Final departure display dropped the HOU next-polygon center and showed only
  KELP CTAF.
- Before route polygons were ready, route-pending center candidates briefly
  leaked as displayable current-polygon rows.

Fix made:

- `brain/src/BrainDisplayIntent.cpp` now hides center rows without route
  context unless they already carry route-entry fact truth.
- Departure final display now includes accepted current-polygon and
  next-polygon route centers, so HOU Center should render orange with remaining
  distance while KELP CTAF may remain as local fallback.
- Added regression scenarios:
  `brain_display_intent_departure_next_center_over_ctaf.scn` and
  `brain_display_intent_route_pending_hides_centers.scn`.
- Focused tests passed, full harness passed `236 / 236`, and Release build
  passed.
- After X-Plane disconnect, the corrective `XVatsim.xpl` was copied into the
  live plugin folder and the installed SHA256 verified as
  `30FA935A1D4DA1CD1ED7054D8DD20BB4700D44058673E41EE80E2F559E32A5C6`.

Non-counting validation flight:

- Date: 2026-05-21
- Callsign: UPS3511
- Route: KPDX -> KONT
- Filed route: `CASCD4 JUDAH Q7 JAGWA TTE ZIGGY8`
- Battle-test record:
  `battle_tests/2026-05-21_UPS3511_KPDX_KONT_INVALID.log`
- Verdict: `INVALID / NON-COUNTING VALIDATION`

Reason:

- No true live ATC coverage was available, so this flight does not increment
  the active live battle-test streak.
- It still provided useful health evidence for Engineer 3.

Observed and confirmed from logs:

- XVatsim received and resolved the VATSIM flight plan.
- Route resolved cleanly as KPDX -> KONT with 6 points and exact traversal.
- Route polygon context resolved as KZSE current, KZOA next, KZLA final.
- Workflow transitioned through DEP, ENR, and ARR as expected.
- The UI showed KPDX CTAF on the ground, slept in empty ENR, and woke at the
  arrival-distance gate to show KONT CTAF.
- Arrival wake occurred at progressNm=553.1 on a 754nm route, matching the
  intended 200nm arrival rule.
- During connected flight, radio-board candidate lines were present and
  relevance rejected all candidate completions:
  `radioCandidateLines=191`, `rejectionLines=191`, `acceptedLines=0`,
  `displayedCandidateLines=0`.
- No connected-flight heavy authority proof fired:
  `authorityProofEvents=0`, `authorityProofRuntimeNonzero=0`,
  `runtimeNoHeavyFallbackFalse=0`.
- Slow refreshes were isolated and explainable:
  startup 170ms, route build 929ms, enroute wake 95ms, arrival wake 101ms.

Important diagnostic update:

- The UPS3511 review still confirms module health only at an aggregate level;
  that old log cannot retroactively prove the exact in-flight xPilot DEN/SLC
  rows were the exact raw candidates processed and rejected by XVatsim.
- The current installed build adds a narrow diagnostics-only Engineer 3 trace
  for future flights: `event=radio-board-candidate-diff` and
  `event=candidate-completion-trace`.
- Those lines record callsign, frequency, facility group, input hash,
  workflow phase, current/next/arrival polygon, candidate diff, completion
  decision, accept/reject reason, and displayed/hidden result.
- This is black-box evidence only. It does not change workflow, relevance,
  display intent, UI wake behavior, or the old authority path.

## Current Runtime Direction

XVatsim is now on the brain-owned / radio-board runtime path.

The live runtime goal is intentionally simple:

- Build the route polygon context from the filed flight plan.
- Build a reachable-controller radio board.
- Let the brain decide which phase matters now.
- Send only phase-relevant reachable candidates to relevance.
- Let relevance return accepted/rejected facts only.
- Let the brain publisher assemble the final UI snapshot.
- Let the UI render only the brain-approved snapshot.
- If the radio board, route polygon, phase, and completion records are
  unchanged, everyone stays idle.

Engineer 1 string-only authority and Engineer 2 broad live-loop authority proof
are not primary runtime discovery models. Old code may remain compiled only if
it cannot self-trigger and is behind an explicit brain-scheduled fallback.

## Current Installed Test Build

Installed X-Plane plugin:

`C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl`

Current installed SHA256:

`10937952D56614905E144EBD9DD1C4CE203349A4AEFB88B85B16A2766016CC42`

Reason for this hash:

- Includes FAA/NASR frequency decision hardening:
  `modules/airport_frequency_catalog`, brain-owned departure/arrival frequency
  facts, and Controller Relevance using endpoint FAA frequency evidence in
  accept/reject decisions.
- Locks the KONT/KLAX SoCal ambiguity: `SCT_APP 124.300` rejects for KONT
  while `SCT_1_APP 127.000` accepts for KONT when KONT FAA APP/DEP facts are
  present.
- Focused SoCal KONT frequency-disambiguation scenario passed.
- Existing KSDF FAA frequency proof scenario passed.
- Existing KONT terminal-authority scenario passed.
- Full harness passed `250 / 250`.
- Live FAA/NASR source acquisition is still a future gate; do not claim live
  frequency disambiguation proof until diagnostics show the endpoint frequency
  cache is populated.
- Includes the 2026-05-22 terminal-authority tightening:
  service-aware APP/DEP tokens, departure and arrival cached endpoint
  terminal-authority facts, KONT rejection of `LAX_S_DEP`, and PANC/ANC
  terminal clue normalization.
- Includes the backend departure/arrival guardrail matrix for `KONT`, `KLAX`,
  `KSAN`, `KLAS`, and `PANC`.
- Focused guardrail passed `10 / 10`; full harness passed `247 / 247`.
- This is not a string-only authority model. Callsign text is a clue only;
  runtime decisions remain scoped to radio-board candidates, frequency
  evidence, and endpoint/route authority proof.
- Approved 300nm radio-board candidate envelope.
- Brain owns the max candidate distance policy.
- Transceiver resolver applies the Brain-owned cap as a worker constraint.
- Brain radio worker defensively filters any injected/cached candidate over the
  cap before board construction.
- `MEM_221_CTR`-style candidates at `325nm` are rejected before relevance.
- Full harness passed `238 / 238`.
- Corrective KONT terminal-authority build for UPS2153 KONT -> PANC.
- Departure APP/DEP now requires terminal-owner proof from the
  `terminal_authority` worker fact before display.
- `SAN_W1_APP` and `LAS_F_APP` style APP/DEP candidates are rejected for KONT
  when their owner does not match the departure terminal authority.
- Full harness passed `237 / 237`.
- Corrective build for SWA3187 KELP -> KHOU departure display failure.
- Departure final display includes accepted current-polygon and next-polygon
  route centers, so HOU Center should render orange with remaining distance.
- Center rows without route context stay hidden unless they already carry
  route-entry fact truth.
- Includes the diagnostics-only Engineer 3 trace for radio-board candidate
  diffs and post-publisher completion/display results.
- Brain-owned display boundary cleanup.
- Relevance no longer builds final display boards.
- Relevance no longer marks candidates as displayed.
- Brain publisher owns accepted-completion filtering, display intent, final
  board assembly, and displayed/hidden completion state.
- Engineer 3 is now the unconditional live refresh entry.
- Brain Display Intent keeps absolute route-entry distance as fact truth and
  writes remaining distance only as display annotation.
- Added `docs/BRAIN_OWNED_RUNTIME_AUDIT.md` as the active cleanup map.
- The old live refresh body, old board collectors, old radio-board runtime, and
  old scheduled authority/departure executors have been deleted from
  `plugin/src/XVatsimPlugin.cpp`.
- Plugin reset wiring now clears only brain-owned runtime state and the brain
  display publisher cache; legacy plugin-owned board/authority caches are gone.
- The plugin no longer owns or links the old departure/arrival/enroute display
  module libraries.
- `core::workflow::BuildDisplayBoard` has been removed; final display assembly
  is owned by `brain/src/BrainDisplayIntent.cpp`.
- The regression harness now checks display output from Brain Display Intent,
  not the retired core display builder.
- Brain Display Intent filters offline/no-frequency rows out of the final UI
  snapshot while preserving them as worker facts/diagnostics.
- Legacy departure/arrival/enroute collectors may produce fact rows for
  harness coverage, but only Brain Display Intent builds final display rows.
- Accepted-completion board filtering and final-display completion marking now
  live in `brain/src/BrainOwnedRuntime.cpp`, not the plugin shell.
- `RunBrainOwnedPublisher` in `brain/src/BrainOwnedRuntime.cpp` now owns
  publisher assembly: accepted-completion filtering, CTAF/UNICOM replacement,
  Brain Display Intent, phase snapshot publish state, and displayed-completion
  marking. The plugin supplies facts and diagnostics only.
- `RunBrainControllerRelevanceWorker` now lives in
  `brain/src/BrainControllerRelevanceWorker.cpp`; the plugin supplies worker
  input and diagnostics only. Brain-owned runtime owns relevance cache reuse
  and candidate completion cache updates.
- Brain-owned runtime owns radio-board reuse and commit state through
  `TryReuseBrainOwnedRadioBoard` and `CommitBrainOwnedRadioBoardRefresh`; the
  plugin still runs the transceiver module as a fact producer.
- `brain/src/BrainRoutePolygonWorker.cpp` now owns route-sector hashing and
  route-polygon worker output shaping; the plugin still runs the route-sector
  resolver as a fact producer.
- `brain/src/BrainRoutePolygonWorker.cpp` also owns route-polygon cache reuse,
  pending retry decisions, transition application, route-state commit, wake
  reason, and relevance invalidation.
- `brain/src/BrainRadioRangeWorker.cpp` now owns radio range worker output
  shaping; the plugin still runs the transceiver resolver as a fact producer.
- `brain/src/BrainOwnedRuntime.cpp` owns radio phase-gate storage and final
  published runtime snapshot commits through `RunBrainOwnedRadioPhaseGate` and
  `CommitBrainOwnedPublishedRuntime`.
- `BuildBrainOwnedControllerRelevanceInput` now shapes Controller Relevance
  worker inputs from brain-owned route/radio context.
- `BuildBrainOwnedPublisherInputFromFacts` now shapes Brain Publisher input
  from brain-owned route context plus plugin-supplied relevance and CTAF facts.
- Brain-owned runtime now turns CTAF lookup facts plus radio state into
  CTAF/UNICOM board stations, including the tuned flag; the plugin no longer
  builds CTAF/UNICOM display stations.
- Brain-owned runtime now owns standby-assist target selection and display flag
  application. The plugin only performs the X-Plane COM1 standby write side
  effect, then feeds the result back to the brain.
- `CommitBrainOwnedPublishedRuntimeFromPublisherOutput` now commits the final
  brain-approved board after standby assist has been applied, so runtime state
  and the rendered UI snapshot stay aligned.
- `DecideBrainOwnedOverlayWake` now owns overlay wake/hide/reason decisions
  from shell facts. The plugin still updates X-Plane window state, but it no
  longer decides whether the UI should wake.
- Workflow/recovery implementation now lives in
  `brain/src/BrainWorkflow.cpp`; `core/WorkflowEngine.h` is only a compatibility
  shim for existing callers.
- The unused pre-Engineer-3 plugin workflow wrapper and unused distance wrappers
  have been removed from `plugin/src/XVatsimPlugin.cpp`.
- `UpdateFlightContextFromNetworkPlan` now owns normal flight-context
  lock/refresh decisions, including departure confirmation, callsign/route
  change relock, route text refresh, and authoritative/missing airport
  coordinate refresh. The plugin only applies the brain output and performs
  reset/invalidate side effects.
- `RetargetFlightContextToNetworkPlan` now owns manual diversion/revert
  flight-context retarget decisions. The plugin only applies the returned
  context and reset/invalidate side effects.
- Brain-owned runtime now owns enroute initial display-hold state and timing.
  The plugin supplies current time/hold duration and passes the returned active
  flag into brain overlay wake.
- Brain-owned runtime now owns active-flight flight-plan sampling cadence and
  cached flight-plan snapshot state. The plugin runs `FlightPlanSampler` only
  when the brain requests a fresh sample.
- `ResolveXPilotSessionBoundary` now owns xPilot disconnect/reconnect/callsign
  boundary decisions. The plugin applies the brain-returned preserve/reset and
  recovery flags.
- `ResolveAircraftRuntimeBoundary` now owns invalid-aircraft-state and
  cold/dark boundary decisions. The plugin applies the brain-returned reset and
  latch flags, then hides/renders the X-Plane overlay.
- Removed unused plugin-side legacy authority quarantine and old radio-board
  readiness helpers.
- Brain-owned runtime now owns cruise target state, filed/current target
  command decisions, source-plan invalidation, gate dwell/reached tracking, and
  cruise header text. The plugin only handles command/status side effects.
- Brain-owned runtime now owns workflow progress latches for departure release,
  arrival wake, and airborne-since timing. The plugin only builds/commits
  workflow state through brain helpers.
- Brain-owned runtime now owns the xPilot-seen latch used by overlay
  wake/disconnect behavior.
- Brain-owned runtime now owns the active flight context storage. The plugin
  commits and clears brain-returned context through brain helpers instead of
  carrying `gFlightContext` as separate shell state.
- Brain-owned runtime now has a cache-reset helper that preserves the active
  flight context, so new-context runtime cache clears do not erase the locked
  flight.
- Brain-owned runtime now owns xPilot session boundary state,
  current-flight recovery request latches, and aircraft cold/dark/invalid-state
  boundary latches. The plugin no longer carries those as separate globals.
- Brain-owned runtime now owns the standby-assist write latch and decides when
  the plugin should perform the COM1 standby write side effect.
- Brain-owned runtime now owns diversion override source-key state and decides
  whether a manual diversion override still belongs to the current source
  VATSIM flight plan.
- Brain-owned runtime now owns preflight route-cache applied-plan state and
  decides when the plugin should clear, validate, or apply the route resolver
  cache.
- Brain-owned runtime now owns display override mode (`Auto`, forced open,
  forced sleep) and preserves it across runtime resets.
- Brain-owned runtime now owns manual query/transient status display state and
  expiry timing.
- Brain-owned runtime now owns controller-message display state, sequence
  tracking, cached recall, visibility, and clear/ack behavior.
- Plugin diagnostics no longer build or log a shadow brain scheduler from old
  diagnostic job names. The plugin includes the brain work model types it uses,
  but no longer depends on `BrainWorkScheduler.h`.
- Brain-owned runtime now owns the latest sampled aircraft, pilot identity,
  flight-plan, and network-plan fact snapshots. The plugin commits sampled
  facts to the brain and command handlers read them from brain-owned state
  instead of carrying separate `gLast*Snapshot` globals.
- Brain-owned runtime now owns pending overlay text-entry mode for manual CTAF
  and diversion prompts. The plugin opens/reads the text box, while the brain
  stores and consumes which command the submission belongs to.
- Removed unused plugin-side hash/active-transceiver helper functions left over
  from the retired board and radio refresh paths.
- `BrainOrchestrator::BuildOverlayViewModel` is now a stateless brain API, and
  the plugin no longer carries a global `gBrain` object.
- The old departure/arrival/enroute board modules are now gated behind
  `XVATSIM_BUILD_REGRESSION_HARNESS` in CMake. They still build for harness
  coverage, but plugin-only builds no longer compile them as part of the live
  module stack.
- The old departure/arrival/enroute board libraries are now named
  `XVatsimHarnessLegacyArrival`, `XVatsimHarnessLegacyDeparture`, and
  `XVatsimHarnessLegacyEnroute`. They are harness legacy coverage targets, not
  live Engineer 3 modules.
- The old departure/arrival/enroute board headers now require
  `XVATSIM_ENABLE_HARNESS_LEGACY_BOARD_MODULES`; accidental live includes fail
  at compile time.
- Plugin diagnostics state is now grouped under one shell-owned
  `PluginDiagnosticsState`, and refresh timing logs say `radioRange` instead of
  the older `activeTx` label.
- Brain-owned runtime now owns workflow phase selection through
  `ResolveBrainOwnedWorkflowSelection`, using narrow `WorkflowSignals` derived
  from radio facts instead of a provisional relevance board pass. The plugin
  supplies facts and receives only the brain-owned phase decision.
- Removed the duplicate `activeBoardSnapshot` runtime field. Brain-owned
  runtime stores the final brain-approved UI board as `finalDisplaySnapshot`,
  and the plugin/UI path names that board as final display.
- Removed stale plugin-local departure/arrival/enroute board variables from the
  Engineer 3 refresh shell. The plugin now keeps only the final display board it
  must pass through standby assist and UI rendering.
- Added explicit final-display structs:
  `FinalDisplayStationSnapshot` and `FinalDisplaySnapshot`. Brain Display
  Intent, Phase Snapshot Publisher, standby assist, overlay wake, runtime final
  display storage, and `BrainOrchestrator` now consume the display-specific
  snapshot instead of reusing `ModuleBoardSnapshot` as the final UI board.
- Brain Display Intent now keeps accepted module boards raw in its output and
  assembles `FinalDisplaySnapshot` separately. UI annotations and
  remaining-distance formatting no longer overwrite the publisher/runtime
  module board snapshots.
- Brain Display Intent now builds enroute display rows directly as
  `FinalDisplayStationSnapshot` entries, so display shaping no longer stages
  through a temporary `ModuleBoardSnapshot`.
- Raw `BoardStationSnapshot` no longer carries `next` or `standby`; those
  UI-only flags now live only on `FinalDisplayStationSnapshot`.
- Raw `BoardStationSnapshot` no longer carries `annotation`; UI text
  annotations now live only on `FinalDisplayStationSnapshot`.
- Raw `BoardStationSnapshot` no longer carries `displayRelation`; relevance
  records relation on completions and Display Intent infers final UI relation
  from fact fields.
- Raw `ModuleBoardSnapshot` no longer carries `displayStations`; board display
  ownership exists only in `FinalDisplaySnapshot`.
- Brain-owned runtime now owns network-plan identity-key construction through
  `BuildBrainOwnedPlanIdentityKey` and
  `BuildBrainOwnedNetworkPlanIdentityKey`; the plugin shell only consumes the
  brain-owned key as data for shell side effects and publisher inputs.
- Heavy route-sector authority proof is no longer exposed as ordinary
  `ResolveAuthorityRelevance`; the remaining route-sector proof entry is
  explicitly named `ResolveBrainScheduledAuthorityVerification`, requires a
  schedule reason, and is used by regression coverage only.
- Workflow selection no longer runs a provisional Controller Relevance pass.
  Brain-owned runtime builds narrow `WorkflowSignals` from radio facts, then
  `ResolveWorkflowStageFromSignals` decides the phase.

Regression harness status for this code:

- Release build passed.
- Full harness passed: `237 / 237`.
- Installed XPL hash verified:
  `2C106BBA63A742F4073715263565D048703521210A20389EC5AAFA3E2BFC87A4`.

## Live Battle Test Gate

Read before counting tests:

- `docs/LIVE_BATTLE_TEST_RELEASE_GATE_CONTRACT.md`

Active live streak for the current installed hash:

- `5`

Milestone 1 completed for hash:

`81CC5DD85D579A89257670F51A0F477EAE825F5D78A9F360FBF2AE1979EEF96A`

Milestone 1 status:

- `5 / 5` consecutive valid live battle tests passed.
- Begin the deliberate brain-owned runtime audit and market-preparation
  process with the contract open.

Rules:

- A valid pass increments the active streak.
- A confirmed fail resets the active streak to `0`.
- An invalid test does not change the streak.
- If runtime authority, workflow, display, or performance cadence code changes,
  restart the active streak unless the change is explicitly docs/logging only.

## Brain-Owned Runtime Audit

The contract cleanup has started because live testing exposed failures caused
by old/runtime display ownership mixing with Engineer 3 state.

Read:

- `docs/BRAIN_OWNED_RUNTIME_AUDIT.md`

The audit is not permission for random refactors. Cleanup must proceed in small
verified slices:

- protect Engineer 3 first
- keep broad authority proof out of ordinary UI refresh
- separate fact state from display state
- move brain-owned runtime seams out of `plugin/src/XVatsimPlugin.cpp`
- quarantine old Engineer 1/2 display and authority paths
- run focused tests, Release build, and full harness after each slice

## What To Check During Each Live Test

Confirm and log:

- XVatsim receives the VATSIM flight plan.
- Correct workflow stage: Departure, Enroute, Arrival, Ready, or sleep.
- Reachable relevant controllers are displayed.
- Reachable irrelevant controllers are filtered.
- Current-polygon centers display as current/green.
- Next-polygon centers render orange with distance intent, without textual
  `NEXT` badges.
- Arrival wakes at 200nm and only then shows destination local/TRACON authority.
- Unchanged radio board goes idle.
- No repeated heavy authority fallback.
- No visible stutters, freezes, or sustained PluginAdmin FPS hit.

If a test fails:

- Pull logs.
- Identify whether the failure was controller relevance, route polygon,
  workflow phase, display intent, UI rendering, reconnect/recovery, or
  performance.
- Fix only the responsible contract block.
- Re-run harness.
- Install the new build.
- Reset the active live streak for the new runtime hash.

## Current Caution

The repo contains older code from previous architecture attempts.

That is not a reason to start random cleanup now.

Until the 5-pass live gate is reached, protect the current runtime path:

- No broad refactor.
- No speculative cleanup.
- No adding back Engineer 2 as a live discovery loop.
- No module-to-module shortcuts.
- No UI updates from modules.

After 5 consecutive valid passes, perform the deliberate cleanup/audit and
market-preparation pass with the brain-owned runtime contract open.
