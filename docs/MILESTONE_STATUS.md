# Milestone Status

Updated: 2026-05-16

## Current Position

Milestones 1 through 5 are complete on the authoritative rebuild plan. Milestone
6 is active and is focused on the global controller authority catalog: route
center/enroute matching must be driven by authoritative catalog data and
controller-feed truth, not inferred aliases. The package zip created during
Milestone 5 remains an internal checkpoint only and must not be treated as the
store-upload package.

## Milestone 5 Cleanup Completed

- Removed dormant prototype modules from the release source tree:
  `recommendation`, `phase_classifier`, and `tune_router`.
- Removed generated build leftovers for those dormant modules.
- Rewrote architecture documentation to describe the current live decision path.
- Updated root and module documentation to emphasize fail-closed source handling.
- Reclassified the package zip as an internal Milestone 5 checkpoint.
- Updated the root CMake project version to `1.0.0`.
- Replaced old xPilot fork/full-client planning with explicit V1 roadmap boundaries.
- Removed empty bootstrap-era `client`, `shared`, and `third_party` directories.
- Moved old beta, preview, showcase, and release-candidate packages into
  `releases/Archived_Previous_Test_Packages_do_not_ship`.
- Updated live network user-agent metadata to `XVatsim/1.0.0`.
- Bounded and sanitized route diagnostic log text at the route-sector resolver
  boundary.
- Removed stale/internal bootstrap and brain wording from the plugin
  description and lifecycle logs.
- Hardened command callbacks so ignored phases pass through safely, and menu
  registration cleans up a parent menu item if submenu creation fails.
- Verified the active kit payload binary and audio hashes match the current
  validated build output.
- Regenerated the internal Milestone 5 checkpoint zip from the current package
  payload.
- Ran a clean temporary install smoke from the regenerated checkpoint zip.
- Completed final closeout scans across source, active release materials,
  command/menu lifecycle seams, status/log wording, and package contents.

## Supporting Release Gate Completed

- Added a repeatable validation script at
  `tools/release_gate/Run-Milestone6Validation.ps1`.
- The gate builds the release harness and plugin, runs every saved scenario,
  scans active source/release text for stale bootstrap-era wording, verifies the
  active customer package file set, compares build/package/installed artifact
  hashes, and performs a clean smoke expansion from the internal checkpoint zip.
- This milestone intentionally avoids runtime behavior changes while it turns
  release trust into a command that can be rerun after every future code change.
- Initial gate run passed after validating 71 saved regression scenarios, the
  active release package file set, build/package/installed artifact hashes, and
  a clean smoke expansion from the internal Milestone 5 checkpoint zip.

## Milestone 6 Authority Catalog Active

- The authoritative roadmap is recorded in `docs/REBUILD_PLAN.md`.
- Current Milestone 6 target: harden `RouteSectorResolver.cpp` and
  `EnrouteModule.cpp` so live enroute matching uses catalog-provided controller
  prefixes and VATSIM facility truth, not `_CTR` suffix assumptions alone.
- First Milestone 6 code pass completed: enroute controller eligibility now
  accepts VATSIM Center and FSS facility classes only when the route authority
  prefix matches, and rejects `_CTR` suffix-only matches when the controller feed
  facility is not enroute service.
- Added saved scenarios for FSS/oceanic-style enroute matching and `_CTR`
  suffix rejection without enroute facility truth.
- Release build passed and all 73 saved regression scenarios passed after this
  pass. The installed X-Plane plugin binary was refreshed from the validated
  build output.
- Second Milestone 6 code pass completed: blank callsign-prefix rows in the
  VATSpy FIR/UIR authority catalog no longer invent boundary or sector
  identifiers as controller prefixes.
- Route-sector resolver status now reports explicit `authority-gaps N` when
  route geometry proves a center sector but the authority catalog does not
  provide a usable controller prefix.
- Added a saved blank-prefix authority-gap scenario and extended the harness to
  assert resolver route status text. Release build passed and all 74 saved
  regression scenarios passed after this pass. The installed X-Plane plugin
  binary was refreshed from the validated build output.
