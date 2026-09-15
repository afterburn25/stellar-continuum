"""Maintained exporter integrity and native checkpoint regression checks."""
import importlib.util
import copy
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from research_runtime_files import copy_research_runtime_files, load_research_runtime_files

spec = importlib.util.spec_from_file_location("stellar_export", Path(__file__).with_name("stellar.py"))
exporter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(exporter)

class PackageIntegrity(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="stellar-package-test-")
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        (self.root / "Configuration").mkdir()
        (self.root / "stellar-continuum.exe").write_bytes(b"fixture, not an executable")
        (self.root / "Configuration/runtime-config.json").write_text("{}")
        self.seal()

    def seal(self, **overrides):
        self.manifest = dict(files=exporter.hashes(self.root), includeSymbols=False, **overrides)
        (self.root / "build-manifest.json").write_text(json.dumps(self.manifest))

    def test_valid_manifest(self):
        exporter.validate_manifest(self.root)

    def test_declared_catalog_cannot_be_omitted_from_sealed_package(self):
        relative="Data/astronomy/hyg-nearby-500-v1.json"
        self.seal(requiredRuntimeFiles=[relative])
        with self.assertRaisesRegex(RuntimeError,"Missing required runtime"):
            exporter.validate_manifest(self.root)
        catalog=self.root/relative; catalog.parent.mkdir(parents=True); catalog.write_text("{}")
        self.seal(requiredRuntimeFiles=[relative])
        exporter.validate_manifest(self.root)
        catalog.write_text("tampered data")
        with self.assertRaisesRegex(RuntimeError,"manifest"):
            exporter.validate_manifest(self.root)

    def test_modified_or_missing_runtime_is_rejected(self):
        (self.root / "stellar-continuum.exe").write_bytes(b"tampered")
        with self.assertRaisesRegex(RuntimeError, "manifest"):
            exporter.validate_manifest(self.root)
        (self.root / "stellar-continuum.exe").unlink()
        with self.assertRaises(RuntimeError): exporter.validate_manifest(self.root)

    def test_extra_file_is_rejected(self):
        (self.root / "unexpected.dll").write_bytes(b"unlisted")
        with self.assertRaisesRegex(RuntimeError, "manifest"): exporter.validate_manifest(self.root)

    def test_sources_and_public_symbols_are_rejected_even_when_manifested(self):
        for name in ("source.cpp", "private.pdb"):
            with self.subTest(name=name):
                item=self.root / name; item.write_bytes(b"development only"); self.seal()
                with self.assertRaises(RuntimeError): exporter.validate_manifest(self.root)
                item.unlink()

    def test_manifest_cannot_escape_export_or_duplicate_entries(self):
        for paths in (["../outside"], ["stellar-continuum.exe", "stellar-continuum.exe"]):
            self.manifest["files"]=[dict(path=p, bytes=0, sha256="") for p in paths]
            (self.root / "build-manifest.json").write_text(json.dumps(self.manifest))
            with self.assertRaises(RuntimeError): exporter.validate_manifest(self.root)

    def test_graphical_release_cannot_be_exported_before_parity(self):
        with self.assertRaisesRegex(RuntimeError, "(?i)graphical"):
            exporter.export("windows-release")

