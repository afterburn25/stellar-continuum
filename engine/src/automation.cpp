#include <stellar/engine/automation.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace stellar::engine {

namespace {

double knob_value(const std::vector<AutomationKnob> &knobs,
                  std::string_view id, double fallback) {
    const auto it = std::find_if(
        knobs.begin(), knobs.end(),
        [&](const AutomationKnob &knob) { return knob.id == id; });
    return it == knobs.end() ? fallback : it->value;
}

std::vector<AutomationKnob>
sorted_knobs(std::vector<AutomationKnob> knobs) {
    std::sort(knobs.begin(), knobs.end(),
              [](const AutomationKnob &a, const AutomationKnob &b) {
                  return a.id < b.id;
              });
    return knobs;
}

bool lock_order(const AutomationOverrideLock &a,
                const AutomationOverrideLock &b) {
    if (a.domain != b.domain) return a.domain < b.domain;
    return a.target < b.target;
}

} // namespace

std::string_view automation_mode_name(AutomationMode mode) noexcept {
    switch (mode) {
    case AutomationMode::Off: return "off";
    case AutomationMode::Advisory: return "advisory";
    case AutomationMode::Assisted: return "assisted";
    case AutomationMode::Automatic: return "automatic";
    }
    return "off";
}

std::optional<AutomationMode>
automation_mode_from_name(std::string_view name) noexcept {
    if (name == "off") return AutomationMode::Off;
    if (name == "advisory") return AutomationMode::Advisory;
    if (name == "assisted") return AutomationMode::Assisted;
    if (name == "automatic") return AutomationMode::Automatic;
    return std::nullopt;
}

AutomationController::AutomationController(std::size_t journal_capacity)
    : mind_(journal_capacity), journal_capacity_(journal_capacity) {}

void AutomationController::set_domain_policy(
    AutomationDomainPolicy policy) {
    policy.policies = sorted_knobs(std::move(policy.policies));
    policy.constraints = sorted_knobs(std::move(policy.constraints));
    policies_[policy.domain] = std::move(policy);
}

void AutomationController::set_mode(std::string_view domain,
                                    AutomationMode mode) {
    auto &policy = policies_[std::string(domain)];
    policy.domain = std::string(domain);
    policy.mode = mode;
}

const AutomationDomainPolicy *
AutomationController::domain_policy(std::string_view domain) const {
    const auto it = policies_.find(std::string(domain));
    return it == policies_.end() ? nullptr : &it->second;
}

AutomationMode AutomationController::mode(std::string_view domain) const {
    const auto *policy = domain_policy(domain);
    return policy ? policy->mode : AutomationMode::Off;
}

double AutomationController::policy_knob(std::string_view domain,
                                         std::string_view id,
                                         double fallback) const {
    const auto *policy = domain_policy(domain);
    if (!policy) return fallback;
    return knob_value(policy->policies, id, fallback);
}

double AutomationController::constraint_value(std::string_view domain,
                                              std::string_view id,
                                              double fallback) const {
    const auto *policy = domain_policy(domain);
    if (!policy) return fallback;
    return knob_value(policy->constraints, id, fallback);
}

std::vector<std::string> AutomationController::domains() const {
    std::set<std::string> unique;
    for (const auto &[domain, _] : policies_) unique.insert(domain);
    unique.insert(action_domains_.begin(), action_domains_.end());
    return {unique.begin(), unique.end()};
}

void AutomationController::add_action(
    UtilityAction action, std::function<std::string()> explain,
    std::function<std::vector<std::string>()> constraint_report) {
    const std::string id = action.id;
    action_domains_.insert(action.domain);
    mind_.add_action(std::move(action));
    explainers_[id] =
        Explainer{std::move(explain), std::move(constraint_report)};
}

bool AutomationController::remove_action(std::string_view id) {
    explainers_.erase(std::string(id));
    return mind_.remove_action(id);
}

void AutomationController::clear_actions(std::string_view domain) {
    for (const auto &id : mind_.action_ids(domain)) {
        explainers_.erase(id);
        (void)mind_.remove_action(id);
    }
    action_domains_.erase(std::string(domain));
}

std::size_t
AutomationController::action_count(std::string_view domain) const {
    return mind_.action_ids(domain).size();
}

void AutomationController::record_operator_override(
    std::string_view domain, std::string_view target, double now_day,
    double lock_days, std::string_view source) {
    if (domain.empty() || target.empty()) return;
    auto &slot = locks_[std::string(domain)][std::string(target)];
    slot.domain = std::string(domain);
    slot.target = std::string(target);
    slot.locked_until_day =
        now_day + (std::isfinite(lock_days) ? std::max(0.0, lock_days)
                                          : 0.0);
    slot.source = std::string(source);
}

void AutomationController::clear_override(std::string_view domain,
                                          std::string_view target) {
    const auto domain_it = locks_.find(std::string(domain));
    if (domain_it == locks_.end()) return;
    domain_it->second.erase(std::string(target));
    if (domain_it->second.empty()) locks_.erase(domain_it);
}

bool AutomationController::target_locked(std::string_view domain,
                                         std::string_view target,
                                         double now_day) const {
    const auto domain_it = locks_.find(std::string(domain));
    if (domain_it == locks_.end()) return false;
    const auto lock_it = domain_it->second.find(std::string(target));
    if (lock_it == domain_it->second.end()) return false;
    return now_day < lock_it->second.locked_until_day;
}

