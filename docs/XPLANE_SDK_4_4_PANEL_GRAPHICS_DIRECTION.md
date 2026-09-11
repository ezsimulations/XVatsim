# X-Plane SDK 4.4 Panel Graphics Direction

Status: Recorded future engineering direction; no renderer change authorized

Research date: 2026-09-11

## Decision

XVatsim should eventually replace its legacy OpenGL overlay rendering path with
X-Plane SDK 4.4 Panel Graphics. This must be a separate, measured renderer
milestone after the validated V2.0.1 flight-loop maintenance release. The SDK
upgrade and renderer migration must not be mixed into the settled operational
refresh-gate commit.

The current renderer remains the release renderer until a native prototype
matches its appearance, behavior, lifecycle safety, and performance. There is
no emergency migration: X-Plane 12.4.4 continues to permit existing OpenGL
plugin drawing.

## Research Baseline

The following official Laminar Research material was reviewed:

- [What's New in 12.4.4: The SDK Update](https://www.x-plane.com/2026/09/whats-new-in-12-4-4-the-sdk-update/)
- [X-Plane SDK Roadmap: User Interface and Avionics](https://developer.x-plane.com/2026/04/x-plane-sdk-roadmap-user-interface-and-avionics/)
- [X-Plane SDK 4.4 staging API documentation](https://xpsdk-api.docs.staging.x-plane.com/)
- [Plugin Guidance for OpenGL Drawing](https://developer.x-plane.com/article/plugin-guidance-for-opengl-drawing/)
- [Official X-Plane SDK 4.4.0-b1 archive](https://www.x-plane.com/wp-content/uploads/2026/09/XPSDK440b1.zip)

The official SDK 4.4.0-b1 archive was examined outside the repository. Its
SHA-256 at the time of research was:

`564A315FC3CE48196B52904A41208F53FD91BCAB945E17CB278FB1312ED12FAB`

The repository currently contains SDK 4.3 headers and libraries (XPLM revision
430). X-Plane 12.4.4-b1 reports XPLM revision 440. SDK 4.4.0-b1 requires
X-Plane 12.4.4 or newer and is beta material, so API and packaging details must
be rechecked against the stable SDK before production adoption.

## Current XVatsim Rendering Architecture

XVatsim already creates its overlay with the modern `XPLMCreateWindowEx`
window API. The migration target is not window ownership; it is the renderer
inside that window.

`modules/overlay/src/OverlayWindow.cpp` currently:

- rasterizes the case, main card, ORB rail, and drawer into cached GDI+ images;
- uploads dirty images as OpenGL textures with `glTexImage2D`;
- binds those textures through XPLM/OpenGL state calls; and
- draws the cached layers with immediate-mode OpenGL quads.

The existing dirty/signature caching is valuable and must be preserved until a
replacement proves better. It prevents continuous rerasterization and texture
upload, although visible frames still cross the OpenGL bridge and draw the
cached quads.

## Relevant SDK 4.4 Capabilities

SDK 4.4 adds `XPLMPanelGraphics.h` and a Panel Graphics window content type.
Relevant facilities include:

- backend-native lines, polygons, quad strips, colors, transforms, clipping,
  scissors, and masks;
- native font creation, measurement, wrapping, and drawing;
- PNG and RGBA texture-atlas creation and drawing;
- retained drawing that records infrequently changing commands and replays
  them efficiently; and
- native texture and indexed draw-call support suitable for an ImGui renderer
  or another textured-mesh client.

Panel Graphics commands are collected during the normal window draw callback
and rendered by X-Plane's native backend. This removes the need for XVatsim to
manage OpenGL state and follows Laminar's Vulkan/Metal direction.

SDK 4.4 also adds browser/CEF windows and native Dear ImGui integration. Those
are not the preferred primary XVatsim renderer:

- CEF adds a browser runtime and unnecessary memory, lifecycle, and security
  surface for a small persistent cockpit overlay.
- Dear ImGui is useful for conventional settings, diagnostics, and developer
  tools, but it does not offer a clear product advantage for XVatsim's custom
  aircraft-style presentation.
- Direct Panel Graphics best fits the existing compact overlay and permits an
  incremental migration.

## Required Architectural Boundaries

The renderer must remain a presentation-only consumer. No SDK 4.4 renderer,
texture helper, hot zone, ImGui layer, or browser content may make controller,
flight-plan, accessory, visibility, or operational decisions.

Rule One remains unchanged:

- modules report bounded mechanical facts;
- the Brain makes product and semantic decisions;
- the plugin obeys the Brain's accepted output; and
- the UI renders Brain-approved facts.

The settled operational refresh gate remains independent of drawing. Panel
Graphics may reduce render callback and OpenGL bridge cost, but it does not
replace flight-loop activation, input-identity gating, worker ownership, or the
one-second settled safety refresh.

## Recommended Migration Plan

### Phase 0: Freeze and Measure

- Keep the validated V2.0.1 performance correction as the baseline.
- Record visible-idle, hidden-idle, content-change, drawer, scale, resize,
  multi-monitor, and VR callback measurements.
- Do not promise a CPU reduction until the native renderer is measured in the
  same scenarios on both strong and lower-end systems.

### Phase 1: Isolated Native Texture Prototype

- Create a renderer interface behind the existing overlay view model.
- Keep the current OpenGL renderer as the known-good implementation.
- Add an experimental Panel Graphics renderer without altering layout, input,
  animation, Brain state, or publication timing.
- Initially preserve the existing cached raster images and present them through
  SDK 4.4 native texture/draw-call facilities. Convert the current BGRA image
  data to the RGBA format required by the new API at the renderer boundary.
- Create or replace native textures only when the existing render signature is
  dirty; never introduce a continuous conversion or upload loop.

This first phase isolates OpenGL-bridge removal from a visual redesign and
provides an apples-to-apples performance comparison.

### Phase 2: Native Static and Text Rendering

- Move the static shell, borders, dividers, rail, and other infrequently
  changing geometry to retained Panel Graphics drawing.
- Rebuild retained command data only when scale, dimensions, theme, or relevant
  content generations change.
- Evaluate native fonts and primitives for dynamic labels and cards only after
  pixel layout, wrapping, DPI behavior, and Unicode coverage match the current
  renderer.
- Remove GDI+ rasterization selectively when measurement shows a benefit and
  visual regression proof is complete.

### Phase 3: Compatibility and Release Gate

- Recheck the stable SDK 4.4 headers, libraries, release notes, and minimum
  X-Plane version before implementation is promoted.
- If XVatsim must support pre-12.4.4 X-Plane versions, use an explicit runtime
  version gate and a safe dual-renderer loading strategy. Direct imports of new
  4.4 symbols can prevent an older XPLM runtime from loading the plugin, so
  dynamic symbol resolution or separate binaries must be evaluated rather than
  assumed.
- Exercise plugin enable/disable, reload, shutdown, device loss, resize,
  pop-out, multi-monitor, and VR lifecycle behavior.
- Require offline regression parity, visual comparison, controlled performance
  evidence, and live Product Owner acceptance before retiring OpenGL.

## Release Boundary

The SDK 4.4 renderer is not part of the V2.0.1 maintenance scope. V2.0.1 should
contain the already validated settled operational refresh gate and associated
regression proof only. Panel Graphics work begins later on a dedicated branch
with its own contract, rollback point, performance budget, and release decision.
