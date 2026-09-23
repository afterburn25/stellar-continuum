#include <stellar/engine/simulation_scheduler.hpp>

namespace stellar::engine {

void SimulationScheduler::add(Key key, SimulationTier tier) {
    if (tier >= SimulationTier::Count)
        throw std::invalid_argument("SimulationScheduler tier out of range");
    items_[key] = tier;
    if (tier == SimulationTier::Dormant) {
        dormant_since_[key] = tick_;
        last_run_.erase(key);
    } else {
        last_run_[key] = tick_;
        dormant_since_.erase(key);
    }
}

void SimulationScheduler::remove(Key key) {
    items_.erase(key);
    last_run_.erase(key);
    dormant_since_.erase(key);
}

void SimulationScheduler::set_tier(Key key, SimulationTier tier) {
    const auto it = items_.find(key);
    if (it == items_.end())
        throw std::invalid_argument("SimulationScheduler cannot re-tier an unknown key");
    if (tier >= SimulationTier::Count)
        throw std::invalid_argument("SimulationScheduler tier out of range");
    if (it->second == tier) return;
    add(key, tier);
}

SimulationTier SimulationScheduler::tier(Key key) const {
    const auto it = items_.find(key);
    if (it == items_.end())
        throw std::invalid_argument("SimulationScheduler unknown key");
    return it->second;
}

bool SimulationScheduler::contains(Key key) const { return items_.find(key) != items_.end(); }

void SimulationScheduler::clear() {
    items_.clear();
    last_run_.clear();
    dormant_since_.clear();
}

std::array<std::vector<SimulationScheduler::Key>,
           static_cast<std::size_t>(SimulationTier::Count)>
SimulationScheduler::advance() {
    ++tick_;
    std::array<std::vector<Key>, static_cast<std::size_t>(SimulationTier::Count)> due;
    for (auto& [key, tier] : items_) {
        if (tier == SimulationTier::Dormant) continue;
        const auto period = policy_.periods[static_cast<std::size_t>(tier)];
        const auto last = last_run_.find(key);
        const Tick since = last == last_run_.end() ? period : tick_ - last->second;
        if (since >= period) {
            due[static_cast<std::size_t>(tier)].push_back(key);
            last_run_[key] = tick_;
        }
    }
    return due;
}

std::uint64_t SimulationScheduler::dormant_elapsed(Key key) const {
    const auto it = dormant_since_.find(key);
    return it == dormant_since_.end() ? 0 : tick_ - it->second;
}

std::array<std::size_t, static_cast<std::size_t>(SimulationTier::Count)>
SimulationScheduler::tier_counts() const {
    std::array<std::size_t, static_cast<std::size_t>(SimulationTier::Count)> counts{};
    for (const auto& [key, tier] : items_) ++counts[static_cast<std::size_t>(tier)];
    return counts;
}

} // namespace stellar::engine
