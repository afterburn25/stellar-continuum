#pragma once

#include <stellar/engine/native_map_platform.hpp>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace stellar::native_general {
// Application preference only. Empty means the platform Pictures default.
struct GeneralPreferences final {
  std::filesystem::path screenshot_directory;
  bool operator==(const GeneralPreferences&) const = default;
};
struct GeneralSettingsLayout final {
  float scale{};
  int font_pixels{}, heading_pixels{};
  stellar::native_map::UiRect panel, audio, video, folder, status;
  stellar::native_map::UiRect browse, defaults, cancel, save;
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
  void set_apply(Apply apply) { apply_=std::move(apply); }
  void set_browse(Browse browse) { browse_=std::move(browse); }
  void set_default_directory(std::filesystem::path value) { default_directory_=std::move(value); }
  void set_text_measurer(Measure measure) { measure_=std::move(measure); }
  void set_navigation(Navigate audio,Navigate video) { audio_=std::move(audio);video_=std::move(video); }
  void open();
  void cancel();
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  [[nodiscard]] bool browsing() const noexcept { return pending_request_.has_value(); }
  [[nodiscard]] bool handle(const stellar::native_map::InputEvent&,int,int);
  void render(stellar::native_map::DrawList&,int,int) const;
  // Owner thread only. Results from a dismissed/reopened view are discarded.
  void accept_browse_result(stellar::native_map::FolderDialogResult);
 private:
  [[nodiscard]] stellar::native_map::Text path_text(const GeneralSettingsLayout&) const;
  std::filesystem::path path_,default_directory_;
  GeneralPreferences saved_,draft_;
  std::string error_;
  Browse browse_;
  Apply apply_;
  Navigate audio_,video_;
  Measure measure_;
  mutable std::string cached_path_source_,cached_path_lines_;
  mutable float cached_path_width_{};
  mutable int cached_path_font_{};
  float path_scroll_{};
  std::uint64_t next_request_{};
  std::optional<std::uint64_t> pending_request_;
  bool visible_{};
};
} // namespace stellar::native_general
