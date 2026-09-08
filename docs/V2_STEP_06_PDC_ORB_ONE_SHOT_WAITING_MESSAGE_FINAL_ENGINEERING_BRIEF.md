# XVatsim V2 — Step 6 One-Shot PDC ORB Final Engineering Brief

Status: accepted V2.0.0 feature baseline

## Product contract

The PDC ORB is a one-shot convenience snapshot of the first mechanically valid waiting xPilot private-message observation for the current flight. It is not a private-message inbox, PDC text classifier, amendment monitor, or replacement for xPilot.

The Brain is the sole product decision owner. Sender spelling, controller roster or position, body wording, `PDC`/`ACARS` labels, IFR/VFR selection, and workflow stage do not classify or veto an otherwise valid observation. The accepted rare limitation is explicit: the first waiting private message may be administrative rather than the clearance the pilot expected. xPilot remains authoritative whenever the snapshot is missing, incorrect, or outdated and for every later revision.

## Acquisition and mechanical admission

Acquisition begins only when the Brain owns an active, canonical flight identity with normalized callsign, departure ICAO, and destination ICAO, including a known departure. An observation already waiting before that context becomes complete remains eligible; incomplete context does not retain or terminally disposition sender/body content.

The xPilot source must pass its existing mechanical qualification:

- the expected plugin source and exact binary fingerprint are present;
- sequence, sender, and body datarefs exist with accepted types;
- the source and capabilities remain unchanged across the read;
- connection state and callsign are stable and connected;
- sequence-before equals sequence-after and is positive;
- sender/body reads complete coherently;
- the body is bounded, valid UTF-8, free of unsafe controls, and non-empty after normalization; and
- no other mechanical issue is present.

The sender dataref must be readable and mechanically qualified, but its value may be empty. The optional xPilot version dataref is advisory-only; a missing, differently typed, or nonmatching optional version value does not weaken the required binary, capability, or tuple locks.

When those facts are valid, the Brain atomically creates one flight-bound artifact, assigns its revision identity and capture time, retains the bounded sender/body snapshot, marks it unread, sets capture complete, closes acquisition, and publishes one semantic mutation. Successful capture is the only terminal message disposition.

## One-shot stop and privacy

After capture, the Brain commands `samplePrivatePayload=false` and `clearQueuedFacts=true`. The plugin clears queued observations and returns before the bridge performs another private-source identity, capability, sequence, sender, or body read. Later delivered facts are early no-ops and cannot replace or amend the retained artifact.

Diagnostics serialize qualification masks, connection/sequence state, byte counts, timing, lifecycle epochs, unread state, retained-byte totals, and accounting. They do not serialize private sender or body text. The retained snapshot is bounded in memory and is exposed only through the Brain-owned drawer projection.

## Rule One ownership

`Brain decides. Modules produce facts. UI displays Brain-approved facts.`

| Layer | Final responsibility |
|---|---|
| xPilot bridge | Qualify the source and datarefs; perform bounded, sequence-bracketed mechanical reads; return connection, sequence, sender/body-read, capability, issue, and timing facts. |
| Plugin | Build the current Brain-owned flight context, request the Brain acquisition decision, transport observations through the bounded queue, obey the post-capture stop, and serialize private-safe accounting. |
| Brain | Bind the flight, arm acquisition, accept and retain the first valid observation, own the latch, unread/viewed state, uncertainty, lifecycle/reset behavior, status/tone/wording, drawer content, and visible acknowledgement. |
| Overlay | Mechanically render the exact Brain presentation, report drawer selection and publication facts, and return only the revision identities actually visible in the first displayed frame. |

No second sampler, inbox, renderer, accessory architecture, semantic controller/text classifier, or renderer-owned PDC decision exists on the production path.

## Presentation contract

The Brain projects an airport-free 52-pixel PDC ORB with these exact closed states:

- `PDC / IDLE` — qualified/waiting or not yet armed;
- `PDC / SOURCE` — source unavailable before capture;
- `PDC / CHECK` — pre-capture mechanical uncertainty;
- `PDC / NEW 1` — captured and not visibly acknowledged; and
- `PDC / MSG 1` — captured and visibly acknowledged.

Selection has precedence and projects `PDC / OPEN`. Closing restores the current Brain-owned closed status without changing the artifact.

The captured drawer title is exactly `PDC — <DEPARTURE ICAO>`. Its first retained entry is the exact warning `CAPTURED SNAPSHOT — CHECK XPILOT FOR REVISIONS`, followed by the one captured message. The overlay derives visible revision identities from prepared lines inside the viewport. Only a Brain-accepted first-frame publication fact containing the captured artifact's exact visible revision identity changes unread to viewed. Opening, pixels alone, a stale fact, a hidden line, or another drawer cannot acknowledge it.

## Lifecycle behavior

Temporary xPilot disconnect/reconnect, temporary overlay sleep, and Plugin Admin suspend/resume preserve the captured artifact and do not re-arm private sampling. Ordinary drawer selection changes only presentation and viewed state.

Session reset, confirmed new flight, confirmed cold/dark boundary, callsign change, canonical plan-identity change, and plugin stop/unload clear the artifact and advance/re-arm the appropriate Brain-owned lifecycle. A new flight cannot inherit the prior flight's snapshot or viewed state.

## Shared accessory architecture and performance

METAR, VATSIM ATIS, and PDC remain three independent Brain-owned products on one accessory rail and one mutually exclusive drawer. They share the established mechanical preparation worker, cached presentation snapshot, render-on-change path, publication queue, and first-visible terminal-fact return path. PDC did not add a scheduler, worker, feed, IPC route, or fourth ORB.

The accepted normal-use session recorded:

- one capture with sampler time `3,859` microseconds and issue mask `0x0`;
- clicks produced/consumed/dropped/discarded/pending `3/3/0/0/0`;
- publication facts accepted/rejected/stale-rejected `13/0/0`;
- maximum synchronous accessory work `23` microseconds;
- maximum click-to-terminal `32,467` microseconds;
- exact accounting `true`; and
- zero threshold, render, liveness, cadence, callback-order, synchronous-budget, prohibited-access, or running-worker failures.

The Product Owner's normal KDEN-to-KMSP session passed `IDLE`, `NEW 1`, `OPEN`, exact drawer/warning/content, `MSG 1`, reopening the immutable snapshot, perceived performance, and normal shutdown without Plugin Admin, reset, recovery, artificial ordering, or retry. The private sender/body are intentionally absent from durable closeout records.

One `1,713 ms` route/authority refresh warning occurred after successful PDC capture, was unrelated to PDC sampling/rendering, caused no perceived lag, and tripped no accessory threshold. It is a nonblocking extended-beta observation. Beta monitoring should confirm that bounded startup preparation settles into stable, calm, low-churn operation across multiple flights.

## Proof and roadmap boundary

The accepted proof baseline is production-renderer visual `14/14`, focused Step 6 `61/61`, complete regression `861/861` twice, `861` scenarios at fingerprint `227121979F59CB3BB359B96DE57AC7DF601E0BE3736F82F38C19EE08437431D9`, and protected evidence `80 manifests / 4,695 rows / 0 mismatches`.

METAR, VATSIM ATIS, and this one-shot PDC ORB complete the V2.0.0 feature scope. The existing IFR/VFR mode foundation remains, but a dedicated VFR evidence engine or live VFR controller projection is not part of V2.0.0. Any such model is an optional Version 3 product decision requiring its own value case, CPU budget, architecture review, Contract Gate, offline proof, and live proof; Version 3 is not promised.
