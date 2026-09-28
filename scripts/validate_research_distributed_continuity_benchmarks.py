#!/usr/bin/env python3
"""Deterministic offline benchmarks for distributed Adaptive Research continuity."""

from __future__ import annotations

import json
import sys
from collections import deque
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"research-distributed-continuity benchmark failed: {message}")


def load(path: Path):
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def jaccard_distance(a: set[str], b: set[str]) -> float:
    union = a | b
    return 0.0 if not union else 1.0 - (len(a & b) / len(union))


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else "data/research/v1")
    scenarios = load(root / "distributed_research_benchmark_scenarios.json")["scenarios"]
    by_id = {row["id"]: row for row in scenarios}
    index = load(root / "index.json")

    all_nodes: list[str] = []
    for domain in index["domains"]:
        filename = index["domain_files"][domain["id"]]
        all_nodes.extend(node["id"] for node in load(root / filename)["nodes"])
    node_ids = sorted(set(all_nodes))
    if len(node_ids) != index["node_count"]:
        fail("node catalog could not be loaded consistently")

    results = {}

    # 1) Communication latency: access must not arrive before information can.
    s = by_id["communication_latency_delivery"]
    delivery = round(s["discovery_time_year"] + s["one_way_latency_years"], 6)
    if abs(delivery - s["expected_delivery_year"]) > 1e-9:
        fail(f"communication delivery mismatch {delivery} != {s['expected_delivery_year']}")
    access_before = s["pre_delivery_access"]
    access_at = s["post_delivery_access"]
    if access_before != "absent" or access_at != "codified":
        fail("communication scenario must move absent -> codified")
    if s["local_practice_after_delivery"] != "not_automatically_practiced":
        fail("codified delivery must not auto-create practice")
    results[s["id"]] = {
        "discovery_year": s["discovery_time_year"],
        "delivery_year": delivery,
        "latency_years": s["one_way_latency_years"],
        "access_before": access_before,
        "access_after": access_at,
        "practice_auto_granted": False,
    }

    # 2) Partition: archives preserve theory better than inactive practice.
    s = by_id["forty_year_partition_and_reintegration"]
    initial = s["initial_field_practice"]
    after = s["practice_after_partition"]
    practice_losses = {}
    for field_id, values in initial.items():
        if field_id not in after:
            fail(f"partition scenario missing after-state for {field_id}")
        losses = {component: values[component] - after[field_id][component] for component in values}
        practice_losses[field_id] = losses
        if losses["theoretical"] > 5:
            fail(f"archive-backed theoretical loss too large for {field_id}: {losses['theoretical']}")
        if losses["engineering"] < 12:
            fail(f"partition should demonstrate meaningful engineering-practice decline for {field_id}")
        if losses["experimental"] <= losses["theoretical"]:
            fail(f"experimental practice should decay more than archived theory for {field_id}")
    if s["expected_access_after_reconnection"] != "codified":
        fail("reconnection should restore codified access in reference scenario")
    results[s["id"]] = {
        "partition_years": s["partition_years"],
        "missed_nodes": len(s["new_core_nodes_missed_during_partition"]),
        "reconnection_latency_years": s["reconnection_latency_years"],
        "practice_recovery_years": s["practice_recovery_years"],
        "losses": practice_losses,
    }

    # 3) Successor inheritance: shared foundations overlap, specialist knowledge diverges.
    s = by_id["successor_state_asymmetric_inheritance"]
    common = set(s["common_widely_replicated_nodes"])
    a_special = set(s["successor_a"]["local_specialist_nodes"])
    b_special = set(s["successor_b"]["local_specialist_nodes"])
    if a_special & b_special:
        fail("successor specialist sets should be distinct in reference fixture")
    a_nodes = common | a_special
    b_nodes = common | b_special
    forbidden = set(s["former_polity_recent_global_nodes_not_present_in_either_local_archive"])
    if forbidden & a_nodes or forbidden & b_nodes:
        fail("successor inherited former-polity recent knowledge absent from local archives")
    distance = jaccard_distance(a_nodes, b_nodes)
    if not (0.25 <= distance <= 0.75):
        fail(f"successor benchmark divergence outside useful reference range: {distance:.3f}")
    if not common.issubset(a_nodes) or not common.issubset(b_nodes):
        fail("successors must retain common replicated foundation")
    results[s["id"]] = {
        "shared_nodes": len(common),
        "successor_a_total": len(a_nodes),
        "successor_b_total": len(b_nodes),
        "specialist_a": len(a_special),
        "specialist_b": len(b_special),
        "jaccard_distance": round(distance, 3),
        "former_global_missing_both": len(forbidden),
    }

    # 4) Archive catastrophe: actionable local records can be lost/recovered without teleporting practice.
    s = by_id["isolated_archive_catastrophe_and_recovery"]
    if s["archive_class_before"] != "local_only":
        fail("archive catastrophe reference requires a genuine single local copy")
    if not (s["access_before"] == "codified" and s["access_after_archive_loss"] == "reference_only" and s["access_after_recovery"] == "codified"):
        fail("archive access transition must be codified -> reference_only -> codified")
    if s["practice_after_recovery"] != "not_automatically_practiced":
        fail("archive recovery must not recreate practice")
    results[s["id"]] = {
        "affected_nodes": len(s["affected_nodes"]),
        "recovery_delay_years": s["external_copy_recovered_after_years"],
        "access_sequence": [s["access_before"], s["access_after_archive_loss"], s["access_after_recovery"]],
        "practice_auto_restored": False,
    }

    # 5) 1,000-year sparse-context reference harness.
    s = by_id["distributed_context_state_soak_1000y"]
    years = s["years"]
    regions_total = s["regions_total"]
    contexts: dict[int, dict] = {}
    pending: list[dict] = []
    recent_events = deque(maxlen=s["max_recent_context_events"])
    archived_summaries = deque(maxlen=s["max_archived_context_summaries"])
    discovered: list[str] = []
    max_contexts = 0
    max_access_exceptions = 0
    max_field_exceptions = 0
    max_pending = 0

    # Only a bounded stream of discoveries is needed; catalog is static/shared.
    discovery_cursor = 0

    for year in range(years):
        # One new civilization-level codified result every three years until the public seed is exhausted.
        if year % 3 == 0 and discovery_cursor < len(node_ids):
            node_id = node_ids[discovery_cursor]
            discovery_cursor += 1
            discovered.append(node_id)
            for region_id, ctx in contexts.items():
                if year < ctx["isolated_until"]:
                    ctx["missing"].add(node_id)
                    recent_events.append((year, "missed_delivery", region_id, node_id))

        # Deterministic intermittent partitions. Multiple normal regions stay implicit.
        if year % 7 == 0:
            region_id = (year * 17 + 11) % regions_total
            if region_id not in contexts:
                duration = 10 + (year % 19)
                contexts[region_id] = {
                    "isolated_until": min(years + 1, year + duration),
                    "missing": set(),
                    "practice_fields": {"communications", "engineering"},
                    "reconnected_at": None,
                }
                recent_events.append((year, "context_diverged", region_id, duration))
            else:
                # Repeated disruption extends the same context rather than duplicating it.
                contexts[region_id]["isolated_until"] = max(contexts[region_id]["isolated_until"], year + 8)

        # When a partition ends, schedule one bounded delivery per missing revision.
        for region_id, ctx in list(contexts.items()):
            if ctx["reconnected_at"] is None and year >= ctx["isolated_until"]:
                ctx["reconnected_at"] = year
                for offset, node_id in enumerate(sorted(ctx["missing"])):
                    pending.append({
                        "region": region_id,
                        "node": node_id,
                        "deliver": year + 1 + (offset % 3),
                    })
                recent_events.append((year, "reconnected", region_id, len(ctx["missing"])))

        # Apply due transmissions and delete them immediately after state application.
        still_pending = []
        for tx in pending:
            if tx["deliver"] <= year:
                ctx = contexts.get(tx["region"])
                if ctx is not None:
                    ctx["missing"].discard(tx["node"])
                    recent_events.append((year, "delivered", tx["region"], tx["node"]))
            else:
                still_pending.append(tx)
        pending = still_pending

        # Reintegrated contexts disappear once science is synchronized and local practice no longer materially differs.
        pending_regions = {tx["region"] for tx in pending}
        for region_id, ctx in list(contexts.items()):
            if (
                ctx["reconnected_at"] is not None
                and not ctx["missing"]
                and region_id not in pending_regions
                and year - ctx["reconnected_at"] >= 6
            ):
                archived_summaries.append((year, region_id, "reintegrated"))
                recent_events.append((year, "context_merged", region_id, None))
                del contexts[region_id]

        access_exceptions = sum(len(ctx["missing"]) for ctx in contexts.values())
        field_exceptions = sum(len(ctx["practice_fields"]) for ctx in contexts.values())
        max_contexts = max(max_contexts, len(contexts))
        max_access_exceptions = max(max_access_exceptions, access_exceptions)
        max_field_exceptions = max(max_field_exceptions, field_exceptions)
        max_pending = max(max_pending, len(pending))

    if max_contexts > s["max_materially_divergent_contexts"]:
        fail(f"soak explicit context max {max_contexts} > {s['max_materially_divergent_contexts']}")
    if max_access_exceptions > s["max_node_access_exceptions_total"]:
        fail(f"soak access exceptions {max_access_exceptions} > {s['max_node_access_exceptions_total']}")
    if max_field_exceptions > s["max_field_practice_exceptions_total"]:
        fail(f"soak field exceptions {max_field_exceptions} > {s['max_field_practice_exceptions_total']}")
    if max_pending > s["max_pending_transmissions"]:
        fail(f"soak pending transmissions {max_pending} > {s['max_pending_transmissions']}")
    if len(recent_events) > s["max_recent_context_events"]:
        fail("recent context event buffer exceeded configured bound")
    if len(archived_summaries) > s["max_archived_context_summaries"]:
        fail("archived context summary buffer exceeded configured bound")

    results[s["id"]] = {
        "regions_total": regions_total,
        "years": years,
        "discoveries_processed": len(discovered),
        "max_explicit_contexts": max_contexts,
        "max_access_exceptions": max_access_exceptions,
        "max_field_practice_exceptions": max_field_exceptions,
        "max_pending_transmissions": max_pending,
        "recent_events_final": len(recent_events),
        "archived_context_summaries_final": len(archived_summaries),
        "explicit_contexts_final": len(contexts),
    }

    print("research distributed continuity benchmark OK")
    for scenario_id in sorted(results):
        print(f"  {scenario_id}: {json.dumps(results[scenario_id], sort_keys=True)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