- Third Milestone 6 code pass completed: airport coverage sectors now carry
  explicit center-vs-terminal source truth so Departure and Arrival APP/DEP
  matching cannot infer terminal authority from center boundary labels or
  terminal-looking center tokens.
- Added a saved scenario proving center coverage tokens cannot masquerade as
  terminal APP/DEP authority while legitimate terminal-sector authority and
  no-terminal-data airport-token fallback remain intact. Release build passed
  and all 75 saved regression scenarios passed after this pass. The installed
  X-Plane plugin binary was refreshed from the validated build output.
- Fourth Milestone 6 code pass completed: enroute authority-gap sectors with no
  explicit controller prefixes no longer create pilot-facing offline route rows.
  The gap remains visible through route resolver status and plugin route-status
  logging instead of becoming a misleading board entry.
- Added a saved scenario proving a no-prefix enroute sector does not display an
  offline row even when a same-prefix `_CTR` controller is online. Release build
  passed and all 76 saved regression scenarios passed after this pass. The
  installed X-Plane plugin binary was refreshed from the validated build output.
- Fifth Milestone 6 code pass completed: authority-gap diagnostics now include
  exact current/next sector identifiers instead of only a count, so catalog data
  gaps can be traced without pilot-facing guesswork.
- Added a saved scenario proving both current and next route authority gaps are
  reported by identifier. Release build passed and all 77 saved regression
  scenarios passed after this pass. The installed X-Plane plugin binary was
  refreshed from the validated build output and matches SHA-256
  `B729B027F1F6513690BB0556271F1EB964797B36FFB5A15E46B0BE6547709A46`.
- Sixth Milestone 6 code pass completed: center boundary GeoJSON `callsign`
  properties are no longer accepted as sector identity or authority lookup
  tokens. Controller authority must come from the VATSpy FIR/UIR catalog, not a
  boundary-side callsign-shaped property.
- Added a saved scenario proving a fake boundary `callsign=PHZH` does not create
  an `HCF` controller-prefix match for unrelated `SAFE` geometry. Release build
  passed and all 78 saved regression scenarios passed after this pass. The
  installed X-Plane plugin binary was refreshed from the validated build output
  and matches SHA-256
  `3DBA07945633CE0DC004B3FCF818AE0EBFD3875A918971FE97B63F294F5C3745`.
- Seventh Milestone 6 code pass completed: ENROUTE offline route rows no longer
  scan match tokens for center-looking `KZ..` labels. The offline row label now
  comes from the proven sector identifier only.
- Added a saved scenario proving a sector carrying a tempting `KZLA` match token
  still displays as its real `SAFE` sector row. Release build passed and all 79
  saved regression scenarios passed after this pass. The installed X-Plane
  plugin binary was refreshed from the validated build output and matches
  SHA-256
  `AD5EF8ED9B0D17F3E664521B54C844FB8AF98C14255EC7CCCF4AE8049DD03812`.
- Eighth Milestone 6 cleanup pass completed: removed dead duplicate
  route-sector authority-collapse helpers and an unused controller-prefix
  normalizer from `RouteSectorResolver.cpp`, eliminating old match-token
  authority shortcut code that was no longer on the live path.
- Release build passed and all 79 saved regression scenarios passed after this
  cleanup. The installed X-Plane plugin binary was refreshed from the validated
  build output and matches SHA-256
  `CFFC41394EBF0A3F6D0DB525ECA3D52E9B20B995053576C31FF650DD70E67F9A`.
- Ninth Milestone 6 code pass completed: Departure and Arrival APP/DEP airspace
  rows now require VATSIM Approach facility truth in addition to callsign suffix
  and terminal-sector token proof. A suffix-shaped controller can no longer
  populate terminal airspace if the feed marks it as another facility class.
- Added a saved scenario proving `SCT_APP` with Center facility truth is rejected
  by both Departure and Arrival airspace collectors while valid terminal and
  PHZH authority scenarios remain intact. Release build passed and all 80 saved
  regression scenarios passed after this pass. The installed X-Plane plugin
  binary was refreshed from the validated build output and matches SHA-256
  `9D730C36FEBD87815AC60D476DF3D8D3A3A86322DCB1BC850A5B8C8BB6E2241E`.
