#include "native_inspection.hpp"
#include <algorithm>
#include <cctype>
#include <format>
#include "stellar/core/campaign_economy.hpp"
#include "stellar/core/construction_state.hpp"
#include "stellar/core/fleet_reach.hpp"
#include "stellar/core/fleet_state.hpp"
#include "stellar/core/interstellar_distance.hpp"
#include "stellar/core/logistics.hpp"

namespace stellar::native_inspection {
namespace {

using namespace stellar::core;
using native_map::Color;
using native_map::FilledRectangle;
using native_map::Point;
using native_map::StrokedRectangle;
using native_map::Text;
using native_map::UiRect;

NativeInspectionFact home_distance_fact(const FreshCampaignState &campaign,
                                        const StellarSystem &system) {
  const auto player = std::ranges::find(
      campaign.civilizations, campaign.player_civilization_id,
      &Civilization::id);
  if (player == campaign.civilizations.end())
    return {"DISTANCE FROM HOMEWORLD", "Unknown", true};
  const auto home = std::ranges::find(campaign.systems,
                                      player->home_system_id,
                                      &StellarSystem::id);
  if (home == campaign.systems.end())
    return {"DISTANCE FROM HOMEWORLD", "Unknown", true};
  return {"DISTANCE FROM HOMEWORLD",
          format_interstellar_metric_primary(
              distance_light_years(home->position, system.position)),
          true};
}

std::string yes_no(bool value) { return value ? "Yes" : "No"; }

std::string_view supply_condition_name(SupplyCondition condition) noexcept {
  switch (condition) {
  case SupplyCondition::Healthy: return "Healthy";
  case SupplyCondition::Strained: return "Strained";
  case SupplyCondition::Critical: return "Critical";
  }
  return "Healthy";
}

} // namespace

std::string_view stellar_class_label(StellarClass value) noexcept {
  switch (value) {
  case StellarClass::MRedDwarf: return "M-type red dwarf";
  case StellarClass::KOrangeDwarf: return "K-type orange dwarf";
  case StellarClass::GYellowDwarf: return "G-type yellow dwarf";
  case StellarClass::FYellowWhiteDwarf: return "F-type yellow-white dwarf";
  case StellarClass::AWhiteStar: return "A-type white star";
  case StellarClass::HotBlueStar: return "Hot blue B/O star";
  case StellarClass::Giant: return "Red/orange giant";
  case StellarClass::WhiteDwarf: return "White dwarf";
  case StellarClass::NeutronStar: return "Neutron star / pulsar";
  case StellarClass::Pulsar: return "Pulsar";
  case StellarClass::BlackHole: return "Black hole";
  case StellarClass::Protostar: return "Young star / protostar";
  }
  return "Legacy classification";
}

std::string_view survey_level_name(SystemSurveyLevel level) noexcept {
  switch (level) {
  case SystemSurveyLevel::unknown: return "Unknown";
  case SystemSurveyLevel::detected: return "Detected";
  case SystemSurveyLevel::partially_surveyed: return "PartiallySurveyed";
  case SystemSurveyLevel::fully_surveyed: return "FullySurveyed";
  }
  return "Unknown";
}

NativeSystemInspection build_system_inspection(
    const FreshCampaignState &campaign, int selected_system_id) {
  NativeSystemInspection inspection;
  if (selected_system_id < 0) return inspection;

  const auto player_id = campaign.player_civilization_id;
  const auto selected = std::ranges::find(campaign.systems, selected_system_id,
                                          &StellarSystem::id);
  if (selected == campaign.systems.end()) {
    inspection.name = "TARGET LOST";
    inspection.survey_status = "Unavailable";
    inspection.guidance = "The selected system is no longer available.";
    return inspection;
  }

  const auto level =
      campaign.knowledge.system_survey_level(player_id, selected->id);
  const auto progress =
      campaign.knowledge.system_survey_progress(player_id, selected->id);
  const auto distance = home_distance_fact(campaign, *selected);

  if (level == SystemSurveyLevel::unknown) {
    const auto player = std::ranges::find(campaign.civilizations, player_id,
                                          &Civilization::id);
    inspection.name = "UNKNOWN";
    inspection.survey_status = "Unknown";
    inspection.guidance =
        player != campaign.civilizations.end() &&
                player->development_stage ==
                    CivilizationDevelopmentStage::PreWarp
            ? "Interstellar operations are not yet available."
            : "Dispatch a scout or science vessel to establish local "
              "information.";
    inspection.facts = {distance};
    inspection.colony_name = "NO COLONY DATA";
    inspection.colony_details = "Survey required";
    return inspection;
  }

  std::string name = selected->name;
  std::ranges::transform(name, name.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  inspection.name = name;
  inspection.survey_status = std::string(survey_level_name(level));
  inspection.survey_progress = progress;

  if (level != SystemSurveyLevel::fully_surveyed) {
    inspection.guidance =
        level == SystemSurveyLevel::detected
            ? "Planet, resource, anomaly and civilization data remain unknown. "
              "Send a scout for reconnaissance or a science vessel for a "
              "detailed survey."
            : "Reconnaissance is incomplete. A science vessel must finish the "
              "detailed survey before settlement-grade facts are available.";
    inspection.facts = {distance};
    inspection.colony_name = "COLONY STATUS UNKNOWN";
    inspection.colony_details = "Detailed survey required";
    return inspection;
  }

  inspection.has_detailed_survey = true;
  inspection.guidance = "Detailed intelligence available.";
  inspection.facts = {
      {"PRIMARY STAR",
       selected->primary ? std::string(stellar_class_label(*selected->primary))
                         : "Unknown",
       selected->primary.has_value() ||
           selected->archetype != StarArchetype::Standard},
      {"SYSTEM TRAITS", [a = selected->archetype] {
         switch (a) {
         case StarArchetype::Standard: return "Standard";
         case StarArchetype::ResourceRich: return "ResourceRich";
         case StarArchetype::HabitableRich: return "HabitableRich";
         case StarArchetype::BarrenFrontier: return "BarrenFrontier";
         case StarArchetype::Nebula: return "Nebula";
         case StarArchetype::NeutronPulsar: return "NeutronPulsar";
         case StarArchetype::BlackHole: return "BlackHole";
         case StarArchetype::AncientRuin: return "AncientRuin";
         case StarArchetype::Dangerous: return "Dangerous";
         case StarArchetype::Legendary: return "Legendary";
         }
         return "Unknown";
       }(), true},
      distance,
      {"HABITABLE WORLD", yes_no(selected->has_habitable_world),
       selected->has_habitable_world},
      {"ANOMALY", yes_no(selected->has_anomaly), selected->has_anomaly},
      {"RARE RESOURCES", yes_no(selected->has_rare_resource),
       selected->has_rare_resource},
      {"PRE-WARP LIFE", yes_no(selected->has_pre_warp_civilization),
       selected->has_pre_warp_civilization},
  };

  const auto colony = std::ranges::find(campaign.colonies, selected->id,
                                        &Colony::system_id);
  if (colony == campaign.colonies.end()) {
    inspection.colony_name = "NO KNOWN COLONY";
    inspection.colony_details =
        "No represented settlement is known in this system.";
    return inspection;
  }
  const bool own_colony = colony->civilization_id == player_id;
  if (!own_colony &&
      !campaign.knowledge.is_civilization_known(player_id,
                                                colony->civilization_id)) {
    inspection.colony_name = "COLONY PRESENCE UNIDENTIFIED";
    inspection.colony_details =
        "Ownership and settlement details are not reliably known.";
    return inspection;
  }

  inspection.colony_name = colony->name;
  std::ranges::transform(inspection.colony_name,
                         inspection.colony_name.begin(), [](unsigned char c) {
                           return static_cast<char>(std::toupper(c));
                         });
  inspection.colony_details = std::format(
      "Population {:.1f}M · Infrastructure {:.2f} · Stability {:.0f}%",
      colony->population_millions, colony->infrastructure,
      colony->stability * 100.);
  const bool player_has_economy = std::ranges::any_of(
      campaign.economies,
      [player_id](const CivilizationEconomy &economy) {
        return economy.civilization_id == player_id;
      });
  if (own_colony && player_has_economy) {
    try {
      const auto construction =
          economic_construction_projection(campaign.construction);
      const auto fleets = economic_fleet_projection(campaign.fleets);
      const EconomyWorldView economy{campaign.civilizations, campaign.bodies,
                                     construction, fleets};
      const auto logistics = economy_logistics(economy, campaign.colonies,
                                               campaign.economies, player_id);
      const auto local = std::ranges::find(
          logistics.colonies, colony->id, &ColonyLogisticsSnapshot::colony_id);
      if (local != logistics.colonies.end())
        inspection.colony_details += std::format(
            " · Supply {} · Local coverage {:.0f}% · Imports {:.2f}/day",
            supply_condition_name(local->condition),
            local->coverage_ratio * 100.,
            local->imported_support_required_per_day);
    } catch (const std::exception &) {
      // A campaign without resolvable logistics keeps the base colony line.
    }
  }
  return inspection;
}

native_map::UiRect append_inspection_card(
    native_map::DrawList &out, const NativeSystemInspection &inspection,
    Point bottom_left, float scale) {
  const float width = 336.f * scale, pad = 14.f * scale;
  const float fact_rows = inspection.has_detailed_survey
                              ? (inspection.facts.size() + 1.f) / 2.f
                              : static_cast<float>(inspection.facts.size());
  const float height =
      pad * 2.f + 40.f * scale + 12.f * scale + 34.f * scale +
      22.f * scale + fact_rows * 34.f * scale + 64.f * scale;
  const UiRect panel{bottom_left.x, bottom_left.y - height, width, height};
  out.overlay.emplace_back(FilledRectangle{panel, {8, 13, 22, 228}});
  out.overlay.emplace_back(StrokedRectangle{panel, {64, 91, 128, 255}});

  float y = panel.y + pad;
  const float x = panel.x + pad, inner = width - pad * 2.f;
  out.overlay.emplace_back(Text{{x, y}, inspection.name,
                                {238, 244, 255, 255}, 20, inner});
  out.overlay.emplace_back(Text{{x, y + 22.f * scale}, inspection.survey_status,
                                {140, 196, 255, 255}, 11, inner});
  y += 40.f * scale;

  // Survey progress track.
  const UiRect track{x, y, inner, 8.f * scale};
  out.overlay.emplace_back(FilledRectangle{track, {30, 40, 56, 255}});
  const float fill_width = static_cast<float>(
      std::clamp(inspection.survey_progress, 0., 1.)) * track.width;
  if (fill_width > 0.f)
    out.overlay.emplace_back(FilledRectangle{
        {track.x, track.y, fill_width, track.height}, {92, 168, 255, 255}});
  y += 12.f * scale;

  out.overlay.emplace_back(Text{{x, y}, inspection.guidance,
                                {154, 181, 211, 235}, 11, inner});
  y += 34.f * scale;

  out.overlay.emplace_back(Text{{x, y}, "INTELLIGENCE SIGNALS",
                                {140, 196, 255, 255}, 11, inner * .5f});
  out.overlay.emplace_back(FilledRectangle{
      {x + inner * .52f, y + 6.f * scale, inner * .48f, 1.f},
      {64, 91, 128, 255}});
  y += 22.f * scale;

  for (std::size_t i = 0; i < inspection.facts.size(); ++i) {
    const auto &fact = inspection.facts[i];
    const float col = static_cast<float>(i % 2), row = static_cast<float>(i / 2);
    const float fx = x + col * (inner * .5f), fy = y + row * 34.f * scale;
    out.overlay.emplace_back(Text{{fx, fy}, fact.label,
                                  {154, 181, 211, 235}, 9, inner * .5f - 8.f});
    out.overlay.emplace_back(Text{
        {fx, fy + 12.f * scale}, fact.value,
        fact.positive ? Color{225, 238, 250, 255} : Color{154, 181, 211, 235},
        11, inner * .5f - 8.f});
  }
  y += fact_rows * 34.f * scale;

  const UiRect card{x, y, inner, 56.f * scale};
  out.overlay.emplace_back(FilledRectangle{card, {16, 24, 38, 255}});
  out.overlay.emplace_back(Text{{card.x + 10.f * scale, card.y + 8.f * scale},
                                "SETTLEMENT INTELLIGENCE",
                                {154, 181, 211, 235}, 9, inner});
  out.overlay.emplace_back(Text{{card.x + 10.f * scale, card.y + 20.f * scale},
                                inspection.colony_name, {238, 244, 255, 255},
                                13, inner - 20.f * scale});
  out.overlay.emplace_back(Text{{card.x + 10.f * scale, card.y + 38.f * scale},
                                inspection.colony_details,
                                {154, 181, 211, 235}, 10, inner - 20.f * scale});
  return panel;
}

} // namespace stellar::native_inspection
