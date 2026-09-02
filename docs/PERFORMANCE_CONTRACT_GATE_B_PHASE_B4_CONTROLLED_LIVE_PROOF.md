# Performance Contract Gate B Phase B4 Controlled Live Proof

Date: 2026-09-01  
Flight: SWA1211, KMSP to KOAK  
Candidate SHA-256: `1EB42038D440721E7DEB906C95031DFE270C78DE1BACC4376800BF63721BB4C0`  
Evidence root: `outputs/performance_contract_gate_b_b4_20260901_161744`

## Verdict

**Runtime behavior: PASS. Formal Phase B4 closeout: HOLD. Controlled rollback: COMPLETE.**

Gate B removed route construction from the X-Plane flight loop in this controlled session. The complete callback P99 was 1.665 ms, no callback exceeded 10 ms, initial and re-enable route builds completed on the worker, periodic unchanged FMS observations did not rebuild the route, stale publication stayed at zero, ownership drained to zero, and X-Plane shut down cleanly.

The literal Phase B4 evidence contract was not completely satisfied:

1. The asynchronous diagnostics writer reported six routine contention drops. One was the route-dispatch record for re-enable request 25. Its terminal, exact-identity publication, worker timing, and aggregate dispatch accounting are present, but the individual dispatch line is absent.
2. The observation completion record does not carry worker wall time or worker CPU time. Therefore each periodic observation can be correlated exactly with callback timing and disposition, but its worker execution cost cannot be reported exactly from live telemetry.

The approved contract required rollback for diagnostic loss. The tested candidate was preserved, and the exact Product-Calm binary was restored. This is an evidence-compliance rollback, not a route-engine performance or correctness failure.

## Deployment and rollback integrity

| Artifact | SHA-256 | Size |
|---|---|---:|
| Authorized B3.1 candidate | `1EB42038D440721E7DEB906C95031DFE270C78DE1BACC4376800BF63721BB4C0` | 2,742,784 bytes |
| Deployed candidate after live test | `1EB42038D440721E7DEB906C95031DFE270C78DE1BACC4376800BF63721BB4C0` | 2,742,784 bytes |
| Preserved Product-Calm rollback source | `3D8E35CBD3B0FAA96CEB24D5422BD73ECB928F8A7F9B7417249FA994E4CD4E70` | 2,689,024 bytes |
| Active installed binary after rollback | `3D8E35CBD3B0FAA96CEB24D5422BD73ECB928F8A7F9B7417249FA994E4CD4E70` | 2,689,024 bytes |

The repository remained at `9440d6245f63e52b47de5fba99c6041ecfdcc31e`. Nothing was staged or committed.

## Complete callback timing

The final diagnostics session contains exactly 3,281 timing records, numbered 1 through 3,281, with zero gaps and zero duplicates.

| Phase | Records | P99 | Maximum | Over 3 ms | Over 10 ms |
|---|---:|---:|---:|---:|---:|
| Entire session | 3,281 | 1.665 ms | 8.525 ms | 4 | 0 |
| Pre-flight-plan/bootstrap | 702 | 1.875 ms | 8.045 ms | 1 | 0 |
| Initial route window | 16 | 8.525 ms | 8.525 ms | 2 | 0 |
| Settled before disable | 1,273 | 1.579 ms | 2.404 ms | 0 | 0 |
| Plugin Admin disable boundary | 1 | 0.344 ms | 0.344 ms | 0 | 0 |
| Re-enable route window | 9 | 5.373 ms | 5.373 ms | 1 | 0 |
| Settled after re-enable | 792 | 1.566 ms | 2.765 ms | 0 | 0 |
| Disconnect and post-disconnect | 488 | 1.254 ms | 1.798 ms | 0 | 0 |

Combined connected settled operation contained 2,065 callbacks with P99 1.566 ms, maximum 2.765 ms, and no callback above 3 ms.

The four callbacks above 3 ms were bounded transition work, not synchronous route construction:

| Sequence | Time | Classification |
|---:|---:|---|
| 1 | 8.045 ms | Session/bootstrap transition |
| 704 | 8.525 ms | Initial network-plan intake and asynchronous request packaging |
| 715 | 3.482 ms | Initial asynchronous result publication window |
| 1,996 | 5.373 ms | Plugin Admin re-enable refresh window |

The callback telemetry submission itself measured P99 4 us and maximum 9 us.

## Route worker proof

The worker reported 38 starts and 38 completions: two route preparations and 36 expanded-FMS observations. There were zero replacements, cancellations, failures, and stale retirements. Maximum pending depth was one.

| Event | Worker total | Expanded FMS | Polygon | Result |
|---|---:|---:|---:|---|
| Initial route preparation, request 2 | 39.666 ms | 32.809 ms | 6.821 ms | Exact publication |
| Re-enable route preparation, request 25 | 31.337 ms | 25.493 ms | 5.829 ms | Exact publication |

Both builds used the same network, source, preflight, policy, anchor, and FMS identities and published result digest `6575050664953968528`. The resulting route was six points across KZMP, KZDV, and KZOA.

Main-thread route timing remained below contract limits:

| Timing | Live result | Contract |
|---|---:|---:|
| Complete desired-identity/package/submit P99 | 74 us | <=250 us |
| Complete desired-identity/package/submit maximum | 74 us | <=1 ms |
| Mailbox exchange maximum | 7 us | Nonblocking |
| Harvest maximum | 91 us | <=1 ms; therefore P99 is also <=91 us |
| Transition maximum | 85 us | <=1 ms; therefore P99 is also <=85 us |

