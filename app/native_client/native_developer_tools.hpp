#pragma once

// Native port of the reference DeveloperToolsLayer: a floating DEVELOPER
// TOOLS panel that lists the six authorized Developer commands and runs them
// through the campaign session boundary. The panel only opens for a
// Developer-mode campaign.

#include <stellar/core/developer_commands.hpp>
#include <stellar/engine/native_map_platform.hpp>

#include <array>
#include <string>

namespace stellar::native_developer {

struct NativeDeveloperToolsView {
  bool developer{};
  bool tools_used{};
  // The most recent command outcome; styled like the reference result text.
  std::string result;
  bool result_accepted{};
};

// Panel geometry, exposed for tests and graphical smoke drivers.
struct DeveloperToolsLayout {
  float scale{};
  int heading_font_pixels{}, body_font_pixels{}, small_font_pixels{};
  native_map::UiRect panel, header, close_button, mode_text, result_text;
  // One clickable row per catalog command; its description renders inside.
  std::array<native_map::UiRect, 6> command_rows;
};

[[nodiscard]] DeveloperToolsLayout
developer_tools_layout_for(int width, int height);

enum class DeveloperToolsCommandKind { None, Close, Run };

struct DeveloperToolsCommand {
  DeveloperToolsCommandKind kind{DeveloperToolsCommandKind::None};
  bool captured{};
  std::string command_id;
};

// Toggleable DEVELOPER TOOLS panel. Command rows report Run commands; the
// caller dispatches them through NativeCampaignSession::run_developer_command.
class NativeDeveloperToolsPanel final {
 public:
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  void open() noexcept { visible_ = true; }
  void close() noexcept { visible_ = false; }
  void toggle() noexcept { visible_ = !visible_; }

  [[nodiscard]] DeveloperToolsCommand
  handle(const native_map::InputEvent &event, int width, int height);
  void render(native_map::DrawList &out, const NativeDeveloperToolsView &view,
              int width, int height,
              const native_map::Point *pointer = nullptr) const;

 private:
  bool visible_{};
};

}  // namespace stellar::native_developer
