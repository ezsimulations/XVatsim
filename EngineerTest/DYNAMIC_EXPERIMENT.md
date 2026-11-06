# UPS344 incremental ownership and route-progress experiment

Date: 2026-10-28

## Question

Can the fixed route/polygon map remain intact while controllers come online or
go offline, with only changed controller facts being processed? Can the engine
report the next owned route polygon even when it is much farther away than the
former 250/300 NM gates?

## Captured replay

The test reused the immutable UPS344 PANC-KONT snapshot and its 3,084-polygon
catalog. The established result remained unchanged:

- 36 route crossings;
- 15 lit route polygon records;
- 54 online operational controller facts;
- SoCal Approach owned by `LAX_S_DEP 124.300` through its active vNAS assumed
  TCPs;
- Seattle broad and child sectors owned by `SEA_16_CTR 135.450`;
- the broad Vancouver parent conflict retained explicitly;
- all controller facts, crossings, diagnostics, source manifests, and progress
  events identical across two new replays.

## Incremental online/offline proof

The replay began with the captured snapshot minus `LAX_S_DEP 124.300` and
`SEA_16_CTR 135.450`, then applied five complete controller snapshots:

1. The initial 52-controller snapshot built the starting ownership state.
2. The full 54-controller snapshot added LAX and Seattle. Only those two
   controllers were evaluated. The update took 2.339 ms in the recorded run.
3. Reapplying the identical 54-controller snapshot evaluated zero controllers
   and changed zero polygons.
4. Removing LAX retired only its two ownership claims. Of those, only the SoCal
   Approach polygon intersected this route.
5. Restoring LAX evaluated only LAX and restored the same two claims.

No stage recalculated route geometry. After the complete sequence, incremental
ownership exactly matched a fresh batch ownership join.

Detailed receipt:
`output/ups344-dynamic-proof/incremental-proof.json`.

## No-distance-gate proof

At route progress 0.0 NM, the first lit route polygon was Vancouver Sandspit:

- entry: 680.542 route NM;
- owner: `CZVR_CTR 133.700`;
- distance gate applied: false.

The engine therefore found and retained the next owned route polygon more than
twice as far away as the former 300 NM limit. Distance is useful context, not
permission to include or exclude a polygon.

Receipt: `output/ups344-dynamic-proof/route-state-proof.json`.

## Verification

- Eight isolated unit tests pass, including overlapping APP/DEP retention and
  same-polygon multi-owner conflict preservation without selecting a winner.
- Full Python compilation passes.
- Two dynamic replays are identical after excluding timing measurements.
- The established full-flight ownership result did not change.
- Production XVatsim source, build configuration, XPL, and X-Plane files were
  not modified.

## Engineering conclusion

The experiment now supports a stable two-part runtime:

1. route geometry and ordered crossings are computed once when the plan or
   authority data changes;
2. complete controller snapshots update only added, removed, or changed
   ownership claims.

This supports the desired behavior: finish processing the current board, then
process newly arrived controller facts without cancelling or restarting the
route/polygon work.
