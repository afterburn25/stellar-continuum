#!/usr/bin/env python3
"""Benchmark biochemical applicability/divergence across shared Adaptive Research graph."""
from __future__ import annotations
import json, sys
from pathlib import Path


def fail(msg:str)->None: raise SystemExit(f"research-biochemistry-benchmark failed: {msg}")
def load(path:Path):
    try:
        with path.open("r",encoding="utf-8") as f:return json.load(f)
    except Exception as exc: fail(f"could not parse {path}: {exc}")

def jaccard_distance(a:set[str],b:set[str])->float:
    union=a|b
    return 0.0 if not union else 1.0-len(a&b)/len(union)

def main()->int:
    root=Path(sys.argv[1] if len(sys.argv)>1 else "data/research/v1")
    index=load(root/"index.json"); bench=load(root/"biochemistry_benchmark_scenarios.json"); pindex=load(root/"starting_profile_index.json")
    catalog=index.get("catalog_id")
    if bench.get("catalog_id")!=catalog or pindex.get("catalog_id")!=catalog: fail("catalog_id mismatch")
    domain_file=index.get("domain_files",{}).get("alternative_biochemistry")
    if not domain_file: fail("alternative_biochemistry domain missing")
    nodes=load(root/domain_file).get("nodes",[])
    if len(nodes)!=30: fail(f"expected 30 biochemical nodes, found {len(nodes)}")

    file_cfg=pindex.get("files",{})
    fragment_files=file_cfg.get("history_fragment_files",[]); profile_files=file_cfg.get("reference_profile_files",[])
    fragments={}; profiles={}
    for filename in fragment_files:
        for row in load(root/filename).get("fragments",[]):
            if row["id"] in fragments: fail(f"duplicate fragment {row['id']}")
            fragments[row["id"]]=row
    for filename in profile_files:
        for row in load(root/filename).get("profiles",[]):
            if row["id"] in profiles: fail(f"duplicate profile {row['id']}")
            profiles[row["id"]]=row

    def profile_traits(profile_id:str)->set[str]:
        profile=profiles.get(profile_id)
        if not profile: fail(f"unknown profile {profile_id}")
        traits=set(profile.get("additional_starting_applicability_traits",[]))
        for fragment_id in profile.get("fragment_ids",[]):
            fragment=fragments.get(fragment_id)
            if not fragment: fail(f"profile {profile_id}: unknown fragment {fragment_id}")
            traits.update(fragment.get("starting_applicability_traits",[]))
        return traits

    shared_foundation={n["id"] for n in nodes if n.get("solution_family")=="biochemical_foundations" and not n.get("applicability",{}).get("requires_evidence")}
    if len(shared_foundation)!=4: fail(f"expected 4 shared biochemical foundations, found {len(shared_foundation)}")
    cross_nodes={n["id"] for n in nodes if n.get("solution_family")=="cross_biochemistry"}
    if len(cross_nodes)!=6: fail(f"expected 6 comparative cross-biochemistry nodes, found {len(cross_nodes)}")
    for node in nodes:
        if node["id"] in cross_nodes and "alien_biology" not in node.get("applicability",{}).get("requires_evidence",[]): fail(f"cross node {node['id']} lacks alien_biology evidence gate")

    outputs={}; specific_sets={}
    all_specific_families={n.get("solution_family") for n in nodes if n.get("solution_family") not in {"biochemical_foundations","cross_biochemistry"}}
    for cfg in bench.get("reference_profiles",[]):
        pid=cfg["profile_id"]; traits=profile_traits(pid)
        missing=sorted(set(cfg.get("expected_traits",[]))-traits)
        if missing: fail(f"{pid}: missing expected traits {missing}")
        applicable=set(); applicable_specific=set(); families=set()
        for node in nodes:
            req_traits=set(node.get("applicability",{}).get("requires_traits",[]))
            req_evidence=set(node.get("applicability",{}).get("requires_evidence",[]))
            if req_evidence: continue
            if req_traits.issubset(traits):
                applicable.add(node["id"])
                family=node.get("solution_family")
                if family not in {"biochemical_foundations","cross_biochemistry"}:
                    applicable_specific.add(node["id"]); families.add(family)
        expected=set(cfg.get("expected_native_specific_families",[])); forbidden=set(cfg.get("forbidden_native_specific_families",[]))
        if not expected.issubset(families): fail(f"{pid}: expected native families missing {sorted(expected-families)}; got {sorted(families)}")
        bad=sorted(families&forbidden)
        if bad: fail(f"{pid}: incompatible native families applicable {bad}")
        if not shared_foundation.issubset(applicable): fail(f"{pid}: shared metabolic biochemical foundations not all applicable")
        specific_sets[pid]=applicable_specific
        outputs[pid]={"traits":len(traits),"applicable_domain_nodes":len(applicable),"applicable_native_specific_nodes":len(applicable_specific),"families":sorted(families)}

    human=specific_sets.get("reference_humanlike_solar_2050",set())
    if human: fail(f"human-like carbon-water reference unexpectedly has exotic native-specific nodes {sorted(human)}")
    minimum=int(bench["assertions"]["minimum_applicable_specific_nodes_for_each_exotic_profile"])
    exotic=[pid for pid in specific_sets if pid!="reference_humanlike_solar_2050"]
    for pid in exotic:
        if len(specific_sets[pid])<minimum: fail(f"{pid}: only {len(specific_sets[pid])} applicable specific nodes, need {minimum}")
    min_distance=float(bench["assertions"]["minimum_pairwise_specific_applicability_jaccard_distance"])
    distances=[]
    for i,left in enumerate(exotic):
        for right in exotic[i+1:]:
            d=jaccard_distance(specific_sets[left],specific_sets[right]); distances.append((left,right,d))
            if d<min_distance: fail(f"biochemical specific applicability too similar {left}/{right}: {d:.3f} < {min_distance:.3f}")
    if int(bench["assertions"]["all_profiles_use_same_catalog_node_count"])!=int(index.get("node_count",-1)): fail("benchmark/catalog node count mismatch")

    print("research biochemistry benchmark OK")
    for pid in sorted(outputs): print(f"  {pid}: {json.dumps(outputs[pid],sort_keys=True)}")
    for left,right,d in distances: print(f"  jaccard_distance {left} vs {right}: {d:.3f}")
    return 0

if __name__=="__main__": raise SystemExit(main())
