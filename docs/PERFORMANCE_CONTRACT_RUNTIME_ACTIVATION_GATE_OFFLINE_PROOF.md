# Runtime Activation Gate — Offline FMS Containment, Phase 1 + B0.1

Date: 2026-09-02

Branch: `v2-development`

Required and verified baseline HEAD: `44e5add57f070fe65a99f146e4be9b952d654ae2`

Status: **FORMALLY CLOSED — offline and controlled-live engineering pass**

## Outcome

Phase 1 implements the product activation contract:

```text
operational = aircraft state valid AND battery on AND xPilot connected
```

The Brain now owns a small pure activation decision with explicit reason and
edge codes. The plugin applies it after aircraft sampling, xPilot polling,
pilot-identity resolution, and the existing xPilot session-boundary decision,
but before VATSIM, controller, FMS, network-plan, radio, PDC, ATIS, METAR,
CTAF, route, authority, workflow-publication, or standby-assist work.

The focused production-coordinator proof executed 50,000 replay/movement
dormant callbacks with a cleared 512-entry FMS fixture. It recorded zero FMS
samples, zero operational-service calls or attempted calls, a dormant-path P99
of `0 us`, and a maximum of `26 us`. A separate 1,000-cycle
connect/disconnect/reconnect stress produced exactly 1,000 disconnect
preservations, asynchronous route/authority invalidations, and same-callsign
recoveries.

B0.1 closes the reviewed Plugin Admin enable-path gap without moving PDC or
any other operational service outside the gate. `XPluginEnable` retains the
existing 10-second registration value as a fallback, then requests the same
registered flight-loop callback on the next X-Plane frame. That callback
samples aircraft and xPilot state and applies the same Brain activation
decision before it can reach `ServicePdcPrivateSource` or any other protected
stage. Connected/powered enable therefore receives an immediate full sample;
disconnected or battery-off enable receives an immediate dormant sample.

Gate B remains closed and unchanged. The accepted Runtime Activation Gate
candidate is installed at its reviewed hash. The predeployment Gate B binary
and Product-Calm rollback remain preserved at their protected hashes.

## Incident addressed

The September 2 DAL7510 KBOS-KDCA offline replay evidence showed that a
never-connected xPilot session still entered the preliminary operational
pipeline. After the X-Crafts FMS was cleared, the once-per-second
`FlightPlanSampler` endpoint search dominated the X-Plane callback:

| Observation | Result |
|---|---:|
| Preserved callback records | 29,834 / 29,834 |
| Post-trigger P99 | 32.877 ms |
| Maximum | 69.184 ms |
| Callbacks above 10 ms | 482 |
| Approximate simulator-thread cost | 29.53 ms/s |
| Runtime facts | `xpilot=0`, `battery=1`, no callsign/context |
| Gate B worker activity | zero |

The correction contains that work at the activation boundary. It does not
change the FlightPlanSampler endpoint algorithm or introduce Product-Calm 2.

## Architecture

### Brain-owned decision

`BrainOwnedOperationalActivationInput` contains only the three contract
facts. `DecideBrainOwnedOperationalActivation` is pure and returns:

- the explicit reason (`aircraft-state-invalid`, `battery-off`,
  `xpilot-disconnected`, or `operational`);
- initial-observation state;
- activation rising edge;
- deactivation falling edge; and
- xPilot disconnect falling edge.

Commit and service-accounting functions are separate fixed-size operations.
They use no heap allocation, formatting, mutex, wait, filesystem, network,
worker lifecycle, UI, or XPLM calls.

### Production ordering

The runtime order in `RefreshOverlayFromBrainEngineer3` is:

1. Harvest/update the update-check lifecycle and update notice.
2. Sample aircraft state and preserve existing invalid-aircraft handling.
3. Poll xPilot when aircraft state is valid.
4. Resolve pilot identity and the existing xPilot session boundary.
5. Perform the one-time preservation/reset/recovery transition, when present.
6. Compute and commit the Brain activation decision.
7. Return through the minimal dormant presentation path unless operational.
8. Only when operational, enter the normal operational service pipeline.

The xPilot falling-edge path preserves the current flight context and now
also invokes the existing nonblocking Gate B route and Gate A authority
invalidation operations exactly once. Repeated disconnected callbacks do not
repeat the boundary or invalidations.

