#pragma once

#include <stellar/core/colony_economy.hpp>
#include <stellar/core/construction_state.hpp>
#include <stellar/core/surface_economy.hpp>
#include <stellar/engine/automation.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

// Stellar Continuum's automation domains — independently configurable
// slices of routine empire management. String keys match the engine
// AutomationController's domain names and are the persisted form.
enum class AutomationDomain {
    Empire = 0,
    Colonies,
    Construction,
    Economy,
    Logistics,
    Research,
    Exploration,
    Colonization,
    Fleets,
    Military,
    Shipbuilding,
    Diplomacy,
};

[[nodiscard]] std::string_view
automation_domain_name(AutomationDomain domain) noexcept;
[[nodiscard]] std::optional<AutomationDomain>
automation_domain_from_name(std::string_view name) noexcept;

// Default per-domain modes applied to civilizations that have no
// explicit policy. Everything is opt-in: a default-constructed
// coordinator changes NO simulation behavior (parity with earlier
// campaign semantics is preserved); a host enables domains explicitly
// through defaults() or set_domain_policy(). Player delegation is
// never silent — Advisory only journals proposals until configured.
struct AutomationDefaults {
    stellar::engine::AutomationMode ai_colonies{
        stellar::engine::AutomationMode::Off};
    stellar::engine::AutomationMode player_colonies{
        stellar::engine::AutomationMode::Advisory};
    stellar::engine::AutomationMode player_construction{
        stellar::engine::AutomationMode::Off};
    // Manual locks created when an operator acts on a target last this
    // many days by default — the override hysteresis window.
    double default_override_lock_days{30.0};
};

// One colony's evaluated situation — diagnostics for the automation UI
// and tests; the planner produces it before scoring.
struct ColonyAutomationReport {
    int colony_id{};
    double power_supply{}, power_demand{};
    double food_capacity_millions{}, water_capacity_millions{},
        housing_capacity_millions{}, population_millions{};
    double employment_rate{}, unemployed_millions{};
    double stored_power_days{}, cargo_transfer_per_day{};
    std::string limiting_supply;
    int free_slots{};
    std::vector<std::string> issues; // ordered by severity
};

[[nodiscard]] ColonyAutomationReport
assess_colony_automation(std::span<const PlanetaryBody> bodies,
                         const Colony &colony);
// Caller-shared index form: identical report, the caller performs the
// colony->body resolution once for many colonies.
[[nodiscard]] ColonyAutomationReport
assess_colony_automation(const SettlementBodyIndex &bodies,
                         const Colony &colony);

// The per-civilization automation coordinator: owns one engine
// AutomationController per civilization and drives domain planners
// through the canonical command paths. Driven by the coordinator's
// `automatic_orders` phase — same cadence, same world views.
//
// Determinism contract: civilizations and colonies iterate in
// ascending id order, candidate lists are rebuilt identically from
// state each evaluation, and all commits flow through the same
// assess→commit calls a human command would use. Nothing reads hidden
// state a civilization could not know — colony planners read only the
// acting civilization's own colonies/economy.
class CivilizationAutomationCoordinator {
public:
    explicit CivilizationAutomationCoordinator(
        AutomationDefaults defaults = {});

    // --- configuration -------------------------------------------------
    [[nodiscard]] AutomationDefaults &defaults() noexcept;
    void set_domain_policy(
        int civilization_id,
        stellar::engine::AutomationDomainPolicy policy);
    [[nodiscard]] const stellar::engine::AutomationDomainPolicy *
    domain_policy(int civilization_id, AutomationDomain domain) const;

    // --- operator control ----------------------------------------------
    // Called by the same handlers that dispatch a human command acting
    // on an automated target: locks the target so automation respects
    // the intervention instead of undoing it next tick.
    void record_operator_override(int civilization_id,
                                  AutomationDomain domain,
                                  std::string_view target,
                                  double lock_days = -1.0);
    void clear_override(int civilization_id, AutomationDomain domain,
                        std::string_view target);

    // --- per-civilization access ----------------------------------------
    [[nodiscard]] stellar::engine::AutomationController &
    controller(int civilization_id);
    [[nodiscard]] const stellar::engine::AutomationController *
    controller(int civilization_id) const;
    [[nodiscard]] std::vector<int> civilizations() const; // sorted

    // --- stepping -------------------------------------------------------
    // The automatic_orders phase entry point: refreshes candidates for
    // every configured domain and lets each civilization's controller
    // decide. `world` is the canonical construction world (commands
    // flow through its assess→commit path); `phase_days` is the
    // elapsed simulation span being integrated. When `world.body_index`
    // is unset a local catalog index is built for the call.
    void advance(ConstructionWorld world, double phase_days);

    // Monotonic automation clock — days accumulated across advances;
    // persisted and used for cooldowns/locks.
    [[nodiscard]] double automation_day() const noexcept;

    // --- persistence -----------------------------------------------------
    struct CivilizationState {
        int civilization_id{};
        stellar::engine::AutomationController::State controller;
    };
    struct State {
        std::uint32_t version{1};
        double automation_day{};
        AutomationDefaults defaults;
        std::vector<CivilizationState> civilizations; // sorted by id
    };
    [[nodiscard]] State capture_state() const;
    void restore_state(const State &state);

private:
    void ensure_civilization(int civilization_id, bool uses_ai);
    void apply_default_modes(int civilization_id, bool uses_ai);
    void refresh_colony_domain(ConstructionWorld &world,
                               int civilization_id, bool uses_ai);
    void refresh_construction_domain(ConstructionWorld &world,
                                     int civilization_id, bool uses_ai);

    AutomationDefaults defaults_;
    std::map<int, stellar::engine::AutomationController> controllers_;
    double automation_day_{};
};

} // namespace stellar::core
