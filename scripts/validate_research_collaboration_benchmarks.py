#!/usr/bin/env python3
"""Deterministic offline benchmarks for cross-polity Adaptive Research collaboration."""

from __future__ import annotations

import json
import sys
from collections import deque
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"research-collaboration benchmark failed: {message}")


def load(path: Path):
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def scaled_lab_units(assigned: float, recommended: float, scaling: dict) -> float:
    if assigned <= 0:
        return 0.0
    first = min(assigned, recommended)
    value = first * scaling["at_or_below_recommended_labs_efficiency_per_lab"]
    if assigned > recommended:
        second = min(assigned - recommended, recommended)
        value += second * scaling["above_recommended_to_2x_recommended_efficiency_per_extra_lab"]
    if assigned > 2 * recommended:
        value += (assigned - 2 * recommended) * scaling["above_2x_recommended_efficiency_per_extra_lab"]
    return value


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else "data/research/v1")
    scenarios = load(root / "research_collaboration_benchmark_scenarios.json")["scenarios"]
    by_id = {row["id"]: row for row in scenarios}
    economy = load(root / "research_economy.json")
    scaling = economy["lab_scaling"]
    base_rp = economy["research_model"]["research_points"]["base_rp_per_effective_lab_per_year"]
    results = {}

    # 1) Real labs: aggregate once, never one diminishing-return bucket per flag.
    s = by_id["real_labs_no_treaty_multiplier"]
    if s["participant_a_labs"] + s["participant_b_labs"] != s["combined_assigned_labs"]:
        fail("joint-lab fixture contribution sum mismatch")
    combined = scaled_lab_units(s["combined_assigned_labs"], s["recommended_labs"], scaling)
    a_only = scaled_lab_units(s["participant_a_labs"], s["recommended_labs"], scaling)
    b_only = scaled_lab_units(s["participant_b_labs"], s["recommended_labs"], scaling)
    wrong_separate = a_only + b_only
    joint_rp = combined * base_rp * s["treaty_multiplier"]
    if abs(combined - s["expected_combined_scaled_lab_units"]) > 1e-9:
        fail(f"combined scaled labs {combined} != expected {s['expected_combined_scaled_lab_units']}")
    if abs(joint_rp - s["expected_joint_rp_before_readiness"]) > 1e-9:
        fail(f"joint RP {joint_rp} != expected {s['expected_joint_rp_before_readiness']}")
    if abs(wrong_separate - s["incorrect_separate_bucket_scaled_units"]) > 1e-9:
        fail("fixture separate-bucket reference drifted")
    if combined >= wrong_separate:
        fail("joint scaling should prevent multi-party diminishing-return bypass")
    if not (joint_rp > a_only * base_rp and joint_rp > b_only * base_rp):
        fail("real added capacity should improve progress over either partner alone")
    if s["treaty_multiplier"] != 1.0:
        fail("treaty multiplier must remain 1.0")
    results[s["id"]] = {
        "participant_a_scaled_units": round(a_only, 3),
        "participant_b_scaled_units": round(b_only, 3),
        "combined_scaled_units": round(combined, 3),
        "incorrect_separate_bucket_units": round(wrong_separate, 3),
        "joint_rp_before_readiness": round(joint_rp, 1),
        "treaty_multiplier": s["treaty_multiplier"],
    }

    # 2) Withdrawal removes real future capacity without rolling progress back.
    s = by_id["participant_withdrawal_removes_real_capacity"]
    before = scaled_lab_units(s["labs_before_withdrawal"], 16, scaling)
    after = scaled_lab_units(s["labs_after_withdrawal"], 16, scaling)
    if abs(before - s["scaled_units_before"]) > 1e-9 or abs(after - s["scaled_units_after"]) > 1e-9:
        fail("withdrawal scaled-unit reference drifted")
    if after >= before:
        fail("withdrawing real labs should reduce effective capacity")
    if s["completed_stage_progress_after"] != s["completed_stage_progress_before"]:
        fail("withdrawal must not reverse completed work")
    if s["delivered_records_recalled"] is not False:
        fail("withdrawal cannot recall delivered records")
    results[s["id"]] = {
        "scaled_units_before": before,
        "scaled_units_after": after,
        "capacity_loss_units": round(before - after, 3),
        "stage_progress_preserved": True,
        "records_recalled": False,
    }

    # 3) Facility withdrawal can hard-block despite labs.
    s = by_id["facility_withdrawal_blocks_stage"]
    if not s["facility_available_before"] or s["facility_available_after"]:
        fail("facility-withdrawal fixture transition invalid")
    if s["contributing_labs_remaining"] <= 0:
        fail("facility-withdrawal fixture must retain labs")
    if s["expected_after_withdrawal"] != "blocked_missing_specialized_facility":
        fail("facility withdrawal must be a hard specific blocker")
    results[s["id"]] = {
        "labs_remaining": s["contributing_labs_remaining"],
        "required_facility": s["required_facility_capability"],
        "facility_after": False,
        "project_state": s["expected_after_withdrawal"],
    }

    # 4) Equal records can create unequal use because compatibility remains local.
    s = by_id["asymmetric_result_usability"]
    if s["participant_a_understanding"] != s["participant_b_understanding"]:
        fail("asymmetric-usability fixture should give equal formal understanding")
    if s["participant_a_operability"] == s["participant_b_operability"]:
        fail("fixture must demonstrate asymmetric operability")
    if s["participant_a_reproduction"] == s["participant_b_reproduction"]:
        fail("fixture must demonstrate asymmetric reproduction")
    if s["native_node_maturity_granted"] is not False:
        fail("joint foreign-tech study cannot grant native maturity")
    results[s["id"]] = {
        "shared_result_components": len(s["result_package"]),
        "understanding_equal": True,
        "participant_a_operability": s["participant_a_operability"],
        "participant_b_operability": s["participant_b_operability"],
        "participant_a_reproduction": s["participant_a_reproduction"],
        "participant_b_reproduction": s["participant_b_reproduction"],
        "native_maturity_granted": False,
    }

    # 5) Classified joint integration can work without giving every partner every compartment.
    s = by_id["classified_joint_compartments"]
    compartments = set(s["compartments"])
    a = set(s["participant_a_access"])
    b = set(s["participant_b_access"])
    integration = set(s["integration_context_access"])
    if not integration.issuperset(compartments):
        fail("integration context lacks required full compartment set")
    if a.issuperset(compartments) or b.issuperset(compartments):
        fail("reference partners should not independently possess full package")
    if not (a | b).issuperset(compartments):
        fail("joint participants collectively lack project compartments")
    if not s["joint_integration_possible"]:
        fail("joint integration should be possible in fixture")
    results[s["id"]] = {
        "compartments": len(compartments),
        "participant_a_missing": sorted(compartments - a),
        "participant_b_missing": sorted(compartments - b),
        "integration_context_complete": True,
        "a_independent_complete": False,
        "b_independent_complete": False,
    }

    # 6) Communication partition blocks only the dependency that uses remote data.
    s = by_id["communication_partition_pauses_dependency_not_whole_treaty"]
    if not s["local_observation_work_continues"]:
        fail("local work must continue in reference partition")
    if not s["cross_site_correlation_requires_remote_data"]:
        fail("cross-site blocker must have a real remote-data dependency")
    if s["generic_joint_research_penalty"] is not False:
        fail("joint partition must not use generic penalty")
    if not s["records_already_local_remain_accessible"]:
        fail("partition must not delete existing local records")
    results[s["id"]] = {
        "partition_years": s["partition_years"],
        "local_work_continues": True,
        "cross_site_state": s["cross_site_correlation_state_during_partition"],
        "generic_penalty": False,
        "local_records_preserved": True,
    }

    # 7) 1,000-year sparse collaboration-state workload.
    s = by_id["research_collaboration_state_soak_1000y"]
    active: dict[int, dict] = {}
    pending_deliveries: list[dict] = []
    recent = deque(maxlen=s["max_recent_collaboration_events"])
    archived = deque(maxlen=s["max_archived_collaboration_summaries"])
    next_id = 1
    max_active = 0
    max_contrib = 0
    max_pending = 0

    for year in range(s["years"]):
        # Start one real scientific collaboration occasionally; diplomatic relations without science allocate no record.
        if year % 9 == 0:
            cid = next_id
            next_id += 1
            participants = 2 + (year % 2)
            contributions = [
                {"polity": f"p{(cid + i) % 19}", "labs": 2 + ((cid + i) % 7)}
                for i in range(participants)
            ]
            active[cid] = {
                "created": year,
                "participants": participants,
                "contributions": contributions,
                "complete_at": year + 12 + (cid % 18),
            }
            recent.append((year, "started", cid, participants))

        # Deterministic withdrawal changes a contribution but does not duplicate the collaboration.
        if year % 23 == 0 and active:
            cid = sorted(active)[year % len(active)]
            record = active[cid]
            if len(record["contributions"]) > 1:
                withdrawn = record["contributions"].pop()
                record["participants"] -= 1
                recent.append((year, "withdrawn", cid, withdrawn["polity"]))

        # Complete collaborations create participant-specific pending result deliveries then archive detail.
        for cid, record in list(active.items()):
            if year >= record["complete_at"]:
                for i, contribution in enumerate(record["contributions"]):
                    pending_deliveries.append({
                        "cid": cid,
                        "polity": contribution["polity"],
                        "deliver": year + 1 + (i % 3),
                    })
                archived.append((year, cid, "completed", record["participants"]))
                recent.append((year, "completed", cid, record["participants"]))
                del active[cid]

        # Apply and delete result deliveries once incorporated.
        remaining = []
        for delivery in pending_deliveries:
            if delivery["deliver"] <= year:
                recent.append((year, "result_delivered", delivery["cid"], delivery["polity"]))
            else:
                remaining.append(delivery)
        pending_deliveries = remaining

        contribution_count = sum(len(record["contributions"]) for record in active.values())
        max_active = max(max_active, len(active))
        max_contrib = max(max_contrib, contribution_count)
        max_pending = max(max_pending, len(pending_deliveries))

    if max_active > s["max_active_collaborations"]:
        fail(f"soak active collaborations {max_active} > {s['max_active_collaborations']}")
    if max_contrib > s["max_participant_contribution_records"]:
        fail(f"soak contribution records {max_contrib} > {s['max_participant_contribution_records']}")
    if max_pending > s["max_pending_result_deliveries"]:
        fail(f"soak pending deliveries {max_pending} > {s['max_pending_result_deliveries']}")
    if len(recent) > s["max_recent_collaboration_events"] or len(archived) > s["max_archived_collaboration_summaries"]:
        fail("collaboration history buffers exceeded configured bounds")

    results[s["id"]] = {
        "years": s["years"],
        "collaborations_created": next_id - 1,
        "max_active_collaborations": max_active,
        "max_participant_contribution_records": max_contrib,
        "max_pending_result_deliveries": max_pending,
        "active_final": len(active),
        "pending_deliveries_final": len(pending_deliveries),
        "recent_events_final": len(recent),
        "archived_summaries_final": len(archived),
    }

    print("research collaboration benchmark OK")
    for scenario_id in sorted(results):
        print(f"  {scenario_id}: {json.dumps(results[scenario_id], sort_keys=True)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
