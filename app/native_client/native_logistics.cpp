#include "native_logistics.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <ranges>
#include <unordered_map>
#include <utility>

#include "stellar/core/campaign_economy.hpp"
#include "stellar/core/construction_state.hpp"
#include "stellar/core/fleet_state.hpp"

namespace stellar::native_logistics {
namespace {

using namespace stellar::core;
using native_map::Color;
using native_map::DrawList;
using native_map::FilledRectangle;
using native_map::Point;
using native_map::StrokedRectangle;
using native_map::Text;
using native_map::TextAlign;
using native_map::UiRect;

constexpr Color panel_color{10, 22, 36, 235};
constexpr Color border_color{116, 174, 225, 255};
constexpr Color title_color{154, 225, 255, 255};
constexpr Color muted_color{154, 181, 211, 235};
constexpr Color message_color{238, 244, 255, 255};
constexpr Color tile_color{16, 24, 38, 255};
constexpr Color supply_color{143, 229, 177, 255};   // reference "8fe5b1"
constexpr Color demand_color{240, 197, 106, 255};   // VisualUi.Gold
constexpr Color delivered_color{140, 196, 255, 255};
constexpr Color shortfall_color{238, 154, 145, 255};  // reference "ee9a91"
constexpr Color button_hover{24, 46, 70, 255};

std::string_view condition_name(SupplyCondition condition) noexcept {
  switch (condition) {
  case SupplyCondition::Healthy: return "Healthy";
  case SupplyCondition::Strained: return "Strained";
  case SupplyCondition::Critical: return "Critical";
  }
  return "Healthy";
}

std::string upper(std::string value) {
  for (auto &c : value)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return value;
}

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

// EconomyWorldView shared by the summary and network builders; identical to the
// reference's PrototypeEconomyLogisticsView input plumbing.
EconomyWorldView economy_view(const FreshCampaignState &campaign,
                              std::vector<EconomyConstructionState> &construction,
                              std::vector<EconomyFleetState> &fleets) {
  construction = economic_construction_projection(campaign.construction);
  fleets = economic_fleet_projection(campaign.fleets);
  return {campaign.civilizations, campaign.bodies, construction, fleets};
}

}  // namespace

std::string_view
logistics_node_kind_label(LogisticsNodeKind kind) noexcept {
  switch (kind) {
  case LogisticsNodeKind::Homeworld: return "Homeworld";
  case LogisticsNodeKind::OrbitalHub: return "Orbital hub";
  case LogisticsNodeKind::LunarSettlement: return "Lunar settlement";
  case LogisticsNodeKind::PlanetarySettlement: return "Planetary settlement";
  case LogisticsNodeKind::ResourceSite: return "Resource site";
  case LogisticsNodeKind::Depot: return "Depot";
  case LogisticsNodeKind::Shipyard: return "Shipyard";
  }
  return "Node";
}

std::string logistics_summary_line(const FreshCampaignState &campaign,
                                   int player_civilization_id) {
  const bool has_economy = std::ranges::any_of(
      campaign.economies, [player_civilization_id](const auto &economy) {
        return economy.civilization_id == player_civilization_id;
      });
  if (!has_economy) return "Supply initializing…";
  try {
    std::vector<EconomyConstructionState> construction;
    std::vector<EconomyFleetState> fleets;
    const auto world = economy_view(campaign, construction, fleets);
    const auto logistics = economy_logistics(world, campaign.colonies,
                                             campaign.economies,
                                             player_civilization_id);
    const double local_coverage =
        logistics.total_support_demand_per_day <= 0.
            ? 1.
            : logistics.total_local_support_capacity_per_day /
                  logistics.total_support_demand_per_day;
    return std::format(
        "Supply {} · effective {:.0f}% · local {:.0f}% · imports {:.2f}/day · "
        "cargo {:.2f}/day · strained {} · critical {}",
        condition_name(logistics.condition),
        logistics.effective_coverage_ratio * 100., local_coverage * 100.,
        logistics.import_requirement_per_day,
        logistics.cargo_handling_capacity_per_day,
        logistics.strained_colony_count, logistics.critical_colony_count);
  } catch (const std::exception &) {
    return "Supply initializing…";
  }
}

NativeHomeLogistics build_home_logistics(const FreshCampaignState &campaign,
                                         int player_civilization_id) {
  NativeHomeLogistics view;
  const bool has_economy = std::ranges::any_of(
      campaign.economies, [player_civilization_id](const auto &economy) {
        return economy.civilization_id == player_civilization_id;
      });
  if (!has_economy) {
    view.guidance = "Campaign logistics are initializing…";
    return view;
  }
  try {
    std::vector<EconomyConstructionState> construction;
    std::vector<EconomyFleetState> fleets;
    const auto world = economy_view(campaign, construction, fleets);
    const auto network = home_system_logistics(world, campaign.colonies,
                                               campaign.economies,
                                               player_civilization_id);
    const auto system = std::ranges::find(
        campaign.systems, network.home_system_id, &StellarSystem::id);
    view.ready = true;
    view.system_name =
        system != campaign.systems.end()
            ? system->name
            : std::format("System {}", network.home_system_id);
    view.corridor_count = static_cast<int>(network.links.size());
    view.supply_per_day = network.total_supply_offered_per_day;
    view.demand_per_day = network.total_demand_per_day;
    view.delivered_per_day = network.total_allocated_per_day;
    view.shortfall_per_day = network.total_unmet_demand_per_day;
    view.guidance =
        view.shortfall_per_day > 0.001
            ? "NEXT DECISION · Restore staffing, power or funding at the "
              "shortfall node, then add freight capacity if delivery still "
              "cannot meet demand."
            : "NETWORK READY · Supply currently meets represented demand. "
              "Expansion will add new corridors and operating requirements.";

    std::unordered_map<int, double> supplies, demands, delivered;
    for (const auto &offer : network.supply_offers)
      supplies[offer.node_id] += offer.available_per_day;
    for (const auto &demand : network.demands)
      demands[demand.node_id] += demand.required_per_day;
    for (const auto &allocation : network.daily_flow.allocations)
      delivered[allocation.destination_node_id] += allocation.allocated_per_day;

    const auto count = std::min<std::size_t>(8, network.nodes.size());
    view.nodes.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      const auto &node = network.nodes[i];
      const double supply = supplies[node.id], demand = demands[node.id];
      const double received = delivered[node.id];
      view.nodes.push_back(
          {node.id, node.name,
           std::string(logistics_node_kind_label(node.kind)),
           demand <= 0.          ? "Supply node"
           : received + 0.0001 >= demand ? "Fully supplied"
                                : "Shortfall",
           supply, demand, received});
    }
  } catch (const std::exception &) {
    view.ready = false;
    view.guidance = "Campaign logistics are initializing…";
  }
  return view;
}

