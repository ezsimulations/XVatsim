# GTI5561 Authority Metadata Coverage Diagnostic Report

## Files Inspected

- `C:\X-Plane 12\Log.txt`
- `C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\authority_source_registry.json`
- `C:\Users\DARRON\OneDrive\Documents\XVatsim\assets\source_data\authority_source_registry.json`
- `C:\Users\DARRON\OneDrive\Documents\XVatsim\modules\route_sector\src\RouteSectorResolver.cpp`
- `C:\Users\DARRON\OneDrive\Documents\XVatsim\core\src\ControllerAuthority.cpp`
- `C:\Users\DARRON\OneDrive\Documents\XVatsim\core\src\MapDataSource.cpp`
- Existing focused authority metadata and authority-gap scenarios under `C:\Users\DARRON\OneDrive\Documents\XVatsim\tools\regression_harness\scenarios`
- Pinned authority source packages referenced by the packaged registry:
  - `https://raw.githubusercontent.com/lennycolton/vatglasses-data/a9fb0bb82d05e7bb12770fd90e6dca510754e0e5/data/z.json`
  - `https://raw.githubusercontent.com/lennycolton/vatglasses-data/a9fb0bb82d05e7bb12770fd90e6dca510754e0e5/data/vh-vm.json`
  - `https://raw.githubusercontent.com/lennycolton/vatglasses-data/a9fb0bb82d05e7bb12770fd90e6dca510754e0e5/data/zwg.json`

## Files Changed

- `C:\Users\DARRON\OneDrive\Documents\XVatsim\outputs\gti5561_authority_metadata_coverage_diagnostic_report.md`

This Step 78 pass is report-only. No authority metadata was added, no route traversal behavior was changed, no fail-open behavior was added, and no controller display behavior was changed.

## Live Route Evidence

Live flight:

`GTI5561 UAAA->VHHH`

Route:

`PIGAL1A PIGAL M610 RULAD/K0930S0950 A460 XKC L888 SADAN Y1 OMBON W245 OVRAL B330 DAGDI W234 NIXID B330 AVPAM A599 POU R473 SIERA`

The live diagnostic line showed:

- `routeResolved=1`
- `traversal=exact`
- `waypoints=54`
- `currentSectors=UAAA`
- `nextSectors=ZWUQ,ZWWW,ZLHW,ZLLL,ZPKM,ZUUU,ZUGY,ZGGG,ZGNN,ZGZU,VHHK`
- `authorityGapSectors=next:ZWUQ,next:ZWWW,next:ZLHW,next:ZLLL,next:ZPKM,next:ZUUU,next:ZUGY,next:ZGGG,next:ZGNN,next:ZGZU`
- `unsupportedList=PIGAL1A`
- `unresolvedList=none`
- `unresolvedAirwayList=none`

The route path was therefore built successfully. The unsupported `PIGAL1A` SID token did not block route traversal. The ten authority gaps were not route-polygon failures; they were authority metadata attachment failures for sectors that were already present in the route traversal.

No live controllers were accepted or displayed for this route during the inspected interval. That is separate from the metadata issue: no controller online means there was no live controller to match, while an authority metadata gap means the static route-sector authority layer had no usable prefix/pattern attached to that sector key.

## Authority Metadata Source

The packaged internal registry used by the plugin is:

`C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\authority_source_registry.json`

The matching repository asset is:

`C:\Users\DARRON\OneDrive\Documents\XVatsim\assets\source_data\authority_source_registry.json`

The registry was generated from:

- Source: `https://github.com/lennycolton/vatglasses-data`
- Commit: `a9fb0bb82d05e7bb12770fd90e6dca510754e0e5`
- Generated: `2026-05-17`

Relevant pinned source packages include:

- `data/z.json` for China-region VATGlasses data.
- `data/vh-vm.json` for Hong Kong / Macau / Vietnam-region VATGlasses data.
- `data/zwg.json`, which was also checked and did not provide direct matching records for these ten route-sector ids.

## How Route-Sector Polygons Get Authority Metadata

`RouteSectorResolver` builds route traversal from boundary polygons, then calls the authority catalog to attach controller callsign patterns and prefixes to each matched route sector.

The important ownership split is:

- Polygon/traversal coverage answers: did the route geometry pass through this sector?
- Authority metadata coverage answers: does XVatsim have static data that can map that sector to controller authority?
- Callsign/prefix matching metadata answers: did that static authority data produce usable `controllerCallsignPatterns` or `controllerPrefixes` on the route-sector match?
- Live controller presence answers: was any live VATSIM controller online to match against those static patterns/prefixes?

`authority-gaps` are emitted when a current or next route-sector match has neither `controllerCallsignPatterns` nor `controllerPrefixes`. A boundary/property callsign or human-readable name by itself is not controller authority coverage. If it does not produce a matching prefix/pattern key, it is correctly classified as a metadata gap.

## Coverage Table

| Sector | Polygon/traversal coverage exists | Authority metadata record exists in pinned source | Callsign/prefix matching metadata attached to live route-sector key | Live controller present | Classification | Data-backed known patterns/prefixes from source | Recommendation |
|---|---:|---:|---:|---:|---|---|---|
| `ZWUQ` | yes | yes | no | no | metadata attachment gap | `ZWUQ_CTR`, `ZWUQ_*_CTR` from owner `WU`, pre `ZWUQ`, type `CTR` | Add data-backed mapping from source owner/pre evidence to route-sector key; do not guess. |
| `ZWWW` | yes | yes | no | no | metadata attachment gap | `ZWWW_CTR`, `ZWWW_*_CTR`; also related owner evidence includes `ZWUQ_CTR` from `WU` and unusable `BJF` owner with no pre/type | Attach source group/owner authority evidence to the `ZWWW` route-sector key when backed by source data. |
| `ZLHW` | yes | yes | no | no | metadata attachment gap | `ZLHW_CTR`, `ZLHW_*_CTR` from owner `LZH`, pre `ZLHW`, type `CTR` | Add source-backed owner/pre mapping for the route-sector key. |
| `ZLLL` | yes | yes | no | no | metadata attachment gap | `ZLLL_CTR`, `ZLLL_*_CTR`; related owner evidence includes `ZLHW_CTR`; `BJF` has no usable pre/type | Attach source group/owner authority evidence to `ZLLL` without treating blank owner records as authority. |
| `ZPKM` | yes | yes | no | no | metadata attachment gap | `ZPKM_CTR`, `ZPKM_*_CTR` from owner `KMG`, pre `ZPKM`, type `CTR` | Add source-backed owner/pre mapping for the route-sector key. |
| `ZUUU` | yes | yes | no | no | metadata attachment gap | `ZUUU_CTR`, `ZUUU_*_CTR`; related owner evidence includes `ZPKM_CTR`, APP/TWR records, and blank `BJF` | Attach enroute CTR authority evidence only; APP/TWR evidence must not become enroute center authority. |
| `ZUGY` | yes | yes | no | no | metadata attachment gap | `ZUGY_CTR`, `ZUGY_*_CTR`; related owner evidence includes `ZPKM_CTR` | Attach source-backed owner/pre mapping for the route-sector key. |
| `ZGGG` | yes | yes | no | no | metadata attachment gap | `ZGGG_CTR`, `ZGGG_*_CTR`; related owner evidence includes `ZGZU_CTR`; `BJF` has no usable pre/type | Attach source group/owner authority evidence to `ZGGG`; keep overlapping owner evidence explicit. |
| `ZGNN` | yes | yes | no | no | metadata attachment gap | `ZGNN_CTR`, `ZGNN_*_CTR`; related owner evidence includes `ZGZU_CTR`; `BJF` has no usable pre/type | Attach source group/owner authority evidence to `ZGNN`; keep overlap proof in diagnostics. |
| `ZGZU` | yes | yes | no | no | metadata attachment gap | `ZGZU_CTR`, `ZGZU_*_CTR` from owner `GZZ`, pre `ZGZU`, type `CTR` | Add source-backed owner/pre mapping for the route-sector key. |

Every listed sector has polygon/traversal coverage and data-backed source evidence somewhere in the pinned VATGlasses `z.json` package. The live route-sector snapshot nevertheless reported each one as an authority gap because no usable callsign/prefix matching metadata was attached to the live route-sector match.

