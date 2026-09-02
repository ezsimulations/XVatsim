# Performance Contract Gate B — Telemetry Closure implementation and live reproof

Date: 2026-09-01  
Branch: `v2-development`  
Baseline HEAD: `9440d6245f63e52b47de5fba99c6041ecfdcc31e`  
Candidate SHA-256: `CC42CD97A2655C1D8FB445FD128C85833E82BB7D535490EA4B08F310B2C5F512`  
Status: **Gate B runtime, performance, and evidence closure PASS; commit held**

## Outcome

The narrow telemetry amendment closes the Phase B4 evidence gap without changing the Gate B route engine. Worker wall and CPU timing now cover every expanded-FMS observation, and a dedicated fixed-capacity SPSC lane preserves route dispatch, terminal, disposition, identity, timing, rebuild-decision, and lifecycle evidence independently of droppable human-readable diagnostics.

The complete offline matrix and the shortened controlled live reproof pass. There is no evidence of a route-related flight-loop spike, stale publication, queue violation, ownership leak, diagnostic loss, or shutdown fault. No further route-performance engineering is warranted from the measured periodic background cost.

## Implemented amendment

- `ObserveExpandedFms` is measured inside the route worker using steady-clock wall time.
- Windows thread CPU time is sampled with `GetThreadTimes`; availability is explicit in the immutable completion fact and persisted evidence.
- The route-evidence producer is fixed-size and nonblocking: no producer mutex, allocation, formatting, filesystem access, or waiting.
- The writer owns formatting and storage.
- Route-evidence submission, dequeue, write, full, not-running, queue-depth, producer-time, and sequence counters are separate from routine diagnostics.
- Shutdown waits for accepted evidence to drain before capturing final counters.

No route algorithm, resolver behavior, identity, cadence, cancellation, publication, or UI functionality was redesigned.

## Offline acceptance

- Release plugin and harness: pass.
- Frozen regression: `861/861`.
- Exact route-oracle parity: `64/64`.
- Combined authority/route parity: `64/64`.
- Gate A, Gate A-Calm 1, Gate A-Calm 2, Product-Calm 1, and Gate B probes: pass.
- Telemetry probe: `99/99` records written while the lane reached exact capacity under a forced two-second writer delay and 100 ms routine-writer contention; P99/max producer time `1/1 us`; zero gaps and losses; shutdown drain `8/8`.
- `git diff --check`: pass, with line-ending notices only.

## Controlled live reproof

The connected test used `SWA1200`, `KAUS` to `KSAN`.

- Complete callback evidence: `1,481/1,481`, contiguous.
- Whole-session callback P99: `1.706 ms`.
- The only callback over 10 ms was the first `12.260 ms` bootstrap callback, before any route request.
- Established-flight callback P99/max: `1.434/5.609 ms`; none above 10 ms.
- Initial and re-enable route builds: `38.219/38.837 ms`, both off-thread.
- Route preparation dispatches/publications: `2/2`; stale publications `0`.
- Expanded-FMS observations: `15`; unchanged `13`; unchanged-triggered rebuilds `0`.
- All observation CPU measurements were available.
- Unchanged scan P50: `25.788 ms`; stable post-warm range `21.980..32.500 ms`.
- The one `122.575 ms` first unchanged scan was background-only and correlated with a `0.371 ms` callback maximum.
- Maximum callback in any observation completion second: `0.563 ms`.
- Route complete package-and-submit P99/max: `92/92 us`.
- Mailbox P99/max: `5/5 us`; harvest max `85 us`; transition max `71 us`.
- Maximum route queue depth: `1`.

### Complete callback-above-3-ms classification

The controlling contract requires every complete callback above `3 ms` to be classified. The four such callbacks are:

| Sequence | Complete callback | Classification | Route contribution |
|---:|---:|---|---:|
| 1 | `12.260 ms` | Bootstrap/session reset | `0` |
| 293 | `9.425 ms` | No-flight-context flight-plan sampling | `0` |
| 449 | `7.801 ms` | Initial connection processing | `89 us` |
| 1007 | `5.609 ms` | Re-enable coordination/FMS observation dispatch | `52 us` submit, `1 us` mailbox |

None was caused by synchronous route preparation or expanded-FMS observation. The two route builds remained entirely on the worker, and no established callback exceeded `10 ms`.

The initial discovery consumed `1.031 s` of measured worker CPU, which is inside the permitted bootstrap and never entered the flight loop. The unchanged observations consumed `375 ms` of CPU in total, approximately `0.19%` of one core at their 15-second cadence. This is not excessive periodic background cost and caused no correlated callback or rebuild disturbance.

## Evidence closure and shutdown

- Route-evidence submitted/dequeued/written: `58/58/58`.
- Route-evidence sequences: `1..58`, zero gaps.
- Timing submitted/dequeued/written: `1,481/1,481/1,481`.
- Timing sequences: `1..1481`, zero gaps.
- Route-evidence/timing full drops: `0/0`.
- Route-evidence/timing not-running rejects: `0/0`.
- Storage and formatting failures: `0/0`.
- Ordinary routine contention drops: `5`; these were deliberately droppable and did not affect contract evidence.
- Worker requests: starts/completions `17/17`; pending, failures, and stale retirements `0` at stop.
- Route leases: created/retired/destroyed on worker `2/2/2`; outstanding `0`.
- Retained route bytes, source datasets, and retirement backlog: `0`.
- Diagnostics drained before stop.
- X-Plane unloaded XVatsim and reported a clean thread exit and normal shutdown.

## Deployment and rollback state

The active installed candidate still hashes exactly to `CC42CD97A2655C1D8FB445FD128C85833E82BB7D535490EA4B08F310B2C5F512`.

The Product-Calm rollback is preserved both in the evidence set and under the plugin's `V2 Test/performance_contract_gate_b_telemetry_closure_20260901_171641` directory. Its SHA-256 remains `3D8E35CBD3B0FAA96CEB24D5422BD73ECB928F8A7F9B7417249FA994E4CD4E70`.

No rollback trigger fired, so the telemetry candidate remains installed pending independent final closeout. No source or binary was committed or staged.

## Evidence

All manifests, offline proof, deployment receipts, startup smoke logs, intermediate lifecycle snapshots, final diagnostics, X-Plane log, observation correlation, installed candidate, and rollback payload are under:

`outputs/performance_contract_gate_b_telemetry_closure_20260901_171641/`
