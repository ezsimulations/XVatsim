from __future__ import annotations

import argparse
import json
import shutil
import time
from pathlib import Path

from .geometry import build_catalog, build_vatglasses_catalog, intersect_route
from .ownership import build_ownership_claims
from .proofs import build_incremental_proof
from .report import route_state_at, write_outputs
from .sources import (
    acquire_live_snapshots,
    load_snapshot_bundle,
    load_static_sources,
    load_route_authority_sources,
    merge_controller_sets,
    parse_fms,
    parse_vatsim_controllers,
    parse_vnas_controllers,
    write_snapshot_bundle,
)


def elapsed_ms(started: float) -> float:
    return round((time.perf_counter() - started) * 1000.0, 3)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Route-scoped live polygon experiment")
    parser.add_argument("--fms", required=True, type=Path)
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--cache", required=True, type=Path)
    parser.add_argument("--capture", default="")
    parser.add_argument("--snapshot", type=Path)
    parser.add_argument("--refresh-sources", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    performance: dict[str, float] = {}
    total_started = time.perf_counter()

    started = time.perf_counter()
    static = load_static_sources(args.cache, args.refresh_sources)
    performance["static_source_acquisition"] = elapsed_ms(started)

    started = time.perf_counter()
    points = parse_fms(args.fms)
    performance["route_parse"] = elapsed_ms(started)

    started = time.perf_counter()
    catalog = build_catalog(
        static["fir_boundaries"], static["tracon_boundaries"], static["vatspy"]
    )
    performance["catalog_build"] = elapsed_ms(started)

    started = time.perf_counter()
    _route, base_crossings, _base_records = intersect_route(points, catalog)
    route_regions = {
        crossing.label
        for crossing in base_crossings
        if crossing.layer == "CENTER"
    }
    performance["base_route_intersection"] = elapsed_ms(started)

    registry_path = (
        Path(__file__).resolve().parents[3]
        / "assets"
        / "source_data"
        / "authority_source_registry.json"
    )
    started = time.perf_counter()
    packages, route_authority_manifest = load_route_authority_sources(
        registry_path,
        route_regions,
        args.cache,
        args.refresh_sources,
    )
    performance["route_authority_source_acquisition"] = elapsed_ms(started)
    started = time.perf_counter()
    catalog.extend(build_vatglasses_catalog(packages))
    performance["route_authority_catalog_build"] = elapsed_ms(started)

    started = time.perf_counter()
    _route, crossings, records_by_key = intersect_route(points, catalog)
    performance["route_intersection"] = elapsed_ms(started)
    static["manifest"]["route_authority"] = route_authority_manifest

    started = time.perf_counter()
    if args.snapshot:
        vatsim_payload, vnas_payload, vnas_facilities, board = load_snapshot_bundle(
            args.snapshot
        )
    else:
        vatsim_payload, vnas_payload, vnas_facilities, board = acquire_live_snapshots(
            args.log
        )
    performance["live_fact_acquisition"] = elapsed_ms(started)

    started = time.perf_counter()
    controllers = merge_controller_sets(
        parse_vatsim_controllers(vatsim_payload),
        parse_vnas_controllers(vnas_payload),
        board,
    )
    claims, diagnostics = build_ownership_claims(
        controllers, catalog, vnas_facilities
    )
    for crossing in crossings:
        crossing.claims = list(claims.get(crossing.polygon_key, []))
    route_conflicts = []
    for crossing in crossings:
        owners = sorted({claim.controller_key for claim in crossing.claims})
        if len(owners) > 1:
            route_conflicts.append(
                f"{crossing.polygon_key}:" + ",".join(owners)
            )
    diagnostics["route_conflicts"] = route_conflicts
    performance["ownership_join"] = elapsed_ms(started)

    output_dir = args.output
    if args.capture:
        output_dir = args.output.parent / args.capture
        write_snapshot_bundle(
            output_dir / "snapshots",
            vatsim_payload,
            vnas_payload,
            vnas_facilities,
            board,
            args.fms,
            static["manifest"],
        )

    started = time.perf_counter()
    write_outputs(
        output_dir,
        points,
        crossings,
        records_by_key,
        controllers,
        dict(diagnostics),
        performance,
        static["manifest"],
    )
    performance["report_write"] = elapsed_ms(started)

    started = time.perf_counter()
    route_polygon_keys = {crossing.polygon_key for crossing in crossings}
    route_controller_keys = {
        claim.controller_key
        for crossing in crossings
        for claim in crossing.claims
    }
    incremental_proof = build_incremental_proof(
        controllers,
        catalog,
        vnas_facilities,
        route_polygon_keys,
        route_controller_keys,
        claims,
    )
    (output_dir / "incremental-proof.json").write_text(
        json.dumps(incremental_proof, indent=2), encoding="utf-8"
    )
    (output_dir / "route-state-proof.json").write_text(
        json.dumps(route_state_at(crossings, 0.0), indent=2), encoding="utf-8"
    )
    performance["proof_generation"] = elapsed_ms(started)
    performance["total"] = elapsed_ms(total_started)
    (output_dir / "performance.json").write_text(
        json.dumps(performance, indent=2), encoding="utf-8"
    )

    lit = [crossing for crossing in crossings if crossing.claims]
    print(f"Catalog polygons: {len(catalog)}")
    print(f"Route crossings: {len(crossings)}")
    print(f"Lit route polygons: {len(lit)}")
    print(f"Online controller facts: {len(controllers)}")
    print(f"Unmapped controllers: {len(diagnostics.get('unmapped', []))}")
    print(f"Ownership conflicts: {len(diagnostics.get('conflicts', []))}")
    print(f"Output: {output_dir / 'map.html'}")
    for crossing in lit:
        owners = ", ".join(
            f"{claim.callsign} {claim.frequency}" for claim in crossing.claims
        )
        print(
            f"LIT {crossing.entry_nm:.1f}-{crossing.exit_nm:.1f} NM "
            f"{crossing.layer} {crossing.label}: {owners}"
        )
    return 0 if crossings else 2


if __name__ == "__main__":
    raise SystemExit(main())

