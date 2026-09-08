# Step 4 Accessory Input Boundary Correction — Offline Proof Summary

Date: 2026-08-28

## Result

The X-Plane mouse callback is now fact-capture-only. It enqueues one immutable
drawer identity, publishes a bounded notification sequence, mechanically wakes
the existing registered flight loop, and returns. FIFO consumption, the sole
open/close/switch decision, presentation projection, and terminal-result
consumption occur on the next safe brain/plugin cycle.

The obsolete render-bound dispatcher is absent from production behavior and
production reporting. Later clicks do not wait on rendering authority.

## Preserved red proof

Before production edits, the five required red cases all failed against the
unmodified implementation:

1. brain selection ran before the mouse callback exit boundary;
2. a single ATIS click failed to visibly switch;
3. a single PDC click failed to visibly switch;
4. two ATIS facts produced an inconsistent visible final drawer; and
5. legacy accounting reported zero brain requests despite a real selection.

The same named cases pass through the corrected production-like path.

## Regression proof

- Corrective: 26/26.
- Step 3 focused: 31/31.
- Step 4 focused: 206/206.
- Complete saved suite: 700/700.
- Scenario fingerprint:
  `C1367F24170283074D7D7371EFD4EDD1C687D7903F959CC3FA19FE3D807AAE8B`.

The 1,000-click production-path stress recorded 1,000 facts produced, consumed,
decided, commanded, and terminally accounted; zero drops; zero final input and
publication queue depth; no behavioral in-flight state; and maximum terminal
time of 100 microseconds.

The 100,000-cycle warm-idle proof recorded zero click consumption, brain
selection, commands, preparation, history, wrapping, rasterization, upload,
publication, or diagnostics and returned to the 0.25-second cadence.

## Regression preservation

- Worker cancellation/join maximum: 17 ms, under 500 ms.
- Real WinHTTP loopback success: 17 ms, `/KDFW?format=json`, one parser call.
- Deferred timing accounting: zero unattributed or overlapping microseconds.
- METAR transport, decoder, parsing, targeting, category, history, refresh, and
  spotlight policy source files were not changed.

No deployment, X-Plane/xPilot startup, live VATSIM request, controlled live
proof, V1.2.3 change, or protected-evidence mutation occurred.

