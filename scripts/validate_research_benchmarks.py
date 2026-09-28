#!/usr/bin/env python3
"""Run deterministic offline Adaptive Research design benchmarks.

This is deliberately NOT the gameplay research runtime. The offline harness may scan the
public possibility graph to test long-run design properties cheaply. Gameplay remains
bound by the event/index-driven contracts in research_runtime_contract.json.
"""

from __future__ import annotations

import json
import math
import sys
from collections import Counter, defaultdict
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"research-benchmark validation failed: {message}")


def load_json(path: Path):
    try:
        with path.open("r", encoding="utf-8") as handle:
            return json.load(handle)
    except Exception as exc:
        fail(f"could not parse {path}: {exc}")


class ResearchHarness:
    def __init__(self, root: Path, scenarios: dict):
        self.root = root
        self.scenarios = scenarios
        self.index = load_json(root / "index.json")
        self.economy = load_json(root / "research_economy.json")
        self.capability_model = load_json(root / "capability_model.json")
        self.grants = load_json(root / "technology_grants.json")
        self.fragments_data = load_json(root / "starting_research_fragments.json")
        self.profiles_data = load_json(root / "starting_reference_profiles.json")
        self.traits_data = load_json(root / "applicability_traits.json")
        self.capacity = load_json(root / "research_capacity.json")

        self.nodes = {}
        for domain in self.index.get("domains", []):
            filename = self.index["domain_files"][domain["id"]]
            payload = load_json(root / filename)
            for node in payload.get("nodes", []):
                self.nodes[node["id"]] = node

        self.state_order = self.index.get("maturation_states", [])
        self.state_rank = {state: idx for idx, state in enumerate(self.state_order)}
        self.fragments = {row["id"]: row for row in self.fragments_data.get("fragments", [])}
        self.profiles = {row["id"]: row for row in self.profiles_data.get("profiles", [])}
        self.composition_cap = self.fragments_data.get("composition_cap_per_competence_component", 78)
        self.cross_caps = {row["id"] for row in self.capability_model.get("cross_lineage_capabilities", [])}
        self.cap_implications = defaultdict(set)
        for edge in self.capability_model.get("implications", []):
            self.cap_implications[edge["from"]].add(edge["to"])
        self.defaults = scenarios["shared_defaults"]

    def compose_profile(self, profile_id: str) -> dict:
        profile = self.profiles.get(profile_id)
        if profile is None:
            fail(f"unknown benchmark starting profile {profile_id}")

        states = {}
        traits = set()
        capabilities = set()
        pressures = {}
        competence = {}
        institutions = Counter()

        def merge_row(row: dict, prefix: str = "starting_"):
            for node_id, state_value in row.get(prefix + "node_states", {}).items():
                state = state_value.get("state") if isinstance(state_value, dict) else state_value
                old = states.get(node_id)
                if old is None or self.state_rank[state] > self.state_rank[old]:
                    states[node_id] = state
            traits.update(row.get(prefix + "applicability_traits", []))
            for cap in row.get(prefix + "capabilities", []):
                capabilities.add(cap.get("id") if isinstance(cap, dict) else cap)
            for pressure_id, value in row.get(prefix + "pressure_state", {}).items():
                pressures[pressure_id] = max(pressures.get(pressure_id, 0), float(value))
            for field_id, comps in row.get(prefix + "field_competence", {}).items():
                target = competence.setdefault(field_id, {"theoretical": 0.0, "experimental": 0.0, "engineering": 0.0})
                for component in target:
                    target[component] = min(
                        self.composition_cap,
                        max(target[component], float(comps.get(component, 0))),
                    )
            for inst in row.get(prefix + "research_institutions", []):
                institutions[inst["institution_archetype_id"]] += int(inst["count"])

        for fragment_id in profile.get("fragment_ids", []):
            merge_row(self.fragments[fragment_id])

        additional = {
            "starting_node_states": profile.get("additional_starting_node_states", {}),
            "starting_applicability_traits": profile.get("additional_starting_applicability_traits", []),
            "starting_capabilities": profile.get("additional_starting_capabilities", []),
            "starting_pressure_state": profile.get("additional_starting_pressure_state", {}),
            "starting_field_competence": profile.get("additional_starting_field_competence", {}),
            "starting_research_institutions": profile.get("additional_starting_research_institutions", []),
        }
        merge_row(additional)

        for node_id, state in list(states.items()):
            if state == "mature":
                self.apply_mature_outputs(node_id, traits, capabilities)
        self.expand_capabilities(capabilities)

        return {
            "states": states,
            "traits": traits,
            "capabilities": capabilities,
            "base_pressures": pressures,
            "competence": competence,
            "institutions": institutions,
        }

    def apply_mature_outputs(self, node_id: str, traits: set[str], capabilities: set[str]) -> None:
        node = self.nodes[node_id]
        for cap in node.get("capabilities", []):
            capabilities.add(cap)
        structural = self.grants.get("on_mature", {}).get(node_id, {})
        traits.update(structural.get("add_civilization_traits", []))

    def expand_capabilities(self, capabilities: set[str]) -> None:
        changed = True
        while changed:
            changed = False
            for source in list(capabilities):
                for target in self.cap_implications.get(source, []):
                    if target not in capabilities:
                        capabilities.add(target)
                        changed = True

    def lab_capacity(self, elapsed_year: int, scenario: dict) -> int:
        schedule = scenario.get("lab_capacity_schedule", self.defaults["lab_capacity_schedule"])
        current = int(schedule[0]["effective_labs"])
        for row in schedule:
            if elapsed_year >= int(row["elapsed_year"]):
                current = int(row["effective_labs"])
            else:
                break
        return current

    def active_pressures(self, civ_cfg: dict, elapsed: int, base: dict) -> dict:
        result = dict(base)
        for row in civ_cfg.get("pressure_schedule", []):
            if int(row["from"]) <= elapsed < int(row["to"]):
                result[row["pressure"]] = max(result.get(row["pressure"], 0), float(row["value"]))
        return result

    def requirements(self, node_id: str) -> dict:
        node = self.nodes[node_id]
        cfg = self.economy["complexity_defaults"][node["complexity"]]
        override = self.economy.get("node_requirement_overrides", {}).get(node_id, {})
        return {
            "minimum_labs": int(override.get("minimum_labs", cfg["minimum_labs"])),
            "recommended_labs": int(override.get("recommended_labs", cfg["recommended_labs"])),
            "required_pressure": override.get("required_pressure", {}),
            "required_pressure_any": override.get("required_pressure_any", {}),
            "required_evidence": override.get("required_evidence", []),
        }

    def project_cost(self, node_id: str) -> float:
        node = self.nodes[node_id]
        base = float(self.economy["complexity_defaults"][node["complexity"]]["base_research_points"])
        return round(base * (1.0 + 0.08 * int(node.get("graph_depth", 0))))

    def prereqs_met(self, node: dict, states: dict[str, str]) -> bool:
        prereq = node.get("prerequisites", {})
        if any(states.get(parent) != "mature" for parent in prereq.get("all_of", [])):
            return False
        any_of = prereq.get("any_of", [])
        if any_of and not any(states.get(parent) == "mature" for parent in any_of):
            return False
        return True

    def caps_met(self, node: dict, capabilities: set[str]) -> bool:
        req = node.get("capability_requirements", {})
        if any(cap not in capabilities for cap in req.get("all_of", [])):
            return False
        any_of = req.get("any_of", [])
        if any_of and not any(cap in capabilities for cap in any_of):
            return False
        return True

    def pressure_gate_met(self, node_id: str, pressures: dict[str, float]) -> bool:
        req = self.requirements(node_id)
        for pressure_id, threshold in req["required_pressure"].items():
            if pressures.get(pressure_id, 0) < float(threshold):
                return False
        any_req = req["required_pressure_any"]
        if any_req and not any(pressures.get(pid, 0) >= float(threshold) for pid, threshold in any_req.items()):
            return False
        return True

    def evidence_gate_clear_for_offline_scenario(self, node: dict, node_id: str) -> bool:
        # Generic benchmarks do not invent alien/anomaly evidence. Foreign-tech brokerage is
        # validated separately from native node progression.
        if node.get("applicability", {}).get("requires_evidence", []):
            return False
        if self.requirements(node_id)["required_evidence"]:
            return False
        return True

    def applicability_met(self, node: dict, traits: set[str]) -> bool:
        return all(trait in traits for trait in node.get("applicability", {}).get("requires_traits", []))

    @staticmethod
    def military_score(states: dict[str, str], nodes: dict[str, dict]) -> float:
        score = 0.0
        for node_id, state in states.items():
            if state != "mature":
                continue
            node = nodes.get(node_id)
            if node and node.get("domain") == "military":
                score += 1.0 + 0.15 * float(node.get("graph_depth", 0))
        return score

    def run_civilizations(self, scenario: dict) -> dict[str, dict]:
        states_by_civ = {}
        for cfg in scenario["civilizations"]:
            profile_id = cfg.get("starting_reference_profile", scenario.get("starting_reference_profile", self.defaults["starting_reference_profile"]))
            composed = self.compose_profile(profile_id)
            for node_id in cfg.get("benchmark_start_grants", {}).get("mature_nodes", []):
                if node_id not in self.nodes:
                    fail(f"scenario {scenario['id']}/{cfg['id']}: unknown benchmark grant {node_id}")
                composed["states"][node_id] = "mature"
                self.apply_mature_outputs(node_id, composed["traits"], composed["capabilities"])
            self.expand_capabilities(composed["capabilities"])
            states_by_civ[cfg["id"]] = {
                "cfg": cfg,
                **composed,
                "rp_progress": {},
                "recent_events": [],
                "detailed_history": [],
                "attention_history": [],
                "military_score_history": [],
                "observed_peer_ratio": None,
                "observed_peer_quality": 0.0,
            }

        duration = int(scenario["duration_years"])
        bg_fraction = float(scenario.get("background_science_fraction", self.defaults["background_science_fraction"]))
        max_recent = int(self.defaults["max_recent_event_records"])
        max_history = int(self.defaults["max_visible_history_detail_records"])

        for elapsed in range(duration + 1):
            # First synthesize only scheduled legitimate peer observations.
            for civ_id, state in states_by_civ.items():
                for obs in state["cfg"].get("observed_peer_schedule", []):
                    if int(obs["year"]) != elapsed:
                        continue
                    peer = states_by_civ.get(obs["peer"])
                    if peer is None:
                        fail(f"scenario {scenario['id']}/{civ_id}: unknown observed peer {obs['peer']}")
                    own_score = max(0.001, self.military_score(state["states"], self.nodes))
                    peer_score = self.military_score(peer["states"], self.nodes)
                    state["observed_peer_ratio"] = peer_score / own_score
                    state["observed_peer_quality"] = float(obs["observation_quality"])
                    state["recent_events"].append((elapsed, "legitimate_peer_observation", obs["peer"]))

            for civ_id, state in states_by_civ.items():
                cfg = state["cfg"]
                pressures = self.active_pressures(cfg, elapsed, state["base_pressures"])
                attention = {key: float(value) for key, value in cfg.get("domain_attention", {}).items()}
                culture = cfg.get("culture", {})

                # Benchmark approximation of the canonical agenda complacency behavior.
                if "military" in attention and culture:
                    base_military = attention["military"]
                    threat_pressure = max(
                        pressures.get("fleet_losses", 0),
                        pressures.get("weapon_ineffectiveness", 0),
                        pressures.get("missile_threat", 0),
                        pressures.get("enemy_speed_superiority", 0),
                    )
                    if threat_pressure < 15 and float(culture.get("complacency_tendency", 50)) >= 65:
                        attention["military"] = max(0.25, base_military - 2.0)
                    ratio = state.get("observed_peer_ratio")
                    quality = state.get("observed_peer_quality", 0)
                    if ratio is not None and quality >= 0.55 and ratio >= 0.60 and float(culture.get("threat_sensitivity", 50)) >= 55:
                        attention["military"] = max(attention["military"], min(4.0, base_military + 1.0))

                state["attention_history"].append((elapsed, attention.get("military", 0.0)))
                state["military_score_history"].append((elapsed, self.military_score(state["states"], self.nodes)))

                # Offline materialization scan. Runtime does this with indexes/events instead.
                for node_id, node in self.nodes.items():
                    if node_id in state["states"]:
                        continue
                    if not self.prereqs_met(node, state["states"]):
                        continue
                    if not self.caps_met(node, state["capabilities"]):
                        continue
                    if not self.applicability_met(node, state["traits"]):
                        continue
                    if not self.evidence_gate_clear_for_offline_scenario(node, node_id):
                        continue
                    if not self.pressure_gate_met(node_id, pressures):
                        continue
                    pressure_relevance = max((pressures.get(pid, 0) for pid in node.get("pressure_affinities", [])), default=0)
                    domain_attention = attention.get(node.get("domain"), 0.0)
                    if domain_attention <= 0 and pressure_relevance < 20:
                        continue
                    state["states"][node_id] = "investigable"
                    state["recent_events"].append((elapsed, "visible", node_id))

                candidates = []
                for node_id, node_state in state["states"].items():
                    if node_state != "investigable":
                        continue
                    node = self.nodes[node_id]
                    req = self.requirements(node_id)
                    total_labs = self.lab_capacity(elapsed, scenario)
                    if total_labs < req["minimum_labs"]:
                        continue
                    pressure_relevance = max((pressures.get(pid, 0) for pid in node.get("pressure_affinities", [])), default=0)
                    domain_attention = attention.get(node.get("domain"), 0.0)
                    hypothesis_bonus = 0.0
                    if node.get("is_hypothesis"):
                        hypothesis_bonus += 0.20 * float(culture.get("curiosity", 50))
                        hypothesis_bonus += 0.10 * float(culture.get("risk_tolerance", 50))
                    depth_bonus = max(0.0, 18.0 - 1.3 * float(node.get("graph_depth", 0)))
                    score = 55.0 * domain_attention + pressure_relevance + hypothesis_bonus + depth_bonus
                    if domain_attention <= 0 and pressure_relevance < 20:
                        score -= 75.0
                    candidates.append((score, node_id))
                candidates.sort(key=lambda item: (-item[0], item[1]))

                if "autonomous_research_portfolios" in state["states"] and state["states"]["autonomous_research_portfolios"] == "mature":
                    program_limit = 8
                elif state["states"].get("distributed_scientific_portfolios") == "mature":
                    program_limit = 4
                elif state["states"].get("coordinated_research_networks") == "mature":
                    program_limit = 2
                else:
                    program_limit = 1

                total_labs = self.lab_capacity(elapsed, scenario)
                directed_labs = max(1, int(math.floor(total_labs * (1.0 - bg_fraction))))
                remaining = directed_labs
                selected = []
                for _, node_id in candidates:
                    if len(selected) >= program_limit:
                        break
                    req = self.requirements(node_id)
                    if remaining < req["minimum_labs"]:
                        continue
                    slots_left = max(1, program_limit - len(selected))
                    fair_share = max(req["minimum_labs"], remaining // slots_left)
                    assigned = min(remaining, max(req["minimum_labs"], fair_share))
                    selected.append((node_id, assigned))
                    remaining -= assigned
                    if remaining <= 0:
                        break

                for node_id, assigned in selected:
                    req = self.requirements(node_id)
                    recommended = max(req["recommended_labs"], 1)
                    base = min(assigned, recommended)
                    extra1 = min(max(assigned - recommended, 0), recommended)
                    extra2 = max(assigned - 2 * recommended, 0)
                    effective_labs = base + 0.35 * extra1 + 0.10 * extra2
                    annual_rp = effective_labs * float(self.economy["research_model"]["research_points"]["base_rp_per_effective_lab_per_year"])
                    state["rp_progress"][node_id] = state["rp_progress"].get(node_id, 0.0) + annual_rp
                    if state["rp_progress"][node_id] >= self.project_cost(node_id):
                        state["states"][node_id] = "mature"
                        self.apply_mature_outputs(node_id, state["traits"], state["capabilities"])
                        self.expand_capabilities(state["capabilities"])
                        state["detailed_history"].append((elapsed, "mature", node_id))
                        state["recent_events"].append((elapsed, "mature", node_id))
                        state["rp_progress"].pop(node_id, None)

                if len(state["recent_events"]) > max_recent:
                    del state["recent_events"][:-max_recent]
                if len(state["detailed_history"]) > max_history:
                    del state["detailed_history"][:-max_history]

        return states_by_civ


def jaccard_distance(a: set[str], b: set[str]) -> float:
    union = a | b
    if not union:
        return 0.0
    return 1.0 - len(a & b) / len(union)


def mature_set(state: dict) -> set[str]:
    return {node_id for node_id, status in state["states"].items() if status == "mature"}


def military_series_value(state: dict, year: int) -> float:
    values = [value for t, value in state["military_score_history"] if t <= year]
    return values[-1] if values else 0.0


def attention_series(state: dict, start: int, end: int) -> list[float]:
    return [value for year, value in state["attention_history"] if start <= year <= end]


def validate_scenario_structure(harness: ResearchHarness, scenario: dict) -> None:
    if not isinstance(scenario.get("duration_years"), int) or scenario["duration_years"] < 1:
        fail(f"scenario {scenario.get('id')}: invalid duration")
    ids = [row.get("id") for row in scenario.get("civilizations", [])]
    if len(ids) != len(set(ids)) or not ids:
        fail(f"scenario {scenario['id']}: civilization ids must be unique/nonempty")
    domain_ids = {row["id"] for row in harness.index.get("domains", [])}
    pressure_ids = set(load_json(harness.root / "pressure_dynamics.json").get("rules", {}))
    for cfg in scenario["civilizations"]:
        for domain_id, value in cfg.get("domain_attention", {}).items():
            if domain_id not in domain_ids:
                fail(f"scenario {scenario['id']}/{cfg['id']}: unknown domain {domain_id}")
            if not isinstance(value, (int, float)) or not 0 <= value <= 4:
                fail(f"scenario {scenario['id']}/{cfg['id']}: invalid domain attention {domain_id}={value}")
        for row in cfg.get("pressure_schedule", []):
            if row.get("pressure") not in pressure_ids:
                fail(f"scenario {scenario['id']}/{cfg['id']}: unknown pressure {row.get('pressure')}")
            if not 0 <= float(row.get("value", -1)) <= 100:
                fail(f"scenario {scenario['id']}/{cfg['id']}: invalid pressure value")
        profile = cfg.get("starting_reference_profile", scenario.get("starting_reference_profile", harness.defaults["starting_reference_profile"]))
        if profile not in harness.profiles:
            fail(f"scenario {scenario['id']}/{cfg['id']}: unknown starting profile {profile}")


def validate_divergence(harness: ResearchHarness, scenario: dict, states: dict) -> dict:
    ids = sorted(states)
    mature = {civ_id: mature_set(states[civ_id]) for civ_id in ids}
    distances = []
    for i, left in enumerate(ids):
        for right in ids[i + 1:]:
            distances.append((left, right, jaccard_distance(mature[left], mature[right])))
    minimum = min(value for _, _, value in distances)
    required = float(scenario["assertions"]["minimum_pairwise_mature_jaccard_distance"])
    if minimum < required:
        fail(f"{scenario['id']}: mature-tree divergence too low {minimum:.3f} < {required:.3f}; distances={distances}")

    common = set.intersection(*(mature[civ_id] for civ_id in ids))
    unique_counts = {civ_id: len(mature[civ_id] - set.union(*(mature[other] for other in ids if other != civ_id))) for civ_id in ids}
    required_unique = int(scenario["assertions"]["minimum_unique_mature_nodes_per_civilization"])
    if any(value < required_unique for value in unique_counts.values()):
        fail(f"{scenario['id']}: insufficient unique mature nodes {unique_counts}, required {required_unique}")

    max_fraction = float(scenario["assertions"]["maximum_catalog_fraction_mature_per_civilization"])
    fractions = {civ_id: len(nodes) / len(harness.nodes) for civ_id, nodes in mature.items()}
    if any(value > max_fraction for value in fractions.values()):
        fail(f"{scenario['id']}: civilization matured too much of catalog {fractions}")
    return {"min_jaccard_distance": round(minimum, 3), "unique_counts": unique_counts, "common_mature": len(common), "fractions": {k: round(v, 3) for k, v in fractions.items()}}


def validate_complacency(harness: ResearchHarness, scenario: dict, states: dict) -> dict:
    leader = states["established_hegemon"]
    challenger = states["rising_challenger"]
    initial_gap = military_series_value(leader, 0) - military_series_value(challenger, 0)
    pre_response_gap = military_series_value(leader, 180) - military_series_value(challenger, 180)
    if not pre_response_gap < initial_gap:
        fail(f"{scenario['id']}: challenger gap did not narrow before leader response ({initial_gap:.2f}->{pre_response_gap:.2f})")

    early_attention = attention_series(leader, 20, 120)
    late_attention = attention_series(leader, 220, 330)
    base_attention = float(leader["cfg"].get("domain_attention", {}).get("military", 0))
    if not early_attention or min(early_attention) >= base_attention:
        fail(f"{scenario['id']}: leader military attention never dropped below base {base_attention}")
    if not late_attention or max(late_attention) <= min(early_attention):
        fail(f"{scenario['id']}: leader military attention did not rise after catch-up observation")

    assertions = scenario["assertions"]
    for key in ("no_hidden_catchup_multiplier", "no_hidden_leader_penalty", "no_forced_parity"):
        if assertions.get(key) is not True:
            fail(f"{scenario['id']}: benchmark must explicitly assert {key}=true")
    return {
        "initial_gap": round(initial_gap, 2),
        "gap_at_180y": round(pre_response_gap, 2),
        "leader_attention_low": round(min(early_attention), 2),
        "leader_attention_late_high": round(max(late_attention), 2),
        "leader_final_military": round(military_series_value(leader, scenario["duration_years"]), 2),
        "challenger_final_military": round(military_series_value(challenger, scenario["duration_years"]), 2),
    }


def validate_foreign_value(harness: ResearchHarness, scenario: dict) -> dict:
    cfgs = {row["id"]: row for row in scenario["civilizations"]}
    holder = cfgs["machine_broker"]
    buyer = cfgs["metabolic_buyer"]
    asset = holder["foreign_asset_fixture"]
    if asset.get("operability") != "unusable":
        fail(f"{scenario['id']}: machine broker fixture must be directly unusable")
    holder_profile = harness.compose_profile(holder.get("starting_reference_profile", harness.defaults["starting_reference_profile"]))
    buyer_profile = harness.compose_profile(buyer.get("starting_reference_profile", harness.defaults["starting_reference_profile"]))
    holder_metabolic = "metabolic_biology" in holder_profile["traits"]
    buyer_metabolic = "metabolic_biology" in buyer_profile["traits"]
    if holder_metabolic or not buyer_metabolic:
        fail(f"{scenario['id']}: fixture no longer represents asymmetric biological compatibility")
    package_components = asset.get("package_components", [])
    if not package_components:
        fail(f"{scenario['id']}: foreign package must contain transferable value")
    if scenario["assertions"].get("transfer_does_not_set_native_technology_mature") is not True:
        fail(f"{scenario['id']}: must preserve anti-instant-unlock rule")
    return {"holder_metabolic": holder_metabolic, "buyer_metabolic": buyer_metabolic, "package_components": len(package_components), "brokerage_value_possible": True}


def validate_soak(harness: ResearchHarness, scenario: dict, states: dict) -> dict:
    assertions = scenario["assertions"]
    max_nodes = int(assertions["maximum_node_state_records_per_civilization"])
    max_recent = int(assertions["maximum_recent_event_records"])
    max_history = int(assertions["maximum_detailed_history_records"])
    summary = {}
    for civ_id, state in states.items():
        counts = {
            "node_states": len(state["states"]),
            "recent_events": len(state["recent_events"]),
            "detailed_history": len(state["detailed_history"]),
            "mature": len(mature_set(state)),
        }
        if counts["node_states"] > max_nodes or counts["node_states"] > len(harness.nodes):
            fail(f"{scenario['id']}/{civ_id}: node state growth unbounded {counts['node_states']}")
        if counts["recent_events"] > max_recent:
            fail(f"{scenario['id']}/{civ_id}: recent event buffer exceeded {max_recent}")
        if counts["detailed_history"] > max_history:
            fail(f"{scenario['id']}/{civ_id}: detailed history exceeded {max_history}")
        summary[civ_id] = counts
    if assertions.get("static_catalog_copies_per_civilization") != 0:
        fail(f"{scenario['id']}: benchmark contract must forbid per-civilization static catalog copies")
    if assertions.get("reconstructible_index_copies_per_civilization") != 0:
        fail(f"{scenario['id']}: benchmark contract must forbid per-civilization index copies")
    if assertions.get("UI_layout_cache_persisted_in_save") is not False:
        fail(f"{scenario['id']}: UI layout cache must not persist in save")
    return summary


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else "data/research/v1")
    scenario_path = root / "research_benchmark_scenarios.json"
    if not scenario_path.is_file():
        fail(f"missing {scenario_path}")
    scenarios = load_json(scenario_path)
    if scenarios.get("catalog_id") != load_json(root / "index.json").get("catalog_id"):
        fail("benchmark catalog_id does not match research index")

    rows = scenarios.get("scenarios", [])
    ids = [row.get("id") for row in rows]
    if len(ids) != len(set(ids)) or None in ids or not ids:
        fail("benchmark scenario ids must be unique and nonempty")
    required = {
        "divergent_same_origin_500y",
        "military_complacency_and_response_350y",
        "foreign_technology_asymmetric_value",
        "research_state_soak_1000y",
    }
    if not required.issubset(set(ids)):
        fail(f"missing required benchmark scenarios {sorted(required - set(ids))}")

    harness = ResearchHarness(root, scenarios)
    results = {}
    for scenario in rows:
        validate_scenario_structure(harness, scenario)
        if scenario["id"] == "foreign_technology_asymmetric_value":
            results[scenario["id"]] = validate_foreign_value(harness, scenario)
            continue
        states = harness.run_civilizations(scenario)
        if scenario["id"] == "divergent_same_origin_500y":
            results[scenario["id"]] = validate_divergence(harness, scenario, states)
        elif scenario["id"] == "military_complacency_and_response_350y":
            results[scenario["id"]] = validate_complacency(harness, scenario, states)
        elif scenario["id"] == "research_state_soak_1000y":
            results[scenario["id"]] = validate_soak(harness, scenario, states)

    print("research benchmark OK")
    for scenario_id in sorted(results):
        print(f"  {scenario_id}: {json.dumps(results[scenario_id], sort_keys=True)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