- Tenth Milestone 6 code pass completed: airport-local Delivery, Ground, and
  Tower rows now require matching VATSIM facility truth in addition to airport
  prefix and callsign suffix. Departure local rows require Delivery/Ground/Tower
  facility classes, and Arrival local rows require Ground/Tower facility classes.
- Extended the harness to collect and assert Arrival local rows, then added
  saved scenarios proving local suffix-only rows are rejected when facility truth
  disagrees and accepted when facility truth matches. Release build passed and
  all 82 saved regression scenarios passed after this pass. The installed
  X-Plane plugin binary was refreshed from the validated build output and
  matches SHA-256
  `8FC54D4E620F576AF11B32FC87954FFCD1B95EB0526D3A7642AF91E098DBC620`.
- Eleventh Milestone 6 code pass completed: the shared workflow engine and
  plugin display/handoff mirror now require a tuned Departure terminal station
  to be live and have a frequency before it can hold Departure on COM1. Offline
  APP/DEP rows can no longer act as workflow truth.
- Added a saved scenario proving an offline APP/DEP row tuned on COM1 does not
  hold Departure after the release window and does not leak into the display.
  Release build passed and all 83 saved regression scenarios passed after this
  pass. The installed X-Plane plugin binary was refreshed from the validated
  build output and matches SHA-256
  `7A5651D467936F0E08A4BCF49E8A6A3AC8AE7F057E9BB7274DAEFAEFA314E2CE`.
- Twelfth Milestone 6 code pass completed: `ControllerFeedSnapshot::Controllers()`
  now returns controller rows only when the feed is available and fresh. A
  malformed stale or unavailable snapshot can no longer expose controller rows
  to Departure, Arrival, ENROUTE, RX, hashing, or overlay paths through the
  shared accessor.
- Extended the stale controller-feed scenario to force attached controller rows
  and added an unavailable-feed companion scenario proving bad feed rows do not
  populate live boards. Release build passed and all 84 saved regression
  scenarios passed after this pass. The installed X-Plane plugin binary was
  refreshed from the validated build output and matches SHA-256
  `4AE4B079A4182AA5447EC890B494CF7E0D4B91832866F35591E9A5A205E35822`.
- Thirteenth Milestone 6 code pass completed: Departure and Arrival APP/DEP
  terminal matching now requires fresh airport-sector coverage. Stale terminal
  geometry can no longer prove terminal authority, and stale known terminal data
  still blocks loose airport-token fallback instead of becoming a guess.
- Added a saved stale airport-sector scenario proving stale Departure and
  Arrival terminal coverage does not populate APP/DEP rows while fresh terminal
  authority and no-sector-data fallback behavior remain intact. Release build
  passed and all 85 saved regression scenarios passed after this pass. The
  installed X-Plane plugin binary was refreshed from the validated build output
  and matches SHA-256
  `45911D956A9BD5B28F1578F1E5362BFE30AFC159B5B25A532C8856A4A49E1ED1`.
- Fourteenth Milestone 6 code pass completed: display-decision diagnostics now
  log route availability, freshness, resolution, and status even when route
  sectors are unavailable, and airport coverage logging now uses the relevant
  departure or arrival coverage snapshot instead of arrival-only status.
- Added a saved ENROUTE display-board scenario proving an offline diagnostic
  route row is not promoted into the active display board; the UI shows route
  ATC offline rather than a false controller row. Release build passed and all
  86 saved regression scenarios passed after this pass. The installed X-Plane
  plugin binary was refreshed from the validated build output and matches
  SHA-256
  `E76BEA33496DCAC1F899591466598361F7187AD71BC4612A5E9F85174F691CC3`.
- Fifteenth Milestone 6 regression pass completed: audited the boundary and
  controller-authority catalog refresh seam and confirmed live center packages
  are staged atomically. A boundary-only or catalog-only refresh must not be
  applied to route matching; only a complete boundary-plus-catalog refresh can
  advance route-sector controller prefixes.
- Added the missing route catalog-only refresh scenario proving a refreshed
  VATSpy authority catalog is not applied against old route-sector boundaries.
  Release build passed and all 87 saved regression scenarios passed after this
  pass. The installed X-Plane plugin binary was refreshed from the validated
  build output and matches SHA-256
  `E76BEA33496DCAC1F899591466598361F7187AD71BC4612A5E9F85174F691CC3`.
