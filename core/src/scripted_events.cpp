#include <stellar/core/scripted_events.hpp>

#include <stellar/core/campaign_coordinator.hpp>
#include <stellar/core/civilization_control.hpp>
#include <stellar/core/colonization_runtime.hpp>
#include <stellar/core/combat_simulation.hpp>
#include <stellar/core/construction_projects.hpp>
#include <stellar/core/exploration_advance.hpp>
#include <stellar/core/shipbuilding.hpp>
#include <stellar/core/surface_construction.hpp>
#include <stellar/engine/event_bus.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
#include <utility>

namespace stellar::core {
namespace {

// ---------------- trigger-name mapping ------------------------------------

std::string_view exploration_event_name(ExplorationEventType type) {
    switch (type) {
    case ExplorationEventType::SystemDetected:
        return "exploration.system_detected";
    case ExplorationEventType::SystemReconnoitered:
        return "exploration.system_reconnoitered";
    case ExplorationEventType::SystemSurveyStarted:
        return "exploration.system_survey_started";
    case ExplorationEventType::SystemSurveyed:
        return "exploration.system_surveyed";
    case ExplorationEventType::ResourceSignatureDetected:
        return "exploration.resource_signature_detected";
    case ExplorationEventType::AnomalySignatureDetected:
        return "exploration.anomaly_signature_detected";
    case ExplorationEventType::ActivitySignatureDetected:
        return "exploration.activity_signature_detected";
    case ExplorationEventType::ResourceSurveyed:
        return "exploration.resource_surveyed";
    case ExplorationEventType::AnomalySurveyed:
        return "exploration.anomaly_surveyed";
    case ExplorationEventType::NativeCivilizationSurveyed:
        return "exploration.native_civilization_surveyed";
    case ExplorationEventType::SensorContact:
        return "exploration.sensor_contact";
    case ExplorationEventType::FirstContact:
        return "exploration.first_contact";
    }
    return "exploration.unknown";
}

std::string_view combat_event_name(CombatEventType type) {
    switch (type) {
    case CombatEventType::EngagementStarted:
        return "combat.engagement_started";
    case CombatEventType::DamageApplied:
        return "combat.damage_applied";
    case CombatEventType::FleetRetreatInitiated:
        return "combat.fleet_retreat_initiated";
    case CombatEventType::FleetEscaped:
        return "combat.fleet_escaped";
    case CombatEventType::FleetDestroyed:
        return "combat.fleet_destroyed";
    case CombatEventType::EngagementEnded:
        return "combat.engagement_ended";
    }
    return "combat.unknown";
}

// ---------------- argument parsing -----------------------------------------

std::vector<std::string> split_effect(std::string_view spec) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= spec.size()) {
        const auto colon = spec.find(':', start);
        if (colon == std::string_view::npos) {
            parts.emplace_back(spec.substr(start));
            break;
        }
        parts.emplace_back(spec.substr(start, colon - start));
        start = colon + 1;
    }
    return parts;
}

std::optional<int> parse_int(std::string_view text) {
    int value = 0;
    const auto *last = text.data() + text.size();
    const auto result =
        std::from_chars(text.data(), last, value);
    if (result.ec != std::errc{} || result.ptr != last)
        return std::nullopt;
    return value;
}

