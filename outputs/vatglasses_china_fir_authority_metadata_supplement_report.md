# Step 79 - VATGlasses China FIR Authority Metadata Supplement

## Files changed

- `core/src/ControllerAuthority.cpp`
  - Expanded VATGlasses static position parsing so source-backed owner/group/pre records can be emitted as authority source records.
  - Merged duplicate source-position authority records by authority id in catalog compilation so one VATGlasses position does not become duplicate live authority rows.
- `modules/route_sector/src/RouteSectorResolver.cpp`
  - Route-sector ownership metadata now merges only `AuthorityKind::Center` source records.
  - Ownership JSON contributes role-valid CTR callsign patterns to matching route-sector keys.
  - Ownership JSON does not attach bare `pre` values as route-sector prefixes.
  - Note: this file also contains the earlier Step 77 X-Plane airway header-filter change in the dirty worktree; Step 79 only changed the ownership metadata merge.
- `tools/regression_harness/scenarios/route_sector_authority_metadata_attaches_china_fir_owner_pre_ctr.scn`
- `tools/regression_harness/scenarios/route_sector_authority_metadata_requires_linked_owner_or_pre.scn`
- `outputs/vatglasses_china_fir_authority_metadata_supplement_report.md`

## Root cause from Step 78

GTI5561 route traversal was healthy, but the route-sector authority metadata bridge did not attach usable controller callsign patterns from pinned VATGlasses `positions` owner/pre/type records to China FIR route-sector keys. The polygons and traversal existed, but the matching metadata used to prove enroute CTR ownership was missing, so the ten China FIR sectors appeared in `authorityGapSectors`.

This was static metadata coverage, not live-controller absence. `GULF_E_FSS` remaining hidden was correct because it was unrelated FSS/radio-only evidence, not route-owned enroute CTR authority proof.

## Mapping strategy

The fix is data-backed only:

- VATGlasses `positions` records are parsed with their source `owner`, `pre`, and `type`.
- Only `type=CTR` records are eligible to feed route-sector enroute authority metadata.
- A source record can attach to a route-sector key only when the normalized key is source-backed by:
  - explicit polygon/sector fields on the record,
  - the position owner id,
  - an explicit VATGlasses airspace owner/group relation,
  - or a normalized `pre` value that exactly matches the route-sector key.
- The route-sector merge attaches role-valid CTR callsign patterns such as `ZWUQ_CTR` and `ZWUQ_*_CTR`.
- Bare `pre` values are not attached as route-sector prefixes from ownership JSON. This avoids broad prefix authority while still closing the route-sector metadata gap with role-valid CTR patterns.
- If multiple source records attach to one route-sector key, patterns are merged and sorted; the code does not silently choose one owner as the winner.
- APP/TWR records are ignored for enroute CTR route-sector authority.
- Blank/incomplete owners remain non-authority.

## Coverage before and after

| Sector | Polygon/traversal coverage | Step 78 gap | Step 79 source evidence | Attached metadata after Step 79 | After gap status |
|---|---|---|---|---|---|
| ZWUQ | yes | gap | `WU`, `pre=ZWUQ`, `type=CTR` | `ZWUQ_*_CTR`, `ZWUQ_CTR` | closed |
| ZWWW | yes | gap | `WUS`, linked group `ZWWW`, `pre=ZWWW`, `type=CTR` | `ZWWW_*_CTR`, `ZWWW_CTR` | closed |
| ZLHW | yes | gap | `LZH`, `pre=ZLHW`, `type=CTR` | `ZLHW_*_CTR`, `ZLHW_CTR` | closed |
| ZLLL | yes | gap | `LZ`, linked group `ZLLL`, `pre=ZLLL`, `type=CTR` | `ZLLL_*_CTR`, `ZLLL_CTR` | closed |
| ZPKM | yes | gap | `KMG`, `pre=ZPKM`, `type=CTR` | `ZPKM_*_CTR`, `ZPKM_CTR` | closed |
| ZUUU | yes | gap | `CD`, linked group `ZUUU`, `pre=ZUUU`, `type=CTR` | `ZUUU_*_CTR`, `ZUUU_CTR` | closed |
| ZUGY | yes | gap | `GY`, linked group `ZUGY`, `pre=ZUGY`, `type=CTR` | `ZUGY_*_CTR`, `ZUGY_CTR` | closed |
| ZGGG | yes | gap | `GZ`, linked group `ZGGG`, `pre=ZGGG`, `type=CTR` | `ZGGG_*_CTR`, `ZGGG_CTR` | closed |
| ZGNN | yes | gap | `NN`, linked group `ZGNN`, `pre=ZGNN`, `type=CTR` | `ZGNN_*_CTR`, `ZGNN_CTR` | closed |
| ZGZU | yes | gap | `GZZ`, `pre=ZGZU`, `type=CTR` | `ZGZU_*_CTR`, `ZGZU_CTR` | closed |

