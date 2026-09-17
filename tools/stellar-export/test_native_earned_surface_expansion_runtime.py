import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock
import native_earned_surface_expansion_runtime as runtime
import native_earned_surface_runtime as base_runtime
from test_native_bmp import bmp


def source():
    return {
        "FormatVersion": 17,
        "SavedAtUtc": "source",
        "SimulationDays": 6832.1009387,
        "Galaxy": {
            "Seed": 115501,
            "PlayerCivilizationId": 0,
            "Economies": [{"CivilizationId": 0, "Credits": 1000.0}],
            "Colonies": [
                {
                    "Id": 9,
                    "CivilizationId": 0,
                    "SystemId": 8,
                    "PlanetaryBodyId": 8004,
                    "SurfaceBuildings": [
                        {
                            "Id": 12,
                            "TypeId": "fabricator",
                            "X": 0.0,
                            "Z": 0.0,
                            "RotationDegrees": 0.0,
                            "IndustryProgress": 450.0,
                            "Condition": 1.0,
                            "IsComplete": True,
                            "IsEnabled": True,
                        }
                    ],
                }
            ],
            "Fleets": [{"Id": 1, "CivilizationId": 0}],
        },
        "AdaptiveResearch": {
            "Civilizations": [
                {
                    "CivilizationId": 0,
                    "Research": {
                        "Research": {
                            "Core": {"TotalEffectiveResearchLabs": 12.0},
                            "Expertise": {"Institutions": []},
                        }
                    },
                }
            ]
        },
    }


def proof(mode, before, after):
    paused = mode == "paused"
    stages = []
    labs_before = before["AdaptiveResearch"]["Civilizations"][0]["Research"][
        "Research"
    ]["Core"]["TotalEffectiveResearchLabs"]
    completed_state = len(before["Galaxy"]["Colonies"][0]["SurfaceBuildings"]) == 3
    if paused:
        stages.append(
            {
                "type_id": "fabricator",
                "building_id": 12,
                "x": 0.0,
                "z": 0.0,
                "rotation": 0.0,
                "authorization": 0.0,
                "treasury_before": 935.0,
                "treasury_after": 935.0,
                "industry_cost": 450.0,
                "industry_progress": 450.0,
                "complete": True,
                "powered": True,
                "staffed": True,
                "enabled": True,
                "efficiency": 1.0,
                "steps": 0,
                "step_days": 1 / 64,
                "cancel_unchanged": True,
            }
        )
    for index, (kind, auth, cost) in enumerate(
        (("power_generator", 25.0, 300.0), ("science_lab", 40.0, 400.0))
    ):
        stages.append(
            {
                "type_id": kind,
                "building_id": 13 + index,
                "x": float(index + 1),
                "z": float(index + 2),
                "rotation": 0.0,
                "authorization": 0.0 if paused else auth,
                "treasury_before": (
                    935.0 if paused else 1000.0 - (0 if index == 0 else 25)
                ),
                "treasury_after": (
                    935.0 if paused else 1000.0 - (25 if index == 0 else 65)
                ),
                "industry_cost": cost,
                "industry_progress": cost,
                "complete": True,
                "powered": True,
                "staffed": True,
                "enabled": True,
                "efficiency": 1.0,
                "steps": 0 if paused else 64,
                "step_days": 1 / 64,
                "cancel_unchanged": True,
            }
        )
    return {
        "mode": mode,
        "player_id": 0,
        "system_id": 8,
        "body_id": 8004,
        "colony_id": 9,
        "before_days": before["SimulationDays"],
        "after_days": after["SimulationDays"],
        "power_supply_before": 6.0 if paused and completed_state else 2.0,
        "power_supply_after": 6.0 if paused else 6.0,
        "science_before": 1.0 if paused and completed_state else 0.0,
        "science_after": 1.0,
        "research_instance_id": "construction:surface:9:14",
        "research_lab_active_count": 1,
        "research_labs_before": labs_before,
        "research_labs_after": labs_before if paused else labs_before + 1,
        "fabricator_operational": True,
        "opened_surface": True,
        "roundtrip": True,
        "stages": stages,
    }


