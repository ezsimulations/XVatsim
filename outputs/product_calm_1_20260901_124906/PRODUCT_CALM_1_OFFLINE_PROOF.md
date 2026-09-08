# Product-Calm 1 Offline and Controlled Live Proof

Date: 2026-09-01

Status: offline implementation, controlled deployment, and controlled live acceptance pass.

## Baseline boundary

- Gate A-Calm 1/2 is formally closed at commit `db2265d17873b492def00311c3e19c27803e0b12`.
- Product-Calm 1 is a separate, uncommitted working change.
- The active X-Plane plugin was not touched until the offline proof was complete.
- Gate B was not opened and no route-construction worker changes were made.

## Implemented correction

The recurring forward scan of as many as 512 FMS entries was replaced by an exact bidirectional endpoint lookup:

- search from the front until the first valid airport is found;
- search from the back until the last valid airport is found;
- never read the same entry twice in one lookup;
- retain the defensive 512-entry limit;
- retain the one-second/movement sample cache and therefore late enrichment;
- retain nearest-airport and GPS destination fallbacks in their original order;
- retain the historical single-airport behavior in which one valid airport supplies both endpoints.

The normal 512-entry fixture with valid endpoints at entries 0 and 511 now performs two `XPLMGetFMSEntryInfo`-equivalent reads instead of 512.

## Focused proof

Release command: `XVatsimRegressionHarness.exe --product-calm-1`

Result: pass.

```text
PRODUCT_CALM_1_PROBE typicalEntries=512 typicalReads=2 emptyReads=512 singleReads=512 lateEnrichmentReads=2 equivalenceCases=8191 benchmarkIterations=50000 benchmarkP99Ns=100 benchmarkMaxNs=200
PRODUCT_CALM_1_PASSED
```

The 8,191-case deterministic equivalence sweep compares every valid/invalid airport pattern for FMS sizes 0 through 12 against the former forward-scan result. It also requires unique bounded reads. Dedicated fixtures cover 512 entries, interior endpoints, one valid airport, no valid airports, an oversized entry count, late endpoint enrichment, and a null reader.

## Protected regression proof

- Complete saved regression: `861/861`, zero failures, 33.339 seconds.
- Exact synchronous/worker authority parity: `64/64`, zero failures, 7.518 seconds.
- Original Gate A focused probe: pass; package-and-submit P99 4 microseconds, submission P99 4 microseconds, harvest P99 12 microseconds, queue depth one, zero stale publications.
- Gate A-Calm 1 focused probe: pass.
- Gate A-Calm 2 focused probe: pass; unchanged five-second requests zero and cold-start output deterministic.
- Release plugin and regression-harness targets: build pass.
- `git diff --check`: pass.

## Candidate artifacts

- Plugin: `build/product-calm-1/dist/XVatsim/win_x64/XVatsim.xpl`
  - bytes: 2,689,024
  - SHA-256: `3D8E35CBD3B0FAA96CEB24D5422BD73ECB928F8A7F9B7417249FA994E4CD4E70`
- Harness: `build/product-calm-1/tools/XVatsimRegressionHarness.exe`
  - bytes: 4,257,792
  - SHA-256: `9B7950CBB04BB70A6B24FB1169D5FAD2209BC8DCD197F0BCE8D3EE800C7B55A6`

## Controlled deployment

X-Plane and xPilot were confirmed stopped before deployment.

- Predeployment active Gate A-Calm binary:
  - bytes: 2,688,000
  - SHA-256: `786FF6D42831457EF570E054B5ECD22D620CDF1BDCCC9A184A8C5C7D725CDA4E`
  - rollback copy: `outputs/product_calm_1_20260901_124906/01_controlled_deployment/predeployment_active/win_x64/XVatsim.xpl`
- Preserved Product-Calm 1 candidate:
  - SHA-256: `3D8E35CBD3B0FAA96CEB24D5422BD73ECB928F8A7F9B7417249FA994E4CD4E70`
  - copy: `outputs/product_calm_1_20260901_124906/01_controlled_deployment/candidate/win_x64/XVatsim.xpl`
- Active deployed plugin:
  - path: `C:/X-Plane 12/Resources/plugins/XVatsim/win_x64/XVatsim.xpl`
  - bytes: 2,689,024
  - verified SHA-256: `3D8E35CBD3B0FAA96CEB24D5422BD73ECB928F8A7F9B7417249FA994E4CD4E70`

## Controlled live acceptance

Route and session: UAL200, KAUS to KMSP, Zibo 737, with X-Plane 12, XVatsim, and xPilot connected. The run covered initial network-plan loading and FMS enrichment, a settled connected period, plugin-admin disable, plugin-admin re-enable, a second settled period, xPilot disconnect, and normal simulator shutdown.