std::optional<double> parse_double(std::string_view text) {
    try {
        std::size_t used = 0;
        const double value =
            std::stod(std::string{text}, &used);
        return used == text.size() ? std::optional{value} : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::string substitute(std::string_view arg,
                       const ScriptedEventContext &context) {
    if (arg == "$civ")
        return std::to_string(context.civilization_id);
    if (arg == "$target_civ")
        return std::to_string(context.target_civilization_id);
    if (arg == "$system")
        return std::to_string(context.system_id);
    if (arg == "$body")
        return std::to_string(context.planetary_body_id);
    if (arg == "$colony")
        return std::to_string(context.colony_id);
    if (arg == "$fleet")
        return std::to_string(context.fleet_id);
    return std::string{arg};
}

} // namespace

// ---------------- coordinator ----------------------------------------------

struct ScriptedEventCoordinator::Impl {
    std::unique_ptr<engine::EventBus> bus{
        std::make_unique<engine::EventBus>()};
    engine::MissionRuntime runtime{bus.get()};
    engine::Subscription subscription;
    std::deque<engine::MissionEffectEvent> emitted;
    std::vector<std::pair<std::uint64_t, ScriptedEventContext>> contexts;
    std::deque<std::string> journal;
    std::vector<std::string> definition_ids;
    bool auto_choose_ai{true};
    bool auto_choose_player{false};

    void log(std::string message) {
        if (journal.size() >= journal_capacity)
            journal.pop_front();
        journal.push_back(std::move(message));
    }

    const ScriptedEventContext *context(std::uint64_t instance) const {
        const auto found = std::find_if(
            contexts.begin(), contexts.end(),
            [=](const auto &entry) { return entry.first == instance; });
        return found == contexts.end() ? nullptr : &found->second;
    }

    void prune_contexts() {
        const auto &instances = runtime.instances();
        std::erase_if(contexts, [&](const auto &entry) {
            return std::none_of(instances.begin(), instances.end(),
                                [&](const auto &instance) {
                                    return instance.id == entry.first;
                                });
        });
    }

    // Applies one authored effect. Returns a human-readable outcome.
    std::string apply_effect(ConstructionWorld &world,
                             const ScriptedEventContext &context,
                             std::string_view spec, bool &applied) {
        const auto parts = split_effect(spec);
        if (parts.empty()) {
            applied = false;
            return "empty effect";
        }
        const std::string &verb = parts.front();
        auto arg = [&](std::size_t index) -> std::string {
            return index < parts.size() ? substitute(parts[index], context)
                                        : std::string{};
        };
        auto civ_id = [&](std::size_t index) -> std::optional<int> {
            return parse_int(arg(index));
        };
        auto find_economy = [&](int id) -> CivilizationEconomy * {
            const auto found =
                std::find_if(world.economies.begin(), world.economies.end(),
                             [=](const auto &e) {
                                 return e.civilization_id == id;
                             });
            return found == world.economies.end() ? nullptr : &*found;
        };
        auto find_colony = [&](std::string_view spec_arg,
                               int owner) -> Colony * {
            if (spec_arg == "first_of_civ") {
                const auto found = std::find_if(
                    world.colonies.begin(), world.colonies.end(),
                    [=](const auto &c) {
                        return c.civilization_id == owner;
                    });
                return found == world.colonies.end() ? nullptr : &*found;
            }
            const auto id = parse_int(spec_arg);
            if (!id)
                return nullptr;
            const auto found =
                std::find_if(world.colonies.begin(), world.colonies.end(),
                             [=](const auto &c) { return c.id == *id; });
            return found == world.colonies.end() ? nullptr : &*found;
        };

        if (verb == "grant_credits" && parts.size() == 3) {
            const auto civ = civ_id(1);
            const auto amount = parse_double(arg(2));
            auto *economy = civ ? find_economy(*civ) : nullptr;
            if (!economy || !amount || *amount <= 0.0) {
                applied = false;
                return "grant_credits: bad target or amount";
            }
            economy->credits += *amount;
            applied = true;
            return "granted " + arg(2) + " credits to civ " + arg(1);
        }
        if (verb == "charge_credits" && parts.size() == 3) {
            const auto civ = civ_id(1);
            const auto amount = parse_double(arg(2));
            auto *economy = civ ? find_economy(*civ) : nullptr;
            if (!economy || !amount || *amount < 0.0 ||
                economy->credits < *amount) {
                applied = false;
                return "charge_credits: bad target or unaffordable";
            }
            economy->credits -= *amount;
            applied = true;
            return "charged " + arg(2) + " credits to civ " + arg(1);
        }
        if (verb == "adjust_stability" && parts.size() == 3) {
            const auto delta = parse_double(arg(2));
            auto *colony = find_colony(parts[1], context.civilization_id);
            if (!colony || !delta) {
                applied = false;
                return "adjust_stability: bad colony or delta";
            }
            colony->stability =
                std::clamp(colony->stability + *delta, 0.0, 1.0);
            applied = true;
            return "colony " + std::to_string(colony->id) +
                   " stability -> " + std::to_string(colony->stability);
        }
        if (verb == "damage_building" && parts.size() == 3) {
            const auto fraction = parse_double(arg(2));
            auto *colony = find_colony(parts[1], context.civilization_id);
            if (!colony || !fraction || *fraction <= 0.0) {
                applied = false;
                return "damage_building: bad colony or fraction";
            }
            const auto building = std::find_if(
                colony->surface_buildings.begin(),
                colony->surface_buildings.end(),
                [](const auto &b) { return b.is_complete; });
            if (building == colony->surface_buildings.end()) {
                applied = false;
                return "damage_building: no complete building or bad "
                       "fraction";
            }
            building->condition =
                std::clamp(building->condition - *fraction, 0.0, 1.0);
            applied = true;
            return "building " + std::to_string(building->id) +
                   " damaged to " + std::to_string(building->condition);
        }
        if (verb == "set_building_enabled" && parts.size() == 4) {
            auto *colony = find_colony(parts[1], context.civilization_id);
            if (!colony) {
                applied = false;
                return "set_building_enabled: bad colony";
            }
            int building_id = -1;
            if (parts[2] == "first") {
                const auto complete = std::find_if(
                    colony->surface_buildings.begin(),
                    colony->surface_buildings.end(),
                    [](const auto &b) { return b.is_complete; });
                if (complete != colony->surface_buildings.end())
                    building_id = complete->id;
            } else if (const auto parsed = parse_int(parts[2]))
                building_id = *parsed;
            const auto enabled = arg(3);
            const bool on = enabled == "1" || enabled == "true";
            if (building_id < 0 || (!on && enabled != "0" &&
                                    enabled != "false")) {
                applied = false;
                return "set_building_enabled: bad building or flag";
            }
            const auto order = set_surface_building_enabled(
                world, colony->civilization_id, colony->id, building_id,
                on);
            applied = order.accepted;
            return "set_building_enabled: " + order.message;
        }
        if (verb == "start_project" && parts.size() == 3) {
            const auto civ = civ_id(1);
            if (!civ) {
                applied = false;
                return "start_project: bad civilization";
            }
            const auto order =
                start_construction_project(world, *civ, arg(2));
            applied = order.accepted;
            return "start_project: " + order.message;
        }
        if (verb.rfind("timeout:", 0) == 0) {
            applied = true;
            return "stage timed out (" +
                   std::string{spec.substr(8)} + ")";
        }
        applied = false;
        return "unknown effect '" + std::string{verb} + "'";
    }

    // Drains the bus queue: each emitted transition's authored effects are
    // interpreted against the world in emission order.
    void drain_emitted(ConstructionWorld &world,
                       ScriptedEventReport &report) {
        while (!emitted.empty()) {
            const auto event = emitted.front();
            emitted.pop_front();
            const auto *ctx = context(event.instance_id);
            if (ctx == nullptr)
                continue;
            for (const auto &effect : event.effects) {
                bool applied = false;
                const auto outcome =
                    apply_effect(world, *ctx, effect, applied);
                if (applied)
                    ++report.effects_applied;
                else
                    ++report.effects_rejected;
                log("instance " + std::to_string(event.instance_id) +
                    " effect '" + effect + "' " +
                    (applied ? "applied: " : "rejected: ") + outcome);
                if (effect.rfind("timeout:", 0) == 0)
                    ++report.timed_out;
            }
        }
    }
};

ScriptedEventCoordinator::ScriptedEventCoordinator() : impl_(new Impl) {
    impl_->subscription = impl_->bus->subscribe<engine::MissionEffectEvent>(
        [emitted = &impl_->emitted](const engine::MissionEffectEvent &event) {
            emitted->push_back(event);
        });
}
ScriptedEventCoordinator::~ScriptedEventCoordinator() = default;
ScriptedEventCoordinator::ScriptedEventCoordinator(
    ScriptedEventCoordinator &&) noexcept = default;
ScriptedEventCoordinator &ScriptedEventCoordinator::operator=(
    ScriptedEventCoordinator &&) noexcept = default;

bool ScriptedEventCoordinator::load_definition(
    std::string_view json_document, std::string *error) {
    auto parsed = engine::MissionDefinition::parse(json_document, error);
    if (!parsed)
        return false;
    const auto id = parsed->id;
    if (!impl_->runtime.add_definition(std::move(*parsed), error))
        return false;
    impl_->definition_ids.push_back(id);
    return true;
}

std::size_t ScriptedEventCoordinator::load_directory(
    const std::filesystem::path &root, std::string *error) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto &entry :
         std::filesystem::directory_iterator(root, ec))
        if (entry.is_regular_file() && entry.path().extension() == ".json")
            files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    std::size_t loaded = 0;
    for (const auto &file : files) {
        std::ifstream stream(file);
        std::ostringstream content;
        content << stream.rdbuf();
        std::string failure;
        if (!load_definition(content.str(), &failure)) {
            if (error != nullptr)
                *error = file.filename().string() + ": " + failure;
            return loaded;
        }
        ++loaded;
    }
    return loaded;
}

