#include <stellar/engine/strategic_ai.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace stellar::engine {

void StrategicMind::add_action(UtilityAction action) {
    actions_[action.id] = std::move(action);
}

bool StrategicMind::remove_action(std::string_view id) {
    return actions_.erase(std::string(id)) != 0;
}

bool StrategicMind::set_enabled(std::string_view id, bool enabled) {
    auto it = actions_.find(std::string(id));
    if (it == actions_.end()) return false;
    it->second.enabled = enabled;
    return true;
}

const UtilityAction* StrategicMind::action(std::string_view id) const {
    auto it = actions_.find(std::string(id));
    return it == actions_.end() ? nullptr : &it->second;
}

std::vector<std::string>
StrategicMind::action_ids(std::string_view domain) const {
    std::vector<std::string> out;
    for (const auto& [id, a] : actions_) {
        if (a.domain == domain) out.push_back(id);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::optional<std::string> StrategicMind::decide(std::string_view domain,
                                                 double now_day,
                                                 double min_utility,
                                                 double hysteresis) {
    const auto ids = action_ids(domain);
    const auto inc_it = incumbents_.find(std::string(domain));
    const std::string incumbent_id =
        inc_it == incumbents_.end() ? std::string() : inc_it->second;

    const UtilityAction* best = nullptr;
    double best_u = -std::numeric_limits<double>::infinity();
    std::uint32_t scored = 0;

    for (const auto& id : ids) {
        auto& a = actions_[id];
        if (!a.enabled || !a.score) continue;
        // Cooldown on the ACTION, measured from its last commit.
        if (a.cooldown_days > 0.0) {
            const auto lc = last_commit_.find(id);
            if (lc != last_commit_.end() &&
                now_day - lc->second < a.cooldown_days) {
                continue;
            }
        }
        double u = a.score() * a.weight;
        if (!std::isfinite(u)) continue;
        if (id == incumbent_id) u *= hysteresis;
        ++scored;
        // Ascending id order + strictly-greater → first max wins ties.
        if (!best || u > best_u) {
            best = &a;
            best_u = u;
        }
    }

    if (!best || best_u < min_utility) return std::nullopt;

    const bool switched = !incumbent_id.empty() && best->id != incumbent_id;
    if (best->commit) best->commit();
    incumbents_[std::string(domain)] = best->id;
    last_commit_[best->id] = now_day;

    Decision d;
    d.at_day = now_day;
    d.domain = std::string(domain);
    d.action_id = best->id;
    d.utility = best_u;
    d.candidates = scored;
    d.switched = switched;
    journal_.push_back(std::move(d));
    if (journal_.size() > journal_capacity_) journal_.pop_front();
    return best->id;
}

std::optional<std::string>
StrategicMind::incumbent(std::string_view domain) const {
    const auto it = incumbents_.find(std::string(domain));
    if (it == incumbents_.end()) return std::nullopt;
    return it->second;
}

double StrategicMind::last_commit_day(std::string_view domain) const {
    const auto inc = incumbent(domain);
    if (!inc) return 0.0;
    const auto it = last_commit_.find(*inc);
    return it == last_commit_.end() ? 0.0 : it->second;
}

} // namespace stellar::engine
