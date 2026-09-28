#pragma once

#include <stellar/engine/strategic_ai.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace stellar::engine {

// Automation — the engine's reusable, deterministic automation
// framework. One AutomationController per automated scope (a
// civilization, a colony, a fleet group — the caller picks the
// granularity). Domains hold the mode and the named policy/constraint
// knobs; candidates are ordinary StrategicMind UtilityActions so the
// argmax/hysteresis/cooldown/determinism contract is shared.
//
// Modes:
//   Off       — the domain never runs.
//   Advisory  — the domain evaluates and journals proposals, never
//               commits. Player-facing "suggest" tier.
//   Assisted  — commits only routine actions; strategic actions are
//               left to the operator.
//   Automatic — commits the winning action (routine or strategic).
//
// Operator control: manual actions on a target create an
// AutomationOverrideLock (record_operator_override) — automation skips
// locked targets until the lock expires, then resumes through the
// normal hysteresis path rather than snapping back. Locks are
// scheduling state and persist with the controller.
//
// Determinism contract: identical to StrategicMind — ascending action
// id order, first-max wins ties, fixed hysteresis multiplier, day-
// based cooldowns, sorted lock/policy/journal serialization. Scorers
// must be pure functions of observable state.

enum class AutomationMode { Off = 0, Advisory, Assisted, Automatic };

[[nodiscard]] std::string_view automation_mode_name(AutomationMode mode) noexcept;
[[nodiscard]] std::optional<AutomationMode>
automation_mode_from_name(std::string_view name) noexcept;

// A named knob on a domain. `policies` steer candidate scoring
// (priorities, posture, risk appetite); `constraints` bound execution
// (reserves, caps, thresholds). Units are defined by the consumer and
// read back through policy_knob()/constraint_value().
struct AutomationKnob {
    std::string id;
    double value{0.0};
};

struct AutomationDomainPolicy {
    std::string domain;
    AutomationMode mode{AutomationMode::Off};
    // Scales every candidate's utility in this domain — meaningful
    // only when a caller arbitrates budgets ACROSS domains.
    double priority{1.0};
    double hysteresis{1.1};
    double min_utility{0.0};
    std::vector<AutomationKnob> policies;    // sorted by id
    std::vector<AutomationKnob> constraints; // sorted by id
};

// Operator lock: a manual action on `target` pauses automation for
// that target until `locked_until_day`, after which ordinary policy
// resumes. `source` records who wrote the lock ("operator" by
// convention — reserved for other producers).
struct AutomationOverrideLock {
    std::string domain;
    std::string target;
    double locked_until_day{0.0};
    std::string source;
};

struct AutomationAlternative {
    std::string action_id;
    double utility{0.0};
};

// Bounded explanation record — the "why" behind every automated act.
// Proposals (Advisory, executed=false), commits, and lock/ mode-skips
// share the journal so a reader sees the complete decision stream.
struct AutomationJournalEntry {
    double at_day{0.0};
    std::string domain;
    std::string action_id;
    std::string target;
    AutomationMode mode{AutomationMode::Off};
    bool executed{false};
    bool switched{false};          // displaced the domain incumbent
    double utility{0.0};
    std::string reason;                              // caller-supplied
    std::vector<AutomationAlternative> alternatives; // bounded
    std::vector<std::string> constraints;            // "name: satisfied"
};

class AutomationController {
public:
    explicit AutomationController(std::size_t journal_capacity = 128);

    // --- domain policy -------------------------------------------------
    void set_domain_policy(AutomationDomainPolicy policy);
    void set_mode(std::string_view domain, AutomationMode mode);
    [[nodiscard]] const AutomationDomainPolicy *
    domain_policy(std::string_view domain) const;
    [[nodiscard]] AutomationMode mode(std::string_view domain) const;
    [[nodiscard]] double policy_knob(std::string_view domain,
                                     std::string_view id,
                                     double fallback = 0.0) const;
    [[nodiscard]] double constraint_value(std::string_view domain,
                                          std::string_view id,
                                          double fallback = 0.0) const;
    // Domains with a policy or registered actions, sorted.
    [[nodiscard]] std::vector<std::string> domains() const;

