#include <stellar/core/adaptive_research_funding.hpp>

#include <stellar/core/detail/adaptive_research_campaign_state_access.hpp>
#include <stellar/core/sovereign_currency.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace stellar::core {
namespace {

std::string trim_lower(std::string_view value) {
  const auto width = [](unsigned char item) {
    return item < 0x80             ? 1U
           : (item & 0xe0) == 0xc0 ? 2U
           : (item & 0xf0) == 0xe0 ? 3U
           : (item & 0xf8) == 0xf0 ? 4U
                                    : 1U;
  };
  const auto whitespace = [](std::string_view unit) {
    if (unit.size() == 1) {
      const auto item = static_cast<unsigned char>(unit.front());
      return item == 0x20 || (item >= 0x09 && item <= 0x0d);
    }
    return unit == "\xc2\x85" || unit == "\xc2\xa0" ||
           unit == "\xe1\x9a\x80" ||
           (unit.size() == 3 && unit[0] == '\xe2' &&
            ((unit[1] == '\x80' &&
              static_cast<unsigned char>(unit[2]) >= 0x80 &&
              static_cast<unsigned char>(unit[2]) <= 0x8a) ||
             unit == "\xe2\x80\xa8" || unit == "\xe2\x80\xa9" ||
             unit == "\xe2\x80\xaf" || unit == "\xe2\x81\x9f")) ||
           unit == "\xe3\x80\x80";
  };
  while (!value.empty()) {
    const auto count = width(static_cast<unsigned char>(value.front()));
    if (count > value.size() || !whitespace(value.substr(0, count))) break;
    value.remove_prefix(count);
  }
  while (!value.empty()) {
    std::size_t start = value.size() - 1;
    while (start > 0 &&
           (static_cast<unsigned char>(value[start]) & 0xc0) == 0x80)
      --start;
    if (!whitespace(value.substr(start))) break;
    value.remove_suffix(value.size() - start);
  }
  std::string result(value);
  std::ranges::transform(result, result.begin(), [](unsigned char item) {
    return item >= 'A' && item <= 'Z' ? static_cast<char>(item + 32)
                                     : static_cast<char>(item);
  });
  return result;
}

CivilizationEconomy *single_economy(
    std::span<CivilizationEconomy> economies, int civilization_id) {
  CivilizationEconomy *found{};
  for (auto &economy : economies) {
    if (economy.civilization_id != civilization_id)
      continue;
    if (found)
      throw AdaptiveResearchFundingSequenceError(
          "Sequence contains more than one matching element");
    found = &economy;
  }
  return found;
}

const ResearchProjectRuntimeState *find_project(
    const AdaptiveResearchCivilizationState &state, std::string_view node_id) {
  const auto found = std::ranges::find(state.active_projects(), node_id,
                                       &ResearchProjectRuntimeState::node_id);
  return found == state.active_projects().end() ? nullptr : &*found;
}

bool has_funding(const AdaptiveResearchCampaignState &campaign,
                 int civilization_id, std::string_view node_id) {
  return std::ranges::any_of(campaign.project_funding(civilization_id),
                             [&](const auto &entry) {
                               return entry.node_id == node_id;
                             });
}

} // namespace

AdaptiveResearchFundingArgumentRangeError::
    AdaptiveResearchFundingArgumentRangeError(std::string message)
    : std::out_of_range(std::move(message)) {}

AdaptiveResearchFundingPolicyError::AdaptiveResearchFundingPolicyError(
    std::string message)
    : std::runtime_error(std::move(message)) {}

AdaptiveResearchFundingSequenceError::AdaptiveResearchFundingSequenceError(
    std::string message)
    : std::runtime_error(std::move(message)) {}

double AdaptiveResearchFundingPolicy::authorization_credits(
    std::string_view complexity) {
  const auto normalized = trim_lower(complexity);
  if (normalized == "foundation") return 0.5;
  if (normalized == "developing") return 2.0;
  if (normalized == "advanced") return 7.5;
  if (normalized == "frontier") return 25.0;
  throw AdaptiveResearchFundingPolicyError(
      "Research complexity '" + std::string(complexity) +
      "' has no authorization cost policy.");
}

double AdaptiveResearchFundingPolicy::milestone_commitment_credits(
    std::string_view complexity) {
  const auto normalized = trim_lower(complexity);
  if (normalized == "foundation") return 0.3;
  if (normalized == "developing") return 1.0;
  if (normalized == "advanced") return 3.0;
  if (normalized == "frontier") return 8.0;
  throw AdaptiveResearchFundingPolicyError(
      "Research complexity '" + std::string(complexity) +
      "' has no milestone cost policy.");
}

