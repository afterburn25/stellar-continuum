#include <stellar/core/diplomacy_lifecycle.hpp>

#include "diplomacy_test_json.hpp"
#include <array>

namespace {
std::string lifecycle_fingerprint(const std::filesystem::path &root) {
  constexpr std::array names{"DiplomacyCampaignClock.cs",
                             "DiplomaticContactAgingService.cs",
                             "DiplomaticProposalLifecycleService.cs",
                             "DiplomaticCommunicationService.cs",
                             "DiplomaticAgreementTerminationService.cs",
                             "DiplomacyCampaignMaintenanceScheduler.cs"};
  std::string bytes;
  for (const auto *name : names) {
    bytes += name;
    bytes.push_back('\0');
    bytes += read(root / "src/Game/Simulation/Diplomacy" / name);
    bytes.push_back('\0');
  }
  return hex(detail::adaptive_research_sha256(
      {reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()}));
}
Json lifecycle_state(const DiplomacyState &state) {
  return {{"Snapshot", jsnapshot(state.snapshot())},
          {"View1", jview(state.build_view_for(1))},
          {"View2", jview(state.build_view_for(2))}};
}
Json aging(const DiplomaticContactAgingResult &value) {
  return {{"ReviewedContacts", value.reviewed_contacts},
          {"NewlyStaleContacts", value.newly_stale_contacts}};
}
Json proposals(const DiplomaticProposalLifecycleReviewResult &value) {
  return {{"PendingProposalsReviewed", value.pending_proposals_reviewed},
          {"NewlyExpiredProposals", value.newly_expired_proposals}};
}
Json termination(const DiplomaticAgreementTerminationResult &value) {
  return {{"AgreementId", value.agreement_id},
          {"AgreementType", static_cast<int>(value.agreement_type)},
          {"RequestedByCivilizationId", value.requested_by_civilization_id},
          {"Terminated", value.terminated},
          {"ClearedMutualAccess", value.cleared_mutual_access},
          {"ResumedHostility", value.resumed_hostility}};
}
Json maintenance(const DiplomacyCampaignMaintenanceResult &value) {
  return {{"Ran", value.ran},
          {"ReviewTick", value.review_tick},
          {"ContactAging", aging(value.contact_aging)},
          {"ProposalLifecycle", proposals(value.proposal_lifecycle)}};
}
DiplomacyCampaignMaintenancePolicy maintenance_policy(const Json &value) {
  return {value.at("ReviewIntervalTicks").get<std::int64_t>(),
          value.at("ContactStaleAfterTicks").get<std::int64_t>(),
          value.at("ProposalLifetimeTicks").get<std::int64_t>()};
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::invalid_argument("Expected fixture and repository root.");
    const auto fixture_path = std::filesystem::absolute(argv[1]);
    const auto root = std::filesystem::absolute(argv[2]);
    const auto fixture = Json::parse(read(fixture_path));
    require(lifecycle_fingerprint(root) == fixture.at("SourceFingerprint"),
            "source fingerprint differed");
    std::size_t count = 0;
    for (const auto &row : fixture.at("Rows")) {
      const auto name = row.at("Name").get<std::string>();
      const auto kind = row.at("Kind").get<std::string>();
      const auto before = lifecycle_fingerprint(root);
      if (kind == "scalar") {
        const auto operation = row.at("Operation").get<std::string>();
        const auto &input = row.at("Input");
        const auto day_value =
            operation == "from-days" ? number(input.at("Days")) : 0.0;
        const auto whole_days = operation == "whole-days"
                                    ? input.at("Days").get<std::int64_t>()
                                    : 0;
        std::optional<DiplomacyCampaignMaintenancePolicy> input_policy;
        if (operation == "policy")
          input_policy = maintenance_policy(input.at("Policy"));
        if (operation != "from-days" && operation != "whole-days" &&
            operation != "policy" && operation != "policy-default")
          throw std::runtime_error("bad scalar operation");
        std::optional<std::int64_t> integer_result;
        std::optional<DiplomacyCampaignMaintenancePolicy> policy_result;
        const auto actual_error = invoke([&] {
          if (operation == "from-days")
            integer_result =
                DiplomacyCampaignClock::from_simulation_days(day_value);
          else if (operation == "whole-days")
            integer_result =
                DiplomacyCampaignClock::ticks_for_whole_days(whole_days);
          else if (operation == "policy")
            input_policy->validate();
          else
            policy_result =
                DiplomacyCampaignMaintenancePolicy::early_release_default();
        });
        Json result = nullptr;
        if (!actual_error && integer_result)
          result = *integer_result;
        else if (!actual_error && policy_result)
          result = {
              {"ReviewIntervalTicks", policy_result->review_interval_ticks},
              {"ContactStaleAfterTicks",
               policy_result->contact_stale_after_ticks},
              {"ProposalLifetimeTicks",
               policy_result->proposal_lifetime_ticks}};
        error(row.at("Error"), actual_error, name);
        require(result == row.at("Result"), name + " result differed");
      } else if (kind == "scheduler") {
        const auto &input = row.at("Input");
        auto state = DiplomacyState::restore(snapshot(input.at("Snapshot")));
        struct CommandInput {
          bool reset{};
          std::int64_t now{};
          bool immediate{};
        };
        std::vector<CommandInput> commands;
        for (const auto &item : input.at("Commands"))
          commands.push_back({item.at("Op").get<std::string>() == "Reset",
                              item.at("Now").get<std::int64_t>(),
                              item.at("Immediate").get<bool>()});
        DiplomacyCampaignMaintenanceScheduler scheduler(
            state, maintenance_policy(input.at("Policy")));
        Json steps = Json::array();
        for (const auto &item : commands) {
          const auto step_before = lifecycle_fingerprint(root);
          std::optional<DiplomacyCampaignMaintenanceResult> returned;
          const auto actual_error = invoke([&] {
            if (item.reset)
              scheduler.reset(item.now, item.immediate);
            else
              returned = scheduler.review_if_due(item.now);
          });
          steps.push_back(
              {{"Op", item.reset ? "Reset" : "Review"},
               {"Error", actual_error ? Json{{"Type", actual_error->type},
                                             {"Message", actual_error->message}}
                                      : Json(nullptr)},
               {"Result", returned ? maintenance(*returned) : Json(nullptr)},
               {"NextReviewTick", scheduler.next_review_tick()},
               {"LastReviewTick", scheduler.last_review_tick()},
               {"BeforeFingerprint", step_before},
               {"AfterFingerprint", lifecycle_fingerprint(root)},
               {"State", lifecycle_state(state)}});
        }
        require(steps == row.at("Steps"), name + " steps differed");
      } else {
        const auto &input = row.at("Input");
        auto state = DiplomacyState::restore(snapshot(input.at("Snapshot")));
        const auto &arguments = input.at("Arguments");
        const auto now = arguments.contains("Now")
                             ? arguments.at("Now").get<std::int64_t>()
                             : 0;
        const auto stale = arguments.contains("Stale")
                               ? arguments.at("Stale").get<std::int64_t>()
                               : 0;
        const auto lifetime = arguments.contains("Lifetime")
                                  ? arguments.at("Lifetime").get<std::int64_t>()
                                  : 0;
        std::vector<DiplomaticProposalLifetimeOverride> overrides;
        if (arguments.contains("Overrides"))
          for (const auto &item : arguments.at("Overrides"))
            overrides.push_back({en<DiplomaticProposalKind>(item, "Kind"),
                                 item.at("Lifetime").get<std::int64_t>()});
        int first = 0;
        int second = 0;
        std::int64_t tick = 0;
        std::int64_t agreement_id = 0;
        std::string reason;
        if (kind == "communication") {
          first = arguments.at("A").get<int>();
          second = arguments.at("B").get<int>();
          tick = arguments.at("Tick").get<std::int64_t>();
        } else if (kind == "termination") {
          agreement_id = arguments.at("AgreementId").get<std::int64_t>();
          first = arguments.at("Requester").get<int>();
          tick = arguments.at("Tick").get<std::int64_t>();
          reason = arguments.at("Reason").get<std::string>();
        }
        if (kind != "aging" && kind != "proposal-lifecycle" &&
            kind != "communication" && kind != "termination")
          throw std::runtime_error("bad stateful kind");
        std::optional<DiplomaticContactAgingResult> aging_value;
        std::optional<DiplomaticProposalLifecycleReviewResult> proposal_value;
        std::optional<DiplomaticAgreementTerminationResult> termination_value;
        const auto actual_error = invoke([&] {
          if (kind == "aging")
            aging_value =
                DiplomaticContactAgingService(state).review(now, stale);
          else if (kind == "proposal-lifecycle")
            proposal_value = DiplomaticProposalLifecycleService(state).review(
                now, lifetime, overrides);
          else if (kind == "communication")
            DiplomaticCommunicationService(state)
                .establish_mutual_communication(first, second, tick);
          else
            termination_value =
                DiplomaticAgreementTerminationService(state).terminate(
                    agreement_id, first, tick, reason);
        });
        Json result = nullptr;
        if (!actual_error && aging_value)
          result = aging(*aging_value);
        else if (!actual_error && proposal_value)
          result = proposals(*proposal_value);
        else if (!actual_error && termination_value)
          result = termination(*termination_value);
        error(row.at("Error"), actual_error, name);
        require(result == row.at("Result"), name + " result differed");
        require(lifecycle_state(state) == row.at("State"),
                name + " state differed");
      }
      require(before == row.at("BeforeFingerprint").get<std::string>() &&
                  lifecycle_fingerprint(root) ==
                      row.at("AfterFingerprint").get<std::string>(),
              name + " fingerprint differed");
      ++count;
    }
    require(count == fixture.at("RowCount").get<std::size_t>(),
            "row count differed");
    DiplomacyState state;
    static_assert(!std::is_constructible_v<DiplomaticContactAgingService,
                                           DiplomacyState &&>);
    static_assert(!std::is_constructible_v<DiplomaticProposalLifecycleService,
                                           DiplomacyState &&>);
    static_assert(!std::is_constructible_v<DiplomaticCommunicationService,
                                           DiplomacyState &&>);
    static_assert(
        !std::is_constructible_v<DiplomaticAgreementTerminationService,
                                 DiplomacyState &&>);
    static_assert(
        !std::is_constructible_v<DiplomacyCampaignMaintenanceScheduler,
                                 DiplomacyState &&>);
    DiplomaticContactAgingService first(state);
    DiplomaticContactAgingService moved(std::move(first));
    require(moved.review(0, 1) == DiplomaticContactAgingResult{0, 0},
            "moved service failed");
    const DiplomaticProposalLifetimeOverride duplicate[] = {
        {DiplomaticProposalKind::demand, 1},
        {DiplomaticProposalKind::demand, 2}};
    const auto duplicate_error = invoke([&] {
      (void)DiplomaticProposalLifecycleService(state).review(0, 1, duplicate);
    });
    require(duplicate_error && duplicate_error->type == "ArgumentException" &&
                duplicate_error->message ==
                    "Duplicate proposal lifetime override kind.",
            "duplicate override boundary differed");
    const DiplomaticProposalLifetimeOverride duplicate_then_invalid[] = {
        {DiplomaticProposalKind::demand, 1},
        {DiplomaticProposalKind::demand, 2},
        {DiplomaticProposalKind::trade_offer, 0}};
    const auto value_error = invoke([&] {
      (void)DiplomaticProposalLifecycleService(state).review(
          0, 1, duplicate_then_invalid);
    });
    require(value_error && value_error->type == "ArgumentOutOfRangeException" &&
                value_error->message ==
                    "Proposal lifetime for TradeOffer must be positive. "
                    "(Parameter 'lifetimeByKind')",
            "override value validation did not precede duplicate boundary");

    DiplomacyStateSnapshot alias_input;
    alias_input.contacts = {{1, "civilization:2", 2, 1, 10, 7,
                             ContactAwareness::communication_available,
                             ContactCondition::active, true, 0.75},
                            {2, "civilization:1", 1, 1, 10, 7,
                             ContactAwareness::communication_available,
                             ContactCondition::active, true, 0.75}};
    alias_input.agreements = {{1, 1, 2, DiplomaticAgreementType::trade,
                               DiplomaticAgreementStatus::active, 5,
                               std::nullopt, std::string("aliased reason")}};
    alias_input.next_claim_id = 1;
    alias_input.next_agreement_id = 2;
    alias_input.next_proposal_id = 1;
    alias_input.next_event_id = 1;
    auto alias_state = DiplomacyState::restore(alias_input);
    const auto pair = detail::DiplomacyCivilizationPair::create(1, 2);
    auto agreements =
        detail::DiplomacyStateAccess::active_agreements(alias_state, pair);
    require(agreements.size() == 1 && agreements.front()->external_terms,
            "alias agreement missing");
    const auto borrowed_reason =
        std::string_view(*agreements.front()->external_terms);
    DiplomaticAgreementTerminationService termination_service(alias_state);
    DiplomaticAgreementTerminationService moved_termination(
        std::move(termination_service));
    const auto ended = moved_termination.terminate(1, 1, 10, borrowed_reason);
    require(ended.terminated, "moved aliased termination failed");

    DiplomaticCommunicationService communication(alias_state);
    DiplomaticCommunicationService moved_communication(
        std::move(communication));
    moved_communication.establish_mutual_communication(1, 2, 11);
    DiplomacyCampaignMaintenanceScheduler scheduler(
        alias_state, DiplomacyCampaignMaintenancePolicy{1, 100, 100});
    scheduler.reset(11, false);
    DiplomacyCampaignMaintenanceScheduler moved_scheduler(std::move(scheduler));
    require(!moved_scheduler.review_if_due(11).ran,
            "moved scheduler cadence failed");
    std::cout << "Diplomacy lifecycle parity passed " << count
              << " actual-source rows plus the duplicate-key boundary.\n";
    return 0;
  } catch (const std::exception &caught) {
    std::cerr << "type=" << typeid(caught).name() << '\n'
              << "message=" << caught.what() << '\n'
              << "cwd=" << std::filesystem::current_path() << '\n'
              << "fixture=" << (argc > 1 ? argv[1] : "<missing>") << '\n'
              << "repoRoot=" << (argc > 2 ? argv[2] : "<missing>") << '\n';
    return 1;
  }
}
