# Runtime Activation Gate Phase 1 + B0.1 — Controlled Live Proof

Date: 2026-09-02

Branch: `v2-development`

Required baseline HEAD: `44e5add57f070fe65a99f146e4be9b952d654ae2`

Candidate SHA-256: `C4789D682D4A01E75BD0B1D749DF3404F280976A28F796B6681728607D0C0AA7`

## Verdict

**FORMALLY CLOSED — independent controlled-live review passed; leave the accepted Runtime Activation Gate Phase 1 + B0.1 candidate installed.**

The live run reproduced the original risk condition—powered aircraft, xPilot disconnected, cleared FMS, and X-Plane replay—without invoking the FMS sampler or any other operational stage. The same remained true after a real connected-to-disconnected transition and during replay. Connected activation, Plugin Admin re-enable, same-callsign recovery, battery transitions, disconnected re-enable, connected/battery-off re-enable, and normal shutdown all behaved correctly.

No callback exceeded 10 ms. All 7,482 callback timing records and all 112 route-evidence records are contiguous and lossless. Gate B remained asynchronous and clean. No rollback trigger occurred.

## Deployment identity and rollback preservation

| Artifact | SHA-256 | Size |
|---|---|---:|
| Installed candidate after normal shutdown | `C4789D682D4A01E75BD0B1D749DF3404F280976A28F796B6681728607D0C0AA7` | 2,760,704 bytes |
| Preserved installed candidate | `C4789D682D4A01E75BD0B1D749DF3404F280976A28F796B6681728607D0C0AA7` | 2,760,704 bytes |
| Preserved predeployment Gate B binary | `CC42CD97A2655C1D8FB445FD128C85833E82BB7D535490EA4B08F310B2C5F512` | 2,755,072 bytes |
| Preserved Product-Calm rollback | `3D8E35CBD3B0FAA96CEB24D5422BD73ECB928F8A7F9B7417249FA994E4CD4E70` | 2,689,024 bytes |
| Final diagnostics | `4859230E90E2C0F85F8C0B1C8B77ECBF4FFD3181562106A67939611BF563D858` | 8,241,182 bytes |
| Final X-Plane log | `0F66ACBD121F9DF135836FABB4507DF80EA612449E29C32C0B11AB8B59F663F1` | 172,988 bytes |

Installed path:

`C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl`

The installed binary was rehashed after X-Plane closed and still matched the reviewed candidate exactly. X-Plane process count was zero at final collection.

## Controlled sequence and observed behavior

| Test step | Expected | Observed |
|---|---|---|
| Startup/main menu | Load candidate without activation or load failure | Candidate loaded; dormant immediate-enable check; no load error |
| E170 powered, xPilot never connected | Dormant operational pipeline | Zero operational-stage calls |
| Cleared FMS and replay while never connected | No FMS inspection despite replay/movement | 626 contiguous callbacks; zero FMS or operational records |
| Initial xPilot connection | Immediate full operational activation | One rising edge; normal plan, PDC, route, authority, and workflow processing began |
| Connected disable/re-enable, first cycle | Immediate gated callback; no duplicate PDC capture | Immediate operational callback; PDC service 2 us; no duplicate capture |
| Connected disable/re-enable, second cycle | Same behavior, no repeated transition work | Immediate operational callback; PDC service 1 us; no duplicate capture |
| Real xPilot disconnect | Preserve current flight and invalidate/cancel once | One disconnect falling edge, one route cancellation, one authority invalidation; preserved flight confirmed |
| Replay after disconnect | Remain dormant | 246 contiguous callbacks; zero FMS, route, authority, PDC, or other operational work |
| Same-callsign reconnect | Automatic current-flight recovery | Recovery accepted with preserved KDCA-KBOS flight context |
| Both batteries off while connected | One battery falling edge, then dormant | One falling edge; one cold/dark reset; no repeated operational work |
| Batteries restored while connected | Immediate full activation | One rising edge; full sample and new product-epoch PDC capture |
| Second xPilot disconnect | Exactly one more disconnect transition | Disconnect falling counter advanced from one to two; matching single invalidations |
| Disable/re-enable while disconnected | Immediate gated callback, still dormant | First callback 478 us; settled 22-25 us; zero operational work |
| Reconnect, then battery off | Operational rise followed by battery fall | Both edges occurred once and in order |
| Disable/re-enable while connected but battery off | Immediate gated callback, still dormant | First callback 803 us; settled 37-42 us; zero operational work |
| Normal X-Plane shutdown | Drain evidence and join workers cleanly | Clean summaries, drainage, retirement, worker join, and process exit |