### Dormant path

The dormant return is above every operational service call. It permits only
the update lifecycle, aircraft/xPilot facts needed by the boundary, one-time
session transitions, minimal overlay/update-notice presentation, and existing
lossless callback timing. Geometry persistence was removed from the two
boundary-frame renderers so the callback cannot perform a settings write while
dormant. Transition and shutdown accounting use deferred asynchronous
diagnostic formatting.

The 0.25-second normal callback cadence is unchanged.

### B0.1 activation-safe enable wake

The enable lifecycle performs no operational service. Its only B0.1 action is
to record a fixed Brain wake-request fact and ask X-Plane to run the already
registered callback on the next flight-loop frame. The first gated callback
consumes that pending fact and increments exactly one of two fixed counters:
`enableImmediateOperationalCallbacks` or `enableImmediateDormantCallbacks`.

This deliberately avoids a second activation rule. Enable does not decide
whether the product is operational and does not inspect or service PDC. The
normal callback remains the sole owner of the activation facts and complete
operational pipeline. Plugin Admin suspension clears any unconsumed wake fact,
and re-enable issues a new request.

## State-transition table

| Prior state | Current facts/event | Decision and one-time work | Operational pipeline |
|---|---|---|---|
| New | Aircraft invalid | Dormant: `aircraft-state-invalid`; existing invalid boundary applies | Skipped |
| New/dormant | Valid, battery off | xPilot/session boundary is resolved; existing cold-dark transition applies once | Skipped |
| New/dormant | Valid, battery on, xPilot never connected | Dormant: `xpilot-disconnected` | Skipped |
| Dormant | Replay or rapid movement, xPilot disconnected | Remains dormant; no new edge | Skipped |
| Dormant | Forced-open display or update notice | Presentation-only maintenance | Skipped |
| Dormant | Late initial xPilot connection | Activation rising edge | Full sample immediately |
| Operational | xPilot disconnect | Preserve flight; cancel/retire route and authority work once; mark falling edge | Skipped |
| Dormant after disconnect | Same-callsign reconnect | Queue existing automatic recovery; activation rising edge | Full sample immediately |
| Dormant/operational | Changed-callsign reconnect/change | Existing flight-scoped reset occurs once | Begins after reset boundary settles |
| Operational | Battery off while xPilot remains connected | Non-disconnect falling edge; existing cold-dark handling | Skipped |
| Dormant, connected | Battery restored | Activation rising edge | Full sample immediately |
| Disabled | Plugin Admin re-enable while connected and powered | Request next-frame callback; same Brain gate becomes operational | Full sample and PDC service immediately |
| Disabled | Plugin Admin re-enable while disconnected | Existing enable lifecycle completes; first callback remains dormant | Skipped |
| Disabled | Plugin Admin re-enable while connected, battery off | Request next-frame callback; same Brain gate returns battery-off | Skipped |
| Any | Session reset | Existing reset semantics retained; disconnected refresh remains dormant | Conditional on current facts |
| Any | Normal shutdown | Existing unregister/cancel/join/drain order retained | None |

## Stage-call accounting

The production path records bounded counters for every protected service.
The focused connected fixture invoked every stage four times. The lifecycle
stress invoked every stage 1,001 times (initial activation plus 1,000
recoveries). Every dormant-attempt counter was zero.

| Protected stage | Connected fixture | Lifecycle stress | Dormant attempts |
|---|---:|---:|---:|
| VATSIM public feed | 4 | 1,001 | 0 |
| Controller snapshot | 4 | 1,001 | 0 |
| Flight plan / FMS | 4 | 1,001 | 0 |
| Network plan | 4 | 1,001 | 0 |
| Radio/transceiver | 4 | 1,001 | 0 |
| PDC private source | 4 | 1,001 | 0 |
| ATIS | 4 | 1,001 | 0 |
| METAR | 4 | 1,001 | 0 |
| CTAF | 4 | 1,001 | 0 |
| Route | 4 | 1,001 | 0 |
| Authority | 4 | 1,001 | 0 |
| Controller relevance | 4 | 1,001 | 0 |
| Workflow/display publication | 4 | 1,001 | 0 |
| Standby assist | 4 | 1,001 | 0 |