Focused proof showed `expect.resolver_route_authority_gaps=<none>` for the ten-sector route and route-sector pattern output for all ten sectors. It also showed ownership JSON prefixes remained empty for those sectors, by design.

## Safety proofs

- No guessed prefixes were added.
  - `route_sector_authority_metadata_requires_linked_owner_or_pre.scn` keeps `ZZZZ` as `current:ZZZZ` authority gap when source `pre=ZWUQ` and linked group `ZWUQ` do not match `ZZZZ`.
- Blank/incomplete owners remain gaps.
  - `BJF` is present in the China-focused fixture but has no role-valid CTR authority pattern.
  - `resolver_authority_blank_prefix_is_data_gap.scn` still passes.
- APP/TWR records do not become enroute CTR authority.
  - The China-focused fixture includes `CDA` APP and `UUT` TWR on `ZUUU`; `ZUUU` receives only `ZUUU_*_CTR` and `ZUUU_CTR`.
- Boundary callsign properties are still not authority keys.
  - `resolver_boundary_callsign_property_is_not_authority_key.scn` still passes.
- `GULF_E_FSS` and unrelated radio-only/FSS evidence remain safe.
  - The China-focused fixture includes `GULF_E_FSS`; resolver enroute output remains unavailable with no enroute callsigns.
- Step 75 fail-closed behavior remains intact.
  - `brain_controller_relevance_blocks_reachable_centers_when_route_unavailable.scn` still passes.
- Existing VATGlasses static frequency matching remains intact.
  - `authority_position_json_vatglasses_static_frequency_matches_suffix.scn` still produces one `VATGLASSES:TRW` authority row, not duplicates.
  - `resolver_vatglasses_frequency_rejects_wrong_prefix.scn` keeps the existing wrong-prefix rejection diagnostics.

## Verification

Build:

```text
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

Result: passed.

Focused scenarios and guardrails:

```text
route_sector_authority_metadata_attaches_china_fir_owner_pre_ctr.scn
route_sector_authority_metadata_requires_linked_owner_or_pre.scn
resolver_authority_blank_prefix_is_data_gap.scn
resolver_boundary_callsign_property_is_not_authority_key.scn
enroute_authority_gap_does_not_display_offline_row.scn
brain_controller_relevance_blocks_reachable_centers_when_route_unavailable.scn
block7_enroute_reachable_center_current_and_next_accepted.scn
block7_enroute_reachable_center_off_route_rejected.scn
enroute_authority_handoff_ignores_irrelevant_polygon.scn
route_sector_stale_does_not_populate_enroute_board.scn
authority_position_json_vatglasses_static_frequency_matches_suffix.scn
resolver_vatglasses_frequency_rejects_wrong_prefix.scn
ctaf_unicom_bypass_retirement_authority_guardrail.scn
standby_assist_decision_ledger_controller_target_unchanged.scn
standby_assist_direct_ctaf_live_gate_on_selects_direct_ctaf.scn
standby_assist_writer_result_controller_success.scn
```

Result: focused bundle passed, 16 scenarios.

Full saved regression:

```text
Full regression passed: 437 scenarios
```

## Result

Step 79 closes the GTI5561 China FIR authority metadata gap with source-backed VATGlasses owner/pre/type `CTR` evidence. It does not invent callsign prefixes, does not display radio-only controllers, does not change route traversal, does not broaden CTAF/UNICOM, standby, direct CTAF, COM writer, source-owned stable-key, SID/STAR, or fail-open behavior.

Recommended Step 80: live-test the updated plugin on a China FIR route with an online route-owned China CTR if possible, and separately open a narrow data-quality/diagnostic step if any sector still reports gaps from missing pinned VATGlasses owner/pre/type source evidence.
