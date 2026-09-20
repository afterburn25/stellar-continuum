#pragma once

// Native port of the reference DeveloperToolsLayer: a floating DEVELOPER
// TOOLS panel that lists the six authorized Developer commands and runs them
// through the campaign session boundary. The panel only opens for a
// Developer-mode campaign. A tab strip adds the Stellar Tools inspectors:
// engine diagnostics (profiler/memory) and the save-slot chain.

#include <stellar/core/developer_commands.hpp>
#include <stellar/engine/localization.hpp>
#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/ui_viewmodels.hpp>

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::native_developer {

enum class DeveloperToolsTab { Commands = 0, Diagnostics = 1, Saves = 2 };
inline constexpr int developer_tools_tab_count = 3;

// One row on the Saves tab — a slot in the primary/.bak/.bak.N chain.
struct DeveloperSaveSlotRow {
  std::string label;
  std::string detail;
  // 0 = verified/current, 1 = loaded-generations history, 2 = no sidecar,
  // 3 = integrity mismatch / unreadable, 4 = missing.
  int status{};
};

struct NativeDeveloperToolsView {
  bool developer{};
  bool tools_used{};
  // The most recent command outcome; styled like the reference result text.
  std::string result;
  bool result_accepted{};
  // Diagnostics tab: preformatted rows (profiler, memory, jobs). Rows with a
  // leading "--" are rendered as section headers.
  std::vector<std::string> diagnostics;
  // Saves tab: the rolling save chain for the session's slot.
  std::vector<DeveloperSaveSlotRow> save_slots;
};

// Panel geometry, exposed for tests and graphical smoke drivers.
struct DeveloperToolsLayout {
  float scale{};
  int heading_font_pixels{}, body_font_pixels{}, small_font_pixels{};
  native_map::UiRect panel, header, close_button, mode_text, result_text;
  std::array<native_map::UiRect, developer_tools_tab_count> tabs;
  // Scrollable content region under the tab strip.
  native_map::UiRect content;
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
  NativeDeveloperToolsPanel();

  // Loads an additional locale JSON document or file into the panel's
  // LocalizationService; later tables override the embedded English
  // catalog, so mods/locale packs can re-skin the panel text.
  bool load_locale_document(std::string_view json, std::string *error = nullptr);
  bool load_locale_file(const std::string &path, std::string *error = nullptr);
  void set_locale(std::string locale) {
    localization_.set_locale(std::move(locale));
  }

  [[nodiscard]] bool visible() const noexcept { return visible_; }
  [[nodiscard]] DeveloperToolsTab active_tab() const noexcept {
    return active_tab_;
  }
  void open() noexcept { visible_ = true; }
  void close() noexcept { visible_ = false; }
  void toggle() noexcept { visible_ = !visible_; }

  [[nodiscard]] DeveloperToolsCommand
  handle(const native_map::InputEvent &event, int width, int height);
  void render(native_map::DrawList &out, const NativeDeveloperToolsView &view,
              int width, int height,
              const native_map::Point *pointer = nullptr) const;

 private:
  [[nodiscard]] std::string tr(std::string_view key) const {
    return localization_.translate(key);
  }

  engine::LocalizationService localization_;
  // Scroll windows for the Diagnostics and Saves tabs (req 24 adoption).
  mutable engine::VirtualizedList diagnostics_list_, saves_list_;
  bool visible_{};
  DeveloperToolsTab active_tab_{DeveloperToolsTab::Commands};
};

}  // namespace stellar::native_developer