LogisticsLayout logistics_layout_for(const NativeHomeLogistics &logistics,
                                     int width, int height) {
  const auto sw = static_cast<float>(width), sh = static_cast<float>(height);
  const auto scale = std::max(1.f, sh / 900.f);
  LogisticsLayout layout;
  layout.scale = scale;
  // Reference geometry: right-docked sidebar panel below the top rail.
  layout.panel = {std::max(112.f * scale, sw - 470.f * scale), 78.f * scale,
                  std::min(450.f * scale, sw - 128.f * scale),
                  std::min(560.f * scale, sh - 210.f * scale)};
  const auto pad = 12.f * scale;
  layout.header = {layout.panel.x + pad, layout.panel.y + pad,
                   layout.panel.width - pad * 2.f, 34.f * scale};
  layout.close_button = {layout.panel.x + layout.panel.width - pad -
                             26.f * scale,
                         layout.header.y, 26.f * scale, 26.f * scale};
  // Four metric tiles in a 2x2 grid.
  const auto metric_top = layout.header.y + layout.header.height + 14.f * scale;
  const auto metric_gap = 10.f * scale;
  const auto metric_width =
      (layout.panel.width - pad * 2.f - metric_gap) * .5f;
  const auto metric_height = 62.f * scale;
  for (int i = 0; i < 4; ++i)
    layout.metrics[i] = {
        layout.panel.x + pad + static_cast<float>(i % 2) *
                                   (metric_width + metric_gap),
        metric_top + static_cast<float>(i / 2) * (metric_height + metric_gap),
        metric_width, metric_height};
  layout.guidance = {layout.panel.x + pad,
                     metric_top + metric_height * 2.f + metric_gap + 14.f * scale,
                     layout.panel.width - pad * 2.f, 44.f * scale};
  const auto list_y = layout.guidance.y + layout.guidance.height + 24.f * scale;
  const auto list_bottom = layout.panel.y + layout.panel.height - pad;
  layout.empty_hint = {layout.panel.x + pad, list_y,
                       layout.panel.width - pad * 2.f, 40.f * scale};
  const auto card_width = layout.panel.width - pad * 2.f;
  float cursor = list_y;
  for (std::size_t i = 0; i < logistics.nodes.size(); ++i) {
    const UiRect card{layout.panel.x + pad, cursor, card_width, 56.f * scale};
    if (card.y + card.height > list_bottom) break;
    layout.node_cards.push_back(card);
    cursor += card.height + 7.f * scale;
  }
  return layout;
}

