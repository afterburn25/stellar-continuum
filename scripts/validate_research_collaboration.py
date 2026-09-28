#!/usr/bin/env python3
"""Validate Adaptive Research cross-polity collaboration contracts."""

from __future__ import annotations

import json
import sys
from collections import Counter
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"research-collaboration validation failed: {message}")


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
        "model": root / "research_collaboration_model.json",
        "runtime": root / "research_collaboration_runtime_extension.json",
        "economy": root / "research_economy.json",
        "capacity": root / "research_capacity.json",
        "facility": root / "research_facility_model.json",
        "competence": root / "research_competence_model.json",
        "exchange": root / "technology_exchange_model.json",
        "foreign": root / "foreign_technology_model.json",
        "distributed": root / "distributed_research_continuity_model.json",
        "secrecy": root / "research_secrecy_model.json",
        "biochemistry": root / "biochemical_applicability_model.json",
        "scenarios": root / "research_collaboration_benchmark_scenarios.json",
    }
    for path in paths.values():
        if not path.is_file():
            fail(f"missing {path}")
    data = {name: load(path) for name, path in paths.items()}
    catalog_id = data["index"].get("catalog_id")
    for name, payload in data.items():
        if payload.get("catalog_id") != catalog_id:
            fail(f"{name}: catalog_id mismatch")

    model = data["model"]
    core = model.get("core_rules", {})
    for key in (
        "agreement_does_not_generate_rp",
        "agreement_does_not_multiply_research_speed",
        "participant_technology_trees_never_merge",
        "unknown_partner_nodes_are_not_revealed_by_treaty_existence",
        "every_scientific_contribution_has_a_real_source",
        "contributions_can_be_unequal",
        "participant_withdrawal_removes_future_contribution_not_already_delivered_records",
        "results_follow_access_rights_and_actual_delivery",
        "joint_result_does_not_guarantee_operability_or_reproduction_for_every_participant",
        "participant_competence_grows_only_from_actual_participation",
    ):
        if core.get(key) is not True:
            fail(f"core collaboration guardrail {key}=true is required")

    collab_types = [row.get("id") for row in model.get("collaboration_types", [])]
    unique(collab_types, "collaboration type")
    expected_types = {
        "joint_directed_project",
        "shared_observation_program",
        "shared_facility_program",
        "expert_exchange_program",
        "joint_foreign_technology_study",
    }
    if set(collab_types) != expected_types:
        fail(f"collaboration type contract mismatch: {sorted(set(collab_types))}")

    if model.get("directed_program_capacity", {}).get("base_model") != "research_capacity.json":
        fail("joint directed programs must use canonical research capacity model")
    if model.get("directed_program_capacity", {}).get("no_collaboration_slot_bypass") is not True:
        fail("collaboration must not bypass directed-program capacity")
    if model.get("directed_program_capacity", {}).get("no_treaty_parallelism_bonus") is not True:
        fail("treaty must not grant research parallelism")

    lab = model.get("lab_and_facility_contribution", {})
    if lab.get("no_free_partner_capacity") is not True:
        fail("partner capacity must come from real assets")
    if "same canonical diminishing-return model" not in lab.get("diminishing_returns", ""):
        fail("joint project must use canonical diminishing-return model")

    coordination = model.get("coordination", {})
    if coordination.get("no_generic_coordination_tax") is not True:
        fail("joint research must not use generic coordination tax")
    if coordination.get("distributed_model") != "distributed_research_continuity_model.json":
        fail("collaboration must integrate distributed continuity")
    if coordination.get("secrecy_model") != "research_secrecy_model.json":
        fail("collaboration must integrate secrecy model")

    progress = model.get("joint_progress", {})
    if progress.get("same_asset_not_double_counted") is not True:
        fail("same contribution asset must not be double counted")
    if progress.get("no_duplicate_rp_for_multi_party_labels") is not True:
        fail("multi-party labels must not duplicate RP")
    if progress.get("research_pressure_not_a_speed_multiplier") is not True:
        fail("Research Pressure remains non-speed input in joint projects")

    competence = model.get("participation_competence", {})
    if competence.get("base_model") != "research_competence_model.json":
        fail("joint participation competence must extend canonical competence model")
    if "does not create" not in competence.get("rule", "").lower():
        fail("passive membership/payment/result receipt must not create practice competence")

    results = model.get("result_access_policy", {})
    if results.get("base_exchange_model") != "technology_exchange_model.json":
        fail("joint result access must reuse technology exchange model")
    if results.get("license_is_law_not_physics") is not True:
        fail("joint result license must remain law not physics")

    assimilation = model.get("native_result_assimilation", {})
    if "co-developed" not in assimilation.get("joint_developer_rule", "").lower():
        fail("native joint result must be co-development, not free grant")
    if "not automatically mature" not in assimilation.get("passive_participant_rule", "").lower():
        fail("passive participant must not automatically mature joint result")
    if "no collaboration result automatically creates" not in assimilation.get("physical_deployment_rule", "").lower():
        fail("joint result must not instantiate physical deployment")

    withdrawal = model.get("withdrawal_and_suspension", {})
    if "remain" not in withdrawal.get("already_delivered_records", "").lower():
        fail("withdrawal must not recall delivered records")
    if "owned elsewhere" not in withdrawal.get("rights_consequences", "").lower():
        fail("diplomatic consequences must remain outside Adaptive Research")

    fair = model.get("fair_information", {})
    for key in (
        "AI_and_player_same_collaboration_rules",
        "AI_evaluates_only_known_partner_capabilities_and_offered_assets",
        "partner_hidden_nodes_not_visible_without_disclosure_or_evidence",
        "AI_does_not_assume_undisclosed_partner_facilities_or_expertise",
        "harder_AI_gets_no_free_joint_RP_or_hidden_partner_knowledge",
    ):
        if fair.get(key) is not True:
            fail(f"collaboration fair-information guardrail {key}=true required")

    perf = model.get("performance", {})
    for key in (
        "only_active_or_strategically_relevant_collaborations_persist_in_detail",
        "participant_contributions_use_aggregate_capacity_and_asset_refs",
        "no_per_scientist_state",
        "no_per_partner_graph_copy",
        "completed_collaboration_history_compressed",
        "delivered_result_transfer_records_compact_into_normal_knowledge_asset_state",
        "inactive_or_terminated_collaboration_records_archive",
        "coordination_reassessment_event_driven",
    ):
        if perf.get(key) is not True:
            fail(f"collaboration performance guardrail {key}=true required")

    runtime = data["runtime"]
    if runtime.get("base_runtime_contract") != "research_runtime_contract.json":
        fail("collaboration runtime extension must extend research_runtime_contract.json")
    if runtime.get("collaboration_model") != "research_collaboration_model.json":
        fail("runtime extension must reference collaboration model")
    event_ids = [row.get("id") for row in runtime.get("external_input_events", [])]
    query_ids = [row.get("id") for row in runtime.get("external_queries", [])]
    unique(event_ids, "collaboration runtime event")
    unique(query_ids, "collaboration runtime query")
    expected_events = {
        "research_collaboration_agreement_changed",
        "research_collaboration_contribution_changed",
        "research_collaboration_communication_changed",
        "research_collaboration_security_changed",
        "research_collaboration_participant_withdrew",
        "research_collaboration_result_rights_changed",
        "research_collaboration_foreign_asset_changed",
    }
    expected_queries = {
        "collaboration_contribution_eligibility",
        "collaboration_effective_capacity",
        "collaboration_participant_status",
        "collaboration_result_preview",
        "collaboration_withdrawal_impact",
        "collaboration_partner_scientific_utility",
    }
    if set(event_ids) != expected_events:
        fail(f"collaboration event contract mismatch: {sorted(set(event_ids))}")
    if set(query_ids) != expected_queries:
        fail(f"collaboration query contract mismatch: {sorted(set(query_ids))}")

    if runtime.get("withdrawal", {}).get("remove_only_actual_future_contributions") is not True:
        fail("runtime withdrawal must remove only future contributions")
    if runtime.get("withdrawal", {}).get("do_not_recall_delivered_records") is not True:
        fail("runtime withdrawal must not recall delivered records")
    if runtime.get("withdrawal", {}).get("do_not_reverse_completed_scientific_work") is not True:
        fail("runtime withdrawal must not reverse completed science")

    save = runtime.get("save_contract", {})
    for key in (
        "only_active_or_strategically_relevant_collaborations_detailed",
        "aggregate_contribution_records",
        "physical_assets_referenced_not_duplicated",
        "no_partner_graph_copies",
        "no_per_scientist_state",
        "pending_result_deliveries_deduplicate_by_participant_and_revision",
        "delivered_result_records_removed_after_application",
        "completed_collaboration_history_compressed",
        "terminated_inactive_records_archive",
    ):
        if save.get(key) is not True:
            fail(f"collaboration save guardrail {key}=true required")

    # Load current public nodes and canonical facility requirements.
    nodes = {}
    for domain in data["index"].get("domains", []):
        filename = data["index"].get("domain_files", {}).get(domain["id"])
        if not filename:
            fail(f"missing domain file mapping for {domain['id']}")
        for node in load(root / filename).get("nodes", []):
            nodes[node["id"]] = node
    if len(nodes) != data["index"].get("node_count"):
        fail("catalog node count mismatch")

    facility_caps = {row["id"] for row in data["facility"].get("facility_capabilities", [])}
    stage_requirements = data["facility"].get("stage_requirements", {})
    access_classes = {row["id"] for row in data["secrecy"].get("access_classes", [])}
    foreign_states = {
        "understanding": {row["id"] for row in data["foreign"].get("understanding_axis", [])},
        "operability": {row["id"] for row in data["foreign"].get("operability_axis", [])},
        "reproduction": {row["id"] for row in data["foreign"].get("reproduction_axis", [])},
    }

    scenario_rows = data["scenarios"].get("scenarios", [])
    scenario_ids = [row.get("id") for row in scenario_rows]
    unique(scenario_ids, "collaboration benchmark scenario")
    expected_scenarios = {
        "real_labs_no_treaty_multiplier",
        "participant_withdrawal_removes_real_capacity",
        "facility_withdrawal_blocks_stage",
        "asymmetric_result_usability",
        "classified_joint_compartments",
        "communication_partition_pauses_dependency_not_whole_treaty",
        "research_collaboration_state_soak_1000y",
    }
    if set(scenario_ids) != expected_scenarios:
        fail(f"collaboration scenario set mismatch: {sorted(set(scenario_ids))}")

    for s in scenario_rows:
        sid = s["id"]
        if s.get("node_id") and s["node_id"] not in nodes:
            fail(f"scenario {sid}: unknown node {s['node_id']}")
        if s.get("access_class") and s["access_class"] not in access_classes:
            fail(f"scenario {sid}: unknown access class {s['access_class']}")
        if s.get("collaboration_type") and s["collaboration_type"] not in set(collab_types):
            fail(f"scenario {sid}: unknown collaboration type {s['collaboration_type']}")
        cap = s.get("required_facility_capability")
        if cap and cap not in facility_caps:
            fail(f"scenario {sid}: unknown facility capability {cap}")
        if sid == "facility_withdrawal_blocks_stage":
            node_req = stage_requirements.get(s["node_id"], {}).get(s.get("stage"), {}).get("all_of", [])
            if cap not in node_req:
                fail(f"scenario {sid}: {cap} is not a canonical hard requirement for {s['node_id']} {s.get('stage')}")
        for key, axis in (
            ("participant_a_understanding", "understanding"),
            ("participant_b_understanding", "understanding"),
            ("participant_a_operability", "operability"),
            ("participant_b_operability", "operability"),
            ("participant_a_reproduction", "reproduction"),
            ("participant_b_reproduction", "reproduction"),
        ):
            if key in s and s[key] not in foreign_states[axis]:
                fail(f"scenario {sid}: invalid {axis} state {s[key]!r}")
        if not s.get("assertions") and sid != "research_collaboration_state_soak_1000y":
            fail(f"scenario {sid}: assertions required")

    # Benchmark seed lab values must match canonical economy seed.
    lab_scenario = next(row for row in scenario_rows if row["id"] == "real_labs_no_treaty_multiplier")
    canonical_base = data["economy"].get("research_model", {}).get("research_points", {}).get("base_rp_per_effective_lab_per_year")
    if lab_scenario["base_rp_per_effective_lab_per_year"] != canonical_base:
        fail("joint lab benchmark base RP does not match research_economy.json")

    print(
        "research collaboration OK: "
        f"{len(collab_types)} collaboration types, {len(event_ids)} input events, {len(query_ids)} queries, "
        f"{len(scenario_rows)} benchmark scenarios"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