double AdaptiveResearchFundingPolicy::complexity_multiplier(
    std::string_view complexity) {
  const auto normalized = trim_lower(complexity);
  if (normalized == "foundation") return 0.75;
  if (normalized == "developing") return 1.25;
  if (normalized == "advanced") return 2.5;
  if (normalized == "frontier") return 5.0;
  throw AdaptiveResearchFundingPolicyError(
      "Research complexity '" + std::string(complexity) +
      "' has no financial cost policy.");
}

AdaptiveResearchFundingQuote AdaptiveResearchFundingPolicy::quote(
    const AdaptiveResearchNodeDefinition &node, double assigned_effective_labs,
    const AdaptiveResearchCatalog &catalog) {
  if (!std::isfinite(assigned_effective_labs) || assigned_effective_labs < 0)
    throw AdaptiveResearchFundingArgumentRangeError(
        "Specified argument was out of the range of valid values. "
        "(Parameter 'assignedEffectiveLabs')");
  const auto multiplier = complexity_multiplier(node.complexity);
  const auto authorization = authorization_credits(node.complexity);
  const auto milestone = milestone_commitment_credits(node.complexity);
  const auto annual = assigned_effective_labs *
                      base_annual_credits_per_effective_lab * multiplier;
  const auto operating_per_day = annual / 365.25;
  const auto scaled = catalog.lab_scaling().scale_assigned_labs(
      assigned_effective_labs, node.project_requirements.recommended_labs);
  const auto rp_per_year =
      scaled * catalog.metadata().base_rp_per_effective_lab_per_year;
  const auto estimated_years =
      rp_per_year <= 0 ? std::numeric_limits<double>::infinity()
                       : node.project_requirements.base_research_points /
                             rp_per_year;
  const auto estimated_operating =
      std::isfinite(estimated_years)
          ? annual * estimated_years
          : std::numeric_limits<double>::infinity();
  return {assigned_effective_labs,
          authorization,
          milestone,
          operating_per_day,
          estimated_operating,
          authorization + milestone + estimated_operating,
          estimated_years,
          node.complexity};
}

double AdaptiveResearchFundingPolicy::estimate_treasury_runway_days(
    double available_credits, double net_credits_per_day_before_research,
    double research_operating_credits_per_day) {
  if (!std::isfinite(available_credits) || available_credits < 0)
    throw AdaptiveResearchFundingArgumentRangeError(
        "Specified argument was out of the range of valid values. "
        "(Parameter 'availableCredits')");
  if (!std::isfinite(net_credits_per_day_before_research))
    throw AdaptiveResearchFundingArgumentRangeError(
        "Specified argument was out of the range of valid values. "
        "(Parameter 'netCreditsPerDayBeforeResearch')");
  if (!std::isfinite(research_operating_credits_per_day) ||
      research_operating_credits_per_day < 0)
    throw AdaptiveResearchFundingArgumentRangeError(
        "Specified argument was out of the range of valid values. "
        "(Parameter 'researchOperatingCreditsPerDay')");
  const auto net_burn = research_operating_credits_per_day -
                        net_credits_per_day_before_research;
  return net_burn <= 0.0000001
             ? std::numeric_limits<double>::infinity()
             : available_credits / net_burn;
}

double AdaptiveResearchCampaignCommands::credits_needed_to_start(
    const AdaptiveResearchFundingQuote &quote) noexcept {
  return quote.authorization_credits + quote.milestone_commitment_credits +
         quote.operating_credits_per_day;
}

std::optional<double> AdaptiveResearchCampaignCommands::cancellation_refund(
    const AdaptiveResearchCampaignState &campaign, int id, std::string_view node_id) {
  const auto &state = campaign.get_civilization(id);
  if (!find_project(state, node_id)) return std::nullopt;
  for (const auto &funding : campaign.project_funding(id))
    if (funding.node_id == node_id)
      return std::max(0., funding.reserved_milestone_credits - funding.consumed_milestone_credits);
  return 0.;
}

