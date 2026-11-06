from __future__ import annotations

import json
from collections import defaultdict
from dataclasses import asdict, dataclass
from typing import Any

from .geometry import normalize_token
from .models import ControllerFact, OwnershipClaim, PolygonRecord


@dataclass(frozen=True)
class OwnershipDelta:
    """Observable result of applying one complete controller snapshot."""

    generation: int
    added_controller_keys: tuple[str, ...]
    removed_controller_keys: tuple[str, ...]
    changed_controller_keys: tuple[str, ...]
    evaluated_controller_keys: tuple[str, ...]
    touched_polygon_keys: tuple[str, ...]
    changed_polygon_keys: tuple[str, ...]


def _controller_fingerprint(controller: ControllerFact) -> str:
    payload = asdict(controller)
    payload["sources"] = sorted(controller.sources)
    return json.dumps(payload, sort_keys=True, separators=(",", ":"), default=str)


def _claim_signature(claim: OwnershipClaim) -> tuple[str, ...]:
    return (
        claim.polygon_key,
        claim.controller_key,
        claim.callsign,
        claim.frequency,
        claim.source,
        claim.reason,
    )


def _callsign_prefix(callsign: str) -> str:
    return normalize_token(callsign).split("_", 1)[0]


def _callsign_role(callsign: str) -> str:
    parts = normalize_token(callsign).split("_")
    return parts[-1] if parts else ""


def _terminal_matches(record: PolygonRecord, callsign: str, facility_id: str = "") -> bool:
    normalized = normalize_token(callsign)
    prefix = _callsign_prefix(normalized)
    role = _callsign_role(normalized)
    record_id = normalize_token(record.properties.get("id", ""))
    if facility_id and record_id != normalize_token(facility_id):
        return False
    if record.prefixes and prefix not in record.prefixes:
        return False
    return role == record.suffix


class OwnershipCatalogIndex:
    """Immutable geometry/facility lookup used by batch and incremental joins."""

    def __init__(
        self,
        records: list[PolygonRecord],
        vnas_facilities: dict[str, object],
    ) -> None:
        self.center_by_prefix: dict[str, list[PolygonRecord]] = defaultdict(list)
        self.terminal_by_signature: dict[
            tuple[str, str, str], list[PolygonRecord]
        ] = defaultdict(list)
        self.terminal_by_prefix_role: dict[
            tuple[str, str], list[PolygonRecord]
        ] = defaultdict(list)
        self.vatglasses_by_callsign: dict[str, list[PolygonRecord]] = defaultdict(list)
        for record in records:
            if record.layer == "CENTER":
                for prefix in record.prefixes:
                    self.center_by_prefix[prefix].append(record)
            elif record.layer == "TERMINAL":
                facility_id = normalize_token(record.properties.get("id", ""))
                for prefix in record.prefixes:
                    self.terminal_by_signature[
                        (facility_id, prefix, record.suffix)
                    ].append(record)
                    self.terminal_by_prefix_role[(prefix, record.suffix)].append(record)
            if record.source == "VATGLASSES":
                for callsign in record.properties.get("controller_callsigns", []):
                    self.vatglasses_by_callsign[normalize_token(callsign)].append(record)

        self.facility_index: dict[tuple[str, str], dict] = {}
        for artcc_id, document in vnas_facilities.items():
            if isinstance(document, dict):
                _index_facilities(
                    document.get("facility"),
                    normalize_token(artcc_id),
                    self.facility_index,
                )

    def match_controller(
        self, controller: ControllerFact
    ) -> tuple[list[OwnershipClaim], str | None]:
        matched: dict[str, set[str]] = defaultdict(set)
        prefix = _callsign_prefix(controller.callsign)
        role = _callsign_role(controller.callsign)

        for record in self.vatglasses_by_callsign.get(
            normalize_token(controller.callsign), []
        ):
            matched[record.key].add(
                "VATSIM callsign -> VATGlasses position ownership"
            )

        if role in {"CTR", "FSS"}:
            for record in self.center_by_prefix.get(prefix, []):
                matched[record.key].add("VATSPY center prefix crosswalk")

        direct_vnas_matches = 0
        for position in controller.vnas_positions:
            facility_id = str(position.get("facilityId", ""))
            default_callsign = str(position.get("defaultCallsign") or controller.callsign)
            signature = (
                normalize_token(facility_id),
                _callsign_prefix(default_callsign),
                _callsign_role(default_callsign),
            )
            for record in self.terminal_by_signature.get(signature, []):
                matched[record.key].add(
                    "active vNAS position + SimAware facility/prefix/suffix"
                )
                direct_vnas_matches += 1

            stars = position.get("starsData") or {}
            assumed_tokens = {
                normalize_token(token) for token in stars.get("assumedTcps", []) if token
            }
            if assumed_tokens and controller.vnas_artcc_id:
                facility = self.facility_index.get(
                    (
                        normalize_token(controller.vnas_artcc_id),
                        normalize_token(facility_id),
                    )
                )
                for assumed_callsign, token in _assumed_position_callsigns(
                    facility, assumed_tokens
                ):
                    signature = (
                        normalize_token(facility_id),
                        _callsign_prefix(assumed_callsign),
                        _callsign_role(assumed_callsign),
                    )
                    for record in self.terminal_by_signature.get(signature, []):
                        matched[record.key].add(
                            f"vNAS assumed TCP {token} -> {assumed_callsign}"
                        )

        if not direct_vnas_matches and role in {"APP", "DEP"}:
            for record in self.terminal_by_prefix_role.get((prefix, role), []):
                matched[record.key].add(
                    "VATSIM callsign + SimAware prefix/suffix"
                )

        controller_claims: list[OwnershipClaim] = []
        for polygon_key, reasons in sorted(matched.items()):
            reason = "; ".join(sorted(reasons))
            source = (
                "VNAS+SIMAWARE"
                if any("vNAS" in item for item in reasons)
                else "VATSIM+CANONICAL"
            )
            controller_claims.append(
                OwnershipClaim(
                    polygon_key=polygon_key,
                    controller_key=controller.key,
                    callsign=controller.callsign,
                    frequency=controller.frequency,
                    source=source,
                    reason=reason,
                )
            )

        if not matched and role in {"DEL", "GND", "TWR", "ATIS", "OBS", "SUP"}:
            return controller_claims, "non_airspace_service"
        if not matched:
            return controller_claims, "unmapped"
        return controller_claims, None