def completed():
    before = source()
    after = copy.deepcopy(before)
    after["SavedAtUtc"] = "done"
    after["SimulationDays"] += 2
    after["Galaxy"]["Economies"][0]["Credits"] -= 65
    after["Galaxy"]["Colonies"][0]["SurfaceBuildings"] += [
        {
            "Id": 13,
            "TypeId": "power_generator",
            "X": 1.0,
            "Z": 2.0,
            "RotationDegrees": 0.0,
            "IndustryProgress": 300.0,
            "IsComplete": True,
            "IsEnabled": True,
        },
        {
            "Id": 14,
            "TypeId": "science_lab",
            "X": 2.0,
            "Z": 3.0,
            "RotationDegrees": 0.0,
            "IndustryProgress": 400.0,
            "IsComplete": True,
            "IsEnabled": True,
        },
    ]
    research = after["AdaptiveResearch"]["Civilizations"][0]["Research"]["Research"]
    research["Core"]["TotalEffectiveResearchLabs"] = 13.0
    research["Expertise"]["Institutions"] = [
        {
            "InstitutionInstanceId": "construction:surface:9:14",
            "InstitutionArchetypeId": "surface_science_laboratory",
            "ContextId": "colony:9",
            "TotalCount": 1,
            "ActiveCount": 1,
        }
    ]
    return (before, after)


