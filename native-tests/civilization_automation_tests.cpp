#include <stellar/core/civilization_automation.hpp>
#include <stellar/core/construction_projects.hpp>
#include <stellar/core/surface_construction.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

using namespace stellar::core;
using stellar::engine::AutomationMode;

constexpr int kAiCiv = 1;
constexpr int kPlayerCiv = 2;
constexpr int kBodyId = 10;
constexpr int kAiColony = 100;
constexpr int kPlayerColony = 200;

// A real ConstructionWorld fixture: one AI and one player civilization,
// each with a harsh-environment colony short on every life support
// need and credits enough to act. Commits flow through the same
// assess→commit commands a human order uses.
struct Fixture {
    std::vector<Civilization> civilizations;
    std::vector<PlanetaryBody> bodies;
    std::vector<ConstructionState> construction;
    std::vector<Colony> colonies;
    std::vector<CivilizationEconomy> economies;
    std::vector<CivilizationConstructionCapabilities> capabilities;

    ConstructionWorld world() {
        return {civilizations, bodies,    construction, colonies,
                economies,      capabilities};
    }
};

PlanetaryBody harsh_body() {
    PlanetaryBody b;
    b.id = kBodyId;
    b.system_id = 1;
    b.name = "Cinder";
    b.kind = PlanetaryBodyKind::Planet;
    b.radius_earth = 0.8;
    b.mass_earth = 0.6;
    b.environment = {0.5,
                     420.0,
                     0.0,
                     PlanetaryAtmosphereRegime::Vacuum,
                     PlanetarySolventRegime::None,
                     0.2,
                     false,
                     true /* has_solid_surface */};
    return b;
}

Colony sparse_colony(int id, int civilization_id) {
    Colony c;
    c.id = id;
    c.civilization_id = civilization_id;
    c.system_id = 1;
    c.planetary_body_id = kBodyId;
    c.name = "Test Colony " + std::to_string(id);
    // Above the sealed-infrastructure sustenance baseline (~500M), so
    // the colony genuinely runs food/water/housing deficits.
    c.population_millions = 600.0;
    c.surface_hub_level = 1;
    return c;
}

Fixture make_fixture() {
    Fixture f;
    Civilization ai;
    ai.id = kAiCiv;
    ai.name = "Automata";
    ai.is_player = false;
    ai.development_stage = CivilizationDevelopmentStage::WarpCapable;
    f.civilizations.push_back(ai);

    Civilization player;
    player.id = kPlayerCiv;
    player.name = "Humans";
    player.is_player = true;
    player.development_stage = CivilizationDevelopmentStage::WarpCapable;
    f.civilizations.push_back(player);

    f.bodies.push_back(harsh_body());
    f.colonies.push_back(sparse_colony(kAiColony, kAiCiv));
    f.colonies.push_back(sparse_colony(kPlayerColony, kPlayerCiv));

    CivilizationEconomy ai_economy;
    ai_economy.civilization_id = kAiCiv;
    ai_economy.credits = 5000.0;
    f.economies.push_back(ai_economy);
    CivilizationEconomy player_economy;
    player_economy.civilization_id = kPlayerCiv;
    player_economy.credits = 5000.0;
    f.economies.push_back(player_economy);

    f.construction = seed_construction(f.civilizations);
    return f;
}

const Colony& colony_of(const Fixture& f, int id) {
    for (const auto& c : f.colonies)
        if (c.id == id) return c;
    throw std::runtime_error("missing colony");
}

std::vector<std::string> building_types(const Colony& c) {
    std::vector<std::string> out;
    out.reserve(c.surface_buildings.size());
    for (const auto& b : c.surface_buildings) out.push_back(b.type_id);
    return out;
}

const stellar::engine::AutomationController* controller_of(
    const CivilizationAutomationCoordinator& coordinator, int id) {
    return coordinator.controller(id);
}

// The production default is opt-in (Off); the tests exercise the same
// enablement path a host uses.
CivilizationAutomationCoordinator automatic_colonies() {
    CivilizationAutomationCoordinator coordinator;
    coordinator.defaults().ai_colonies =
        stellar::engine::AutomationMode::Automatic;
    return coordinator;
}

} // namespace

