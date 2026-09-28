#!/usr/bin/env python3
"""Validate distributed Adaptive Research knowledge/continuity contracts."""

from __future__ import annotations

import json
import sys
from collections import Counter
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"research-distributed-continuity validation failed: {message}")


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
        "pressure": root / "pressure_dynamics.json",
        "tacit": root / "tacit_knowledge_model.json",
        "competence": root / "research_competence_model.json",
        "runtime": root / "research_runtime_contract.json",
        "model": root / "distributed_research_continuity_model.json",
        "extension": root / "distributed_research_runtime_extension.json",
        "scenarios": root / "distributed_research_benchmark_scenarios.json",
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
    context = model.get("research_context", {})
    if context.get("not_one_per_colony") is not True:
        fail("research contexts must not be one-per-colony")
    creation_rule = context.get("creation_rule", "").lower()
    if "persistent divergence" not in creation_rule:
        fail("context creation rule must require material persistent divergence")

    perf = model.get("performance", {})
    required_perf = [
        "no_full_graph_copy_per_context",
        "no_context_for_normal_synchronized_colony",
        "knowledge_access_stored_as_exceptions",
        "field_practice_stored_as_exceptions",
        "pending_transmissions_bounded_and_mergeable_by_destination_revision",
        "delivered_transmissions_removed_after_state_application",
        "old_context_events_compressed",
        "obsolete_contexts_merge_or_archive",
        "low_frequency_continuity_maintenance",
    ]
    for key in required_perf:
        if perf.get(key) is not True:
            fail(f"performance guardrail {key}=true is required")

    forbidden = " ".join(model.get("context_state", {}).get("forbidden_full_copies", [])).lower()
    for concept in ("complete", "node-state", "competence", "per-scientist"):
        if concept not in forbidden:
            fail(f"context forbidden-copy rules should explicitly cover {concept}")

    states = [row.get("id") for row in model.get("knowledge_access_states", [])]
    unique(states, "knowledge access state")
    if states != ["absent", "reference_only", "codified", "practiced"]:
        fail(f"knowledge access states changed unexpectedly: {states}")

    dissemination_rules = " ".join(model.get("knowledge_dissemination", {}).get("rules", [])).lower()
    for required in ("do not teleport", "never creates", "no universal distance research penalty"):
        if required not in dissemination_rules:
            fail(f"knowledge dissemination rules missing guardrail phrase {required!r}")

    pressure_ids = set(data["pressure"].get("rules", {}))
    for pressure_id in model.get("isolation_and_reconnection", {}).get("relevant_existing_pressures", []):
        if pressure_id not in pressure_ids:
            fail(f"unknown continuity pressure reference {pressure_id}")

    tacit_ids = {row.get("id") for row in data["tacit"].get("knowledge_asset_types", [])}
    required_tacit = {
        "codified_records",
        "experimental_dataset",
        "experimental_protocols",
        "intact_prototype",
        "manufacturing_tooling",
        "expert_cohort",
        "operating_institution",
        "training_pipeline",
    }
    if not required_tacit.issubset(tacit_ids):
        fail(f"tacit model missing continuity asset types: {sorted(required_tacit - tacit_ids)}")

    field_model = model.get("field_practice", {})
    if field_model.get("base_model") != "research_competence_model.json":
        fail("regional field practice must extend canonical competence model")
    if "sparse" not in field_model.get("regional_representation", "").lower():
        fail("regional field practice must be sparse")

    fracture = " ".join(model.get("political_fracture_and_successors", {}).get("inheritance_rules", [])).lower()
    for guardrail in (
        "does not automatically receive every technology",
        "follow their real location",
        "overlap heavily",
        "diverge sharply",
    ):
        if guardrail not in fracture:
            fail(f"successor inheritance rules missing {guardrail!r}")

    fair = model.get("fair_information", {})
    for key in (
        "player_and_AI_same_access_rules",
        "AI_cannot_use_unsynchronized_core_knowledge_in_isolated_context",
        "successor_AI_receives_only_inherited_research_state",
        "enemy_or_foreign_context_state_requires_legitimate_intelligence",
    ):
        if fair.get(key) is not True:
            fail(f"fair-information guardrail {key}=true is required")

    extension = data["extension"]
    if extension.get("base_runtime_contract") != "research_runtime_contract.json":
        fail("distributed runtime extension must explicitly extend research_runtime_contract.json")
    if extension.get("continuity_model") != "distributed_research_continuity_model.json":
        fail("runtime extension must reference distributed continuity model")

    event_ids = [row.get("id") for row in extension.get("external_input_events", [])]
    query_ids = [row.get("id") for row in extension.get("external_queries", [])]
    unique(event_ids, "distributed input event")
    unique(query_ids, "distributed query")
    required_events = {
        "research_communication_path_changed",
        "research_information_policy_changed",
        "research_archive_replica_changed",
        "research_asset_location_changed",
        "research_context_polity_changed",
        "research_context_topology_changed",
        "external_science_sharing_changed",
    }
    required_queries = {
        "context_knowledge_access",
        "context_can_apply_capability",
        "context_field_practice",
        "pending_knowledge_delivery",
        "successor_research_inheritance_preview",
        "research_context_divergence_summary",
    }
    if set(event_ids) != required_events:
        fail(f"distributed input event contract mismatch: {sorted(set(event_ids))}")
    if set(query_ids) != required_queries:
        fail(f"distributed query contract mismatch: {sorted(set(query_ids))}")

    save = extension.get("save_contract", {})
    for key in (
        "store_only_materially_divergent_contexts",
        "store_only_node_access_exceptions",
        "store_only_field_practice_exceptions",
        "pending_transmissions_deduplicate_by_destination_and_revision",
        "delivered_transmissions_removed_after_application",
        "archived_context_history_compressed",
        "no_static_graph_copy",
        "no_full_context_node_map",
    ):
        if save.get(key) is not True:
            fail(f"distributed save guardrail {key}=true is required")

    successor_forbidden = " ".join(extension.get("successor_materialization", {}).get("forbidden_shortcuts", [])).lower()
    for concept in ("complete known-node", "complete competence", "all former capabilities", "automatically"):
        if concept not in successor_forbidden:
            fail(f"successor forbidden shortcuts missing {concept!r}")

    ext_fair = extension.get("fair_information", {})
    for key in (
        "AI_uses_same_context_access_and_delivery_state",
        "isolated_AI_context_cannot_read_unsynchronized_core_discovery",
        "successor_AI_gets_only_materialized_inherited_state",
        "foreign_context_access_requires_legitimate_intelligence",
    ):
        if ext_fair.get(key) is not True:
            fail(f"runtime extension fair-information guardrail {key}=true is required")

    # Validate benchmark node references against the current public catalog.
    all_nodes = []
    for domain in data["index"].get("domains", []):
        filename = data["index"].get("domain_files", {}).get(domain.get("id"))
        if not filename:
            fail(f"missing domain file mapping for {domain.get('id')}")
        all_nodes.extend(load(root / filename).get("nodes", []))
    node_ids = {row.get("id") for row in all_nodes}
    if len(node_ids) != data["index"].get("node_count"):
        fail("catalog node count mismatch while validating continuity scenarios")

    scenario_rows = data["scenarios"].get("scenarios", [])
    scenario_ids = [row.get("id") for row in scenario_rows]
    unique(scenario_ids, "continuity benchmark scenario")
    required_scenarios = {
        "communication_latency_delivery",
        "forty_year_partition_and_reintegration",
        "successor_state_asymmetric_inheritance",
        "isolated_archive_catastrophe_and_recovery",
        "distributed_context_state_soak_1000y",
    }
    if set(scenario_ids) != required_scenarios:
        fail(f"continuity scenario set mismatch: {sorted(set(scenario_ids))}")

    node_keys = (
        "node_id",
        "new_core_nodes_missed_during_partition",
        "common_widely_replicated_nodes",
        "local_specialist_nodes",
        "former_polity_recent_global_nodes_not_present_in_either_local_archive",
        "affected_nodes",
    )

    def check_node_value(value, label):
        values = value if isinstance(value, list) else [value]
        for node_id in values:
            if node_id not in node_ids:
                fail(f"{label}: unknown benchmark node {node_id!r}")

    for scenario in scenario_rows:
        sid = scenario["id"]
        for key in node_keys:
            if key in scenario:
                check_node_value(scenario[key], f"scenario {sid}.{key}")
        for subkey in ("successor_a", "successor_b"):
            if subkey in scenario:
                check_node_value(scenario[subkey].get("local_specialist_nodes", []), f"scenario {sid}.{subkey}")
        if not scenario.get("assertions") and sid != "distributed_context_state_soak_1000y":
            fail(f"scenario {sid}: assertions required")

    print(
        "research distributed continuity OK: "
        f"{len(states)} access states, {len(event_ids)} input events, {len(query_ids)} queries, "
        f"{len(scenario_rows)} benchmark scenarios"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
