#include "native_overview.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>
#include <utility>
#include "stellar/core/detail/legacy_number_format.hpp"
#include "stellar/core/fleet_combat_intelligence.hpp"
#include "stellar/core/fleet_reach.hpp"
#include "stellar/core/interstellar_distance.hpp"
#include "stellar/core/knowledge.hpp"

namespace stellar::native_overview {
namespace {
using native_map::Color;
using native_map::DrawList;
using native_map::FilledRectangle;
using native_map::Image;
using native_map::Point;
using native_map::StrokedRectangle;
using native_map::Text;
using native_map::TextAlign;
using native_map::UiRect;

constexpr Color title_color{154, 225, 255, 255};
constexpr Color muted_color{154, 181, 211, 235};
constexpr Color message_color{238, 244, 255, 255};
constexpr Color button_color{14, 30, 48, 255};
constexpr Color button_hover{24, 46, 70, 255};
constexpr Color border_color{116, 174, 225, 255};

void fill(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(FilledRectangle{bounds, color});
}
void stroke(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(StrokedRectangle{bounds, color});
}
void text(DrawList &out, Point at, std::string value, Color color, int pixels,
          float wrap = 0.f, TextAlign align = TextAlign::Left) {
  out.overlay.emplace_back(
      Text{at, std::move(value), color, pixels, wrap, std::nullopt, align});
}

// Reference MetricFormat.InterstellarDistance: metric primary + parsec suffix.
std::string interstellar_distance(const double light_years) {
  if (!std::isfinite(light_years) || light_years < 0.0)
    return "Distance unconfirmed";
  return core::format_interstellar_metric_primary(light_years) + " · " +
         core::detail::legacy_custom_fixed(light_years / 3.26156, 1, 1) + " pc";
}

// Reference: population >= 1000M renders as billions.
std::string population_label(const double millions) {
  return millions >= 1000.0
             ? core::detail::legacy_custom_fixed(millions / 1000.0, 2, 2) +
                   "B people"
             : core::detail::legacy_custom_fixed(millions, 1, 1) + "M people";
}
constexpr std::array<NativeLeaderCard, 3> council{{
    {"Civil Administration",
     "Planetary development and public services",
     "assets/visual/leaders/planetary-governor.jpg"},
    {"Science Directorate",
     "Research institutions and discovery",
     "assets/visual/leaders/chief-scientist.jpg"},
    {"Fleet Command",
     "Exploration, defense and fleet operations",
     "assets/visual/leaders/fleet-commander.jpg"},
}};

}  // namespace

std::span<const NativeLeaderCard> leadership_council() noexcept {
  return council;
}

NativeEmpireOverview build_empire_overview(
    const core::FreshCampaignState &campaign,
    const std::optional<int> selected_system_id) {
  NativeEmpireOverview overview;
  const auto player_id = campaign.player_civilization_id;

  // UiSelectedSystemHomeReference: name is knowledge-gated; distance is not.
  if (selected_system_id) {
    if (const auto selected = std::ranges::find(
            campaign.systems, *selected_system_id, &core::StellarSystem::id);
        selected != campaign.systems.end()) {
      overview.selected_system_name =
          campaign.knowledge.is_system_known(player_id, selected->id)
              ? selected->name
              : "Unknown";
      const auto home = std::ranges::find_if(
          campaign.civilizations, [player_id](const auto &civilization) {
            return civilization.id == player_id;
          });
      const auto home_system =
          home != campaign.civilizations.end()
              ? std::ranges::find(campaign.systems, home->home_system_id,
                                  &core::StellarSystem::id)
              : campaign.systems.end();
      if (home_system != campaign.systems.end())
        overview.selected_system_distance = interstellar_distance(
            core::distance_light_years(home_system->position,
                                       selected->position));
    }
  }

  for (const auto &colony : campaign.colonies) {
    if (colony.civilization_id != player_id) continue;
    NativeOverviewColony row{colony.id, colony.system_id,
                             colony.planetary_body_id, {},
                             {}, colony.population_millions};
    if (colony.planetary_body_id)
      if (const auto body = std::ranges::find(campaign.bodies,
                                              *colony.planetary_body_id,
                                              &core::PlanetaryBody::id);
          body != campaign.bodies.end())
        row.planet_name = body->name;
    if (const auto system =
            std::ranges::find(campaign.systems, colony.system_id,
                              &core::StellarSystem::id);
        system != campaign.systems.end())
      row.system_name = system->name;
    overview.colonies.push_back(std::move(row));
  }
  std::ranges::sort(overview.colonies, {}, &NativeOverviewColony::colony_id);

  for (const auto &fleet : campaign.fleets)
    if (fleet.is_active && fleet.civilization_id == player_id)
      overview.combined_power += core::own_fleet_combat_power(fleet);
  return overview;
}

OverviewLayout overview_layout_for(const NativeEmpireOverview &overview,
                                   const UiRect content) noexcept {
  const auto scale =
      std::clamp(std::min(content.width / 320.f, content.height / 300.f), .7f,
                 1.1f);
  OverviewLayout layout{
      scale,
      static_cast<int>(std::lround(13.f * scale)),
      static_cast<int>(std::lround(9.f * scale)),
      static_cast<int>(std::lround(12.f * scale)),
      static_cast<int>(std::lround(11.f * scale)),
      content,
      {}};
  // Header block: SELECTED SYSTEM + name + distance + separator + EMPIRE
  // OVERVIEW + COLONIES n — then one 52·scale row per colony.
  float y = content.y + 108.f * scale;
  layout.colony_rows.reserve(overview.colonies.size());
  for (std::size_t index = 0; index < overview.colonies.size(); ++index) {
    layout.colony_rows.push_back(
        {content.x, y, content.width, 48.f * scale});
    y += 52.f * scale;
  }

  // Reference BuildLeadershipCouncil (PlayerControls.cs sidebar): the three
  // fixed office portraits sit under the empire summary. The render's own
  // flow: colony end + 10·s, separator, +10·s, combined-power line (~30·s
  // block), separator, +10·s, heading, then 46·s card rows.
  const float bottom = content.y + content.height;
  float cursor = overview.colonies.empty()
                     ? content.y + 102.f * scale
                     : layout.colony_rows.back().y +
                           layout.colony_rows.back().height + 10.f * scale;
  cursor += 40.f * scale;  // separator + combined-power line
  if (cursor + 60.f * scale <= bottom) {
    cursor += 10.f * scale;  // council separator
    layout.council_heading = {content.x, cursor, content.width,
                              14.f * scale};
    cursor += 18.f * scale;
    for (std::size_t index = 0; index < council.size(); ++index) {
      if (cursor + 42.f * scale > bottom) break;
      layout.leader_rows.push_back(
          {content.x, cursor, content.width, 42.f * scale});
      cursor += 46.f * scale;
    }
  }
  return layout;
}

void render_empire_overview(DrawList &out, const NativeEmpireOverview &overview,
                            const OverviewLayout &layout,
                            const Point pointer,
                            const OverviewImageProvider *portraits) {
  const auto scale = layout.scale;
  const auto inner_x = layout.bounds.x;
  const auto inner_w = layout.bounds.width;
  float y = layout.bounds.y;

  text(out, {inner_x, y}, "SELECTED SYSTEM", title_color,
       layout.label_font_pixels, inner_w);
  y += 15.f * scale;
  text(out, {inner_x, y}, overview.selected_system_name, message_color,
       layout.body_font_pixels, inner_w);
  y += 22.f * scale;
  text(out, {inner_x, y}, "DISTANCE FROM HOMEWORLD", muted_color,
       layout.small_font_pixels, inner_w);
  y += 15.f * scale;
  text(out, {inner_x, y}, overview.selected_system_distance, title_color,
       layout.small_font_pixels, inner_w);
  y += 24.f * scale;
  stroke(out, {inner_x, y, inner_w, 1.f}, {64, 96, 128, 255});
  y += 10.f * scale;
  text(out, {inner_x, y},
       "COLONIES   " + std::to_string(overview.colonies.size()), title_color,
       layout.label_font_pixels, inner_w);
  y += 16.f * scale;

  for (std::size_t index = 0; index < overview.colonies.size(); ++index) {
    const auto &colony = overview.colonies[index];
    const auto row = layout.colony_rows[index];
    if (row.y + row.height > layout.bounds.y + layout.bounds.height) break;
    fill(out, row, row.contains(pointer) ? button_hover : button_color);
    stroke(out, row, border_color);
    text(out, {row.x + 7.f * scale, row.y + 4.f * scale}, colony.planet_name,
         message_color, layout.body_font_pixels, inner_w - 14.f * scale);
    text(out, {row.x + 7.f * scale, row.y + 21.f * scale}, colony.system_name,
         muted_color, layout.small_font_pixels, inner_w - 14.f * scale);
    text(out, {row.x + 7.f * scale, row.y + 34.f * scale},
         population_label(colony.population_millions), message_color,
         layout.small_font_pixels, inner_w - 14.f * scale);
  }

  if (!layout.colony_rows.empty())
    y = layout.colony_rows.back().y + layout.colony_rows.back().height +
        10.f * scale;
  if (y + 30.f * scale <= layout.bounds.y + layout.bounds.height) {
    stroke(out, {inner_x, y, inner_w, 1.f}, {64, 96, 128, 255});
    y += 10.f * scale;
    text(out, {inner_x, y},
         "Combined power  " +
             core::detail::legacy_custom_fixed(overview.combined_power, 0, 0),
         title_color, layout.small_font_pixels, inner_w);
  }

  // LEADERSHIP COUNCIL (reference PlayerControls.cs sidebar section).
  if (layout.council_heading.height > 0.f) {
    const float council_sep = layout.council_heading.y - 10.f * scale;
    stroke(out, {inner_x, council_sep, inner_w, 1.f}, {64, 96, 128, 255});
    text(out, {inner_x, layout.council_heading.y}, "LEADERSHIP COUNCIL",
         title_color, layout.label_font_pixels, inner_w);
    for (std::size_t index = 0; index < layout.leader_rows.size(); ++index) {
      const auto row = layout.leader_rows[index];
      const auto &card = council[index];
      fill(out, row, row.contains(pointer) ? button_hover : button_color);
      stroke(out, row, border_color);
      const UiRect portrait_frame{row.x + 4.f * scale, row.y + 4.f * scale,
                                  34.f * scale, row.height - 8.f * scale};
      if (portraits)
        if (const auto image = (*portraits)(card.asset_path)) {
          const float ratio =
              std::min(portrait_frame.width /
                           static_cast<float>(image->width()),
                       portrait_frame.height /
                           static_cast<float>(image->height()));
          const UiRect destination{
              portrait_frame.x +
                  (portrait_frame.width - image->width() * ratio) * .5f,
              portrait_frame.y +
                  (portrait_frame.height - image->height() * ratio) * .5f,
              image->width() * ratio, image->height() * ratio};
          out.overlay.emplace_back(Image{image, destination, std::nullopt,
                                         {255, 255, 255, 255}, row});
        }
      text(out, {row.x + 46.f * scale, row.y + 4.f * scale},
           std::string{card.role}, message_color, layout.body_font_pixels,
           inner_w - 50.f * scale);
      text(out, {row.x + 46.f * scale, row.y + 21.f * scale},
           std::string{card.responsibility}, muted_color,
           layout.small_font_pixels, inner_w - 50.f * scale);
    }
  }
}

}  // namespace stellar::native_overview