The aggregate worker stop record confirms two route dispatches and two accepted publications. One of those individual dispatch records was lost to routine diagnostic contention, but its request 25 terminal and exact publication records are present.

## Periodic expanded-FMS observations

There were 36 accepted observations. Each lifecycle's first observation was classified changed and triggered exactly one route build. The remaining 34 observations were unchanged and triggered zero route builds.

- Observation identity stayed `1640831459027092767`.
- Routine cadence was 15 to 16 seconds, with one 17-second interval immediately after initial route construction.
- Unchanged-observation package-and-submit time was 31 to 43 us.
- The maximum callback at an unchanged observation dispatch or completion tick was 0.779 ms.
- No periodic observation coincided with a callback above 1 ms, a warning, stale publication, or a route rebuild.
- Observation dispatching stopped after xPilot disconnect.

Whole-process X-Plane CPU samples taken during the live watch varied from approximately 27.4 to 49.6 CPU-seconds per 15-second interval and were dominated by the simulator and aircraft. They did not show a repeatable 15-second disturbance correlated with XVatsim callback telemetry. These samples cannot isolate the route worker.

The per-observation correlation is preserved in `04_shutdown/FMS_OBSERVATION_CORRELATION.csv`. Its `workerWallUs` column is deliberately marked `not-instrumented`; the live binary did not emit that required measurement. No claim of exact periodic worker cost is made.

## Lifecycle, stale-result, and ownership proof

- Plugin Admin disable cancelled route lifecycle epoch 4 and invalidated authority state.
- Re-enable created lifecycle epoch 4 work and published only the exact current route identity.
- xPilot disconnect advanced route lifecycle epoch 5 with reason `network-plan-unavailable`; route-dependent and authority-dependent state failed closed.
- Normal shutdown advanced cancellation through plugin-disable, plugin-stop, and session-runtime-reset boundaries.
- Accepted route publications: 2.
- Stale route publications/rejections: 0.
- Maximum route queue depth: 1.
- Request nodes in use at stop: 0; peak: 1.
- Leases created/retired/destroyed on worker: 2/2/2.
- Outstanding leases at stop: 0.
- Retained route bytes at stop: 0; peak: 1,749.
- Retained source datasets at stop: 0; peak: 1.
- Retirement backlog at stop: 0.
- X-Plane recorded `Plugin stopped`, `Plugin unload complete`, `Clean exit from threads`, and the final shutdown marker.

## Diagnostic drainage

| Counter | Result |
|---|---:|
| Submitted timing | 3,281 |
| Written timing | 3,281 |
| Timing-full drops | 0 |
| Timing-not-running rejections | 0 |
| Submitted critical | 3 |
| Critical contention/full drops | 0/0 |
| Routine contention drops | 6 |
| Formatting failures | 0 |
| Storage failures | 0 |
| Maximum routine/critical queue depth | 4 |
| Maximum timing-lane depth | 1 |

Timing proof is mathematically complete. Critical shutdown accounting is complete. Routine diagnostic evidence is not lossless, which is why formal closeout remains on hold.

## Required narrow amendment

Do not redesign the route engine or open another performance gate. Add only the telemetry needed to satisfy the already-approved Phase B4 proof:

1. Measure `ObserveExpandedFms` worker wall time around the worker-owned call and carry it in the immutable completion fact.
2. Where supported, record worker-thread CPU time for the observation; otherwise explicitly report it as unavailable and retain wall time as the required portable metric.
3. Publish one compact observation terminal record containing request identity, lifecycle, observation identity, worker wall time, optional worker CPU time, disposition, and route-rebuild decision.
4. Move route dispatch/terminal/disposition evidence to a fixed-capacity, nonblocking, single-producer timing/evidence lane so these lifecycle records cannot be lost to the routine writer mutex. Keep routine human-readable diagnostics droppable.
5. Add separate counters for route-evidence full/rejected conditions and require both to remain zero.
6. Prove forced routine-queue contention, delayed writer, saturation, disable/re-enable, disconnect, and shutdown without losing route evidence.
7. Re-run the focused Gate B probes and perform a shortened controlled live reproof covering initial route creation, at least four unchanged observations, disable/re-enable, two more unchanged observations, disconnect, and shutdown.

The next live run must not be requested without advance notice. Codex should first finish the build, deployment, hash verification, and startup smoke test, then give Darron a precise flight-preparation checklist and explicitly state when the environment is ready for flight-plan setup.

## Evidence

- `outputs/performance_contract_gate_b_b4_20260901_161744/04_shutdown/xvatsim_diagnostics_final.log`
- `outputs/performance_contract_gate_b_b4_20260901_161744/04_shutdown/X-Plane_Log_final.txt`
- `outputs/performance_contract_gate_b_b4_20260901_161744/04_shutdown/FMS_OBSERVATION_CORRELATION.csv`
- `outputs/performance_contract_gate_b_b4_20260901_161744/04_shutdown/GateB_candidate_after_controlled_live.xpl`
- `outputs/performance_contract_gate_b_b4_20260901_161744/00_predeployment/Product_Calm_active_XVatsim.xpl`

No Gate B source edit, commit, or staging action was performed during Phase B4.
