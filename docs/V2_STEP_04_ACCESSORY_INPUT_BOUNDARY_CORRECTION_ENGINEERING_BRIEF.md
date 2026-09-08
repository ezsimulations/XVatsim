# V2 Step 4 Accessory Input Boundary Correction

Date: 2026-08-28

## Failure reproduced

The production mouse-down path previously captured an ORB hit and immediately
called the plugin's accessory dispatcher before the X-Plane callback returned.
That dispatcher drained the fact, invoked the brain, projected a presentation,
and could begin preparation or alter window state inside the originating input
callback. METAR happened to remain usable, while single ATIS and PDC switches
could fail to become visible and a second click could close the brain-selected
but not-yet-visible drawer.

The first five corrective scenarios preserved the unmodified behavior before
production edits: brain selection began before the callback-exit boundary;
single ATIS and PDC clicks did not visibly switch; two ATIS facts could leave
the visible result inconsistent with their captured identities; and legacy
render-bound dispatcher counters remained zero despite real brain changes.
All five red cases failed before the correction and pass through the corrected
production-like path.

## Corrected ownership boundary

The mouse callback now performs only mechanical hit testing, immutable fact
capture, bounded FIFO enqueue, notification-sequence publication, and a wake of
the already registered X-Plane flight loop. It does not call the brain, drain
input, project or commit presentation, start preparation, change geometry,
rasterize, upload, or wait.

The next safe plugin/brain cycle consumes at most eight facts in FIFO order.
For each consumed fact, the brain makes exactly one open, close, or switch
decision and projects one immutable presentation command when state changed.
The overlay mechanically prepares and publishes that command through the
existing single commit coordinator. Terminal publication facts return to the
brain without controlling later input eligibility.

The old `AccessoryInputDispatchCoordinator` remains only as isolated legacy
test support. It is no longer a production member, behavioral gate, or source
of production accounting. Production diagnostics serialize the actual click
sequence, captured drawer, callback boundary, brain cycle and decision,
previous/resulting drawer, selection generation, command identity, terminal
disposition, and click-to-terminal time.

## Timing wake

After a successful enqueue, the callback mechanically requests the existing
registered flight loop at the next safe simulator-cycle opportunity through
`XPLMSetFlightLoopCallbackInterval(..., -1.0f, 1, ...)`. No new scheduler,
worker, thread, or polling loop exists. While bounded click, preparation,
command, or terminal-publication work remains, the existing flight loop asks
for next-cycle service. Once all work is terminal and queues are empty, it
returns immediately to the established 0.25-second cadence.

Corrective proof covers a click immediately after enable, a click immediately
after a normal cycle, preparation becoming ready just after a cycle, callback
exit before brain decision, a mechanically inert wake, return to normal
cadence, and 100,000 unchanged cycles with zero work.

## Product invariants

The correction does not change VATSIM transport, METAR decoding or parsing,
category policy, primary targeting, refresh cadence, cache/history policy,
lookup spotlight behavior, ORB typography or colors, route/authority logic, or
the main controller card. ATIS and PDC remain truthful selectable empty-state
drawers until their later product steps; selecting either dispatches no network
request and does not alter METAR state.

## Proof baseline

- Corrective scenarios: 26/26.
- Step 3 focused: 31/31.
- Step 4 focused: 206/206.
- Complete saved suite: 700/700.
- Canonical scenario fingerprint:
  `C1367F24170283074D7D7371EFD4EDD1C687D7903F959CC3FA19FE3D807AAE8B`.
- 1,000-click stress: all facts consumed, decided, commanded, and terminally
  accounted; zero drops; final queues zero; no behavioral in-flight state;
  maximum terminal time 100 microseconds.
- Warm idle: 100,000 cycles with zero recurring accessory or METAR work and
  final cadence 0.25 seconds.

Deployment and controlled live reproof are not authorized by this correction.