## Runtime activation accounting

Final activation summary:

| Counter | Result |
|---|---:|
| Gated callbacks | 7,463 |
| Dormant callbacks | 5,712 |
| Operational callbacks | 1,751 |
| Enable immediate-wake requests | 5 |
| Enable immediate callbacks | 5 |
| Immediate operational callbacks | 2 |
| Immediate dormant callbacks | 3 |
| Pending immediate wakes at shutdown | 0 |
| Activation rising edges | 6 |
| Deactivation falling edges | 4 |
| xPilot disconnect falling edges | 2 |
| Dormant operational attempts | **0** |

The five enable wakes reconcile exactly:

1. Startup/main-menu dormant check.
2. First connected Plugin Admin re-enable, operational.
3. Second connected Plugin Admin re-enable, operational.
4. Disconnected Plugin Admin re-enable, dormant.
5. Connected/battery-off Plugin Admin re-enable, dormant.

The six rising edges reconcile to initial connection, two connected re-enables, same-callsign reconnect, battery restoration, and the final reconnect. The four falling edges reconcile to two real xPilot disconnects and two connected battery-off transitions.

## Dormant stage containment

Every protected operational service reported zero dormant attempts. Stage call counts matched operational callbacks only:

| Stage group | Operational calls | Dormant attempts |
|---|---:|---:|
| VATSIM feed | 1,751 | **0** |
| Controller snapshot | 1,751 | **0** |
| Flight-plan/FMS sampling | 1,751 | **0** |
| Network-plan polling | 1,751 | **0** |
| Radio/transceiver sampling | 1,751 | **0** |
| PDC private source | 1,751 | **0** |
| ATIS cycle | 1,751 | **0** |
| METAR cycle | 1,751 | **0** |
| CTAF processing | 1,689 | **0** |
| Route preparation coordination | 1,689 | **0** |
| Authority/controller relevance | 1,689 | **0** |
| Workflow/display publication | 1,689 | **0** |
| Standby-assist operational work | 1,751 | **0** |

The lower 1,689 count reflects operational callbacks with complete flight context; it is not a dormant-path invocation.

### Original failure-condition reproduction

The never-connected replay window covered timing sequences 2,134 through 2,759:

- 626 records, zero gaps.
- P99 36 us; maximum 71 us.
- Zero callbacks above 250 us.
- Zero FMS samples.
- Zero route or authority submissions.
- Zero network, radio, PDC, ATIS, METAR, CTAF, workflow, or standby operational calls.

The post-disconnect replay window covered sequences 4,989 through 5,234:

- 246 records, zero gaps.
- P99 39 us; maximum 93 us.
- Zero callbacks above 250 us.
- Zero FMS, route, authority, PDC, or other operational work.

This directly corrects the September 2 offline DAL7510 replay failure mode, where the cleared 512-entry FMS had previously been scanned approximately once per second and callbacks reached 69.184 ms.

## Callback timing

The timing lane contains sequences 1 through 7,482 with zero gaps. Of these, 7,463 are activation-gated callbacks and 19 are explicitly identified accessory-only callbacks.

| Population | Count | P50 | P95 | P99 | Maximum | Above 3 ms | Above 10 ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| Entire live session | 7,482 | 23 us | 316 us | 986 us | 8,282 us | 12 | **0** |
| Dormant, including transitions/accessory timing | 5,713 | 22 us | 39 us | 47 us | 8,258 us | 1 | **0** |
| Dormant settled, transition callbacks excluded | 5,706 | 22 us | 39 us | 45 us | 702 us | **0** | **0** |
| Operational | 1,769 | 289 us | 571 us | 1,637 us | 8,282 us | 11 | **0** |
| Explicit transition callbacks | 13 | 1,441 us | 8,258 us | 8,258 us | 8,258 us | 3 | **0** |

The 19 accessory-only timings explain the difference between timing-state classification and the activation summary. Ground-truth operational/dormant counts are the runtime activation counters above.

