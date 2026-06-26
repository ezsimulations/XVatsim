# Step 80 - GTI5561 Route/Authority Rebuild Performance Diagnostic

## Files and logs inspected

- `C:\X-Plane 12\Resources\plugins\XVatsim\logs\xvatsim_diagnostics.log`
- `modules/route_sector/src/RouteSectorResolver.cpp`
- `brain/src/BrainRoutePolygonWorker.cpp`
- `brain/src/BrainControllerRelevanceWorker.cpp`
- `core/src/ControllerAuthority.cpp`

Files changed in Step 80:

- `outputs/gti5561_route_authority_performance_diagnostic_report.md`

No code, cache behavior, route traversal, authority relevance behavior, controller relevance, display logic, CTAF/UNICOM, standby/direct CTAF/COM writer, or source-owned stable-key behavior was changed.

## Live timing summary

The fresh GTI5561 UAAA->VHHH log shows the Step 79 correctness fix is active:

- Route status after source load: `ROUTE 54 pts 1/11 sectors exact unsupported 1`
- No `authority-gaps` suffix appears after the updated source path loads.
- Current/next/final polygons are stable: `UAAA`, `ZWUQ`, `VHHK`.
- Settled summary ticks are healthy at `0-3 ms`, usually `0-1 ms`.
- The initial expensive rebuild was:
  - `tick=395`
  - `totalMs=2037`
  - `route=1278 ms`
  - `authorityRelevance=756 ms`
- Later authority-only rebuilds were:
  - `tick=785`, `totalMs=97`, `authorityRelevance=96 ms`
  - `tick=815`, `totalMs=98`, `authorityRelevance=97 ms`
  - `tick=890`, `totalMs=91`, `authorityRelevance=91 ms`

## Initial 2037 ms breakdown

At `tick=359`, source/route authority was not ready:

```text
routeStatus="ROUTE sectors pending"
authorityStatus="AUTHORITY route unavailable"
BrainRoutePolygonWorker reason=boundary-cache-unavailable cache=source-pending
BrainAuthorityRelevanceWorker reason=route-snapshot-unavailable cache=input-unavailable
```

At `tick=395`, source and route inputs became available and the route was built:

```text
routeStatus="ROUTE 54 pts 1/11 sectors exact unsupported 1"
BrainRoutePolygonWorker ms=1278 reason=route-key-changed cache=route-rebuild
BrainAuthorityRelevanceWorker ms=756 reason=no-authority-cache cache=authority-proof-build
src=ctrl=13,center=1,catalog=1,terminal=1
```

Route hash moved from the pending/unresolved hash to the resolved route hash:

```text
tick=359 routeHash=16035533154935068057 current=none next=none arrival=none
tick=395 routeHash=3098207324906631348 current=UAAA next=ZWUQ arrival=VHHK
```

Classification: `necessary`.

Reason: route/source/controller authority inputs genuinely changed. The route snapshot went from source-pending/unavailable to resolved with current/next/final polygons, and there was no existing authority cache.

## Later 96-97 ms authority rebuild breakdown

After `tick=395`, route remained stable:

```text
routeHash=3098207324906631348
current=UAAA
next=ZWUQ
arrival=VHHK
BrainRoutePolygonWorker cache=route-polygon-cache-hit
```

The later rebuilds were authority-only:

| Tick | totalMs | route ms | authority ms | route cache | authority reason | authority source counters | status counters |
|---|---:|---:|---:|---|---|---|---|
| 785 | 97 | 0 | 96 | route-polygon-cache-hit | signature-or-source-dirty | `ctrl=39,center=1,catalog=1,terminal=1` | candidates 22, authorities 14, polygons 18, routeKeys 3, diagnostics 23 |
| 815 | 98 | 0 | 97 | route-polygon-cache-hit | signature-or-source-dirty | `ctrl=41,center=1,catalog=1,terminal=1` | candidates 22, authorities 14, polygons 18, routeKeys 3, diagnostics 23 |
| 890 | 91 | 0 | 91 | route-polygon-cache-hit | signature-or-source-dirty | `ctrl=46,center=1,catalog=1,terminal=1` | candidates 21, authorities 14, polygons 18, routeKeys 3, diagnostics 22 |

