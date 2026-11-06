from __future__ import annotations

import csv
import html
import json
from dataclasses import asdict
from pathlib import Path
from typing import Any

from shapely.geometry import mapping

from .models import ControllerFact, PolygonRecord, RouteCrossing, RoutePoint


def _claim_rows(crossing: RouteCrossing) -> str:
    if not crossing.claims:
        return "Unowned in captured facts"
    return "<br>".join(
        html.escape(
            f"{claim.callsign} {claim.frequency} — {claim.source}: {claim.reason}"
        )
        for claim in crossing.claims
    )


def build_progress_timeline(crossings: list[RouteCrossing]) -> list[dict[str, Any]]:
    events: list[tuple[float, int, str, RouteCrossing]] = []
    for crossing in crossings:
        if not crossing.claims:
            continue
        events.append((crossing.entry_nm, 1, "ENTER", crossing))
        events.append((crossing.exit_nm, 0, "EXIT", crossing))
    events.sort(key=lambda item: (item[0], item[1], item[3].polygon_key))
    active: dict[str, RouteCrossing] = {}
    timeline: list[dict[str, Any]] = []
    for distance_nm, _order, action, crossing in events:
        if action == "EXIT":
            active.pop(crossing.polygon_key, None)
        else:
            active[crossing.polygon_key] = crossing
        timeline.append(
            {
                "distance_nm": round(distance_nm, 3),
                "action": action,
                "polygon_key": crossing.polygon_key,
                "layer": crossing.layer,
                "label": crossing.label,
                "owners": [claim.callsign for claim in crossing.claims],
                "frequencies": [claim.frequency for claim in crossing.claims],
                "active_lit_polygons": [
                    {
                        "polygon_key": item.polygon_key,
                        "layer": item.layer,
                        "label": item.label,
                        "owners": [claim.callsign for claim in item.claims],
                        "frequencies": [claim.frequency for claim in item.claims],
                    }
                    for item in sorted(
                        active.values(), key=lambda value: (value.layer, value.label, value.polygon_key)
                    )
                ],
            }
        )
    return timeline


def route_state_at(
    crossings: list[RouteCrossing], progress_nm: float
) -> dict[str, Any]:
    """Return active and next lit polygons; distance is evidence, never a gate."""

    lit = [crossing for crossing in crossings if crossing.claims]
    active = [
        crossing
        for crossing in lit
        if crossing.entry_nm <= progress_nm < crossing.exit_nm
    ]
    future_entries = [
        crossing.entry_nm for crossing in lit if crossing.entry_nm > progress_nm
    ]
    next_entry_nm = min(future_entries) if future_entries else None
    next_crossings = (
        [
            crossing
            for crossing in lit
            if abs(crossing.entry_nm - next_entry_nm) < 0.001
        ]
        if next_entry_nm is not None
        else []
    )

    def describe(crossing: RouteCrossing) -> dict[str, Any]:
        return {
            "polygon_key": crossing.polygon_key,
            "layer": crossing.layer,
            "label": crossing.label,
            "entry_nm": round(crossing.entry_nm, 3),
            "exit_nm": round(crossing.exit_nm, 3),
            "owners": [claim.callsign for claim in crossing.claims],
            "frequencies": [claim.frequency for claim in crossing.claims],
        }

    return {
        "progress_nm": round(progress_nm, 3),
        "active_lit_polygons": [
            describe(crossing)
            for crossing in sorted(
                active,
                key=lambda item: (item.layer, item.entry_nm, item.polygon_key),
            )
        ],
        "next_entry_nm": round(next_entry_nm, 3) if next_entry_nm is not None else None,
        "distance_to_next_entry_nm": (
            round(next_entry_nm - progress_nm, 3)
            if next_entry_nm is not None
            else None
        ),
        "next_lit_polygons": [
            describe(crossing)
            for crossing in sorted(
                next_crossings, key=lambda item: (item.layer, item.polygon_key)
            )
        ],
        "distance_gate_applied": False,
    }