def _diagnostics_for_claims(
    claims: dict[str, list[OwnershipClaim]],
    classifications: dict[str, str],
) -> dict[str, list[str]]:
    diagnostics: dict[str, list[str]] = defaultdict(list)
    for controller_key, classification in sorted(classifications.items()):
        diagnostics[classification].append(controller_key)

    for polygon_key, polygon_claims in claims.items():
        owners = {claim.controller_key for claim in polygon_claims}
        if len(owners) > 1:
            diagnostics["conflicts"].append(
                f"{polygon_key}:" + ",".join(sorted(owners))
            )
    return diagnostics


def build_ownership_claims(
    controllers: dict[str, ControllerFact],
    records: list[PolygonRecord],
    vnas_facilities: dict[str, object],
) -> tuple[dict[str, list[OwnershipClaim]], dict[str, list[str]]]:
    index = OwnershipCatalogIndex(records, vnas_facilities)
    claims: dict[str, list[OwnershipClaim]] = defaultdict(list)
    classifications: dict[str, str] = {}
    for controller in controllers.values():
        controller_claims, classification = index.match_controller(controller)
        for claim in controller_claims:
            claims[claim.polygon_key].append(claim)
        if classification:
            classifications[controller.key] = classification
    return claims, _diagnostics_for_claims(claims, classifications)


