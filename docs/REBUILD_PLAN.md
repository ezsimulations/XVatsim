# XVatsim Rebuild Plan

This is the authoritative milestone map for the reliability rebuild. The goal is
trustworthy behavior, not "good enough" behavior.

## Milestone 1 - Offline Regression Harness

Build a deterministic runner that can replay saved route text, aircraft position,
online controllers, sector data, and expected outputs without X-Plane flights.

Primary target files:
- `plugin/src/XVatsimPlugin.cpp`
- `modules/route_sector/src/RouteSectorResolver.cpp`
- `modules/enroute/src/EnrouteModule.cpp`

## Milestone 2 - Remove Masking Fallbacks

Remove fallbacks that can hide bad state. If the route engine cannot prove center
coverage, the plugin should fail closed and expose that truth instead of inventing
plausible-looking output.

Primary target:
- `ApplyArrivalCenterCoverageFallback(...)` in `plugin/src/XVatsimPlugin.cpp`

## Milestone 3 - True Route Geometry

Route-to-sector traversal must use true route-leg versus polygon intersection
logic. Current and next sector authority must come from actual crossings, not
sampled approximations.

Primary target:
- `modules/route_sector/src/RouteSectorResolver.cpp`

## Milestone 4 - Typed Route Grammar

Route parsing must be structured input, not token-shape inference. Tokens must be
classified as control, coordinate, point, airway, or procedure. Airway tokens must
never be eligible for point resolution.

Primary target:
- `modules/route_sector/src/RouteSectorResolver.cpp`

## Milestone 5 - Deterministic Internal Nav Graph

Point resolution must be deterministic. Duplicate idents need to resolve by graph
truth, region/type, and segment context, not nearest-plausible selection.

Primary target:
- `ResolveRoutePointToken(...)` paths in `modules/route_sector/src/RouteSectorResolver.cpp`

## Milestone 6 - Global Controller Authority Catalog

Finish the controller authority catalog and make center/enroute matching fully
data-driven. Matching must come from authoritative external data and controller
feed truth, not inferred aliases. Unmatched center-controller cases should become
explicit data gaps, not logic surprises.

Primary target files:
- `modules/route_sector/src/RouteSectorResolver.cpp`
- `modules/enroute/src/EnrouteModule.cpp`

## Milestone 7 - Workflow Ownership From Proven State

Departure, Enroute, and Arrival ownership should transition from proven geometry
and aircraft state first. Timers should be protective wrappers, not primary truth.

Primary targets:
- `ResolveWorkflowStage(...)`
- `CanConfirmDepartureLocation(...)`

## Milestone 8 - XPLMFindNavAid Audit

Every remaining `XPLMFindNavAid` use must be classified as either acceptable
airport-resolution support or replaced.

Primary target files:
- `modules/network_plan_link/src/NetworkPlanLink.cpp`
- `modules/flight_plan/src/FlightPlanSampler.cpp`
- `modules/diversion_context/src/DiversionContextModule.cpp`

## Milestone 9 - Line-By-Line Core Cleanup

After the structural rebuilds land, audit the core modules line by line and remove
obsolete branches, stale latches, duplicate logic, and old safety hacks.

Primary target files:
- `plugin/src/XVatsimPlugin.cpp`
- `modules/route_sector/src/RouteSectorResolver.cpp`
- `modules/enroute/src/EnrouteModule.cpp`
- `modules/departure/src/DepartureModule.cpp`
- arrival modules
