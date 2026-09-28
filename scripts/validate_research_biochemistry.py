#!/usr/bin/env python3
"""Validate species-neutral alternative-biochemistry Adaptive Research data."""
from __future__ import annotations
import json, sys
from collections import Counter
from pathlib import Path


def fail(msg: str) -> None: raise SystemExit(f"research-biochemistry validation failed: {msg}")
def load(path: Path):
    try:
        with path.open("r", encoding="utf-8") as f: return json.load(f)
    except Exception as exc: fail(f"could not parse {path}: {exc}")
def unique(rows,label):
    ids=[r.get("id") for r in rows]
    if not ids or None in ids or "" in ids: fail(f"{label} ids must be nonempty")
    dupes=[k for k,v in Counter(ids).items() if v>1]
    if dupes: fail(f"duplicate {label} ids: {dupes}")
    return set(ids)


def main()->int:
    root=Path(sys.argv[1] if len(sys.argv)>1 else "data/research/v1")
    names=["index.json","applicability_traits.json","knowledge_fields.json","emergence_model.json","biochemical_applicability_model.json","alternative_biochemistry.json","starting_profile_index.json","starting_research_fragments.json","starting_reference_profiles.json","starting_biochemical_fragments.json","starting_biochemical_reference_profiles.json","research_facility_model.json","biochemical_research_facilities.json","research_facility_index.json","evidence_types.json","pressure_dynamics.json"]
    payload={name:load(root/name) for name in names}
    catalog=payload["index.json"].get("catalog_id")
    for name,data in payload.items():
        if data.get("catalog_id")!=catalog: fail(f"{name}: catalog_id mismatch")

    index=payload["index.json"]
    if int(index.get("node_count",-1))!=370: fail(f"expected 370-node public catalog, found {index.get('node_count')}")
    domains=index.get("domains",[])
    if len(domains)!=21: fail(f"expected 21 domains, found {len(domains)}")
    domain={d["id"]:d for d in domains}.get("alternative_biochemistry")
    if not domain or int(domain.get("node_count",-1))!=30: fail("alternative_biochemistry domain must contain 30 nodes")
    if index.get("domain_files",{}).get("alternative_biochemistry")!="alternative_biochemistry.json": fail("alternative_biochemistry domain file mapping missing")
    solution_ids=unique(index.get("alternative_solution_sets",[]),"alternative solution set")
    if len(solution_ids)!=16 or "cross_biochemistry_compatibility" not in solution_ids: fail(f"expected 16 solution sets including cross_biochemistry_compatibility, found {len(solution_ids)}")

    traits=payload["applicability_traits.json"].get("traits",[]); trait_ids=unique(traits,"trait")
    expected_traits={"carbon_centered_biochemistry","water_solvent_biology","ammonia_rich_biology","hydrocarbon_solvent_biology","silicon_centered_biochemistry","cryogenic_biology","mineral_structural_biology","liquid_medium_native"}
    missing=sorted(expected_traits-trait_ids)
    if missing: fail(f"missing biochemical applicability traits {missing}")
    if len(trait_ids)!=14: fail(f"expected 14 public applicability traits, found {len(trait_ids)}")
    rules=payload["applicability_traits.json"].get("rules",{})
    if rules.get("traits_are_not_species_ids") is not True or rules.get("biochemical_traits_are_composable_dimensions") is not True: fail("biochemical applicability must be species-neutral/composable")
    if rules.get("solvent_and_backbone_traits_are_separate") is not True: fail("solvent and molecular substrate traits must remain separate")

    field_ids=unique(payload["knowledge_fields.json"].get("fields",[]),"knowledge field")
    if len(field_ids)!=36 or "biochemistry" not in field_ids: fail(f"expected 36 fields including biochemistry, found {len(field_ids)}")
    emergence=payload["emergence_model.json"]
    if int(emergence.get("applicability_trait_count",-1))!=14: fail("emergence trait count must be 14")
    if int(emergence.get("pressure_rule_count",-1))!=59: fail("biochemistry expansion must not inflate the global pressure catalog")

    model=payload["biochemical_applicability_model.json"]
    if model.get("research_rules",{}).get("one_civilization_can_support_multiple_biochemical_populations") is not True: fail("multibiochemistry civilizations must be supported")
    if model.get("pressure_rule","").lower().find("reuse existing")<0: fail("biochemistry model must reuse existing causal pressures where adequate")
    pressure_ids=set(payload["pressure_dynamics.json"].get("rules",{}))
    for group,refs in model.get("existing_pressure_mapping",{}).items():
        unknown=sorted(set(refs)-pressure_ids)
        if unknown: fail(f"biochemistry pressure mapping {group} references unknown pressures {unknown}")
    for dimension in model.get("dimensions",{}).values():
        refs=[]
        if dimension.get("generic_trait"): refs.append(dimension["generic_trait"])
        refs.extend(dimension.get("known_public_traits",[]))
        unknown=sorted(set(refs)-trait_ids)
        if unknown: fail(f"biochemistry model references unknown traits {unknown}")

    domain_nodes=payload["alternative_biochemistry.json"].get("nodes",[]); ids=unique(domain_nodes,"alternative-biochemistry node")
    if len(ids)!=30: fail(f"expected 30 alternative-biochemistry nodes, found {len(ids)}")
    all_nodes={}
    for d in domains:
        p=load(root/index["domain_files"][d["id"]])
        for n in p.get("nodes",[]): all_nodes[n["id"]]=n
    evidence_ids=unique(payload["evidence_types.json"].get("evidence_types",[]),"evidence")
    for node in domain_nodes:
        for fid in node.get("knowledge_fields",[]):
            if fid not in field_ids: fail(f"{node['id']}: unknown field {fid}")
        for trait in node.get("applicability",{}).get("requires_traits",[]):
            if trait not in trait_ids: fail(f"{node['id']}: unknown trait {trait}")
        for ev in node.get("applicability",{}).get("requires_evidence",[]):
            if ev not in evidence_ids: fail(f"{node['id']}: unknown evidence {ev}")
        for p in node.get("pressure_affinities",[]):
            if p not in pressure_ids: fail(f"{node['id']}: unknown pressure {p}")
        for mode in ("all_of","any_of"):
            for prereq in node.get("prerequisites",{}).get(mode,[]):
                if prereq not in all_nodes: fail(f"{node['id']}: unknown prerequisite {prereq}")

    ammonia={"ammonia_solvent_homeostasis","ammonia_closed_loop_recycling","ammonia_nutrient_synthesis","ammonia_bioregenerative_support","ammonia_bioindustrial_processes","ammonia_ecosystem_stabilization"}
    hydro={"cryogenic_solvent_homeostasis","cryogenic_membrane_materials","hydrocarbon_nutrient_synthesis","hydrocarbon_closed_ecology","cryogenic_bioreactor_industry","cryogenic_biosphere_management"}
    silicon={"silicon_biochemical_frameworks","silicate_metabolic_cycles","silicon_tissue_repair","silicon_life_support_cycles","silicon_biological_computation"}
    mineral={"mineral_nutrient_processing","mineral_structural_regeneration","mineral_biofabrication"}
    cross={"cross_solvent_biochemistry","xenonutrient_translation","biochemical_interface_engineering","cross_biochemistry_medicine","multibiochemistry_habitat","cross_biochemistry_biofabrication"}
    for nid in ammonia:
        if "ammonia_rich_biology" not in all_nodes[nid]["applicability"]["requires_traits"]: fail(f"{nid}: missing ammonia_rich_biology gate")
    for nid in hydro:
        req=set(all_nodes[nid]["applicability"]["requires_traits"])
        if not {"hydrocarbon_solvent_biology","cryogenic_biology"}.issubset(req): fail(f"{nid}: missing hydrocarbon+cryogenic gates")
    for nid in silicon:
        if "silicon_centered_biochemistry" not in all_nodes[nid]["applicability"]["requires_traits"]: fail(f"{nid}: missing silicon_centered_biochemistry gate")
    for nid in mineral:
        if "mineral_structural_biology" not in all_nodes[nid]["applicability"]["requires_traits"]: fail(f"{nid}: missing mineral_structural_biology gate")
    for nid in cross:
        if "alien_biology" not in all_nodes[nid]["applicability"]["requires_evidence"]: fail(f"{nid}: comparative branch must require alien_biology evidence")

    base_profiles=payload["starting_reference_profiles.json"].get("profiles",[]); base_by={p["id"]:p for p in base_profiles}
    human=set(base_by["reference_humanlike_solar_2050"].get("additional_starting_applicability_traits",[]))
    if not {"carbon_centered_biochemistry","water_solvent_biology"}.issubset(human): fail("human-like reference must explicitly be carbon-water")
    bio_profiles={p["id"]:p for p in payload["starting_biochemical_reference_profiles.json"].get("profiles",[])}
    expected_profiles={"reference_ammonia_rich_early_space","reference_cryogenic_hydrocarbon_early_space","reference_silicon_centered_early_space"}
    if set(bio_profiles)!=expected_profiles: fail(f"unexpected biochemical reference profiles {sorted(bio_profiles)}")
    frag_by={f["id"]:f for f in payload["starting_biochemical_fragments.json"].get("fragments",[])}
    checks={
        "reference_ammonia_rich_early_space":("history_ammonia_rich_biochemistry","ammonia_rich_biology"),
        "reference_cryogenic_hydrocarbon_early_space":("history_cryogenic_hydrocarbon_biochemistry","hydrocarbon_solvent_biology"),
        "reference_silicon_centered_early_space":("history_silicon_centered_biochemistry","silicon_centered_biochemistry")}
    for pid,(fid,trait) in checks.items():
        if fid not in bio_profiles[pid].get("fragment_ids",[]): fail(f"{pid}: missing fragment {fid}")
        if trait not in set(frag_by[fid].get("starting_applicability_traits",[])): fail(f"{fid}: missing trait {trait}")

    pindex=payload["starting_profile_index.json"]
    if pindex.get("counts")!={"history_fragments":15,"reference_profiles":8}: fail(f"starting profile counts must be 15/8, found {pindex.get('counts')}")

    base_fac=payload["research_facility_model.json"]; ext=payload["biochemical_research_facilities.json"]
    base_caps={r["id"] for r in base_fac.get("facility_capabilities",[])}; ext_caps=unique(ext.get("facility_capabilities",[]),"biochemical facility capability")
    all_caps=base_caps|ext_caps
    base_insts={r["id"] for r in base_fac.get("institution_archetypes",[])}; ext_insts=unique(ext.get("institution_archetypes",[]),"biochemical institution")
    if base_insts & ext_insts: fail(f"duplicate institution ids across facility catalogs: {sorted(base_insts & ext_insts)}")
    provider_caps=set()
    for inst in base_fac.get("institution_archetypes",[])+ext.get("institution_archetypes",[]): provider_caps.update(inst.get("facility_capabilities",[]))
    for inst in ext.get("institution_archetypes",[]):
        if inst.get("enabled_by") not in all_nodes: fail(f"institution {inst['id']}: unknown enabling tech {inst.get('enabled_by')}")
        unknown=sorted(set(inst.get("facility_capabilities",[]))-all_caps)
        if unknown: fail(f"institution {inst['id']}: unknown facility capabilities {unknown}")
    for nid,stages in ext.get("stage_requirements",{}).items():
        if nid not in all_nodes: fail(f"facility requirement references unknown node {nid}")
        for stage,req in stages.items():
            for cap in req.get("all_of",[])+req.get("any_of",[]):
                if cap not in provider_caps: fail(f"{nid}/{stage}: no provider for facility capability {cap}")
    findex=payload["research_facility_index.json"]
    if set(findex.get("facility_catalog_files",[]))!={"research_facility_model.json","biochemical_research_facilities.json"}: fail("research facility index must include base and biochemical catalogs")

    print(f"research biochemistry OK: {len(ids)} nodes, {len(trait_ids)} traits, {len(field_ids)} fields, {len(bio_profiles)} biochemical reference starts, {len(ext_insts)} specialist institutions")
    return 0

if __name__=="__main__": raise SystemExit(main())