- Sixteenth Milestone 6 regression pass completed: audited terminal-boundary
  generation seams and confirmed terminal containment already rejects old
  airport coverage snapshots when the active terminal boundary generation
  changes.
- Added a cached airport terminal coverage scenario proving a terminal-only
  boundary refresh rebuilds cached airport coverage onto the new terminal
  generation before it can prove APP/DEP containment. Release build passed and
  all 88 saved regression scenarios passed after this pass. The installed
  X-Plane plugin binary was refreshed from the validated build output and
  matches SHA-256
  `E76BEA33496DCAC1F899591466598361F7187AD71BC4612A5E9F85174F691CC3`.
- Milestone 6 closeout completed: audited the controller-authority live path in
  `RouteSectorResolver.cpp` and `EnrouteModule.cpp` for remaining suffix,
  alias, match-token, boundary-property, or callsign shortcuts. No remaining
  center live-path shortcut was found: ENROUTE matching requires fresh resolved
  route sectors, explicit catalog-resolved controller prefixes, and VATSIM
  Center/FSS facility truth. Release build passed and all 88 saved regression
  scenarios passed. The installed X-Plane plugin binary matches SHA-256
  `E76BEA33496DCAC1F899591466598361F7187AD71BC4612A5E9F85174F691CC3`.
- First Milestone 7 code pass completed: departure release now distinguishes
  known terminal-geometry truth from unknown terminal geometry. If fresh
  terminal containment proves the aircraft has exited the departure terminal
  area, workflow ownership moves to ENROUTE immediately instead of waiting for
  the release timer. If terminal geometry is unknown, the release timer remains
  a protective guard.
- Added saved workflow scenarios proving known terminal exit releases before
  the timer and unknown terminal geometry still keeps the timer guard. Release
  build passed and all 90 saved regression scenarios passed. The installed
  X-Plane plugin binary was refreshed from the validated build output and
  matches SHA-256
  `9D7558BD3B32F883D0649F93740FD1A24C658F80A8C4476D16B40F56EB2DF845`.
- Second Milestone 7 code pass completed: departure-location confirmation now
  requires valid aircraft state before accepting current-airport or coordinate
  proximity proof. A cached/current airport string can no longer lock a new
  flight context if aircraft position validity is unavailable.
- Added saved confirmation scenarios proving valid aircraft state can confirm
  the filed departure and invalid aircraft state cannot. Release build passed
  and all 92 saved regression scenarios passed. The installed X-Plane plugin
  binary was refreshed from the validated build output and matches SHA-256
  `D61D65075F307DD01B11B55FC8A583573EA7B12E21CB91C5BF2C8B9CF766AC84`.
- Milestone 7 closeout completed: audited workflow ownership and removed the
  remaining permissive terminal-airspace fallback that could trust an inside
  flag without known terminal geometry. Departure terminal ownership now comes
  from known geometry plus live terminal authority, ENROUTE release comes from
  known terminal exit or proven center handoff, and the departure release timer
  remains only an unknown-geometry guard.
- Added saved scenarios proving known terminal geometry holds Departure inside
  terminal airspace and an unproven inside flag is ignored after the guard
  timer. Release build passed and all 94 saved regression scenarios passed. The
  installed X-Plane plugin binary was refreshed from the validated build output
  and matches SHA-256
  `590F9E61B79730DFC20E6C76C4683AEB2D878D8F36E26AC6CC7F6CFDAF339202`.
- Milestone 8 audit pass completed: all active-source `XPLMFindNavAid` uses
  were located and classified. The only remaining callsites are airport-only
  support in `NetworkPlanLink.cpp`, `FlightPlanSampler.cpp`, and
  `DiversionContextModule.cpp`; none participate in route waypoint expansion,
  airway parsing, sector matching, terminal/center coverage, or controller
  authority.
- Added `docs/MILESTONE8_XPLM_NAV_AID_AUDIT.md` and callsite comments that
  preserve the classification rule for future changes. Release build passed and
  all 94 saved regression scenarios passed. The installed X-Plane plugin binary
  was refreshed from the validated build output and matches SHA-256
  `E8E38A7291AA3D748019215B4A72B2FEFFFE3C25B62A19917D9DEE546CE3EE49`.