int main() {
    // --- AI civilization automates a real colony through canonical commands ------
    {
        Fixture f = make_fixture();
        auto automation = automatic_colonies();
        automation.advance(f.world(), 1.0);
        const auto& colony = colony_of(f, kAiColony);
        check(!colony.surface_buildings.empty(),
              "ai colony received a canonical placement");
        if (!colony.surface_buildings.empty()) {
            const auto& placed = colony.surface_buildings.back();
            check(!placed.is_complete,
                  "placement enters construction, not completion");
            check(placed.slot_index.has_value(), "placement took a slot");
        }
        check(f.economies.front().credits < 5000.0,
              "placement charged treasury");
        const auto* controller = controller_of(automation, kAiCiv);
        check(controller != nullptr, "ai controller exists");
        check(controller && !controller->journal().empty(),
              "commit journaled");
        if (controller && !controller->journal().empty()) {
            const auto& entry = controller->journal().back();
            check(entry.executed, "journal records execution");
            check(entry.domain == "colonies", "journal records domain");
            check(!entry.reason.empty(), "journal carries a reason");
            check(entry.mode == AutomationMode::Automatic,
                  "journal records mode");
        }
    }

    // --- Player civilization defaults to Advisory: proposes, never commits ------
    {
        Fixture f = make_fixture();
        auto automation = automatic_colonies();
        automation.advance(f.world(), 1.0);
        const auto& colony = colony_of(f, kPlayerColony);
        check(colony.surface_buildings.empty(),
              "advisory player colony untouched");
        const auto* controller = controller_of(automation, kPlayerCiv);
        check(controller != nullptr, "player controller exists");
        check(controller && !controller->journal().empty(),
              "advisory journaled a proposal");
        if (controller && !controller->journal().empty()) {
            const auto& entry = controller->journal().back();
            check(!entry.executed, "proposal not executed");
            check(entry.mode == AutomationMode::Advisory,
                  "advisory mode journaled");
            check(!entry.action_id.empty(), "proposal names an action");
        }
        check(f.economies[1].credits == 5000.0,
              "advisory charged nothing");
    }

    // --- Delegation: set the player domain to Automatic and it acts -------------
    {
        Fixture f = make_fixture();
        auto automation = automatic_colonies();
        stellar::engine::AutomationDomainPolicy policy;
        policy.domain =
            std::string(automation_domain_name(AutomationDomain::Colonies));
        policy.mode = AutomationMode::Automatic;
        automation.set_domain_policy(kPlayerCiv, std::move(policy));
        automation.advance(f.world(), 1.0);
        check(!colony_of(f, kPlayerColony).surface_buildings.empty(),
              "delegated player colony built");
    }

    // --- Operator override lock suppresses automation on the target --------------
    {
        Fixture f = make_fixture();
        auto automation = automatic_colonies();
        automation.record_operator_override(kAiCiv, AutomationDomain::Colonies,
                                            "colony:" +
                                                std::to_string(kAiColony),
                                            30.0);
        automation.advance(f.world(), 1.0);
        check(colony_of(f, kAiColony).surface_buildings.empty(),
              "locked colony not automated");
        automation.clear_override(kAiCiv, AutomationDomain::Colonies,
                                  "colony:" + std::to_string(kAiColony));
        automation.advance(f.world(), 1.0);
        check(!colony_of(f, kAiColony).surface_buildings.empty(),
              "cleared lock resumes automation");
    }

    // --- Lock expiry: short lock re-opens without manual clear ----------------------
    {
        Fixture f = make_fixture();
        auto automation = automatic_colonies();
        automation.record_operator_override(kAiCiv, AutomationDomain::Colonies,
                                            "colony:" +
                                                std::to_string(kAiColony),
                                            1.5);
        automation.advance(f.world(), 1.0); // day 1 — locked
        check(colony_of(f, kAiColony).surface_buildings.empty(),
              "lock active during window");
        automation.advance(f.world(), 1.0); // day 2 — expired (1.5)
        check(!colony_of(f, kAiColony).surface_buildings.empty(),
              "expired lock resumes");
    }

    // --- Persistence round-trip -----------------------------------------------------
    {
        Fixture f = make_fixture();
        auto automation = automatic_colonies();
        automation.advance(f.world(), 1.0);
        const auto saved = automation.capture_state();

        CivilizationAutomationCoordinator restored;
        restored.restore_state(saved);
        check(restored.automation_day() == automation.automation_day(),
              "day restored");
        const auto* restored_player = controller_of(restored, kPlayerCiv);
        check(restored_player &&
                  restored_player->mode("colonies") ==
                      AutomationMode::Advisory,
              "player policy restored");
        check(restored_player && !restored_player->journal().empty(),
              "player journal restored");

        // Continuation stays deterministic: advancing the restored
        // coordinator on identical world state matches an unrestored run.
        Fixture f_a = make_fixture(), f_b = make_fixture();
        auto baseline = automatic_colonies();
        auto continued = automatic_colonies();
        baseline.advance(f_a.world(), 1.0);
        continued.advance(f_b.world(), 1.0);
        continued.restore_state(continued.capture_state());
        for (int i = 0; i < 4; ++i) {
            baseline.advance(f_a.world(), 1.0);
            continued.advance(f_b.world(), 1.0);
        }
        check(building_types(colony_of(f_a, kAiColony)) ==
                  building_types(colony_of(f_b, kAiColony)),
              "restored continuation matches baseline buildings");
        check(f_a.economies.front().credits ==
                  f_b.economies.front().credits,
              "restored continuation matches baseline treasury");
    }

    // --- Determinism: identical fixture, identical automation ----------------------------
    {
        Fixture f_a = make_fixture(), f_b = make_fixture();
        auto a = automatic_colonies();
        auto b = automatic_colonies();
        for (int i = 0; i < 8; ++i) {
            a.advance(f_a.world(), 1.0);
            b.advance(f_b.world(), 1.0);
        }
        check(building_types(colony_of(f_a, kAiColony)) ==
                  building_types(colony_of(f_b, kAiColony)),
              "bit-identical ai colony outcomes");
        check(f_a.economies.front().credits ==
                  f_b.economies.front().credits,
              "bit-identical treasuries");
        const auto* ca = controller_of(a, kAiCiv);
        const auto* cb = controller_of(b, kAiCiv);
        check(ca && cb && ca->journal().size() == cb->journal().size(),
              "identical journal lengths");
    }

    // --- Off mode silences the domain --------------------------------------------------
    {
        Fixture f = make_fixture();
        auto automation = automatic_colonies();
        stellar::engine::AutomationDomainPolicy policy;
        policy.domain =
            std::string(automation_domain_name(AutomationDomain::Colonies));
        policy.mode = AutomationMode::Off;
        automation.set_domain_policy(kAiCiv, std::move(policy));
        automation.advance(f.world(), 1.0);
        check(colony_of(f, kAiColony).surface_buildings.empty(),
              "off domain commits nothing");
    }

    // --- Treasury reserve constraint blocks spending ---------------------------------------
    {
        Fixture f = make_fixture();
        f.economies.front().credits = 20.0; // below building cost + reserve
        auto automation = automatic_colonies();
        automation.advance(f.world(), 1.0);
        check(colony_of(f, kAiColony).surface_buildings.empty(),
              "insufficient treasury blocks placement");
        check(f.economies.front().credits == 20.0,
              "treasury untouched");
    }

    // --- assess_colony_automation reports the deficits ----------------------------------------
    {
        Fixture f = make_fixture();
        const auto& colony = colony_of(f, kAiColony);
        const auto report = assess_colony_automation(f.bodies, colony);
        check(report.colony_id == kAiColony, "report ids colony");
        check(!report.issues.empty(), "report finds issues");
        check(report.free_slots > 0, "report counts free slots");
    }

    // --- Mode round-trip through set_domain_policy --------------------------------------------
    {
        auto automation = automatic_colonies();
        stellar::engine::AutomationDomainPolicy policy;
        policy.domain =
            std::string(automation_domain_name(AutomationDomain::Research));
        policy.mode = AutomationMode::Assisted;
        automation.set_domain_policy(kAiCiv, std::move(policy));
        const auto* stored =
            automation.domain_policy(kAiCiv, AutomationDomain::Research);
        check(stored && stored->mode == AutomationMode::Assisted,
              "explicit domain policy readable");
        check(automation_domain_from_name("research") ==
                  AutomationDomain::Research,
              "domain name round-trip");
        check(automation_domain_name(AutomationDomain::Logistics) ==
                  "logistics",
              "domain name lookup");
    }

    if (failures == 0) {
        std::cout << "all civilization automation tests passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
