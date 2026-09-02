# Gate B Telemetry Closure final runtime metrics

Date: 2026-09-01
Flight: `SWA1200`, `KAUS` to `KSAN`
Result: **PASS**

## Flight-loop timing

- Complete records: `1,481/1,481`, sequences `1..1481`, zero gaps.
- Whole-session P99: `1.706 ms`.
- Whole-session maximum: `12.260 ms`, the first bootstrap callback before any route request.
- Post-initial-route P99: `1.434 ms`.
- Post-initial-route maximum: `5.609 ms`.
- Post-initial-route callbacks above 10 ms: `0`.
- Performance warnings: `0`.

## Route and FMS work

- Initial route build: `38.219 ms` on the worker; completion-second callback maximum `2.812 ms`.
- Re-enable route build: `38.837 ms` on the worker; completion-second callback maximum `0.341 ms`.
- Route preparations: `2`, both published with the same exact result digest.
- Expanded-FMS observations: `15`; accepted `15`; CPU timing available `15/15`.
- Changed observations: `2`, exactly the initial and re-enable observations; each caused one required route build.
- Unchanged observations: `13`; route rebuilds `0`.
- Unchanged wall P50 / P99 / maximum: `25.788 / 122.575 / 122.575 ms`.
- After the first warm observation, unchanged wall range: `21.980..32.500 ms`, mean `25.703 ms`.
- Unchanged worker CPU total: `375 ms`; approximately `0.19%` of one core at the 15-second cadence including the warm outlier.
- Same-second callback maximum across all observation completions: `0.563 ms`.

The `1.043 s` initial FMS discovery is the explicitly permitted bootstrap and remained entirely below-normal-priority worker work. The first unchanged observation was a one-time `122.575 ms` warm scan. The subsequent stable background cost does not justify additional route-performance engineering: it averages roughly `22 ms` of measured CPU every 15 seconds and produced no callback disturbance or rebuild.

## Main-thread and queue gates

- Complete desired-identity/package/submit P99 / maximum: `92 / 92 us`.
- Mailbox exchange P99 / maximum: `5 / 5 us`.
- Harvest maximum: `85 us`; therefore observed harvest P99 is also at most `85 us`.
- Transition maximum: `71 us`; therefore observed transition P99 is also at most `71 us`.
- Maximum pending work depth: `1`.
- Stale publications: `0`.
- Failures and cancellations: `0`.

## Evidence and ownership

- Route-evidence records: submitted/dequeued/written `58/58/58`, sequences `1..58`, zero gaps.
- Route-evidence full drops / not-running rejects: `0/0`.
- Timing full drops / not-running rejects: `0/0`.
- Route-evidence producer maximum: `15 us`.
- Diagnostics storage / formatting failures: `0/0`.
- Ordinary routine contention drops: `5`; intentionally droppable and isolated from timing/evidence lanes.
- Route leases created/retired/destroyed on worker: `2/2/2`.
- Outstanding leases, request nodes, retained route bytes, retained source datasets, retirement backlog: all `0` at shutdown.
- Writer drained before stop: `1`.
- X-Plane reported plugin unload, clean thread exit, and normal shutdown.

## Binary state

- Active telemetry candidate SHA-256: `CC42CD97A2655C1D8FB445FD128C85833E82BB7D535490EA4B08F310B2C5F512`.
- Preserved Product-Calm rollback SHA-256: `3D8E35CBD3B0FAA96CEB24D5422BD73ECB928F8A7F9B7417249FA994E4CD4E70`.
- No rollback trigger fired. The candidate remains installed pending independent final closeout.
- No files were staged and no commit was created.
