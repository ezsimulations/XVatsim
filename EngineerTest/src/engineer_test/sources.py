from __future__ import annotations

import hashlib
import json
import re
import shutil
from dataclasses import asdict
from pathlib import Path
from typing import Any

import requests

from .models import ControllerFact, RoutePoint


FIR_BOUNDARIES_URL = (
    "https://raw.githubusercontent.com/vatsimnetwork/"
    "vatspy-data-project/master/Boundaries.geojson"
)
VATSPY_URL = (
    "https://raw.githubusercontent.com/vatsimnetwork/"
    "vatspy-data-project/master/VATSpy.dat"
)
TRACON_BOUNDARIES_URL = (
    "https://github.com/vatsimnetwork/simaware-tracon-project/"
    "releases/latest/download/TRACONBoundaries.geojson"
)
VATSIM_DATA_URL = "https://data.vatsim.net/v3/vatsim-data.json"
VNAS_CONTROLLERS_URL = "https://live.env.vnas.vatsim.net/data-feed/controllers.json"
VNAS_FACILITY_URL = "https://data-api.vnas.vatsim.net/api/artccs/{artcc_id}"


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def normalize_frequency(value: Any) -> str:
    if value is None:
        return ""
    if isinstance(value, (int, float)) and float(value) > 1_000_000:
        return f"{float(value) / 1_000_000.0:.3f}"
    text = str(value).strip()
    try:
        return f"{float(text):.3f}"
    except ValueError:
        digits = "".join(ch for ch in text if ch.isdigit())
        if len(digits) >= 6:
            return f"{int(digits) / 1000.0:.3f}"
        return text


def fetch_cached(url: str, cache_path: Path, refresh: bool = False) -> bytes:
    if cache_path.exists() and not refresh:
        return cache_path.read_bytes()
    response = requests.get(url, timeout=45)
    response.raise_for_status()
    cache_path.parent.mkdir(parents=True, exist_ok=True)
    cache_path.write_bytes(response.content)
    return response.content


def load_static_sources(cache_dir: Path, refresh: bool = False) -> dict[str, Any]:
    specs = {
        "fir_boundaries": (FIR_BOUNDARIES_URL, cache_dir / "Boundaries.geojson"),
        "tracon_boundaries": (
            TRACON_BOUNDARIES_URL,
            cache_dir / "TRACONBoundaries.geojson",
        ),
        "vatspy": (VATSPY_URL, cache_dir / "VATSpy.dat"),
    }
    result: dict[str, Any] = {}
    manifest: dict[str, Any] = {}
    for key, (url, path) in specs.items():
        payload = fetch_cached(url, path, refresh)
        result[key] = payload
        manifest[key] = {
            "url": url,
            "path": str(path),
            "bytes": len(payload),
            "sha256": sha256_bytes(payload),
        }
    result["manifest"] = manifest
    return result


def _region_aliases(region: str) -> set[str]:
    normalized = str(region).strip().upper()
    aliases = {normalized}
    if normalized.startswith("KZ") and len(normalized) == 4:
        aliases.add(normalized[1:])
    if normalized.startswith("CZ") and len(normalized) == 4:
        aliases.add("Z" + normalized[2:])
    if normalized == "PAZA":
        aliases.add("ZAN")
    return aliases