void ScriptedEventCoordinator::set_auto_choose_ai(bool enabled) noexcept {
    impl_->auto_choose_ai = enabled;
}
bool ScriptedEventCoordinator::auto_choose_ai() const noexcept {
    return impl_->auto_choose_ai;
}
void ScriptedEventCoordinator::set_auto_choose_player(
    bool enabled) noexcept {
    impl_->auto_choose_player = enabled;
}
bool ScriptedEventCoordinator::auto_choose_player() const noexcept {
    return impl_->auto_choose_player;
}

std::size_t ScriptedEventCoordinator::definition_count() const noexcept {
    return impl_->definition_ids.size();
}

std::vector<std::string> ScriptedEventCoordinator::definition_ids() const {
    return impl_->definition_ids;
}

ScriptedEventReport ScriptedEventCoordinator::advance(
    ConstructionWorld &world, const SimulationStepResult &step,
    double elapsed_days) {
    ScriptedEventReport report;
    auto &impl = *impl_;

    auto feed = [&](std::string_view name, const nlohmann::json &payload,
                    const ScriptedEventContext &context) {
        const auto before = impl.runtime.instances().size();
        impl.runtime.handle_event(name, payload.dump());
        ++report.events_fed;
        const auto &instances = impl.runtime.instances();
        for (std::size_t i = before; i < instances.size(); ++i) {
            impl.contexts.emplace_back(instances[i].id, context);
            ++report.instances_started;
            impl.log("instance " + std::to_string(instances[i].id) +
                     " started '" + instances[i].mission_id + "' stage '" +
                     instances[i].stage_id + "' for civ " +
                     std::to_string(context.civilization_id));
        }
    };

    for (const auto &event : step.exploration_events) {
        nlohmann::json payload{{"civilization_id", event.civilization_id},
                               {"fleet_id", event.fleet_id},
                               {"system_id", event.system_id}};
        ScriptedEventContext context;
        context.civilization_id = event.civilization_id;
        context.fleet_id = event.fleet_id;
        context.system_id = event.system_id;
        if (event.planetary_body_id) {
            payload["planetary_body_id"] = *event.planetary_body_id;
            context.planetary_body_id = *event.planetary_body_id;
        }
        if (event.target_civilization_id) {
            payload["target_civilization_id"] =
                *event.target_civilization_id;
            context.target_civilization_id = *event.target_civilization_id;
        }
        feed(exploration_event_name(event.type), payload, context);
    }
    for (const auto &event : step.colonization_events) {
        const nlohmann::json payload{
            {"civilization_id", event.civilization_id},
            {"fleet_id", event.fleet_id},
            {"system_id", event.system_id},
            {"colony_id", event.colony_id}};
        feed("colonization.colony_established", payload,
             {.civilization_id = event.civilization_id,
              .system_id = event.system_id,
              .colony_id = event.colony_id,
              .fleet_id = event.fleet_id});
    }
    for (const auto &event : step.construction_events) {
        const nlohmann::json payload{
            {"civilization_id", event.civilization_id},
            {"project_id", event.project_id}};
        feed("construction.project_completed", payload,
             {.civilization_id = event.civilization_id});
    }
    for (const auto &event : step.shipbuilding_events) {
        const nlohmann::json payload{
            {"civilization_id", event.civilization_id},
            {"fleet_id", event.fleet_id},
            {"design_id", event.design_id}};
        feed("shipbuilding.ship_completed", payload,
             {.civilization_id = event.civilization_id,
              .fleet_id = event.fleet_id});
    }
    for (const auto &event : step.research_events) {
        const nlohmann::json payload{
            {"civilization_id", event.civilization_id},
            {"technology_id", event.technology_id}};
        feed("research.technology_completed", payload,
             {.civilization_id = event.civilization_id});
    }
    for (const auto &event : step.combat_events) {
        nlohmann::json payload{
            {"actor_civilization_id", event.actor_civilization_id},
            {"actor_fleet_id", event.actor_fleet_id},
            {"shield_damage", event.shield_damage},
            {"armor_damage", event.armor_damage},
            {"hull_damage", event.hull_damage}};
        ScriptedEventContext context;
        context.civilization_id = event.actor_civilization_id;
        context.fleet_id = event.actor_fleet_id;
        if (event.system_id) {
            payload["system_id"] = *event.system_id;
            context.system_id = *event.system_id;
        }
        if (event.target_civilization_id) {
            payload["target_civilization_id"] =
                *event.target_civilization_id;
            context.target_civilization_id = *event.target_civilization_id;
        }
        if (event.target_fleet_id)
            payload["target_fleet_id"] = *event.target_fleet_id;
        feed(combat_event_name(event.type), payload, context);
    }

    // Stage timers.
    impl.runtime.advance(elapsed_days);

    // Drain emitted transitions (timeout effects + stage entries).
    impl.drain_emitted(world, report);

    // Auto-choice pass: AI civilizations always resolve; player-bound
    // chains only when the host opted in.
    for (const auto &pending : this->pending()) {
        const auto civ = std::find_if(
            world.civilizations.begin(), world.civilizations.end(),
            [&](const auto &c) {
                return c.id == pending.context.civilization_id;
            });
        const bool ai = civ != world.civilizations.end() &&
                        civilization_uses_ai(*civ, world.control);
        if ((ai && impl.auto_choose_ai) ||
            (!ai && impl.auto_choose_player)) {
            if (impl.runtime.choose(pending.instance_id,
                                    pending.choice_ids.front())) {
                ++report.choices_applied;
                impl.log("instance " +
                         std::to_string(pending.instance_id) +
                         " auto-chose '" + pending.choice_ids.front() +
                         "'");
            }
        }
    }
    impl.drain_emitted(world, report);
    impl.prune_contexts();
    return report;
}

