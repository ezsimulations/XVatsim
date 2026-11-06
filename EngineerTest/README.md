# EngineerTest

EngineerTest is an isolated airspace-ownership laboratory. It does not build,
load, or modify the XVatsim plugin.

The first experiment answers one question:

> Can online controller facts light canonical polygons, and can a filed route
> identify which of those lit polygons it traverses, without votes or relevance
> filters?

## Authority model

- VATSpy/FIR GeoJSON owns Center geometry and Center prefix crosswalks.
- SimAware owns terminal polygon geometry and terminal position patterns.
- VATSIM owns live controller identity, frequency, and online state.
- vNAS owns active US position/sector assignments when present.
- The xPilot/XVatsim radio-board log is retained as an independent observed fact.
- The FMS plan owns the route polyline.
- Shapely performs geometry intersections. It never decides controller ownership.

There is no vote tally. Direct facts are joined by stable identifiers and every
claim retains its provenance. Conflicting owners and unmapped controllers are
reported instead of silently resolved.

## Run the UPS344 PANC-KONT replay

```powershell
.\run.ps1 `
  -FmsPath "C:\X-Plane 12\Output\FMS plans\PANCKONT01.fms" `
  -LogPath "C:\X-Plane 12\Resources\plugins\XVatsim\logs\xvatsim_diagnostics_2026_10_28.log" `
  -CaptureName "ups344_2026-10-28"
```

The first run downloads and caches public source data. Outputs are written to
`output/<capture-name>/`:

- `map.html`: interactive route and lit-polygon map.
- `results.json`: complete machine-readable evidence.
- `crossings.csv`: ordered route/polygon crossings.
- `timeline.csv`: entry/exit events and the active lit-polygon set after each
  route-progress transition.
- `route-state-proof.json`: current and next lit polygons from route progress;
  distance is reported but never used as an eligibility gate.
- `incremental-proof.json`: staged controller online/offline updates, the exact
  controllers evaluated, and the exact polygons whose ownership changed.
- `performance.json`: measured acquisition, catalog, intersection, and join time.
- `snapshots/`: immutable VATSIM, vNAS, radio-board, FMS, and source-hash inputs.

The route-source manifest explicitly lists crossed regions for which no
VATGlasses subdivision package exists. Missing detailed geometry is a visible
data gap and is never replaced by a guess.

Open `map.html` in a browser. Gray polygons are traversed but unowned in the
captured live facts. Filled polygons have at least one online ownership claim.
Center and terminal polygons remain separate layers and may overlap.

## Replay without live network data

```powershell
.\run.ps1 `
  -FmsPath ".\output\ups344_2026-10-28\snapshots\route.fms" `
  -SnapshotPath ".\output\ups344_2026-10-28\snapshots" `
  -OutputName "ups344-replay"
```

This must reproduce the same ownership and crossing result from the captured
inputs. Static source files are content-hashed in the snapshot manifest.

The replay also stages two route controllers coming online, an unchanged
refresh, one controller going offline, and restoration. Geometry and route
crossings remain immutable throughout those ownership-only updates.

Run the isolated unit tests with `./test.ps1`.

## Non-goals

EngineerTest does not tune radios, drive Assist, decide UI hierarchy, infer
airport ownership, or run on X-Plane's flight-loop thread. Those decisions are
intentionally excluded until the geometry and ownership premise is proven.

