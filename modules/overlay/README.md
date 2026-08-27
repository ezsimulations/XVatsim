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

Step 3 provides structure and explicit empty-state text only. It has no live
METAR, VATSIM ATIS, PDC, or private-message source.

