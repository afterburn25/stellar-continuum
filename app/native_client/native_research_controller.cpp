#include "native_research_controller.hpp"
#include "native_research_presentation.hpp"

#include <stellar/core/adaptive_research_funding.hpp>
#include <stellar/core/sovereign_currency.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace stellar::native_research {
namespace {
using namespace stellar::core;

[[nodiscard]] std::string trim(std::string value) {
  const auto space = [](const unsigned char value) { return std::isspace(value); };
  const auto first = std::ranges::find_if_not(value, space);
  const auto last = std::ranges::find_if_not(value | std::views::reverse, space).base();
  return first < last ? std::string(first, last) : std::string{};
}

void require_utf8(std::string_view value) {
  while (!value.empty()) {
    const auto lead = static_cast<unsigned char>(value.front());
    std::uint32_t point{};
    std::size_t width{};
    if (lead <= 0x7f) { point = lead; width = 1; }
    else if (lead >= 0xc2 && lead <= 0xdf) { point = lead & 0x1f; width = 2; }
    else if (lead >= 0xe0 && lead <= 0xef) { point = lead & 0x0f; width = 3; }
    else if (lead >= 0xf0 && lead <= 0xf4) { point = lead & 0x07; width = 4; }
    else throw std::invalid_argument("Research search is not valid UTF-8.");
    if (value.size() < width)
      throw std::invalid_argument("Research search is not valid UTF-8.");
    for (std::size_t index = 1; index < width; ++index) {
      const auto byte = static_cast<unsigned char>(value[index]);
      if ((byte & 0xc0) != 0x80)
        throw std::invalid_argument("Research search is not valid UTF-8.");
      point = (point << 6) | (byte & 0x3f);
    }
    if ((width == 3 && (point < 0x800 || (point >= 0xd800 && point <= 0xdfff))) ||
        (width == 4 && (point < 0x10000 || point > 0x10ffff)))
      throw std::invalid_argument("Research search is not valid UTF-8.");
    value.remove_prefix(width);
  }
}

[[nodiscard]] std::string fold_ascii(std::string value) {
  std::ranges::transform(value, value.begin(), [](const unsigned char character) {
    return character < 0x80
               ? static_cast<char>(std::tolower(character))
               : static_cast<char>(character);
  });
  return value;
}

[[nodiscard]] std::string label(std::string value) {
  bool upper = true;
  for (auto &character : value) {
    if (character == '_') { character = ' '; upper = true; continue; }
    if (upper && static_cast<unsigned char>(character) < 0x80) {
      character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
      upper = false;
    }
  }
  return value;
}

struct PlayerContext {
  FreshCampaignState &world;
  AdaptiveResearchCampaignState &campaign;
  const AdaptiveResearchCivilizationState &state;
  const Civilization &civilization;
  CivilizationEconomy *economy;
  const AdaptiveResearchAuthority &authority;
};

[[nodiscard]] PlayerContext context(CampaignFrame &frame) {
  auto &runtime = frame.runtime();
  auto &world = runtime.world().campaign();
  const auto civilization = std::ranges::find(
      world.civilizations, world.player_civilization_id, &Civilization::id);
  if (civilization == world.civilizations.end() || !civilization->is_player)
    throw std::runtime_error("The campaign has no valid player civilization.");
  auto &campaign = runtime.research();
  auto *state = campaign.try_get_civilization(world.player_civilization_id);
  if (!state)
    throw std::runtime_error("The player has no adaptive research state.");
  CivilizationEconomy *economy{};
  for (auto &candidate : world.economies) {
    if (candidate.civilization_id != world.player_civilization_id) continue;
    if (economy)
      throw std::runtime_error("The player has duplicate economy state.");
    economy = &candidate;
  }
  return {world, campaign, *state, *civilization, economy,
          runtime.research_runtime().authority()};
}

[[nodiscard]] const AdaptiveResearchProjectView *project_for(
    const AdaptiveResearchView &view, const std::string_view node_id) {
  const auto found = std::ranges::find(view.active_projects, node_id,
                                       &AdaptiveResearchProjectView::node_id);
  return found == view.active_projects.end() ? nullptr : &*found;
}

[[nodiscard]] std::string positive_amount(
    const SovereignCurrencyDefinition &currency, const double value) {
  const auto formatted = currency.format(value);
  if (value > 0. && formatted == currency.format(0.)) {
    const auto smallest_visible_budget_unit =
        .01 / currency.local_units_per_budget_unit;
    return "Under " + currency.format(smallest_visible_budget_unit);
  }
  return formatted;
}

[[nodiscard]] std::string operating_cost_rate(
    const SovereignCurrencyDefinition &currency, const double value) {
  if (value > 0. && currency.format(value) == currency.format(0.))
    return positive_amount(currency, value) + "/day";
  return currency.format_rate(-value);
}

[[nodiscard]] std::string funding_signature(
    const PlayerContext &player,
    const SovereignCurrencyDefinition &currency) {
  std::ostringstream result;
  result.imbue(std::locale::classic());
  result << std::hexfloat << player.civilization.species_id.size() << ':'
         << player.civilization.species_id << ';' << currency.name.size() << ':'
         << currency.name << ';' << currency.code.size() << ':' << currency.code
         << ';' << currency.symbol.size() << ':' << currency.symbol << ';'
         << currency.local_units_per_budget_unit << ';';
  return result.str();
}

[[nodiscard]] std::optional<NativeResearchCost> quote_for(
    const PlayerContext &player, const AdaptiveResearchView &view,
    const AdaptiveResearchNodeView &node,
    const AdaptiveResearchProjectView *project,
    const SovereignCurrencyDefinition &currency) {
  if (!node.minimum_labs || !node.recommended_labs) return std::nullopt;
  if (!project && node.state >= ResearchMaturity::mature) return std::nullopt;
  const auto labs = project ? project->assigned_effective_labs
                            : std::min<double>(*node.recommended_labs,
                                               view.directed_program_capacity.free_effective_labs);
  if (!std::isfinite(labs) || labs < 0.) return std::nullopt;
  const auto quote = AdaptiveResearchFundingPolicy::quote(
      player.authority.catalog().get_node(node.node_id), labs,
      player.authority.catalog());
  const auto needed =
      AdaptiveResearchCampaignCommands::credits_needed_to_start(quote);
  return NativeResearchCost{
      .assigned_effective_labs = quote.assigned_effective_labs,
      .authorization_credits = quote.authorization_credits,
      .milestone_commitment_credits = quote.milestone_commitment_credits,
      .operating_credits_per_day = quote.operating_credits_per_day,
      .estimated_total_credits = quote.estimated_total_credits,
      .estimated_years_at_full_funding =
          quote.estimated_years_at_full_funding,
      .credits_needed_to_start = needed,
      .formatted_authorization =
          positive_amount(currency, quote.authorization_credits),
      .formatted_milestone_commitment =
          positive_amount(currency, quote.milestone_commitment_credits),
      .formatted_operating_cost_rate =
          operating_cost_rate(currency, quote.operating_credits_per_day),
      .formatted_estimated_total =
          positive_amount(currency, quote.estimated_total_credits),
      .formatted_credits_needed_to_start =
          positive_amount(currency, needed)};
}

[[nodiscard]] NativeResearchAction primary_action(
    const PlayerContext &player, const AdaptiveResearchView &view,
    const AdaptiveResearchNodeView &node,
    const AdaptiveResearchProjectView *project,
    const std::optional<NativeResearchCost> &cost) {
  if (project) {
    if (!project->paused)
      return {NativeResearchIntent::Pause, true, {}};
    if (project->pause_reason == "hypothesis_resolution_required")
      return {NativeResearchIntent::Resume, false,
              "This hypothesis must be resolved before the program can resume."};
    if (!project->current_blockers.empty())
      return {NativeResearchIntent::Resume, false,
              project->current_blockers.front().message};
    if (!player.economy)
      return {NativeResearchIntent::Resume, false,
              "The player has no economy available to fund research."};
    if (!cost || player.economy->credits + .000001 < cost->operating_credits_per_day)
      return {NativeResearchIntent::Resume, false,
              "The treasury cannot fund the next operating day."};
    return {NativeResearchIntent::Resume, true, {}};
  }
  if (node.state < ResearchMaturity::investigable ||
      node.state >= ResearchMaturity::mature)
    return {};
  if (!node.blockers.empty())
    return {NativeResearchIntent::Start, false, node.blockers.front().message};
  const auto &capacity = view.directed_program_capacity;
  if (!capacity.lab_capacity_only && capacity.maximum_directed_programs &&
      capacity.active_program_count >= *capacity.maximum_directed_programs)
    return {NativeResearchIntent::Start, false,
            "No directed research program slot is available."};
  if (!node.minimum_labs ||
      capacity.free_effective_labs + .000001 < *node.minimum_labs)
    return {NativeResearchIntent::Start, false,
            "Too few effective laboratories are available."};
  if (!player.economy)
    return {NativeResearchIntent::Start, false,
            "The player has no economy available to fund research."};
  if (!cost || player.economy->credits + .000001 < cost->credits_needed_to_start)
    return {NativeResearchIntent::Start, false,
            "The treasury cannot fund authorization, milestones, and the first operating day."};
  return {NativeResearchIntent::Start, true, {}};
}

[[nodiscard]] bool matches(const NativeResearchNode &node,
                           const std::string &folded_query) {
  if (folded_query.empty()) return true;
  std::string text = node.display_name + " " + node.domain_label + " " +
                     node.solution_family + " " + node.purpose + " " + node.benefits;
  for (const auto &capability : node.known_capabilities)
    text += " " + capability.display_name;
  return fold_ascii(std::move(text)).contains(folded_query);
}

} // namespace

