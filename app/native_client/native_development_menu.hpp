#pragma once

// Native port of the reference MainMenuLayer DevelopmentPanel: the campaign
// menu's DEVELOPMENT row opens this submenu with the Developer save entry,
// a WORLD SEED field for a fresh Developer campaign, the Developer tools
// shortcut and a Back row.

#include <stellar/engine/native_map_platform.hpp>

#include <string>

namespace stellar::native_development {

struct DevelopmentMenuLayout {
  float scale{};
  int heading_font_pixels{}, body_font_pixels{}, small_font_pixels{};
  native_map::UiRect panel, header, open_button, seed_label, seed_input,
      new_button, tools_button, back_button, status_text;
};

[[nodiscard]] DevelopmentMenuLayout
development_menu_layout_for(int width, int height);

enum class DevelopmentMenuCommandKind {
  None,
  Back,
  OpenDeveloper,
  NewDeveloperCampaign,
  OpenTools,
};

struct DevelopmentMenuCommand {
  DevelopmentMenuCommandKind kind{DevelopmentMenuCommandKind::None};
  bool captured{};
  // Parsed world seed when kind == NewDeveloperCampaign.
  std::int64_t seed{};
};

struct DevelopmentMenuView {
  bool developer_mode{};
  bool has_developer_save{};
  std::string status;
};

// The Development submenu is a campaign-menu child; Escape and Back return to
// the menu rather than closing the menu itself.
class NativeDevelopmentMenu final {
 public:
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  void open();
  void close() noexcept;
  [[nodiscard]] bool wants_text_input() const noexcept {
    return visible_ && seed_focused_;
  }
  [[nodiscard]] const std::string &seed_text() const noexcept {
    return seed_text_;
  }

  [[nodiscard]] DevelopmentMenuCommand
  handle(const native_map::InputEvent &event, int width, int height);
  void render(native_map::DrawList &out, const DevelopmentMenuView &view,
              int width, int height,
              const native_map::Point *pointer = nullptr) const;

 private:
  bool visible_{};
  std::string seed_text_{"20260908"};  // PlayableDemoScenario.Seed
  bool seed_focused_{}, confirm_armed_{}, invalid_seed_{}, pressed_{};
};

}  // namespace stellar::native_development
