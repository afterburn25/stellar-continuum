// Scripted-event coordinator coverage: a real ConstructionWorld fixture
// feeds SimulationStepResult domain events into loaded data-authored
// chains; choices and timeouts apply authored effects through the same
// canonical commands/state an operator uses, and everything persists.

#include <stellar/core/campaign_coordinator.hpp>
#include <stellar/core/construction_projects.hpp>
#include <stellar/core/scripted_events.hpp>
#include <stellar/core/surface_construction.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

using namespace stellar::core;

constexpr int kAiCiv = 1;
constexpr int kPlayerCiv = 2;
constexpr int kBodyId = 10;
constexpr int kAiColony = 100;
constexpr int kPlayerColony = 200;

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

    PlanetaryBody body;
    body.id = kBodyId;
    body.system_id = 1;
    body.name = "Cinder";
    body.kind = PlanetaryBodyKind::Planet;
    body.radius_earth = 0.8;
    body.mass_earth = 0.6;
    body.environment = {0.5,
                        420.0,
                        0.0,
                        PlanetaryAtmosphereRegime::Vacuum,
                        PlanetarySolventRegime::None,
                        0.2,
                        false,
                        true};
    f.bodies.push_back(body);

    auto colony_for = [](int id, int civ) {
        Colony c;
        c.id = id;
        c.civilization_id = civ;
        c.system_id = 1;
        c.planetary_body_id = kBodyId;
        c.name = "Test Colony " + std::to_string(id);
        c.population_millions = 600.0;
        c.surface_hub_level = 1;
        return c;
    };
    Colony ai_colony = colony_for(kAiColony, kAiCiv);
    // A complete building so damage/enable effects have a live target.
    SurfaceBuilding lab;
    lab.id = 9001;
    lab.type_id = "science_lab";
    lab.is_complete = true;
    ai_colony.surface_buildings.push_back(lab);
    f.colonies.push_back(ai_colony);
    f.colonies.push_back(colony_for(kPlayerColony, kPlayerCiv));

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

const Colony &colony_of(const Fixture &f, int id) {
    for (const auto &c : f.colonies)
        if (c.id == id) return c;
    throw std::runtime_error("missing colony");
}

const CivilizationEconomy &economy_of(const Fixture &f, int id) {
    for (const auto &e : f.economies)
        if (e.civilization_id == id) return e;
    throw std::runtime_error("missing economy");
}

const char *kChain = R"json({
  "id": "test_chain",
  "triggers": [
    { "event": "exploration.anomaly_surveyed", "stage": "signal" }
  ],
  "stages": {
    "signal": {
      "title_key": "T_SIGNAL", "body_key": "B_SIGNAL",
      "choices": [
        { "id": "investigate", "next": "approach" },
        { "id": "ignore", "next": "" }
      ]
    },
    "approach": {
      "title_key": "T_APPROACH", "body_key": "B_APPROACH",
      "timer_days": 10, "timeout_stage": "vault"
    },
    "vault": {
      "title_key": "T_VAULT", "body_key": "B_VAULT",
      "choices": [
        { "id": "excavate", "next": "",
          "effects": ["grant_credits:$civ:250",
                      "damage_building:first_of_civ:0.5",
                      "adjust_stability:first_of_civ:-0.1"] },
        { "id": "seal", "next": "",
          "effects": ["adjust_stability:first_of_civ:0.05"] }
      ]
    }
  }
})json";

const char *kChargeChain = R"json({
  "id": "charge_chain",
  "triggers": [
    { "event": "colonization.colony_established", "stage": "cost" }
  ],
  "stages": {
    "cost": {
      "title_key": "T_COST", "body_key": "B_COST",
      "choices": [
        { "id": "pay", "next": "",
          "effects": ["charge_credits:$civ:99999"] }
      ]
    }
  }
})json";

SimulationStepResult anomaly_step(int civ, int system) {
    SimulationStepResult step;
    ExplorationEvent event;
    event.type = ExplorationEventType::AnomalySurveyed;
    event.civilization_id = civ;
    event.fleet_id = 55;
    event.system_id = system;
    event.planetary_body_id = kBodyId;
    event.message = "surveyed";
    step.exploration_events.push_back(event);
    return step;
}

} // namespace

