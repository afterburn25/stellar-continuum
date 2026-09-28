import copy
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

import native_settlement_completion_runtime as runtime
from test_native_fresh_progression_runtime import payload as fresh_payload


def bmp(w, h):
    stride=(w*3+3)&~3; pixels=(bytes(range(251))*((stride*h//251)+1))[:stride*h]
    return b"BM"+struct.pack("<IHHI",54+len(pixels),0,0,54)+struct.pack("<IiiHHIIiiII",40,w,h,1,24,0,len(pixels),2835,2835,0,0)+pixels


class CompletionTests(unittest.TestCase):
    def source(self, root, fault=None):
        value=fresh_payload(100.0); g=value["Galaxy"]; player=g["PlayerCivilizationId"]
        g["Colonies"]=[{"Id":1,"CivilizationId":player,"SystemId":0,"PlanetaryBodyId":0,"Kind":0,"PopulationMillions":100.0}]
        g["Knowledge"]=[{"CivilizationId":player,"KnownSystemIds":[1],"SystemSurveys":[{"SystemId":1,"Level":3,"Progress":1.0}]}]
        g["PlanetaryBodies"]=[{"Id":1001,"SystemId":1,"ParentBodyId":None}]
        f=g["Fleets"][0]; f.update(CivilizationId=player,IsActive=True,Role=2,DesignId="colony_ship",DestinationSystemId=1,DestinationPlanetaryBodyId=1001,CurrentSystemId=0,MissionOrderRevision=4,SettlementDaysCompleted=24.0,EmbarkedPopulationMillions=4.5)
        if fault=="wrongtarget": f["DestinationPlanetaryBodyId"]=999
        if fault=="clock": value["SimulationDays"]="bad"
        path=root/"active.json"; path.write_text(json.dumps(value)); return path

    def run_case(self, fault=None):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t); package=root/"package"; package.mkdir(); source=self.source(root,fault); calls=[]
            def launch(args,**unused):
                save=Path(args[args.index("--save-path")+1]); before=json.loads(save.read_text()); after=copy.deepcopy(before)
                mode="--settlement-completion-smoke" in args
                if mode:
                    g=after["Galaxy"]; f=g["Fleets"][0]; f.update(IsActive=False,EmbarkedPopulationMillions=0,DestinationSystemId=None,DestinationPlanetaryBodyId=None,MissionOrderRevision=5)
                    g["Colonies"].append({"Id":99,"CivilizationId":g["PlayerCivilizationId"],"SystemId":1,"PlanetaryBodyId":1001,"Kind":0,"PopulationMillions":4.5})
                    after["SimulationDays"]+=6; mode_name="resume"; side=True
                else: mode_name="paused"; side=False
                save.write_text(json.dumps(after)); w=int(args[args.index("--width")+1]); h=int(args[args.index("--height")+1]); capture=Path(args[-1]); capture.write_bytes(bmp(w,h))
                if side: capture.with_name(capture.stem+"-establishment.bmp").write_bytes(bmp(w,h))
                p={"mode":mode_name,"player_id":0,"fleet_id":7,"system_id":1,"body_id":1001,"colony_id":99,"kind":"colony","revision":5,"before_days":before["SimulationDays"],"after_days":after["SimulationDays"],"steps":384 if side else 0,"step_days":1/64,"colonies_before":1 if side else 2,"colonies_after":2,"consumed":True,"opened_colony":True,"read_only":True,"feedback":side,"roundtrip":True}
                if not side:p["before_days"]=p["after_days"]=before["SimulationDays"]
                if fault=="malformed": proof="settlement_completion={bad}"
                elif fault=="duplicate": proof='settlement_completion={"mode":"resume","mode":"resume"}'
                elif fault=="falsefounding": p["consumed"]=False; proof="settlement_completion="+json.dumps(p,separators=(",",":"))
                else: proof="settlement_completion="+json.dumps(p,separators=(",",":"))
                calls.append(args); return subprocess.CompletedProcess(args,0,"gpu_driver=vulkan systems=500 save=ok \n"+proof,"")
            with mock.patch.object(runtime.subprocess,"run",side_effect=launch):
                if fault:
                    with self.assertRaises(RuntimeError): runtime.validate_native_settlement_completion_export(package,{},source)
                else:
                    result=runtime.validate_native_settlement_completion_export(package,{},source); self.assertEqual(len(calls),2); self.assertEqual(len(result["settlementCompletionCaptures"]),3); self.assertEqual(len(result["settlementCompletionSaveCaptures"]),2)

    def test_complete(self): self.run_case()
    def test_malformed_proof(self): self.run_case("malformed")
    def test_duplicate_keys(self): self.run_case("duplicate")
    def test_false_founding(self): self.run_case("falsefounding")
    def test_wrong_target(self): self.run_case("wrongtarget")
    def test_invalid_source_clock(self): self.run_case("clock")

if __name__=="__main__": unittest.main()
