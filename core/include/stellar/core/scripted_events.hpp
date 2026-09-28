#pragma once

#include <stellar/core/construction_state.hpp>
#include <stellar/engine/mission_graph.hpp>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::engine {
class EventBus;
class Subscription;
} // namespace stellar::engine

namespace stellar::core {

struct SimulationStepResult;
struct ConstructionWorld;

// SCRIPTED_EVENTS — data-authored event chains wired into the campaign.
//
// engine::MissionRuntime supplies the chain machinery (JSON definitions,
// triggered instances, timed stages, choices, serialize/restore); this
// coordinator is the authoritative Core consumer that was missing:
//
//  - feed:   the discrete domain events emitted by a strategic step
//            (exploration surveys/first contact, colonization, combat,
//            construction/shipbuilding/research completions) are mapped
//            to named mission triggers with scalar JSON payloads;
//  - timing: stage timers advance with simulated days, following pause
//            and game speed like every other simulation clock;
//  - choice: pending stages are surfaced through pending(); operator or
//            AI choices commit via choose(), emitting the authored
//            effect list which is interpreted through the same canonical
//            commands/state an operator order uses — no parallel rules;
//  - save:   instances, trigger contexts, the bounded applied-effects
//            journal and auto-choice policy round-trip through
//            capture_state()/restore_state() (carried by
//            CampaignRuntimeContinuation and the developer save JSON).
//
// Trigger names (data contract):
//   exploration.<type>: system_detected, system_reconnoitered,
//     system_survey_started, system_surveyed, resource_signature_detected,
//     anomaly_signature_detected, activity_signature_detected,
//     resource_surveyed, anomaly_surveyed, native_civilization_surveyed,
//     sensor_contact, first_contact
//     payload: civilization_id, fleet_id, system_id,
//              planetary_body_id?, target_civilization_id?
//   colonization.colony_established
//     payload: civilization_id, fleet_id, system_id, colony_id
//   construction.project_completed   payload: civilization_id, project_id
//   shipbuilding.ship_completed      payload: civilization_id, fleet_id,
//                                            design_id
//   research.technology_completed    payload: civilization_id,
//                                            technology_id
//   combat.<type>: engagement_started, damage_applied,
//     fleet_retreat_initiated, fleet_escaped, fleet_destroyed,
//     engagement_ended
//     payload: actor_civilization_id, actor_fleet_id, system_id?,
//              target_civilization_id?, target_fleet_id?,
//              shield_damage, armor_damage, hull_damage
//
// Effect vocabulary (authored on choices; applied by the interpreter in
// argument order, first failure leaves earlier effects committed and is
// journaled):
//   grant_credits:<civ>:<amount>           treasury += amount
//   charge_credits:<civ>:<amount>          treasury -= amount, rejects if
//                                          the economy cannot cover it
//   adjust_stability:<colony>:<delta>      colony stability clamp [0,1]
//   damage_building:<colony>:<fraction>    first complete building loses
//                                          condition (repairable through
//                                          the canonical repair command)
//   set_building_enabled:<colony>:<b|first>:<0|1>   canonical enable gate
//   start_project:<civ>:<project_id>       canonical construction order
//
// Token substitution inside arguments: $civ, $target_civ, $system,
// $body, $colony, $fleet resolve from the trigger context bound to the
// instance; a <colony> argument may also be the literal id or
// "first_of_civ" (first colony owned by the bound civilization in span
// order — deterministic).
//
// The coordinator is inert until at least one definition is loaded, so
// campaigns and parity fixtures without a data/events root behave
// exactly as before.

// Scalar context captured from the triggering domain event and bound to
// each new instance — the substitution source for effect arguments and
// the audience/civilization attribution for pending stages.
struct ScriptedEventContext {
    int civilization_id{};
    int target_civilization_id{};
    int system_id{};
    int planetary_body_id{-1};
    int colony_id{-1};
    int fleet_id{};
};

// A live stage that offers choices — the consumer-facing surface for a
// UI, a headless chooser or a scripted test.
struct ScriptedEventPending {
    std::uint64_t instance_id{};
    std::string mission_id;
    std::string stage_id;
    std::string title_key;
    std::string body_key;
    std::vector<std::string> choice_ids;
    ScriptedEventContext context;
};

struct ScriptedEventReport {
    int events_fed{};
    int instances_started{};
    int choices_applied{};
    int timed_out{};
    int effects_applied{};
    int effects_rejected{};
};

class ScriptedEventCoordinator {
public:
    static constexpr std::size_t journal_capacity = 128;