class IncrementalOwnershipEngine:
    """Apply complete online snapshots without rebuilding unchanged ownership."""

    def __init__(
        self,
        records: list[PolygonRecord],
        vnas_facilities: dict[str, object],
    ) -> None:
        self._index = OwnershipCatalogIndex(records, vnas_facilities)
        self._controllers: dict[str, ControllerFact] = {}
        self._claims_by_controller: dict[str, list[OwnershipClaim]] = {}
        self._claims_by_polygon: dict[str, list[OwnershipClaim]] = defaultdict(list)
        self._classifications: dict[str, str] = {}
        self._generation = 0

    @property
    def generation(self) -> int:
        return self._generation

    @property
    def claims(self) -> dict[str, list[OwnershipClaim]]:
        return {
            key: sorted(value, key=_claim_signature)
            for key, value in sorted(self._claims_by_polygon.items())
            if value
        }

    @property
    def diagnostics(self) -> dict[str, list[str]]:
        return dict(
            _diagnostics_for_claims(self._claims_by_polygon, self._classifications)
        )

    def apply_snapshot(
        self, controllers: dict[str, ControllerFact]
    ) -> OwnershipDelta:
        previous_keys = set(self._controllers)
        incoming_keys = set(controllers)
        added = incoming_keys - previous_keys
        removed = previous_keys - incoming_keys
        changed = {
            key
            for key in incoming_keys.intersection(previous_keys)
            if _controller_fingerprint(controllers[key])
            != _controller_fingerprint(self._controllers[key])
        }
        evaluated = added.union(changed)
        retiring = removed.union(changed)
        touched: set[str] = set()
        pending_matches: dict[str, tuple[list[OwnershipClaim], str | None]] = {}

        before_by_polygon: dict[str, tuple[tuple[str, ...], ...]] = {}
        for key in retiring:
            for claim in self._claims_by_controller.get(key, []):
                touched.add(claim.polygon_key)
        for key in evaluated:
            preview_claims, classification = self._index.match_controller(controllers[key])
            pending_matches[key] = (preview_claims, classification)
            for claim in preview_claims:
                touched.add(claim.polygon_key)
        for polygon_key in touched:
            before_by_polygon[polygon_key] = tuple(
                sorted(
                    (_claim_signature(claim) for claim in self._claims_by_polygon.get(polygon_key, []))
                )
            )

        for key in retiring:
            for claim in self._claims_by_controller.pop(key, []):
                remaining = [
                    item
                    for item in self._claims_by_polygon.get(claim.polygon_key, [])
                    if item.controller_key != key
                ]
                if remaining:
                    self._claims_by_polygon[claim.polygon_key] = remaining
                else:
                    self._claims_by_polygon.pop(claim.polygon_key, None)
            self._classifications.pop(key, None)

        for key in evaluated:
            controller_claims, classification = pending_matches[key]
            self._claims_by_controller[key] = controller_claims
            for claim in controller_claims:
                self._claims_by_polygon[claim.polygon_key].append(claim)
            if classification:
                self._classifications[key] = classification

        self._controllers = dict(controllers)
        self._generation += 1
        changed_polygons = {
            polygon_key
            for polygon_key in touched
            if before_by_polygon.get(polygon_key, ())
            != tuple(
                sorted(
                    _claim_signature(claim)
                    for claim in self._claims_by_polygon.get(polygon_key, [])
                )
            )
        }
        return OwnershipDelta(
            generation=self._generation,
            added_controller_keys=tuple(sorted(added)),
            removed_controller_keys=tuple(sorted(removed)),
            changed_controller_keys=tuple(sorted(changed)),
            evaluated_controller_keys=tuple(sorted(evaluated)),
            touched_polygon_keys=tuple(sorted(touched)),
            changed_polygon_keys=tuple(sorted(changed_polygons)),
        )


def _find_facility(node: object, facility_id: str) -> dict | None:
    if not isinstance(node, dict):
        return None
    if normalize_token(node.get("id", "")) == normalize_token(facility_id):
        return node
    for child in node.get("childFacilities", []) or []:
        found = _find_facility(child, facility_id)
        if found is not None:
            return found
    return None


def _index_facilities(
    node: object, artcc_id: str, output: dict[tuple[str, str], dict]
) -> None:
    if not isinstance(node, dict):
        return
    facility_id = normalize_token(node.get("id", ""))
    if facility_id:
        output[(artcc_id, facility_id)] = node
    for child in node.get("childFacilities", []) or []:
        _index_facilities(child, artcc_id, output)


def _assumed_position_callsigns(
    facility: dict | None, assumed_tokens: set[str]
) -> list[tuple[str, str]]:
    if facility is None:
        return []
    stars = facility.get("starsConfiguration") or {}
    token_by_tcp_id: dict[str, str] = {}
    for tcp in stars.get("tcps", []) or []:
        token = normalize_token(f"{tcp.get('subset', '')}{tcp.get('sectorId', '')}")
        tcp_id = str(tcp.get("id", ""))
        if token and tcp_id:
            token_by_tcp_id[tcp_id] = token
    result: list[tuple[str, str]] = []
    for position in facility.get("positions", []) or []:
        position_stars = position.get("starsConfiguration") or {}
        token = token_by_tcp_id.get(str(position_stars.get("tcpId", "")), "")
        callsign = str(position.get("callsign", "")).strip().upper()
        if token in assumed_tokens and callsign:
            result.append((callsign, token))
    return result

