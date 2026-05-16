# Brain Model

Updated: 2026-05-15

## Current Role

The brain does not discover controllers, parse routes, fetch feeds, or invent missing
source truth. Those responsibilities belong to explicit source, workflow, and route
authority layers.

For V1, the brain converts validated live state into a safe overlay view model.

## Inputs

- workflow stage and reason
- aircraft state snapshot
- xPilot session snapshot
- radio state snapshot
- matched VATSIM network plan snapshot
- fresh controller feed snapshot
- transceiver resolution snapshot when needed
- selected module board snapshot
- manual query/status snapshot

## Outputs

- overlay mode and visibility
- title, status, and footer text
- COM1/COM2/TX/RX/MODE C/Assist display state
- body lines for the selected board
- bounded optional action affordances

## Reliability Rules

- Display formatting must clamp and sanitize live text.
- Missing or stale source data must stay visible as unavailable/stale state, not be
  replaced with guessed substitutes.
- The brain may format already-resolved data, but it must not create controller
  authority or route truth.
- Experimental UI surfaces must stay behind explicit release gates.