int main(int argc, char **argv) {
    const std::filesystem::path source_root =
        argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path{};

    // --- AI chain end-to-end: trigger, auto-choice, timer, effects ----------
    {
        Fixture f = make_fixture();
        ScriptedEventCoordinator events;
        std::string error;
        check(events.load_definition(kChain, &error),
              "definition parses");
        check(events.definition_count() == 1, "one definition loaded");

        auto world = f.world();
        auto report = events.advance(world, anomaly_step(kAiCiv, 7), 0.25);
        check(report.instances_started == 1, "anomaly survey started chain");
        // Auto-choose resolves 'investigate' in the same advance.
        check(report.choices_applied == 1, "ai auto-chose investigate");
        check(events.pending().empty(),
              "ai chain left the choice stage");
        const auto &instances = events.runtime().instances();
        check(instances.size() == 1 && instances[0].stage_id == "approach",
              "chain waits on the timed approach stage");

        report = events.advance(world, SimulationStepResult{}, 10.5);
        check(report.timed_out == 1, "approach timer fired");
        check(report.choices_applied == 1, "ai auto-chose excavate");
        check(economy_of(f, kAiCiv).credits == 5250.0,
              "excavate granted 250 credits through the treasury");
        const auto &colony = colony_of(f, kAiColony);
        check(colony.surface_buildings.front().condition <= 0.5 + 1e-9,
              "excavate damaged the lab through authoritative condition");
        check(colony.stability <= 0.9 + 1e-9,
              "excavate reduced colony stability");
        check(events.runtime().instances().empty(),
              "terminal choice completed the instance");
        check(!events.journal().empty(), "effects journaled");
    }

    // --- Player chain waits for an operator choice --------------------------
    {
        Fixture f = make_fixture();
        ScriptedEventCoordinator events;
        (void)events.load_definition(kChain);
        auto world = f.world();
        auto report =
            events.advance(world, anomaly_step(kPlayerCiv, 9), 0.25);
        check(report.choices_applied == 0,
              "player chain does not auto-resolve");
        const auto pending = events.pending();
        check(pending.size() == 1, "player stage is pending");
        if (pending.size() == 1) {
            check(pending[0].context.civilization_id == kPlayerCiv,
                  "pending binds the player civilization");
            check(pending[0].choice_ids.size() == 2 &&
                      pending[0].choice_ids[0] == "investigate",
                  "pending exposes authored choices");
            check(pending[0].title_key == "T_SIGNAL",
                  "pending exposes the title key");
        }
        if (!pending.empty()) {
            check(!events.choose(world, pending[0].instance_id, "nonsense"),
                  "unknown choice rejected");
            check(events.choose(world, pending[0].instance_id,
                                "investigate"),
                  "operator choice accepted");
        }
        report = events.advance(world, SimulationStepResult{}, 10.5);
        // Player auto-resolve is off: vault stage stays pending.
        const auto vault = events.pending();
        check(vault.size() == 1 && vault[0].stage_id == "vault",
              "player vault awaits operator");
        if (!vault.empty()) {
            check(events.choose(world, vault[0].instance_id, "seal"),
                  "operator chose seal");
            check(colony_of(f, kPlayerColony).stability == 1.0,
                  "seal raised stability to the clamp");
        }
    }

    // --- Save/load continuation ---------------------------------------------
    {
        Fixture f = make_fixture();
        ScriptedEventCoordinator events;
        (void)events.load_definition(kChain);
        auto world = f.world();
        (void)events.advance(world, anomaly_step(kPlayerCiv, 3), 0.25);
        const auto state = events.capture_state();
        check(!state.empty(), "state captures");

        Fixture f2 = make_fixture();
        ScriptedEventCoordinator restored;
        (void)restored.load_definition(kChain);
        check(restored.restore_state(state),
              "state restores into a fresh coordinator");
        auto pending = restored.pending();
        check(pending.size() == 1 &&
                  pending[0].context.civilization_id == kPlayerCiv,
              "pending chain survives restore");
        auto world2 = f2.world();
        check(restored.choose(world2, pending[0].instance_id, "investigate"),
              "restored chain accepts choices");
        const auto report =
            restored.advance(world2, SimulationStepResult{}, 10.5);
        check(report.timed_out == 1, "restored timers still fire");
    }

    // --- Effect rejection: unaffordable charge ------------------------------
    {
        Fixture f = make_fixture();
        ScriptedEventCoordinator events;
        (void)events.load_definition(kChargeChain);
        auto world = f.world();
        SimulationStepResult step;
        step.colonization_events.push_back(
            {.civilization_id = kPlayerCiv,
             .fleet_id = 1,
             .system_id = 2,
             .colony_id = kPlayerColony,
             .message = "founded"});
        (void)events.advance(world, step, 0.25);
        const auto pending = events.pending();
        check(pending.size() == 1, "charge chain pending");
        if (!pending.empty())
            check(events.choose(world, pending[0].instance_id, "pay"),
                  "pay choice transitions even when effects reject");
        check(economy_of(f, kPlayerCiv).credits == 5000.0,
              "unaffordable charge_credits rejected, treasury intact");
    }

    // --- Player auto-choose opt-in ------------------------------------------
    {
        Fixture f = make_fixture();
        ScriptedEventCoordinator events;
        (void)events.load_definition(kChain);
        events.set_auto_choose_player(true);
        auto world = f.world();
        const auto report =
            events.advance(world, anomaly_step(kPlayerCiv, 4), 0.25);
        check(report.choices_applied == 1,
              "opted-in player chain auto-resolves");
    }

    // --- Shipped data loads --------------------------------------------------
    if (!source_root.empty()) {
        ScriptedEventCoordinator events;
        std::string error;
        const auto loaded = events.load_directory(
            source_root / "data" / "events", &error);
        check(loaded >= 3, "shipped data/events definitions load");
        check(error.empty(), "shipped definitions parse without error");
        check(events.definition_count() >= 3,
              "definition ids are enumerated");
    }

    if (failures == 0) {
        std::cout << "all scripted events tests passed\n";
        return 0;
    }
    std::cerr << failures << " scripted events test(s) failed\n";
    return 1;
}
