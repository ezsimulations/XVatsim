# Brain Display Intent Contract

This contract defines how XVatsim turns accepted controller facts into UI rows.
It does not change controller detection truth. It sits after the radio-board,
route-polygon, and relevance workers.

The goal is simple:

- Controller workers prove what is reachable and route-relevant.
- The brain decides what the pilot should see now.
- The UI only renders the brain-approved display intent.

## Purpose

The UAL2862 KIAD -> KSFO live battle test proved the Engineer 3 controller
identification path can identify and filter controllers correctly, but it also
showed that relevance alone is not enough. The UI needs a separate display
intent layer so current, next, and arrival controllers are colored, ordered,
and refreshed correctly.

## Contract Rules

- Relevance workers may only produce accepted or rejected controller facts.
- Relevance workers must not decide final UI color, ordering, or distance
  refresh behavior.
- A brain-owned display-intent step decides what is shown.
- The display-intent step consumes accepted controller facts, route polygon
  position, workflow phase, arrival distance, radio state, and UI capacity.
- The UI must not run authority proof or route relevance work.
- Display distance refresh may repaint the UI, but must not rerun heavy
  authority proof.
- Arrival local/TRACON/center display remains gated by the existing 200nm
  arrival wake rule.
- Assist text remains `Active` and `Standby`; this contract only controls
  controller row intent, color, distance, and visibility.

## Display Relations

Each accepted controller fact must be mapped to exactly one display relation:

- `CURRENT_POLYGON`: controller belongs to the current route polygon.
- `NEXT_POLYGON`: controller belongs to the next route polygon ahead.
- `ARRIVAL_PREP`: controller belongs to the arrival airport, arrival TRACON, or
  arrival center after the 200nm arrival wake.
- `FILTERED`: controller was reachable but is not relevant to the current
  display phase.
- `HIDDEN`: controller is relevant later, but not yet eligible for display.

## UI Semantics

`CURRENT_POLYGON`:

- Display in green.
- Show `Active` or `Standby` using existing radio/assist state rules.
- No distance suffix is required unless the existing UI already has one.

`NEXT_POLYGON`:

- Display in orange.
- Show distance to the next polygon entry/controller authority.
- Refresh distance on a light display cadence so the pilot can see progress.
- Do not rerun authority proof just to refresh distance.

`ARRIVAL_PREP`:

- Begins only when within 200nm of destination.
- Display arrival center, arrival TRACON, and airport local authorities that
  are relevant and reachable.
- Preserve existing arrival wake behavior.

`FILTERED` and `HIDDEN`:

- Do not display.
- Log the reason so rejected or deferred controllers are explainable.

## Multiple Controllers In One Polygon

When more than one accepted center controller belongs to the same current or
next polygon:

- Display all accepted controllers if UI capacity allows.
- If UI capacity is constrained, the brain must choose deterministically and
  log which controller was hidden because of capacity.
- The UI must not randomly replace or collapse accepted controllers.

## Events That May Recompute Display Intent

Display intent may recompute when:

- The radio-board snapshot changes.
- A route polygon transition occurs.
- The workflow phase changes.
- The aircraft crosses the 200nm arrival wake boundary.
- A light distance-refresh tick occurs for already accepted next-polygon rows.

Display intent must not recompute because:

- A draw callback occurred.
- The overlay refreshed.
- A controller row was rendered.
- Old authority fallback woke itself.

## Diagnostics

Every displayed controller row must log:

- callsign
- frequency
- facility group
- polygon key
- display relation
- color intent
- distance intent, when applicable
- active/standby intent
- source accepted fact id or reason

Every accepted-but-not-displayed controller must log:

- callsign
- frequency
- facility group
- polygon key, if known
- relation: `FILTERED` or `HIDDEN`
- reason

## Required Tests

The contract is not complete until these scenarios exist and pass:

- Next-polygon center displays orange with distance.
- Current-polygon center displays green.
- Crossing from current polygon to next polygon changes the display intent
  without requiring a radio-board change.
- Distance to next polygon refreshes without rerunning heavy authority proof.
- Off-route CLE/MEM-style centers remain hidden.
- Two DEN-style center controllers in one relevant polygon are handled
  deterministically and logged.
- Arrival prep at 200nm still wakes arrival local/TRACON/center display.
- Unchanged board and unchanged polygon logs idle behavior with no authority
  work.

## Completion Definition

This contract is complete only when the brain owns controller display intent
end-to-end and diagnostics prove that UI rendering cannot trigger authority,
route, or world-scan work.

## Implementation Status

Status update 2026-05-20:

- Added `BrainDisplayIntentWorker` as the brain-owned layer between accepted
  controller facts and UI rows.
- Current-polygon centers are emitted as `CURRENT_POLYGON` display intent.
- Next-polygon centers are emitted as `NEXT_POLYGON` display intent with
  distance countdown derived from route progress.
- Hidden/off-route accepted facts are logged as display-intent hidden rows
  instead of leaking into the UI.
- The publisher now renders display-intent output instead of asking relevance
  to be the final UI painter.
- Added regression scenarios for current/next coloring and distance refresh.
- Full regression harness passed with `234` scenarios.

Installed plugin hash after this pass:

`40E32D881AEE270A64A16735E9A297DBDED2CBD11B2D7FB7BCF8DEBF5EC8C3B4`
