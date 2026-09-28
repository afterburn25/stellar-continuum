#!/usr/bin/env python3
"""Validate Adaptive Research secrecy/classification/compartmentalization contracts."""

from __future__ import annotations

import json
import sys
from collections import Counter
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"research-secrecy validation failed: {message}")


def load(path: Path):
    try:
        with path.open("r", encoding="utf-8") as handle:
            return json.load(handle)
    except Exception as exc:
        fail(f"could not parse {path}: {exc}")


def unique(values, label: str) -> None:
    dupes = [value for value, count in Counter(values).items() if count > 1]
    if dupes:
        fail(f"duplicate {label}: {dupes}")


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else "data/research/v1")
    paths = {
        "index": root / "index.json",
        "secrecy": root / "research_secrecy_model.json",
        "runtime": root / "research_secrecy_runtime_extension.json",
        "distributed": root / "distributed_research_continuity_model.json",
        "foreign": root / "foreign_technology_model.json",
        "exchange": root / "technology_exchange_model.json",
        "tacit": root / "tacit_knowledge_model.json",
        "scenarios": root / "research_secrecy_benchmark_scenarios.json",
    }
    for path in paths.values():
        if not path.is_file():
            fail(f"missing {path}")
    data = {name: load(path) for name, path in paths.items()}
    catalog_id = data["index"].get("catalog_id")
    for name, payload in data.items():
        if payload.get("catalog_id") != catalog_id:
            fail(f"{name}: catalog_id mismatch")

    secrecy = data["secrecy"]
    core = secrecy.get("core_rules", {})
    for key in (
        "classification_attaches_to_records_projects_and_assets_not_physics",
        "scientific_maturity_unchanged_by_classification",
        "classification_does_not_make_deployed_capability_unobservable",
        "classification_does_not_directly_modify_rp_generation_or_project_cost",
        "classification_can_limit_eligible_participants_and_dissemination",
        "legal_or_policy_restrictions_are_not_physical_force_fields",
        "actual_protection_comes_from_real_security_controls",
        "compromise_requires_factual_external_event_or_known_leak_not_hidden_assumption",
    ):
        if core.get(key) is not True:
            fail(f"core secrecy guardrail {key}=true is required")

    access_rows = secrecy.get("access_classes", [])
    access_ids = [row.get("id") for row in access_rows]
    unique(access_ids, "access class")
    if access_ids != ["normal_scientific","restricted_program","classified","special_access_compartment"]:
        fail(f"access class contract changed unexpectedly: {access_ids}")
    defaults = [row["id"] for row in access_rows if row.get("default") is True]
    if defaults != ["normal_scientific"]:
        fail(f"normal_scientific must be the sole default access class, got {defaults}")

    existence_ids = [row.get("id") for row in secrecy.get("existence_disclosure_states", [])]
    unique(existence_ids, "existence-disclosure state")
    if existence_ids != ["acknowledged","restricted_metadata","concealed_metadata"]:
        fail(f"existence-disclosure state contract changed: {existence_ids}")

    external_release_ids = [row.get("id") for row in secrecy.get("external_release_policy", [])]
    unique(external_release_ids, "external-release policy")
    if external_release_ids != ["none","summary_only","scientific_records","full_authorized_package"]:
        fail(f"external release policy contract changed: {external_release_ids}")

    record_scope = set(secrecy.get("record_scope", {}).get("possible_targets", []))
    required_record_targets = {
        "active_research_project",
        "node_research_revision",
        "experimental_dataset",
        "engineering_blueprints",
        "manufacturing_process_records",
    }
    if not required_record_targets.issubset(record_scope):
        fail(f"record scope missing {sorted(required_record_targets - record_scope)}")
    if secrecy.get("record_scope", {}).get("rule", "").lower().find("do not create one classification row for every normal research node") < 0:
        fail("record-scope rule must require sparse nondefault classification")

    policy = secrecy.get("security_policy_record", {})
    if policy.get("sparse_only_when_nondefault") is not True:
        fail("security policy records must be sparse/nondefault only")

    compartments = secrecy.get("compartment_model", {})
    if "do not split" not in compartments.get("rule", "").lower():
        fail("compartment model must not split scientific truth")
    if "block" not in compartments.get("cross_compartment_integration", "").lower():
        fail("missing-compartment integration must be a concrete blocker")

    project = secrecy.get("research_project_effects", {})
    if project.get("no_direct_percent_penalty") is not True:
        fail("classification must not have a direct percent research penalty")
    effects_text = " ".join(project.get("actual_effects", [])).lower()
    for phrase in ("only authorized effective research labs", "only authorized expert", "only authorized facilities"):
        if phrase not in effects_text:
            fail(f"project security effects missing {phrase!r}")
    if "not an arbitrary secrecy multiplier" not in project.get("rule", "").lower():
        fail("project secrecy slowdown must be causal, not a multiplier")

    compromise = secrecy.get("compromise_model", {})
    compromise_states = compromise.get("known_states", [])
    if compromise_states != ["no_known_compromise","suspected","confirmed_partial","confirmed_material","scope_unknown"]:
        fail(f"compromise-state contract changed: {compromise_states}")
    compromise_effects = " ".join(compromise.get("research_effects", [])).lower()
    if "never directly set the recipient's native node mature" not in compromise_effects:
        fail("compromise must never directly mature a recipient native node")
    if "never tell the owner who has the data" not in compromise_effects:
        fail("owner compromise knowledge must not be omniscient")

    observation = secrecy.get("observation_vs_records", {})
    if "legitimate_foreign_capability_observed" not in observation.get("rule", ""):
        fail("classified capability observation must reuse legitimate foreign-capability path")
    if "cannot suppress" not in observation.get("secrecy_limit", "").lower():
        fail("classification must not suppress physical observables")

    declass = secrecy.get("declassification_and_reclassification", {})
    if "does not change research maturity" not in declass.get("declassification_rule", "").lower():
        fail("declassification must not change maturity")
    if "cannot erase copies already" not in declass.get("reclassification_rule", "").lower():
        fail("reclassification must not magically recall copies")
    if "cannot un-leak" not in declass.get("compromise_persistence", "").lower():
        fail("reclassification cannot reverse existing compromise")

    distributed = secrecy.get("distributed_research_interface", {})
    if distributed.get("base_model") != "distributed_research_continuity_model.json":
        fail("secrecy model must extend distributed continuity model")
    distributed_text = " ".join(distributed.get("rules", [])).lower()
    for phrase in ("access exceptions", "obey real communications latency", "does not delete existing context copies"):
        if phrase not in distributed_text:
            fail(f"distributed secrecy interface missing {phrase!r}")

    exchange = secrecy.get("technology_exchange_interface", {})
    if exchange.get("base_model") != "technology_exchange_model.json":
        fail("secrecy model must reuse technology exchange model")
    if exchange.get("external_release_is_not_automatic_license") is not True:
        fail("external release must not automatically imply a technology license")

    perf = secrecy.get("performance", {})
    for key in (
        "default_normal_scientific_access_not_stored_per_node",
        "only_nondefault_security_records_persist",
        "compartment_membership_uses_group_refs_not_per_person_lists",
        "compromise_history_compressed_after_strategic_relevance_fades",
        "declassified_redundant_records_merge_into_normal_access_state",
        "no_full_graph_security_copy",
        "security_reassessment_event_driven",
    ):
        if perf.get(key) is not True:
            fail(f"secrecy performance guardrail {key}=true is required")

    fair = secrecy.get("fair_information", {})
    for key in (
        "AI_and_player_same_classification_access_rules",
        "AI_does_not_know_undetected_compromise",
        "foreign_classified_programs_require_legitimate_evidence_or_intelligence",
        "observed_capability_does_not_reveal_hidden_implementation",
        "harder_AI_receives_no_secret_program_omniscience",
    ):
        if fair.get(key) is not True:
            fail(f"secrecy fair-information guardrail {key}=true is required")

    runtime = data["runtime"]
    if runtime.get("base_runtime_contract") != "research_runtime_contract.json":
        fail("secrecy runtime extension must extend research_runtime_contract.json")
    if runtime.get("distributed_continuity_model") != "distributed_research_continuity_model.json":
        fail("secrecy runtime extension must integrate distributed continuity")
    if runtime.get("secrecy_model") != "research_secrecy_model.json":
        fail("runtime extension must reference research_secrecy_model.json")

    event_ids = [row.get("id") for row in runtime.get("external_input_events", [])]
    query_ids = [row.get("id") for row in runtime.get("external_queries", [])]
    unique(event_ids, "secrecy runtime input event")
    unique(query_ids, "secrecy runtime query")
    required_events = {
        "research_security_policy_changed",
        "research_security_service_changed",
        "research_compromise_reported",
        "research_records_acquired",
        "classified_research_asset_control_changed",
        "classified_capability_observed",
        "research_declassification_triggered",
    }
    required_queries = {
        "research_access_authorized",
        "classified_project_eligible_capacity",
        "research_security_summary",
        "research_compromise_assessment",
        "declassification_impact_preview",
        "foreign_protected_technology_assessment",
    }
    if set(event_ids) != required_events:
        fail(f"secrecy event contract mismatch: {sorted(set(event_ids))}")
    if set(query_ids) != required_queries:
        fail(f"secrecy query contract mismatch: {sorted(set(query_ids))}")

    runtime_project = runtime.get("project_integration", {})
    if runtime_project.get("no_direct_secrecy_multiplier") is not True:
        fail("runtime project integration must forbid direct secrecy multiplier")
    if "block" not in runtime_project.get("compartment_integration", "").lower():
        fail("runtime compartment integration must use a concrete blocker")

    save = runtime.get("save_contract", {})
    for key in (
        "default_normal_policy_not_serialized_per_node",
        "only_nondefault_security_records_persist",
        "compartment_membership_group_refs_only",
        "no_per_person_clearance_lists",
        "known_compromise_history_compressed",
        "declassified_redundant_policy_records_removed",
        "no_full_graph_security_state",
        "physical_asset_refs_not_duplicated",
    ):
        if save.get(key) is not True:
            fail(f"secrecy save guardrail {key}=true is required")

    runtime_fair = runtime.get("fair_information", {})
    for key in (
        "AI_and_player_use_same_authorization_rules",
        "AI_does_not_know_undetected_leaks",
        "AI_does_not_see_foreign_classification_metadata_without_intelligence",
        "observed_foreign_capability_does_not_reveal_implementation",
        "harder_AI_gets_no_free_access_to_classified_records",
    ):
        if runtime_fair.get(key) is not True:
            fail(f"runtime secrecy fair-information guardrail {key}=true is required")

    # Canonical foreign assessment axis states remain the recipient-side vocabulary.
    foreign = data["foreign"]
    foreign_states = {
        "understanding": {row["id"] for row in foreign.get("understanding_axis", [])},
        "operability": {row["id"] for row in foreign.get("operability_axis", [])},
        "reproduction": {row["id"] for row in foreign.get("reproduction_axis", [])},
    }

    # Validate benchmark node/asset references.
    node_ids = set()
    for domain in data["index"].get("domains", []):
        filename = data["index"].get("domain_files", {}).get(domain["id"])
        if not filename:
            fail(f"missing domain file mapping for {domain['id']}")
        node_ids.update(node["id"] for node in load(root / filename).get("nodes", []))
    tacit_ids = {row["id"] for row in data["tacit"].get("knowledge_asset_types", [])}
    record_asset_ids = record_scope | tacit_ids

    scenario_rows = data["scenarios"].get("scenarios", [])
    scenario_ids = [row.get("id") for row in scenario_rows]
    unique(scenario_ids, "secrecy benchmark scenario")
    expected_scenarios = {
        "restricted_capacity_no_magic_penalty",
        "compartment_integration_blocker",
        "classified_deployed_capability_observed",
        "partial_blueprint_compromise",
        "declassification_increases_resilience_not_truth",
        "reclassification_cannot_recall_existing_copies",
        "research_security_state_soak_1000y",
    }
    if set(scenario_ids) != expected_scenarios:
        fail(f"secrecy benchmark scenario set mismatch: {sorted(set(scenario_ids))}")
    for row in scenario_rows:
        sid = row["id"]
        node_id = row.get("node_id")
        if node_id is not None and node_id not in node_ids:
            fail(f"scenario {sid}: unknown node {node_id}")
        access = row.get("access_class")
        if access is not None and access not in set(access_ids):
            fail(f"scenario {sid}: unknown access class {access}")
        existence = row.get("existence_disclosure")
        if existence is not None and existence not in set(existence_ids):
            fail(f"scenario {sid}: unknown existence-disclosure state {existence}")
        for key in ("acquired_assets","not_acquired_assets"):
            for asset_id in row.get(key, []):
                if asset_id not in record_asset_ids:
                    fail(f"scenario {sid}: unknown record/tacit asset {asset_id!r}")
        if "foreign_understanding_before" in row and row["foreign_understanding_before"] not in foreign_states["understanding"]:
            fail(f"scenario {sid}: invalid foreign understanding state before")
        if "foreign_understanding_after" in row and row["foreign_understanding_after"] not in foreign_states["understanding"]:
            fail(f"scenario {sid}: invalid foreign understanding state after")
        if "foreign_reproduction_after" in row and row["foreign_reproduction_after"] not in foreign_states["reproduction"]:
            fail(f"scenario {sid}: invalid foreign reproduction state")
        if "foreign_operability_after" in row and row["foreign_operability_after"] not in foreign_states["operability"]:
            fail(f"scenario {sid}: invalid foreign operability state")
        if not row.get("assertions") and sid != "research_security_state_soak_1000y":
            fail(f"scenario {sid}: assertions required")

    print(
        "research secrecy OK: "
        f"{len(access_ids)} access classes, {len(existence_ids)} existence states, "
        f"{len(event_ids)} input events, {len(query_ids)} queries, {len(scenario_rows)} benchmark scenarios"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
