# Gate A-Calm 1 Controlled Live Report

Date: 2026-09-01
Flight identity: UAL250
Route: MMGL to KLAX
State exercised: connected idle, plugin disable, plugin re-enable, xPilot disconnect, normal X-Plane shutdown
Deployed binary SHA-256: `254A5AA8E2FFB9BD8E2DF17696AE89A8A146304C0E113C84DF1AD74970C6D538`

## Verdict

Gate A-Calm 1 passes its live performance and lifecycle objectives. Complete callback timing is now observable, diagnostic submission is inexpensive, storage and formatting remained off the simulator thread, and every record accepted by the writer was drained during normal shutdown.

Overall product calm is not yet complete. The run reproduced two synchronous route rebuilds, including a 106.072 ms callback after plugin re-enable. That event is Gate B evidence under the approved acceptance distinction and is not an authority or Gate A-Calm 1 regression.

The run also provides strong evidence for the already-planned Gate A-Calm 2 semantic-input correction: authority requests continued at the five-second radio cadence even when useful authority evidence was unchanged.

No source or binary was changed during this diagnostic run. No commit was created.

## Complete callback results

| Window | Samples captured | P50 | P95 | P99 | Maximum | >3 ms | >10 ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| Whole session | 3,668 | 0.276 ms | 0.952 ms | 2.107 ms | 1,423.532 ms | 8 | 2 |
| Initial settled window, 30 seconds after bootstrap through plugin disable | 2,013 | 0.277 ms | 0.915 ms | 2.104 ms | 3.586 ms | 2 | 0 |
| Settled after plugin re-enable | 494 | 0.282 ms | 0.941 ms | 2.297 ms | 3.578 ms | 1 | 0 |
| After xPilot disconnect | 270 | 0.245 ms | 0.951 ms | 1.565 ms | 1.730 ms | 0 | 0 |

The writer emitted callback sequence numbers 1 through 3,669. Sequence 2,003 was not accepted because a producer lost the nonblocking mailbox contention race, leaving 3,668 persisted callback records and one detectable timing gap.

### Callbacks above 3 ms

| Sequence | Complete time | Classification |
|---:|---:|---|
| 1 | 9.679 ms | Startup transient before flight-plan bootstrap |
| 470 | 7.630 ms | Established transient, below product 10 ms limit |
| 481 | 3.316 ms | Established transient |
| 650 | 1,423.532 ms | Initial route bootstrap; route stage 1,422.405 ms |
| 1,236 | 3.586 ms | Established transient |
| 2,530 | 3.161 ms | Established transient |
| 2,788 | 106.072 ms | Plugin re-enable route rebuild; route stage 99.603 ms and network-plan stage 4.707 ms |
| 3,077 | 3.578 ms | Established transient |

Only the two route-build callbacks exceeded 10 ms. The 1.423-second initial callback was the explicitly permitted bootstrap. The later 106 ms route rebuild prevents overall product closure but belongs to Gate B.

## Gate A authority results

| Measure | Result | Contract |
|---|---:|---:|
| Main-thread authority dispatch P50 | 7 us | — |
| Main-thread authority dispatch P95 | 14 us | — |
| Main-thread authority dispatch P99 | 16 us | <250 us |
| Main-thread authority dispatch maximum | 33 us | <250 us target comfortably retained |
| Main-thread authority relevance P99 | 16 us | <250 us |
| Main-thread authority relevance maximum | 18 us | — |
| Worker queue maximum pending depth | 1 | 1 |
| Accepted and published completions | 158 | — |
| Rejected completions | 29 | — |
| Stale completions published | 0 | 0 |
| Authority worker P99 | 422.884 ms | Off-thread; no flight-loop wait |
| Authority worker maximum | 480.470 ms | Off-thread; no flight-loop wait |

All 29 rejected completions were rejected for `controller-digest-mismatch`; none was published. The long worker times did not appear in complete-callback timing.

## Gate A-Calm 2 evidence

The live stream persisted 186 authority-dispatch records. Two dispatch records were among the 13 nonblocking writer contention drops, so request identifiers reached 188.

- 139 adjacent dispatch intervals were exactly five seconds.
- 45 additional redispatches occurred in the same tick as another request.
- All 186 persisted dispatches had distinct transceiver digests.
- Controller digest changed 48 times and produced 49 distinct persisted controller digests.
- Controller digest changes caused all 29 stale-result rejections.

This is direct evidence that volatile radio/controller refresh representation is still being treated as semantic authority evidence. It is now safe for X-Plane because the expensive computation is asynchronous, but it wastes worker CPU and creates avoidable replacement/rejection traffic.

Recommended next action remains the separately authorized Gate A-Calm 2 design:

