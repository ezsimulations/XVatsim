# Performance Contract Gate A — Controlled Live Final Report

Date: 2026-09-01 (America/Los_Angeles)

Flight: UAL472, KBOS to KORD. The captured session covers connected ground operation, departure, an approach-to-advisory handoff, transition to ENROUTE mode, 52.6 NM of tracked route progress, normal xPilot disconnect, and normal X-Plane shutdown.

## Final verdict

Gate A's primary purpose is proven live: no synchronous authority computation caused a flight-loop spike. The asynchronous worker absorbed expensive authority evaluations, while the simulator-thread authority stage remained bounded to approximately one millisecond.

The controlled deployment was safe and materially successful. Normal shutdown completed without a deadlock, crash, stale publication, or leaked authority worker. The plugin reported `workerRunning=0`, emitted lifecycle invalidations, unloaded, and stopped normally.

Gate A is not ready for permanent closeout or commit. Two hardening findings remain: deterministic five-second transceiver-identity churn and a live authority preparation/submit P99 of 1.000 ms against the strict 250-microsecond contract. The overall established P99 was also 3.040 ms, a 40-microsecond miss against the 3.000-ms product target, although the bounded ENROUTE P99 passed at 2.827 ms and no established callback exceeded 10 ms.

No rollback is indicated. No Gate B or source change was made during this deployment.

## Final artifacts

- `07_postshutdown_diagnostics_20260901_095630.log`
  - SHA-256: `0769D4915FBED1E27B4D45670AE4F67A3E2D9A0AAFABD2B0D6087A2C7A482373`
- `08_X-Plane_Log_postshutdown_20260901_095630.txt`
  - SHA-256: `D219CE6A68559F49201FA4B2E26E7F5C53B11B06C14FAB11E158D662A4EF0E45`
- `10_enroute_watch_samples_20260901_095200.csv`
- Deployed XVatsim binary SHA-256: `790ED5EF437FC26A5AEB8FEFE6B211DAF192AE1ACF7E2733530C633FE5E4BA86`
- Rollback binary SHA-256: `CA2840F6FE61124E37881124DF78E4B0A0049FD47C5BF4FA6EE179D9A6E77934`

## Performance results

### Bootstrap classification

The only X-Plane `Perf warning` was the initial 1,170.609-ms callback after route data arrived. Its measured route expansion was 1,169.301 ms; main-thread authority handling was 523 microseconds. This is Gate B evidence and not a Gate A regression.

### Established session

| Measurement | Result | Contract assessment |
|---|---:|---|
| Instrumented callbacks after bootstrap | 1,393 | Substantial sample |
| P50 | 1.184 ms | Pass |
| P95 | 2.427 ms | Pass |
| P99 | 3.040 ms | Near miss: 0.040 ms above target |
| Maximum | 3.987 ms | Below 10-ms hard gate |
| Callbacks over 10 ms | 0 | Pass |

### Moving-aircraft window

The moving window covered 2,725 seconds and 1,170 instrumented callbacks.

| Measurement | Result |
|---|---:|
| Moving P99 | 3.096 ms |
| Moving maximum | 3.987 ms |
| Moving callbacks over 10 ms | 0 |
| Authority-stage P99 | 1.024 ms |
| Authority-stage maximum | 1.213 ms |
| Route-stage maximum | 0 ms |

The callbacks near 3–4 ms were combinations of flight-plan sampling, radio-range work, authority packaging/harvest, and small untracked overhead. They were not worker computations on the simulator thread.

### ENROUTE window

ENROUTE mode began at diagnostic tick 1732492 and remained active through disconnect. The captured ENROUTE interval was 435 seconds with 206 instrumented callbacks and route progress from approximately 8.2 NM to 52.6 NM.

| Measurement | Result | Assessment |
|---|---:|---|
| P50 | 1.145 ms | Pass |
| P95 | 2.384 ms | Pass |
| P99 | 2.827 ms | Pass |
| Maximum | 3.226 ms | Below 10-ms hard gate |
| Callbacks over 10 ms | 0 | Pass |
| Authority-stage P99 | 1.024 ms | No authority spike |
| Authority-stage maximum | 1.082 ms | No authority spike |
| Route-stage maximum | 0 ms | Stable route |