Additional exact counters:

- dormant timing callbacks: `50,000`;
- dormant operational attempts: `0`;
- dormant FMS samples and entries examined: `0`;
- lifecycle stress cycles: `1,000`;
- lifecycle stress disconnect invalidations: `1,000`;
- lifecycle stress automatic recoveries: `1,000`;
- changed-callsign resets in the focused transition fixture: `1`;
- explicit session resets: `1`; and
- normal shutdowns: `1`.

B0.1 enable-path counters:

- connected/powered enable: one immediate callback, one PDC service, one PDC
  capture;
- disconnected enable: one immediate dormant callback, zero PDC services;
- connected/battery-off enable: one immediate dormant callback, zero PDC
  services;
- observation `120` present at enable and observation `121` replacing it at
  five seconds: both captured exactly once, with the first capture before the
  former 10-second callback boundary;
- connected initial enable plus connected re-enable: two immediate operational
  callbacks and two PDC service calls; the unchanged observation was captured
  only once; and
- disconnected initial enable plus disconnected re-enable: two immediate
  dormant callbacks and zero PDC or operational calls.

## Focused performance and lifecycle result

| Gate | Contract | Result |
|---|---:|---:|
| Dormant callback P99 | <=250 us | `0 us` |
| Dormant callback maximum | <=3 ms | `26 us` |
| Dormant FMS samples | 0 | `0` |
| Dormant operational attempts | 0 | `0` |
| Repeated disconnect/reset work | 0 | `0` |
| Full sample on activation | required | pass |
| Same-callsign recovery | exact | pass |
| Changed-callsign reset | exact | pass |
| Lifecycle stress | exact/bounded | `1,000/1,000` pass |
| Connected/powered enable | immediate gated full sample | pass |
| Disconnected enable | zero PDC/operational work | pass |
| Battery-off connected enable | zero PDC/operational work | pass |
| Enable-time PDC replacement | no missed/duplicate observation | pass |
| Connected/disconnected re-enable | exact immediate gated behavior | pass |

The focused timer surrounds the pure production coordinator and injected
stage boundary. It intentionally does not estimate XPLM dataref or overlay
cost; that residual must be measured in the controlled live proof.

## Offline acceptance

| Proof | Result |
|---|---:|
| Release plugin and regression harness | pass |
| Runtime Activation Gate probe | pass |
| Step 6 PDC focused regression | `61/61` |
| Frozen regression | `861/861` |
| Exact route-oracle parity | `64/64` |
| Exact authority parity | `64/64` |
| Combined authority/route parity | `64/64` |
| Gate A | pass |
| Gate A-Calm 1 | pass |
| Gate A-Calm 2 | pass |
| Product-Calm 1 | pass |
| Gate B | pass |
| Gate B Telemetry | pass |
| Gate B lifecycle replay | `1,000/1,000` stale rejection/coordinator pass |
| Gate B queue depth | one |
| Gate B route leases | `104/104/104`, zero outstanding |
| Gate B submit P99/max | `3/24 us` |
| Gate B harvest P99/max | `4/37 us` |
| Gate B transition P99/max | `16/373 us` |
| Gate B telemetry | `99/99`, zero gaps/losses, `8/8` shutdown drain |
| `git diff --check` | pass (line-ending notices only) |

The final frozen replay completed in 38.635 seconds. Route-only parity
completed in 5.170 seconds, authority-only parity in 8.528 seconds, and
combined parity in 8.703 seconds. The focused Step 6 PDC suite completed in
1.056 seconds.

## Protected hashes and binaries

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Runtime Activation Gate B0.1 accepted and installed candidate | 2,760,704 | `C4789D682D4A01E75BD0B1D749DF3404F280976A28F796B6681728607D0C0AA7` |
| Exact regression harness | 4,436,992 | `67CA5733FE3C1F4ABEAD18AED7868E6F27FB279A4DE17D65AE816F82433608F4` |
| Preserved predeployment Gate B binary | 2,755,072 | `CC42CD97A2655C1D8FB445FD128C85833E82BB7D535490EA4B08F310B2C5F512` |
| Preserved Product-Calm rollback | 2,689,024 | `3D8E35CBD3B0FAA96CEB24D5422BD73ECB928F8A7F9B7417249FA994E4CD4E70` |

