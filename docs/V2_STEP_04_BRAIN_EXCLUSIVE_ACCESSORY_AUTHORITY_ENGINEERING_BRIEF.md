# V2 Step 4 Brain-Exclusive Accessory Authority Engineering Brief

Date: 2026-08-28

## Governing invariant

The brain is the sole semantic and product-decision authority. Modules and
workers perform bounded mechanical operations and return facts. The overlay
renders one immutable brain command. No renderer acknowledgement controls the
brain's ability to decide a later pilot fact.

This correction is limited to the Step 3 accessory and Step 4 METAR paths. It
does not alter route-sector resolution, controller authority, flight-plan
interpretation, the VATSIM endpoint, WinHTTP sequencing, METAR parser policy,
category thresholds, targeting, refresh cadence, cache policy, or history
policy.

## Corrected causes

Three deterministic red cases were preserved against the pre-correction
implementation:

1. A hidden empty METAR preparation survived a later accepted KDFW
   observation and made the first opening empty.
2. The normal update path could apply a ready plan before the draw-time path;
   the latter then returned "already current" without terminating the
   render-bound action, permanently blocking later clicks.
3. Lifecycle cancellation could end an action after more than 500 ms while
   reporting no liveness failure.

The correction removes the stale hidden-preparation shortcut, removes the
draw-time presentation commit, and removes render acknowledgement from pilot
input eligibility. Cancellation retains the complete elapsed time.

## METAR fact ownership

The brain issues one `BrainMetarWorkerRequest` containing command identity,
lifecycle epoch, request identity, purpose, airport, and primary/lookup
generations. The existing METAR worker alone performs the unchanged bounded
WinHTTP transaction.

After receiving the bounded payload, that same worker calls the stateless
`DecodeVatsimMetarPayload(std::string_view)` function. The decoder has no
requested-airport input, no brain pointer, no thread, mailbox, scheduler,
deadline, or product state. It returns only JSON syntax, cardinality, field
type, station/raw string, whitespace, NUL, size-bound, and elapsed-time facts.

The worker returns one composite `BrainMetarWorkerFact`. The brain alone
decides station match, raw-report acceptance, changed versus identical,
parsing eligibility, category commitment, cache/history mutation, source
health, presentation, and future request eligibility.

## Ownership map

| Stage | Input | Output | Authority |
| --- | --- | --- | --- |
| METAR planning | workflow, flight plan, clocks, bounded state | one request command or no command | brain semantic decision |
| WinHTTP | immutable brain request | bounded HTTP/transport fact | worker mechanics |
| JSON decoding | bounded payload only | decoded wire-format facts | stateless module mechanics |
| METAR acceptance | composite worker fact | acceptance/rejection, parse/cache/history/presentation changes | brain semantic decision |
| Pilot click | bounded drawer click fact | open/close/switch brain command | brain semantic decision |
| Command projection | brain-owned state and layout generation | immutable complete accessory command | brain semantic decision |
| Text preparation | exact immutable command snapshot | exact-identity wrapped drawing plan | worker mechanics |
| Commit | latest command plus exact prepared result | committed rail/drawer state | single overlay coordinator mechanics |
| Raster/upload/draw | committed state only | pixels and first-frame fact | overlay mechanics |
| Delivery disposition | mechanical publication fact | authoritative delivery ledger | brain interpretation |

No domain worker communicates directly with another worker.

## Immutable command and revisions

Every brain presentation command carries an exact command identity, lifecycle
epoch, active drawer, selection generation, rail presentation revision, drawer
content revision, primary METAR ORB fields, drawer strings, ordered entries,
scroll-reset generation, and immutable shared snapshot handles.

Rail revision advances only when brain-approved rendered ORB fields change.
Drawer revision also tracks canonical METAR presentation and all brain history
generations while hidden. Therefore accepting KDFW while METAR is closed
invalidates an earlier empty plan without rasterizing or uploading the closed
drawer. Unchanged state reuses the same immutable snapshot and performs no
history traversal or copying.

Preparation cache hits require exact command identity, lifecycle epoch, drawer
revision, history generation, content generation, layout generation,
typography generation, scale, and bounded layout dimensions. Obsolete results
are rejected mechanically; their associated command is terminally superseded
or cancelled.

## Single publication sequence

`OverlayWindow::UpdateAccessory` is the only production presentation commit
coordinator:

1. The plugin drains prior mechanical publication facts to the brain.
2. It verifies bounded terminal-fact capacity before projecting a new brain
   command or consuming another click fact.
3. The brain projects the latest immutable command.
4. If its open drawer requires preparation, the plugin queues the exact
   immutable preparation snapshot.
5. Worker readiness advances an event sequence. The normal update path
   consumes only the exact requested result; there is no draw-time commit and
   no recurring readiness scan.
6. The coordinator commits the exact latest command, installs its prepared
   plan, and marks only the brain-revised rail/drawer textures dirty.
7. The draw callback renders committed state only.
8. The exact committed command receives one terminal publication fact after
   either a no-pixel commit, its successful first visible frame, supersession,
   exact mechanical failure, or lifecycle cancellation.

Terminal facts are stored without overwrite or coalescing. Two slots are
reserved before command issue because a new command can terminally supersede
the prior command and later terminate itself. Saturation leaves the click fact
queued for a later brain decision; it never blocks the simulator/draw thread
and the overlay does not invent a semantic response.

First-frame acknowledgement is emitted only after the exact command is
committed, required texture work succeeds, and its textured rail/drawer draw
completes. A newer click never waits for preparation, commit, raster, upload,
draw, or acknowledgement.

## Terminal publication facts

The bounded fact records command/lifecycle identity, applied identity, rail
and drawer revisions, active rendered drawer, preparation/commit/first-frame
and total elapsed time, and exact mechanical failure stage. Terminal outcomes
are:

- committed with no new visible frame required;
- first frame displayed;
- superseded before commit;
- superseded after commit but before first display;
- publication failed at preparation, commit, rasterization, texture upload, or
  post-commit stage; or
- lifecycle cancelled.

The brain rejects stale epochs and duplicate/out-of-order terminal facts and
records any command exceeding 500 ms as a liveness failure, including a
cancelled command.

## Corrective proof

The 44 `v2_step4_brain_exclusive_*` scenarios include the three preserved red
reproductions and production-path coverage for the stateless decoder,
composite fact, brain acceptance, first populated open, exact preparation
identity, single coordinator, all readiness/update/draw orderings, supersession
and failure outcomes, terminal-capacity saturation, nonblocking later clicks,
manual ownership, identical KDFW/KABQ behavior, Enroute KSAN, lifecycle epochs,
1,000 actions, and 100,000 unchanged cycles.

Final totals are 44/44 brain-exclusive, 31/31 Step 3, 180/180 Step 4, and
674/674 saved scenarios. The scenario fingerprint is
`EE5B15EAE725EBA23AEA1CE9F9FEB82D0D93578C0A18EBA6129AE8B898E645C3`.

The synchronous accessory limit remains 16.7 ms and command-to-first-visible
frame liveness remains 500 ms. Network work stays off simulator/draw threads;
changed accepted METAR parsing remains on the bounded brain harvest path.

`INITIAL-PLAN WORKLOAD ACCEPTED AS CURRENT BASELINE — NO SAFE NARROW CHANGE PROPOSED`

No route or authority optimization is part of this correction. Deployment and
controlled live reproof remain unauthorized.