def load_route_authority_sources(
    registry_path: Path,
    route_regions: set[str],
    cache_dir: Path,
    refresh: bool = False,
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    if not registry_path.exists():
        return [], {"status": "registry-unavailable", "path": str(registry_path)}
    registry_bytes = registry_path.read_bytes()
    registry = json.loads(registry_bytes)
    wanted: set[str] = set()
    for region in route_regions:
        wanted.update(_region_aliases(region))
    packages: list[dict[str, Any]] = []
    coverage: dict[str, list[str]] = {region: [] for region in sorted(route_regions)}
    manifest: dict[str, Any] = {
        "registry_path": str(registry_path),
        "registry_sha256": sha256_bytes(registry_bytes),
        "wanted_regions": sorted(wanted),
        "packages": [],
    }
    for index, entry in enumerate(registry.get("authority_sources", [])):
        regions = {str(value).strip().upper() for value in entry.get("regions", [])}
        if not regions.intersection(wanted):
            continue
        source = str(entry.get("source", ""))
        package: dict[str, Any] = {"source": source, "regions": sorted(regions)}
        package_manifest: dict[str, Any] = {
            "source": source,
            "regions": sorted(regions),
            "files": {},
        }
        url_fields = {
            "payload": "url",
            "positions": "positions_url",
            "airspace": "airspace_url",
            "ownership": "ownership_url",
        }
        for payload_name, field_name in url_fields.items():
            url = entry.get(field_name)
            if not url:
                continue
            region_name = "-".join(sorted(regions)) or f"package-{index}"
            filename = f"{region_name}-{payload_name}.json"
            payload = fetch_cached(
                str(url), cache_dir / "vatglasses" / filename, refresh
            )
            package[payload_name] = json.loads(payload)
            package_manifest["files"][payload_name] = {
                "url": url,
                "bytes": len(payload),
                "sha256": sha256_bytes(payload),
            }
        packages.append(package)
        manifest["packages"].append(package_manifest)
        for route_region in coverage:
            if regions.intersection(_region_aliases(route_region)):
                coverage[route_region].extend(sorted(regions))
    manifest["status"] = "ready"
    manifest["package_count"] = len(packages)
    manifest["route_region_coverage"] = {
        region: sorted(set(matches)) for region, matches in coverage.items()
    }
    manifest["missing_route_regions"] = sorted(
        region for region, matches in coverage.items() if not matches
    )
    return packages, manifest


def parse_fms(path: Path) -> list[RoutePoint]:
    points: list[RoutePoint] = []
    for raw_line in path.read_text(encoding="utf-8-sig", errors="replace").splitlines():
        fields = raw_line.split()
        if len(fields) < 6 or fields[0] not in {"1", "2", "3", "11", "28"}:
            continue
        try:
            altitude = float(fields[-3])
            latitude = float(fields[-2])
            longitude = float(fields[-1])
        except ValueError:
            continue
        if not (-90 <= latitude <= 90 and -180 <= longitude <= 180):
            continue
        points.append(
            RoutePoint(
                name=fields[1],
                latitude=latitude,
                longitude=longitude,
                altitude_ft=altitude,
            )
        )
    if len(points) < 2:
        raise ValueError(f"FMS route has fewer than two usable points: {path}")
    return points


def _merge_controller(
    controllers: dict[str, ControllerFact], incoming: ControllerFact
) -> None:
    existing = controllers.get(incoming.key)
    if existing is None:
        controllers[incoming.key] = incoming
        return
    existing.sources.update(incoming.sources)
    existing.on_radio_board = existing.on_radio_board or incoming.on_radio_board
    if incoming.facility and not existing.facility:
        existing.facility = incoming.facility
    if incoming.radio_name:
        existing.radio_name = incoming.radio_name
    if incoming.vnas_positions:
        existing.vnas_positions.extend(incoming.vnas_positions)
    if incoming.vnas_artcc_id:
        existing.vnas_artcc_id = incoming.vnas_artcc_id


def parse_vatsim_controllers(payload: dict[str, Any]) -> dict[str, ControllerFact]:
    result: dict[str, ControllerFact] = {}
    for row in payload.get("controllers", []):
        callsign = str(row.get("callsign", "")).strip().upper()
        frequency = normalize_frequency(row.get("frequency"))
        try:
            facility_number = int(row.get("facility", 0))
        except (TypeError, ValueError):
            facility_number = 0
        if not callsign or not frequency or facility_number < 1 or facility_number > 6:
            continue
        fact = ControllerFact(
            callsign=callsign,
            frequency=frequency,
            facility=str(facility_number),
            sources={"VATSIM"},
        )
        _merge_controller(result, fact)
    return result


def parse_vnas_controllers(payload: Any) -> dict[str, ControllerFact]:
    rows = payload.get("controllers", []) if isinstance(payload, dict) else payload
    result: dict[str, ControllerFact] = {}
    for row in rows or []:
        if not row.get("isActive") or row.get("isObserver"):
            continue
        vatsim = row.get("vatsimData") or {}
        callsign = str(vatsim.get("callsign", "")).strip().upper()
        frequency = normalize_frequency(vatsim.get("primaryFrequency"))
        if not callsign or not frequency:
            continue
        positions = [
            position
            for position in row.get("positions", [])
            if position.get("isActive", True)
        ]
        radio_name = next(
            (str(position.get("radioName", "")) for position in positions if position.get("isPrimary")),
            "",
        )
        fact = ControllerFact(
            callsign=callsign,
            frequency=frequency,
            facility=str(vatsim.get("facilityType", "")),
            sources={"VNAS"},
            radio_name=radio_name,
            vnas_positions=positions,
            vnas_artcc_id=str(row.get("artccId", "")).strip().upper(),
        )
        _merge_controller(result, fact)
    return result


_BOARD_CANDIDATE = re.compile(
    r"(?P<callsign>[A-Z0-9_\-]+)@(?P<frequency>\d{3}\.\d{3})"
    r":facility=(?P<facility>\d+):group=(?P<group>[A-Z_]+)"
)


def parse_latest_radio_board(log_path: Path) -> dict[str, ControllerFact]:
    if not log_path.exists():
        return {}
    latest = ""
    with log_path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if "event=radio-board-candidate-diff" in line:
                latest = line
    if not latest or " candidates=" not in latest:
        return {}
    candidate_text = latest.split(" candidates=", 1)[1].split(" status=", 1)[0]
    result: dict[str, ControllerFact] = {}
    for match in _BOARD_CANDIDATE.finditer(candidate_text):
        fact = ControllerFact(
            callsign=match.group("callsign"),
            frequency=match.group("frequency"),
            facility=match.group("group"),
            sources={"RADIO_BOARD"},
            on_radio_board=True,
        )
        _merge_controller(result, fact)
    return result


def merge_controller_sets(*sets: dict[str, ControllerFact]) -> dict[str, ControllerFact]:
    merged: dict[str, ControllerFact] = {}
    for controller_set in sets:
        for fact in controller_set.values():
            _merge_controller(merged, fact)
    return merged


def _active_vnas_artccs(payload: Any) -> set[str]:
    rows = payload.get("controllers", []) if isinstance(payload, dict) else payload
    return {
        str(row.get("artccId", "")).strip().upper()
        for row in rows or []
        if row.get("isActive") and not row.get("isObserver") and row.get("artccId")
    }


def acquire_live_snapshots(
    log_path: Path,
) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any], dict[str, ControllerFact]]:
    vatsim_response = requests.get(VATSIM_DATA_URL, timeout=30)
    vatsim_response.raise_for_status()
    vnas_response = requests.get(VNAS_CONTROLLERS_URL, timeout=30)
    vnas_response.raise_for_status()
    vnas_payload = vnas_response.json()
    facilities: dict[str, Any] = {}
    for artcc_id in sorted(_active_vnas_artccs(vnas_payload)):
        response = requests.get(VNAS_FACILITY_URL.format(artcc_id=artcc_id), timeout=45)
        if response.status_code == 200:
            facilities[artcc_id] = response.json()
    return (
        vatsim_response.json(),
        vnas_payload,
        facilities,
        parse_latest_radio_board(log_path),
    )


