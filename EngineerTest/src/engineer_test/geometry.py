from __future__ import annotations

import json
import math
from collections.abc import Iterable
from typing import Any

from shapely.geometry import LineString, Polygon, shape
from shapely.validation import make_valid
from shapely.strtree import STRtree

from .models import PolygonRecord, RouteCrossing, RoutePoint


EARTH_RADIUS_NM = 3440.065


def normalize_token(value: Any) -> str:
    return "".join(ch for ch in str(value).strip().upper() if ch.isalnum() or ch == "_")


def parse_vatspy_fir_prefixes(payload: str) -> dict[str, set[str]]:
    by_boundary: dict[str, set[str]] = {}
    in_firs = False
    for raw_line in payload.splitlines():
        line = raw_line.strip()
        if line == "[FIRs]":
            in_firs = True
            continue
        if in_firs and line.startswith("["):
            break
        if not in_firs or not line or line.startswith(";"):
            continue
        fields = [field.strip() for field in line.split("|")]
        if len(fields) < 4:
            continue
        identifier, _name, prefix, boundary = fields[:4]
        boundary = normalize_token(boundary or identifier)
        if not boundary:
            continue
        tokens = by_boundary.setdefault(boundary, set())
        tokens.add(normalize_token(identifier))
        if prefix:
            tokens.add(normalize_token(prefix))
    return by_boundary


def build_catalog(
    fir_payload: bytes, tracon_payload: bytes, vatspy_payload: bytes
) -> list[PolygonRecord]:
    fir = json.loads(fir_payload)
    tracon = json.loads(tracon_payload)
    fir_prefixes = parse_vatspy_fir_prefixes(vatspy_payload.decode("utf-8", "replace"))
    records: list[PolygonRecord] = []

    for index, feature in enumerate(fir.get("features", [])):
        properties = dict(feature.get("properties") or {})
        identifier = normalize_token(properties.get("id", ""))
        if not identifier or not feature.get("geometry"):
            continue
        geometry = shape(feature["geometry"])
        if not geometry.is_valid:
            properties["engineer_test_geometry_repaired"] = True
            geometry = make_valid(geometry)
        if geometry.is_empty:
            continue
        prefixes = tuple(sorted(fir_prefixes.get(identifier, {identifier})))
        records.append(
            PolygonRecord(
                key=f"VATSPY_FIR:{identifier}:{index}",
                layer="CENTER",
                source="VATSPY_FIR",
                label=identifier,
                geometry=geometry,
                properties=properties,
                prefixes=prefixes,
            )
        )

    for index, feature in enumerate(tracon.get("features", [])):
        properties = dict(feature.get("properties") or {})
        identifier = normalize_token(properties.get("id", ""))
        if not identifier or not feature.get("geometry"):
            continue
        prefixes_value = properties.get("prefix") or []
        if isinstance(prefixes_value, str):
            prefixes_value = [prefixes_value]
        prefixes = tuple(
            sorted({normalize_token(value) for value in prefixes_value if value})
        )
        suffix = normalize_token(properties.get("suffix", "APP") or "APP")
        geometry = shape(feature["geometry"])
        if not geometry.is_valid:
            properties["engineer_test_geometry_repaired"] = True
            geometry = make_valid(geometry)
        if geometry.is_empty:
            continue
        discriminator = "-".join(prefixes) or "NONE"
        records.append(
            PolygonRecord(
                key=f"SIMAWARE_TRACON:{identifier}:{discriminator}:{suffix}:{index}",
                layer="TERMINAL",
                source="SIMAWARE_TRACON",
                label=f"{identifier} {properties.get('name', '').strip()}".strip(),
                geometry=geometry,
                properties=properties,
                prefixes=prefixes,
                suffix=suffix,
            )
        )
    return records


def _parse_dms(value: Any) -> float:
    text = str(value).strip()
    sign = -1.0 if text.startswith("-") else 1.0
    digits = text.lstrip("+-")
    if len(digits) < 5 or not digits.isdigit():
        raise ValueError(f"invalid DMS coordinate: {value}")
    degrees = int(digits[:-4])
    minutes = int(digits[-4:-2])
    seconds = int(digits[-2:])
    return sign * (degrees + minutes / 60.0 + seconds / 3600.0)


def _mapping_items(value: Any) -> list[tuple[str, dict[str, Any]]]:
    if isinstance(value, dict):
        return [(str(key), item) for key, item in value.items() if isinstance(item, dict)]
    if isinstance(value, list):
        return [(str(index), item) for index, item in enumerate(value) if isinstance(item, dict)]
    return []


