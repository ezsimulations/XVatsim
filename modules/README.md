# Modules

Each live module performs one isolated task and exposes an explicit
input/output seam. The brain decides when a module runs and what facts are
accepted for display.

Rules:

- modules do not talk to peer modules directly
- modules do not decide workflow phase or UI display truth
- modules do not invent missing truth for another source
- modules return unavailable/stale/empty state rather than substituting guessed data
- modules must be part of either the live plugin build or the regression harness to remain in the release source tree

Live module set:

- `aircraft_state`
- `controller_feed`
- `ctaf_lookup`
- `diversion_context`
- `flight_plan`
- `network_plan_link`
- `overlay`
- `pilot_identity`
- `radio_state`
- `route_sector`
- `settings_store`
- `transceiver_resolver`
- `vatsim_data_feed`
- `xpilot_bridge`

Harness-only legacy board coverage:

- `arrival`
- `departure`
- `enroute`

These old board collectors are compiled only when
`XVATSIM_BUILD_REGRESSION_HARNESS` is enabled. They are retained to protect
historical regression scenarios while the live plugin runs the Engineer 3
brain-owned path: radio board facts, route polygon facts, controller relevance
facts, brain publisher, then UI render.