def write_snapshot_bundle(
    directory: Path,
    vatsim_payload: dict[str, Any],
    vnas_payload: dict[str, Any],
    vnas_facilities: dict[str, Any],
    board: dict[str, ControllerFact],
    fms_path: Path,
    static_manifest: dict[str, Any],
) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "vatsim-data.json").write_text(
        json.dumps(vatsim_payload, indent=2), encoding="utf-8"
    )
    (directory / "vnas-controllers.json").write_text(
        json.dumps(vnas_payload, indent=2), encoding="utf-8"
    )
    facilities_directory = directory / "vnas-facilities"
    facilities_directory.mkdir(parents=True, exist_ok=True)
    for artcc_id, payload in sorted(vnas_facilities.items()):
        (facilities_directory / f"{artcc_id}.json").write_text(
            json.dumps(payload, indent=2), encoding="utf-8"
        )
    board_rows = []
    for fact in sorted(board.values(), key=lambda item: item.key):
        row = asdict(fact)
        row["sources"] = sorted(fact.sources)
        board_rows.append(row)
    (directory / "radio-board.json").write_text(
        json.dumps(board_rows, indent=2), encoding="utf-8"
    )
    (directory / "source-manifest.json").write_text(
        json.dumps(static_manifest, indent=2), encoding="utf-8"
    )
    shutil.copy2(fms_path, directory / "route.fms")


def load_snapshot_bundle(
    directory: Path,
) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any], dict[str, ControllerFact]]:
    vatsim = json.loads((directory / "vatsim-data.json").read_text(encoding="utf-8"))
    vnas = json.loads((directory / "vnas-controllers.json").read_text(encoding="utf-8"))
    facilities: dict[str, Any] = {}
    facilities_directory = directory / "vnas-facilities"
    if facilities_directory.exists():
        for path in facilities_directory.glob("*.json"):
            facilities[path.stem.upper()] = json.loads(path.read_text(encoding="utf-8"))
    board_rows = json.loads((directory / "radio-board.json").read_text(encoding="utf-8"))
    board: dict[str, ControllerFact] = {}
    for row in board_rows:
        fact = ControllerFact(
            callsign=row["callsign"],
            frequency=row["frequency"],
            facility=row.get("facility", ""),
            online=row.get("online", True),
            sources=set(row.get("sources", ["RADIO_BOARD"])),
            radio_name=row.get("radio_name", ""),
            vnas_positions=row.get("vnas_positions", []),
            vnas_artcc_id=row.get("vnas_artcc_id", ""),
            on_radio_board=row.get("on_radio_board", True),
        )
        board[fact.key] = fact
    return vatsim, vnas, facilities, board

