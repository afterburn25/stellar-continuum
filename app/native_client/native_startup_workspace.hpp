#pragma once
#include "native_menu_hover.hpp"

#include "native_new_game_workspace.hpp"
#include "native_startup_artwork.hpp"
#include "native_startup_session.hpp"

#include <stellar/engine/native_map_platform.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace stellar::native_startup_ui {

enum class StartupScreen { Entry, ModeSelection, Setup, LoadSlots, Busy, Failure, Development };
enum class StartupOperationOrigin { NewCampaign, SavedCampaign };
enum class StartupIntentKind {
  None, OpenSetup, OpenModeSelection, OpenLoad, OpenSettings, Back, Exit, ReturnToCampaign,
  Create, LoadSelected, CancelOperation, CopySetup, CopyDiagnostics
};
struct StartupIntent {
  StartupIntentKind kind{StartupIntentKind::None};
  bool captured{};
  std::string seed_text, species_id;
  int system_count{}, pre_warp_civilization_count{6}, ancient_civilization_count{1};
  std::filesystem::path save_path;
};
struct StartupLayout {
  float scale{};
  int heading_font{}, body_font{}, small_font{};
  stellar::native_map::UiRect panel, title, subtitle, new_campaign,
      load_campaign, exit, list, back, primary, status, settings,
      return_to_campaign, story_campaign, sandbox_campaign, development;
  [[nodiscard]] static StartupLayout for_viewport(int width,
                                                   int height) noexcept;
};

class NativeStartupWorkspace final {
public:
  using TextMeasurer =
      stellar::native_setup_ui::NativeNewGameWorkspace::TextMeasurer;
  using PortraitProvider =
      stellar::native_setup_ui::NativeNewGameWorkspace::PortraitProvider;

  void set_hover_callback(std::function<void()> callback){setup_.set_hover_callback(callback);hover_feedback_.set_callback(std::move(callback));}
  void set_setup(stellar::native_setup::NativeNewCampaignSetupView);
  // The executable owns the generated build identity; the workspace only
  // presents the value it is given.
  void set_build_label(std::string value) { build_label_ = std::move(value); }
  void set_diagnostics(std::string value) { diagnostics_ = std::move(value); }
  void set_return_to_campaign_available(bool available) noexcept;
  void set_continue_save(std::filesystem::path value) {continue_save_=std::move(value);}
  void show_setup() noexcept;
  void show_entry() noexcept;
  void set_slots(stellar::native_startup::NativeStartupSaveSlots);
  void set_setup_message(std::string message, bool accepted);
  void begin_operation(stellar::native_startup::NativeStartupView,
                       StartupOperationOrigin);
  void set_operation(stellar::native_startup::NativeStartupView);
  void show_failure(std::string message);
  [[nodiscard]] StartupScreen screen() const noexcept { return screen_; }
  [[nodiscard]] bool wants_text_input() const noexcept;
  [[nodiscard]] StartupIntent handle(const stellar::native_map::InputEvent &,
                                     int width, int height,
                                     const TextMeasurer &);
  void render(stellar::native_map::DrawList &, int width, int height,
              const TextMeasurer &, const PortraitProvider * = nullptr) const;
  void render(stellar::native_map::DrawList &, int width, int height,
              const TextMeasurer &, const PortraitProvider *,
              const StartupArtworkProvider *, bool backdrop_only = false) const;

private:
  stellar::native_menu_audio::HoverFeedback hover_feedback_;
  void reset_pointer() noexcept;
  StartupScreen screen_{StartupScreen::Entry};
  stellar::native_setup_ui::NativeNewGameWorkspace setup_;
  stellar::native_startup::NativeStartupSaveSlots slots_;
  std::optional<std::size_t> selected_slot_;
  float load_scroll_{};
  stellar::native_startup::NativeStartupView operation_;
  StartupArtworkKind busy_artwork_{StartupArtworkKind::NewGalaxyGeneration};
  std::string loading_tip_;
  int last_loading_tip_{-1};
  std::string failure_;
  std::string build_label_;
  std::string diagnostics_;
  std::filesystem::path continue_save_;
  stellar::native_map::Point pointer_{};
  bool return_to_campaign_available_{};
};
} // namespace stellar::native_startup_ui
