# XVatsim V2 Step 4 VATSIM METAR — Offline Proof Summary

Date: 2026-08-27
Status: PASS — offline implementation candidate, Director review pending

## Authorization boundary

This proof stayed inside the approved Step 4 offline gate. It did not deploy an
`.xpl`, start X-Plane or xPilot, make a live VATSIM request, perform a controlled
live proof, or modify V1.2.3. Those actions remain separately gated.

The 116 pre-existing untracked evidence files under `outputs/` were excluded
from edits and commits. Step 4 created only the explicitly approved new evidence
paths.

## Contract results

- Governing architecture: PASS — brain decides, removable modules produce
  facts, overlay renders brain-approved facts.
- Plugin boundary: PASS — the plugin binds `AsyncFactWorkerHost`, forwards
  bounded facts, invokes the generic brain cycle/lifecycle entry points, and
  contains no METAR endpoint, parser, target, cadence, or concrete-client policy.
- Source: PASS — production source is restricted to
  `https://metar.vatsim.net/:icao?format=json`.
- Targeting: PASS — IFR departure/arrival phase rules and conservative VFR
  committed-departure rules are brain-owned; no nearby scan or route prefetch.
- Lookup: PASS — strict ICAO validation, immediate pending drawer, one-shot
  dispatch, eight-second success spotlight, bounded failure, manual-selection
  ownership, no delayed reopen, no lookup refresh, and primary preemption.
- Parser/category: PASS — METAR/SPECI, observation time, statute-mile and metric
  visibility, fractions, `9999`, CAVOK, RVR exclusion, ceiling rules, remarks
  isolation, thresholds, and conservative UNKNOWN behavior.
- Freshness/cache: PASS — 60-second primary eligibility, 120/240/300-second
  bounded backoff, 90-minute monotonic stale deadline, adjacent-month time
  resolution, transient cached state, and stale gray/unknown presentation.
- History: PASS — bounded newest-first accepted changed content, stable identity,
  pinned-primary presentation outside history, and ATIS/PDC isolation.
- Disconnect/lifecycle: PASS — disconnected request and visible-update
  suppression, deferred unparsed in-flight fact, reconnect reevaluation,
  preservation boundaries, clearing boundaries, cancellation, and join.
- Quiet runtime: PASS — unchanged content separates content, source-health,
  freshness, and presentation generations and causes no recurring accessory work.

## Regression proof

- Focused Step 4 scenarios: 53/53 passed in 929 ms.
- Complete saved scenarios: 547/547 passed in 13,895 ms.
- Canonical scenario-set SHA-256:
  `780ABD3A26E0E6C96ADBB3261BB6C3960B21D658F4613C21754BBDD516DF8902`.
- Fingerprint input per scenario: UTF-8 repository-relative filename with
  forward slashes, NUL, raw bytes, NUL; ordinal case-insensitive filename order
  with ordinal tie-break.

Focused scenario files:

1. `v2_step4_ambiguous_weather_unknown.scn`
2. `v2_step4_brain_owned_worker_dispatch_only.scn`
3. `v2_step4_category_threshold_boundaries.scn`
4. `v2_step4_ceiling_rules.scn`
5. `v2_step4_changed_content_history_once.scn`
6. `v2_step4_departure_enroute_primary_switch.scn`
7. `v2_step4_disconnect_inflight_completion_deferred.scn`
8. `v2_step4_disconnect_request_suppression.scn`
9. `v2_step4_fresh_cache_transient_failure.scn`
10. `v2_step4_freshness_monotonic_deadline.scn`
11. `v2_step4_history_isolation.scn`
12. `v2_step4_identical_content_health_only.scn`
13. `v2_step4_ifr_arrival_primary_stable.scn`
14. `v2_step4_ifr_departure_no_arrival_prefetch.scn`
15. `v2_step4_ifr_departure_primary_only.scn`
16. `v2_step4_lifecycle_boundaries.scn`
17. `v2_step4_lookup_completion_no_delayed_reopen.scn`
18. `v2_step4_lookup_drawer_switch_respected.scn`
19. `v2_step4_lookup_manual_close_respected.scn`
20. `v2_step4_lookup_no_periodic_refresh.scn`
21. `v2_step4_lookup_orb_primary_authority.scn`
22. `v2_step4_lookup_pending_failure_returns_primary.scn`
23. `v2_step4_lookup_pending_fetching_presentation.scn`
24. `v2_step4_lookup_pending_success_to_spotlight.scn`
25. `v2_step4_lookup_preserves_primary.scn`
26. `v2_step4_lookup_replacement_invalidates_pending.scn`
27. `v2_step4_lookup_spotlight_activation.scn`
28. `v2_step4_lookup_spotlight_expiry_no_duplicate.scn`
29. `v2_step4_lookup_truthful_newest_history.scn`
30. `v2_step4_metar_speci_parsing.scn`
31. `v2_step4_normal_binary_source_isolation.scn`
32. `v2_step4_observation_time_month_boundary.scn`
33. `v2_step4_official_vatsim_url_only.scn`
34. `v2_step4_orb_category_text_and_tone.scn`
35. `v2_step4_pinned_primary_not_history.scn`
36. `v2_step4_primary_state_change_preempts_spotlight.scn`
37. `v2_step4_primary_update_preempts_spotlight.scn`
38. `v2_step4_request_priority_and_backoff.scn`
39. `v2_step4_speci_between_clock_boundaries.scn`
40. `v2_step4_stale_primary_gray_unknown.scn`
41. `v2_step4_strict_icao_validation.scn`
42. `v2_step4_target_switch_rejects_stale_completion.scn`
43. `v2_step4_transport_response_rejections.scn`
44. `v2_step4_unchanged_content_zero_publication.scn`
45. `v2_step4_vatsim_json_extraction.scn`
46. `v2_step4_vatsim_raw_optional_prefixes.scn`
47. `v2_step4_vfr_brain_committed_departure_source.scn`
48. `v2_step4_vfr_departure_primary.scn`
49. `v2_step4_vfr_missing_departure_unknown.scn`
50. `v2_step4_vfr_no_nearby_scan.scn`
51. `v2_step4_visibility_formats.scn`
52. `v2_step4_warm_unchanged_zero_work.scn`
53. `v2_step4_worker_prompt_cancel_join.scn`

## Performance and shutdown

- Timing separation fixture: network wait 2,500,000 microseconds; brain
  simulator-thread acceptance 7 microseconds.
- Warm unchanged proof: 100,000 brain cycles in 67,987 microseconds and 100,000
  presentation updates in 1,633 microseconds.
- Warm deltas: parsing 0, fingerprinting 0, history 0, wrapping 0,
  rasterization 0, uploads 0, and snapshot publication 0.
- Cooperative cancellation: five independently identified phases; maximum
  observed cancel/join latency 15 ms against a 500 ms limit; no running worker,
  open handle, or open callback state remained.
- A real local loopback peer accepted a production WinHTTP request and withheld
  its response; request-handle closure interrupted the wait and joined in 0 ms
  with callback and handle closure confirmed.

## Deterministic visual proof

The production brain projection, accessory layout, wrapping, presentation, and
Windows raster path generated 14 PNG cases in
`outputs/v2_step_04_metar_visual_evidence`.

- Repeat run PNG comparisons: 14 compared, 0 hash differences.
- Main-card production signature: `8949928878432326300` in all cases.
- Final-run projection range: 2–10 microseconds.
- Final-run wrapping range: 10–616 microseconds.
- Final-run presentation range: 3–24 microseconds.
- Final-run raster range: 2,936–3,269 microseconds.
- Visual manifest SHA-256:
  `E75427A54FE739C7383D645FAE7B1C86352173969E5F945C456A69BD7F8478D7`.

The performance CSV is deliberately timing-sensitive and is not expected to
hash-identically across runs; deterministic status applies to all 14 PNGs.

## Deferred proof

No claim of live VATSIM availability, in-simulator interaction, or X-Plane frame
performance is made. Controlled live proof, deployment, and rollback from such
a deployment require separate authorization.