AdaptiveResearchCommandResult AdaptiveResearchCampaignCommands::cancel_directed_research(
    AdaptiveResearchFundingWorldView world, AdaptiveResearchCampaignState &campaign,
    int id, std::string_view node_id) {
  const std::string owned_node(node_id);
  AdaptiveResearchCivilizationState *state{};
  std::optional<double> refund;
  try {
    state = &detail::AdaptiveResearchCampaignStateAccess::get_civilization(campaign, id);
    refund = cancellation_refund(campaign, id, owned_node);
  } catch (const AdaptiveResearchCampaignMissingState &error) {
    return AdaptiveResearchCommandResult::rejected(error.what());
  }
  if (!refund) return AdaptiveResearchCommandResult::rejected("No active research project exists for that node.");
  auto *economy = single_economy(world.economies, id);
  if (!economy || !std::isfinite(economy->credits) || !std::isfinite(economy->credits + *refund))
    return AdaptiveResearchCommandResult::rejected("No valid treasury is available to receive the research refund.");
  const auto currency = sovereign_currency_for_civilization(world.civilizations, id);
  auto result = campaign.runtime().authority().cancel_directed_research(*state, owned_node);
  if (!result.accepted) return result;
  detail::AdaptiveResearchCampaignStateAccess::release_project_funding(campaign, id, owned_node);
  economy->credits += *refund;
  // Remove explicit waiting intent too, so cancellation cannot restart on the
  // next queue tick. AI takeover remains an independently authorized controller.
  const auto &queue = campaign.plan(id).queue;
  if (std::ranges::find(queue, owned_node) != queue.end())
    (void)campaign.edit_plan(id, ResearchPlanCommand::RemoveQueued, owned_node);
  result.message += " Refunded " + currency.format(*refund) +
      " in unused milestone funds. Authorization and operating costs are not refundable. "
      "Restarting requires new authorization and milestone funding.";
  if (!result.events.empty()) result.events.front().message = result.message;
  return result;
}

std::vector<AdaptiveResearchRuntimeEvent> AdaptiveResearchCampaignCommands::start_queued_research(
    AdaptiveResearchFundingWorldView world,AdaptiveResearchCampaignState &campaign,int id){
  std::vector<AdaptiveResearchRuntimeEvent> events;
  const auto &state=campaign.get_civilization(id);
  while(!campaign.plan(id).queue.empty()){
    const auto node_id=campaign.plan(id).queue.front();
    const auto *node_state=state.try_get_node_state(node_id);
    if(!node_state||node_state->maturity<ResearchMaturity::investigable)break;
    const bool active=std::any_of(state.active_projects().begin(),state.active_projects().end(),
        [&](const auto &p){return p.node_id==node_id;});
    if(!active&&node_state->maturity<ResearchMaturity::mature){
      const auto &node=campaign.runtime().authority().catalog().get_node(node_id);
      const double labs=std::min<double>(node.project_requirements.recommended_labs,state.free_effective_labs());
      auto started=start_directed_research(world,campaign,id,node_id,labs,
          campaign.get_start(id).applicability_context_id);
      if(!started.accepted)break;
      events.insert(events.end(),started.events.begin(),started.events.end());
    }
    const auto removed=campaign.edit_plan(id,ResearchPlanCommand::RemoveQueued,node_id);
    if(!removed.accepted)break;
  }
  return events;
}

