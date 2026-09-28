#include <stellar/core/civilization_automation.hpp>

#include <stellar/core/civilization_control.hpp>
#include <stellar/core/construction_projects.hpp>
#include <stellar/core/settlement_body_index.hpp>
#include <stellar/core/surface_construction.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <set>
#include <span>
#include <utility>

namespace stellar::core {
namespace {

constexpr std::string_view domain_keys[] = {
    "empire",      "colonies",   "construction", "economy",
    "logistics",   "research",   "exploration",  "colonization",
    "fleets",      "military",   "shipbuilding", "diplomacy",
};

std::string colony_target(int colony_id) {
    return "colony:" + std::to_string(colony_id);
}

const Civilization *civ_of(std::span<const Civilization> civs, int id) {
    const auto it = std::find_if(civs.begin(), civs.end(),
                                 [&](const auto &c) { return c.id == id; });
    return it == civs.end() ? nullptr : &*it;
}

const CivilizationEconomy *economy_of(
    std::span<const CivilizationEconomy> economies, int id) {
    const auto it = std::find_if(
        economies.begin(), economies.end(),
        [&](const auto &e) { return e.civilization_id == id; });
    return it == economies.end() ? nullptr : &*it;
}

const ConstructionState *construction_of(
    std::span<const ConstructionState> states, int id) {
    const auto it = std::find_if(
        states.begin(), states.end(),
        [&](const auto &s) { return s.civilization_id == id; });
    return it == states.end() ? nullptr : &*it;
}

// Free building slots in ascending order — the deterministic slot
// choice shared by every placement candidate.
std::vector<int> free_slots(const Colony &colony) {
    const int capacity = surface_building_capacity(colony);
    if (capacity <= 0) return {};
    const auto occupied_map = planetary_building_slots(colony);
    std::set<int> occupied;
    for (const auto &[_, slot] : occupied_map) occupied.insert(slot);
    std::vector<int> out;
    for (int slot = 0; slot < capacity; ++slot)
        if (!occupied.contains(slot)) out.push_back(slot);
    return out;
}

} // namespace

std::string_view
automation_domain_name(AutomationDomain domain) noexcept {
    const auto index = static_cast<std::size_t>(domain);
    if (index >= std::size(domain_keys)) return "empire";
    return domain_keys[index];
}

std::optional<AutomationDomain>
automation_domain_from_name(std::string_view name) noexcept {
    for (std::size_t i = 0; i < std::size(domain_keys); ++i)
        if (domain_keys[i] == name)
            return static_cast<AutomationDomain>(i);
    return std::nullopt;
}

namespace {
ColonyAutomationReport
assess_colony_automation_impl(const auto &bodies,
                              const Colony &colony) {
    ColonyAutomationReport report;
    report.colony_id = colony.id;
    report.population_millions = colony.population_millions;

    const auto output = surface_colony_output(colony);
    report.power_supply = output.supply;
    report.power_demand = output.demand;
    report.stored_power_days = output.stored_power_days;
    report.cargo_transfer_per_day = output.cargo_transfer_capacity_per_day;

    const auto capacity = colony_sustenance_capacity(
        bodies, colony, surface_sustenance_projection(output));
    report.food_capacity_millions = capacity.food_capacity_millions;
    report.water_capacity_millions = capacity.water_capacity_millions;
    report.housing_capacity_millions = capacity.housing_capacity_millions;
    report.limiting_supply = capacity.limiting_supply;

    const auto labor = colony_labor(colony);
    report.employment_rate = labor.employment_rate;
    report.unemployed_millions = labor.unemployed_population_millions;

    report.free_slots = static_cast<int>(free_slots(colony).size());

    if (output.demand > output.supply)
        report.issues.push_back("power deficit");
    if (capacity.food_capacity_millions <
        colony.population_millions)
        report.issues.push_back("food support deficit");
    if (capacity.water_capacity_millions <
        colony.population_millions)
        report.issues.push_back("water support deficit");
    if (capacity.housing_capacity_millions <
        colony.population_millions)
        report.issues.push_back("housing deficit");
    for (const auto &building : colony.surface_buildings) {
        if (building.is_complete && building.condition < .5)
            report.issues.push_back("damaged building");
        if (building.is_complete && !building.is_enabled &&
            building.condition > minimum_operational_condition)
            report.issues.push_back("disabled building");
        if (!building.is_complete) {
            // Under construction — do not double-report.
        }
    }
    if (colony.stored_extracted_materials > 0.0 &&
        output.cargo_transfer_capacity_per_day <= 0.0)
        report.issues.push_back("stockpile without cargo handling");
    return report;
}
}
ColonyAutomationReport
assess_colony_automation(std::span<const PlanetaryBody> bodies,
                         const Colony &colony) {
    return assess_colony_automation_impl(bodies, colony);
}
ColonyAutomationReport
assess_colony_automation(const SettlementBodyIndex &bodies,
                         const Colony &colony) {
    return assess_colony_automation_impl(bodies, colony);
}

CivilizationAutomationCoordinator::CivilizationAutomationCoordinator(
    AutomationDefaults defaults)
    : defaults_(defaults) {}

AutomationDefaults &CivilizationAutomationCoordinator::defaults()
    noexcept {
    return defaults_;
}

void CivilizationAutomationCoordinator::set_domain_policy(
    int civilization_id, stellar::engine::AutomationDomainPolicy policy) {
    controllers_[civilization_id].set_domain_policy(std::move(policy));
}

const stellar::engine::AutomationDomainPolicy *
CivilizationAutomationCoordinator::domain_policy(
    int civilization_id, AutomationDomain domain) const {
    const auto it = controllers_.find(civilization_id);
    if (it == controllers_.end()) return nullptr;
    return it->second.domain_policy(automation_domain_name(domain));
}

void CivilizationAutomationCoordinator::record_operator_override(
    int civilization_id, AutomationDomain domain, std::string_view target,
    double lock_days) {
    controllers_[civilization_id].record_operator_override(
        automation_domain_name(domain), target, automation_day_,
        lock_days >= 0.0 ? lock_days
                         : defaults_.default_override_lock_days);
}

void CivilizationAutomationCoordinator::clear_override(
    int civilization_id, AutomationDomain domain,
    std::string_view target) {
    const auto it = controllers_.find(civilization_id);
    if (it == controllers_.end()) return;
    it->second.clear_override(automation_domain_name(domain), target);
}

stellar::engine::AutomationController &
CivilizationAutomationCoordinator::controller(int civilization_id) {
    return controllers_[civilization_id];
}

const stellar::engine::AutomationController *
CivilizationAutomationCoordinator::controller(
    int civilization_id) const {
    const auto it = controllers_.find(civilization_id);
    return it == controllers_.end() ? nullptr : &it->second;
}

std::vector<int>
CivilizationAutomationCoordinator::civilizations() const {
    std::vector<int> out;
    out.reserve(controllers_.size());
    for (const auto &[id, _] : controllers_) out.push_back(id);
    return out;
}

double CivilizationAutomationCoordinator::automation_day()
    const noexcept {
    return automation_day_;
}

CivilizationAutomationCoordinator::State
CivilizationAutomationCoordinator::capture_state() const {
    State state;
    state.automation_day = automation_day_;
    state.defaults = defaults_;
    for (const auto &[id, controller] : controllers_)
        state.civilizations.push_back(
            {id, controller.capture_state()});
    return state;
}

void CivilizationAutomationCoordinator::restore_state(
    const State &state) {
    controllers_.clear();
    automation_day_ = state.automation_day;
    defaults_ = state.defaults;
    for (const auto &entry : state.civilizations) {
        auto [it, inserted] =
            controllers_.try_emplace(entry.civilization_id);
        if (!inserted)
            throw std::invalid_argument(
                "Duplicate automation civilization id.");
        // Validates mode ranges, journal sequence order and history
        // bounds before the state enters the live coordinator.
        it->second.restore_state(entry.controller);
    }
}

void CivilizationAutomationCoordinator::ensure_civilization(
    int civilization_id, bool uses_ai) {
    (void)controllers_[civilization_id];
    apply_default_modes(civilization_id, uses_ai);
}

// Idempotent: defaults only fill domains with NO policy, so explicit
// operator configuration and restored state always win.
void CivilizationAutomationCoordinator::apply_default_modes(
    int civilization_id, bool uses_ai) {
    auto &controller = controllers_[civilization_id];
    using stellar::engine::AutomationDomainPolicy;
    const std::string colonies_domain =
        std::string(automation_domain_name(AutomationDomain::Colonies));
    if (!controller.domain_policy(colonies_domain)) {
        AutomationDomainPolicy colonies;
        colonies.domain = colonies_domain;
        colonies.mode = uses_ai ? defaults_.ai_colonies
                                : defaults_.player_colonies;
        // Affordability-blocked candidates score 0 — the floor keeps a
        // fully-blocked refresh from committing a no-op.
        colonies.min_utility = 0.05;
        controller.set_domain_policy(std::move(colonies));
    }
    if (uses_ai) {
        // AI civilizations run themselves: colonies are the shipped
        // production domain; every other domain keeps its existing
        // subsystem-driven behavior (no policy → Off → not double-run).
        return;
    }
    const std::string construction_domain = std::string(
        automation_domain_name(AutomationDomain::Construction));
    if (!controller.domain_policy(construction_domain)) {
        AutomationDomainPolicy construction;
        construction.domain = construction_domain;
        construction.mode = defaults_.player_construction;
        construction.min_utility = 0.05;
        controller.set_domain_policy(std::move(construction));
    }
}

void CivilizationAutomationCoordinator::advance(
    ConstructionWorld world, double phase_days) {
    if (phase_days < 0.0 || !std::isfinite(phase_days))
        return;
    automation_day_ += phase_days;
    // Colony refresh resolves each colony's body out of the catalog;
    // share one catalog index across every civilization instead of
    // rescanning `world.bodies` per colony.
    std::optional<SettlementBodyIndex> local_body_index;
    if (!world.body_index) {
        local_body_index.emplace(world.bodies);
        world.body_index = &*local_body_index;
    }
    for (const auto &civilization : world.civilizations) {
        if (civilization.is_seeded_ancient) continue;
        const bool uses_ai =
            civilization_uses_ai(civilization, world.control);
        ensure_civilization(civilization.id, uses_ai);
        auto &controller = controllers_[civilization.id];
        // Rebuild this tick's candidates — scores read live state, so
        // registration is cheap refresh, not retained plans.
        controller.clear_actions(
            automation_domain_name(AutomationDomain::Colonies));
        refresh_colony_domain(world, civilization.id, uses_ai);
        (void)controller.decide(
            automation_domain_name(AutomationDomain::Colonies),
            automation_day_);
        controller.clear_actions(
            automation_domain_name(AutomationDomain::Construction));
        refresh_construction_domain(world, civilization.id, uses_ai);
        (void)controller.decide(
            automation_domain_name(AutomationDomain::Construction),
            automation_day_);
    }
}

void CivilizationAutomationCoordinator::refresh_colony_domain(
    ConstructionWorld &world, int civilization_id, bool uses_ai) {
    auto &controller = controllers_[civilization_id];
    const std::string domain =
        std::string(automation_domain_name(AutomationDomain::Colonies));
    if (controller.mode(domain) == stellar::engine::AutomationMode::Off)
        return;

    const auto *economy = economy_of(world.economies, civilization_id);
    if (!economy) return;
    const double credits = economy->credits;
    const double reserve =
        controller.constraint_value(domain, "reserve_credits", 10.0);

    for (const auto &colony : world.colonies) {
        if (colony.civilization_id != civilization_id) continue;
        if (colony.surface_hub_level <= 0) continue;
        // Placement requires a surveyed solid-surface body — the same
        // precondition the canonical assess path enforces.
        const PlanetaryBody *body =
            colony.planetary_body_id
                ? world.body_index->last_body_with_id(
                      *colony.planetary_body_id)
                : nullptr;
        if (!body || !body->environment.has_solid_surface) continue;
        const std::string target = colony_target(colony.id);
        if (controller.target_locked(domain, target, automation_day_))
            continue;

        const auto report =
            assess_colony_automation(*world.body_index, colony);
        const auto output = surface_colony_output(colony);
        const auto slots = free_slots(colony);

        const auto add_placement =
            [&](std::string_view type_id, double severity,
                std::string reason) {
                if (slots.empty()) return;
                const auto *definition = find_surface_building(type_id);
                if (!definition ||
                    !surface_available_for_settlement(colony, *definition))
                    return;
                // Do not double-queue a type already under construction
                // or upgrade at this colony.
                for (const auto &building : colony.surface_buildings)
                    if (!building.is_complete &&
                        building.type_id == type_id)
                        return;
                const int slot = slots.front();
                // Same price a human quote would show — the environment
                // multiplier and away-rounding are part of the contract.
                const double cost = surface_authorization_cost(
                    world.read(), colony, *definition);
                const bool affordable =
                    credits - cost >= reserve;
                stellar::engine::UtilityAction action;
                action.id = "colony:" + std::to_string(colony.id) +
                            ":place:" + std::string(type_id);
                action.domain = domain;
                action.target = target;
                action.routine = true;
                action.cooldown_days = 2.0;
                action.score = [severity, affordable]() {
                    return affordable ? severity : 0.0;
                };
                action.commit = [world, civilization_id,
                                 colony_id = colony.id,
                                 type = std::string(type_id), slot]() {
                    const auto quote = assess_planetary_building_slot(
                        world.read(), civilization_id, colony_id,
                        slot, type);
                    if (quote.accepted)
                        (void)commit_surface_building_placement(world,
                                                                quote);
                };
                auto explain = [reason]() { return reason; };
                auto constraints = [cost, reserve, credits]() {
                    return std::vector<std::string>{
                        std::string("treasury_reserve: ") +
                        (credits - cost >= reserve ? "satisfied"
                                                   : "blocked")};
                };
                controller.add_action(std::move(action),
                                      std::move(explain),
                                      std::move(constraints));
            };

        // Crisis-first ordering, mirroring the mission's priority:
        // essential services → power → food → water → housing →
        // repair → logistics → growth.
        const double power_deficit =
            std::max(0.0, output.demand - output.supply);
        if (power_deficit > 0.0) {
            const double severity =
                std::min(0.95, 0.6 + power_deficit * 0.08);
            add_placement("power_generator", severity,
                          "Power deficit projected: demand exceeds "
                          "supply.");
        }
        if (report.food_capacity_millions <
            report.population_millions) {
            add_placement("controlled_agriculture", 0.85,
                          "Food support below population needs.");
        }
        if (report.water_capacity_millions <
            report.population_millions) {
            add_placement("water_reclamation", 0.84,
                          "Water support below population needs.");
        }
        if (report.housing_capacity_millions <
            report.population_millions) {
            add_placement("habitat_complex", 0.80,
                          "Housing capacity below population needs.");
        }

        // Damaged completed buildings → canonical repair command.
        for (const auto &building : colony.surface_buildings) {
            if (!building.is_complete || building.condition >= .5)
                continue;
            stellar::engine::UtilityAction action;
            action.id = "colony:" + std::to_string(colony.id) +
                        ":repair:" + std::to_string(building.id);
            action.domain = domain;
            action.target = target;
            action.routine = true;
            action.cooldown_days = 2.0;
            const double severity = 0.75 - building.condition * 0.4;
            action.score = [severity]() { return severity; };
            action.commit = [world, civilization_id,
                             colony_id = colony.id,
                             building_id = building.id]() {
                (void)repair_surface_building(world, civilization_id,
                                              colony_id, building_id);
            };
            controller.add_action(std::move(action), [] {
                return std::string("Building condition below "
                                   "maintenance threshold.");
            });
        }

        // Disabled-but-serviceable buildings → re-enable (only if the
        // grid can cover their demand — re-powering into a deficit is
        // not routine).
        for (const auto &building : colony.surface_buildings) {
            if (!building.is_complete || building.is_enabled ||
                building.condition <= minimum_operational_condition)
                continue;
            const auto *definition =
                find_surface_building(building.type_id);
            const double demand =
                definition ? definition->power_demand : 0.0;
            if (power_deficit > 0.0 && demand > 0.0) continue;
            stellar::engine::UtilityAction action;
            action.id = "colony:" + std::to_string(colony.id) +
                        ":enable:" + std::to_string(building.id);
            action.domain = domain;
            action.target = target;
            action.routine = true;
            action.cooldown_days = 2.0;
            action.score = []() { return 0.62; };
            action.commit = [world, civilization_id,
                             colony_id = colony.id,
                             building_id = building.id]() {
                (void)set_surface_building_enabled(world,
                                                   civilization_id,
                                                   colony_id,
                                                   building_id, true);
            };
            controller.add_action(std::move(action), [] {
                return std::string(
                    "Serviceable building disabled; re-enabling.");
            });
        }

        // Stockpile with no cargo handling → cargo terminal.
        if (colony.stored_extracted_materials > 0.0 &&
            output.cargo_transfer_capacity_per_day <= 0.0) {
            add_placement("cargo_terminal", 0.45,
                          "Extracted stockpile with no cargo "
                          "handling.");
        }

        // Growth: unemployed workforce → productive buildings shaped
        // by the domain's economic policy knobs.
        if (report.unemployed_millions > 0.01 &&
            power_deficit <= 0.0) {
            const double industry_w = controller.policy_knob(
                domain, "industry_weight", 1.0);
            const double research_w = controller.policy_knob(
                domain, "research_weight", 1.0);
            const double trade_w = controller.policy_knob(
                domain, "trade_weight", 1.0);
            add_placement("fabricator", 0.35 * industry_w,
                          "Unemployed workforce; expanding "
                          "industry.");
            add_placement("science_lab", 0.35 * research_w,
                          "Unemployed workforce; expanding "
                          "research.");
            add_placement("trade_hub", 0.30 * trade_w,
                          "Unemployed workforce; expanding "
                          "trade.");
        }

        // Capacity exhausted but unmet demand → hub upgrade widens the
        // slot count (canonical upgrade path).
        if (slots.empty() && !report.issues.empty()) {
            stellar::engine::UtilityAction action;
            action.id = "colony:" + std::to_string(colony.id) +
                        ":upgrade_hub";
            action.domain = domain;
            action.target = target;
            action.routine = true;
            action.cooldown_days = 10.0;
            action.score = []() { return 0.5; };
            action.commit = [world, civilization_id,
                             colony_id = colony.id]() {
                (void)upgrade_surface_hub(world, civilization_id,
                                          colony_id);
            };
            controller.add_action(std::move(action), [] {
                return std::string("Building slots exhausted; "
                                   "expanding surface hub.");
            });
        }
    }
    (void)uses_ai;
}

void CivilizationAutomationCoordinator::refresh_construction_domain(
    ConstructionWorld &world, int civilization_id, bool uses_ai) {
    auto &controller = controllers_[civilization_id];
    const std::string domain = std::string(
        automation_domain_name(AutomationDomain::Construction));
    const auto mode = controller.mode(domain);
    if (mode == stellar::engine::AutomationMode::Off) return;
    // AI empire construction stays on the existing
    // ensure_automatic_construction_orders path — this domain is the
    // player-delegation surface; double-queuing AI orders would fight
    // the shipped behavior.
    if (uses_ai) return;

    const auto *civilization =
        civ_of(world.civilizations, civilization_id);
    const auto *economy = economy_of(world.economies, civilization_id);
    const auto *state = construction_of(world.construction,
                                        civilization_id);
    if (!civilization || !economy || !state) return;
    if (state->active_project_id || !state->queued_projects.empty())
        return;

    const double reserve = controller.constraint_value(
        domain, "reserve_credits", 10.0);
    const double credits = economy->credits;

    const auto science_w =
        controller.policy_knob(domain, "science_weight", 1.0);
    const auto industry_w =
        controller.policy_knob(domain, "industry_weight", 1.0);
    const auto orbital_w =
        controller.policy_knob(domain, "orbital_weight", 1.0);
    const auto ftl_w = controller.policy_knob(domain, "ftl_weight", 1.0);

    for (const auto &project :
         available_construction_projects(world.read(),
                                         civilization_id)) {
        // Mirrors ensure_automatic_construction_orders' trait weighting
        // so delegated choice matches the shipped AI shape, scaled by
        // the player's policy knobs.
        double base = 1.0;
        switch (project.category) {
        case ConstructionCategory::Science:
            base = 1.0 +
                   civilization->traits.scientific_curiosity * .9;
            base *= science_w;
            break;
        case ConstructionCategory::Industry:
            base = 1.0 + civilization->traits.greed * .55 +
                   civilization->traits.territoriality * .2;
            base *= industry_w;
            break;
        case ConstructionCategory::Orbital:
            base = 1.15 + civilization->traits.territoriality * .3 +
                   civilization->traits.scientific_curiosity * .2;
            base *= orbital_w;
            break;
        case ConstructionCategory::Ftl:
            base = 1.3 + civilization->traits.scientific_curiosity * .4 +
                   civilization->traits.aggression * .2;
            base *= ftl_w;
            break;
        }
        const double cost = project.credit_cost;
        const bool affordable = credits - cost >= reserve;

        stellar::engine::UtilityAction action;
        action.id = "empire:project:" + project.id;
        action.domain = domain;
        action.target = "empire:" + std::to_string(civilization_id);
        action.routine = true;
        action.cooldown_days = 5.0;
        action.score = [base, affordable]() {
            return affordable ? base * 0.4 : 0.0;
        };
        action.commit = [world, civilization_id,
                         id = project.id]() {
            (void)start_construction_project(world, civilization_id,
                                             id);
        };
        const std::string name = project.name;
        controller.add_action(
            std::move(action),
            [name]() { return "Queue " + name + "."; },
            [cost, reserve, credits]() {
                return std::vector<std::string>{
                    std::string("treasury_reserve: ") +
                    (credits - cost >= reserve ? "satisfied"
                                               : "blocked")};
            });
    }
}

} // namespace stellar::core