Classification for all three later rebuilds: `unknown`.

Reason: the log proves controller-feed generation changed while route/source metadata generations stayed stable, but the current diagnostics do not expose enough detail to prove whether the watched authority candidate set changed due to route-relevant controller evidence or unrelated controller-feed noise.

## Meaning of `signature-or-source-dirty`

In `RouteSectorResolver::ResolveBrainScheduledAuthorityVerification`, `signature-or-source-dirty` is emitted only after the route progress still matches the previous proof. It means at least one of these changed:

- authority watch input signature,
- scoped authority relevance signature,
- operational scope signature.

The relevant code checks:

```text
watchInputSignature != lastAuthorityWatchInputSignature_
relevanceSignature != lastAuthorityRelevanceSignature_
operationalScopeSignature != lastAuthorityOperationalScopeSignature_
```

The operational signature includes route/source/aircraft/work-scope structure. The watch and relevance signatures include filtered controller/feed data that can touch route authority watches, route-scoped patterns, route-scoped tokens, endpoint-local watches, transceiver proof candidates, and duplicated ATIS route clues.

The live log does not currently print which sub-signature changed or which controller/candidate entry changed.

## What changed between cache-hit ticks and rebuild ticks

| Input | Evidence | Changed? | Diagnostic conclusion |
|---|---|---:|---|
| route hash | `3098207324906631348` from resolved build onward | no | route rebuild did not repeat after initial build |
| route-sector snapshot hash/projection | current `UAAA`, next `ZWUQ`, arrival `VHHK` stayed stable | no visible change | stable in exposed diagnostics |
| source generation | `center=1,catalog=1,terminal=1` on rebuilds | no | VATGlasses/source metadata generation stayed stable |
| controller feed generation | `ctrl=13 -> 39 -> 41 -> 46` on authority rebuilds | yes | controller feed churn is the visible trigger |
| relevant controller signature | not directly logged | unknown | cannot prove relevance vs unrelated noise |
| candidate count | `23 -> 22 -> 22 -> 21` across authority builds | yes on tick 785 and 890; unchanged at 815 | candidate identity not logged |
| active controller count | `active=0` throughout | no | no live relevant authority was accepted |
| authority metadata generation | authorities 14, polygons 18, routeKeys 3 stayed stable | no | metadata itself was not changing |

## Is unrelated controller-feed noise the cause?

Not proven.

The log shows controller-feed generation changed and the authority candidate/diagnostic counts changed on some rebuilds. That could be unrelated global feed churn, but it could also be a route-watched candidate entering/leaving the filtered authority watch set. Because the current diagnostics do not log the watch-input delta or candidate identities, Step 80 cannot honestly classify the later rebuilds as avoidable.

No cache-key tightening is recommended yet.

## Is Step 79 VATGlasses metadata expansion recomputed unnecessarily?

Possibly, but not proven from the current log.

Source metadata generations remained stable on the later rebuilds:

```text
center=1,catalog=1,terminal=1
authorities=14
polygons=18
routeKeys=3
```

The code uses cached core authority/polygon catalog helpers and a route-scope artifact cache. However, the later `signature-or-source-dirty` rebuild still spends about 91-97 ms in authority relevance. The log does not distinguish:

- cache hit/miss for `GetCachedAuthorityRelevanceScopeArtifacts`,
- core controller authority catalog cache hit/miss,
- core authority polygon catalog cache hit/miss,
- watched controller candidate delta,
- actual authority evaluation cost.

Therefore the current evidence cannot prove whether Step 79 metadata expansion is being recomputed unnecessarily or whether most cost is in live authority evaluation against changed controller candidates.

