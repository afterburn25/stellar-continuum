#include "native_economy.hpp"
#include "native_ui_theme.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <ranges>
#include <utility>
#include <vector>

#include "stellar/core/campaign_economy.hpp"
#include "stellar/core/construction_state.hpp"
#include "stellar/core/detail/legacy_number_format.hpp"
#include "stellar/core/fleet_state.hpp"
#include "stellar/core/sovereign_currency.hpp"

namespace stellar::native_economy {

namespace {

using core::CivilizationEconomy;
using core::Colony;
using core::EconomyConstructionState;
using core::EconomyFleetState;
using core::FreshCampaignState;
using core::IndustryPriority;
using native_map::Color;
using native_map::DrawList;
using native_map::FilledRectangle;
using native_map::InputEventType;
using native_map::Point;
using native_map::StrokedRectangle;
using native_map::Text;
using native_map::TextAlign;
using native_map::UiRect;

constexpr Color tile_color = native_ui::color::surface_secondary;
constexpr Color muted_color = native_ui::color::text_secondary;
constexpr Color accent_color = native_ui::color::economy;
constexpr Color gold_color = native_ui::color::economy;
constexpr Color income_color = native_ui::color::success;
constexpr Color cost_color = native_ui::color::danger;

void fill(DrawList &out, UiRect rect, Color color) {
  out.overlay.emplace_back(FilledRectangle{rect, color});
}
void stroke(DrawList &out, UiRect rect, Color color) {
  out.overlay.emplace_back(StrokedRectangle{rect, color});
}
void text(DrawList &out, Point at, std::string value, Color color, int pixels,
          float wrap = 0.f, TextAlign align = TextAlign::Left) {
  out.overlay.emplace_back(
      Text{at, std::move(value), color, pixels, wrap, std::nullopt, align});
}

// EconomyWorldView plumbing identical to native_logistics (the reference's
// PrototypeEconomyLogisticsView input path).
core::EconomyWorldView
economy_view(const FreshCampaignState &campaign,
             std::vector<EconomyConstructionState> &construction,
             std::vector<EconomyFleetState> &fleets) {
  construction = core::economic_construction_projection(campaign.construction);
  fleets = core::economic_fleet_projection(campaign.fleets);
  return {campaign.civilizations, campaign.bodies, construction, fleets};
}

std::string grouped(double value) {
  auto digits = std::to_string(static_cast<std::int64_t>(std::llround(value)));
  const bool negative = !digits.empty() && digits.front() == '-';
  for (auto at = static_cast<std::ptrdiff_t>(digits.size()) - 3;
       at > (negative ? 1 : 0); at -= 3)
    digits.insert(static_cast<std::size_t>(at), 1, ',');
  return digits;
}

std::string signed_rate(double value) {
  // Reference "{0:+0.00;-0.00;0.00} / DAY".
  return std::format("{:+.2f} / DAY", value);
}

std::string_view priority_display(IndustryPriority priority) noexcept {
  switch (priority) {
  case IndustryPriority::InfrastructureFirst: return "Infrastructure first";
  case IndustryPriority::ShipbuildingFirst: return "Shipbuilding first";
  case IndustryPriority::Balanced: default: return "Balanced";
  }
}

}  // namespace

NativeEconomyView
build_economy_view(const FreshCampaignState &campaign,
                   const core::AdaptiveResearchCampaignState *research,
                   const std::optional<core::CivilizationIndustryAllocation>
                       &last_allocation) {
  NativeEconomyView view;
  const auto player = campaign.player_civilization_id;
  const auto economy = std::ranges::find(campaign.economies, player,
                                         &CivilizationEconomy::civilization_id);
  if (economy == campaign.economies.end()) return view;
  view.ready = true;

  const auto currency =
      core::sovereign_currency_for_civilization(campaign.civilizations, player);
  std::vector<EconomyConstructionState> construction;
  std::vector<EconomyFleetState> fleets;
  const auto world = economy_view(campaign, construction, fleets);
  const auto flow = core::economy_credit_flow(world, campaign.colonies,
                                              campaign.economies, player);
  const auto capacity =
      core::industry_storage_capacity(world, campaign.colonies, player);

  view.cards = {{
      {"RESERVES", currency.format(economy->credits)},
      {"NET / DAY", currency.format_rate(flow.net_credits_per_day),
       flow.net_credits_per_day < 0.},
      {"INCOME / DAY", currency.format_rate(flow.gross_income_per_day)},
      {"COSTS / DAY", currency.format_rate(-flow.operating_costs_per_day),
       true},
      {"MATERIALS IN STORAGE",
       grouped(economy->industry) + " / " + grouped(capacity)},
      {"MATERIALS / DAY", signed_rate(economy->last_industry_per_second)},
  }};

  // Reference TreasuryHealth.Assess + the PlayerControls status switch.
  const auto health = core::assess_treasury(economy->credits,
                                            flow.net_credits_per_day,
                                            economy->operating_arrears);
  view.treasury_healthy =
      health.state == core::TreasuryHealthState::Surplus;
  switch (health.state) {
  case core::TreasuryHealthState::Surplus:
    view.treasury_status =
        "SURPLUS · Current income covers operating commitments.";
    break;
  case core::TreasuryHealthState::Deficit:
    view.treasury_status = std::format(
        "DEFICIT · Treasury runway {} days. Pause research, reduce fleet or "
        "surface upkeep, or add staffed revenue before reserves run out.",
        core::detail::legacy_custom_fixed(health.runway_days, 0, 1));
    break;
  case core::TreasuryHealthState::Depleted:
    view.treasury_status =
        "TREASURY DEPLETED · New authorizations are blocked. Pause research, "
        "reduce upkeep, or restore staffed revenue.";
    break;
  case core::TreasuryHealthState::Arrears:
  default:
    view.treasury_status = std::format(
        "OPERATING ARREARS · {} unpaid · {:.0f}% of current base operations "
        "funded. New income repays arrears before rebuilding reserves.",
        currency.format(economy->operating_arrears),
        economy->last_base_operations_funding_fraction * 100.);
    break;
  }

  // Reference UiIndustryPriority: stored priority (default Balanced) plus the
  // most recent allocation when the presentation cache holds one.
  const auto priority =
      economy->industry_priority.value_or(IndustryPriority::Balanced);
  view.industry_priority = priority;
  const auto weights = last_allocation
      ? core::IndustryPriorityWeights{last_allocation->construction_weight,
                                      last_allocation->shipbuilding_weight}
      : core::campaign_industry_weights(campaign.economies, player);
  const auto name = priority_display(priority);
  if (last_allocation) {
    view.priority_status = std::format(
        "Current choice: {} ({:.0f}:{:.0f}). Most recent allocation: {:.1f} "
        "materials to infrastructure and {:.1f} to shipbuilding.",
        name, weights.construction_weight, weights.shipbuilding_weight,
        last_allocation->construction_allocated,
        last_allocation->shipbuilding_allocated);
  } else {
    view.priority_status = std::format(
        "Current choice: {} ({:.0f}:{:.0f}). It applies when demand "
        "competes; spare materials go to other work. Infrastructure includes "
        "surface sites and empire projects.",
        name, weights.construction_weight, weights.shipbuilding_weight);
  }

  view.income_rows = {
      {"COLONY ECONOMY", currency.format_rate(flow.colony_revenue_per_day), "",
       true},
      {"SURFACE TRADE", currency.format_rate(flow.trade_revenue_per_day), "",
       true},
  };
  double reserved = 0.;
  if (research)
    for (const auto &funding : research->project_funding(player))
      reserved += std::max(0., funding.reserved_milestone_credits -
                                   funding.consumed_milestone_credits);
  view.cost_rows = {
      {"COLONY ADMINISTRATION",
       currency.format_rate(-flow.colony_administration_per_day)},
      {"POPULATION SERVICES",
       currency.format_rate(-flow.population_services_per_day)},
      {"HABITAT SUPPORT",
       currency.format_rate(-flow.habitat_support_per_day)},
      {"FLEET OPERATIONS",
       currency.format_rate(-flow.fleet_operations_per_day)},
      {"ORBITAL MAINTENANCE",
       currency.format_rate(-flow.orbital_maintenance_per_day)},
      {"SURFACE MAINTENANCE",
       currency.format_rate(-flow.surface_maintenance_per_day)},
      {"RESEARCH PROGRAMS",
       currency.format_rate(-flow.research_operations_per_day),
       "  ·  " + currency.format(reserved) + " RESERVED"},
  };
  return view;
}

EconomyLayout economy_layout_for(const NativeEconomyView &, int width,
                                 int height) {
  const auto sw = static_cast<float>(width), sh = static_cast<float>(height);
  const auto scale = std::max(1.f, sh / 900.f);
  EconomyLayout layout;
  layout.scale = scale;
  layout.heading_font_pixels = std::max(11, static_cast<int>(21.f * scale));
  layout.body_font_pixels = std::max(9, static_cast<int>(13.f * scale));
  layout.small_font_pixels = std::max(8, static_cast<int>(11.f * scale));
  // Right-docked panel below the top rail, matching the SUPPLY NETWORK dock;
  // taller to fit the six cards, priority block and the cash-flow rows.
  layout.panel = {std::max(112.f * scale, sw - 482.f * scale), 70.f * scale,
                  std::min(462.f * scale, sw - 128.f * scale),
                  std::min(768.f * scale, sh - 130.f * scale)};
  const auto pad = 12.f * scale;
  layout.header = {layout.panel.x + pad, layout.panel.y + pad,
                   layout.panel.width - pad * 2.f, 40.f * scale};
  layout.close_button = {layout.panel.x + layout.panel.width - pad -
                             26.f * scale,
                         layout.header.y, 26.f * scale, 26.f * scale};
  // Six metric cards in a 2x3 grid (reference GridContainer Columns=2).
  const auto card_gap = 8.f * scale;
  const auto card_width =
      (layout.panel.width - pad * 2.f - card_gap) * .5f;
  const auto card_height = 52.f * scale;
  const auto cards_top =
      layout.header.y + layout.header.height + 10.f * scale;
  for (int i = 0; i < 6; ++i)
    layout.cards[i] = {layout.panel.x + pad +
                           static_cast<float>(i % 2) *
                               (card_width + card_gap),
                       cards_top + static_cast<float>(i / 2) *
                                       (card_height + card_gap),
                       card_width, card_height};
  const auto content = layout.panel.width - pad * 2.f;
  layout.treasury_status = {layout.panel.x + pad,
                            cards_top + card_height * 3.f + card_gap * 2.f +
                                10.f * scale,
                            content, 34.f * scale};
  // INDUSTRIAL PRIORITY heading renders inside render(); the status text and
  // the three toggle buttons sit below it.
  layout.priority_status = {layout.panel.x + pad,
                            layout.treasury_status.y +
                                layout.treasury_status.height + 30.f * scale,
                            content, 44.f * scale};
  const auto buttons_top = layout.priority_status.y +
                           layout.priority_status.height + 6.f * scale;
  const auto button_width = (content - card_gap * 2.f) / 3.f;
  for (int i = 0; i < 3; ++i)
    layout.priority_buttons[i] = {layout.panel.x + pad +
                                      static_cast<float>(i) *
                                          (button_width + card_gap),
                                  buttons_top, button_width, 30.f * scale};
  layout.footer = {layout.panel.x + pad,
                   layout.panel.y + layout.panel.height - pad - 46.f * scale,
                   content, 46.f * scale};
  return layout;
}

EconomyCommand
NativeEconomyPanel::handle(const native_map::InputEvent &event,
                           const NativeEconomyView &view, int width,
                           int height) {
  EconomyCommand command;
  if (!visible_) return command;
  const auto layout = economy_layout_for(view, width, height);
  if (event.type == InputEventType::LeftReleased &&
      layout.close_button.contains(event.position)) {
    close();
    command.kind = EconomyCommandKind::Close;
    command.captured = true;
    return command;
  }
  if (event.type == InputEventType::LeftReleased)
    for (int i = 0; i < 3; ++i)
      if (layout.priority_buttons[i].contains(event.position)) {
        command.kind = EconomyCommandKind::SetIndustryPriority;
        command.priority = static_cast<IndustryPriority>(i);
        command.captured = true;
        return command;
      }
  if ((event.type == InputEventType::LeftPressed ||
       event.type == InputEventType::RightPressed ||
       event.type == InputEventType::LeftReleased ||
       event.type == InputEventType::RightReleased ||
       event.type == InputEventType::Wheel) &&
      layout.panel.contains(event.position))
    command.captured = true;
  return command;
}

void NativeEconomyPanel::render(DrawList &out, const NativeEconomyView &view,
                                int width, int height) const {
  if (!visible_) return;
  const auto layout = economy_layout_for(view, width, height);
  const auto scale = layout.scale;
  const auto pad = 12.f * scale;
  native_ui::panel(out, layout.panel, native_ui::Tone::Economy);

  text(out, {layout.header.x, layout.header.y}, "SOVEREIGN TREASURY",
       gold_color, layout.heading_font_pixels);
  text(out, {layout.header.x, layout.header.y + 24.f * scale},
       "Live civilian revenue and operating commitments", muted_color,
       layout.small_font_pixels);
  native_ui::button(out, layout.close_button, "X", {},
                    layout.body_font_pixels + 1);

  if (!view.ready) {
    text(out, {layout.panel.x + pad,
               layout.header.y + layout.header.height + 20.f * scale},
         "Industry priority is unavailable while the campaign initializes.",
         muted_color, layout.body_font_pixels, layout.panel.width - pad * 2.f);
    return;
  }

  const Color card_tones[6] = {gold_color,
                               accent_color,
                               income_color,
                               cost_color,
                               accent_color,
                               accent_color};
  for (int i = 0; i < 6; ++i) {
    const auto &card = view.cards[i];
    const auto &rect = layout.cards[i];
    fill(out, rect, tile_color);
    stroke(out, rect, native_ui::color::keyline);
    fill(out, {rect.x, rect.y, 3.f, rect.height},
         card.warning ? cost_color : card_tones[i]);
    text(out, {rect.x + 10.f * scale, rect.y + 7.f * scale}, card.label,
         muted_color, layout.small_font_pixels - 1);
    text(out, {rect.x + 10.f * scale, rect.y + 24.f * scale}, card.value,
         card.warning ? cost_color : card_tones[i],
         layout.body_font_pixels + 3);
  }

  fill(out, layout.treasury_status, native_ui::color::surface_secondary);
  fill(out, {layout.treasury_status.x, layout.treasury_status.y, 3.f,
             layout.treasury_status.height},
       view.treasury_healthy ? income_color : cost_color);
  text(out, {layout.treasury_status.x + 9.f * scale,
             layout.treasury_status.y + 5.f * scale},
       view.treasury_status,
       view.treasury_healthy ? income_color : cost_color,
       layout.body_font_pixels, layout.treasury_status.width - 18.f * scale);

  const auto priority_heading_y =
      layout.priority_status.y - 18.f * scale;
  text(out, {layout.panel.x + pad, priority_heading_y}, "INDUSTRIAL PRIORITY",
       accent_color, layout.body_font_pixels);
  text(out, {layout.priority_status.x, layout.priority_status.y},
       view.priority_status, muted_color, layout.small_font_pixels,
       layout.priority_status.width);
  const char *priority_labels[3] = {"Balanced", "Infrastructure",
                                    "Shipbuilding"};
  for (int i = 0; i < 3; ++i) {
    const auto &rect = layout.priority_buttons[i];
    const bool active = static_cast<int>(view.industry_priority) == i;
    native_ui::button(out, rect, priority_labels[i], {-1.f, -1.f},
                      layout.small_font_pixels, native_ui::Tone::Economy,
                      active);
  }

  // DAILY CASH FLOW — two stacked groups matching the reference income and
  // operating-costs grids.
  const auto flow_top = layout.priority_buttons[0].y +
                        layout.priority_buttons[0].height + 14.f * scale;
  text(out, {layout.panel.x + pad, flow_top}, "DAILY CASH FLOW", accent_color,
       layout.body_font_pixels);
  auto cursor = flow_top + 20.f * scale;
  const auto flow_bottom = layout.footer.y - 6.f * scale;
  const auto flow_rows = [&](const char *heading, Color tone,
                             const std::vector<NativeEconomyFlowRow> &rows) {
    if (cursor > flow_bottom) return;
    text(out, {layout.panel.x + pad, cursor}, heading, tone,
         layout.small_font_pixels - 1);
    cursor += 15.f * scale;
    for (const auto &row : rows) {
      if (cursor > flow_bottom) return;
      text(out, {layout.panel.x + pad + 4.f * scale, cursor}, row.label,
           muted_color, layout.small_font_pixels);
      text(out, {layout.panel.x + layout.panel.width - pad, cursor},
           row.value + row.suffix, row.income ? income_color : cost_color,
           layout.small_font_pixels, 0.f, TextAlign::Right);
      cursor += 15.f * scale;
    }
    cursor += 6.f * scale;
  };
  flow_rows("INCOME", income_color, view.income_rows);
  flow_rows("OPERATING COSTS", cost_color, view.cost_rows);

  text(out, {layout.footer.x, layout.footer.y},
       "Every civilization begins with its own sovereign currency. Trade "
       "hubs add revenue while they have enough surface power. Construction "
       "and ship orders are one-time capital costs; active research programs "
       "have a continuing daily cost.",
       muted_color, layout.small_font_pixels - 1, layout.footer.width);
}

}  // namespace stellar::native_economy