void NativeResearchController::require_owner() const {
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error(
        "Native research must be controlled by its simulation owner thread.");
}

void NativeResearchController::bind_generation(const std::uint64_t generation) {
  if (generation_ && *generation_ != generation) {
    selected_node_id_.reset();
    funding_revision_ = 0;
    funding_signature_.reset();
  }
  generation_ = generation;
}

NativeResearchWindow NativeResearchController::build(
    CampaignFrame &frame, const std::uint64_t campaign_generation,
    const NativeResearchQuery &query) {
  require_owner();
  bind_generation(campaign_generation);
  auto search = trim(query.search);
  require_utf8(search);
  if (search.size() > 256)
    throw std::invalid_argument("Research search exceeds 256 UTF-8 bytes.");
  const auto folded_search = fold_ascii(std::move(search));
  auto player = context(frame);
  const auto currency = sovereign_currency_for_civilization(
      player.world.civilizations, player.world.player_civilization_id);
  const auto signature = funding_signature(player, currency);
  if (!funding_signature_ || *funding_signature_ != signature) {
    if (funding_revision_ == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("Native research funding revision is exhausted.");
    ++funding_revision_;
    funding_signature_ = signature;
  }
  const auto target = "species:" + player.civilization.species_id;
  const auto view = player.authority.kernel().build_view(player.state, target);

  std::unordered_map<std::string, std::size_t> domain_counts;
  std::unordered_set<std::string> visible_ids;
  std::vector<NativeResearchNode> all_nodes;
  all_nodes.reserve(view.visible_nodes.size());
  for (const auto &node : view.visible_nodes) {
    // Match ResearchWorkspaceView.IsDetailed: recognized rumors and hypotheses
    // remain unnamed until they become investigable.
    if (node.state < ResearchMaturity::investigable) continue;
    visible_ids.insert(node.node_id);
    ++domain_counts[node.domain_id];
    const auto *project = project_for(view, node.node_id);
    const auto cost = quote_for(player, view, node, project, currency);
    NativeResearchNode projected{
        .id = node.node_id,
        .display_name = node.display_name,
        .domain_id = node.domain_id,
        .domain_label = label(node.domain_id),
        .solution_family = node.solution_family,
        .maturity = node.state,
        .graph_depth = player.authority.catalog().get_node(node.node_id).graph_depth,
        .active = project != nullptr,
        .paused = project && project->paused,
        .stage_progress = project ? project->stage_progress :
                          node.state == ResearchMaturity::mature ? 1. : 0.,
        .total_progress = project ? project->total_progress :
                          node.state == ResearchMaturity::mature ? 1. : 0.,
        .assigned_effective_labs = project ? project->assigned_effective_labs : 0.,
        .readiness_band = project ? project->readiness_band : std::string{},
        .cost = cost,
        .cancel_action = project
                             ? NativeResearchAction{
                                   NativeResearchIntent::Cancel, false,
                                   "Cancellation is unavailable for this program."}
                             : NativeResearchAction{},
    };
    for (const auto &blocker : project ? project->current_blockers : node.blockers)
      projected.blockers.push_back(blocker.message);
    for (const auto &capability_id : node.known_capabilities) {
      const auto *definition = player.authority.catalog().find_capability(capability_id);
      projected.known_capabilities.push_back(
          {capability_id, definition ? definition->name : "Known capability"});
    }
    projected.primary_action = primary_action(player, view, node, project, cost);
    projected.purpose = research_purpose(node.node_id, projected.domain_label, label(node.solution_family));
    projected.benefits = research_benefit(node.node_id);
    projected.research_points = player.authority.catalog().get_node(node.node_id).project_requirements.base_research_points;
    all_nodes.push_back(std::move(projected));
  }

  NativeResearchWindow result;
  result.campaign_generation = campaign_generation;
  result.research_revision = player.state.revision();
  result.funding_revision = funding_revision_;
  result.currency = currency;
  if (player.economy) {
    result.treasury_credits = player.economy->credits;
    result.formatted_treasury =
        positive_amount(currency, player.economy->credits);
  }
  result.free_effective_labs = view.directed_program_capacity.free_effective_labs;
  result.total_effective_labs = player.state.total_effective_research_labs();
  result.domain_tabs.push_back({{}, "All Research", all_nodes.size()});
  for (const auto &[category, title] : research_categories) {
    std::size_t count{};
    for (const auto &[domain, known] : domain_counts)
      if (research_category_matches(category, domain)) count += known;
    result.domain_tabs.push_back({category, title, count});
  }
  for (auto &node : all_nodes) {
    if (query.domain_id && !research_category_matches(*query.domain_id, node.domain_id)) continue;
    if (!matches(node, folded_search)) continue;
    result.nodes.push_back(std::move(node));
  }
  for (const auto &edge : view.visible_edges) {
    if (visible_ids.contains(edge.from_visible_node_id) &&
        visible_ids.contains(edge.to_visible_node_id))
      result.edges.push_back({edge.from_visible_node_id, edge.to_visible_node_id,
                              edge.relationship});
  }
  if (selected_node_id_ && visible_ids.contains(*selected_node_id_))
    result.selected_node_id = selected_node_id_;
  else
    selected_node_id_.reset();
  return result;
}

NativeResearchCommandOutcome NativeResearchController::execute(
    CampaignFrame &frame, const std::uint64_t campaign_generation,
    const std::int64_t expected_research_revision,
    const std::uint64_t expected_funding_revision,
    const NativeResearchIntent intent, const std::string_view node_id) {
  require_owner();
  if (!generation_ || *generation_ != campaign_generation)
    return {false, "The campaign changed; refresh research before issuing a command.", 0};
  auto player = context(frame);
  if (player.state.revision() != expected_research_revision)
    return {false, "Research changed; refresh before issuing a command.",
            player.state.revision()};
  const auto currency = sovereign_currency_for_civilization(
      player.world.civilizations, player.world.player_civilization_id);
  if (!funding_signature_ || expected_funding_revision != funding_revision_ ||
      *funding_signature_ != funding_signature(player, currency))
    return {false, "Research funding changed; refresh before issuing a command.",
            player.state.revision()};
  const auto target = "species:" + player.civilization.species_id;
  const auto view = player.authority.kernel().build_view(player.state, target);
  const auto visible = std::ranges::find(view.visible_nodes, node_id,
                                         &AdaptiveResearchNodeView::node_id);
  if (visible == view.visible_nodes.end() ||
      visible->state < ResearchMaturity::investigable)
    return {false, "That research program is not known to the player.",
            player.state.revision()};
  if (intent == NativeResearchIntent::Cancel)
    return {false,
            "Cancellation is unavailable for this program.",
            player.state.revision()};

  AdaptiveResearchCommandResult command;
  if (intent == NativeResearchIntent::Start) {
    const auto &definition = player.authority.catalog().get_node(node_id);
    const auto labs = std::min<double>(definition.project_requirements.recommended_labs,
                                      player.state.free_effective_labs());
    command = AdaptiveResearchCampaignCommands::start_directed_research(
        {player.world.civilizations, player.world.economies}, player.campaign,
        player.world.player_civilization_id, node_id, labs, target);
  } else if (intent == NativeResearchIntent::Pause) {
    command = AdaptiveResearchCampaignCommands::pause_directed_research(
        player.campaign, player.world.player_civilization_id, node_id);
  } else if (intent == NativeResearchIntent::Resume) {
    const auto project = std::ranges::find(player.state.active_projects(), node_id,
                                           &ResearchProjectRuntimeState::node_id);
    if (project == player.state.active_projects().end())
      return {false, "That research project is no longer active.",
              player.state.revision()};
    command = AdaptiveResearchCampaignCommands::resume_directed_research(
        {player.world.civilizations, player.world.economies}, player.campaign,
        player.world.player_civilization_id, node_id,
        project->assigned_effective_labs);
  } else {
    return {false, "No research command was selected.", player.state.revision()};
  }
  return {command.accepted, command.message, player.state.revision()};
}

void NativeResearchController::select(std::optional<std::string> node_id) {
  require_owner();
  selected_node_id_ = std::move(node_id);
}

const std::optional<std::string> &NativeResearchController::selection() const {
  require_owner();
  return selected_node_id_;
}

} // namespace stellar::native_research
