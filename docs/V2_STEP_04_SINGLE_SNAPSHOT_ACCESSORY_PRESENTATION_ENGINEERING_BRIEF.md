# XVatsim V2 Step 4 Single-Snapshot Accessory Presentation Engineering Brief

## Purpose

The correction removes the split semantic presentation/preparation contract that
allowed METAR, ATIS, and PDC selection commands to acquire incompatible content
generations. The Brain now projects one immutable accessory presentation
snapshot. Generic mechanical preparation retains that exact snapshot and binds
its result by lifecycle, command, selected drawer revision, and mechanical
layout identity.

## Governing design

- The Brain remains the sole semantic owner of drawer selection, ORB content,
  drawer content, history, availability, and command identity.
- `BrainOwnedAccessoryPresentationSnapshot` is the sole semantic payload.
- Per-drawer content revisions prevent a METAR mutation from becoming an ATIS
  or PDC content requirement.
- Mechanical layout changes reprepare the same command and do not request a new
  Brain command.
- One generic preparation path measures, wraps, and builds a bounded plan from
  the exact immutable snapshot.
- The overlay accepts only a plan carrying the current lifecycle and command
  identity. A stale plan is discarded and cannot alter visible state.
- The existing lossless publication queue and unified visible-publication
  coordinator remain the only terminal-fact return path.

## Visible invalidation correction

Drawer-authoritative content revision and visible presentation invalidation are
separate Brain-owned decisions. Hidden, unselected drawer content may advance
its own revision without creating a command, snapshot, raster, upload, or
publication. A new presentation is requested only when the selected drawer,
the active drawer's displayed content, an exact displayed ORB semantic field,
or lifecycle-visible state changes. One accepted logical mutation creates at
most one command.

## Visual-proof tool migration

`tools/step4_metar_visual_proof/src/main.cpp` now consumes the current two-
argument presentation projector, reads identities only from the immutable
snapshot, builds keys with `BuildAccessoryPreparationKeyForCommand`, and keeps
the exact snapshot in every prepared plan. Visuals that claim transitions use
Brain-owned selection, accepted worker facts, history mutation, freshness
cycles, and lifecycle operations. The tool no longer clones snapshots or
fabricates command, lifecycle, revision, history, or content identities.

The measured `performance.csv` remains a separate timing artifact. The
deterministic `SHA256SUMS.txt` covers the 43 PNGs and the deterministic
publication-count report, so independent runs can truthfully have identical
visual manifests without normalizing measured wall-clock timings.

## Preservation boundary

The correction does not change VATSIM authority, METAR transport or parsing,
KDFW/KABQ/KSAN targeting, classification, refresh cadence, spotlight policy,
history policy, Arrival, V1.2.3, ORB design, or the accepted publication queue.
It grants no deployment or controlled-live authority.
