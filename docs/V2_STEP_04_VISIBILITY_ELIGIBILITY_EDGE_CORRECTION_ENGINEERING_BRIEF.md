# Step 4 Visibility-Eligibility Edge Correction — Engineering Brief

Date: 2026-08-29

## Diagnosis

`OverlayWindow::Draw()` opened a later visible-publication attempt only when
the rail or required drawer texture was dirty. A hidden command could therefore
commit terminally, prepare a reusable texture, become visibly eligible, and be
drawn without ever opening or terminally accounting for a visible attempt.
The previous 717-scenario suite missed this because several hidden-publication
checks proved fields, source tokens, or directly consumed facts rather than the
production visibility/draw state transition.

## Correction

`AccessoryVisiblePublicationState` is a production-owned edge tracker used by
`OverlayWindow`. It keys attempts by command, lifecycle, rail revision, drawer
revision, drawer state, and a mechanical visibility epoch. It opens on visible
eligibility regardless of texture dirtiness, terminalizes only after the first
qualifying draw (or a truthful missing-texture failure), and deduplicates
unchanged frames. `visibilityEpoch` is transported and serialized with the
existing separate attempt identity and timing.

The hidden command remains terminal under
`CommittedNoVisibleFrameRequired`; its later attempt has `commandTerminal=false`.
No Brain semantic authority moved into the overlay.

## Red and green proof

The 20 production-state corrective scenarios initially passed 4 and failed 16.
Failures covered clean reusable textures, visibility regain, missing required
textures, rail/drawer requirements, supersession, click-visible accounting, and
the production-like startup sequence. After correction all 20 pass.

The 17 earlier hidden-publication scenarios remain and pass. Five shallow checks
were hardened to drive the production-owned state component: named hidden
terminal behavior, no fabricated click timing, zero-raster resynchronization,
stable awaiting timer, and later-visibility timing.

## Scope

Current-gate production/test changes are confined to the approved Brain fact
field, overlay window/core, plugin diagnostic serialization, regression harness,
20 corrective scenarios, and this gate's evidence documents. No application,
deployment, staging, or live request was used.
