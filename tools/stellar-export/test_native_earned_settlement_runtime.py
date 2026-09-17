import json
import copy
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

import native_earned_settlement_runtime as runtime
from test_native_fresh_progression_runtime import payload as fresh_payload


class EarnedSettlementTests(unittest.TestCase):
    def chain(self):
        source=fresh_payload(6587.15625); source["Galaxy"]["Seed"]=115501; g=source["Galaxy"]; player=g["PlayerCivilizationId"]
        g["Economies"]=[{"CivilizationId":player,"Credits":900.0}]; g["Knowledge"]=[{"CivilizationId":player,"SystemSurveys":[{"SystemId":8,"Level":3,"Progress":1.0}]}]
        g["PlanetaryBodies"]=[{"Id":8004,"SystemId":8}]; g["Colonies"]=[]
        eligible=copy.deepcopy(source); eligible["SimulationDays"]=6600.0
        populated=copy.deepcopy(eligible); populated["SimulationDays"]=6650.0; populated["Galaxy"]["Fleets"]=[populated["Galaxy"]["Fleets"][0]]
        fleet=populated["Galaxy"]["Fleets"][0]; fleet.update(Id=2,CivilizationId=player,Role=2,DesignId="colony_ship",IsActive=True,EmbarkedPopulationMillions=250.0,MissionOrderRevision=0,DestinationSystemId=None,DestinationPlanetaryBodyId=None,SettlementBodyId=None,CurrentSystemId=0)
        partial=copy.deepcopy(populated); partial["SimulationDays"]=6700.0; fleet=partial["Galaxy"]["Fleets"][0]; fleet.update(CurrentSystemId=8,DestinationSystemId=None,DestinationPlanetaryBodyId=8004,SettlementBodyId=8004,SettlementDaysCompleted=5.0,MissionOrderRevision=1)
        founded=copy.deepcopy(partial); founded["SimulationDays"]=6762.421875; fleet=founded["Galaxy"]["Fleets"][0]; fleet.update(IsActive=False,EmbarkedPopulationMillions=0,MissionOrderRevision=2); founded["Galaxy"]["Colonies"]=[{"Id":9,"CivilizationId":player,"SystemId":8,"PlanetaryBodyId":8004,"Kind":0,"PopulationMillions":250.0}]
        final={"target":{"system_id":8,"body_id":8004,"kind":"colony"},"fleet_id":2,"colony_id":9,"before_day":6587.15625,"after_day":6762.421875,"outcomes":{"fleets":1,"colonies":1,"construction_states":len(founded["Galaxy"].get("ConstructionStates",[])),"treasury":900.0}}
        return source,{"eligible-site":eligible,"populated-vessel":populated,"partial-establishment":partial,"founded":founded},final

    def test_checkpoint_chain_accepts_arrival_null_destination_and_revisions(self):
        source, checkpoints, final=self.chain()
        _, player, fleet=runtime._validate_checkpoint_chain(source,checkpoints,final)
        self.assertEqual(player,0); self.assertEqual(fleet["MissionOrderRevision"],0)

    def test_checkpoint_chain_rejects_wrong_partial_revision_or_final_outcome(self):
        source, checkpoints, final=self.chain(); checkpoints["partial-establishment"]["Galaxy"]["Fleets"][0]["CurrentSystemId"]=7
        with self.assertRaises(RuntimeError): runtime._validate_checkpoint_chain(source,checkpoints,final)
        source, checkpoints, final=self.chain(); checkpoints["founded"]["Galaxy"]["Fleets"][0]["MissionOrderRevision"]=1
        with self.assertRaises(RuntimeError): runtime._validate_checkpoint_chain(source,checkpoints,final)
        source, checkpoints, final=self.chain(); final["outcomes"]["treasury"]=901.0
        with self.assertRaises(RuntimeError): runtime._validate_checkpoint_chain(source,checkpoints,final)
    def setup(self, root):
        package=root/"package"; (package/"Data"/"research"/"v1").mkdir(parents=True)
        checker=root/"checker.exe"; checker.write_text("x"); catalog=root/"catalog.json"; catalog.write_text("{}")
        value=fresh_payload(100.0); g=value["Galaxy"]; player=g["PlayerCivilizationId"]
        g["Economies"]=[{"CivilizationId":player,"Credits":1000.0}]
        g["Fleets"][0].update(Id=31,CivilizationId=player,Role=2,DesignId="colony_ship",DestinationSystemId=8,DestinationPlanetaryBodyId=8004,CurrentSystemId=8,MissionOrderRevision=1,EmbarkedPopulationMillions=250.0,IsActive=True)
        source=root/"source.json"; source.write_text(json.dumps(value)); return package,checker,catalog,source

    def test_parser_requires_established_and_traces(self):
        final={"kind":"earned_settlement","terminal":"blocked: no target","before_day":1,"after_day":2,"step_days":1/64,"visited_systems":[1],"ordered_steps":["x"]}
        with self.assertRaises(RuntimeError): runtime._checker_evidence(json.dumps(final))

    def test_parser_binds_successful_visits_to_last_target(self):
        visits=[{"kind":"earned_settlement_visit","system_id":2,"day":6600.0,"stage":"reconnaissance"},
                {"kind":"earned_settlement_visit","system_id":8,"day":6700.0,"stage":"reconnaissance"}]
        final={"kind":"earned_settlement","terminal":"established","before_day":6587.15625,
               "after_day":6762.421875,"step_days":1/64,"visited_systems":[2,8],
               "ordered_steps":["scout reconnaissance 2","science full survey 2","scout reconnaissance 8","science full survey 8","paid build 2","exact settlement order 2"],
               "outcomes":{"fleets":3,"colonies":2,"construction_states":1,"treasury":100.0},
               "target":{"system_id":8,"body_id":8004,"kind":"colony"},"fleet_id":2,"colony_id":9}
        evidence, parsed=runtime._checker_evidence("\n".join(json.dumps(x,separators=(",",":")) for x in visits+[final]))
        self.assertEqual(evidence["colony_id"],9); self.assertEqual([x["system_id"] for x in parsed],[2,8])

    def test_parser_rejects_wrong_visit_target_order_or_bound(self):
        visit={"kind":"earned_settlement_visit","system_id":2,"day":6600.0,"stage":"reconnaissance"}
        final={"kind":"earned_settlement","terminal":"established","before_day":6587.15625,"after_day":6601.0,"step_days":1/64,"visited_systems":[2],"ordered_steps":["x"],"outcomes":{"fleets":1,"colonies":1,"construction_states":1,"treasury":1.0},"target":{"system_id":3,"body_id":4,"kind":"colony"},"fleet_id":2,"colony_id":9}
        with self.assertRaises(RuntimeError): runtime._checker_evidence(json.dumps(visit)+"\n"+json.dumps(final))

    def test_blocked_checker_is_failure(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t); package,checker,catalog,source=self.setup(root)
            blocked=subprocess.CompletedProcess([],2,"terminal blocked","reason")
            with mock.patch.object(runtime.subprocess,"run",return_value=blocked):
                with self.assertRaisesRegex(RuntimeError,"blocked"):
                    runtime.validate_native_earned_settlement_export(package,{},source,checker,catalog)

    def test_missing_inputs_fail_before_launch(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t); package,checker,catalog,source=self.setup(root)
            with self.assertRaises(RuntimeError): runtime.validate_native_earned_settlement_export(package,{},source,root/"missing.exe",catalog)

if __name__=="__main__": unittest.main()
