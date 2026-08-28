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
raster/upload/draw timing. A matching generation must be drawn before an action
is recorded complete.

Step 4 renders exactly one neutral `METAR` line when there is no usable primary
observation. A usable fresh or still-fresh cached primary renders exactly two
lines: ICAO and VFR/MVFR/IFR/LIFR, with the corresponding category tone. The
selected state is border-only; `OPEN`, freshness, pending, unavailable, stale,
and lookup text never appear inside the ORB. Detailed state, raw text, pinned
primary content, chronological history, and bounded lookup
pending/spotlight/failure presentations remain in the drawer. Hidden changes
do not prepare or rasterize the drawer; current content is prepared once when
selected.

The overlay does not select airports, validate lookups, schedule requests,
parse weather, classify categories, decide freshness, reorder history, or own
spotlight deadlines. VATSIM ATIS, PDC, and private-message sources remain
unconnected.