## Can route rebuild repeat unnecessarily for the same route?

No repeated route rebuild was observed after the initial source-ready route build.

Evidence:

- Initial route build: `tick=395`, `route=1278 ms`, `reason=route-key-changed`, `cache=route-rebuild`.
- Later expensive authority rebuilds: `route=0 ms`, `BrainRoutePolygonWorker cache=route-polygon-cache-hit`.
- Route hash stayed stable: `3098207324906631348`.

The initial route build is classified as necessary. Repeated route rebuild for the same GTI5561 route was not observed in this log.

## Rebuild classification

| Tick | Rebuild | Classification | Why |
|---|---|---|---|
| 359 | source-pending authority unavailable check, 7 ms | necessary | route/source snapshot unavailable; fail-closed behavior was correct |
| 395 | route rebuild + first authority proof, 2037 ms | necessary | route changed from pending to resolved; source generations became available; no authority cache existed |
| 785 | authority proof rebuild, 97 ms | unknown | route/source stable, controller feed generation changed, candidate count changed; candidate identity/delta not logged |
| 815 | authority proof rebuild, 98 ms | unknown | route/source stable, controller feed generation changed, candidate count unchanged; sub-signature/candidate delta not logged |
| 890 | authority proof rebuild, 91 ms | unknown | route/source stable, controller feed generation changed, candidate count changed; candidate identity/delta not logged |

No rebuild is classified as `avoidable` in Step 80 because the log does not prove unrelated controller-feed noise.

## Proposed Step 81 plan

### Safe diagnostic counters

Add diagnostic-only counters/ledger fields before optimizing:

- which sub-signature changed: progress, operational, watch input, relevance;
- route hash and route structural signature;
- source generation tuple: center/catalog/terminal;
- controller feed generation and relevant watched-candidate count;
- hash of watched authority candidate entries;
- added/removed watched candidate callsigns and frequencies;
- route-scoped metadata artifact cache hit/miss;
- core VATGlasses/controller authority catalog cache hit/miss;
- core authority polygon catalog cache hit/miss;
- time spent in:
  - scope artifact build,
  - watch input signature build,
  - scoped relevance signature build,
  - authority decision evaluation,
  - polygon relevance evaluation.

### Safe cache-key tightening

Do not tighten cache keys until Step 81 diagnostics prove the changing input is unrelated to route-owned authority relevance.

Potential safe target after proof:

- If controller feed generation changes but watched authority candidate entries are byte-identical, treat it as a watch/signature hit.
- If only off-route, non-watch-token, non-frequency, non-endpoint controllers changed, avoid rebuilding route-owned authority relevance.

### Precompile/reuse metadata

Potential safe target after proof:

- Ensure VATGlasses owner/pre/type expansion and authority polygon indexes are compiled once per source generation and route structural scope.
- Log cache hit/miss for this path first; only optimize if the diagnostic proves repeated cache misses or expensive recomputation.

### Risky changes to avoid

- Do not skip rebuilds based only on total controller count.
- Do not ignore candidate count changes without knowing candidate identity.
- Do not remove frequency/token/ATIS/transceiver candidate inputs from signatures without focused guardrails.
- Do not weaken Step 75 fail-closed behavior.
- Do not allow radio-only Centers/FSS to display without route-owned authority proof.

## Verification

Build command:

```text
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config RelWithDebInfo
```

Build result: passed.

Full regression decision: intentionally skipped because Step 80 is report-only and no code behavior changed.

## Conclusion

The initial 2037 ms rebuild was necessary. The later 91-98 ms authority rebuilds were triggered while route/source metadata stayed stable and controller feed generation changed, but the current diagnostics do not prove whether that controller-feed churn was relevant or unrelated. Step 81 should add diagnostic counters first, then decide whether cache-key tightening or metadata precompile reuse is safe.
