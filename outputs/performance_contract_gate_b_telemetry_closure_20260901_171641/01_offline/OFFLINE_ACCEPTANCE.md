# Gate B Telemetry Closure offline acceptance

Date: 2026-09-01
Status: **PASS; candidate ready for controlled deployment**

This amendment changes telemetry only. It does not change route scheduling, route preparation, identity, cancellation, source observation, or publication behavior.

## Implementation proof

- Expanded-FMS observation wall time is measured around the worker-owned `ObserveExpandedFms` call and carried in the immutable worker fact.
- Windows worker CPU time is sampled with `GetThreadTimes`; the fact explicitly carries availability and emits `available` or `unavailable`.
- Dispatch, terminal, disposition, lifecycle, identity, timing, and rebuild-decision evidence uses a dedicated preallocated fixed-capacity SPSC lane.
- The flight-loop producer performs no mutex acquisition, waiting, heap allocation, formatting, or filesystem work.
- Ordinary human-readable diagnostics remain droppable and have independent counters.
- Route-evidence submission, dequeue, write, full, not-running, depth, producer-time, and sequence counters are independent.

## Focused results

- Gate A: pass; package-and-submit P99 `4 us`, harvest P99 `1 us`, stale publications `0`.
- Gate A-Calm 1: pass.
- Gate A-Calm 2: pass; unchanged five-second requests `0`.
- Product-Calm 1: pass; `8,191` equivalence cases.
- Gate B: pass; submit P99/max `43/50 us`, harvest P99/max `4/34 us`, transition P99/max `15/658 us`, maximum pending depth `1`, stale publications `0`.
- Expanded-FMS observation timing: worker wall maximum `80 us`; Windows CPU measurement available.
- Gate B telemetry: pass; producer P99/max `1/1 us`, `99/99` evidence records written, exact fixed capacity reached, full drops `0`, during-session not-running rejects `0`, sequence gaps `0`.
- Forced two-second writer delay: pass.
- Forced 100 ms routine-writer contention: pass; routine contention observed without route-evidence loss.
- Shutdown drainage: `8/8` accepted evidence records persisted before stop.
- Post-stop submission: rejected and counted as not-running.

## Complete regression and parity

- Release plugin and harness: pass.
- Frozen regression: `861/861`.
- Exact route-oracle parity: `64/64`.
- Combined authority/route parity: `64/64`.
- `git diff --check`: pass (line-ending notices only).

## Candidate

- Plugin bytes: `2,755,072`.
- Plugin SHA-256: `CC42CD97A2655C1D8FB445FD128C85833E82BB7D535490EA4B08F310B2C5F512`.
- Harness bytes: `4,415,488`.
- Harness SHA-256: `6AC1E633599FC52FC5D1321F466D9D400E5DCCA82F98172826326795C04AEAB4`.

Deployment remains controlled and rollback-protected. No live-flight setup is requested until deployment hash verification and startup smoke testing are complete.