The dedicated timing lane recorded every complete callback:

- timing records: 2,015;
- sequence: contiguous from 1 through 2,015, with no gaps or duplicates;
- written timing records at shutdown: 2,015;
- timing-lane full losses: zero;
- timing records rejected while stopped: zero.

Established-flight performance after the initial route build, excluding only the separately identified re-enable route rebuild, was:

- callbacks: 1,315;
- callback P50: 267 microseconds;
- callback P95: 789 microseconds;
- callback P99: 1,811 microseconds;
- callback maximum: 2,992 microseconds;
- callbacks above 3 milliseconds: zero;
- callbacks above 10 milliseconds: zero.

The connected-only established subsets also remained within contract:

- initial settled connected period, sequences 700 through 1,377: 678 callbacks, P99 1,863 microseconds, maximum 2,992 microseconds, zero above 3 milliseconds;
- post-re-enable settled connected period: 328 callbacks, P99 1,756 microseconds, maximum 2,035 microseconds, zero above 3 milliseconds;
- disconnected period: 305 callbacks, P99 1,541 microseconds, maximum 2,204 microseconds, zero above 3 milliseconds.

The callback's recorded routine diagnostic-submission field was nonblocking in the live run: P99 4 microseconds and maximum 8 microseconds. The dedicated timing-lane producer maximum was 73 microseconds. Four ordinary routine diagnostic records were dropped on contention as designed. No critical or timing record was lost, no queue filled, and there were no formatting or storage failures.

Authority behavior remained a Gate A-Calm pass:

- authority requests: 14;
- exact accepted publications: 14;
- stale, cancelled, or obsolete publications: zero;
- authority dispatch P99 and maximum: 16 microseconds;
- maximum pending depth: one;
- unchanged transceiver refreshes suppressed by the final dispatch: 66;
- controller generation-only refreshes suppressed by the final dispatch: 5.

## Slow-callback classification

Four callbacks in the full startup-to-shutdown record exceeded 3 milliseconds:

| Sequence | Complete time | Classification |
|---:|---:|---|
| 1 | 12.507 ms | plugin/session startup before established flight |
| 529 | 8.578 ms | pre-route bootstrap while network-plan and private-source data were becoming available |
| 699 | 1,115.266 ms | initial expanded route build; `BrainRoutePolygonWorker` consumed 1,114.978 ms |
| 1,382 | 66.586 ms | plugin re-enable expanded route rebuild; route work consumed 60.462 ms and network-plan work 4.509 ms |

The only established callback above 10 milliseconds was the known synchronous route rebuild after re-enable. Authority dispatch consumed 7 microseconds in that callback and the 394.411-millisecond authority computation ran off-thread. The initial route callback likewise spent 6 microseconds on authority dispatch while the 530.209-millisecond authority computation ran off-thread. These two route events are Gate B evidence, not a Gate A-Calm or Product-Calm 1 regression.

## Shutdown proof and preserved evidence

Normal shutdown produced `Plugin stopped`, `Clean exit from threads`, and `X-Plane has shut down`. X-Plane and xPilot process count was zero after closure. Writer-stop accounting was coherent:

- submitted timing / dequeued timing / written timing: 2,015 / 2,015 / 2,015;
- total dequeued / written: 2,181 / 2,181;
- routine contention drops: 4;
- critical contention drops: zero;
- routine, critical, and timing queue-full drops: zero;
- formatting failures: zero;
- storage failures: zero;
- maximum routine queue depth: 2;
- maximum timing depth: 1;
- routine producer maximum: 9 microseconds;
- timing-lane producer maximum: 73 microseconds.

Complete post-shutdown evidence:

- `02_controlled_live_test/05_postshutdown_diagnostics_complete.log`
  - bytes: 12,227,758
  - SHA-256: `E984AE09EDA826277BD6B03C7FA32CDA3DC781C5F8F10452BEC8D26F18592D6D`
- `02_controlled_live_test/05_postshutdown_X-Plane_Log_complete.txt`
  - bytes: 157,820
  - SHA-256: `6D24DA219ABA5F549D8E8172AA5EDC491A331A28B84B99EBF0196FA5602B0FD5`

## Verdict

Product-Calm 1 satisfies its acceptance boundary. The exact endpoint lookup preserves the former endpoint semantics and fallbacks, ordinary settled callback performance is within the 3-millisecond P99 contract, and disable/re-enable, disconnect, and shutdown behavior are clean. Product-Calm 1 is ready for formal closure in a separate scoped checkpoint. Gate B remains unopened and should address only the independently proven synchronous expanded-route construction path.