### Classification of every callback above 3 ms

| Sequence | Callback | Classification | Route/authority contribution |
|---:|---:|---|---|
| 1 | 8.258 ms | Startup aircraft-load/session reset and update lifecycle; dormant transition | None |
| 3,545 | 6.318 ms | Initial xPilot activation with no established flight context; flight-plan sample 5.271 ms and display-log work 718 us | No route build; no authority evaluation on simulator thread |
| 3,606 | 8.282 ms | Initial complete plan/PDC activation; network-plan processing 4.064 ms and PDC 3.046 ms | Route work dispatched asynchronously |
| 3,617 | 3.354 ms | Operational follow-up/publication cycle; remaining time not attributed by emitted substage records | Route/authority main-thread operations remained bounded; no synchronous route build |
| 4,085 | 5.877 ms | First connected re-enable follow-up, including METAR harvest/publication | Route submission 69 us; preparation remained off-thread |
| 4,373 | 4.234 ms | Second connected re-enable follow-up; emitted records do not assign the full residual | No synchronous route build; Gate B limits remained clean |
| 5,470 | 5.660 ms | Same-callsign reconnect activation and recovery | Route/authority work asynchronous and stale-gated |
| 5,590 | 5.723 ms | Operational periodic/enrichment cycle; residual outside the route worker coordinator | FMS observation/route submission 18 us; route build off-thread |
| 6,048 | 7.830 ms | Battery-restoration product rebuild and new product-epoch PDC capture; PDC 3.149 ms | Route preparation asynchronous |
| 6,057 | 3.511 ms | Operational follow-up/publication cycle; residual not fully attributed by substage records | No synchronous route build |
| 6,226 | 3.450 ms | Operational refresh; authority submission 8 us and ATIS evaluation 281 us, with residual elsewhere | Authority remained off-thread; no route spike |
| 6,958 | 5.137 ms | Final reconnect/enrichment cycle | FMS observation/route submission 19 us; route preparation off-thread |

No callback above 3 ms was caused by dormant FMS work. No callback exceeded 10 ms. The partially attributed operational residuals are documented without assigning an unsupported cause; they do not violate this correction's acceptance boundary.

## B0.1 PDC and enable-path proof

- `XPluginEnable` requested the next-frame gated callback; it performed no operational service directly.
- Connected, powered re-enables immediately entered the existing activation boundary.
- Disconnected and connected/battery-off re-enables entered the same boundary and remained dormant.
- PDC private-source servicing occurred only during operational callbacks.
- The initial product epoch captured PDC exactly once.
- Both unchanged connected Plugin Admin re-enables preserved that capture and did not duplicate it.
- The battery-off cold/dark reset deliberately created a new product epoch; battery restoration captured PDC once in that new epoch.
- Total live PDC captures: two, exactly one per applicable product epoch.

## Disconnect, preservation, and recovery

Each genuine xPilot connected-to-disconnected edge produced exactly one session-boundary transition, one route invalidation/cancellation event, and one authority invalidation. Subsequent disconnected callbacks did not repeat reset or cancellation work.

On same-callsign reconnect, X-Plane logged:

`Automatic current-flight recovery accepted reason=recovery-departure-ground stage=DEPARTURE preserved=1 plan=1 route=KDCA->KBOS`

Two same-callsign recovery events were accepted across the complete live sequence. There were zero stale route or authority publications.

## Gate B non-regression

Final route-worker accounting:

| Metric | Result |
|---|---:|
| Worker starts/completions/failures | 33 / 33 / 0 |
| Queue replacements | 0 |
| Maximum pending depth | **1** |
| Route preparations accepted | 6 |
| Route stale rejections | **0** |
| Route invalidations | 13 |
| FMS observations accepted | 27 |
| Expanded-FMS dispositions | 6 changed / 21 unchanged |
| Route submit maximum | 110 us |
| Mailbox exchange maximum | 6 us |
| Harvest maximum | 80 us |
| Transition maximum | 77 us |
| Request nodes in use at shutdown | **0** |
| Outstanding leases at shutdown | **0** |
| Retirement backlog at shutdown | **0** |