1. Create a dedicated, immutable authority transceiver projection containing only evidence used by authority evaluation.
2. Exclude volatile radio diagnostics, refresh metadata, ordering noise, and health timestamps from the authority digest.
3. Replace controller generation invalidation with a digest of controller fields that authority evaluation actually consumes.
4. Add counters for refreshes observed, semantic evidence changes, dispatches suppressed, and dispatches issued.
5. Prove unchanged five-second refreshes cause zero requests while real controller/transceiver changes still invalidate immediately.

## Diagnostics writer results

Shutdown counters:

| Counter | Result |
|---|---:|
| Accepted routine submissions | 4,699 |
| Accepted critical submissions | 3 |
| Dequeued | 4,702 |
| Written | 4,702 |
| Producer contention drops | 13 |
| Routine queue-full drops | 0 |
| Critical queue-full drops | 0 |
| Formatting failures | 0 |
| Storage failures | 0 |
| Maximum queue depth | 3 |
| Maximum producer call | 63 us |

The live result confirms that accepted diagnostics drain exactly and that filesystem activity does not block the flight loop. Complete-callback diagnostic submission P99 was 5 us for the whole session and 2 us after xPilot disconnect.

The 13 `try_lock` contention drops are not a flight-performance failure; dropping is how the mailbox preserves the literal nonblocking contract. They are an evidence-completeness observation. One of those drops was complete-callback sequence 2,003. Before formal diagnostics closeout, consider a reserved nonblocking timing lane or per-producer SPSC lanes so callback timing cannot be displaced by concurrent worker diagnostics. Do not replace this behavior with a waiting mutex.

## Lifecycle results

- Plugin disable stopped the callback stream, invalidated authority state, stopped the authority worker, and preserved flight context.
- Plugin re-enable continued the diagnostic sequence, restarted workers, advanced lifecycle identity, rebuilt the route, and accepted only the matching authority completion.
- xPilot disconnect removed authority-dependent data, left the authority worker idle, and produced no authority dispatch, terminal, or publication after the disconnect boundary.
- Normal shutdown invalidated authority state, stopped workers, drained the diagnostics writer, unloaded XVatsim, and ended with X-Plane's `Clean exit from threads` record.

## Process-wide handle and memory isolation

X-Plane's total handle count rose at approximately one handle per second. The controlled plugin-disable comparison did not attribute that trend to XVatsim:

| State | Observed net handle slope |
|---|---:|
| XVatsim active | approximately 0.95 handles/second |
| XVatsim disabled, second clean interval | 0.983 handles/second |
| XVatsim re-enabled | 1.05 handles/second |

The first disabled interval included a batch release and had a lower net slope, but the following clean disabled interval matched the active rate. Thread count remained flat within each interval. Process private memory fluctuated and did not establish a monotonic XVatsim leak. The handle behavior belongs to X-Plane or another loaded component and should not be assigned to an XVatsim performance gate without a broader plugin-by-plugin isolation run.

## Acceptance decision

| Requirement | Result |
|---|---|
| Complete settled callback P99 <=3 ms | Pass: 2.104 ms initial settled; 2.297 ms post-enable settled |
| No established callback >10 ms attributable to Gate A authority or diagnostics | Pass |
| Authority main-thread P99 <250 us | Pass: 16 us |
| Diagnostic submission remains nonblocking | Pass: 5 us P99, 63 us maximum producer call |
| Slow/unavailable storage cannot delay X-Plane | Pass from offline forced-fault proof; live storage had zero failures |
| Worker queue one running plus at most one pending | Pass |
| Zero stale authority publications | Pass |
| Clean disable/re-enable/disconnect/shutdown | Pass |
| Overall product: no established callback >10 ms | Not yet: 106.072 ms route rebuild is Gate B evidence |
| Zero five-second authority churn | Not yet: Gate A-Calm 2 evidence |

## Recommended sequencing

1. Treat Gate A-Calm 1 performance and lifecycle behavior as passed.
2. Decide whether the single missing callback timing record requires the narrow nonblocking timing-lane hardening before formal Gate A-Calm 1 closeout. It does not justify reopening authority or route architecture.
3. Open Gate A-Calm 2 next to eliminate semantic-input churn and repeat the offline/live proof.
4. Complete the already-planned Product-Calm 1 exact bidirectional FMS endpoint scan, preserving late enrichment and all fallbacks, then remeasure.
5. Open Gate B only afterward for asynchronous latest-only route preparation and route-substage instrumentation.
6. Keep the process-wide handle trend outside XVatsim scope unless a broader plugin isolation test attributes it to XVatsim.

## Preserved raw evidence

- `xvatsim_diagnostics_2026_09_01_complete_daily.log`
- `X-Plane_Log_UAL250_MMGL_KLAX.txt`

The current controlled session begins at diagnostics line 5,993 with `event=diagnostics-session-start` and ends with `event=diagnostics-writer-stop`.
