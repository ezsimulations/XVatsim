# Tuned Center Route Ownership Override Fix

## Live Trigger

VOI1742 MMGL-KLAS crossed from Mexico Center route ownership into Mazatlan route ownership. Route polygon tracking correctly changed from `currentPolygon=MMEX,nextPolygon=MMZT` to `currentPolygon=MMZT,nextPolygon=KZAB`, but `MMEX_CTR 126.600` remained displayed as active/current.

## Root Cause

`BrainControllerRelevanceWorker` treated `station.tuned` as a substitute for Center route ownership through the `routeMatch.matched || station.tuned` branch. When `MMEX_CTR` stayed tuned/reachable after the polygon transition, the branch accepted it, assigned the current route polygon key, and emitted `CURRENT_POLYGON` with reason `center-tuned-current-radio`.

## Ownership Policy

Policy name: `center-tuned-off-route-not-route-owned`

- Tuned radio state is advisory evidence only for Center controllers.
- Center route ownership comes only from brain-owned route polygon matching.
- A Center can become `CURRENT_POLYGON` or `NEXT_POLYGON` only when `MatchCenterToRoutePolygon` proves the callsign belongs to the current or next route sector.
- BrainDisplayIntent fallback relation inference also treats tuned Center state as advisory only; COM1-active equality cannot create `CURRENT_POLYGON`.
- Tuned state may remain on a station as context, but it cannot create `CURRENT_POLYGON`, rewrite a stale center to the current polygon, promote an off-route Center, or bypass route authority proof.
- Terminal facility relevance was intentionally not changed by this fix.

## Downstream Ownership

`BrainDisplayIntent` continues to consume brain-owned relevance decisions and relation facts. Its Center fallback can infer current/next only from route polygon context or route-entry distance, not tuned radio state. UI, overlay, publisher, and standby assist do not create route ownership and were not changed. Standby assist consumes the final brain display order only, so hiding the stale tuned Center upstream prevents it from being selected as the next controller target.

## Regressions Added

- `brain_controller_relevance_tuned_old_center_not_current_after_polygon_transition.scn`
- `brain_controller_relevance_tuned_off_route_center_cannot_satisfy_route_match.scn`
- `standby_assist_does_not_select_tuned_stale_center_after_polygon_transition.scn`

## Expected Result

After an MMEX to MMZT transition, `MMEX_CTR` is rejected by Center relevance unless MMEX is still route-current or route-next by polygon proof. The displayed Center chain follows `MMZT` current and `KZAB` next authority only, and standby assist cannot select the stale tuned MMEX Center.