def _position_patterns(
    positions: dict[str, dict[str, Any]], owner_token: str
) -> set[str]:
    position = positions.get(owner_token)
    if not position:
        return set()
    role = normalize_token(position.get("type", ""))
    token = normalize_token(owner_token)
    sector = token[1:] if len(token) > 1 and token[0].isalpha() else token
    prefixes = position.get("pre") or []
    if isinstance(prefixes, str):
        prefixes = [prefixes]
    patterns: set[str] = set()
    for prefix_value in prefixes:
        prefix = normalize_token(prefix_value)
        if not prefix or not role:
            continue
        patterns.add(f"{prefix}_{sector}_{role}" if sector else f"{prefix}_{role}")
        patterns.add(f"{prefix}_{role}")
    return patterns


def build_vatglasses_catalog(packages: list[dict[str, Any]]) -> list[PolygonRecord]:
    records: list[PolygonRecord] = []
    for package_index, package in enumerate(packages):
        root = package.get("payload") or {}
        airspace_root = package.get("airspace") or (
            root.get("airspace", {}) if isinstance(root, dict) else {}
        )
        positions_root = package.get("positions") or (
            root.get("positions", {}) if isinstance(root, dict) else {}
        )
        ownership_root = package.get("ownership") or {}
        positions = {key: value for key, value in _mapping_items(
            positions_root.get("positions", positions_root)
            if isinstance(positions_root, dict)
            else positions_root
        )}
        ownership_airspace = (
            ownership_root.get("airspace", {})
            if isinstance(ownership_root, dict)
            else {}
        )
        owners_by_airspace: dict[str, set[str]] = {}
        if isinstance(ownership_airspace, dict):
            for owner, owned_ids in ownership_airspace.items():
                for owned_id in owned_ids or []:
                    owners_by_airspace.setdefault(str(owned_id), set()).add(str(owner))

        airspace_value = (
            airspace_root.get("airspace", airspace_root)
            if isinstance(airspace_root, dict)
            else airspace_root
        )
        for airspace_key, airspace in _mapping_items(airspace_value):
            owner_tokens = {
                str(value) for value in (airspace.get("owner") or []) if value
            }
            owner_tokens.update(owners_by_airspace.get(airspace_key, set()))
            patterns: set[str] = set()
            for owner_token in owner_tokens:
                patterns.update(_position_patterns(positions, owner_token))
            group = normalize_token(airspace.get("group", ""))
            if group == "CTR":
                layer = "CENTER_SECTOR"
            elif group in {"APP", "DEP"} or group.startswith("N"):
                layer = "TERMINAL_SECTOR"
            else:
                layer = "OTHER_SECTOR"
            label = str(airspace.get("id") or airspace_key)
            for sector_index, sector in enumerate(airspace.get("sectors", []) or []):
                raw_points = sector.get("points") or []
                try:
                    coordinates = [
                        (_parse_dms(pair[1]), _parse_dms(pair[0]))
                        for pair in raw_points
                        if isinstance(pair, list) and len(pair) >= 2
                    ]
                except ValueError:
                    continue
                if len(coordinates) < 3:
                    continue
                if coordinates[0] != coordinates[-1]:
                    coordinates.append(coordinates[0])
                geometry = Polygon(coordinates)
                properties = {
                    "id": airspace_key,
                    "name": label,
                    "group": group,
                    "owner_tokens": sorted(owner_tokens),
                    "controller_callsigns": sorted(patterns),
                    "min_fl": sector.get("min", 0),
                    "max_fl": sector.get("max", 999),
                    "regions": package.get("regions", []),
                }
                if not geometry.is_valid:
                    properties["engineer_test_geometry_repaired"] = True
                    geometry = make_valid(geometry)
                if geometry.is_empty:
                    continue
                region = "-".join(package.get("regions", [])) or str(package_index)
                records.append(
                    PolygonRecord(
                        key=(
                            f"VATGLASSES:{region}:{airspace_key}:{sector_index}:"
                            f"{properties['min_fl']}-{properties['max_fl']}"
                        ),
                        layer=layer,
                        source="VATGLASSES",
                        label=f"{region} {label}",
                        geometry=geometry,
                        properties=properties,
                    )
                )
    return records


def haversine_nm(a: tuple[float, float], b: tuple[float, float]) -> float:
    lon1, lat1 = map(math.radians, a)
    lon2, lat2 = map(math.radians, b)
    dlat = lat2 - lat1
    dlon = lon2 - lon1
    value = math.sin(dlat / 2) ** 2 + math.cos(lat1) * math.cos(lat2) * math.sin(dlon / 2) ** 2
    return 2 * EARTH_RADIUS_NM * math.asin(min(1.0, math.sqrt(value)))


