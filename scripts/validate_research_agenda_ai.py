#!/usr/bin/env python3
"""Validate Adaptive Research agenda, scientific culture, and fair-information AI planning."""

from __future__ import annotations

import json
import sys
from collections import Counter
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"research-agenda-ai validation failed: {message}")


def load_json(path: Path):
    try:
        with path.open("r", encoding="utf-8") as handle:
            return json.load(handle)
    except Exception as exc:
        fail(f"could not parse {path}: {exc}")


def unique_ids(rows, label: str) -> set[str]:
    ids = [row.get("id") for row in rows]
    if None in ids or "" in ids:
        fail(f"{label} row missing id")
    dupes = [key for key, count in Counter(ids).items() if count > 1]
    if dupes:
        fail(f"duplicate {label} ids: {dupes}")
    return set(ids)


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else "data/research/v1")
    paths = {
        "index": root / "index.json",
        "fields": root / "knowledge_fields.json",
        "pressures": root / "pressure_dynamics.json",
        "capabilities": root / "capability_model.json",
        "agenda": root / "research_agenda_model.json",
        "culture": root / "scientific_culture_model.json",
        "ai": root / "research_ai_planning_contract.json",
        "runtime": root / "research_runtime_contract.json",
        "view": root / "research_view_model_contract.json",
    }
    for path in paths.values():
        if not path.is_file():
            fail(f"missing {path}")

    data = {key: load_json(path) for key, path in paths.items()}
    catalog_id = data["index"].get("catalog_id")
    for label, payload in data.items():
        if payload.get("catalog_id") != catalog_id:
            fail(f"{label}: catalog_id does not match index")

    domain_ids = unique_ids(data["index"].get("domains", []), "domain")
    field_ids = unique_ids(data["fields"].get("fields", []), "knowledge field")
    pressure_ids = set(data["pressures"].get("rules", {}))
    cap_ids = unique_ids(data["capabilities"].get("cross_lineage_capabilities", []), "cross-lineage capability")
    if not domain_ids or not field_ids or not pressure_ids or not cap_ids:
        fail("canonical agenda target catalogs must be nonempty")

    agenda = data["agenda"]
    levels = agenda.get("priority_levels", [])
    level_ids = unique_ids(levels, "research priority level")
    ranks = sorted(row.get("rank") for row in levels)
    if ranks != list(range(len(levels))):
        fail(f"research priority ranks must be contiguous starting at 0, found {ranks}")
    for expected in ("deprioritized", "routine", "important", "strategic", "critical"):
        if expected not in level_ids:
            fail(f"missing core research priority level {expected}")

    player = agenda.get("player_control", {})
    required_player_rules = {
        "can_set_high_level_priorities": True,
        "cannot_select_unknown_technology": True,
        "cannot_force_candidate_visibility": True,
        "cannot_convert_priority_directly_to_rp": True,
        "can_direct_visible_project_if_all_authoritative_requirements_met": True,
    }
    for key, expected in required_player_rules.items():
        if player.get(key) is not expected:
            fail(f"research agenda player_control must set {key}={expected}")

    agenda_perf = agenda.get("performance", {})
    for key in ("sparse_priority_maps", "no_per_tick_full_agenda_recalculation", "event_or_low_frequency_review", "visible_candidate_ranking_only", "background_sampling_uses_field_pressure_indexes_not_full_graph_scan"):
        if agenda_perf.get(key) is not True:
            fail(f"research agenda performance must set {key}=true")

    complacency = agenda.get("complacency_interface", {})
    forbidden_inputs = set(complacency.get("forbidden_inputs", []))
    for required in ("global hidden technology rank", "exact unseen enemy technology", "secret catch-up target"):
        if required not in forbidden_inputs:
            fail(f"complacency interface must explicitly forbid {required!r}")
    if "direct research-speed penalty" not in str(complacency.get("effect", "")):
        fail("complacency interface must explicitly reject direct research-speed penalties")

    capacity_outputs = set(agenda.get("research_capacity_requests", {}).get("outputs", []))
    required_capacity_outputs = {
        "desired_general_effective_lab_capacity",
        "desired_field_specialized_lab_capacity",
        "desired_facility_capability",
        "desired_training_pipeline_for_field_or_lineage",
        "desired_sample_or_experimental_asset",
        "desired_foreign_expert_or_technology_package_access",
    }
    missing = sorted(required_capacity_outputs - capacity_outputs)
    if missing:
        fail(f"research agenda missing capacity-request outputs {missing}")

    culture = data["culture"]
    axes = culture.get("axes", [])
    axis_ids = unique_ids(axes, "scientific culture axis")
    if not 8 <= len(axis_ids) <= 16:
        fail(f"scientific culture should remain a small bounded vector, found {len(axis_ids)} axes")
    required_axes = {
        "curiosity", "risk_tolerance", "institutional_conservatism", "threat_sensitivity",
        "complacency_tendency", "long_term_orientation", "openness", "secrecy",
        "reproducibility_rigor", "portfolio_diversity"
    }
    missing_axes = sorted(required_axes - axis_ids)
    if missing_axes:
        fail(f"scientific culture missing core axes {missing_axes}")
    for row in axes:
        if row.get("range") != [0, 100]:
            fail(f"scientific culture axis {row['id']} must use normalized 0..100 range")

    state_rules = culture.get("state_rules", {})
    if state_rules.get("mutable_over_time") is not True:
        fail("scientific culture must be mutable over time")
    if state_rules.get("no_axis_directly_multiplies_RP") is not True:
        fail("scientific culture axes must not directly multiply RP")
    if state_rules.get("no_axis_reveals_unknown_nodes") is not True:
        fail("scientific culture axes must not reveal unknown nodes")
    if state_rules.get("no_axis_overrides_evidence_prerequisite_applicability_or_facility_rules") is not True:
        fail("scientific culture must not override research requirements")
    if culture.get("performance", {}).get("no_per_tick_personality_simulation") is not True:
        fail("scientific culture must not be simulated per tick")

    natural = culture.get("natural_complacency_example", {})
    not_allowed = set(natural.get("not_allowed", []))
    for token in ("hidden #1 ranking penalty", "forced catch-up bonus for rivals"):
        if token not in not_allowed:
            fail(f"scientific culture complacency example must forbid {token!r}")

    ai = data["ai"]
    allowed = set(ai.get("information_boundary", {}).get("allowed_inputs", []))
    forbidden = set(ai.get("information_boundary", {}).get("forbidden_inputs", []))
    if not allowed or not forbidden:
        fail("AI information boundary must define allowed and forbidden inputs")
    for required in ("unknown Technology Possibility Graph nodes", "exact hidden enemy node state", "global hidden technology ranking"):
        if required not in forbidden:
            fail(f"AI planning must forbid {required!r}")
    if ai.get("information_boundary", {}).get("same_authoritative_blockers_as_player") is not True:
        fail("AI research planning must use the same authoritative blockers as player")

    planning_layer_ids = unique_ids(ai.get("planning_layers", []), "AI research planning layer")
    for required in ("agenda_review", "project_shortlist", "project_selection", "capacity_planning"):
        if required not in planning_layer_ids:
            fail(f"AI research planning missing layer {required}")

    utility_ids = unique_ids(ai.get("visible_candidate_utility_components", []), "AI research utility component")
    for required in ("recognized_need", "strategic_alignment", "capability_gap_value", "readiness", "lab_opportunity_cost", "alternative_coverage", "uncertainty_risk", "long_horizon_value"):
        if required not in utility_ids:
            fail(f"AI research utility missing component {required}")

    scoring = ai.get("scoring_policy", {})
    if scoring.get("no_single_global_fixed_weight_vector") is not True:
        fail("AI research planning must not use one universal fixed utility weight vector")
    if scoring.get("explanation_required") is not True:
        fail("AI research decisions must be explainable")

    shortlist = ai.get("shortlist_policy", {})
    count = shortlist.get("bounded_candidate_count")
    if not isinstance(count, int) or not 1 <= count <= 64:
        fail(f"AI shortlist candidate count must be bounded 1..64, found {count!r}")
    if shortlist.get("never_expand_from_hidden_graph") is not True:
        fail("AI shortlist must never expand from hidden graph")

    dominance = ai.get("dominance_and_catchup", {})
    for key in ("forced_convergence", "hidden_catchup_multiplier", "hidden_leader_penalty"):
        if dominance.get(key) is not False:
            fail(f"AI dominance/catch-up contract must set {key}=false")

    difficulty = ai.get("difficulty_policy", {})
    forbidden_grants = str(difficulty.get("higher_difficulty_does_not_grant", ""))
    for term in ("hidden graph access", "exact enemy technology", "free RP", "free labs", "free evidence"):
        if term not in forbidden_grants:
            fail(f"AI difficulty policy must explicitly exclude {term!r}")

    perf = ai.get("performance", {})
    for key in ("agenda_review_not_per_tick", "shortlist_bounded", "visible_candidates_only", "cache_utility_until_material_input_changes", "dormant_AI_can_use_coarser_review_interval", "no_pairwise_all_civilizations_research_comparison"):
        if perf.get(key) is not True:
            fail(f"AI research performance must set {key}=true")

    runtime = data["runtime"]
    runtime_events = {row.get("id") for row in runtime.get("external_input_events", [])}
    for required in ("research_policy_changed", "scientific_culture_context_changed", "strategic_research_goal_changed"):
        if required not in runtime_events:
            fail(f"research runtime missing agenda/culture input event {required}")
    runtime_queries = {row.get("id") for row in runtime.get("external_queries", [])}
    for required in ("research_agenda_summary", "research_capacity_requests"):
        if required not in runtime_queries:
            fail(f"research runtime missing agenda query {required}")
    if runtime.get("fair_information_contract", {}).get("research_agenda_never_uses_hidden_global_technology_rank") is not True:
        fail("runtime must forbid hidden global technology rank in research agenda")

    view = data["view"]
    projection = view.get("projection_rules", {})
    if projection.get("agenda_never_projects_unknown_targets") is not True:
        fail("research view must not project unknown agenda targets")
    if projection.get("view_does_not_dump_hidden_AI_utility_scores") is not True:
        fail("research view must not expose raw hidden AI utility scores")
    commands = {row.get("id") for row in view.get("command_contract_from_UI_or_AI", [])}
    if "set_research_priority_policy" not in commands:
        fail("research view command contract missing set_research_priority_policy")
    if "ResearchAgendaView" not in view or "ResearchCapacityRequestView" not in view:
        fail("research view must define agenda and capacity-request projections")

    print(
        "research agenda/AI OK: "
        f"{len(level_ids)} priority levels, {len(axis_ids)} scientific-culture axes, "
        f"{len(utility_ids)} AI utility components, shortlist={count}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