class EarnedSurfaceExpansionTests(unittest.TestCase):

    def test_resume_proof_and_saved_sites_bind(self):
        before, after = completed()
        value = proof("expansion", before, after)
        runtime._proof("earned_surface_expansion=" + json.dumps(value), "expansion")
        runtime._save(after, value, runtime._source(before))

    def test_expansion_follows_the_actual_earned_planet(self):
        before, after = completed()
        for payload in (before, after):
            payload["Galaxy"]["Colonies"][0].update(SystemId=44, PlanetaryBodyId=44010)
        value = proof("expansion", before, after)
        value.update(system_id=44, body_id=44010)
        runtime._proof("earned_surface_expansion=" + json.dumps(value), "expansion")
        state = runtime._source(before)
        runtime._save(after, value, state)
        value["body_id"] = 44011
        with self.assertRaisesRegex(RuntimeError, "identity"):
            runtime._save(after, value, state)
        value["body_id"] = 44010
        after["Galaxy"]["Colonies"][0]["PlanetaryBodyId"] = 44011
        with self.assertRaisesRegex(RuntimeError, "moved"):
            runtime._save(after, value, state)

    def test_rejects_bad_cost_time_output_and_site(self):
        before, after = completed()
        for mutate in (
            lambda x: x["stages"][0].update(industry_cost=299),
            lambda x: x["stages"][1].update(treasury_after=936),
            lambda x: x.update(after_days=x["after_days"] + 1),
            lambda x: x.update(science_after=0),
            lambda x: x.update(research_labs_after=12),
        ):
            value = proof("expansion", before, after)
            mutate(value)
            with self.assertRaises(RuntimeError):
                runtime._proof(
                    "earned_surface_expansion=" + json.dumps(value), "expansion"
                )
        value = proof("expansion", before, after)
        after["Galaxy"]["Colonies"][0]["SurfaceBuildings"][2]["Id"] = 99
        with self.assertRaises(RuntimeError):
            runtime._save(after, value, runtime._source(before))

    def test_paused_requires_no_time_or_output_mutation(self):
        before, after = completed()
        paused = copy.deepcopy(after)
        paused["SavedAtUtc"] = "paused"
        value = proof("paused", after, paused)
        runtime._proof("earned_surface_expansion=" + json.dumps(value), "paused")
        runtime._bind_paused_final(value, proof("expansion", before, after), paused)
        value["stages"][0]["authorization"] = 1
        with self.assertRaises(RuntimeError):
            runtime._proof("earned_surface_expansion=" + json.dumps(value), "paused")

    def test_rejects_forged_paused_output_or_treasury(self):
        before, after = completed()
        paused = copy.deepcopy(after)
        paused["SavedAtUtc"] = "paused"
        resume = proof("expansion", before, after)
        value = proof("paused", after, paused)
        value["science_after"] = 2
        with self.assertRaises(RuntimeError):
            runtime._bind_paused_final(value, resume, paused)
        value = proof("paused", after, paused)
        value["stages"][1]["treasury_before"] += 1
        value["stages"][1]["treasury_after"] += 1
        with self.assertRaises(RuntimeError):
            runtime._bind_paused_final(value, resume, paused)

    def test_rejects_same_count_identity_swap(self):
        before, after = completed()
        before["Galaxy"]["Fleets"].append({"Id": 2, "CivilizationId": 1})
        after["Galaxy"]["Fleets"] = [
            {"Id": 2, "CivilizationId": 0},
            {"Id": 1, "CivilizationId": 1},
        ]
        with self.assertRaises(RuntimeError):
            runtime._save(
                after, proof("expansion", before, after), runtime._source(before)
            )

    def test_rejects_malformed_source_and_forged_research_institution(self):
        malformed = source()
        malformed["Galaxy"]["Colonies"] = []
        with self.assertRaises(RuntimeError):
            runtime._source(malformed)
        before, after = completed()
        value = proof("expansion", before, after)
        after["AdaptiveResearch"]["Civilizations"][0]["Research"]["Research"][
            "Expertise"
        ]["Institutions"][0]["InstitutionInstanceId"] = "foreign"
        with self.assertRaises(RuntimeError):
            runtime._save(after, value, runtime._source(before))

    def test_rejects_forged_research_instance_context_and_count(self):
        before, after = completed()
        source_state = runtime._source(before)
        for key, value in (
            ("research_instance_id", "construction:surface:9:13"),
            ("research_lab_active_count", 2),
        ):
            forged = proof("expansion", before, after)
            forged[key] = value
            with self.assertRaises(RuntimeError):
                runtime._proof(
                    "earned_surface_expansion=" + json.dumps(forged), "expansion"
                )
        forged = copy.deepcopy(after)
        institution = forged["AdaptiveResearch"]["Civilizations"][0]["Research"][
            "Research"
        ]["Expertise"]["Institutions"][0]
        institution["ContextId"] = "colony:8"
        with self.assertRaises(RuntimeError):
            runtime._save(forged, proof("expansion", before, after), source_state)
        institution["ContextId"] = "colony:9"
        institution["ActiveCount"] = True
        with self.assertRaises(RuntimeError):
            runtime._save(forged, proof("expansion", before, after), source_state)

    def test_public_api_orchestrates_both_modes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            package = root / "package"
            package.mkdir()
            (package / "stellar-continuum-native.exe").write_text("x")
            path = (root / "source.json").resolve()
            initial = source()
            path.write_text(json.dumps(initial))
            calls = []

            def launch(args, **unused):
                save = Path(args[args.index("--save-path") + 1])
                before = json.loads(save.read_text())
                paused = "--earned-surface-expansion-paused-smoke" in args
                if paused:
                    after = copy.deepcopy(before)
                    after["SavedAtUtc"] = "paused"
                    value = proof("paused", before, after)
                else:
                    _, after = completed()
                    value = proof("expansion", before, after)
                save.write_text(json.dumps(after))
                capture = Path(args[-1])
                width = int(args[args.index("--width") + 1])
                height = int(args[args.index("--height") + 1])
                capture.write_bytes(bmp(width, height))
                if not paused:
                    for tag in (
                        "-power-review",
                        "-power-construction",
                        "-power-complete",
                        "-science-review",
                        "-science-construction",
                        "-science-complete",
                    ):
                        capture.with_name(capture.stem + tag + ".bmp").write_bytes(
                            bmp(width, height)
                        )
                calls.append(paused)
                return subprocess.CompletedProcess(
                    args,
                    0,
                    "gpu_driver=vulkan systems=500 save=ok \nearned_surface_expansion="
                    + json.dumps(value),
                    "",
                )

            with mock.patch.object(base_runtime.subprocess, "run", side_effect=launch):
                result = runtime.validate_native_earned_surface_expansion_export(
                    package, {}, path
                )
            self.assertEqual(calls, [False, True])
            self.assertTrue(result["nativeEarnedSurfaceExpansion"])
            self.assertEqual(len(result["earnedSurfaceExpansionCaptures"]), 8)


if __name__ == "__main__":
    unittest.main()
