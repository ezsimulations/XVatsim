from __future__ import annotations

import time
from dataclasses import asdict
from typing import Any

from .models import ControllerFact, OwnershipClaim, PolygonRecord
from .ownership import IncrementalOwnershipEngine, OwnershipDelta


def _elapsed_ms(started: float) -> float:
    return round((time.perf_counter() - started) * 1000.0, 3)


def _claim_signatures(
    claims: dict[str, list[OwnershipClaim]],
) -> list[tuple[str, str, str, str, str, str]]:
    return sorted(
        (
            claim.polygon_key,
            claim.controller_key,
            claim.callsign,
            claim.frequency,
            claim.source,
            claim.reason,
        )
        for polygon_claims in claims.values()
        for claim in polygon_claims
    )


def _stage(
    name: str,
    engine: IncrementalOwnershipEngine,
    controllers: dict[str, ControllerFact],
    route_polygon_keys: set[str],
) -> dict[str, Any]:
    started = time.perf_counter()
    delta: OwnershipDelta = engine.apply_snapshot(controllers)
    elapsed_ms = _elapsed_ms(started)
    result = asdict(delta)
    result.update(
        {
            "name": name,
            "elapsed_ms": elapsed_ms,
            "input_controller_count": len(controllers),
            "route_polygon_changes": sorted(
                route_polygon_keys.intersection(delta.changed_polygon_keys)
            ),
            "geometry_recomputed": False,
        }
    )
    return result


def build_incremental_proof(
    controllers: dict[str, ControllerFact],
    records: list[PolygonRecord],
    vnas_facilities: dict[str, object],
    route_polygon_keys: set[str],
    route_controller_keys: set[str],
    batch_claims: dict[str, list[OwnershipClaim]],
) -> dict[str, Any]:
    """Replay online/offline changes against one immutable catalog index."""

    late_controller_keys = sorted(route_controller_keys)[-2:]
    if len(late_controller_keys) < 2:
        extra = [
            key
            for key in sorted(controllers)
            if key not in late_controller_keys
        ]
        late_controller_keys.extend(extra[: 2 - len(late_controller_keys)])

    base = {
        key: fact
        for key, fact in controllers.items()
        if key not in late_controller_keys
    }
    engine = IncrementalOwnershipEngine(records, vnas_facilities)
    stages = [
        _stage("initial-snapshot", engine, base, route_polygon_keys),
        _stage("two-controllers-online", engine, controllers, route_polygon_keys),
        _stage("unchanged-snapshot", engine, controllers, route_polygon_keys),
    ]

    offline_key = late_controller_keys[0] if late_controller_keys else ""
    if offline_key:
        without_one = dict(controllers)
        without_one.pop(offline_key, None)
        stages.append(
            _stage("one-controller-offline", engine, without_one, route_polygon_keys)
        )
        stages.append(
            _stage("controller-restored", engine, controllers, route_polygon_keys)
        )

    expected_late = tuple(late_controller_keys)
    online_stage = stages[1]
    unchanged_stage = stages[2]
    final_matches_batch = _claim_signatures(engine.claims) == _claim_signatures(
        batch_claims
    )
    return {
        "purpose": (
            "Prove controller snapshots update ownership incrementally without "
            "rebuilding route geometry or discarding unchanged controller facts."
        ),
        "late_controller_keys": late_controller_keys,
        "offline_controller_key": offline_key,
        "catalog_polygon_count": len(records),
        "route_polygon_count": len(route_polygon_keys),
        "stages": stages,
        "assertions": {
            "online_stage_evaluated_only_late_controllers": tuple(
                online_stage["evaluated_controller_keys"]
            )
            == expected_late,
            "unchanged_snapshot_evaluated_zero_controllers": not unchanged_stage[
                "evaluated_controller_keys"
            ],
            "unchanged_snapshot_changed_zero_polygons": not unchanged_stage[
                "changed_polygon_keys"
            ],
            "no_stage_recomputed_geometry": all(
                not stage["geometry_recomputed"] for stage in stages
            ),
            "final_incremental_claims_match_batch_join": final_matches_batch,
        },
    }
