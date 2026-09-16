#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "stellar/core/fleet_state.hpp"
#include "stellar/core/fresh_campaign.hpp"
#include "stellar/engine/native_map_platform.hpp"

namespace stellar::native_missions {

// Reference ExplorationMissionPhase.
enum class NativeMissionPhase {
  awaiting_order,
  traveling,
  scouting,
  science_survey,
  establishing_colony
};

struct NativeMissionCard {
  int fleet_id{};
  core::FleetRole role{};
  NativeMissionPhase phase{};
  std::string fleet_name, destination, eta, summary;
};

// Reference UiExplorationMissions: ActiveMissions.Take(8) — owned, active
// scout/science/colony fleets carrying observer-safe status text.
struct NativeMissionBoard {
  std::vector<NativeMissionCard> missions;
};

[[nodiscard]] NativeMissionBoard
build_mission_board(const core::FreshCampaignState &campaign);

[[nodiscard]] std::string_view
mission_phase_label(NativeMissionPhase phase) noexcept;
// Reference ExplorationMissionPanel.MissionColor — keyed on the phase label,
// so only "Traveling" and "Science survey" tint; every other label is gold.
[[nodiscard]] native_map::Color mission_phase_color(NativeMissionPhase) noexcept;

struct MissionLayout {
  float scale{};
  int heading_font_pixels{}, body_font_pixels{}, small_font_pixels{};
  native_map::UiRect panel, header, close_button, empty_hint;
  std::vector<native_map::UiRect> cards;
};

[[nodiscard]] MissionLayout
mission_layout_for(const NativeMissionBoard &board, int width, int height);

enum class MissionViewCommandKind { None, Close };

struct MissionViewCommand {
  MissionViewCommandKind kind{MissionViewCommandKind::None};
  bool captured{};
};

// Toggleable MISSIONS & SETTLEMENT panel (reference ExplorationMissionPanel's
// missions tab). Mission cards are display-only, matching the reference.
class NativeMissionView final {
 public:
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  void open() noexcept { visible_ = true; }
  void close() noexcept { visible_ = false; }
  void toggle() noexcept { visible_ = !visible_; }

  [[nodiscard]] MissionViewCommand handle(const native_map::InputEvent &event,
                                          const NativeMissionBoard &board,
                                          int width, int height);
  void render(native_map::DrawList &out, const NativeMissionBoard &board,
              int width, int height) const;

 private:
  bool visible_{};
};

}  // namespace stellar::native_missions
