#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace stellar::engine {

// Strategic AI substrate — deterministic utility-based action
// selection for civilizations/factions. The engine owns the DECISION
// MACHINERY, not the policy: callers register candidate actions with
// utility scorers (reading their own authoritative state) and commit
// effects (issuing their own commands). decide() evaluates one domain,
// applies incumbent hysteresis and cooldowns, picks the argmax with
// ascending-id tie-break, commits it, and journals the result.
//
// Determinism contract: candidates iterate in ascending id order;
// equal utilities resolve to the smaller action id; hysteresis is a
// fixed multiplier — no RNG anywhere. Scorers MUST be pure functions
// of observable state (the caller's job — a nondeterministic scorer
// breaks the contract).
//
// Planning cadence is owned by the caller: register each domain's
// decide() as a SimulationExecutor task at the tier that fits its
// timescale (empire strategy at Background, fleet tactics at Nearby).
// That separation IS the hierarchical-cadence story — one class,
// many cadences.

// A candidate action. `score` returns utility (typically 0..1; any
// finite double works). `commit` performs the action — called at most
// once per decide() for the winner only.
struct UtilityAction {
    std::string id;
    std::string domain; // "economy", "expansion", "military", ...
    std::function<double()> score;
    std::function<void()> commit;
    double cooldown_days{0.0};      // min days between commits
    double weight{1.0};             // caller-side importance scaling
    bool enabled{true};
};

struct Decision {
    double at_day{0.0};
    std::string domain;
    std::string action_id;
    double utility{0.0};
    std::uint32_t candidates{0};    // how many scored
    bool switched{false};           // displaced the incumbent
};

class StrategicMind {
public:
    explicit StrategicMind(std::size_t journal_capacity = 256)
        : journal_capacity_(journal_capacity) {}

    // Register/replace a candidate action (same id replaces).
    void add_action(UtilityAction action);
    bool remove_action(std::string_view id);
    bool set_enabled(std::string_view id, bool enabled);
    [[nodiscard]] const UtilityAction* action(std::string_view id) const;
    [[nodiscard]] std::vector<std::string> action_ids(
        std::string_view domain) const; // ascending

    // Evaluate all enabled actions in `domain`, pick argmax(utility ×
    // weight, incumbent × hysteresis), commit if it clears
    // `min_utility`, journal the decision. Returns the committed id or
    // nullopt. `now_day` drives cooldowns — pass the campaign clock.
    std::optional<std::string> decide(std::string_view domain,
                                      double now_day,
                                      double min_utility = 0.0,
                                      double hysteresis = 1.1);

    // The last committed action per domain (the hysteresis incumbent).
    [[nodiscard]] std::optional<std::string> incumbent(
        std::string_view domain) const;
    [[nodiscard]] double last_commit_day(std::string_view domain) const;

    // Bounded decision journal — developer/debug visibility into WHY
    // the AI acted. Oldest entries drop past capacity.
    [[nodiscard]] const std::deque<Decision>& journal() const {
        return journal_;
    }

private:
    std::unordered_map<std::string, UtilityAction> actions_;
    std::unordered_map<std::string, std::string> incumbents_; // domain->action
    std::unordered_map<std::string, double> last_commit_;     // action->day
    std::deque<Decision> journal_;
    std::size_t journal_capacity_;
};

} // namespace stellar::engine
