#pragma once

// Native port of the reference dashboard Economy page
// (PlayerControls.BuildEconomyPage + Main.Economy): a toggleable ECONOMY
// panel showing the sovereign treasury cards, the industry-priority
// toggles, and the daily cash-flow breakdown. All authoritative
// calculation stays in core (economy_credit_flow / assess_treasury /
// campaign_industry_weights / set_industry_priority).

#include <stellar/core/adaptive_research_campaign.hpp>
#include <stellar/core/colony_economy.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/industry_allocation.hpp>
#include <stellar/engine/native_map_platform.hpp>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace stellar::native_economy {

struct NativeEconomyCard {
  std::string label, value;
  // Reference modulate: accent normally, warning red when the net flow is
  // negative.
  bool warning{};
};

struct NativeEconomyFlowRow {
  std::string label, value;
  // Reference research row tail ("· N RESERVED"); empty for other rows.
  std::string suffix;
  bool income{};
};

struct NativeEconomyView {
  // False when the campaign has no resolvable player economy — the panel
  // then renders the reference "initializing" guidance.
  bool ready{};
  // RESERVES, NET / DAY, INCOME / DAY, COSTS / DAY, MATERIALS IN STORAGE,
  // MATERIALS / DAY — reference card order.
  std::array<NativeEconomyCard, 6> cards;
  std::string treasury_status;
  bool treasury_healthy{};
  core::IndustryPriority industry_priority{core::IndustryPriority::Balanced};
  std::string priority_status;
  std::vector<NativeEconomyFlowRow> income_rows, cost_rows;
};

// `research` may be null (headless callers); milestone reservation reporting
// then degrades like the reference's `_adaptiveResearch is null` branch.
// `last_allocation` is the presentation-side cache of the player's most
// recent CivilizationIndustryAllocation, matching the reference's
// _lastPlayerIndustryAllocation (cleared when a new priority is chosen).
[[nodiscard]] NativeEconomyView
build_economy_view(const core::FreshCampaignState &campaign,
                   const core::AdaptiveResearchCampaignState *research,
                   const std::optional<core::CivilizationIndustryAllocation>
                       &last_allocation);

// Panel geometry, exposed for tests and graphical smoke drivers.
struct EconomyLayout {
  float scale{};
  int heading_font_pixels{}, body_font_pixels{}, small_font_pixels{};
  native_map::UiRect panel, header, close_button, treasury_status,
      priority_status, footer;
  std::array<native_map::UiRect, 6> cards;
  // Balanced / Infrastructure first / Shipbuilding first — reference order.
  std::array<native_map::UiRect, 3> priority_buttons;
};

[[nodiscard]] EconomyLayout
economy_layout_for(const NativeEconomyView &view, int width, int height);

enum class EconomyCommandKind { None, Close, SetIndustryPriority };

struct EconomyCommand {
  EconomyCommandKind kind{EconomyCommandKind::None};
  bool captured{};
  core::IndustryPriority priority{core::IndustryPriority::Balanced};
};

// Toggleable ECONOMY panel. Display rows are read-only; the priority
// toggles report SetIndustryPriority commands for the caller to dispatch
// through core::set_industry_priority.
class NativeEconomyPanel final {
 public:
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  void open() noexcept { visible_ = true; }
  void close() noexcept { visible_ = false; }
  void toggle() noexcept { visible_ = !visible_; }

  // Reference MouseFilter.Stop: presses inside the panel are captured and
  // never reach the map.
  [[nodiscard]] EconomyCommand handle(const native_map::InputEvent &event,
                                      const NativeEconomyView &view,
                                      int width, int height);
  void render(native_map::DrawList &out, const NativeEconomyView &view,
              int width, int height) const;

 private:
  bool visible_{};
};

}  // namespace stellar::native_economy
