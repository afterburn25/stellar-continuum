#!/usr/bin/env python3
"""Validate Adaptive Research starting histories, runtime boundary, and view model."""
from __future__ import annotations
import json, sys
from collections import Counter
from pathlib import Path

def fail(msg: str) -> None: raise SystemExit(f"research-start-runtime validation failed: {msg}")
def load(path: Path):
    try:
        with path.open("r", encoding="utf-8") as f: return json.load(f)
    except Exception as exc: fail(f"could not parse {path}: {exc}")
def unique(rows, label: str) -> set[str]:
    ids=[r.get("id") for r in rows]
    if not ids or None in ids or "" in ids: fail(f"{label} ids must be nonempty")
    dupes=[k for k,v in Counter(ids).items() if v>1]
    if dupes: fail(f"duplicate {label} ids: {dupes}")
    return set(ids)

def main() -> int:
    root=Path(sys.argv[1] if len(sys.argv)>1 else "data/research/v1")
    files={"index":"index.json","traits":"applicability_traits.json","caps":"capability_model.json","fields":"knowledge_fields.json","facilities":"research_facility_model.json","pressures":"pressure_dynamics.json","evidence":"evidence_types.json","tacit":"tacit_knowledge_model.json","capacity":"research_capacity.json","profile_contract":"starting_research_profile_contract.json","profile_index":"starting_profile_index.json","runtime":"research_runtime_contract.json","view":"research_view_model_contract.json"}
    data={}
    for key,filename in files.items():
        path=root/filename
        if not path.is_file(): fail(f"missing {path}")
        data[key]=load(path)
    catalog=data["index"].get("catalog_id")
    for key,payload in data.items():
        if payload.get("catalog_id")!=catalog: fail(f"{key}: catalog_id mismatch")

    nodes=[]
    for domain in data["index"].get("domains",[]):
        filename=data["index"].get("domain_files",{}).get(domain["id"])
        if not filename: fail(f"domain {domain['id']}: no file mapping")
        nodes.extend(load(root/filename).get("nodes",[]))
    node_ids=unique(nodes,"node"); node_by_id={n["id"]:n for n in nodes}
    state_order=data["index"].get("maturation_states",[]); state_rank={s:i for i,s in enumerate(state_order)}
    if "mature" not in state_rank or "investigable" not in state_rank: fail("missing required maturation states")
    trait_ids=unique(data["traits"].get("traits",[]),"trait")
    cap_ids=unique(data["caps"].get("cross_lineage_capabilities",[]),"capability")
    field_ids=unique(data["fields"].get("fields",[]),"field")
    institutions=data["facilities"].get("institution_archetypes",[]); institution_ids=unique(institutions,"institution"); inst_by_id={i["id"]:i for i in institutions}
    pressure_ids=set(data["pressures"].get("rules",{})); evidence_ids=unique(data["evidence"].get("evidence_types",[]),"evidence"); tacit_ids=unique(data["tacit"].get("knowledge_asset_types",[]),"tacit asset")
    capacity_stage_ids={r.get("stage_id") for r in data["capacity"].get("directed_program_model",{}).get("progression",[]) if r.get("stage_id")}

    contract=data["profile_contract"]
    if contract.get("future_tree_rule",{}).get("starting_profile_must_not_list_hidden_future_nodes") is not True: fail("profile contract must forbid hidden future nodes")
    if contract.get("future_tree_rule",{}).get("profile_does_not_create_species_specific_research_catalog") is not True: fail("profile contract must forbid species-specific catalogs")
    if contract.get("starting_visibility",{}).get("unknown_future_nodes_not_seeded_as_placeholders") is not True: fail("profile contract must forbid unknown placeholders")
    if contract.get("performance",{}).get("composed_profile_not_repeatedly_evaluated_during_campaign") is not True: fail("profile composition must be initialization-only")

    pindex=data["profile_index"]; cfg=pindex.get("files",{})
    fragment_files=cfg.get("history_fragment_files") or ([cfg["history_fragments"]] if cfg.get("history_fragments") else [])
    profile_files=cfg.get("reference_profile_files") or ([cfg["reference_profiles"]] if cfg.get("reference_profiles") else [])
    if not fragment_files or not profile_files: fail("starting_profile_index must list fragment/profile files")
    fragments=[]; profiles=[]; composition_cap=None
    for filename in fragment_files:
        payload=load(root/filename)
        if payload.get("catalog_id")!=catalog: fail(f"{filename}: catalog_id mismatch")
        fragments.extend(payload.get("fragments",[]))
        if payload.get("composition_cap_per_competence_component") is not None: composition_cap=payload["composition_cap_per_competence_component"]
    for filename in profile_files:
        payload=load(root/filename)
        if payload.get("catalog_id")!=catalog: fail(f"{filename}: catalog_id mismatch")
        profiles.extend(payload.get("profiles",[]))
    fragment_ids=unique(fragments,"starting fragment"); frag_by_id={f["id"]:f for f in fragments}; profile_ids=unique(profiles,"starting reference profile")
    base_ids={f["id"] for f in fragments if f.get("kind")=="base_era"}
    if not base_ids: fail("at least one base-era fragment required")
    if not isinstance(composition_cap,(int,float)) or not 1<=composition_cap<=100: fail("invalid composition competence cap")
    counts=pindex.get("counts",{})
    if int(counts.get("history_fragments",-1))!=len(fragments): fail(f"profile index fragment count {counts.get('history_fragments')} != {len(fragments)}")
    if int(counts.get("reference_profiles",-1))!=len(profiles): fail(f"profile index profile count {counts.get('reference_profiles')} != {len(profiles)}")
    if pindex.get("rules",{}).get("starting_history_catalogs_are_modular") is not True: fail("profile index must declare modular catalogs")

    def validate_seed(row: dict,label: str)->None:
        for nid,raw in row.get("starting_node_states",{}).items():
            state=raw.get("state") if isinstance(raw,dict) else raw
            if nid not in node_ids: fail(f"{label}: unknown node {nid}")
            if state not in state_rank: fail(f"{label}: invalid state {state} for {nid}")
            if state in {"unknown","rumored"}: fail(f"{label}: omit {state} node {nid} rather than seeding it")
        for fid,comps in row.get("starting_field_competence",{}).items():
            if fid not in field_ids: fail(f"{label}: unknown field {fid}")
            for component in ("theoretical","experimental","engineering"):
                value=comps.get(component,0)
                if not isinstance(value,(int,float)) or not 0<=value<=100: fail(f"{label}: invalid {fid}.{component}={value}")
        for trait in row.get("starting_applicability_traits",[]):
            if trait not in trait_ids: fail(f"{label}: unknown trait {trait}")
        for cap in row.get("starting_capabilities",[]):
            cid=cap.get("id") if isinstance(cap,dict) else cap
            if cid not in cap_ids: fail(f"{label}: unknown capability {cid}")
        for inst in row.get("starting_research_institutions",[]):
            iid=inst.get("institution_archetype_id")
            if iid not in institution_ids: fail(f"{label}: unknown institution {iid}")
            if not isinstance(inst.get("count"),int) or inst["count"]<1: fail(f"{label}: invalid institution count {iid}")
        for pid,value in row.get("starting_pressure_state",{}).items():
            if pid not in pressure_ids: fail(f"{label}: unknown pressure {pid}")
            if not isinstance(value,(int,float)) or not 0<=value<=100: fail(f"{label}: invalid pressure {pid}={value}")
        for ev in row.get("starting_evidence",[]):
            eid=ev.get("type_id") if isinstance(ev,dict) else ev
            if eid not in evidence_ids: fail(f"{label}: unknown evidence {eid}")
        for asset in row.get("starting_tacit_assets",[]):
            aid=asset.get("type_id") if isinstance(asset,dict) else asset
            if aid not in tacit_ids: fail(f"{label}: unknown tacit asset {aid}")
        stage=row.get("starting_directed_program_stage")
        if stage is not None and stage not in capacity_stage_ids: fail(f"{label}: unknown capacity stage {stage}")

    for frag in fragments:
        validate_seed(frag,f"fragment {frag['id']}")
        if frag.get("kind") not in {"base_era","scientific_history_fragment","environmental_history_fragment"}: fail(f"fragment {frag['id']}: invalid kind")

    def compose(profile:dict)->dict:
        refs=profile.get("fragment_ids",[]); missing=sorted(set(refs)-fragment_ids)
        if missing: fail(f"profile {profile['id']}: unknown fragments {missing}")
        bases=[r for r in refs if r in base_ids]
        if len(bases)!=1: fail(f"profile {profile['id']}: exactly one base-era fragment required, found {bases}")
        states={}; traits=set(); caps=set(); pressures={}; competence={}; insts=Counter()
        def merge(row:dict):
            for nid,raw in row.get("starting_node_states",{}).items():
                s=raw.get("state") if isinstance(raw,dict) else raw
                if nid not in states or state_rank[s]>state_rank[states[nid]]: states[nid]=s
            traits.update(row.get("starting_applicability_traits",[]))
            for cap in row.get("starting_capabilities",[]): caps.add(cap.get("id") if isinstance(cap,dict) else cap)
            for pid,val in row.get("starting_pressure_state",{}).items(): pressures[pid]=max(pressures.get(pid,0),val)
            for fid,comps in row.get("starting_field_competence",{}).items():
                tgt=competence.setdefault(fid,{"theoretical":0,"experimental":0,"engineering":0})
                for c in tgt: tgt[c]=min(composition_cap,max(tgt[c],comps.get(c,0)))
            for inst in row.get("starting_research_institutions",[]): insts[inst["institution_archetype_id"]]+=inst["count"]
        for ref in refs: merge(frag_by_id[ref])
        extra={"starting_node_states":profile.get("additional_starting_node_states",{}),"starting_field_competence":profile.get("additional_starting_field_competence",{}),"starting_applicability_traits":profile.get("additional_starting_applicability_traits",[]),"starting_capabilities":profile.get("additional_starting_capabilities",[]),"starting_research_institutions":profile.get("additional_starting_research_institutions",[]),"starting_pressure_state":profile.get("additional_starting_pressure_state",{}),"starting_evidence":profile.get("additional_starting_evidence",[]),"starting_tacit_assets":profile.get("additional_starting_tacit_assets",[]),"starting_directed_program_stage":profile.get("additional_starting_directed_program_stage")}
        validate_seed(extra,f"profile {profile['id']}"); merge(extra)
        return {"states":states,"traits":traits,"caps":caps,"pressures":pressures,"competence":competence,"institutions":insts}

    for profile in profiles:
        result=compose(profile); states=result["states"]
        for nid,state in states.items():
            if state_rank[state]<state_rank["investigable"]: continue
            prereq=node_by_id[nid].get("prerequisites",{})
            for parent in prereq.get("all_of",[]):
                if states.get(parent)!="mature": fail(f"profile {profile['id']}: {nid}={state} but prerequisite {parent} not Mature")
            any_of=prereq.get("any_of",[])
            if any_of and not any(states.get(p)=="mature" for p in any_of): fail(f"profile {profile['id']}: {nid}={state} but no any_of prerequisite Mature {any_of}")
            missing_traits=[t for t in node_by_id[nid].get("applicability",{}).get("requires_traits",[]) if t not in result["traits"]]
            if missing_traits: fail(f"profile {profile['id']}: seeded {nid} lacks traits {missing_traits}")
        for iid,count in result["institutions"].items():
            if count<1: fail(f"profile {profile['id']}: invalid institution count {iid}")
            enabled=inst_by_id[iid].get("enabled_by")
            if enabled and states.get(enabled)!="mature": fail(f"profile {profile['id']}: institution {iid} requires Mature {enabled}")
        if not states: fail(f"profile {profile['id']}: empty research history")

    runtime=data["runtime"]
    for key in ("no_full_graph_per_tick_scan","foreign_assessments_recompute_on_relevant_change_only","UI_projection_rebuilds_on_research_state_change_not_every_frame"):
        if runtime.get("tick_policy",{}).get(key) is not True: fail(f"runtime must set {key}=true")
    if runtime.get("save_contract",{}).get("do_not_serialize_static_catalog") is not True: fail("runtime must not serialize static catalog")
    if runtime.get("fair_information_contract",{}).get("enemy_exact_hidden_technology_never_used_as_research_input") is not True: fail("runtime must forbid hidden enemy tech")
    unique(runtime.get("external_input_events",[]),"runtime input event"); unique(runtime.get("external_queries",[]),"runtime query")

    view=data["view"]; proj=view.get("projection_rules",{}); perf=view.get("performance",{})
    for key in ("read_only","unknown_nodes_never_projected","hidden_node_count_never_exposed","visible_edges_only_between_projected_nodes"):
        if proj.get(key) is not True: fail(f"view must set {key}=true")
    if perf.get("no_per_frame_hidden_graph_query") is not True: fail("view must forbid per-frame hidden graph query")
    commands=unique(view.get("command_contract_from_UI_or_AI",[]),"view command")
    for required in ("start_directed_research","pause_directed_research","resume_directed_research","reallocate_research_labs"):
        if required not in commands: fail(f"view missing command {required}")

    print(f"research start/runtime OK: {len(fragments)} fragments/{len(fragment_files)} files, {len(profiles)} profiles/{len(profile_files)} files, {len(runtime.get('external_input_events',[]))} input events, {len(runtime.get('external_queries',[]))} queries")
    return 0

if __name__=="__main__": raise SystemExit(main())