    ScriptedEventCoordinator();
    ~ScriptedEventCoordinator();
    ScriptedEventCoordinator(ScriptedEventCoordinator &&) noexcept;
    ScriptedEventCoordinator &
    operator=(ScriptedEventCoordinator &&) noexcept;
    ScriptedEventCoordinator(const ScriptedEventCoordinator &) = delete;
    ScriptedEventCoordinator &
    operator=(const ScriptedEventCoordinator &) = delete;

    // --- Definitions (data-authored) -------------------------------------
    [[nodiscard]] bool load_definition(std::string_view json_document,
                                       std::string *error = nullptr);
    // Loads every *.json in `root` in sorted filename order
    // (deterministic registration); returns the count added. Missing or
    // unreadable roots are not an error — they just yield zero.
    [[nodiscard]] std::size_t load_directory(const std::filesystem::path &root,
                                             std::string *error = nullptr);
    [[nodiscard]] std::size_t definition_count() const noexcept;
    [[nodiscard]] std::vector<std::string> definition_ids() const;

    // --- Simulation hooks -------------------------------------------------
    // Feeds the step's domain events, advances stage timers by
    // `elapsed_days`, drains emitted transitions (timeouts + auto-choice
    // for AI civilizations), and applies authored effects through
    // canonical commands on `world`. Called by the campaign coordinator
    // once per accepted step, after all phases ran.
    [[nodiscard]] ScriptedEventReport advance(ConstructionWorld &world,
                                              const SimulationStepResult &step,
                                              double elapsed_days);

    // --- Operator path -----------------------------------------------------
    // Applies an operator/player choice: stage transition + authored
    // effects through the canonical interpreter. Returns false for an
    // unknown instance, a stage without choices, or an unknown choice id.
    [[nodiscard]] bool choose(ConstructionWorld &world,
                              std::uint64_t instance_id,
                              std::string_view choice_id);

    // Live stages offering choices, in instance-creation order.
    [[nodiscard]] std::vector<ScriptedEventPending> pending() const;

    // AI civilizations auto-resolve their choice stages (first authored
    // choice — deterministic). Enabled by default: an unattended AI civ
    // must never stall its own chains.
    void set_auto_choose_ai(bool enabled) noexcept;
    [[nodiscard]] bool auto_choose_ai() const noexcept;
    // Player civilizations normally wait for an operator; headless hosts
    // opt in so player-bound chains resolve deterministically.
    void set_auto_choose_player(bool enabled) noexcept;
    [[nodiscard]] bool auto_choose_player() const noexcept;

    // --- Save/load ---------------------------------------------------------
    // The snapshot embeds the authored definition documents, so a save is
    // self-contained: restore re-registers its trigger contract before
    // instances come back and never depends on a host data root.
    [[nodiscard]] std::string capture_state() const;
    // Unknown mission/stage references in the document are an error —
    // they indicate save corruption, not missing data files.
    [[nodiscard]] bool restore_state(std::string_view document,
                                     std::string *error = nullptr);

    // Bounded applied-effects journal (newest last) — the explainability
    // surface shared with the automation controller's journal model.
    [[nodiscard]] const std::deque<std::string> &journal() const noexcept;

    [[nodiscard]] const engine::MissionRuntime &runtime() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Data members are pimpl'd because engine::EventBus is immovable and
    // this coordinator lives inside the movable step coordinator.
};

} // namespace stellar::core
