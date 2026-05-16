# Modules

Each module performs one isolated task and exposes an explicit input/output seam.

Rules:

- modules do not talk to peer modules directly
- modules do not invent missing truth for another source
- modules return unavailable/stale/empty state rather than substituting guessed data
- modules must be part of either the live plugin build or the regression harness to remain in the release source tree

Live module set:

- `aircraft_state`
- `arrival`
- `controller_feed`
- `ctaf_lookup`
- `departure`
- `diversion_context`
- `enroute`
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