std::vector<AutomationOverrideLock> AutomationController::locks() const {
    std::vector<AutomationOverrideLock> out;
    for (const auto &[_, targets] : locks_)
        for (const auto &[__, lock] : targets) out.push_back(lock);
    std::sort(out.begin(), out.end(), lock_order);
    return out;
}

std::optional<std::string>
AutomationController::decide(std::string_view domain, double now_day) {
    const auto *policy = domain_policy(domain);
    const AutomationMode mode =
        policy ? policy->mode : AutomationMode::Off;
    if (mode == AutomationMode::Off) return std::nullopt;

    const double hysteresis = policy ? policy->hysteresis : 1.1;
    const double min_utility = policy ? policy->min_utility : 0.0;

    if (mode == AutomationMode::Advisory) {
        const auto ranked = mind_.rank(domain, now_day, hysteresis);
        if (ranked.empty()) return std::nullopt;
        // Bounded proposal signal: journal only when the top pick
        // changes, so centuries of identical advice stay one entry.
        const std::string &top = ranked.front().action_id;
        const auto last = last_proposal_.find(std::string(domain));
        if (last != last_proposal_.end() && last->second == top)
            return std::nullopt;
        last_proposal_[std::string(domain)] = top;

        AutomationJournalEntry entry;
        entry.at_day = now_day;
        entry.domain = std::string(domain);
        entry.action_id = top;
        entry.target = ranked.front().target;
        entry.mode = mode;
        entry.utility = ranked.front().utility;
        entry.alternatives = alternatives(ranked, 1);
        const auto explainer = explainers_.find(top);
        if (explainer != explainers_.end()) {
            if (explainer->second.reason)
                entry.reason = explainer->second.reason();
            if (explainer->second.constraints)
                entry.constraints = explainer->second.constraints();
        }
        append_journal(std::move(entry));
        return std::nullopt;
    }

    const bool routine_only = mode == AutomationMode::Assisted;
    const std::string domain_key(domain);
    ActionEligibility eligible =
        [this, routine_only, now_day,
         &domain_key](const UtilityAction &action) {
            if (routine_only && !action.routine) return false;
            if (!action.target.empty() &&
                target_locked(domain_key, action.target, now_day))
                return false;
            return true;
        };

    auto winner =
        mind_.decide(domain, now_day, min_utility, hysteresis,
                     std::move(eligible));
    if (!winner) return std::nullopt;

    const auto &decisions = mind_.journal();
    const Decision &decision = decisions.back();

    AutomationJournalEntry entry;
    entry.at_day = now_day;
    entry.domain = std::string(domain);
    entry.action_id = decision.action_id;
    entry.target = decision.target;
    entry.mode = mode;
    entry.executed = true;
    entry.switched = decision.switched;
    entry.utility = decision.utility;
    const auto explainer = explainers_.find(decision.action_id);
    if (explainer != explainers_.end()) {
        if (explainer->second.reason)
            entry.reason = explainer->second.reason();
        if (explainer->second.constraints)
            entry.constraints = explainer->second.constraints();
    }
    append_journal(std::move(entry));
    ++executed_;
    return winner;
}

std::vector<RankedCandidate>
AutomationController::evaluate(std::string_view domain,
                               double now_day) const {
    const auto *policy = domain_policy(domain);
    const double hysteresis = policy ? policy->hysteresis : 1.1;
    auto ranked = mind_.rank(domain, now_day, hysteresis);
    // Locked targets are reported but flagged by the caller — locks
    // are caller-visible through target_locked()/locks().
    return ranked;
}

const std::deque<AutomationJournalEntry> &
AutomationController::journal() const noexcept {
    return journal_;
}

std::uint64_t AutomationController::executed_count() const noexcept {
    return executed_;
}

const StrategicMind &AutomationController::mind() const noexcept {
    return mind_;
}

AutomationController::State AutomationController::capture_state() const {
    State state;
    for (const auto &[_, policy] : policies_)
        state.policies.push_back(policy);
    std::sort(state.policies.begin(), state.policies.end(),
              [](const AutomationDomainPolicy &a,
                 const AutomationDomainPolicy &b) {
                  return a.domain < b.domain;
              });
    state.locks = locks();
    state.mind = mind_.capture_state();
    state.journal = journal_;
    return state;
}

void AutomationController::restore_state(const State &state) {
    policies_.clear();
    locks_.clear();
    journal_.clear();
    last_proposal_.clear();
    executed_ = 0;
    for (const auto &policy : state.policies) policies_[policy.domain] = policy;
    for (const auto &lock : state.locks)
        locks_[lock.domain][lock.target] = lock;
    mind_.restore_state(state.mind);
    journal_ = state.journal;
    while (journal_.size() > journal_capacity_) journal_.pop_front();
}

void AutomationController::append_journal(AutomationJournalEntry entry) {
    journal_.push_back(std::move(entry));
    while (journal_.size() > journal_capacity_) journal_.pop_front();
}

std::vector<AutomationAlternative> AutomationController::alternatives(
    const std::vector<RankedCandidate> &ranked, std::size_t skip_first,
    std::size_t maximum) {
    std::vector<AutomationAlternative> out;
    for (std::size_t i = skip_first; i < ranked.size() &&
                                     out.size() < maximum;
         ++i)
        out.push_back({ranked[i].action_id, ranked[i].utility});
    return out;
}

} // namespace stellar::engine
