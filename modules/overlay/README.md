# Overlay Module

Renders the in-sim overlay from a brain-issued view model only.

## V2 Step 3 Accessory Presentation

The overlay presents three readable ORBs and one visible drawer surface within
the existing XPLM window. The three histories remain brain-owned and independent;
the overlay renders only the selected brain-approved snapshot.

Overlay responsibilities are presentation-only:

- produce bounded ORB-click facts for the plugin to consume exactly once;
- resolve shared card, rail, drawer, drag, resize, clamp, and hit-test geometry;
- keep mouse-wheel drawer scrolling separate from main-card scrolling;
- retain only the selected drawer's presentation offset and render plan;
- rasterize and draw separate rail and drawer textures using render-on-change
  signatures; and
- reject prepared plans whose drawer or generation does not exactly match the
  current brain-approved presentation.

Drawer text preparation uses one event-driven Windows worker at below-normal
priority. Startup is asynchronous and fail-closed. Enqueue and ready-plan
publication are nonblocking: contention returns immediately, and an exact
pending generation may retry during existing accessory activity without a new
timer or recurring flight loop. The worker consumes immutable brain-approved
snapshots and produces immutable generation-tagged plans.

The worker cannot access XPLM, OpenGL, texture identifiers, settings, files,
network sources, plugin lifecycle callbacks, or mutable brain state. It owns its
GDI+ measurement context and is cancelled and joined at disable or stop.

After warmup, an unchanged frame performs no history traversal or copying, text
wrapping, GDI+ measurement, job submission, rasterization, texture upload,
allocation, file/network work, or diagnostic logging. Hidden history changes do
not rasterize or upload a drawer.

Timing reports distinguish asynchronous worker preparation wait from synchronous
simulator-thread dispatch/presentation work, X-Plane frame-cadence wait, and
raster/upload/draw timing. Pilot input is delivered directly to the brain and
never waits for a render acknowledgement. Rendering acknowledgement is bounded
telemetry only.

Lifecycle close, disable, and stop paths discard queued input facts, cancel
pending preparation, and terminally classify every undelivered command with
its full elapsed time. Bounded aggregate diagnostics report input queue counts,
publication outcomes, cancellations, maximum elapsed time, and final queue and
publication state without per-frame busy logging.

Step 4 renders exactly one neutral `METAR` line when there is no usable primary
observation. A usable fresh or still-fresh cached primary renders exactly two
lines: ICAO and VFR/MVFR/IFR/LIFR, with the corresponding category tone. The
selected state is border-only; `OPEN`, freshness, pending, unavailable, stale,
and lookup text never appear inside the ORB. Detailed state, raw text, pinned
primary content, chronological history, and bounded lookup
pending/spotlight/failure presentations remain in the drawer. Hidden changes
invalidate older command identities and revisions without rasterizing or
uploading the closed drawer; current content is prepared once for the exact
brain command when selected.

Successful METAR ICAO/category lines use Segoe UI Bold at 10.0 design pixels
times effective layout scale. Production-raster proof covers 0.85, 1.0, and
1.35 scale without clipping or border contact.

The overlay does not select airports, validate lookups, schedule requests,
parse weather, classify categories, decide freshness, reorder history, or own
spotlight deadlines. VATSIM ATIS, PDC, and private-message sources remain
unconnected.

## Step 4 Automatic ORB Publication And Timing Attribution

The rail invalidation key is the rendered rail, not the accessory content
generation. It includes only fields capable of changing rail pixels. An
accepted primary METAR therefore publishes one automatic rail raster/upload
without a pilot click, while drawer history, lookup spotlight, fetch age, and
identical-primary changes perform no rail work.

Deferred preparation timing remains owned until binding or explicit terminal
cancellation. The action ledger separately attributes worker queue wait, worker
CPU, publication handoff, ready collection, ready-to-bind delay, simulator-thread
dispatch, frame cadence, and draw work. It reports lost or overlapping time
instead of assigning asynchronous waits to the synchronous 16.7-millisecond
budget. The 500-millisecond end-to-end liveness limit remains independent.

## Brain-exclusive command publication

The brain is the sole semantic authority for selection, drawer ownership,
METAR state, and presentation revisions. The overlay consumes one immutable
brain command and performs only preparation, geometry, commit, raster, upload,
and draw mechanics.

`OverlayWindow::UpdateAccessory` is the single production commit coordinator.
Preparation completion is signaled by an event sequence and consumed only by
that normal update path. The draw callback never commits presentation state.
Exact command/lifecycle/revision checks reject obsolete prepared output without
choosing a fallback.

Every accepted command receives one bounded terminal mechanical fact:
committed without pixel work, first frame displayed, superseded before commit,
superseded after commit before display, exact-stage publication failure, or
lifecycle cancellation. Terminal capacity is reserved before command issue;
facts are never overwritten or coalesced. The brain consumes their product
meaning. A missed frame can never prevent the next click from reaching the
brain.