bool NativeLogisticsView::handle(const native_map::InputEvent &event,
                                 const NativeHomeLogistics &logistics,
                                 int width, int height) {
  if (!visible_) return false;
  const auto layout = logistics_layout_for(logistics, width, height);
  if (event.type == native_map::InputEventType::LeftReleased &&
      layout.close_button.contains(event.position)) {
    close();
    return true;
  }
  // ContainPointerInput: presses inside the panel never reach the map.
  if ((event.type == native_map::InputEventType::LeftPressed ||
       event.type == native_map::InputEventType::RightPressed ||
       event.type == native_map::InputEventType::LeftReleased ||
       event.type == native_map::InputEventType::RightReleased ||
       event.type == native_map::InputEventType::Wheel) &&
      layout.panel.contains(event.position))
    return true;
  return false;
}

void NativeLogisticsView::render(DrawList &out,
                                 const NativeHomeLogistics &logistics,
                                 int width, int height) const {
  if (!visible_) return;
  const auto layout = logistics_layout_for(logistics, width, height);
  const auto scale = layout.scale;
  fill(out, layout.panel, panel_color);
  stroke(out, layout.panel, border_color);

  text(out, {layout.header.x, layout.header.y}, "SUPPLY NETWORK", title_color,
       std::max(11, static_cast<int>(21.f * scale)));
  text(out, {layout.header.x, layout.header.y + 22.f * scale},
       std::format("{} · {} NODES · {} CORRIDORS",
                   upper(logistics.system_name), logistics.nodes.size(),
                   logistics.corridor_count),
       muted_color, std::max(9, static_cast<int>(12.f * scale)));
  fill(out, layout.close_button, button_hover);
  text(out, {layout.close_button.x + 8.f * scale,
             layout.close_button.y + 4.f * scale},
       "X", muted_color, std::max(10, static_cast<int>(14.f * scale)));

  const char *titles[4] = {"SUPPLY / DAY", "DEMAND / DAY", "DELIVERED / DAY",
                           "SHORTFALL / DAY"};
  const double values[4] = {logistics.supply_per_day, logistics.demand_per_day,
                            logistics.delivered_per_day,
                            logistics.shortfall_per_day};
  const Color colors[4] = {supply_color, demand_color, delivered_color,
                           logistics.shortfall_per_day > 0.001
                               ? shortfall_color
                               : supply_color};
  for (int i = 0; i < 4; ++i) {
    fill(out, layout.metrics[i], tile_color);
    text(out, {layout.metrics[i].x + 10.f * scale,
               layout.metrics[i].y + 8.f * scale},
         titles[i], muted_color, std::max(8, static_cast<int>(10.f * scale)));
    text(out, {layout.metrics[i].x + 10.f * scale,
               layout.metrics[i].y + 26.f * scale},
         std::format("{:.2f}", values[i]), colors[i],
         std::max(12, static_cast<int>(20.f * scale)));
  }

  text(out, {layout.guidance.x, layout.guidance.y}, logistics.guidance,
       logistics.shortfall_per_day > 0.001 ? shortfall_color : supply_color,
       std::max(9, static_cast<int>(12.f * scale)), layout.guidance.width);

  const auto nodes_y = layout.guidance.y + layout.guidance.height;
  text(out, {layout.panel.x + 12.f * scale, nodes_y}, "NETWORK NODES",
       title_color, std::max(9, static_cast<int>(12.f * scale)));

  if (!logistics.ready || layout.node_cards.empty()) {
    text(out, {layout.empty_hint.x, layout.empty_hint.y},
         logistics.ready ? "No represented supply nodes are available yet."
                         : logistics.guidance,
         muted_color, std::max(10, static_cast<int>(13.f * scale)),
         layout.empty_hint.width);
    return;
  }
  for (std::size_t i = 0; i < layout.node_cards.size(); ++i) {
    const auto &card = layout.node_cards[i];
    const auto &node = logistics.nodes[i];
    fill(out, card, tile_color);
    text(out, {card.x + 10.f * scale, card.y + 8.f * scale}, upper(node.name),
         message_color, std::max(10, static_cast<int>(14.f * scale)),
         card.width - 120.f * scale);
    const bool shortfall = node.status == "Shortfall";
    text(out, {card.x + card.width - 10.f * scale, card.y + 10.f * scale},
         upper(node.status), shortfall ? shortfall_color : supply_color,
         std::max(8, static_cast<int>(10.f * scale)), 0.f, TextAlign::Right);
    text(out, {card.x + 10.f * scale, card.y + 32.f * scale},
         std::format("{} · SUPPLY {:.2f} · DEMAND {:.2f} · DELIVERED {:.2f}",
                     upper(node.kind_label), node.supply_per_day,
                     node.demand_per_day, node.delivered_per_day),
         muted_color, std::max(8, static_cast<int>(11.f * scale)),
         card.width - 20.f * scale);
  }
}

}  // namespace stellar::native_logistics
