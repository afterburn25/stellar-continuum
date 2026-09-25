#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include <stellar/engine/foundation.hpp>

namespace stellar::engine {

// Importance-based simulation detail tiers. The scheduler decides *when* an
// item runs; the owner decides *what* a tier means (full substep, coarse
// economic tick, analytic propagation).
enum class SimulationTier : std::size_t {
    Active = 0,    // every tick — current system, engaged fleets
    Nearby = 1,    // high cadence — adjacent/observed systems
    Normal = 2,    // regular cadence — ordinary colonies/fleets
    Background = 3,// coarse ticks — distant economy
    Dormant = 4,   // analytic — stable unobserved state advanced in bulk
    Count = 5
};

struct SimulationTierPolicy {
    // Ticks between runs per tier. Dormant entries advance analytically:
    // owners accumulate elapsed ticks via dormant_elapsed() instead of
    // receiving per-tick work.
    std::array<std::uint64_t, static_cast<std::size_t>(SimulationTier::Count)> periods{
        1, 2, 4, 16, 0};
    void validate() const {
        for (std::size_t i = 0; i + 1 < periods.size(); ++i)
            if (periods[i] == 0)
                throw std::invalid_argument("SimulationTierPolicy periods must be positive "
                                            "(Dormant may be 0 = analytic)");
    }
};

// Fixed-cadence scheduler for importance-tiered work items. Items are keyed by
// an owner-chosen stable key (EntityId value, system id, ...). advance()
// returns the keys due this tick grouped by tier; dormant items accumulate
// elapsed time and surface only through dormant_elapsed().
class SimulationScheduler {
public:
    using Key = std::uint64_t;

    explicit SimulationScheduler(SimulationTierPolicy policy = {}) {
        policy.validate();
        policy_ = policy;
    }

    void add(Key key, SimulationTier tier);
    void remove(Key key);
    void set_tier(Key key, SimulationTier tier);
    [[nodiscard]] SimulationTier tier(Key key) const;
    [[nodiscard]] bool contains(Key key) const;
    [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }
    void clear();

    // Advance one tick; returns due keys per tier (index = tier), each
    // sorted ascending — iteration order is deterministic regardless of
    // registration order or map internals. Dormant items never appear —
    // query dormant_elapsed() for bulk propagation.
    std::array<std::vector<Key>, static_cast<std::size_t>(SimulationTier::Count)>
        advance();

    // Peek/mark split used by SimulationExecutor for deferred (budget-
    // capped) execution and event wakeups: begin_tick() increments the
    // clock, collect_due() lists eligible items *without* marking them
    // run, and mark_ran() records execution so elapsed accounting stays
    // exact when an item is deferred past its due tick.
    struct DueItem {
        Key key;
        SimulationTier tier;
        std::uint64_t elapsed{}; // ticks since the item last ran
    };
    Tick begin_tick() { return ++tick_; }
    [[nodiscard]] std::vector<DueItem> collect_due() const;
    void mark_ran(Key key);
    [[nodiscard]] std::uint64_t elapsed_since_run(Key key) const;

    [[nodiscard]] Tick tick() const noexcept { return tick_; }
    [[nodiscard]] std::uint64_t dormant_elapsed(Key key) const;
    void dormant_consumed(Key key) { dormant_since_[key] = tick_; }
    // All dormant keys, sorted — bulk analytic propagation source.
    [[nodiscard]] std::vector<Key> dormant_keys() const;

    [[nodiscard]] std::array<std::size_t, static_cast<std::size_t>(SimulationTier::Count)>
        tier_counts() const;

    [[nodiscard]] const SimulationTierPolicy& policy() const noexcept { return policy_; }

    // --- persistence -------------------------------------------------
    // Serializable per-item state: tier plus the tick bookkeeping that
    // drives elapsed accounting (last run for scheduled tiers, dormant
    // accumulation start for Dormant). The scheduler holds no callbacks,
    // so its entire authoritative state is data.
    struct ItemState {
        Key key{};
        SimulationTier tier{};
        Tick last_run{};         // valid when has_last_run
        bool has_last_run{};
        Tick dormant_since{};    // valid when has_dormant_since
        bool has_dormant_since{};
    };
    struct State {
        std::uint32_t version{1};
        Tick tick{};
        std::vector<ItemState> items; // sorted by key
    };
    [[nodiscard]] State capture_state() const;
    // Restores tick and per-item state. Snapshot items are registered if
    // absent; existing keys are overwritten (timers are restored exactly,
    // not reset the way set_tier() would). Local keys missing from the
    // snapshot keep their current state — the caller decides whether that
    // is a migration case.
    void restore_state(const State& state);

private:
    SimulationTierPolicy policy_;
    std::unordered_map<Key, SimulationTier> items_;
    // Last tick each item ran (non-dormant tiers).
    std::unordered_map<Key, Tick> last_run_;
    // Tick at which dormant accumulation started (or was last consumed).
    std::unordered_map<Key, Tick> dormant_since_;
    Tick tick_{};
};

} // namespace stellar::engine
