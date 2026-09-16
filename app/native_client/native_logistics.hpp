#pragma once

// Native port of the reference LogisticsNetworkPanel + Main.Logistics
// (UiLogisticsSummary / UiHomeSystemLogistics): a read-only presentation model
// of the player civilization's home-system supply network plus a toggleable
// SUPPLY NETWORK panel. All authoritative calculation stays in
// core::home_system_logistics / core::economy_logistics.

#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/logistics.hpp>
#include <stellar/engine/native_map_platform.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace stellar::native_logistics {

struct NativeLogisticsNodeRow {
  int node_id{};
  std::string name, kind_label, status;
  double supply_per_day{}, demand_per_day{}, delivered_per_day{};
};

struct NativeHomeLogistics {
  // False when the campaign has no resolvable economy/logistics state for the
  // player — the panel then renders the reference "initializing" guidance.
  bool ready{};
  std::string system_name{"INITIALIZING"};
  int corridor_count{};
  double supply_per_day{}, demand_per_day{}, delivered_per_day{},
      shortfall_per_day{};
  std::string guidance;
  // Up to eight node rows, mirroring the reference's Take(8) bound.
  std::vector<NativeLogisticsNodeRow> nodes;
};

// UiLogisticsSummary port — one-line status-bar supply readout.
[[nodiscard]] std::string
logistics_summary_line(const core::FreshCampaignState &campaign,
                       int player_civilization_id);

[[nodiscard]] NativeHomeLogistics
build_home_logistics(const core::FreshCampaignState &campaign,
                     int player_civilization_id);

[[nodiscard]] std::string_view
logistics_node_kind_label(core::LogisticsNodeKind kind) noexcept;

// Panel geometry, exposed for tests and graphical smoke drivers.
struct LogisticsLayout {
  native_map::UiRect panel, header, close_button, guidance, empty_hint;
  // SUPPLY / DEMAND / DELIVERED / SHORTFALL metric tiles.
  native_map::UiRect metrics[4];
  std::vector<native_map::UiRect> node_cards;
  float scale{};
};

[[nodiscard]] LogisticsLayout
logistics_layout_for(const NativeHomeLogistics &logistics, int width,
                     int height);

// Toggleable SUPPLY NETWORK panel; read-only like the reference — it renders
// the filtered presentation model and never mutates logistics state.
class NativeLogisticsView final {
 public:
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  void open() noexcept { visible_ = true; }
  void close() noexcept { visible_ = false; }
  void toggle() noexcept { visible_ = !visible_; }

  // Returns true when the event was consumed by the panel (close button or a
  // press inside the panel bounds — reference MouseFilter.Stop behavior).
  [[nodiscard]] bool handle(const native_map::InputEvent &event,
                            const NativeHomeLogistics &logistics, int width,
                            int height);
  void render(native_map::DrawList &out, const NativeHomeLogistics &logistics,
              int width, int height) const;

 private:
  bool visible_{};
};

}  // namespace stellar::native_logistics
