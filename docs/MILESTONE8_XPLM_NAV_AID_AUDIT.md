# Milestone 8 XPLMFindNavAid Audit

Milestone 8 scope is limited to active source uses of `XPLMFindNavAid`.
The reliability rule is:

- Allowed: airport-only lookup used to validate or coordinate-stamp an airport.
- Not allowed: route waypoint resolution, airway expansion, sector selection,
  controller-authority matching, or any logic that can invent ATC relevance.

## Active Callsite Classification

| File | Function | Classification | Reason |
| --- | --- | --- | --- |
| `modules/network_plan_link/src/NetworkPlanLink.cpp` | `ResolveAirportCoordinates(...)` | Acceptable airport-resolution support | Resolves only a normalized filed VATSIM departure/destination airport as `xplm_Nav_Airport`, then rejects the result unless the resolved airport id exactly matches the requested ICAO. |
| `modules/flight_plan/src/FlightPlanSampler.cpp` | `ApplyNearestAirportFallback(...)` | Acceptable current-airport support | Finds only the nearest airport to a valid aircraft position, requires airport type, valid coordinates, and a 10 nm distance cap. It is used only to identify current airport/departure context, not route or controller authority. |
| `modules/diversion_context/src/DiversionContextModule.cpp` | `ResolveAirport(...)` | Acceptable manual diversion support | Accepts only a four-character normalized airport ICAO, resolves only `xplm_Nav_Airport`, and rejects any result whose resolved airport id does not exactly match the requested ICAO. |

## Explicit Non-Uses

- No active `XPLMFindNavAid` call resolves route fixes, VORs, NDBs, or airway
  endpoints.
- No active `XPLMFindNavAid` call participates in ENROUTE sector matching.
- No active `XPLMFindNavAid` call creates controller callsign prefixes,
  controller authority, or terminal/center coverage.

If a future change needs route or procedure navigation data, it must use the
internal nav graph and typed route parser, not `XPLMFindNavAid`.
