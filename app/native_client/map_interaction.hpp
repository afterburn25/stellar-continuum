#pragma once
#include <stellar/engine/native_map_platform.hpp>
#include <cmath>
namespace stellar::native_map {
class PointerGesture final {
 public:
  void begin(bool captured_by_ui) noexcept { captured_=captured_by_ui; travel_=0.f; active_=true; }
  void capture_for_ui() noexcept { if(active_)captured_=true; }
  void move(Point delta) noexcept { if(active_)travel_+=std::hypot(delta.x,delta.y); }
  [[nodiscard]] bool allows_world_drag() const noexcept { return active_&&!captured_; }
  [[nodiscard]] bool captured_by_ui() const noexcept { return active_&&captured_; }
  [[nodiscard]] bool release_as_world_click() noexcept { const bool click=active_&&!captured_&&travel_<3.f; active_=false;captured_=false;travel_=0.f;return click; }
  void cancel() noexcept { active_=false;captured_=false;travel_=0.f; }
 private:
  float travel_{}; bool captured_{},active_{};
};
} // namespace stellar::native_map