- First Milestone 9 cleanup pass completed: audited the live plugin workflow
  seam and removed obsolete local helper code left over from pre-shared
  workflow ownership. The removed helpers duplicated COM1-tuned center/terminal
  handoff checks that now live in `core::workflow`, plus an unused local
  destination-distance implementation superseded by the shared workflow engine.
- Release build passed and all 94 saved regression scenarios passed. The
  installed X-Plane plugin binary was refreshed from the validated build output
  and matches SHA-256
  `45AE1F6FA36C21C6A488FE986B5A902FDBFB3FFEBEF705ADD416507C80BB014B`.
- Second Milestone 9 cleanup pass completed: audited ENROUTE, Departure, and
  Arrival module seams. Preserved the intentional terminal-airspace fallback
  behavior, and simplified `EnrouteModule::Collect(...)` so the already-proven
  authoritative-route guard is not re-checked before setting display state.
- Release build passed and all 94 saved regression scenarios passed. The
  installed X-Plane plugin binary was refreshed from the validated build output
  and matches SHA-256
  `C64DB3B667576500C80BDF8948180F921B12AB861ADB375AE773CCF22E6E35F1`.
- Third Milestone 9 cleanup pass completed: audited `RouteSectorResolver.cpp`
  for cleanup-only targets. Centralized stale-status suffix handling and
  removed an unused include without changing route parsing, nav-graph
  resolution, sector traversal, or controller-authority behavior.
- Release build passed and all 94 saved regression scenarios passed. The
  installed X-Plane plugin binary was refreshed from the validated build output
  and matches SHA-256
  `325AD662834F49E66C850643BDA3CC21A75AE8F27D6BB49839305CFB4E9DD366`.
- Fourth Milestone 9 cleanup pass completed: audited the plugin lifecycle and
  display diagnostic seam in `XVatsimPlugin.cpp`. Removed dead diagnostic string
  mirrors that were written and cleared but never read; display and board log
  suppression remains driven by the existing hashes.
- Release build passed and all 94 saved regression scenarios passed. The
  installed X-Plane plugin binary was refreshed from the validated build output
  and matches SHA-256
  `6158711A76D66C648D429860695EFC781BF5140BB8223A6FFCE6ACB0C016E46E`.
- Fifth Milestone 9 cleanup pass completed: audited the plugin menu lifecycle
  seam in `XVatsimPlugin.cpp`. Removed the redundant post-create menu null
  guard after `XPLMCreateMenu(...)` already fails closed; menu item order and
  command behavior are unchanged.
- Release build passed and all 94 saved regression scenarios passed. The
  installed X-Plane plugin binary was refreshed from the validated build output
  and matches SHA-256
  `1F1EBDC4F0A7A77683CF4F5D5248F5DCA7CF9C31B799F7BB595AED061CF8E38A`.
- Sixth Milestone 9 cleanup pass completed: audited command registration and
  unregistration in `XVatsimPlugin.cpp`. Centralized repeated
  `XPLMCreateCommand`/register and unregister/null patterns while preserving
  the existing command names, descriptions, handlers, and duplicate-registration
  guard.
- Release build passed and all 94 saved regression scenarios passed. The
  installed X-Plane plugin binary was refreshed from the validated build output
  and matches SHA-256
  `812B48CBC92EA52142A3572E952A963F89307EC70661131D2ABBB424FB5547F2`.
- Seventh Milestone 9 cleanup pass completed: audited reset/session-state paths
  in `XVatsimPlugin.cpp`. Centralized repeated flight-progress and enroute
  initial-display reset assignments while preserving distinct diversion-retarget
  behavior, and removed one redundant cruise-gate reset already covered by the
  cruise-target reset helper.
- Release build passed and all 94 saved regression scenarios passed. The
  installed X-Plane plugin binary was refreshed from the validated build output
  and matches SHA-256
  `B6DE976FEC3014517023F6185F8343E479512C4E17CE47EAC4B035843D46C66B`.

## Release Rule

Do not create or upload the final store package until Milestones 6 through 9 are
complete and the final validation pass has been run from a fresh package.