All route submit, harvest, and transition maxima remained below 250 us and therefore also below the 1 ms absolute limit. Six route leases were created, retired, and destroyed on the worker. No route preparation executed synchronously in the simulator callback.

Route evidence contained sequences 1 through 112 with zero gaps:

- Dispatch: 33.
- Terminal: 33.
- Disposition: 33.
- Lifecycle: 13.
- Route preparations: six, all published.
- Stale/rejected publications: zero.

Authority evidence was also exact: 22 dispatches, 22 terminals, 22 dispositions, and zero stale or rejected publications.

## Diagnostic integrity and shutdown

| Counter | Result |
|---|---:|
| Timing records submitted/written | 7,482 / 7,482 |
| Timing sequence gaps | **0** |
| Route-evidence records submitted/written | 112 / 112 |
| Route-evidence sequence gaps | **0** |
| Critical contention drops | **0** |
| Timing full/not-running drops | **0 / 0** |
| Route-evidence full/not-running drops | **0 / 0** |
| Formatting failures | **0** |
| Storage failures | **0** |
| Drained before stop | **yes** |

There were three ordinary routine-diagnostic contention drops. These were allowed, noncritical human-readable records; neither lossless timing nor route evidence was affected. Producer maxima remained bounded: ordinary diagnostics 35 us, timing 114 us, and route evidence 26 us.

Normal shutdown drained the writer, cancelled or retired applicable worker state, joined workers, left no request nodes or leases outstanding, and emitted final summaries before X-Plane exited.

## Evidence notes

1. The first aircraft-session transition was logged as `battery-off` while the user later confirmed both E170 batteries were on. Reason-only changes within the dormant state are not transition-logged. This did not affect the proof: the user-confirmed powered replay window had zero operational work, and later live battery-off/on transitions were explicitly recorded and behaved correctly.
2. A separate local UDP dataref query was unavailable in this setup. No source or simulator setting was changed to work around it; activation proof relies on the plugin's production counters, timing evidence, user-directed state transitions, and X-Plane logs.
3. The three droppable routine-diagnostic contention losses are outside the lossless timing and route-evidence contracts.

## Acceptance checklist

| Requirement | Result |
|---|---|
| Candidate deployed and installed hash exact | PASS |
| Powered, never-connected replay remains dormant | PASS |
| Post-disconnect replay remains dormant | PASS |
| Zero dormant FMS samples | PASS |
| Zero dormant operational-stage attempts | PASS |
| Dormant P99 at or below 250 us | PASS — 47 us overall, 45 us settled |
| No settled dormant callback above 3 ms | PASS — maximum 702 us |
| Complete callback P99 at or below 3 ms | PASS — 986 us |
| No callback above 10 ms | PASS — maximum 8.282 ms |
| Immediate powered/connected enable servicing | PASS |
| Disconnected and battery-off enable remains dormant | PASS |
| Exactly-once PDC per product epoch | PASS |
| Disconnect preservation occurs once per falling edge | PASS |
| Same-callsign automatic recovery | PASS |
| Battery transitions and cold/dark reset | PASS |
| Gate B queue depth one and zero stale publications | PASS |
| Route main-thread maxima below 250 us | PASS |
| Timing and route-evidence lanes contiguous/lossless | PASS |
| Clean diagnostic drainage and normal shutdown | PASS |

## Repository boundary

- Branch and baseline HEAD remained exact.
- `git diff --check` passed; only existing line-ending notices were emitted.
- The Git index is empty: no files were staged.
- No commit or push was performed.
- Product-Calm 2 was not started.
- Unrelated modified and untracked repository content was preserved.

## Preserved raw evidence

- `xvatsim_diagnostics_final.log`
- `X-Plane_Log_final.txt`
- `Installed_Runtime_Activation_Gate_B0_1_final_XVatsim.xpl`
- `Installed_Gate_B_predeployment_XVatsim.xpl`
- `Product_Calm_rollback_XVatsim.xpl`
- State-boundary diagnostic and X-Plane snapshots retained alongside this report.

## Recommendation

Runtime Activation Gate Phase 1 + B0.1 is formally closed. Leave candidate `C4789D68...C0AA7` installed for continuing beta testing. No rollback or additional activation-gate source change is required.

Do not stage, commit, push, or begin Product-Calm 2 until separate closeout authorization is issued.