The approach/advisory handoff changed radio candidates and the Brain progressed from departure to ENROUTE without route reconstruction or a performance warning.

## Asynchronous authority engine

| Measurement | Result | Assessment |
|---|---:|---|
| Dispatches | 856 | Excessive due to identity churn |
| Harvested terminals | 855 | One latest request invalidated during shutdown |
| Published completions | 697 | Exact identity only |
| Rejected completions | 158 | All controller-digest mismatch |
| Stale publications | 0 | Pass |
| Maximum pending depth | 1 | Pass |
| Reported worker cancellations before shutdown | 0 | No runtime cancellation pressure |
| Mailbox/main-scope P50 | 11 microseconds | Pass |
| Mailbox/main-scope P95 | 885 microseconds | Strict microbudget miss |
| Mailbox/main-scope P99 | 1,000 microseconds | Strict microbudget miss |
| Mailbox/main-scope maximum | 1,305 microseconds | No spike, but above contract |
| Worker P99 | 14.400 ms | Off-thread |
| Worker maximum | 682.117 ms | Initial off-thread computation |
| Unique route digests | 1 | Stable |
| Unique dataset identities | 1 | Stable |
| Unique transceiver digests | 856 | Every dispatch changed |
| Median dispatch interval | 5 seconds | Confirms deterministic churn |

The normal shutdown snapshot contained one latest request without a harvested terminal. Lifecycle invalidation discarded it, and the subsequent plugin-admin suspend record reported `workerRunning=0`. This is the expected bounded shutdown behavior, not a leak or stale publication.

## Remaining hardening work

### 1. Stable authority-specific transceiver identity

The transceiver digest currently hashes continuously changing and diagnostic fields such as candidate distance/score, aircraft distance, feed age, and fetch-in-progress state. Radio-board refresh republishes that digest, making the authority identity change about every five seconds even when route and dataset evidence are stable.

Create an authority-specific digest containing only facts capable of changing authority output. Preserve the full owned transceiver snapshot as worker input, but do not invalidate authority for diagnostic age/fetch state or insignificant continuous distance changes. Tests should prove that an unchanged authority fixture remains idle across repeated five-second radio refreshes.

### 2. Live complete preparation/submit microbudget

The raw/common dispatch path is about 11 microseconds, but controller-evidence changes produce approximately 0.8–1.3 ms samples in the instrumented `RefreshBrainAuthorityRelevanceSnapshot` scope. Determine the exact contributor, keep accepted-snapshot retirement off-thread, and require the live P99 to meet the 250-microsecond contract using the same full-scope instrumentation.

### 3. Overall 3-ms P99

The full established window missed the 3-ms P99 target by 40 microseconds, while the focused ENROUTE window passed. Treat this as a measured near miss, not a Gate A authority regression. Retest after the five-second churn is removed before deciding whether any separate product-level optimization is required.

## Evidence limitations

The route remained on KZBW with KZOB next; no center-polygon transition occurred before the bounded flight ended. The route digest remained unchanged, so this flight did not exercise late FMS enrichment. Those two scenarios remain unproven live. Offline delay, parity, stale-result, queue-depth, and lifecycle tests remain the evidence for those paths until a later focused flight naturally supplies them.

## Memory and lifecycle conclusion

Whole-process memory varied with flight/scenery and cannot isolate XVatsim allocations. The early ground working set rose and then dropped, so it did not show a monotonic Gate A leak. During the final moving sample, memory stepped upward concurrently with route progress, which is consistent with normal X-Plane/scenery allocation but is not attributable from process-wide counters.

The stronger Gate A lifecycle evidence is direct: bounded queue depth, zero stale publications, normal disconnect handling, `workerRunning=0` at plugin disable, clean plugin unload, and `[XVatsim] Plugin stopped.` No leaked authority thread or shutdown failure was observed.

## Recommended decision

Accept the live proof that Gate A eliminated authority-caused flight-loop stalls. Keep Gate B closed except for the recorded 1.169-second initial route-build evidence. Do not commit or declare full Gate A closeout until the narrow transceiver-identity churn and live submit-scope microbudget findings are corrected and re-proved.