def write_outputs(
    output_dir: Path,
    route_points: list[RoutePoint],
    crossings: list[RouteCrossing],
    records_by_key: dict[str, PolygonRecord],
    controllers: dict[str, ControllerFact],
    diagnostics: dict[str, list[str]],
    performance: dict[str, float],
    source_manifest: dict[str, Any],
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    timeline = build_progress_timeline(crossings)

    result = {
        "route": [asdict(point) for point in route_points],
        "crossings": [
            {
                **asdict(crossing),
                "claims": [asdict(claim) for claim in crossing.claims],
            }
            for crossing in crossings
        ],
        "controllers": [
            {
                "callsign": fact.callsign,
                "frequency": fact.frequency,
                "facility": fact.facility,
                "sources": sorted(fact.sources),
                "radio_name": fact.radio_name,
                "on_radio_board": fact.on_radio_board,
                "vnas_positions": fact.vnas_positions,
                "vnas_artcc_id": fact.vnas_artcc_id,
            }
            for fact in sorted(controllers.values(), key=lambda item: item.key)
        ],
        "diagnostics": diagnostics,
        "progress_timeline": timeline,
        "performance_ms": performance,
        "source_manifest": source_manifest,
        "summary": {
            "route_points": len(route_points),
            "route_crossings": len(crossings),
            "lit_route_polygons": sum(1 for crossing in crossings if crossing.claims),
            "online_controllers": len(controllers),
            "unmapped_controllers": len(diagnostics.get("unmapped", [])),
            "non_airspace_services": len(
                diagnostics.get("non_airspace_service", [])
            ),
            "ownership_conflicts": len(diagnostics.get("conflicts", [])),
            "route_ownership_conflicts": len(
                diagnostics.get("route_conflicts", [])
            ),
            "repaired_source_polygons": sum(
                1
                for record in records_by_key.values()
                if record.properties.get("engineer_test_geometry_repaired")
            ),
        },
    }
    (output_dir / "results.json").write_text(
        json.dumps(result, indent=2), encoding="utf-8"
    )
    (output_dir / "performance.json").write_text(
        json.dumps(performance, indent=2), encoding="utf-8"
    )

    with (output_dir / "crossings.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "order",
                "entry_nm",
                "exit_nm",
                "layer",
                "polygon",
                "source",
                "owners",
                "frequencies",
                "ownership_provenance",
            ]
        )
        for index, crossing in enumerate(crossings, start=1):
            writer.writerow(
                [
                    index,
                    f"{crossing.entry_nm:.1f}",
                    f"{crossing.exit_nm:.1f}",
                    crossing.layer,
                    crossing.label,
                    crossing.source,
                    ";".join(claim.callsign for claim in crossing.claims),
                    ";".join(claim.frequency for claim in crossing.claims),
                    ";".join(f"{claim.source}:{claim.reason}" for claim in crossing.claims),
                ]
            )

    with (output_dir / "timeline.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "distance_nm",
                "action",
                "layer",
                "polygon",
                "owners",
                "frequencies",
                "active_lit_polygons_after_event",
            ]
        )
        for event in timeline:
            writer.writerow(
                [
                    f"{event['distance_nm']:.3f}",
                    event["action"],
                    event["layer"],
                    event["label"],
                    ";".join(event["owners"]),
                    ";".join(event["frequencies"]),
                    ";".join(
                        f"{item['layer']}:{item['label']}:{','.join(item['owners'])}"
                        for item in event["active_lit_polygons"]
                    ),
                ]
            )

    features = []
    for crossing in crossings:
        record = records_by_key[crossing.polygon_key]
        features.append(
            {
                "type": "Feature",
                "geometry": mapping(record.geometry),
                "properties": {
                    "key": crossing.polygon_key,
                    "label": crossing.label,
                    "layer": crossing.layer,
                    "source": crossing.source,
                    "entry_nm": round(crossing.entry_nm, 1),
                    "exit_nm": round(crossing.exit_nm, 1),
                    "lit": bool(crossing.claims),
                    "owners": [claim.callsign for claim in crossing.claims],
                    "frequencies": [claim.frequency for claim in crossing.claims],
                    "claim_html": _claim_rows(crossing),
                },
            }
        )
    geojson = {"type": "FeatureCollection", "features": features}
    route_coords = [[point.longitude, point.latitude] for point in route_points]
    table_rows = "".join(
        "<tr>"
        f"<td>{index}</td><td>{crossing.entry_nm:.1f}</td><td>{crossing.exit_nm:.1f}</td>"
        f"<td>{html.escape(crossing.layer)}</td><td>{html.escape(crossing.label)}</td>"
        f"<td>{_claim_rows(crossing)}</td></tr>"
        for index, crossing in enumerate(crossings, start=1)
    )
    map_html = f"""<!doctype html>
<html><head><meta charset="utf-8"><title>EngineerTest route ownership</title>
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css">
<style>
html,body{{height:100%;margin:0;font-family:Segoe UI,Arial,sans-serif;background:#101820;color:#edf4f7}}
#map{{height:68%}} #report{{height:32%;overflow:auto;padding:10px 14px;box-sizing:border-box}}
table{{border-collapse:collapse;width:100%;font-size:13px}} th,td{{border:1px solid #44515b;padding:5px;text-align:left}}
th{{position:sticky;top:0;background:#24303a}} .legend{{background:#fff;color:#111;padding:7px;border-radius:4px}}
</style></head><body><div id="map"></div><div id="report">
<h2>Ordered route polygon crossings</h2>
<p>Filled polygons are lit by captured online ownership facts. Gray outlines are traversed but unowned.</p>
<table><thead><tr><th>#</th><th>Entry NM</th><th>Exit NM</th><th>Layer</th><th>Polygon</th><th>Owner evidence</th></tr></thead>
<tbody>{table_rows}</tbody></table></div>
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
<script>
const route={json.dumps(route_coords)};
const polygons={json.dumps(geojson)};
const map=L.map('map');
L.tileLayer('https://{{s}}.tile.openstreetmap.org/{{z}}/{{x}}/{{y}}.png',{{maxZoom:18,attribution:'&copy; OpenStreetMap contributors'}}).addTo(map);
const routeLayer=L.polyline(route.map(p=>[p[1],p[0]]),{{color:'#1687ff',weight:4,opacity:.95}}).addTo(map);
function style(feature){{
  const p=feature.properties;
  if(!p.lit) return {{color:'#69757e',weight:1,fillOpacity:.03}};
  return p.layer.includes('TERMINAL')
    ? {{color:'#ff3b30',weight:3,fillColor:'#ff6a5f',fillOpacity:.28}}
    : p.layer.includes('CENTER')
      ? {{color:'#23a455',weight:3,fillColor:'#42d77d',fillOpacity:.20}}
      : {{color:'#c89d2d',weight:2,fillColor:'#e8bd4d',fillOpacity:.16}};
}}
const polygonLayer=L.geoJSON(polygons,{{style,onEachFeature:(feature,layer)=>{{
  const p=feature.properties;
  layer.bindPopup(`<b>${{p.label}}</b><br>${{p.layer}} · ${{p.source}}<br>Route ${{p.entry_nm}}–${{p.exit_nm}} NM<br>${{p.claim_html}}`);
}}}}).addTo(map);
const bounds=routeLayer.getBounds(); if(bounds.isValid()) map.fitBounds(bounds.pad(.06));
const legend=L.control({{position:'topright'}}); legend.onAdd=()=>{{const d=L.DomUtil.create('div','legend');d.innerHTML='<b>EngineerTest</b><br><span style="color:#23a455">■</span> Lit Center<br><span style="color:#ff3b30">■</span> Lit terminal<br><span style="color:#69757e">□</span> Traversed/unowned<br><span style="color:#1687ff">━</span> FMS route';return d;}};legend.addTo(map);
</script></body></html>"""
    (output_dir / "map.html").write_text(map_html, encoding="utf-8")

