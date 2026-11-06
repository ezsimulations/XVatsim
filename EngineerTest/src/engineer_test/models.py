from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any


@dataclass(frozen=True)
class RoutePoint:
    name: str
    latitude: float
    longitude: float
    altitude_ft: float | None = None


@dataclass
class ControllerFact:
    callsign: str
    frequency: str
    facility: str = ""
    online: bool = True
    sources: set[str] = field(default_factory=set)
    radio_name: str = ""
    vnas_positions: list[dict[str, Any]] = field(default_factory=list)
    vnas_artcc_id: str = ""
    on_radio_board: bool = False

    @property
    def key(self) -> str:
        return f"{self.callsign}|{self.frequency}"


@dataclass
class PolygonRecord:
    key: str
    layer: str
    source: str
    label: str
    geometry: Any
    properties: dict[str, Any]
    prefixes: tuple[str, ...] = ()
    suffix: str = ""


@dataclass(frozen=True)
class OwnershipClaim:
    polygon_key: str
    controller_key: str
    callsign: str
    frequency: str
    source: str
    reason: str


@dataclass
class RouteCrossing:
    polygon_key: str
    layer: str
    label: str
    source: str
    entry_nm: float
    exit_nm: float
    claims: list[OwnershipClaim] = field(default_factory=list)