bool ScriptedEventCoordinator::choose(ConstructionWorld &world,
                                      std::uint64_t instance_id,
                                      std::string_view choice_id) {
    auto &impl = *impl_;
    const auto *context = impl.context(instance_id);
    if (context == nullptr)
        return false;
    const auto pending_list = pending();
    const auto is_choice_stage =
        std::any_of(pending_list.begin(), pending_list.end(),
                    [&](const auto &p) {
                        return p.instance_id == instance_id;
                    });
    if (!is_choice_stage)
        return false;
    ScriptedEventReport report;
    const bool chosen = impl.runtime.choose(instance_id, choice_id);
    if (!chosen)
        return false;
    impl.log("instance " + std::to_string(instance_id) +
             " operator-chose '" + std::string{choice_id} + "'");
    impl.drain_emitted(world, report);
    impl.prune_contexts();
    return true;
}

std::vector<ScriptedEventPending> ScriptedEventCoordinator::pending() const {
    std::vector<ScriptedEventPending> out;
    auto &impl = *impl_;
    for (const auto &instance : impl.runtime.instances()) {
        const auto *definition =
            impl.runtime.definition(instance.mission_id);
        if (definition == nullptr)
            continue;
        const auto stage = definition->stages.find(instance.stage_id);
        if (stage == definition->stages.end() || stage->second.choices.empty())
            continue;
        ScriptedEventPending entry;
        entry.instance_id = instance.id;
        entry.mission_id = instance.mission_id;
        entry.stage_id = instance.stage_id;
        entry.title_key = stage->second.title_key;
        entry.body_key = stage->second.body_key;
        for (const auto &choice : stage->second.choices)
            entry.choice_ids.push_back(choice.id);
        if (const auto *context = impl.context(instance.id))
            entry.context = *context;
        out.push_back(std::move(entry));
    }
    return out;
}

