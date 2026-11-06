from __future__ import annotations

import unittest

from shapely.geometry import Polygon

from engineer_test.geometry import build_vatglasses_catalog, intersect_route
from engineer_test.models import (
    ControllerFact,
    OwnershipClaim,
    PolygonRecord,
    RouteCrossing,
    RoutePoint,
)
from engineer_test.ownership import (
    IncrementalOwnershipEngine,
    build_ownership_claims,
)
from engineer_test.report import build_progress_timeline, route_state_at


class GeometryTests(unittest.TestCase):
    def test_nested_center_and_terminal_crossings_are_both_retained(self) -> None:
        route = [
            RoutePoint("A", 0.0, -2.0, 10_000),
            RoutePoint("B", 0.0, 2.0, 10_000),
        ]
        center = PolygonRecord(
            "center", "CENTER", "TEST", "CENTER", Polygon([(-1, -1), (1, -1), (1, 1), (-1, 1)]), {}
        )
        terminal = PolygonRecord(
            "terminal", "TERMINAL", "TEST", "APP", Polygon([(-0.5, -0.5), (0.5, -0.5), (0.5, 0.5), (-0.5, 0.5)]), {}
        )
        _line, crossings, _records = intersect_route(route, [center, terminal])
        self.assertEqual({crossing.polygon_key for crossing in crossings}, {"center", "terminal"})

    def test_progress_timeline_retains_overlapping_active_polygons(self) -> None:
        claim = OwnershipClaim("center", "SEA|135.450", "SEA_16_CTR", "135.450", "TEST", "direct")
        terminal_claim = OwnershipClaim("terminal", "SCT|124.300", "LAX_S_DEP", "124.300", "TEST", "direct")
        timeline = build_progress_timeline(
            [
                RouteCrossing("center", "CENTER", "KZLA", "TEST", 0, 100, [claim]),
                RouteCrossing("terminal", "TERMINAL", "SCT", "TEST", 80, 100, [terminal_claim]),
            ]
        )
        terminal_entry = next(
            event
            for event in timeline
            if event["polygon_key"] == "terminal" and event["action"] == "ENTER"
        )
        self.assertEqual(
            {item["polygon_key"] for item in terminal_entry["active_lit_polygons"]},
            {"center", "terminal"},
        )

    def test_next_lit_polygon_has_no_distance_gate(self) -> None:
        current_claim = OwnershipClaim(
            "current", "CZVR|133.700", "CZVR_CTR", "133.700", "TEST", "direct"
        )
        next_claim = OwnershipClaim(
            "next", "SEA|135.450", "SEA_16_CTR", "135.450", "TEST", "direct"
        )
        state = route_state_at(
            [
                RouteCrossing(
                    "current", "CENTER", "CZVR", "TEST", 0, 100, [current_claim]
                ),
                RouteCrossing(
                    "next", "CENTER_SECTOR", "ZSE Sector 16", "TEST", 600, 700, [next_claim]
                ),
            ],
            progress_nm=50,
        )
        self.assertEqual(state["distance_to_next_entry_nm"], 550)
        self.assertEqual(
            [item["polygon_key"] for item in state["next_lit_polygons"]],
            ["next"],
        )
        self.assertFalse(state["distance_gate_applied"])

    def test_vatglasses_position_generates_exact_controller_pattern(self) -> None:
        package = {
            "regions": ["ZSE"],
            "payload": {
                "positions": {
                    "S16": {"pre": ["SEA"], "type": "CTR", "frequency": "135.450"}
                },
                "airspace": [
                    {
                        "id": "Sector 16",
                        "group": "CTR",
                        "owner": ["S16"],
                        "sectors": [
                            {
                                "min": 0,
                                "max": 999,
                                "points": [["000000", "-0010000"], ["000000", "0010000"], ["010000", "0010000"]],
                            }
                        ],
                    }
                ],
            },
        }
        records = build_vatglasses_catalog([package])
        self.assertEqual(len(records), 1)
        self.assertIn("SEA_16_CTR", records[0].properties["controller_callsigns"])


