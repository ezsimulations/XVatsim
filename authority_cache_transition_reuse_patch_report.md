# XVatsim Performance Recovery Patch 2: Authority Proof Cache Containment

Date: 2026-06-30

## Patch Design Before Runtime Edits

### Root Cause From Live Diagnostics

The remaining live stalls are `BrainAuthorityRelevanceWorker` proof rebuilds after the first route proof has already been built:

- tick 4536: route unchanged, `signature-or-source-dirty`, controller/source generation changed, radio candidates unchanged
- tick 5900: route window transition `MMEX/MMZT -> MMZT/KZAB`, `route-progress-dirty`
- tick 10879: route window transition `MMZT/KZAB -> KZAB/KZLA`, `route-progress-dirty`

The current cache treats the active current/next route window as part of the proof identity. Normal polygon transitions therefore invalidate the whole proof even when the proof universe is only shifting or narrowing among already route-owned sectors.

### Current Authority Cache Inputs

Current authority cache/reuse is driven by these fields in `RouteSectorResolver.cpp`:

- `BuildAuthorityProgressRouteSignature`
  - route availability/resolution
  - center boundary generation
  - authority catalog generation
  - departure/destination
  - current sector identities/tokens/controller patterns
  - next sector identities/tokens/controller patterns
- `BuildAuthorityOperationalScopeSignature`
  - aircraft validity/on-ground
  - controller feed available/stale
  - authority transceiver available/stale
  - terminal boundary generation
  - work stage/window
  - endpoint inclusion
  - deferred sector count
  - current/next route sector identities/tokens
- `BuildAuthorityWatchInputSignature`
  - controller feed available/stale
  - scoped artifact signature
  - transceiver availability
  - route-touching controller identity/frequency/facility/actionability/ATIS
  - route-proximate transceiver station identity/frequency/rounded location
- `BuildScopedAuthorityRelevanceSignature`
  - controller feed available/stale
  - structural route signature
  - route authority match keys
  - transceiver availability
  - controller proof entries and route-proximate transceiver proof entries

### Current Invalidation Modes

- `route-progress-dirty`
  - forced when `BuildAuthorityProgressRouteSignature` changes
  - current/next polygon changes are enough to force this even if the new current/next sector was already in the previous route scope
- `signature-or-source-dirty`
  - forced when scoped relevance or operational signatures differ after progress still matches
  - controller generation churn can force this when the route-touching proof entries change, even if the route-owned authority result set can safely reuse the prior proof

### Design Target

Split proof ownership from active window selection:

- Stable proof ownership signature:
  - route-owned authority candidate keys
  - authority catalog/source generations
  - stable controller callsign/frequency/facility/actionable/ATIS proof keys
  - stable transceiver proof keys only when used as proof evidence
- Active window signature:
  - current/next sectors
  - aircraft position/progress
  - arrival/current window
  - relevant route geometry slice

If stable proof ownership is unchanged and the new active route keys are a subset of already proven route-owned proof keys, reuse the cached proof data and recompute only the live relevant authority slice. If a new route-owned center candidate appears, authority catalog/source generations change, or source-owned proof keys change, rebuild remains required.

### Containment Approach

Patch 2 will add an authority proof cache layer inside `RouteSectorResolver.cpp` only:

1. Store the last built proof evidence and stable proof ownership signature.
2. On route window transition, test whether the new route authority keys are covered by the cached proof keys.
3. On controller/source generation churn, test whether route-owned stable proof candidate keys are unchanged.
4. If safe, return a recomputed/narrowed snapshot using cached proof evidence with cache status:
   - `authority-cache-window-reuse`
   - `authority-cache-stable-proof-reuse`
5. Fall back to `authority-proof-build` when a truly new route-owned center candidate appears or stable proof ownership changes.

### Ownership Guardrails

The cache reuse must not:

- create Center route ownership from tuned/COM state
- create Center route ownership from reachable/radio-range alone
- bypass route authority proof
- alter display order
- alter standby selection
- move authority decisions into UI/helper/runtime code

The reused proof is advisory cache data inside the brain-owned route authority decision layer. Route polygon authority remains the source of Center ownership.

## Implementation Notes

Patch 2 keeps authority ownership inside `RouteSectorResolver.cpp` and adds a conservative proof-reuse branch before the expensive full authority proof build.

Runtime implementation points:

- Stable direct proof keys are built from normalized callsign, frequency, facility, authority id, polygon key, and matched authority pattern.
- Reuse is allowed only when the current direct route-owned proof entries are already included in the cached proof ledger and the current route authority keys are covered by the cached route authority keys.
- Route-window transitions return `authority-cache-window-reuse` with reason `route-window-proof-subset`.
- Source/controller churn with unchanged route-owned proof returns `authority-cache-stable-proof-reuse` with reason `stable-proof-ownership-unchanged`.
- The reuse path refreshes the live relevant authority slice from the current route-scoped authority catalog and current route geometry before returning the snapshot.
- The refreshed snapshot is passed back through `BuildBrainOwnedAuthorityRelevanceSnapshot`, so final live relevant authorities remain brain-owned.
- Route-window reuse does not replace the broader cached proof snapshot, preserving already proven downstream sectors for later transitions.
- Stable-proof reuse does update the cached snapshot because the route window did not move and the stable proof ownership stayed unchanged.

## Changed Files

- `modules/route_sector/src/RouteSectorResolver.cpp`
  - Added stable direct authority proof keys.
  - Added route-window and stable-proof reuse modes.
  - Added refreshed current relevant-authority slice generation for reused proofs.
  - Added survivor-ledger reset/refresh before brain-owned projection.
