"""Maintained exporter integrity and native checkpoint regression checks."""
import importlib.util
import copy
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

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