class ResearchRuntimePackaging(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(
            prefix="stellar-research-package-test-")
        self.addCleanup(self.scratch.cleanup)
        self.repository = Path(self.scratch.name)
        self.source = self.repository / "data/research/v1"
        (self.source / "nested").mkdir(parents=True)
        (self.source / "alpha.json").write_bytes(b'{"id":"alpha"}\n')
        (self.source / "nested/beta.json").write_bytes(b'{"id":"beta"}\n')
        (self.source / "README.md").write_text(
            "source documentation", encoding="utf-8")
        self.declaration = self.repository / "export/research-runtime-files.json"
        self.declaration.parent.mkdir()
        self.write_declaration(["alpha.json", "nested/beta.json"])

    def write_declaration(self, files, **overrides):
        document = {
            "schemaVersion": 1,
            "root": "data/research/v1",
            "destination": "Data/research/v1",
            "files": files,
        }
        document.update(overrides)
        self.declaration.write_text(json.dumps(document), encoding="utf-8")

    def package(self):
        output = self.repository / "package"
        (output / "Configuration").mkdir(parents=True)
        (output / "stellar-continuum.exe").write_bytes(b"test executable")
        (output / "Configuration/runtime-config.json").write_text(
            "{}", encoding="utf-8")
        required = copy_research_runtime_files(
            self.repository, output, self.declaration)
        manifest = {
            "files": exporter.hashes(output),
            "includeSymbols": False,
            "requiredRuntimeFiles": required,
        }
        (output / "build-manifest.json").write_text(
            json.dumps(manifest), encoding="utf-8")
        return output, required

    def test_only_declared_json_is_copied_and_sealed(self):
        output, required = self.package()
        self.assertEqual(required, ["Data/research/v1/alpha.json",
                                    "Data/research/v1/nested/beta.json"])
        self.assertFalse((output / "Data/research/v1/README.md").exists())
        exporter.validate_manifest(output)
        (output / required[1]).write_text("corrupt", encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "manifest"):
            exporter.validate_manifest(output)

    def test_declaration_must_match_canonical_inventory(self):
        self.write_declaration(["alpha.json"])
        with self.assertRaisesRegex(RuntimeError, "undeclared"):
            load_research_runtime_files(self.repository, self.declaration)

    def test_paths_are_sorted_unique_safe_and_case_unique(self):
        for files, message in (
                (["nested/beta.json", "alpha.json"], "sorted"),
                (["alpha.json", "alpha.json", "nested/beta.json"], "unique"),
                (["ALPHA.json", "alpha.json", "nested/beta.json"], "unique"),
                (["C:/escape.json", "alpha.json", "nested/beta.json"], "Unsafe"),
                (["$<CONFIG>.json", "alpha.json", "nested/beta.json"], "Unsafe"),
                (["alpha.json", "nested/beta.json", "nested;beta.json"], "Unsafe"),
                (["alpha.json", "nested//beta.json", "nested/beta.json"], "Unsafe"),
                (["../escape.json", "alpha.json", "nested/beta.json"], "Unsafe")):
            with self.subTest(files=files):
                self.write_declaration(files)
                with self.assertRaisesRegex(RuntimeError,message):
                    load_research_runtime_files(self.repository, self.declaration)

    def test_schema_and_files_require_exact_json_kinds(self):
        for schema, files in ((True, ["alpha.json", "nested/beta.json"]),
                              (1.0, ["alpha.json", "nested/beta.json"]),
                              (1, "alpha.json"), (1, [])):
            with self.subTest(schema=schema, files=files):
                self.write_declaration(files, schemaVersion=schema)
                with self.assertRaisesRegex(
                        RuntimeError, "schema|nonempty string array"):
                    load_research_runtime_files(
                        self.repository, self.declaration)

    def test_linked_runtime_file_is_rejected(self):
        linked = self.source / "nested/beta.json"
        external = self.repository / "external.json"
        external.write_bytes(linked.read_bytes())
        linked.unlink()
        try:
            linked.symlink_to(external)
        except OSError as error:
            self.skipTest(f"Creating a file symlink is unavailable: {error}")
        with self.assertRaisesRegex(RuntimeError, "linked"):
            load_research_runtime_files(self.repository, self.declaration)

@unittest.skipUnless(os.environ.get("STELLAR_NATIVE_EXE"), "Set STELLAR_NATIVE_EXE to run native integration checks")
class NativeRecovery(unittest.TestCase):
    def setUp(self):
        self.exe=Path(os.environ["STELLAR_NATIVE_EXE"]).resolve()
        self.scratch=tempfile.TemporaryDirectory(prefix="stellar-recovery-test-")
        self.addCleanup(self.scratch.cleanup); self.root=Path(self.scratch.name)

    def invoke(self, *args):
        return subprocess.run([str(self.exe), "--headless", *map(str,args)], cwd=self.root,
                              capture_output=True, text=True, timeout=30)

    def test_roundtrip_across_worker_counts(self):
        checkpoint=self.root / "save.scf"
        self.assertEqual(self.invoke("--ticks",5,"--workers",2,"--save",checkpoint).returncode,0)
        resumed=self.invoke("--ticks",5,"--workers",1,"--load",checkpoint)
        direct=self.invoke("--ticks",10,"--workers",4)
        self.assertEqual(resumed.returncode,0,resumed.stderr)
        a=json.loads(resumed.stdout); b=json.loads(direct.stdout)
        self.assertEqual(a["completedTicks"],10)
        self.assertEqual((a["checkpointHash"],a["distanceSum"]),(b["checkpointHash"],b["distanceSum"]))

    def test_corruption_and_old_game_format_fail_cleanly(self):
        checkpoint=self.root / "bad.scf"
        for content in ('{"schemaVersion":16}', "STELLAR_FOUNDATION_V1\n1", "STELLAR_FOUNDATION_V1\n1 500 2 0\n"):
            with self.subTest(content=content):
                checkpoint.write_text(content)
                result=self.invoke("--load",checkpoint)
                self.assertEqual(result.returncode,1)
                self.assertIn("error [",result.stderr)
                self.assertIn("Working directory:",result.stderr)
                self.assertEqual(checkpoint.read_text(),content)

    def test_existing_checkpoint_is_preserved(self):
        checkpoint=self.root / "preserve.scf"; checkpoint.write_text("previous valid game")
        result=self.invoke("--ticks",0,"--save",checkpoint)
        self.assertEqual(result.returncode,1)
        self.assertIn("Refusing to overwrite",result.stderr)
        self.assertEqual(checkpoint.read_text(),"previous valid game")

    def test_invalid_cli_inputs_fail_without_native_crash(self):
        for args in (("--systems","-1"),("--workers","0"),("--ticks","1000001"),("--unrecognized","1")):
            with self.subTest(args=args):
                result=self.invoke(*args)
                self.assertEqual(result.returncode,1)
                self.assertIn("error [",result.stderr)

    def test_galaxy_loads_assets_relative_to_executable(self):
        output=self.root / "galaxy.json"
        result=self.invoke("--generate-galaxy","--systems",250,"--seed",-1,"--catalog-output",output)
        self.assertEqual(result.returncode,0,result.stderr)
        report=json.loads(result.stdout); data=json.loads(output.read_text())
        self.assertEqual(Path(report["assetPath"]).resolve(),(self.exe.parent/"Data/astronomy/hyg-nearby-500-v1.json").resolve())
        self.assertEqual(len(data["systems"]),250)
        self.assertEqual(data["systems"][0]["name"],"Sol")
        self.assertEqual(data["solBodies"][-1]["name"],"Pluto")
        self.assertGreater(report["planetaryBodies"],10)
        self.assertEqual(report["planetaryBodies"],len(data["planetaryBodies"]))
        self.assertEqual(data["phase"],"physical-before-civilizations")
        self.assertNotIn("constructionStates",data)
        self.assertFalse(report["gameplayParity"])
        bodies={b["id"]:b for b in data["planetaryBodies"]}
        self.assertEqual(len(bodies),len(data["planetaryBodies"]))
        system_ids={s["id"] for s in data["systems"]}
        for body in bodies.values():
            self.assertIn(body["systemId"],system_ids)
            if body["parentBodyId"] is not None:
                self.assertIn(body["parentBodyId"],bodies)
                self.assertEqual(bodies[body["parentBodyId"]]["systemId"],body["systemId"])
        repeated=self.root/"repeated.json"
        repeat=self.invoke("--generate-galaxy","--systems",250,"--seed",-1,"--repeat",2,"--catalog-output",repeated)
        self.assertEqual(repeat.returncode,0,repeat.stderr)
        self.assertEqual(output.read_bytes(),repeated.read_bytes())
        original=output.read_bytes()
        again=self.invoke("--generate-galaxy","--catalog-output",output)
        self.assertEqual(again.returncode,1)
        self.assertEqual(original,output.read_bytes())

    def test_missing_and_damaged_galaxy_assets_fail_cleanly(self):
        destination=self.root/"Data/astronomy/hyg-nearby-500-v1.json"
        missing=self.invoke("--generate-galaxy","--asset-root",self.root)
        self.assertEqual(missing.returncode,1)
        self.assertIn("hyg-nearby-500-v1.json",missing.stderr)
        destination.parent.mkdir(parents=True)
        valid=json.loads((self.exe.parent/"Data/astronomy/hyg-nearby-500-v1.json").read_text())
        duplicate=json.loads(json.dumps(valid)); duplicate["systems"][1]["hygId"]=0
        wrong_version=json.loads(json.dumps(valid)); wrong_version["catalogVersion"]="future"
        for content in ("{damaged",json.dumps(duplicate),json.dumps(wrong_version)):
            destination.write_text(content)
            result=self.invoke("--generate-galaxy","--asset-root",self.root)
            self.assertEqual(result.returncode,1,result.stderr)
            self.assertIn("Cannot load stellar catalog",result.stderr)
            self.assertIn(str(destination),result.stderr)

    def test_founding_catalog_runs_independently_and_rejects_invalid_setup(self):
        output=self.root/"founding.json"
        args=("--generate-galaxy","--found-civilizations","--systems",250,
              "--civilizations",1,"--ancients",0,"--player-species","pelagic_high_pressure")
        first=self.invoke(*args,"--catalog-output",output)
        self.assertEqual(first.returncode,0,first.stderr)
        report=json.loads(first.stdout); data=json.loads(output.read_text())
        self.assertEqual(data["format"],"stellar-founding-catalog-v1")
        self.assertEqual(data["phase"],"founding-before-colonies")
        self.assertNotIn("constructionStates",data)
        self.assertFalse(report["gameplayParity"])
        self.assertEqual(report["foundingCivilizations"],1)
        self.assertEqual(data["civilizations"][0]["speciesId"],"pelagic_high_pressure")
        self.assertTrue(data["civilizations"][0]["isPlayer"])
        self.assertNotEqual(data["civilizations"][0]["homeSystemId"],0)
        self.assertEqual(len(data["civilizations"][0]["leadership"]),7)
        second=self.root/"founding-repeat.json"
        repeated=self.invoke(*args,"--repeat",2,"--catalog-output",second)
        self.assertEqual(repeated.returncode,0,repeated.stderr)
        self.assertEqual(output.read_bytes(),second.read_bytes())
        for invalid in (("--civilizations",14),("--ancients",-1),("--player-species","missing_species"),("--plan-homes",)):
            with self.subTest(invalid=invalid):
                failed=self.invoke(*args,*invalid)
                self.assertEqual(failed.returncode,1,failed.stderr)
                self.assertIn("error [",failed.stderr)
        unused=self.invoke("--generate-galaxy","--civilizations",1)
        self.assertEqual(unused.returncode,1)
        self.assertIn("require --found-civilizations",unused.stderr)

    def test_colony_seeding_preserves_sol_settlements_and_starting_budgets(self):
        output=self.root/"colonies.json"
        result=self.invoke("--generate-galaxy","--seed-colonies","--systems",250,"--catalog-output",output)
        self.assertEqual(result.returncode,0,result.stderr)
        report=json.loads(result.stdout); data=json.loads(output.read_text())
        self.assertEqual(data["format"],"stellar-colony-catalog-v1")
        self.assertEqual(data["phase"],"colonies-before-fleets")
        self.assertFalse(report["gameplayParity"])
        self.assertEqual((report["seededColonies"],report["seededEconomies"]),(9,7))
        self.assertEqual([(c["name"],c["planetaryBodyId"],c["populationMillions"]) for c in data["colonies"][:3]],
                         [("Earth",3,9500),("Luna",9,.10),("Mars",4,.25)])
        self.assertEqual(data["economies"][0]["credits"],500)
        self.assertEqual(data["economies"][0]["lastCreditsPerSecond"],0)
        self.assertEqual(data["economies"][-1]["credits"],50000)
        construction=data["constructionStates"]
        self.assertEqual([state["civilizationId"] for state in construction],
                         [civilization["id"] for civilization in data["civilizations"]])
        registry_ids={"research_network","industrial_automation","orbital_launch_complex","orbital_shipyard",
                      "asteroid_resource_network","warp_test_facility"}
        for civilization,state in zip(data["civilizations"],construction):
            with self.subTest(construction_civilization=civilization["id"]):
                self.assertEqual(state["civilizationId"],civilization["id"])
                self.assertIsNone(state["activeProjectId"])
                self.assertEqual(state["activeProjectProgress"],0)
                self.assertEqual(state["activeProjectAuthorizationCredits"],0)
                self.assertEqual(state["queuedProjects"],[])
                if civilization["isSeededAncient"]:
                    self.assertEqual(set(state["completedProjectIds"]),registry_ids)
                    self.assertEqual(len(state["completedProjectIds"]),len(registry_ids))
                else:
                    self.assertEqual(state["completedProjectIds"],[])
        support=data["colonySupport"]
        self.assertEqual(len(support),9)
        colonies={colony["id"]:colony for colony in data["colonies"]}
        self.assertEqual({item["colonyId"] for item in support},set(colonies))
        for colony in colonies.values():
            self.assertEqual(colony["storedFoodPopulationDaysMillions"],colony["populationMillions"]*30)
            self.assertEqual(colony["storedWaterPopulationDaysMillions"],colony["populationMillions"]*7)
        for item in support:
            with self.subTest(colony_id=item["colonyId"]):
                colony=colonies[item["colonyId"]]
                self.assertEqual(item["surface"]["supply"],2)
                self.assertEqual(item["surface"]["demand"],0)
                self.assertEqual(item["surface"]["poweredBuildingIds"],[])
                self.assertEqual(item["surface"]["staffedBuildingIds"],[])
                self.assertTrue(math.isfinite(item["sustenance"]["supportRatio"]))
                self.assertTrue(math.isfinite(item["reserves"]["foodReserveDays"]))
                self.assertTrue(math.isfinite(item["reserves"]["waterReserveDays"]))
                habitat=item["habitat"]; turnover=item["turnover"]
                self.assertEqual((habitat["colonyId"],habitat["civilizationId"],habitat["systemId"],habitat["speciesId"]),(colony["id"],colony["civilizationId"],colony["systemId"],colony["populationSpeciesId"]))
                self.assertEqual((turnover["colonyId"],turnover["speciesId"]),(colony["id"],colony["populationSpeciesId"]))
                self.assertTrue(math.isfinite(habitat["typicalDayMetabolicDemandMillions"]) and habitat["typicalDayMetabolicDemandMillions"]>0)
                self.assertTrue(math.isfinite(habitat["adultBiomassMillionKg"]) and habitat["adultBiomassMillionKg"]>0)
                self.assertTrue(math.isfinite(turnover["effectiveGrowthPaceFactor"]) and turnover["effectiveGrowthPaceFactor"]>0)
        self.assertGreaterEqual(next(item for item in support if colonies[item["colonyId"]]["name"]=="Earth")["sustenance"]["supportedPopulationMillions"],9500)
        for name in ("Luna","Mars"):
            item=next(item for item in support if colonies[item["colonyId"]]["name"]==name)
            self.assertGreaterEqual(item["sustenance"]["supportRatio"],0)
            self.assertGreater(item["habitat"]["gravityMitigationPopulationMillions"]+item["habitat"]["thermalControlPopulationMillions"]+item["habitat"]["pressureControlPopulationMillions"]+item["habitat"]["sealedHabitatPopulationMillions"]+item["habitat"]["artificialBiospherePopulationMillions"]+item["habitat"]["radiationShieldingPopulationMillions"],0)
        earth=next(item for item in support if colonies[item["colonyId"]]["name"]=="Earth")
        self.assertEqual(earth["habitat"]["environment"]["planetaryBodyId"],3)
        self.assertEqual(earth["turnover"]["planetaryBodyId"],3)
        self.assertEqual(earth["turnover"]["naturalEnvironmentTurnoverFactor"],1)
        self.assertFalse(earth["turnover"]["environmentalPressureApplied"])
        self.assertTrue(report["colonyBiologyPreview"])
        self.assertTrue(report["surfaceSupportPreview"])
        self.assertTrue(report["logisticsPreview"])
        self.assertTrue(data["logisticsPreview"])
        logistics=data["logistics"]
        self.assertEqual(len(logistics),7)
        self.assertEqual({item["civilizationId"] for item in logistics},{economy["civilizationId"] for economy in data["economies"]})
        for item in logistics:
            economy=item["economyLogistics"]
            coverage=item["coverage"]
            network=item["homeNetwork"]
            self.assertEqual(economy["civilizationId"],item["civilizationId"])
            self.assertEqual(coverage["civilizationId"],item["civilizationId"])
            self.assertEqual(network["civilizationId"],item["civilizationId"])
            self.assertEqual(network["dailyFlow"]["totalAllocatedPerDay"],network["totalAllocatedPerDay"])
            self.assertEqual(network["dailyFlow"]["totalUnmetDemandPerDay"],network["totalUnmetDemandPerDay"])
            self.assertEqual(sum(flow["allocatedPerDay"] for flow in network["dailyFlow"]["allocations"]),network["totalAllocatedPerDay"])
            unmet_total=network["totalUnmetDemandPerDay"]
            self.assertAlmostEqual(sum(row["perDay"] for row in network["dailyFlow"]["unmetDemandPerDay"]),unmet_total,delta=1e-12*max(1,abs(unmet_total)))
        human_civilization=next(item for item in data["civilizations"] if item["isPlayer"])
        self.assertEqual(human_civilization["speciesId"],"terran_baseline")
        self.assertEqual(human_civilization["homeSystemId"],0)
        human_civilization_id=human_civilization["id"]
        human=next(item for item in logistics if item["civilizationId"]==human_civilization_id)
        self.assertEqual(human["homeNetwork"]["homeSystemId"],0)
        self.assertEqual([node["id"] for node in human["homeNetwork"]["nodes"]],[1,2,3])
        # The source convention assigns the homeworld kind to Earth; Luna and Mars are planetary settlements.
        self.assertEqual([(node["name"],node["kind"]) for node in human["homeNetwork"]["nodes"]],[("Earth",0),("Luna",3),("Mars",3)])
        self.assertEqual([(link["capacityPerDay"],link["transitDays"]) for link in human["homeNetwork"]["links"]],[(.05,.75),(.05,.75)])
        human_colony_ids={item["id"] for item in data["colonies"] if item["civilizationId"]==human_civilization_id}
        self.assertEqual({item["colonyId"] for item in human["economyLogistics"]["colonies"]},human_colony_ids)
        self.assertEqual(human["coverage"]["externalSystems"],[])
        self.assertFalse(human["coverage"]["hasUnrepresentedInterstellarSupportGap"])
        repeated=self.root/"colonies-repeat.json"
        again=self.invoke("--generate-galaxy","--seed-colonies","--systems",250,"--repeat",2,"--catalog-output",repeated)
        self.assertEqual(again.returncode,0,again.stderr)
        self.assertEqual(output.read_bytes(),repeated.read_bytes())

    def test_native_home_preview_preserves_human_origin_and_distinct_worlds(self):
        for count in (250,500,1000,2500):
            with self.subTest(count=count):
                output=self.root/f"homes-{count}.json"
                result=self.invoke("--generate-galaxy","--plan-homes","--systems",count,"--catalog-output",output)
                self.assertEqual(result.returncode,0,result.stderr)
                report=json.loads(result.stdout); data=json.loads(output.read_text())
                self.assertEqual(report["plannedHomeworlds"],7)
                self.assertEqual(data["homeworldPlanning"],"normal-before-nearby-expansion")
                self.assertFalse(report["gameplayParity"])
                homes=data["homeworldPreview"]
                self.assertEqual(len({h["systemId"] for h in homes}),7)
                self.assertEqual((homes[0]["speciesId"],homes[0]["systemId"],homes[0]["planetaryBodyId"]),("terran_baseline",0,3))
                bodies={b["id"]:b for b in data["planetaryBodies"]}
                for home in homes:
                    self.assertEqual(bodies[home["planetaryBodyId"]]["systemId"],home["systemId"])
                    self.assertFalse(bodies[home["planetaryBodyId"]]["hasPreWarpCivilization"])
                    self.assertGreaterEqual(home["naturalHabitability"],.20)
                    if home["speciesId"]!="terran_baseline": self.assertNotEqual(home["systemId"],0)

    def test_fresh_campaign_is_complete_deterministic_and_not_a_player_save(self):
        first=self.root/"fresh.json"
        result=self.invoke("--seed-campaign","--systems",250,"--catalog-output",first)
        self.assertEqual(result.returncode,0,result.stderr)
        report=json.loads(result.stdout)
        data=json.loads(first.read_text(encoding="utf-8"))
        exporter.validate_fresh_campaign(data,report)
        # Exercise the exporter gate against realistic partial/corrupt diagnostic
        # records, independently of the Core parity consumer.
        for category in ("economy", "construction", "shipyard", "unexpected-fleet",
                         "report-count", "report-mode", "save-claim"):
            with self.subTest(corruption=category):
                damaged=copy.deepcopy(data); summary=copy.deepcopy(report)
                if category=="economy": damaged["economies"][0].pop("credits")
                elif category=="construction": damaged["constructionStates"][0]["activeProjectProgress"]=1
                elif category=="shipyard": damaged["shipyards"][0]["reservedPopulationSpeciesId"]="terran_baseline"
                elif category=="unexpected-fleet":
                    damaged["fleets"].append({"civilizationId":damaged["playerCivilizationId"]})
                    summary["seededFleets"]+=1
                elif category=="report-count": summary["systems"]+=1
                elif category=="report-mode": summary["mode"]="foundation-distance-benchmark"
                else: summary["playerSaveCompatible"]=True
                with self.assertRaises(RuntimeError): exporter.validate_fresh_campaign(damaged,summary)
        self.assertEqual(report["mode"],"fresh-campaign")
        self.assertEqual(len(data["civilizations"]),7)
        self.assertEqual(len(data["colonies"]),9)
        self.assertEqual(data["civilizations"][0]["homeSystemId"],0)
        self.assertGreater(report["elapsedMs"],0)
        second=self.root/"fresh-repeat.json"
        repeated=self.invoke("--generate-galaxy","--seed-campaign","--systems",250,
                             "--repeat",2,"--catalog-output",second)
        self.assertEqual(repeated.returncode,0,repeated.stderr)
        self.assertEqual(first.read_bytes(),second.read_bytes())
        loaded=self.invoke("--load",first)
        self.assertEqual(loaded.returncode,1)
        self.assertIn("not a migrated game save",loaded.stderr)

    def test_fresh_campaign_failures_preserve_output_and_report_terminal_context(self):
        output=self.root/"preserved.json"
        output.write_text("existing campaign evidence",encoding="utf-8")
        for options in (("--seed-colonies",),("--plan-homes",),("--found-civilizations",),
                        ("--systems",100),("--player-species","unknown_species"),
                        ("--asset-root",self.root/"missing"),("--catalog-output",output)):
            with self.subTest(options=options):
                result=self.invoke("--seed-campaign",*options)
                self.assertEqual(result.returncode,1,result.stderr)
                self.assertIn("error [",result.stderr)
                self.assertIn("Working directory:",result.stderr)
                self.assertEqual(output.read_text(encoding="utf-8"),"existing campaign evidence")

    def test_campaign_simulation_retains_state_and_writes_deterministic_diagnostics(self):
        first=self.root/"campaign-simulation-first.json"
        second=self.root/"campaign-simulation-second.json"
        arguments=("--simulate-campaign","--systems",250,"--ticks",2,
                   "--step-days",.5,"--repeat",2)
        initial=self.invoke(*arguments,"--catalog-output",first)
        repeated=self.invoke(*arguments,"--catalog-output",second)
        self.assertEqual(initial.returncode,0,initial.stderr)
        self.assertEqual(repeated.returncode,0,repeated.stderr)
        report=json.loads(initial.stdout); again=json.loads(repeated.stdout)
        diagnostic=json.loads(first.read_text(encoding="utf-8"))
        self.assertEqual(report["mode"],"legacy-campaign-simulation-benchmark")
        self.assertEqual(report["totalSimulatedDays"],2)
        self.assertTrue(report["stateAdvancedBeyondSeed"])
        self.assertTrue(report["repeatFinalStatesDeterministic"])
        self.assertGreater(report["outputRecordCounts"]["industryAllocations"],0)
        self.assertTrue(any(economy["lastCreditsPerSecond"]!=0 or
                            economy["lastIndustryPerSecond"]!=0 or
                            economy["lastSciencePerSecond"]!=0
                            for economy in diagnostic["economies"]))
        self.assertEqual(report["finalStateHash"],again["finalStateHash"])
        self.assertEqual(report["finalStateHash"],diagnostic["simulation"]["stateHash"])
        self.assertEqual(first.read_bytes(),second.read_bytes())
        self.assertEqual(diagnostic["format"],"stellar-campaign-simulation-diagnostic-v1")
        self.assertFalse(diagnostic["playerSaveCompatible"])
        self.assertNotIn("stepMeanMs",diagnostic)

        flag_value=self.invoke("--simulate-campaign","--systems",250,"--ticks",1,
                               "--catalog-output","--seed-campaign")
        self.assertEqual(flag_value.returncode,0,flag_value.stderr)
        flag_path=self.root/"--seed-campaign"; preserved=flag_path.read_bytes()
        overwrite=self.invoke("--simulate-campaign","--systems",250,"--ticks",1,
                              "--catalog-output","--seed-campaign")
        self.assertEqual(overwrite.returncode,1)
        self.assertIn("Refusing to overwrite",overwrite.stderr)
        self.assertEqual(flag_path.read_bytes(),preserved)

        failures=(("--simulate-campaign","--ticks",0),
                  ("--simulate-campaign","--step-days","NaN"),
                  ("--simulate-campaign","--step-days","1e308","--ticks",10000),
                  ("--simulate-campaign","--seed-campaign"),
                  ("--simulate-campaign","--generate-galaxy"),
                  ("--generate-galaxy","--ticks",1))
        for options in failures:
            with self.subTest(options=options):
                failed=self.invoke(*options)
                self.assertEqual(failed.returncode,1,failed.stderr)

    def test_adaptive_campaign_reports_complete_deterministic_state(self):
        first = self.root / "adaptive-first.json"
        second = self.root / "adaptive-second.json"
        arguments = ("--simulate-adaptive-campaign", "--systems", 250,
                     "--ticks", 1, "--step-days", .25, "--repeat", 2)
        initial = self.invoke(*arguments, "--catalog-output", first)
        repeated = self.invoke(*arguments, "--catalog-output", second)
        self.assertEqual(initial.returncode, 0, initial.stderr)
        self.assertEqual(repeated.returncode, 0, repeated.stderr)

        report = json.loads(initial.stdout)
        again = json.loads(repeated.stdout)
        diagnostic = json.loads(first.read_text(encoding="utf-8"))
        self.assertEqual(report["mode"],
                         "adaptive-campaign-simulation-benchmark")
        self.assertTrue(report["legacyResearchDisabled"])
        self.assertTrue(report["repeatFinalStatesDeterministic"])
        self.assertTrue(report["stateAdvancedBeyondSeed"])
        self.assertGreater(report["finalStateCounts"]["researchCivilizations"], 0)
        self.assertEqual(report["finalStateHash"], again["finalStateHash"])
        self.assertEqual(report["finalStateHash"],
                         diagnostic["simulation"]["stateHash"])
        self.assertEqual(first.read_bytes(), second.read_bytes())

        self.assertEqual(
            diagnostic["format"],
            "stellar-adaptive-campaign-simulation-diagnostic-v1")
        research = diagnostic["research"]
        self.assertEqual(research["schemaVersion"], 2)
        self.assertEqual(len(research["civilizations"]),
                         report["finalStateCounts"]["researchCivilizations"])
        self.assertTrue(research["civilizations"])
        for civilization in research["civilizations"]:
            self.assertEqual(civilization["research"]["schemaVersion"], 5)
        diplomacy = diagnostic["diplomacy"]
        for key in ("contacts", "relationships", "claims", "agreements",
                    "proposals", "recentHistory", "lastProcessedTick",
                    "nextMaintenanceReviewTick"):
            self.assertIn(key, diplomacy)
        self.assertIsInstance(diagnostic["combatIntelligence"], list)

    def test_adaptive_campaign_uses_resolved_assets_and_fails_cleanly(self):
        default_output = self.root / "adaptive-default-assets.json"
        default = self.invoke("--simulate-adaptive-campaign", "--systems", 250,
                              "--ticks", 1, "--catalog-output", default_output)
        self.assertEqual(default.returncode, 0, default.stderr)
        report = json.loads(default.stdout)
        self.assertEqual(
            Path(report["assetPath"]).resolve(),
            (self.exe.parent / "Data/astronomy/hyg-nearby-500-v1.json").resolve())
        self.assertEqual(Path(report["researchAssetPath"]).resolve(),
                         (self.exe.parent / "Data/research/v1").resolve())

        # Normalize the existing ancestors before the native invocation. A
        # missing child cannot reliably resolve Windows 8.3 aliases afterward.
        assets = (self.root / "explicit-assets").resolve()
        shutil.copytree(self.exe.parent / "Data/astronomy",
                        assets / "Data/astronomy")
        shutil.copytree(self.exe.parent / "Data/research",
                        assets / "Data/research")
        explicit_output = self.root / "adaptive-explicit-assets.json"
        explicit = self.invoke("--simulate-adaptive-campaign", "--systems", 250,
                               "--ticks", 1, "--asset-root", assets,
                               "--catalog-output", explicit_output)
        self.assertEqual(explicit.returncode, 0, explicit.stderr)
        explicit_report = json.loads(explicit.stdout)
        self.assertEqual(
            Path(explicit_report["assetPath"]).resolve(),
            (assets / "Data/astronomy/hyg-nearby-500-v1.json").resolve())
        research_root = (assets / "Data/research/v1").resolve()
        self.assertEqual(Path(explicit_report["researchAssetPath"]).resolve(),
                         research_root)

        unavailable = assets / "Data/research-v1-unavailable"
        research_root.rename(unavailable)
        try:
            missing = self.invoke("--simulate-adaptive-campaign", "--systems", 250,
                                  "--ticks", 1, "--asset-root", assets)
            self.assertEqual(missing.returncode, 1, missing.stderr)
            self.assertIn(str(research_root), missing.stderr)
            self.assertIn("Cannot initialize Adaptive Research", missing.stderr)
            self.assertIn("Working directory:", missing.stderr)
        finally:
            unavailable.rename(research_root)

        index = research_root / "index.json"
        original = index.read_bytes()
        try:
            index.write_bytes(b"{corrupt")
            corrupt = self.invoke("--simulate-adaptive-campaign", "--systems", 250,
                                  "--ticks", 1, "--asset-root", assets)
            self.assertEqual(corrupt.returncode, 1, corrupt.stderr)
            self.assertIn(str(research_root), corrupt.stderr)
            self.assertIn(str(index), corrupt.stderr)
            self.assertIn("Cannot initialize Adaptive Research", corrupt.stderr)
            self.assertIn("Working directory:", corrupt.stderr)
        finally:
            index.write_bytes(original)

    @unittest.skipUnless(os.name == "nt", "Windows short paths are required")
    def test_adaptive_campaign_resolved_assets_with_real_short_root(self):
        import ctypes

        get_short_path = ctypes.windll.kernel32.GetShortPathNameW
        get_short_path.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p,
                                   ctypes.c_uint32]
        get_short_path.restype = ctypes.c_uint32
        required = get_short_path(str(self.root), None, 0)
        if required == 0:
            self.skipTest("GetShortPathNameW is unavailable for the scratch root")
        buffer = ctypes.create_unicode_buffer(required)
        written = get_short_path(str(self.root), buffer, required)
        if written == 0 or written >= required:
            self.skipTest("GetShortPathNameW could not return the scratch root")
        short_root = Path(buffer.value)
        if os.path.normcase(str(short_root)) == os.path.normcase(str(self.root)):
            self.skipTest("The scratch root has no distinct Windows short alias")

        original_root = self.root
        self.root = short_root
        try:
            self.test_adaptive_campaign_uses_resolved_assets_and_fails_cleanly()
        finally:
            self.root = original_root

    def test_adaptive_campaign_rejects_modes_bounds_and_output_collisions(self):
        output = self.root / "adaptive-preserved.json"
        output.write_bytes(b"existing adaptive diagnostic")
        collision = self.invoke("--simulate-adaptive-campaign", "--systems", 250,
                                "--ticks", 1, "--catalog-output", output)
        self.assertEqual(collision.returncode, 1, collision.stderr)
        self.assertIn("Refusing to overwrite", collision.stderr)
        self.assertEqual(output.read_bytes(), b"existing adaptive diagnostic")
        self.assertFalse(Path(str(output) + ".pending").exists())

        pending_output = self.root / "adaptive-pending.json"
        pending = Path(str(pending_output) + ".pending")
        pending.write_bytes(b"owned by another writer")
        pending_collision = self.invoke(
            "--simulate-adaptive-campaign", "--systems", 250, "--ticks", 1,
            "--catalog-output", pending_output)
        self.assertEqual(pending_collision.returncode, 1,
                         pending_collision.stderr)
        self.assertEqual(pending.read_bytes(), b"owned by another writer")
        self.assertFalse(pending_output.exists())

        failures = (("--simulate-campaign", "--simulate-adaptive-campaign",
                     "--systems", 250),
                    ("--simulate-adaptive-campaign", "--ticks", 0),
                    ("--simulate-adaptive-campaign", "--ticks", -1),
                    ("--simulate-adaptive-campaign", "--repeat", 0),
                    ("--simulate-adaptive-campaign", "--step-days", 0),
                    ("--simulate-adaptive-campaign", "--step-days", -1))
        for options in failures:
            with self.subTest(options=options):
                failed = self.invoke(*options)
                self.assertEqual(failed.returncode, 1, failed.stderr)
                self.assertIn("error [", failed.stderr)
                self.assertIn("Working directory:", failed.stderr)

    def test_global_campaign_help_and_option_values_do_not_change_modes(self):
        for mode in ("--seed-campaign", "--generate-galaxy"):
            for global_option in ("--help", "--version"):
                for options in ((mode,global_option),(global_option,mode)):
                    with self.subTest(options=options):
                        result=self.invoke(*options)
                        self.assertEqual(result.returncode,0,result.stderr)
                        self.assertIn("Stellar Engine",result.stdout)
        # A filename resembling a mode flag must remain a filename.
        result=self.invoke("--ticks",0,"--save","--seed-campaign")
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(json.loads(result.stdout)["mode"],"foundation-distance-benchmark")
        self.assertTrue((self.root/"--seed-campaign").read_text().startswith("STELLAR_FOUNDATION_V1"))

if __name__ == "__main__": unittest.main()