This is not live-controller-feed absence. Live-controller absence means no controller was online. These ten gaps mean static source authority metadata did not reach the route-sector matching fields used by brain-owned relevance.

## UAAA And VHHK

`UAAA` was the current sector and was not listed in `authorityGapSectors`, so the route-sector snapshot had usable authority matching metadata for it. The inspected live diagnostic did not print the exact attached pattern/prefix values, so no extra value is claimed here.

`VHHK` was the final/arrival next sector and was not listed as a gap. The packaged registry includes `vh-vm.json`, and existing focused scenarios prove Hong Kong authority matching data can produce usable `HKG`-based prefixes/patterns such as `HKG`, `HKG_CTR`, `HKG_*_CTR`, `HKG_FSS`, and `HKG_*_FSS`.

The fact that `UAAA` and `VHHK` were not gaps confirms the route-sector metadata path can work when usable matching metadata is attached to the route-sector key.

## GULF_E_FSS Behavior

`GULF_E_FSS` was visible in xPilot on `133.600`, but XVatsim did not display it in the UI. That was correct for this test.

The inspected diagnostics showed no accepted/displayed controller relevance rows for the route and no unsafe radio-only promotion. This preserved the Step 75 safety rule: radio-only or unrelated controllers must not be displayed as route/current/next polygon authority without route-owned proof.

## Root Cause Classification

Classification: static authority metadata attachment gap.

The route resolver built the route and traversal correctly. The missing data was not caused by the absence of online controllers. The pinned source data contains China-region owner/pre/type records that can generate controller callsign patterns for the affected sectors, but the current route-sector authority lookup did not attach those source-backed patterns/prefixes to the route-sector keys in the live snapshot.

Likely technical cause:

- VATGlasses static source records are keyed through owner ids, `pre` fields, and airspace/group ownership relationships.
- Route traversal sectors are keyed by boundary ids such as `ZWUQ`, `ZWWW`, `ZLHW`, and so on.
- Current catalog compilation and lookup do not consistently bridge those source owner/pre/group records onto the route-sector ids for these China FIR sectors.
- Blank or incomplete owner records such as `BJF` must remain non-authority evidence unless they produce a real prefix/pattern.

## Recommended Step 79

Recommended next step:

`Step 79: data-backed VATGlasses route-sector authority metadata supplement for China FIR owner/pre mappings`

Scope should be narrow:

- Use pinned source data relationships only.
- Do not invent callsign prefixes.
- Do not use live-controller absence as metadata proof.
- Add focused diagnostics/scenarios proving that source owner/pre/type records attach usable `CTR` patterns/prefixes to route-sector keys like `ZWUQ`, `ZWWW`, `ZLHW`, `ZLLL`, `ZPKM`, `ZUUU`, `ZUGY`, `ZGGG`, `ZGNN`, and `ZGZU`.
- Preserve Step 75 fail-closed behavior when metadata is missing or ambiguous.
- Keep APP/TWR records from becoming enroute Center authority unless the source data explicitly supports that role.
- Keep `GULF_E_FSS` and unrelated FSS/radio-only controllers from displaying without route-owned authority proof.

## Verification

Build command:

`& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo`

Build result:

- Passed.
- `XVatsimPlugin` built.
- `XVatsimRegressionHarness` built.

Focused scenario results:

- `resolver_authority_gap_identifiers_trace_current_and_next.scn` passed.
- `resolver_boundary_callsign_property_is_not_authority_key.scn` passed.
- `resolver_authority_blank_prefix_is_data_gap.scn` passed.
- `resolver_authority_reports_unmapped_controller_gap.scn` passed.
- `enroute_authority_gap_does_not_display_offline_row.scn` passed.
- `authority_source_registry_accepts_real_vatglasses_urls.scn` passed.

Full saved regression:

- Intentionally skipped because Step 78 was report-only and made no code or scenario changes.

## Step 78 Decision

The GTI5561 route polygon/traversal path was healthy after the airway loader fix. The ten reported authority gaps are real static metadata attachment gaps, not missing route polygons and not merely absent live controllers.

No unsafe display behavior was found during this test. The system failed closed: without attached route-sector authority matching metadata, no unrelated or radio-only controller was promoted into the UI.