std::string ScriptedEventCoordinator::capture_state() const {
    auto &impl = *impl_;
    nlohmann::json doc{{"version", 1},
                       {"auto_choose_ai", impl.auto_choose_ai},
                       {"auto_choose_player", impl.auto_choose_player}};
    try {
        doc["runtime"] = nlohmann::json::parse(impl.runtime.serialize());
    } catch (...) {
        doc["runtime"] = nlohmann::json::object();
    }
    doc["contexts"] = nlohmann::json::array();
    for (const auto &[instance, context] : impl.contexts)
        doc["contexts"].push_back(
            {{"instance", instance},
             {"civilization_id", context.civilization_id},
             {"target_civilization_id", context.target_civilization_id},
             {"system_id", context.system_id},
             {"planetary_body_id", context.planetary_body_id},
             {"colony_id", context.colony_id},
             {"fleet_id", context.fleet_id}});
    doc["journal"] = impl.journal;
    return doc.dump();
}

bool ScriptedEventCoordinator::restore_state(std::string_view document,
                                             std::string *error) {
    auto &impl = *impl_;
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(document);
    } catch (const std::exception &ex) {
        if (error != nullptr)
            *error = ex.what();
        return false;
    }
    if (!doc.is_object()) {
        if (error != nullptr)
            *error = "scripted event snapshot must be an object";
        return false;
    }
    std::vector<std::pair<std::uint64_t, ScriptedEventContext>> contexts;
    const auto context_doc =
        doc.value("contexts", nlohmann::json::array());
    if (context_doc.size() > 2048) {
        if (error != nullptr)
            *error = "scripted event context count exceeds bound";
        return false;
    }
    for (const auto &entry : context_doc) {
        ScriptedEventContext context;
        context.civilization_id = entry.value("civilization_id", 0);
        context.target_civilization_id =
            entry.value("target_civilization_id", 0);
        context.system_id = entry.value("system_id", 0);
        context.planetary_body_id = entry.value("planetary_body_id", -1);
        context.colony_id = entry.value("colony_id", -1);
        context.fleet_id = entry.value("fleet_id", 0);
        contexts.emplace_back(entry.value("instance", std::uint64_t{}),
                              context);
    }
    std::deque<std::string> journal;
    for (const auto &entry :
         doc.value("journal", nlohmann::json::array()))
        if (entry.is_string()) {
            journal.push_back(entry.get<std::string>());
            if (journal.size() > journal_capacity)
                journal.pop_front();
        }
    if (doc.contains("runtime")) {
        const auto &runtime_doc = doc["runtime"];
        if (runtime_doc.is_object() &&
            runtime_doc.contains("instances") &&
            runtime_doc["instances"].is_array() &&
            runtime_doc["instances"].size() > 2048) {
            if (error != nullptr)
                *error = "scripted event instance count exceeds bound";
            return false;
        }
        if (!impl.runtime.restore(runtime_doc.dump(), error))
            return false;
    }
    impl.contexts = std::move(contexts);
    impl.journal = std::move(journal);
    impl.auto_choose_ai = doc.value("auto_choose_ai", impl.auto_choose_ai);
    impl.auto_choose_player =
        doc.value("auto_choose_player", impl.auto_choose_player);
    impl.prune_contexts();
    return true;
}

const engine::MissionRuntime &
ScriptedEventCoordinator::runtime() const noexcept {
    return impl_->runtime;
}

const std::deque<std::string> &
ScriptedEventCoordinator::journal() const noexcept {
    return impl_->journal;
}

} // namespace stellar::core