    // --- candidates ----------------------------------------------------
    // Register/replace a candidate (same id replaces). `explain`
    // produces the journal's reason line; `constraint_report` produces
    // the satisfied/blocked constraint lines — both are evaluated at
    // decision time and must be pure functions of observable state.
    void add_action(UtilityAction action,
                    std::function<std::string()> explain = {},
                    std::function<std::vector<std::string>()> constraint_report = {});
    bool remove_action(std::string_view id);
    void clear_actions(std::string_view domain);
    [[nodiscard]] std::size_t action_count(std::string_view domain) const;

    // --- operator locks --------------------------------------------------
    void record_operator_override(std::string_view domain,
                                  std::string_view target, double now_day,
                                  double lock_days,
                                  std::string_view source = "operator");
    void clear_override(std::string_view domain, std::string_view target);
    [[nodiscard]] bool target_locked(std::string_view domain,
                                     std::string_view target,
                                     double now_day) const;
    [[nodiscard]] std::vector<AutomationOverrideLock> locks() const; // sorted

    // --- decisions -----------------------------------------------------
    // Mode-gated evaluation and commit for one domain:
    //   Off       → returns nullopt, nothing evaluated or journaled.
    //   Advisory  → ranks candidates; journals a proposal only when the
    //               top pick differs from the domain's last proposal
    //               (bounded signal, no per-tick spam). Never commits.
    //   Assisted  → commits the winner only when routine; non-routine
    //               actions are filtered out before argmax.
    //   Automatic → commits the argmax winner.
    // Locked targets are always filtered out. Returns the committed id.
    std::optional<std::string> decide(std::string_view domain,
                                      double now_day);
    // Pure advisory snapshot — the same ranking decide() would see,
    // without journaling or committing.
    [[nodiscard]] std::vector<RankedCandidate>
    evaluate(std::string_view domain, double now_day) const;

    [[nodiscard]] const std::deque<AutomationJournalEntry> &
    journal() const noexcept;
    [[nodiscard]] std::uint64_t executed_count() const noexcept;
    [[nodiscard]] const StrategicMind &mind() const noexcept;

    // --- persistence ---------------------------------------------------
    // Policies, locks, incumbents/cooldowns and the bounded journal are
    // authoritative automation state and round-trip through State.
    // Actions/explainers are code — the owner re-registers them, then
    // restores this state.
    struct DomainProposal {
        std::string domain;
        std::string action_id;
    };
    struct State {
        std::uint32_t version{1};
        std::vector<AutomationDomainPolicy> policies; // sorted by domain
        std::vector<AutomationOverrideLock> locks;    // sorted
        StrategicMind::State mind;
        std::deque<AutomationJournalEntry> journal;
        // Last advisedly-proposed top pick per domain — the suppression
        // that keeps advisory mode from re-journaling identical advice.
        // Without it a restored controller re-proposes once per domain
        // and save→load→continue diverges from the uninterrupted run.
        std::vector<DomainProposal> last_proposals;   // sorted by domain
    };
    [[nodiscard]] State capture_state() const;
    void restore_state(const State &state);

private:
    struct Explainer {
        std::function<std::string()> reason;
        std::function<std::vector<std::string>()> constraints;
    };
    void append_journal(AutomationJournalEntry entry);
    [[nodiscard]] static std::vector<AutomationAlternative>
    alternatives(const std::vector<RankedCandidate> &ranked,
                 std::size_t skip_first, std::size_t maximum = 4);

    std::unordered_map<std::string, AutomationDomainPolicy> policies_;
    // domain -> target -> lock
    std::unordered_map<std::string,
                       std::unordered_map<std::string, AutomationOverrideLock>>
        locks_;
    StrategicMind mind_;
    std::set<std::string> action_domains_;
    std::unordered_map<std::string, Explainer> explainers_;
    std::unordered_map<std::string, std::string> last_proposal_;
    std::deque<AutomationJournalEntry> journal_;
    std::size_t journal_capacity_;
    std::uint64_t executed_{};
};

} // namespace stellar::engine