- `tools/regression_harness/src/main.cpp`
  - Added repeat-run aircraft-state support.
  - Added repeat controller replacement support.
  - Added `expect.resolver_authority_repeat_relevant_matches`.
- `tools/regression_harness/scenarios/authority_cache_reuses_proof_on_route_window_transition.scn`
- `tools/regression_harness/scenarios/authority_cache_reuses_proof_on_second_route_window_transition.scn`
- `tools/regression_harness/scenarios/authority_cache_reuses_proof_for_route_scope_source_churn.scn`
- `tools/regression_harness/scenarios/authority_cache_rebuilds_when_new_route_owned_center_appears.scn`
- `tools/regression_harness/scenarios/authority_cache_reuse_tuned_reachable_off_route_center_not_authoritative.scn`
- `tools/regression_harness/scenarios/resolver_authority_cache_reuses_proof_for_watch_only_controller_churn.scn`
  - Updated expected cache mode to the new stable-proof reuse diagnostic.
- `authority_cache_transition_reuse_patch_report.md`

## Authority Cache Before/After

Before:

- `BuildAuthorityProgressRouteSignature` included current/next sector identity. A normal transition such as `MMEX/MMZT -> MMZT/KZAB` could force `route-progress-dirty` and a full `authority-proof-build`.
- Controller/source generation churn could produce `signature-or-source-dirty` and force a full proof build even when the route-owned proof set was unchanged.
- Existing cache hits were tied to matching progress and operational signatures. They did not distinguish stable proof ownership from active current/next window state.

After:

- Stable proof ownership is checked separately from the active route window.
- If the current direct route-owned proof entries are covered by the cached proof ledger and the current route keys are a subset of cached route keys, the resolver reuses the cached proof.
- The live current/next slice is still recomputed from current route geometry, so `aircraftInside` and `routeEntryDistanceNm` do not come from a stale window.
- A true new route-owned Center candidate still forces `authority-proof-build`.

## Performance Impact Table

| Live pattern | Before | After |
| --- | --- | --- |
| tick 4536 source/controller generation changed, stable route-owned proof unchanged | `signature-or-source-dirty`, full proof build around 470 ms | `authority-cache-stable-proof-reuse`; refreshes only the direct proof slice |
| tick 5900 `MMEX/MMZT -> MMZT/KZAB` | `route-progress-dirty`, full proof build around 508 ms | `authority-cache-window-reuse`; reuses already proven route keys and refreshes active slice |
| tick 10879 `MMZT/KZAB -> KZAB/KZLA` | `route-progress-dirty`, full proof build around 478 ms | `authority-cache-window-reuse` when KZAB/KZLA proof keys are already covered |
| true new route-owned Center appears | full proof build | full proof build remains allowed |

Expected result: stable live-flight route progress and source churn no longer solve the full authority proof world synchronously when the route-owned proof set is already proven.

## Authority Ownership Impact Table

| Evidence/input | Authority impact after Patch 2 |
| --- | --- |
| Route-owned authority catalog and polygons | Remain authoritative. Current proof entries must match route-scoped authority decisions. |
| Current/next/arrival route window | Selects and refreshes the active slice only when proof ownership is already covered. |
| Controller generation | Can trigger stable-proof reuse when route-owned proof identities are unchanged. It cannot create ownership by itself. |
| Tuned/COM state | Not used by the new cache key or reuse gate. Cannot create Center route ownership. |
| Reachable/radio-range/transceiver state | Not used to satisfy stable direct proof coverage. Transceiver metadata is copied for diagnostics only; stale proof survivor flags are reset during reuse. |
| Frequency | Used only as part of the stable direct controller identity key. Frequency alone cannot create ownership. |
| UI/overlay/standby assist | Not changed. They continue to consume brain-published results. |

## Proof That Tuned/Reachable/Radio/COM State Did Not Become Authoritative

- The changed `RouteSectorResolver.cpp` diff contains no `station.tuned`, `COM1`, `COM2`, `reachable`, or radio-range authority fallback.
- The new stable proof key uses direct route-scoped authority decisions from `EvaluateControllerAuthority` / `ActivateAuthorityPolygons`.
- The reuse gate requires cached stable direct proof coverage and cached route-key coverage.
- The reuse branch rebuilds relevant authorities from direct proof and current route geometry, not from tuned radio state or transceiver reachability.
- Existing searches still find pre-existing tuned/fallback diagnostics in `BrainControllerRelevanceWorker.cpp`, `BrainDisplayIntent.cpp`, and `BrainOwnedRuntime.cpp`, but Patch 2 did not modify those files or add any downstream workaround.
- Guardrail `authority_cache_reuse_tuned_reachable_off_route_center_not_authoritative.scn` passed.

## Verification

- Build: `RelWithDebInfo` passed.
- Focused Patch 2 scenarios: 5 passed.
  - `authority_cache_reuses_proof_on_route_window_transition.scn`
  - `authority_cache_reuses_proof_on_second_route_window_transition.scn`
  - `authority_cache_reuses_proof_for_route_scope_source_churn.scn`
  - `authority_cache_rebuilds_when_new_route_owned_center_appears.scn`
  - `authority_cache_reuse_tuned_reachable_off_route_center_not_authoritative.scn`
- Existing tuned Center and authority ownership guardrails: 33 passed.
- Full saved regression: 448 passed.
- `git diff --check`: passed. Git reported only normal CRLF working-copy warnings.

## XPL Copy Confirmation

The XPL was not copied into the simulator during Patch 2.

## Recommendation

Commit after review if the diff looks acceptable. Hold public release until live testing confirms that the remaining route-polygon rebuild spikes are either gone or isolated for the next recovery patch.
