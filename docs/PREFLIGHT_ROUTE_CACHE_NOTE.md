# Preflight Route Cache Note

Date noted: 2026-05-17

This is a future performance feature, not a controller-authority truth source.
Do not implement it until the authority evidence contract is complete.

## Idea

Create an optional desktop companion tool, likely `XVatsimPreflightCache.exe`,
that pilots can run before launching X-Plane. XVatsim itself cannot run before
X-Plane because it is an in-sim plugin, so any preflight cache builder must be
external to the sim process.

## Intended Workflow

1. The pilot uses SimBrief Downloader to export the `X-Plane 11/12` `.fms`
   format into `C:\X-Plane 12\Output\FMS plans`.
2. Before launching X-Plane, the pilot runs the XVatsim preflight cache tool.
3. The tool lets the pilot select a stored `.fms` file, or optionally choose
   the newest `.fms` plan.
4. The tool parses the route, builds route geometry, identifies
   route-intersecting authority polygons, and writes a cache file into the
   XVatsim plugin folder.
5. When X-Plane launches, XVatsim reads the cache and avoids repeating the
   heaviest route/polygon preparation inside the sim startup or flight loop.

## Guardrails

- The cache is only a performance seed. Live VATSIM controller data,
  frequencies, transceivers, source ownership, and authority evidence still
  decide what appears in the UI.
- The cache must be invalidated if the selected `.fms` file, route waypoint
  sequence, departure, arrival, authority registry commit/hash, boundary data,
  or AIRAC cycle changes.
- Missing or stale cache must fall back to normal async behavior.
- This feature must never become required for correctness.

## Likely Cache Contents

- Selected `.fms` path and modified timestamp/hash.
- Departure and arrival ICAO.
- Route waypoint latitude/longitude sequence hash.
- Authority source registry commit/hash.
- Boundary and terminal source hash.
- Route-intersecting center/terminal polygon IDs.
- Precomputed route entry distances.
