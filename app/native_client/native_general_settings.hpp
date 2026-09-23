#pragma once
#include "native_menu_hover.hpp"
#include "native_dropdown.hpp"

#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/localization.hpp>
#include <filesystem>
#include <array>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace stellar::native_general {
// Application preference only. Empty means the platform Pictures default.
struct GeneralPreferences final {
  std::filesystem::path screenshot_directory;
  std::array<bool,5> asset_categories_collapsed{false,true,false,true,false};
  bool assets_hidden{};
  int eruption_quality{2}; // Low / Medium / High / Ultra; rendering only.
  int nebula_density{1}; // Low / Medium / High; presentation only.
  // Accessibility: pauses decorative motion (system tumble, planet spin,
  // eruption animation) without touching simulation or authoritative clocks.
  bool reduce_motion{};
  // Accessibility: interface scale preset 0=Compact,1=Standard,2=Large,3=Huge.
  // Applied as a user multiplier on top of the viewport-derived UI scale.
  int interface_scale{1};
  bool operator==(const GeneralPreferences&) const = default;
};
// Presentation multiplier each interface_scale preset contributes to UI
// layout scale. Kept inside the engine accessibility clamp (0.75..2.0).
[[nodiscard]] inline float interface_scale_multiplier(int preset) noexcept {
  switch(preset) {
    case 0: return .85f;
    case 2: return 1.2f;
    case 3: return 1.45f;
    default: return 1.f;
  }
}
struct GeneralSettingsLayout final {
  float scale{};
  int font_pixels{}, heading_pixels{};
  stellar::native_map::UiRect panel, audio, video, folder, status;
  stellar::native_map::UiRect browse, defaults, cancel, save;
  stellar::native_map::UiRect nebula,eruptions;
  stellar::native_map::UiRect motion,iscale;
  [[nodiscard]] static GeneralSettingsLayout for_viewport(int width,int height) noexcept;
};
class NativeGeneralSettings final {
 public:
  using Browse=std::function<bool(std::uint64_t,const std::filesystem::path&)>;
  using Apply=std::function<void(const GeneralPreferences&)>;
  using Navigate=std::function<void()>;
  using Measure=std::function<stellar::native_map::TextExtent(const stellar::native_map::Text&)>;
  explicit NativeGeneralSettings(std::filesystem::path path);
  [[nodiscard]] const GeneralPreferences& saved() const noexcept { return saved_; }
  [[nodiscard]] const GeneralPreferences& draft() const noexcept { return draft_; }
  [[nodiscard]] std::string error() const { return error_; }
  // Persistence failure leaves the previous preference and destination intact.
  [[nodiscard]] bool save(GeneralPreferences);
  void set_hover_callback(std::function<void()> callback){hover_feedback_.set_callback(std::move(callback));}
  void set_apply(Apply apply) { apply_=std::move(apply); }
  void set_browse(Browse browse) { browse_=std::move(browse); }
  void set_default_directory(std::filesystem::path value) { default_directory_=std::move(value); }
  void set_text_measurer(Measure measure) { measure_=std::move(measure); }
  void set_navigation(Navigate audio,Navigate video) { audio_=std::move(audio);video_=std::move(video); }
  // Borrowed; the owner must outlive this view. Null keeps literal English.
  void set_localization(const stellar::engine::LocalizationTable* table){locale_=table;}
  void open();
  void cancel();
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  [[nodiscard]] bool browsing() const noexcept { return pending_request_.has_value(); }
  [[nodiscard]] bool handle(const stellar::native_map::InputEvent&,int,int);
  void render(stellar::native_map::DrawList&,int,int) const;
  // Owner thread only. Results from a dismissed/reopened view are discarded.
  void accept_browse_result(stellar::native_map::FolderDialogResult);
 private:
  stellar::native_menu_audio::HoverFeedback hover_feedback_;
  stellar::native_ui::Dropdown nebula_dropdown_,eruption_dropdown_;
  [[nodiscard]] stellar::native_map::Text path_text(const GeneralSettingsLayout&) const;
  [[nodiscard]] std::string tr(std::string_view key,std::string_view fallback)const;
  [[nodiscard]] std::string trf(std::string_view key,std::initializer_list<std::string> args,std::string_view fallback)const;
  std::filesystem::path path_,default_directory_;
  GeneralPreferences saved_,draft_;
  std::string error_;
  Browse browse_;
  Apply apply_;
  Navigate audio_,video_;
  Measure measure_;
  const stellar::engine::LocalizationTable* locale_{};
  mutable std::string cached_path_source_,cached_path_lines_;
  mutable float cached_path_width_{};
  mutable int cached_path_font_{};
  float path_scroll_{};
  std::uint64_t next_request_{};
  std::optional<std::uint64_t> pending_request_;
  bool visible_{};
};
} // namespace stellar::native_general