The installed X-Plane plugin and preserved accepted candidate match exactly.

## Controlled-live acceptance and formal closeout

Independent controlled-live review accepted the correction on 2026-09-02.
The production test reproduced the powered, xPilot-disconnected, cleared-FMS
replay condition before initial connection and again after a real disconnect.
Both windows remained dormant with zero FMS samples and zero operational-stage
attempts.

| Live proof | Result |
|---|---:|
| Installed/preserved candidate identity | exact `C4789D68...C0AA7` |
| Complete callback timing | `7,482/7,482`, zero gaps |
| Complete callback P99 / maximum | `986 us / 8,282 us` |
| Settled dormant P99 / maximum | `45 us / 702 us` |
| Dormant operational attempts | zero |
| Dormant FMS samples | zero |
| Route evidence | `112/112`, zero gaps |
| Gate B queue depth / stale publications | one / zero |
| Route submit / harvest maxima | `110 us / 80 us` |
| Diagnostic timing/route-evidence loss | zero |
| Normal shutdown | clean |

Connected activation, immediate Plugin Admin re-enable, exactly-once PDC
capture per product epoch, disconnect preservation, same-callsign recovery,
battery transitions, disconnected re-enable, connected/battery-off re-enable,
worker retirement, evidence drainage, and normal shutdown all passed. The
three dropped records were permitted ordinary human-readable diagnostics;
the lossless timing and route-evidence lanes were unaffected.

The complete controlled-live proof is preserved at:

`outputs/performance_contract_runtime_activation_gate_b0_1_20260902_063111/01_controlled_deployment_20260902_064441/RUNTIME_ACTIVATION_GATE_PHASE_1_B0_1_CONTROLLED_LIVE_PROOF.md`

No rollback or additional activation-gate work is required. Product-Calm 2
remains a separate future gate for pathological FMS scans while legitimately
connected.

## Source audit

The correction source set is exactly:

- `brain/include/XVatsim/brain/BrainOwnedRuntime.h`: activation types,
  reason/stage enums, fixed counters including immediate-enable accounting,
  and function declarations.
- `brain/src/BrainOwnedRuntime.cpp`: pure decision, fixed-size accounting,
  enable-wake request/consumption, suspend marker, and static reason/stage
  tokens.
- `plugin/src/XVatsimPlugin.cpp`: production ordering, dormant return,
  transition/shutdown accounting, one-time disconnect invalidation, and
  removal of boundary-frame geometry persistence. B0.1 adds only the
  next-frame gated callback request after flight-loop registration.
- `tools/regression_harness/CMakeLists.txt`: focused probe source registration.
- `tools/regression_harness/src/main.cpp`: focused probe command registration.
- `tools/regression_harness/src/RuntimeActivationGateProbe.h` and `.cpp`:
  deterministic production-coordinator, enable/PDC replacement, timing,
  stage, transition, stress, reset, and shutdown proof.

No source file in FlightPlanSampler, route preparation, authority evaluation,
network semantics, radio behavior, display product logic, or UI product
behavior changed. Static inspection places the dormant gate at plugin line
6323 and the first operational service at line 6345. PDC remains inside the
operational pipeline at line 6586. The enable lifecycle registers the callback
at line 7174 and requests its next-frame run at line 7176. All protected
operational service calls are below the dormant return. The Brain decision
begins at line 4174 and contains no prohibited API or blocking primitive.

The repository began at the required HEAD with no tracked or staged changes.
Unrelated pre-existing untracked `docs/` and `outputs/` content was preserved.
Only the seven source/probe files above, this report, and the Runtime
Activation evidence directories belong to this correction. The original
Phase 1 directory remains preserved; the B0.1 directory named below is the
controlling candidate evidence.

## Residual scope boundary

- The deterministic offline timing result covers the pure production
  coordinator and service boundary. The accepted live proof separately covers
  complete production callback timing, including XPLM aircraft/xPilot sampling
  and overlay maintenance.
- Update-check and minimal overlay/update-notice presentation intentionally
  remain alive while dormant; their cost was included in the accepted complete
  callback timing lane.