AdaptiveResearchCommandResult
AdaptiveResearchCampaignCommands::start_directed_research(
    AdaptiveResearchFundingWorldView world,
    AdaptiveResearchCampaignState &campaign, int civilization_id,
    std::string_view node_id, double requested_assigned_labs,
    std::optional<std::string_view> target_context) {
  const std::string owned_node(node_id);
  const auto owned_context = target_context
                                 ? std::optional<std::string>(*target_context)
                                 : std::nullopt;
  auto *economy = single_economy(world.economies, civilization_id);
  if (!economy)
    return AdaptiveResearchCommandResult::rejected(
        "Civilization " + std::to_string(civilization_id) +
        " has no economy available to fund research.");

  AdaptiveResearchCivilizationState *state{};
  const AdaptiveResearchNodeDefinition *node{};
  try {
    state = &detail::AdaptiveResearchCampaignStateAccess::get_civilization(
        campaign, civilization_id);
    node = &campaign.runtime().authority().catalog().get_node(owned_node);
  } catch (const AdaptiveResearchCampaignMissingState &error) {
    return AdaptiveResearchCommandResult::rejected(error.what());
  } catch (const std::out_of_range &error) {
    return AdaptiveResearchCommandResult::rejected(error.what());
  }

  AdaptiveResearchFundingQuote quote;
  try {
    quote = AdaptiveResearchFundingPolicy::quote(
        *node, requested_assigned_labs,
        campaign.runtime().authority().catalog());
  } catch (const AdaptiveResearchFundingArgumentRangeError &error) {
    return AdaptiveResearchCommandResult::rejected(error.what());
  } catch (const AdaptiveResearchFundingPolicyError &error) {
    return AdaptiveResearchCommandResult::rejected(error.what());
  }

  if (has_funding(campaign, civilization_id, owned_node))
    return AdaptiveResearchCommandResult::rejected(
        node->name + " already has an active milestone funding commitment.");
  const auto first_day = credits_needed_to_start(quote);
  const auto currency =
      sovereign_currency_for_civilization(world.civilizations, civilization_id);
  if (economy->credits + 0.000001 < first_day)
    return AdaptiveResearchCommandResult::rejected(
        node->name + " requires " + currency.format(quote.authorization_credits) +
        " to authorize and " +
        currency.format(quote.milestone_commitment_credits) +
        " for prototype milestones, plus " +
        currency.format(quote.operating_credits_per_day) +
        " for its first operating day; " + currency.format(economy->credits) +
        " is available.");
  auto result = campaign.runtime().authority().start_directed_research(
      *state, owned_node, requested_assigned_labs,
      owned_context ? std::optional<std::string_view>(*owned_context)
                    : std::nullopt);
  if (!result.accepted) return result;
  detail::AdaptiveResearchCampaignStateAccess::reserve_project_milestones(
      campaign, civilization_id, owned_node, quote.authorization_credits,
      quote.milestone_commitment_credits);
  economy->credits -=
      quote.authorization_credits + quote.milestone_commitment_credits;
  result.message += " Authorized for " +
                    currency.format(quote.authorization_credits) + "; " +
                    currency.format(quote.milestone_commitment_credits) +
                    " reserved for prototypes and validation; planned "
                    "operations cost " +
                    currency.format_rate(-quote.operating_credits_per_day) +
                    ".";
  return result;
}

AdaptiveResearchCommandResult
AdaptiveResearchCampaignCommands::pause_directed_research(
    AdaptiveResearchCampaignState &campaign, int civilization_id,
    std::string_view node_id) {
  const std::string owned_node(node_id);
  AdaptiveResearchCivilizationState *state{};
  try {
    state = &detail::AdaptiveResearchCampaignStateAccess::get_civilization(
        campaign, civilization_id);
  } catch (const AdaptiveResearchCampaignMissingState &error) {
    return AdaptiveResearchCommandResult::rejected(error.what());
  }
  return campaign.runtime().authority().pause_directed_research(*state,
                                                                 owned_node);
}

AdaptiveResearchCommandResult
AdaptiveResearchCampaignCommands::resume_directed_research(
    AdaptiveResearchFundingWorldView world,
    AdaptiveResearchCampaignState &campaign, int civilization_id,
    std::string_view node_id, double requested_assigned_labs) {
  const std::string owned_node(node_id);
  AdaptiveResearchCivilizationState *state{};
  const AdaptiveResearchNodeDefinition *node{};
  try {
    state = &detail::AdaptiveResearchCampaignStateAccess::get_civilization(
        campaign, civilization_id);
    node = &campaign.runtime().authority().catalog().get_node(owned_node);
  } catch (const AdaptiveResearchCampaignMissingState &error) {
    return AdaptiveResearchCommandResult::rejected(error.what());
  } catch (const std::out_of_range &error) {
    return AdaptiveResearchCommandResult::rejected(error.what());
  }
  const auto *project = find_project(*state, owned_node);
  if (!project || !project->paused)
    return AdaptiveResearchCommandResult::rejected(
        "The project is not currently paused.");
  if (project->pause_reason == "hypothesis_resolution_required")
    return AdaptiveResearchCommandResult::rejected(
        node->name +
        " requires scientific resolution before research can resume.");
  auto *economy = single_economy(world.economies, civilization_id);
  if (!economy)
    return AdaptiveResearchCommandResult::rejected(
        "Civilization " + std::to_string(civilization_id) +
        " has no economy available to fund research.");
  const auto quote = AdaptiveResearchFundingPolicy::quote(
      *node, requested_assigned_labs,
      campaign.runtime().authority().catalog());
  const auto currency =
      sovereign_currency_for_civilization(world.civilizations, civilization_id);
  if (economy->credits + 0.000001 < quote.operating_credits_per_day)
    return AdaptiveResearchCommandResult::rejected(
        node->name + " needs " +
        currency.format(quote.operating_credits_per_day) +
        " for its first resumed operating day; " +
        currency.format(economy->credits) + " is available.");
  return campaign.runtime().authority().resume_directed_research(
      *state, owned_node, requested_assigned_labs);
}

} // namespace stellar::core
