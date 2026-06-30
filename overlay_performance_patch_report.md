# XVatsim Performance Recovery Patch 1: Overlay Blocking Containment

Date: 2026-06-30

## Scope

This patch addresses only the live `OverlayUpdate` stalls observed at:

- tick 3041: `273 ms`, `visible-model / ui-update`
- tick 6821: `402 ms`, `dormant-model / ui-update`
- tick 8609: `505 ms`, `visible-model / ui-update`

No brain behavior, route resolution, authority relevance, standby assist, publisher, or display ordering policy was changed.

## Runtime Changes

| Area | Before | After | Performance impact |
|---|---|---|---|
| Hidden window creation | First `OverlayWindow::Update()` could lazily call `Create()` during a normal refresh tick. | `XPluginEnable()` pre-creates the hidden overlay window before the delayed flight loop starts. | Moves one-time XPLM/GDI+ window allocation out of recurring overlay update ticks. |
| Transition sound | `SyncVisibility()` called `PlayTransitionSound()`, which could run filesystem and MCI open/seek/play work during wake/hide. | Overlay update hot path skips transition sound and records `ss=1` when it would have played. | Removes MCI open/play from wake/hide refresh ticks. |
| Bring-to-front | Visible overlay updates could repeatedly check/front the XPLM window. | Fronting is limited to first visibility transition or active text entry, with a 3 second cosmetic cooldown. | Prevents repeated native fronting work during stable visible refreshes. |
| Overlay diagnostics | `OverlayUpdate` recorded only total time. | Diagnostic result now includes sub-step timing and flags. | Next live run can identify the blocking sub-step instead of treating overlay as one opaque blob. |

## New Overlay Timing Fields

The `OverlayUpdate` diagnostic result now includes:

- `createUs`: hidden XPLM window create time during update fallback
- `setVisUs`: `XPLMSetWindowIsVisible` time
- `frontUs`: front-check / bring-front time
- `bodyUs`: view model copy, enable, and scroll clamp time
- `soundUs`: transition-sound decision time
- `otherUs`: unclassified residual update time
- `wc`: window create called
- `sv`: set-visible called
- `fc`: front check considered
- `fb`: bring-front called
- `ft`: bring-front throttled
- `ss`: transition sound skipped in the hot path

The plugin also emits `event=overlay-preinit` during enable so hidden window creation cost is visible outside normal refresh ticks.

## Ownership Confirmation

- Overlay still consumes the same `OverlayViewModel` built from the brain-published final display snapshot.
- No code was added to sort, filter, promote, hide, tune, or replace any brain row.
- No route, relevance, authority, standby assist, publisher, or display intent file was changed.
- The patch only changes native overlay window handling and overlay timing diagnostics.

## Regression Coverage

The real `OverlayWindow` path depends on XPLM, GDI+, OpenGL, and Windows MCI, so the saved offline harness cannot directly assert native window timing. The patch therefore uses runtime sub-step diagnostics for live verification and existing saved guardrails for overlay row ownership/order.

Focused guardrails passed: `11 / 11`

- `brain_display_intent_arrival_center_next_polygon_sorts_before_terminal.scn`
- `standby_assist_arrival_uses_brain_display_order_center_before_app_ground.scn`
- all saved `brain_display_*overlay_cap*.scn` live-consumption/cap scenarios

Full saved regression passed: `443 / 443`.

RelWithDebInfo build passed and produced `build/dist/XVatsim/win_x64/XVatsim.xpl`.

## Post-Diff Impact Review

| File | Block | Authority/display impact |
|---|---|---|
| `plugin/src/XVatsimPlugin.cpp` | `FormatOverlayUpdateResult` and `RecordDiagnosticJob` result strings | Diagnostics only; no row mutation. |
| `plugin/src/XVatsimPlugin.cpp` | `PreinitializeOverlayWindow()` in `XPluginEnable()` | Creates hidden native window before flight loop; does not build or alter brain rows. |
| `modules/overlay/src/OverlayWindow.cpp` | `OverlayWindow::Update()` timing wrapper | Measures create/body/visibility/front/sound sub-steps; preserves incoming view model. |
| `modules/overlay/src/OverlayWindow.cpp` | `OverlayWindow::SyncVisibility()` | Skips cosmetic transition sound in hot path and throttles fronting; does not alter row content/order. |
| `modules/overlay/src/OverlayWindow.cpp` | `OverlayWindow::Hide()` timing wrapper | Measures hide visibility/body work; no brain state changes. |

## Live-Test Checklist

- Confirm `event=overlay-preinit` appears once on plugin enable with acceptable timing.
- During xPilot connect/wake, confirm `OverlayUpdate visible-model` no longer carries `createUs` or MCI-related delay.
- During dormant transition, confirm `ss=1` and `soundUs` remains near zero.
- Confirm `frontUs` is nonzero only on first visible transition or text entry.
- Confirm displayed rows and order match brain display intent in live flight.
- Confirm no XPL copy was performed by this patch review step.