class OwnershipTests(unittest.TestCase):
    def test_assumed_vnas_tcp_lights_approach_polygon_without_vote(self) -> None:
        polygon = PolygonRecord(
            key="sct-lax-app",
            layer="TERMINAL",
            source="SIMAWARE_TRACON",
            label="SCT SoCal Approach",
            geometry=Polygon([(-119, 33), (-117, 33), (-117, 35), (-119, 35)]),
            properties={"id": "SCT"},
            prefixes=("LAX",),
            suffix="APP",
        )
        controller = ControllerFact(
            callsign="LAX_S_DEP",
            frequency="124.300",
            sources={"VATSIM", "VNAS", "RADIO_BOARD"},
            vnas_artcc_id="ZLA",
            vnas_positions=[
                {
                    "facilityId": "SCT",
                    "defaultCallsign": "LAX_S_DEP",
                    "starsData": {"assumedTcps": ["6L"]},
                }
            ],
        )
        facility = {
            "facility": {
                "id": "ZLA",
                "childFacilities": [
                    {
                        "id": "SCT",
                        "starsConfiguration": {
                            "tcps": [{"id": "tcp-l", "subset": 6, "sectorId": "L"}]
                        },
                        "positions": [
                            {
                                "callsign": "LAX_L_APP",
                                "starsConfiguration": {"tcpId": "tcp-l"},
                            }
                        ],
                        "childFacilities": [],
                    }
                ],
            }
        }
        claims, diagnostics = build_ownership_claims(
            {controller.key: controller}, [polygon], {"ZLA": facility}
        )
        self.assertEqual(len(claims[polygon.key]), 1)
        self.assertEqual(claims[polygon.key][0].callsign, "LAX_S_DEP")
        self.assertIn("assumed TCP 6L", claims[polygon.key][0].reason)
        self.assertNotIn(controller.key, diagnostics.get("unmapped", []))

    def test_incremental_snapshot_only_evaluates_new_or_changed_controllers(self) -> None:
        sea = PolygonRecord(
            "sea", "CENTER", "TEST", "KZSE", Polygon([(0, 0), (1, 0), (1, 1), (0, 1)]), {}, prefixes=("SEA",)
        )
        oak = PolygonRecord(
            "oak", "CENTER", "TEST", "KZOA", Polygon([(1, 0), (2, 0), (2, 1), (1, 1)]), {}, prefixes=("OAK",)
        )
        nct = PolygonRecord(
            "nct", "TERMINAL", "TEST", "NCT", Polygon([(1, 0), (2, 0), (2, 1), (1, 1)]), {"id": "NCT"}, prefixes=("SFO",), suffix="APP"
        )
        engine = IncrementalOwnershipEngine([sea, oak, nct], {})
        sea_controller = ControllerFact("SEA_16_CTR", "135.450")
        first = {sea_controller.key: sea_controller}

        initial = engine.apply_snapshot(first)
        self.assertEqual(initial.evaluated_controller_keys, (sea_controller.key,))
        self.assertEqual(initial.changed_polygon_keys, ("sea",))

        oak_controller = ControllerFact("OAK_42_CTR", "132.200")
        sfo_controller = ControllerFact("SFO_APP", "135.100")
        expanded = {
            **first,
            oak_controller.key: oak_controller,
            sfo_controller.key: sfo_controller,
        }
        added = engine.apply_snapshot(expanded)
        self.assertEqual(
            set(added.evaluated_controller_keys),
            {oak_controller.key, sfo_controller.key},
        )
        self.assertNotIn(sea_controller.key, added.evaluated_controller_keys)
        self.assertEqual(set(added.changed_polygon_keys), {"oak", "nct"})
        self.assertIn("sea", engine.claims)

        unchanged = engine.apply_snapshot(expanded)
        self.assertEqual(unchanged.evaluated_controller_keys, ())
        self.assertEqual(unchanged.touched_polygon_keys, ())
        self.assertEqual(unchanged.changed_polygon_keys, ())

        without_sea = dict(expanded)
        without_sea.pop(sea_controller.key)
        removed = engine.apply_snapshot(without_sea)
        self.assertEqual(removed.removed_controller_keys, (sea_controller.key,))
        self.assertEqual(removed.evaluated_controller_keys, ())
        self.assertEqual(removed.changed_polygon_keys, ("sea",))
        self.assertNotIn("sea", engine.claims)

    def test_overlapping_app_and_dep_polygons_are_both_retained(self) -> None:
        geometry = Polygon([(-1, -1), (1, -1), (1, 1), (-1, 1)])
        app = PolygonRecord(
            "app", "TERMINAL", "TEST", "SCT APP", geometry, {"id": "SCT"}, prefixes=("LAX",), suffix="APP"
        )
        departure = PolygonRecord(
            "dep", "TERMINAL", "TEST", "SCT DEP", geometry, {"id": "SCT"}, prefixes=("LAX",), suffix="DEP"
        )
        app_controller = ControllerFact("LAX_APP", "124.500")
        dep_controller = ControllerFact("LAX_S_DEP", "124.300")
        controllers = {
            app_controller.key: app_controller,
            dep_controller.key: dep_controller,
        }
        claims, _diagnostics = build_ownership_claims(controllers, [app, departure], {})
        route = [RoutePoint("A", 0, -2), RoutePoint("B", 0, 2)]
        _line, crossings, _records = intersect_route(route, [app, departure])
        for crossing in crossings:
            crossing.claims = list(claims.get(crossing.polygon_key, []))
        state = route_state_at(crossings, 100)
        self.assertEqual(
            {item["polygon_key"] for item in state["active_lit_polygons"]},
            {"app", "dep"},
        )

    def test_same_polygon_owner_conflict_is_reported_without_winner(self) -> None:
        polygon = PolygonRecord(
            "app", "TERMINAL", "TEST", "SCT APP", Polygon([(-1, -1), (1, -1), (1, 1), (-1, 1)]), {"id": "SCT"}, prefixes=("LAX",), suffix="APP"
        )
        first = ControllerFact("LAX_APP", "124.500")
        second = ControllerFact("LAX_2_APP", "125.200")
        claims, diagnostics = build_ownership_claims(
            {first.key: first, second.key: second}, [polygon], {}
        )
        self.assertEqual(
            {claim.controller_key for claim in claims[polygon.key]},
            {first.key, second.key},
        )
        self.assertEqual(len(diagnostics["conflicts"]), 1)
        self.assertIn(first.key, diagnostics["conflicts"][0])
        self.assertIn(second.key, diagnostics["conflicts"][0])


if __name__ == "__main__":
    unittest.main()