- Invalid-aircraft state retains the existing early invalid-state handling;
  xPilot polling resumes after the aircraft sampler again reports valid.
- Disconnect cancellation adds only the already-proven nonblocking Gate A/B
  invalidation operations to a one-time transition; settled disconnected
  callbacks do not execute them.
- This gate prevents the offline FMS scan. It deliberately does not optimize
  the endpoint algorithm when XVatsim is legitimately operational.
- The immediate enable request is delivered by X-Plane on the next
  flight-loop frame, not synchronously inside `XPluginEnable`; this preserves
  the single activation boundary. Live accounting reconciled all five enable
  wake requests with five consumed callbacks.

No offline or controlled-live blocker remains. The gate is formally closed.

## Controlled-live protocol executed

The following authorized protocol was executed and accepted. It is retained
here as the reproducible live-proof procedure:

1. Preserve/hash the installed Gate B plugin and Product-Calm rollback; deploy
   only candidate
   `C4789D682D4A01E75BD0B1D749DF3404F280976A28F796B6681728607D0C0AA7`;
   verify the installed hash and complete startup smoke before asking Darron
   to interact with X-Plane.
2. Load the X-Crafts aircraft parked, battery on, with xPilot disconnected and
   never connected for this session. Clear the FMS so all 512 endpoint slots
   are invalid. Hold for at least 90 seconds, then use replay/scrubbing or
   rapid aircraft repositioning for at least 90 seconds.
3. While still disconnected, force the XVatsim display open and exercise an
   update notice if one is available. Confirm presentation changes but no
   operational stage counter advances.
4. Connect xPilot with a filed test callsign. Confirm one activation rising
   edge and an immediate full operational sample. Let initial route/authority
   work settle and remain connected for at least 60 seconds.
5. While connected and powered, disable/re-enable XVatsim in Plugin Admin with
   a controlled PDC observation available. Confirm one immediate-enable wake
   request, one consumed operational callback before the former 10-second
   boundary, one PDC service, and exactly one admission for the observation.
   Repeat once with the observation unchanged and confirm service occurs but
   the observation is not admitted twice.
6. Disconnect xPilot. Confirm exactly one disconnect falling edge, one route
   invalidation, one authority invalidation, preserved flight context, and no
   subsequent operational counter changes during a 60-second disconnected
   hold. Repeat the cleared-FMS/replay action during this hold.
7. Reconnect with the same callsign. Confirm one automatic recovery, one
   activation rising edge, and an immediate full operational sample with no
   stale publication.
8. Exercise battery off/on while connected. Confirm one non-disconnect falling
   edge and one reactivation sample. Then disconnect, disable/re-enable in
   Plugin Admin, and confirm one immediate-enable wake is consumed by a dormant
   callback with zero PDC/operational work. Repeat the re-enable while xPilot is
   connected but the battery is off and require the same dormant result.
9. Perform a normal X-Plane shutdown. Preserve the complete timing lane,
   activation summary, stage counters, route evidence, diagnostics writer
   drainage, X-Plane log, process observations, deployed/preserved hashes, and
   rollback recommendation.

Live acceptance:

- lossless, contiguous complete-callback timing;
- disconnected dormant P99 <=250 us and no dormant callback above 3 ms after
  transition callbacks are separately classified;
- zero dormant operational-stage attempts and zero FMS samples;
- exact single falling/rising edges, preservation, invalidation, and recovery;
- every Plugin Admin enable wake is consumed exactly once, with connected and
  dormant classifications matching the live facts;
- no missed or duplicated one-shot PDC observation across connected re-enable;
- no route/authority stale publication, queue-depth, ownership, or lifecycle
  regression; and
- clean diagnostic drainage and shutdown.

Rollback for hash/load failure, any dormant FMS/operational call, repeated
disconnect work, stale publication, lifecycle/ownership failure, timing-lane
loss, or a settled disconnected callback above 3 ms attributable to XVatsim.

## Evidence

Raw output, exact manifests, binaries, static audit, test commands, repository
status, hashes, deployment, and controlled-live closeout evidence are under:

`outputs/performance_contract_runtime_activation_gate_b0_1_20260902_063111/`