def route_coordinates(points: list[RoutePoint]) -> list[tuple[float, float]]:
    return [(point.longitude, point.latitude) for point in points]


def cumulative_route_distances(coords: list[tuple[float, float]]) -> list[float]:
    values = [0.0]
    for first, second in zip(coords, coords[1:]):
        values.append(values[-1] + haversine_nm(first, second))
    return values


def _route_altitude_ft(
    progress_nm: float, points: list[RoutePoint], cumulative: list[float]
) -> float | None:
    for index in range(len(cumulative) - 1):
        if progress_nm <= cumulative[index + 1]:
            first = points[index].altitude_ft
            second = points[index + 1].altitude_ft
            if first is None or second is None:
                return first if first is not None else second
            span = cumulative[index + 1] - cumulative[index]
            fraction = 0.0 if span <= 0 else (progress_nm - cumulative[index]) / span
            return first + max(0.0, min(1.0, fraction)) * (second - first)
    return points[-1].altitude_ft


def _progress_nm(
    coordinate: tuple[float, float],
    coords: list[tuple[float, float]],
    cumulative: list[float],
) -> float:
    px, py = coordinate
    best_distance = float("inf")
    best_progress = 0.0
    for index, (a, b) in enumerate(zip(coords, coords[1:])):
        ax, ay = a
        bx, by = b
        dx = bx - ax
        dy = by - ay
        denominator = dx * dx + dy * dy
        fraction = 0.0 if denominator == 0 else ((px - ax) * dx + (py - ay) * dy) / denominator
        fraction = max(0.0, min(1.0, fraction))
        nx = ax + fraction * dx
        ny = ay + fraction * dy
        distance = (px - nx) ** 2 + (py - ny) ** 2
        if distance < best_distance:
            best_distance = distance
            best_progress = cumulative[index] + fraction * haversine_nm(a, b)
    return best_progress


def _line_components(geometry: Any) -> Iterable[Any]:
    if geometry.is_empty:
        return
    if geometry.geom_type in {"LineString", "LinearRing"}:
        yield geometry
        return
    if geometry.geom_type == "Point":
        yield geometry
        return
    for child in getattr(geometry, "geoms", []):
        yield from _line_components(child)


def intersect_route(
    points: list[RoutePoint], records: list[PolygonRecord]
) -> tuple[LineString, list[RouteCrossing], dict[str, PolygonRecord]]:
    coords = route_coordinates(points)
    route = LineString(coords)
    cumulative = cumulative_route_distances(coords)
    geometries = [record.geometry for record in records]
    tree = STRtree(geometries)
    crossings: list[RouteCrossing] = []
    records_by_key: dict[str, PolygonRecord] = {}

    for candidate_index in tree.query(route):
        record = records[int(candidate_index)]
        intersection = route.intersection(record.geometry)
        if intersection.is_empty:
            continue
        progress_values: list[float] = []
        for component in _line_components(intersection):
            if component.geom_type == "Point":
                progress_values.append(_progress_nm((component.x, component.y), coords, cumulative))
            else:
                component_coords = list(component.coords)
                if component_coords:
                    progress_values.append(_progress_nm(component_coords[0], coords, cumulative))
                    progress_values.append(_progress_nm(component_coords[-1], coords, cumulative))
        if not progress_values:
            continue
        entry_nm = min(progress_values)
        exit_nm = max(progress_values)
        minimum_fl = record.properties.get("min_fl")
        maximum_fl = record.properties.get("max_fl")
        if minimum_fl is not None or maximum_fl is not None:
            altitude_ft = _route_altitude_ft(
                (entry_nm + exit_nm) / 2.0, points, cumulative
            )
            if altitude_ft is not None:
                flight_level = altitude_ft / 100.0
                minimum = float(minimum_fl or 0)
                maximum = float(maximum_fl if maximum_fl is not None else 999)
                if flight_level < minimum or flight_level > maximum:
                    continue
        crossings.append(
            RouteCrossing(
                polygon_key=record.key,
                layer=record.layer,
                label=record.label,
                source=record.source,
                entry_nm=entry_nm,
                exit_nm=exit_nm,
            )
        )
        records_by_key[record.key] = record

    crossings.sort(key=lambda item: (item.entry_nm, item.layer, item.exit_nm, item.polygon_key))
    return route, crossings, records_by_key

