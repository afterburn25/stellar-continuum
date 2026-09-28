#!/usr/bin/env python3
"""Deterministic offline benchmarks for Adaptive Research secrecy/compartmentalization."""

from __future__ import annotations

import json
import sys
from collections import deque
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"research-secrecy benchmark failed: {message}")


def load(path: Path):
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else "data/research/v1")
    scenarios = load(root / "research_secrecy_benchmark_scenarios.json")["scenarios"]
    by_id = {row["id"]: row for row in scenarios}
    index = load(root / "index.json")
    node_ids = []
    for domain in index["domains"]:
        node_ids.extend(node["id"] for node in load(root / index["domain_files"][domain["id"]])["nodes"])
    node_ids = sorted(set(node_ids))
    if len(node_ids) != index["node_count"]:
        fail("catalog could not be loaded consistently")

    results = {}

    # 1) Classified capacity uses real authorized labs, not a hidden multiplier.
    s = by_id["restricted_capacity_no_magic_penalty"]
    if s["total_scientifically_eligible_labs"] - s["security_authorized_labs"] != s["unauthorized_labs_excluded"]:
        fail("restricted-capacity fixture arithmetic mismatch")
    if s["direct_secrecy_rp_multiplier"] != 1.0:
        fail("classification must not apply a direct RP multiplier")
    if s["security_authorized_labs"] <= 0 or s["security_authorized_labs"] >= s["total_scientifically_eligible_labs"]:
        fail("restricted-capacity fixture must demonstrate a smaller real authorized pool")
    results[s["id"]] = {
        "total_scientifically_eligible_labs": s["total_scientifically_eligible_labs"],
        "authorized_labs": s["security_authorized_labs"],
        "excluded_labs": s["unauthorized_labs_excluded"],
        "direct_secrecy_multiplier": s["direct_secrecy_rp_multiplier"],
    }

    # 2) Compartment integration is a concrete access blocker.
    s = by_id["compartment_integration_blocker"]
    required = set(s["engineering_required_compartments"])
    initial = set(s["initial_integration_access"])
    later = set(s["later_integration_access"])
    initial_missing = sorted(required - initial)
    later_missing = sorted(required - later)
    if not initial_missing:
        fail("compartment fixture must start with a missing integration compartment")
    if later_missing:
        fail(f"later integration access still missing {later_missing}")
    if s["initial_blocker"] != "missing_compartment_integration_access" or s["expected_after_access"] != "unblocked":
        fail("compartment fixture blocker contract mismatch")
    results[s["id"]] = {
        "compartments_total": len(s["compartments"]),
        "required_for_engineering": len(required),
        "initial_missing": initial_missing,
        "later_missing": later_missing,
        "blocker_removed": True,
    }

    # 3) Physical observation can reveal effect, not implementation records.
    s = by_id["classified_deployed_capability_observed"]
    if s["foreign_understanding_before"] != "unknown" or s["foreign_understanding_after"] != "observed":
        fail("classified observation should move foreign understanding unknown -> observed")
    if s["foreign_reproduction_after"] != "none":
        fail("mere capability observation must not produce reproduction ability")
    if s["records_acquired"]:
        fail("observation fixture must not acquire implementation records")
    results[s["id"]] = {
        "existence_policy": s["existence_disclosure"],
        "understanding_after": s["foreign_understanding_after"],
        "reproduction_after": s["foreign_reproduction_after"],
        "records_acquired": 0,
    }

    # 4) Partial blueprint compromise improves assessment but is not instant native mastery.
    s = by_id["partial_blueprint_compromise"]
    acquired = set(s["acquired_assets"])
    missing = set(s["not_acquired_assets"])
    if not {"experimental_dataset","engineering_blueprints"}.issubset(acquired):
        fail("partial-compromise fixture missing expected records")
    if not {"manufacturing_tooling","expert_cohort","operating_institution","training_pipeline"}.issubset(missing):
        fail("partial-compromise fixture must omit tooling/experts/institution/training")
    if s["native_node_maturity_granted"] is not False:
        fail("stolen records must not grant native Mature technology")
    if s["foreign_understanding_after"] != "characterized":
        fail("reference partial compromise should reach characterized understanding")
    if s["foreign_reproduction_after"] != "component_replication":
        fail("reference partial compromise should stop at component replication")
    results[s["id"]] = {
        "acquired_record_classes": len(acquired),
        "missing_tacit_or_physical_classes": len(missing),
        "understanding_after": s["foreign_understanding_after"],
        "reproduction_after": s["foreign_reproduction_after"],
        "native_maturity_granted": False,
    }

    # 5) Declassification changes dissemination/resilience, not scientific truth.
    s = by_id["declassification_increases_resilience_not_truth"]
    if s["maturity_before"] != s["maturity_after"]:
        fail("declassification changed scientific maturity")
    if s["authorized_contexts_after"] <= s["authorized_contexts_before"]:
        fail("declassification should expand authorized contexts in reference fixture")
    if s["archive_copies_after_dissemination"] <= s["protected_archive_copies_before"]:
        fail("declassification fixture should improve archive redundancy")
    if s["maximum_delivery_latency_years"] <= 0:
        fail("declassification dissemination should still take real communication time")
    if s["practice_auto_granted"] is not False:
        fail("declassification must not auto-grant practice")
    results[s["id"]] = {
        "maturity_unchanged": True,
        "authorized_contexts_before": s["authorized_contexts_before"],
        "authorized_contexts_after": s["authorized_contexts_after"],
        "archive_copies_before": s["protected_archive_copies_before"],
        "archive_copies_after": s["archive_copies_after_dissemination"],
        "max_delivery_latency_years": s["maximum_delivery_latency_years"],
        "practice_auto_granted": False,
    }

    # 6) Reclassification cannot physically recall already distributed records.
    s = by_id["reclassification_cannot_recall_existing_copies"]
    if s["codified_contexts_immediately_after_reclassification"] != s["codified_contexts_before"]:
        fail("reclassification improperly changed already distributed copy count")
    if s["new_authorized_contexts"] >= s["codified_contexts_before"]:
        fail("reclassification fixture must demonstrate policy narrower than existing distribution")
    if s["automatic_remote_deletion"] is not False:
        fail("reclassification must not remotely delete copies")
    if s["future_normal_dissemination_allowed"] is not False:
        fail("reclassification should stop future normal dissemination in reference fixture")
    results[s["id"]] = {
        "copies_before": s["codified_contexts_before"],
        "new_authorized_contexts": s["new_authorized_contexts"],
        "copies_immediately_after": s["codified_contexts_immediately_after_reclassification"],
        "remote_deletion": False,
    }

    # 7) 1,000-year sparse security-state workload.
    s = by_id["research_security_state_soak_1000y"]
    years = s["years"]
    if s["catalog_nodes"] != index["node_count"]:
        fail(f"security soak catalog reference {s['catalog_nodes']} != current {index['node_count']}; intentionally update benchmark after catalog expansion")

    policies: dict[str, dict] = {}
    compromises: dict[str, int] = {}
    recent_events = deque(maxlen=s["max_recent_security_events"])
    archived = deque(maxlen=s["max_archived_security_summaries"])
    cursor = 0
    max_policies = 0
    max_compartments = 0
    max_compromises = 0

    for year in range(years):
        # Nondefault classification is occasional; normal research allocates no row.
        if year % 11 == 0:
            node = node_ids[cursor % len(node_ids)]
            cursor += 1
            access = "special_access_compartment" if year % 33 == 0 else ("classified" if year % 22 == 0 else "restricted_program")
            compartments = ((year // 11) % 3) + 1 if access == "special_access_compartment" else 0
            policies[node] = {"access": access, "compartments": compartments, "created": year}
            recent_events.append((year, "classified", node, access))

        # Declassification returns records to default state and removes policy rows.
        if year % 17 == 0 and policies:
            node = sorted(policies, key=lambda n: policies[n]["created"])[0]
            record = policies.pop(node)
            archived.append((year, node, record["access"], "declassified"))
            recent_events.append((year, "declassified", node, None))
            compromises.pop(node, None)

        # Known compromise assessments are sparse and expire/compress after review.
        if year % 29 == 0 and policies:
            node = sorted(policies)[year % len(policies)]
            compromises[node] = year
            recent_events.append((year, "known_compromise", node, "confirmed_partial"))

        for node, detected_year in list(compromises.items()):
            if year - detected_year >= 80 or node not in policies:
                archived.append((year, node, "compromise", "compressed"))
                del compromises[node]

        # Long-lived nondefault policy reviews sometimes normalize stale restrictions.
        for node, record in list(policies.items()):
            if year - record["created"] >= 180 and (year + len(node)) % 37 == 0:
                policies.pop(node)
                compromises.pop(node, None)
                archived.append((year, node, record["access"], "policy_retired"))
                recent_events.append((year, "policy_retired", node, None))

        active_compartments = sum(record["compartments"] for record in policies.values())
        max_policies = max(max_policies, len(policies))
        max_compartments = max(max_compartments, active_compartments)
        max_compromises = max(max_compromises, len(compromises))

    if max_policies > s["max_nondefault_security_records"]:
        fail(f"security soak policy rows {max_policies} > {s['max_nondefault_security_records']}")
    if max_compartments > s["max_active_compartments"]:
        fail(f"security soak compartments {max_compartments} > {s['max_active_compartments']}")
    if max_compromises > s["max_known_compromise_assessments"]:
        fail(f"security soak compromise assessments {max_compromises} > {s['max_known_compromise_assessments']}")
    if len(recent_events) > s["max_recent_security_events"] or len(archived) > s["max_archived_security_summaries"]:
        fail("bounded security history buffers exceeded configured length")

    results[s["id"]] = {
        "years": years,
        "catalog_nodes": len(node_ids),
        "max_nondefault_security_records": max_policies,
        "max_active_compartments": max_compartments,
        "max_known_compromise_assessments": max_compromises,
        "security_records_final": len(policies),
        "known_compromises_final": len(compromises),
        "recent_security_events_final": len(recent_events),
        "archived_security_summaries_final": len(archived),
    }

    print("research secrecy benchmark OK")
    for scenario_id in sorted(results):
        print(f"  {scenario_id}: {json.dumps(results[scenario_id], sort_keys=True)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
