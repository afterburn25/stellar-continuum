#pragma once
#include "native_video_settings.hpp"
#include <chrono>
#include <functional>
#include <optional>

namespace stellar::native_video_settings {
// App-owned across startup, load and live campaign replacement. Core never
// sees display preferences. Apply may partially change the backend then throw;
// every failed preview/commit therefore explicitly restores prior preferences.
class NativeVideoController final {
public:
  using Clock = std::chrono::steady_clock;
  using Apply = std::function<void(const NativeVideoSettings&)>;
  using Now = std::function<Clock::time_point()>;
  using Persist = std::function<void(const NativeVideoSettings&)>;
  NativeVideoController(std::filesystem::path, Apply, Now = Clock::now, Persist = {});
  ~NativeVideoController();
  NativeVideoController(const NativeVideoController&) = delete;
  NativeVideoController& operator=(const NativeVideoController&) = delete;
  void open();
  void close();
  void service(bool focused=true,bool renderable=true);
  bool handle(const stellar::native_map::InputEvent&,int width,int height);
  void render(stellar::native_map::DrawList&,int width,int height) const;
  void set_display_choices(std::vector<VideoDisplayChoice> choices,std::string label);
  [[nodiscard]] bool visible()const noexcept{return view_.visible();}
  [[nodiscard]] bool previewing()const noexcept{return previous_.has_value();}
  [[nodiscard]] bool backend_state_known() const noexcept{return !faulted_;}
  [[nodiscard]] const NativeVideoSettings& active()const noexcept{return active_;}
  [[nodiscard]] const std::string& notice()const noexcept{return notice_;}
  [[nodiscard]] double remaining_seconds()const;
private:
  void restore(std::string reason);
  void recover(std::string reason);
  void show_error(std::string reason);
  std::filesystem::path path_;
  Apply apply_;Now now_;Persist persist_;
  NativeVideoSettings active_;
  NativeVideoSettingsView view_;
  std::optional<NativeVideoSettings> previous_;
  Clock::time_point deadline_;
  std::string notice_;
  bool faulted_{};
};
}
