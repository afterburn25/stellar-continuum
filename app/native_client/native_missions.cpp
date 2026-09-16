#include "native_missions.hpp"
#include <algorithm>
#include <cmath>
#include <ranges>
#include "stellar/core/colonization_runtime.hpp"
#include "stellar/core/colony_biology.hpp"
#include "stellar/core/colony_economy.hpp"
#include "stellar/core/detail/legacy_number_format.hpp"
#include "stellar/core/exploration_advance.hpp"
#include "stellar/core/fleet_transit.hpp"
#include "stellar/core/knowledge.hpp"
#include "stellar/core/lane_network.hpp"
#include "stellar/core/settlement_knowledge.hpp"
#include "stellar/core/species_environment.hpp"
#include "stellar/core/survey_operations.hpp"

namespace stellar::native_missions {
namespace {

using namespace stellar::core;
using native_map::Color;
using native_map::FilledRectangle;
using native_map::Point;
using native_map::StrokedRectangle;
using native_map::Text;
using native_map::UiRect;

std::string fixed(double value, int minimum, int maximum) {
  return detail::legacy_custom_fixed(value, minimum, maximum);
}

// Reference ExplorationMissionStatus internal record.
struct MissionStatus {
  NativeMissionPhase phase{NativeMissionPhase::awaiting_order};
  std::optional<double> transit_days, survey_days, mission_days;
  std::string summary;
};

const StellarSystem *find_system(const FreshCampaignState &campaign, int id) {
  const auto found = std::ranges::find(campaign.systems, id, &StellarSystem::id);
  return found == campaign.systems.end() ? nullptr : &*found;
}

const PlanetaryBody *find_body(const FreshCampaignState &campaign, int id) {
  const auto found = std::ranges::find(campaign.bodies, id, &PlanetaryBody::id);
  return found == campaign.bodies.end() ? nullptr : &*found;
}

// Reference OperatingCapacity.
double operating_capacity(const FreshCampaignState &campaign,
                          int civilization_id) {
  const auto economy =
      std::ranges::find(campaign.economies, civilization_id,
                        &CivilizationEconomy::civilization_id);
  const double value =
      economy != campaign.economies.end()
          ? economy->last_base_operations_funding_fraction
          : 1.0;
  return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
}

// Reference ResolveSettlementBody: an explicit destination body is honored
// only when the colony assessment still permits founding; otherwise the
// resolver falls back to the best available surveyed world.
const PlanetaryBody *resolve_settlement_body(
    const FreshCampaignState &campaign, const FleetState &fleet, int system_id,
    const std::string &species_id) {
  if (fleet.destination_planetary_body_id) {
    const auto *body = find_body(campaign, *fleet.destination_planetary_body_id);
    if (!body || body->system_id != system_id)
      return nullptr;
    return species_colonization_assessment(species_id, *body)
                       .can_found_current_colony
                   ? body
                   : nullptr;
  }
  const SettlementKnowledgeWorldView world{
      campaign.systems, campaign.bodies, campaign.civilizations,
      campaign.colonies, campaign.fleets, campaign.knowledge};
  const auto resolved = resolve_best_available_settlement_body(
      world, fleet.civilization_id, system_id, species_id);
  return resolved ? find_body(campaign, *resolved) : nullptr;
}

// Reference ExplorationMissionStatus.Build.
MissionStatus evaluate(const FreshCampaignState &campaign,
                       const FleetState &fleet) {
  const int civilization_id = campaign.player_civilization_id;
  const double capacity = operating_capacity(campaign, civilization_id);
  const auto *knowledge = &campaign.knowledge;

  const auto awaiting = [](std::string summary) {
    MissionStatus status;
    status.summary = std::move(summary);
    return status;
  };

  if (!fleet.is_active)
    return awaiting(fleet.name + " is not an active mission fleet.");

  if (capacity <= 1e-7)
    return awaiting(fleet.name +
                    " is suspended because fleet operations are unfunded. "
                    "Restore the operating budget to resume its existing "
                    "mission.");

  if (fleet.hold_requested) {
    if (fleet.current_system_id) {
      const auto *system = find_system(campaign, *fleet.current_system_id);
      const std::string name =
          system ? system->name : "the current system";
      return awaiting(fleet.name + " is held at " + name +
                      "; resume to continue its existing mission.");
    }
    const std::optional<int> next_stop =
        !fleet.planned_route_system_ids.empty()
            ? std::optional<int>{fleet.planned_route_system_ids.front()}
            : fleet.destination_system_id;
    const auto *system =
        next_stop ? find_system(campaign, *next_stop) : nullptr;
    const std::string name = system ? system->name : "its next stop";
    MissionStatus status;
    status.phase = NativeMissionPhase::traveling;
    status.summary = fleet.name + " is holding after reaching " + name +
                     "; resume to continue its existing mission.";
    return status;
  }

  if (fleet.destination_system_id) {
    const auto *target = find_system(campaign, *fleet.destination_system_id);
    if (!target)
      return awaiting(fleet.name + " references an unknown destination system.");

    const double distance =
        measure_remaining_fleet_route(&campaign.systems, &fleet)
            .distance_light_years;
    const double speed = fleet.strategic_speed;
    const double local_days =
        fleet_remaining_chart_distance(campaign.systems, fleet) /
        fleet_local_transit_rate(fleet);
    std::optional<double> transit_days;
    if (speed > 0.0 && std::isfinite(distance))
      transit_days =
          std::max(0.0, (distance / speed + local_days) / capacity);

    std::optional<double> survey_days;
    const auto survey_level = knowledge->system_survey_level(
        civilization_id, target->id);
    if (fleet.role == FleetRole::Science &&
        survey_level >= SystemSurveyLevel::partially_surveyed) {
      const auto profile = SurveyOperationsProfiler{}.build(
          campaign.systems, campaign.bodies, target->id);
      survey_days = std::max(
          0.0,
          profile.estimated_science_survey_days *
                  (1.0 - knowledge->system_survey_progress(civilization_id,
                                                         target->id)) /
                  capacity);
    }

    std::optional<double> mission_days;
    switch (fleet.role) {
      case FleetRole::Science:
        if (transit_days && survey_days)
          mission_days = *transit_days + *survey_days;
        break;
      case FleetRole::Colony:
        if (transit_days)
          mission_days =
              *transit_days +
              (fleet.prevent_automatic_settlement
                   ? 0.0
                   : ColonizationSimulation::establishment_days(fleet) / capacity);
        break;
      case FleetRole::Scout:
        if (transit_days)
          mission_days =
              *transit_days +
              (survey_level < SystemSurveyLevel::partially_surveyed
                   ? ExplorationSimulation::scout_reconnaissance_days / capacity
                   : 0.0);
        break;
      default:
        mission_days = transit_days;
        break;
    }

    const std::string eta =
        transit_days
            ? "approximately " + fixed(*transit_days, 0, 1) +
                  " transit days remain"
            : "transit ETA is unavailable";
    std::string follow_up;
    if (survey_days)
      follow_up = "; approximately " + fixed(*survey_days, 0, 1) +
                  " known detailed-survey days remain after arrival";
    else if (fleet.role == FleetRole::Science)
      follow_up = "; detailed-survey duration remains unknown until "
                  "reconnaissance establishes system complexity";

    std::string destination = target->name;
    if (fleet.role == FleetRole::Colony &&
        fleet.destination_planetary_body_id) {
      const auto *body =
          find_body(campaign, *fleet.destination_planetary_body_id);
      if (body && body->system_id == target->id)
        destination = body->name + " in " + target->name;
    }

    MissionStatus status;
    status.phase = NativeMissionPhase::traveling;
    status.transit_days = transit_days;
    status.survey_days = survey_days;
    status.mission_days = mission_days;
    status.summary = fleet.name + " is traveling to " + destination + "; " +
                     eta + follow_up + ".";
    return status;
  }

  const StellarSystem *system =
      fleet.current_system_id ? find_system(campaign, *fleet.current_system_id)
                              : nullptr;
  if (!system)
    return awaiting(fleet.name + " is awaiting mission orders.");

  switch (fleet.role) {
    case FleetRole::Scout: {
      const auto level =
          knowledge->system_survey_level(civilization_id, system->id);
      if (level < SystemSurveyLevel::partially_surveyed) {
        const double completed =
            fleet.reconnaissance_system_id == system->id
                ? fleet.reconnaissance_days_completed
                : 0.0;
        const double remaining =
            std::max(0.0,
                     (ExplorationSimulation::scout_reconnaissance_days -
                      completed) /
                         capacity);
        MissionStatus status;
        status.phase = NativeMissionPhase::scouting;
        status.mission_days = remaining;
        status.summary = fleet.name + " is scouting " + system->name +
                         "; approximately " + fixed(remaining, 1, 1) +
                         " game days remain.";
        return status;
      }
      return awaiting(fleet.name +
                      " has completed reconnaissance-grade work in " +
                      system->name + " and is awaiting another order.");
    }
    case FleetRole::Science: {
      if (knowledge->is_system_fully_surveyed(civilization_id, system->id))
        return awaiting(fleet.name + " has completed the detailed survey of " +
                        system->name + " and is awaiting another order.");
      std::optional<double> survey_days;
      if (knowledge->system_survey_level(civilization_id, system->id) >=
          SystemSurveyLevel::partially_surveyed) {
        const auto profile = SurveyOperationsProfiler{}.build(
            campaign.systems, campaign.bodies, system->id);
        survey_days = std::max(
            0.0,
            profile.estimated_science_survey_days *
                    (1.0 - knowledge->system_survey_progress(civilization_id,
                                                           system->id)) /
                    capacity);
      }
      const std::string estimate =
          survey_days
              ? "approximately " + fixed(*survey_days, 0, 1) +
                    " detailed-survey days remain"
              : "survey duration is not yet known; the first science pass "
                "will establish reconnaissance-grade complexity";
      MissionStatus status;
      status.phase = NativeMissionPhase::science_survey;
      status.survey_days = survey_days;
      status.mission_days = survey_days;
      status.summary = fleet.name + " is conducting a detailed survey of " +
                       system->name + "; " + estimate + ".";
      return status;
    }
    case FleetRole::Colony: {
      if (fleet.embarked_population_millions <= 0.0)
        return awaiting(fleet.name +
                        " is not carrying colonists and has no active colony "
                        "mission.");
      if (fleet.prevent_automatic_settlement)
        return awaiting(fleet.name + " is on station in " + system->name +
                        "; select a surveyed world to authorize settlement.");
      if (fleet.settlement_body_id) {
        const double remaining = std::max(
            0.0,
            (ColonizationSimulation::establishment_days(fleet) -
             fleet.settlement_days_completed) /
                capacity);
        const auto *body = find_body(campaign, *fleet.settlement_body_id);
        const std::string site = body ? body->name : "settlement";
        MissionStatus status;
        status.phase = NativeMissionPhase::establishing_colony;
        status.mission_days = remaining;
        status.summary = "Establishing " + site + ": approximately " +
                         fixed(remaining, 1, 1) +
                         " game days remain. Habitats and services are under "
                         "construction.";
        return status;
      }
      const std::string species_id =
          fleet.embarked_population_species_id.value_or("");
      const auto species = std::ranges::find_if(
          species_environment_profiles(),
          [&](const auto &profile) { return profile.id == species_id; });
      if (species_id.empty() ||
          species == species_environment_profiles().end())
        return awaiting(fleet.name +
                        " carries population without a valid passenger "
                        "species identity.");
      if (!knowledge->is_system_fully_surveyed(civilization_id, system->id))
        return awaiting(fleet.name + " is carrying " +
                        fixed(fleet.embarked_population_millions, 0, 1) +
                        " million " + species->display_name + " colonists in " +
                        system->name +
                        ", but a completed science survey is still required "
                        "before settlement.");
      if (std::ranges::find(campaign.colonies, system->id,
                            &Colony::system_id) != campaign.colonies.end())
        return awaiting(fleet.name + " is carrying colonists in " +
                        system->name +
                        ", but that system already contains a founded colony "
                        "under the current single-colony early-release "
                        "model.");
      const auto *candidate =
          resolve_settlement_body(campaign, fleet, system->id, species_id);
      if (!candidate)
        return awaiting(fleet.name + " is carrying " +
                        fixed(fleet.embarked_population_millions, 0, 1) +
                        " million " + species->display_name + " colonists in " +
                        system->name +
                        ", but no surveyed body is currently viable for that "
                        "population.");
      const auto assessment =
          species_colonization_assessment(species_id, *candidate);
      const char *viability =
          assessment.viability ==
                  SpeciesColonizationViability::NaturallyViable
              ? "naturally viable"
              : "currently supported by the prototype "
                "habitat-compatibility fallback";
      const double establish_days =
          ColonizationSimulation::establishment_days(fleet);
      MissionStatus status;
      status.phase = NativeMissionPhase::establishing_colony;
      status.mission_days = establish_days / capacity;
      status.summary =
          fleet.name + " has arrived at " + candidate->name + " in " +
          system->name + "; the world is " + viability + " for " +
          species->display_name + " and establishment requires " +
          fixed(establish_days, 0, 0) + " game days.";
      return status;
    }
    default:
      return awaiting(fleet.name + " is awaiting mission orders.");
  }
}

}  // namespace

NativeMissionBoard build_mission_board(const FreshCampaignState &campaign) {
  NativeMissionBoard board;
  const int player_id = campaign.player_civilization_id;
  // Reference ActiveMissions: IsActive && owned && mission role, ordered by id.
  std::vector<const FleetState *> active;
  for (const auto &fleet : campaign.fleets) {
    if (!fleet.is_active || fleet.civilization_id != player_id)
      continue;
    if (fleet.role != FleetRole::Scout && fleet.role != FleetRole::Science &&
        fleet.role != FleetRole::Colony)
      continue;
    active.push_back(&fleet);
  }
  std::ranges::sort(active, {}, [](const FleetState *fleet) {
    return fleet->id;
  });
  for (const auto *fleet : active) {
    if (board.missions.size() >= 8)
      break;
    const auto status = evaluate(campaign, *fleet);
    NativeMissionCard card;
    card.fleet_id = fleet->id;
    card.role = fleet->role;
    card.phase = status.phase;
    card.fleet_name = fleet->name;
    const auto destination_id = fleet->destination_system_id
                                    ? fleet->destination_system_id
                                    : fleet->current_system_id;
    const auto *system =
        destination_id ? find_system(campaign, *destination_id) : nullptr;
    card.destination = system ? system->name : "Deep space";
    card.eta = status.mission_days
                   ? fixed(*status.mission_days, 1, 1) + " days remaining"
               : status.phase == NativeMissionPhase::awaiting_order
                   ? "Ready for orders"
                   : "ETA unavailable";
    card.summary = status.summary;
    board.missions.push_back(std::move(card));
  }
  return board;
}

std::string_view mission_phase_label(NativeMissionPhase phase) noexcept {
  switch (phase) {
    case NativeMissionPhase::awaiting_order:
      return "Awaiting order";
    case NativeMissionPhase::traveling:
      return "Traveling";
    case NativeMissionPhase::scouting:
      return "Scouting";
    case NativeMissionPhase::science_survey:
      return "Science survey";
    case NativeMissionPhase::establishing_colony:
      return "Establishing colony";
  }
  return "Awaiting order";
}

Color mission_phase_color(NativeMissionPhase phase) noexcept {
  // Reference MissionColor keys on the rendered phase label; "Scouting" and
  // "Establishing colony" fall through to gold there, matching here.
  switch (phase) {
    case NativeMissionPhase::traveling:
      return {154, 225, 255, 255};
    case NativeMissionPhase::science_survey:
      return {180, 160, 228, 255};
    default:
      return {232, 199, 102, 255};
  }
}

namespace {

std::string_view role_label(FleetRole role) noexcept {
  switch (role) {
    case FleetRole::Scout:
      return "Scout";
    case FleetRole::Science:
      return "Science";
    case FleetRole::Colony:
      return "Colony";
    default:
      return "Fleet";
  }
}

}  // namespace

MissionLayout mission_layout_for(const NativeMissionBoard &board, int width,
                                 int height) {
  MissionLayout layout;
  const auto sw = static_cast<float>(width),
             sh = static_cast<float>(height);
  layout.scale = std::min(sw / 1600.f, sh / 900.f);
  const auto scale = layout.scale;
  layout.heading_font_pixels = std::max(14, static_cast<int>(18.f * scale));
  layout.body_font_pixels = std::max(10, static_cast<int>(13.f * scale));
  layout.small_font_pixels = std::max(9, static_cast<int>(11.f * scale));
  layout.panel = {std::max(112.f * scale, sw - 470.f * scale), 78.f * scale,
                  std::min(450.f * scale, sw - 128.f * scale),
                  std::min(560.f * scale, sh - 210.f * scale)};
  const auto pad = 12.f * scale;
  layout.header = {layout.panel.x + pad, layout.panel.y + pad,
                   layout.panel.width - pad * 2.f, 34.f * scale};
  layout.close_button = {layout.panel.x + layout.panel.width - pad -
                             26.f * scale,
                         layout.header.y, 26.f * scale, 26.f * scale};
  auto top = layout.header.y + layout.header.height + 12.f * scale;
  const auto card_height = 78.f * scale;
  const auto card_gap = 8.f * scale;
  for (std::size_t i = 0; i < board.missions.size(); ++i) {
    UiRect card{layout.panel.x + pad, top, layout.panel.width - pad * 2.f,
                card_height};
    if (card.y + card.height >
        layout.panel.y + layout.panel.height - pad)
      break;
    layout.cards.push_back(card);
    top += card_height + card_gap;
  }
  if (layout.cards.empty())
    layout.empty_hint = {layout.panel.x + pad, top,
                         layout.panel.width - pad * 2.f, 60.f * scale};
  return layout;
}

MissionViewCommand NativeMissionView::handle(const native_map::InputEvent &event,
                                             const NativeMissionBoard &board,
                                             int width, int height) {
  MissionViewCommand command;
  if (!visible_)
    return command;
  const auto layout = mission_layout_for(board, width, height);
  if (event.type == native_map::InputEventType::LeftReleased &&
      layout.close_button.contains(event.position)) {
    command.kind = MissionViewCommandKind::Close;
    command.captured = true;
    return command;
  }
  // ContainPointerInput: pointer input inside the panel never reaches the map.
  if ((event.type == native_map::InputEventType::LeftPressed ||
       event.type == native_map::InputEventType::RightPressed ||
       event.type == native_map::InputEventType::LeftReleased ||
       event.type == native_map::InputEventType::RightReleased ||
       event.type == native_map::InputEventType::Wheel ||
       event.type == native_map::InputEventType::PointerMove) &&
      layout.panel.contains(event.position))
    command.captured = true;
  return command;
}

void NativeMissionView::render(native_map::DrawList &out,
                               const NativeMissionBoard &board, int width,
                               int height) const {
  if (!visible_)
    return;
  const auto layout = mission_layout_for(board, width, height);
  const auto scale = layout.scale;
  const auto muted = Color{122, 154, 192, 255};
  const auto body = Color{190, 212, 236, 255};
  const auto accent = Color{154, 225, 255, 255};
  out.overlay.emplace_back(FilledRectangle{layout.panel, {10, 17, 31, 242}});
  out.overlay.emplace_back(
      StrokedRectangle{layout.panel, {72, 101, 145, 255}});
  out.overlay.emplace_back(Text{{layout.header.x, layout.header.y}, "MISSIONS & SETTLEMENT",
                                Color{233, 242, 252, 255},
                                layout.heading_font_pixels});
  out.overlay.emplace_back(FilledRectangle{layout.close_button,
                                           {30, 41, 62, 220}});
  out.overlay.emplace_back(
      StrokedRectangle{layout.close_button, {96, 125, 168, 255}});
  out.overlay.emplace_back(Text{
      {layout.close_button.x + 8.f * scale,
       layout.close_button.y + 4.f * scale},
      "x", Color{212, 226, 244, 255}, layout.body_font_pixels});
  if (board.missions.empty()) {
    out.overlay.emplace_back(Text{{layout.empty_hint.x, layout.empty_hint.y},
                                  "No active mission fleets.",
                                  body, layout.body_font_pixels});
    out.overlay.emplace_back(Text{
        {layout.empty_hint.x, layout.empty_hint.y + 18.f * scale},
        "Commission scout, science, or colony ships to begin.", muted,
        layout.small_font_pixels});
    return;
  }
  for (std::size_t i = 0;
       i < layout.cards.size() && i < board.missions.size(); ++i) {
    const auto &card = board.missions[i];
    const auto &rect = layout.cards[i];
    out.overlay.emplace_back(FilledRectangle{rect, {17, 27, 47, 240}});
    out.overlay.emplace_back(
        StrokedRectangle{rect, {59, 83, 118, 255}});
    const auto pad = 10.f * scale;
    auto line = rect.y + 8.f * scale;
    out.overlay.emplace_back(Text{{rect.x + pad, line}, card.fleet_name,
                                  accent, layout.body_font_pixels});
    out.overlay.emplace_back(
        Text{{rect.x + pad, line + 17.f * scale},
             std::string(role_label(card.role)) + " - " +
                 std::string(mission_phase_label(card.phase)),
             mission_phase_color(card.phase), layout.small_font_pixels});
    line += 36.f * scale;
    out.overlay.emplace_back(
        Text{{rect.x + pad, line}, card.destination + " - " + card.eta, body,
             layout.small_font_pixels});
    out.overlay.emplace_back(
        Text{{rect.x + pad, line + 15.f * scale}, card.summary, muted,
             layout.small_font_pixels, rect.width - pad * 2.f});
  }
}

}  // namespace stellar::native_missions
